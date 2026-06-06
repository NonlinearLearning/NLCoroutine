export module async_simple.coro.collect;

import std;
import async_simple.common;
import async_simple.executor;
import async_simple.signal;
import async_simple.coro.lazy;
import async_simple.coro.lazy_local;

export namespace async_simple::coro {

// `CollectAnyResult<T>` 表示：
// "一组任务里，最先完成的那个任务的结果"。
//
// 它不仅保存值本身，还保存"这个值来自第几个任务"。
template <typename T>
struct CollectAnyResult {
    std::size_t _idx = static_cast<std::size_t>(-1);
    Result<T> _value;

    std::size_t index() const { return _idx; }
    bool hasError() const { return _value.hasError(); }
    std::exception_ptr getException() const { return _value.getException(); }
    T&& value() && { return std::move(_value).value(); }
};

template <>
struct CollectAnyResult<void> {
    std::size_t _idx = static_cast<std::size_t>(-1);
    Result<void> _value;

    std::size_t index() const { return _idx; }
    bool hasError() const { return _value.hasError(); }
    std::exception_ptr getException() const { return _value.getException(); }
    void value() const { _value.value(); }
};

namespace detail {

// 这两个 `startTask()` 是为了统一启动：
// - `Lazy<T>`
// - `RescheduleLazy<T>`
//
// 两者最大的区别是：
// - 普通 `Lazy` 默认没有执行器，可以按需补绑
// - `RescheduleLazy` 天生就代表"要经执行器调度"
template <typename T, typename F>
void startTask(Lazy<T>&& lazy, Executor* executor,
               const std::shared_ptr<LazyLocalBase>& local, F&& callback) {
    auto prepared = local ? std::move(lazy).setLazyLocal(local) : std::move(lazy);
    if (prepared.getExecutor() == nullptr && executor != nullptr) {
        std::move(prepared).via(executor).start(std::forward<F>(callback));
    } else {
        std::move(prepared).start(std::forward<F>(callback));
    }
}

template <typename T, typename F>
void startTask(RescheduleLazy<T>&& lazy, Executor*,
               const std::shared_ptr<LazyLocalBase>& local, F&& callback) {
    auto prepared = local ? std::move(lazy).setLazyLocal(local) : std::move(lazy);
    std::move(prepared).start(std::forward<F>(callback));
}

template <typename LazyType>
using lazy_value_t = typename detail::lazy_value<std::decay_t<LazyType>>::type;

// `CollectAllAwaiter` 是 `collectAll`/`collectAllPara` 背后的 awaiter。
// 它的职责是：
// 1. 启动一组子任务
// 2. 收集每个子任务的 `Result`
// 3. 等最后一个子任务结束后，恢复外层等待它的协程
template <typename LazyType>
struct CollectAllAwaiter {
    using ValueType = lazy_value_t<LazyType>;

    struct SharedState {
        // 多个子任务完成时会并发写结果，所以这里需要互斥锁。
        std::mutex mutex;
        // 按输入顺序保存每个子任务的完成结果。
        std::vector<Result<ValueType>> results;

        // `remaining` 用原子计数器记录"还有多少子任务没结束"。
        // `std::atomic` 可以让多个线程安全地同时读写这个计数。
        std::atomic<std::size_t> remaining = 0;
        // 最后一个子任务结束时需要恢复的外层协程。
        std::coroutine_handle<> continuation;
    };

    explicit CollectAllAwaiter(std::vector<LazyType>&& input, bool parallel)
        : _input(std::move(input)), _parallel(parallel) {}

    bool await_ready() const noexcept { return _input.empty(); }

    bool await_suspend(std::coroutine_handle<> continuation) {
        auto promise =
            std::coroutine_handle<detail::LazyPromiseBase>::from_address(continuation.address())
                .promise();

        // 为整次 collect 创建一份共享状态。
        // 为什么用 `shared_ptr`？
        // 因为所有子任务的回调都要共同持有它，直到最后一个回调执行完。
        _state = std::make_shared<SharedState>();
        _state->results.resize(_input.size());
        _state->remaining.store(_input.size(), std::memory_order_release);
        _state->continuation = continuation;

        for (std::size_t index = 0; index < _input.size(); ++index) {
            std::shared_ptr<LazyLocalBase> local;
            if (promise._lazyLocalOwner) {
                // 这里新建一层 local，不是为了替代父上下文，
                // 而是为了让 collect 内部也能有自己的附加状态，同时仍能向上回溯。
                local = std::make_shared<LazyLocalBase>();
                local->setParent(promise._lazyLocalOwner);
            }
            auto callback = [state = _state, index](Result<ValueType> result) mutable {
                {
                    std::lock_guard lock(state->mutex);
                    state->results[index] = std::move(result);
                }
                // 最后一个任务结束时，恢复外层协程。
                if (state->remaining.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                    state->continuation.resume();
                }
            };
            if (_parallel) {
                detail::startTask(std::move(_input[index]), promise._executor, local,
                                  std::move(callback));
            } else {
                detail::startTask(std::move(_input[index]), nullptr, local, std::move(callback));
            }
        }
        return true;
    }

    auto await_resume() {
        return std::move(_state->results);
    }

