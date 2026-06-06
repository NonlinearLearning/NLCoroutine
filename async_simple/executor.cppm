export module async_simple.executor;

import std;
import async_simple.experimental.coroutine;
import async_simple.signal;

export namespace async_simple {

// `CurrentExecutor` 不是协程语言自带概念，而是这个教学框架额外暴露的查询点。
// 当协程里 `co_await CurrentExecutor{}` 时，真正返回的是当前 promise 上记录的
// `Executor*`，也就是“这条协程链后续准备交给谁调度恢复”。
struct CurrentExecutor {};

// 这是一个极简统计结构，暂时只记录还有多少任务在等待执行。
// 这里故意做得很小，因为这个项目目前是教学版，不追求完整监控面。
struct ExecutorStat {
    std::size_t pendingTaskCount = 0;
};

struct ScheduleOptions {
    bool prompt = true;
};

// 协程不是框架，也不是现成的异步编程方案。
// C++20 协程只定义“如何挂起/恢复一个协程帧”，并不附带线程池、事件循环或调度器。
// `Executor` 就是这个项目在语言机制之上补出来的运行时接口：
// - 协程帧由编译器和 `promise_type` 维护
// - 何时、何地恢复该帧，由 `Executor` 决定
//
// 这样能把“协程机制”和“调度策略”明确分层：
// - `Lazy`/awaiter 解决控制流连接
// - `Executor` 解决恢复动作的投递位置
class Executor {
public:
    using Context = void*;
    static constexpr Context NULLCTX = nullptr;
    using Duration = std::chrono::duration<std::int64_t, std::micro>;
    // 执行器实际投递的是“未来某个时刻要执行的一段恢复逻辑”。
    using Func = std::function<void()>;

    class TimeAwaitable;
    class TimeAwaiter;

    // 这是一个非常简化的优先级枚举。
    // 枚举值越小，优先级越高。
    //
    // 这里使用 `enum class` 而不是老式 `enum`，因为：
    // 1. 名字不会污染外围作用域
    // 2. 类型更安全，不会随意隐式转换成整数
    enum class Priority {
        HIGHEST = 0x0,
        DEFAULT = 0x7,
        YIELD = 0x8,
        LOWEST = 0xF,
    };

    // `std::move(name)` 的意思不是"真的移动内存块"，
    // 而是把 `name` 标记成"可以把内部资源转交出去"。
    // 对 `std::string` 来说，这通常意味着避免一次不必要的拷贝。
    explicit Executor(std::string name = "default") : _name(std::move(name)) {}
    virtual ~Executor() = default;

    // 禁止拷贝。
    // 执行器通常持有线程池、队列、同步原语等资源，拷贝语义往往没有明确意义。
    Executor(const Executor&) = delete;
    Executor& operator=(const Executor&) = delete;

    // 这是执行器最核心的接口：
    // "请安排这个函数在未来某个时机执行"。
    //
    // 具体是：
    // - 立刻执行
    // - 放入线程池
    // - 切到某个特定线程
    // - 放入事件循环
    //
    // 都由派生类决定。
    virtual bool schedule(Func func) = 0;

    // 带附加调度信息的重载。
    // 现在最小实现里大多忽略这个参数，但保留这个接口可以让 `Yield`
    // 这种"低优先级恢复"拥有扩展点。
    virtual bool schedule(Func func, std::uint64_t) {
        return schedule(std::move(func));
    }

    // `coroutine_handle` 可以看成“恢复某个协程帧的句柄”。
    // 这个重载把语言层的 `handle.resume()` 包装成执行器能理解的普通任务投递。
    template <typename Promise>
    bool schedule(std::coroutine_handle<Promise> handle,
                  std::uint64_t scheduleInfo = static_cast<std::uint64_t>(Priority::DEFAULT)) {
        auto resumeHandle = [handle]() mutable {
            handle.resume();
        };
        return schedule(std::move(resumeHandle), scheduleInfo);
    }

