export module async_simple.coro.lazy;

import std;
import async_simple.common;
import async_simple.executor;
import async_simple.signal;
import async_simple.coro.detached_coroutine;
import async_simple.experimental.coroutine;
export import async_simple.coro.lazy_local;

export namespace async_simple::coro {

template <typename T>
class Lazy;

template <typename T>
class RescheduleLazy;

// `Yield` 是这个框架自定义的 awaitable 入口，语义是：
// “当前协程愿意先挂起，再交回执行器安排后续恢复”。
// 它不是语言关键字，而是建立在 `await_transform` 之上的运行时扩展。
struct Yield {};

// 查询当前协程链上可见的 local。
// local 不挂在线程上，而是挂在 promise 上随协程链传播。
template <typename T = LazyLocalBase>
struct CurrentLazyLocals {};

// 查询当前协程 local 中携带的取消槽位。
struct CurrentSlot {};

// 阻断当前协程继续继承 signal，但保留其他 local 上下文。
struct ForbidSignal {};

// `Result<T>` 是协程完成结果的最小包装：
// - 正常完成时保存值
// - 异常完成时保存异常指针
//
// 这里故意保持结构简单，方便把注意力集中到协程控制流而不是结果类型体操上。
template <typename T>
struct Result {
    // 成功路径上的返回值；异常路径时通常为空。
    std::optional<T> value_;
    // 失败路径上捕获到的异常。
    std::exception_ptr error_;

    bool hasError() const noexcept { return error_ != nullptr; }
    std::exception_ptr getException() const noexcept { return error_; }

    T& value() & {
        if (error_) {
            std::rethrow_exception(error_);
        }
        return *value_;
    }

    const T& value() const& {
        if (error_) {
            std::rethrow_exception(error_);
        }
        return *value_;
    }

    T&& value() && {
        if (error_) {
            std::rethrow_exception(error_);
        }
        return std::move(*value_);
    }
};

template <>
struct Result<void> {
    // `void` 结果只有异常态，没有值槽位。
    std::exception_ptr error_;

    bool hasError() const noexcept { return error_ != nullptr; }
    std::exception_ptr getException() const noexcept { return error_; }

    void value() const {
        if (error_) {
            std::rethrow_exception(error_);
        }
    }
};

namespace detail {

template <typename T>
concept DerivedFromLazyLocal = std::is_base_of_v<LazyLocalBase, T>;

// `promise_type` 是协程帧对外暴露的运行时接口；编译器会围绕它生成协程状态机。
// 这个项目把与返回值无关的部分收敛到 `LazyPromiseBase`：
// - continuation：当前帧结束后恢复谁
// - executor：恢复动作准备交给谁
// - lazyLocal：这条协程链携带的上下文
//
// 可以把它理解成“教学版协程帧头部”。
class LazyPromiseBase {
public:
    // final suspend 的核心职责只有一个：把控制流交还给 continuation。
    struct FinalAwaiter {
        bool await_ready() const noexcept { return false; }

        template <typename PromiseType>
        std::coroutine_handle<> await_suspend(std::coroutine_handle<PromiseType> handle) noexcept {
            auto continuation = handle.promise()._continuation;
            return continuation ? continuation : std::noop_coroutine();
        }

        void await_resume() const noexcept {}
    };

    // `Yield` 最终会被 promise 改写成这个 awaiter。
    struct YieldAwaiter {
        // 当前协程恢复时要重新投递到的执行器。
        Executor* executor;
        // 当前协程关联的取消信号槽位。
        Slot* slot;

        bool await_ready() const noexcept {
            return signalHelper{Terminate}.hasCanceled(slot);
        }

        template <typename PromiseType>
        void await_suspend(std::coroutine_handle<PromiseType> handle) {
            logicAssert(executor != nullptr, "Yield requires a bound executor");
            // 不立刻恢复当前帧，而是显式重新投递给执行器。
            executor->schedule(handle, static_cast<std::uint64_t>(Executor::Priority::YIELD));
        }

        void await_resume() const {
            signalHelper{Terminate}.checkHasCanceled(slot, "Yield canceled");
        }
    };

    LazyPromiseBase() noexcept = default;

    // `Lazy` 的惰性来自这里：调用协程函数只创建协程帧，不立即执行函数体。
    std::suspend_always initial_suspend() noexcept { return {}; }
    FinalAwaiter final_suspend() noexcept { return {}; }

    // 默认不篡改 awaitable；只有框架自定义入口才在下面做 await_transform。
    template <typename Awaitable>
    decltype(auto) await_transform(Awaitable&& awaitable) {
        return std::forward<Awaitable>(awaitable);
    }