    std::vector<LazyType> _input;
    bool _parallel = false;
    // 所有子任务共享的收集状态。
    std::shared_ptr<SharedState> _state;
};

// `CollectAnyAwaiter` 的目标和上面不同：
// - 它不等全部完成
// - 只要第一个结果出现，就立刻恢复外层协程
// - 同时给剩余兄弟任务发 `Terminate` 信号
template <typename LazyType>
struct CollectAnyAwaiter {
    using ValueType = lazy_value_t<LazyType>;

    struct SharedState {
        // 保护 `result` 的首个写入。
        std::mutex mutex;

        // 这份 signal 会共享给所有子任务。
        // 第一个任务完成后，向这份 signal 发 `Terminate`，
        // 其余任务就可以在自己的 await 点感知到"应该停止了"。
        std::shared_ptr<Signal> signal = Signal::create();
        // 第一个完成任务的结果和来源下标。
        std::optional<CollectAnyResult<ValueType>> result;
        // 保证外层协程只会被恢复一次。
        std::atomic<bool> resumed = false;
        // 第一个任务完成后要恢复的外层协程。
        std::coroutine_handle<> continuation;
    };

    explicit CollectAnyAwaiter(std::vector<LazyType>&& input) : _input(std::move(input)) {}

    bool await_ready() const noexcept { return _input.empty(); }

    bool await_suspend(std::coroutine_handle<> continuation) {
        auto promise = std::coroutine_handle<detail::LazyPromiseBase>::from_address(continuation.address()).promise();

        _state = std::make_shared<SharedState>();
        _state->continuation = continuation;

        for (std::size_t index = 0; index < _input.size(); ++index) {
            // 每个子任务都会得到一个挂在同一 signal 上的 slot。
            auto local = std::make_shared<LazyLocalBase>(_state->signal.get(), SignalType::All);
            if (promise._lazyLocalOwner) {
                local->setParent(promise._lazyLocalOwner);
            }
            auto callback = [state = _state, index](Result<ValueType> result) mutable {
                bool shouldResume = false;
                {
                    std::lock_guard lock(state->mutex);
                    if (!state->result.has_value()) {
                        CollectAnyResult<ValueType> any;
                        any._idx = index;
                        any._value = std::move(result);
                        state->result = std::move(any);
                        shouldResume = true;
                    }
                }
                if (shouldResume) {
                    state->signal->emits(Terminate);

                    // `resumed` 这个原子布尔值用于保证：
                    // 无论多少任务几乎同时完成，外层协程都只恢复一次。
                    if (!state->resumed.exchange(true, std::memory_order_acq_rel)) {
                        state->continuation.resume();
                    }
                }
            };
            detail::startTask(std::move(_input[index]), promise._executor, local,
                              std::move(callback));
        }
        return true;
    }

    auto await_resume() {
        if (_state->result.has_value()) {
            return std::move(*_state->result);
        }
        return CollectAnyResult<ValueType>{};
    }

    std::vector<LazyType> _input;
    // 所有竞争子任务共享的状态。
    std::shared_ptr<SharedState> _state;
};

}  // namespace detail

template <typename LazyType>
auto collectAll(std::vector<LazyType>&& input)
    -> Lazy<std::vector<Result<typename detail::lazy_value<LazyType>::type>>> {
    co_return co_await detail::CollectAllAwaiter<LazyType>(std::move(input), false);
}

template <typename LazyType>
auto collectAllPara(std::vector<LazyType>&& input)
    -> Lazy<std::vector<Result<typename detail::lazy_value<LazyType>::type>>> {
    co_return co_await detail::CollectAllAwaiter<LazyType>(std::move(input), true);
}

template <typename LazyType>
auto collectAny(std::vector<LazyType>&& input)
    -> Lazy<CollectAnyResult<typename detail::lazy_value<LazyType>::type>> {
    co_return co_await detail::CollectAnyAwaiter<LazyType>(std::move(input));
}

template <typename LazyType>
auto collectAllWindowed(std::size_t maxConcurrency, bool yieldBetweenBatches,
                        std::vector<LazyType>&& input)
    -> Lazy<std::vector<Result<typename detail::lazy_value<LazyType>::type>>> {
    using ValueType = typename detail::lazy_value<LazyType>::type;
    std::vector<Result<ValueType>> output;
    output.reserve(input.size());

    // "窗口化执行"的意思是：
    // 不一次把全部任务都启动，而是每次只启动 `maxConcurrency` 个。
    //
    // 这在任务很多时能限制同时在跑的任务数量，避免资源一下子打满。
    std::size_t index = 0;
    while (index < input.size()) {
        std::vector<LazyType> batch;
        auto end = std::min(input.size(), index + maxConcurrency);
        batch.reserve(end - index);
        for (; index < end; ++index) {
            batch.push_back(std::move(input[index]));
        }

        auto batchResults = co_await collectAllPara(std::move(batch));
        for (auto& result : batchResults) {
            output.push_back(std::move(result));
        }

        if (yieldBetweenBatches && index < input.size()) {
            // 如果用户要求批次之间主动让出执行权，就做一次 `Yield`。
            co_await Yield{};
        }
    }

    co_return output;
}

}  // namespace async_simple::coro