    // `syncAwait` 之类的同步桥接需要知道当前线程是否已经在执行器内部，
    // 否则“等待自己恢复自己”会形成死锁。
    virtual bool currentThreadInExecutor() const {
        throw std::logic_error("currentThreadInExecutor() is not implemented");
    }

    virtual ExecutorStat stat() const { return ExecutorStat{}; }

    virtual std::size_t currentContextId() const { return 0; }

    virtual Context checkout() { return NULLCTX; }

    virtual bool checkin(Func func, Context, ScheduleOptions) {
        return schedule(std::move(func));
    }

    virtual bool checkin(Func func, Context ctx) {
        static ScheduleOptions options;
        return checkin(std::move(func), ctx, options);
    }

    TimeAwaitable after(Duration dur);

    TimeAwaitable after(
        Duration dur,
        std::uint64_t scheduleInfo,
        Slot* slot = nullptr);

    const std::string& name() const { return _name; }

protected:
    virtual void schedule(Func func, Duration dur) {
        schedule(
            std::move(func),
            dur,
            static_cast<std::uint64_t>(Priority::DEFAULT),
            nullptr);
    }

    virtual void schedule(
        Func func,
        Duration dur,
        std::uint64_t,
        Slot* slot = nullptr) {
        std::thread([this, func = std::move(func), dur, slot]() mutable {
            auto promise = std::make_shared<std::promise<void>>();
            auto future = promise->get_future();
            bool hasNotCanceled = signalHelper{Terminate}.tryEmplace(
                slot,
                [p = std::move(promise)](SignalType, Signal*) mutable {
                    p->set_value();
                });
            if (hasNotCanceled) {
                future.wait_for(dur);
            }
            // 定时等待结束后，真正的恢复动作仍然回到执行器统一调度。
            schedule(std::move(func));
        }).detach();
    }

private:
    std::string _name;
};

class Executor::TimeAwaiter {
public:
    TimeAwaiter(
        Executor* executor,
        Executor::Duration duration,
        std::uint64_t scheduleInfo,
        Slot* slot)
        : _executor(executor),
          _duration(duration),
          _scheduleInfo(scheduleInfo),
          _slot(slot) {}

    bool await_ready() const noexcept {
        return signalHelper{Terminate}.hasCanceled(_slot);
    }

    template <typename Promise>
    void await_suspend(std::coroutine_handle<Promise> continuation) {
        // 定时 awaitable 自己不持有线程；它只是把“稍后恢复 continuation”这个请求
        // 交给执行器实现。
        _executor->schedule(
            continuation,
            _duration,
            _scheduleInfo,
            _slot);
    }

    void await_resume() {
        signalHelper{Terminate}.checkHasCanceled(
            _slot,
            "async_simple timer is canceled");
    }

private:
    Executor* _executor;
    Executor::Duration _duration;
    std::uint64_t _scheduleInfo;
    Slot* _slot;
};

class Executor::TimeAwaitable {
public:
    TimeAwaitable(
        Executor* executor,
        Executor::Duration duration,
        std::uint64_t scheduleInfo,
        Slot* slot)
        : _executor(executor),
          _duration(duration),
          _scheduleInfo(scheduleInfo),
          _slot(slot) {}

    auto coAwait(Executor*) {
        return TimeAwaiter(_executor, _duration, _scheduleInfo, _slot);
    }

private:
    Executor* _executor;
    Executor::Duration _duration;
    std::uint64_t _scheduleInfo;
    Slot* _slot;
};

inline Executor::TimeAwaitable Executor::after(Duration dur) {
    return TimeAwaitable(
        this,
        dur,
        static_cast<std::uint64_t>(Priority::DEFAULT),
        nullptr);
}

inline Executor::TimeAwaitable Executor::after(
    Duration dur,
    std::uint64_t scheduleInfo,
    Slot* slot) {
    return TimeAwaitable(this, dur, scheduleInfo, slot);
}

}  // namespace async_simple