    auto await_transform(CurrentExecutor) {
        return ReadyAwaiter<Executor*>(_executor);
    }

    template <typename TLocal>
    auto await_transform(CurrentLazyLocals<TLocal>) {
        return ReadyAwaiter<TLocal*>(_lazyLocal ? dynamicCast<TLocal>(_lazyLocal) : nullptr);
    }

    auto await_transform(CurrentSlot) {
        return ReadyAwaiter<Slot*>(_lazyLocal ? _lazyLocal->getSlot() : nullptr);
    }

    auto await_transform(ForbidSignal) {
        if (_lazyLocal) {
            _lazyLocal->forbidSignal();
        }
        return ReadyAwaiter<void>();
    }

    auto await_transform(Yield) {
        return YieldAwaiter{_executor, _lazyLocal ? _lazyLocal->getSlot() : nullptr};
    }

    // 当前帧结束后需要恢复的上层协程。
    std::coroutine_handle<> _continuation = nullptr;
    // 当前帧恢复时要交给哪个执行器；`via()` 或父协程继承会写入这里。
    Executor* _executor = nullptr;
    // 当前协程链可见的 local 原始指针，便于快速读取。
    LazyLocalBase* _lazyLocal = nullptr;
    // 持有 local 生命周期。
    std::shared_ptr<LazyLocalBase> _lazyLocalOwner;
};

template <typename T>
class LazyPromise : public LazyPromiseBase {
public:
    Lazy<T> get_return_object() noexcept;

    // `co_return value` 最终会落到这里，把结果写进协程帧。
    template <typename V>
    void return_value(V&& value) noexcept(std::is_nothrow_constructible_v<T, V&&>) {
        _value = std::forward<V>(value);
    }

    // 未捕获异常不会立刻跨栈抛回去，而是先存进协程帧，等 await_resume/取结果时再传播。
    void unhandled_exception() noexcept {
        _error = std::current_exception();
    }

    T result() && {
        if (_error) {
            std::rethrow_exception(_error);
        }
        return std::move(*_value);
    }

    Result<T> resultObject() && {
        Result<T> result;
        result.value_ = std::move(_value);
        result.error_ = _error;
        return result;
    }

private:
    std::optional<T> _value;
    std::exception_ptr _error;
};

template <>
class LazyPromise<void> : public LazyPromiseBase {
public:
    Lazy<void> get_return_object() noexcept;

    // `Lazy<void>` 只有完成态，没有值槽位。
    void return_void() noexcept {}

    void unhandled_exception() noexcept {
        _error = std::current_exception();
    }

    void result() {
        if (_error) {
            std::rethrow_exception(_error);
        }
    }

    Result<void> resultObject() && {
        Result<void> result;
        result.error_ = _error;
        return result;
    }

private:
    std::exception_ptr _error;
};

template <typename T>
struct LazyAwaiterBase {
    using Handle = CoroHandle<LazyPromise<T>>;

    // `_handle` 指向被等待的子协程帧。
    explicit LazyAwaiterBase(Handle handle) : _handle(handle) {}
    LazyAwaiterBase(const LazyAwaiterBase&) = delete;
    LazyAwaiterBase& operator=(const LazyAwaiterBase&) = delete;

    LazyAwaiterBase(LazyAwaiterBase&& other) noexcept
        : _handle(std::exchange(other._handle, nullptr)) {}

    LazyAwaiterBase& operator=(LazyAwaiterBase&& other) noexcept {
        if (this != &other) {
            if (_handle) {
                _handle.destroy();
            }
            _handle = std::exchange(other._handle, nullptr);
        }
        return *this;
    }

    ~LazyAwaiterBase() {
        if (_handle) {
            _handle.destroy();
        }
    }

    // 子协程已经完成时，父协程不需要真的挂起。
    bool await_ready() const noexcept { return !_handle || _handle.done(); }

    T awaitResumeValue() {
        // 结果取出后立即销毁帧，避免完成后的悬空占用。
        auto value = std::move(_handle.promise()).result();
        _handle.destroy();
        _handle = nullptr;
        return value;
    }

    Result<T> awaitResumeResult() {
        auto result = std::move(_handle.promise()).resultObject();
        _handle.destroy();
        _handle = nullptr;
        return result;
    }

    Handle _handle;
};

template <>
struct LazyAwaiterBase<void> {
    using Handle = CoroHandle<LazyPromise<void>>;

    explicit LazyAwaiterBase(Handle handle) : _handle(handle) {}
    LazyAwaiterBase(const LazyAwaiterBase&) = delete;
    LazyAwaiterBase& operator=(const LazyAwaiterBase&) = delete;

    LazyAwaiterBase(LazyAwaiterBase&& other) noexcept
        : _handle(std::exchange(other._handle, nullptr)) {}

    LazyAwaiterBase& operator=(LazyAwaiterBase&& other) noexcept {
        if (this != &other) {
            if (_handle) {
                _handle.destroy();
            }
            _handle = std::exchange(other._handle, nullptr);
        }
        return *this;
    }

    ~LazyAwaiterBase() {
        if (_handle) {
            _handle.destroy();
        }
    }

    bool await_ready() const noexcept { return !_handle || _handle.done(); }

    void awaitResumeValue() {
        std::move(_handle.promise()).result();
        _handle.destroy();
        _handle = nullptr;
    }

    Result<void> awaitResumeResult() {
        auto result = std::move(_handle.promise()).resultObject();
        _handle.destroy();
        _handle = nullptr;
        return result;
    }

    Handle _handle;
};

template <typename T, bool Reschedule>
class LazyBase {
public:
    using promise_type = LazyPromise<T>;
    using Handle = CoroHandle<promise_type>;
    using ValueType = T;

    struct AwaiterBase : public LazyAwaiterBase<T> {
        using Base = LazyAwaiterBase<T>;

        explicit AwaiterBase(Handle handle) : Base(handle) {}

        template <typename PromiseType>
        auto await_suspend(std::coroutine_handle<PromiseType> continuation) {
            // 父协程挂起前，把 continuation 记录到子协程帧里。
            this->_handle.promise()._continuation = continuation;
            if constexpr (requires { continuation.promise()._executor; }) {
                // 执行器和 local 都沿着 co_await 链向下继承。
                if (this->_handle.promise()._executor == nullptr) {
                    this->_handle.promise()._executor = continuation.promise()._executor;
                }
                if (this->_handle.promise()._lazyLocal == nullptr) {
                    this->_handle.promise()._lazyLocal = continuation.promise()._lazyLocal;
                    this->_handle.promise()._lazyLocalOwner =
                        continuation.promise()._lazyLocalOwner;
                }
            }
            if constexpr (Reschedule) {
                // `RescheduleLazy` 打断对称转移，显式走一次执行器调度。
                auto* executor = this->_handle.promise()._executor;
                logicAssert(executor != nullptr, "RescheduleLazy requires an executor");
                executor->schedule(this->_handle);
                return true;
            } else {
                // 普通 `Lazy` 保持语言级的直接控制流转移。
                return this->_handle;
            }
        }
    };

    struct ValueAwaiter : public AwaiterBase {
        explicit ValueAwaiter(Handle handle) : AwaiterBase(handle) {}

        T await_resume() {
            return AwaiterBase::Base::awaitResumeValue();
        }
    };

    struct ResultAwaiter : public AwaiterBase {
        explicit ResultAwaiter(Handle handle) : AwaiterBase(handle) {}

        Result<T> await_resume() {
            return AwaiterBase::Base::awaitResumeResult();
        }
    };

    LazyBase() noexcept = default;
    explicit LazyBase(Handle handle) noexcept : _coro(handle) {}

    LazyBase(const LazyBase&) = delete;
    LazyBase& operator=(const LazyBase&) = delete;

    LazyBase(LazyBase&& other) noexcept : _coro(std::exchange(other._coro, nullptr)) {}

    LazyBase& operator=(LazyBase&& other) noexcept {
        if (this != &other) {
            if (_coro) {
                _coro.destroy();
            }
            _coro = std::exchange(other._coro, nullptr);
        }
        return *this;
    }

    ~LazyBase() {
        if (_coro) {
            _coro.destroy();
        }
    }

    Executor* getExecutor() const {
        return _coro ? _coro.promise()._executor : nullptr;
    }

    bool isReady() const {
        return !_coro || _coro.done();
    }

    template <typename F>
    void start(F&& callback) {
        logicAssert(_coro != nullptr, "Lazy has no coroutine state");
        // `start()` 需要一个根协程来接住最终结果，否则单独的 `Lazy` 只有协程帧，
        // 并不会自己开始执行。
        auto launchTask = [](LazyBase self,std::decay_t<F> cb) -> detail::DetachedCoroutine {
            cb(co_await self.coAwaitResult());
        };
        [[maybe_unused]] auto detached =
            launchTask(std::move(*this), std::forward<F>(callback));
    }

    // `co_await lazy` 会把 `Lazy` 转成 awaiter，再接入标准 await 三段式协议。
    auto operator co_await() {
        return ValueAwaiter(std::exchange(_coro, nullptr));
    }

    // 框架内部需要保留“值或异常”两种完成态时走这个版本。
    auto coAwaitResult() {
        return ResultAwaiter(std::exchange(_coro, nullptr));
    }

    // fire-and-forget：启动后不再向调用方返回结果。
    void detach() {
        auto ignoreResult = [](Result<T>) {};
        start(std::move(ignoreResult));
    }

protected:
    template <typename Local>
    void setLazyLocalOwner(std::shared_ptr<Local> local) {
        _coro.promise()._lazyLocalOwner = std::move(local);
        _coro.promise()._lazyLocal = _coro.promise()._lazyLocalOwner.get();
    }

    void bindExecutor(Executor* executor) {
        _coro.promise()._executor = executor;
    }

    Handle release() {
        return std::exchange(_coro, nullptr);
    }

    // 当前 `Lazy` 对应的协程帧句柄。
    Handle _coro = nullptr;
};

template <typename LazyType>
struct lazy_value;

template <typename T>
struct lazy_value<Lazy<T>> {
    using type = T;
};

template <typename T>
struct lazy_value<RescheduleLazy<T>> {
    using type = T;
};

}
template <typename T>
class Lazy : public detail::LazyBase<T, false> {
    using Base = detail::LazyBase<T, false>;

public:
    using typename Base::Handle;
    using Base::Base;

    RescheduleLazy<T> via(Executor* executor) && {
        logicAssert(this->_coro != nullptr, "Lazy has no coroutine state");
        // `.via()` 的本质不是“立刻切线程”，而是把后续恢复路径改成“先经过执行器”。
        this->bindExecutor(executor);
        return RescheduleLazy<T>(this->release());
    }

    template <detail::DerivedFromLazyLocal Local = LazyLocalBase, typename... Args>
    Lazy<T> setLazyLocal(Args&&... args) && {
        logicAssert(this->_coro != nullptr, "Lazy has no coroutine state");
        this->setLazyLocalOwner(std::make_shared<Local>(std::forward<Args>(args)...));
        return Lazy<T>(this->release());
    }

    template <detail::DerivedFromLazyLocal Local>
    Lazy<T> setLazyLocal(std::shared_ptr<Local> local) && {
        logicAssert(this->_coro != nullptr, "Lazy has no coroutine state");
        this->setLazyLocalOwner(std::move(local));
        return Lazy<T>(this->release());
    }

    template <detail::DerivedFromLazyLocal Local>
    Lazy<T> setLazyLocal(std::unique_ptr<Local> local) && {
        logicAssert(this->_coro != nullptr, "Lazy has no coroutine state");
        this->setLazyLocalOwner(std::shared_ptr<Local>(std::move(local)));
        return Lazy<T>(this->release());
    }

    template <typename F>
    void directlyStart(F&& callback, Executor* executor) {
        // 便捷入口：先补齐执行器，再走非阻塞启动。
        this->bindExecutor(executor);
        this->start(std::forward<F>(callback));
    }
};

template <typename T>
class RescheduleLazy : public detail::LazyBase<T, true> {
    using Base = detail::LazyBase<T, true>;

public:
    using typename Base::Handle;
    using Base::Base;

    template <detail::DerivedFromLazyLocal Local = LazyLocalBase, typename... Args>
    RescheduleLazy<T> setLazyLocal(Args&&... args) && {
        logicAssert(this->_coro != nullptr, "RescheduleLazy has no coroutine state");
        this->setLazyLocalOwner(std::make_shared<Local>(std::forward<Args>(args)...));
        return RescheduleLazy<T>(this->release());
    }

    template <detail::DerivedFromLazyLocal Local>
    RescheduleLazy<T> setLazyLocal(std::shared_ptr<Local> local) && {
        logicAssert(this->_coro != nullptr, "RescheduleLazy has no coroutine state");
        this->setLazyLocalOwner(std::move(local));
        return RescheduleLazy<T>(this->release());
    }

    template <detail::DerivedFromLazyLocal Local>
    RescheduleLazy<T> setLazyLocal(std::unique_ptr<Local> local) && {
        logicAssert(this->_coro != nullptr, "RescheduleLazy has no coroutine state");
        this->setLazyLocalOwner(std::shared_ptr<Local>(std::move(local)));
        return RescheduleLazy<T>(this->release());
    }
};

template <typename T>
inline Lazy<T> detail::LazyPromise<T>::get_return_object() noexcept {
    return Lazy<T>(CoroHandle<LazyPromise<T>>::from_promise(*this));
}

inline Lazy<void> detail::LazyPromise<void>::get_return_object() noexcept {
    return Lazy<void>(CoroHandle<LazyPromise<void>>::from_promise(*this));
}

}  // namespace async_simple::coro
