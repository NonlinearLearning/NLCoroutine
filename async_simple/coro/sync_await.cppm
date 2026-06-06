export module async_simple.coro.sync_await;

import std;
import async_simple.common;
import async_simple.executor;
import async_simple.coro.lazy;
import async_simple.util.condition;

export namespace async_simple::coro {

// `syncAwait()` 不是协程机制的一部分，而是“从同步世界进入协程世界”的桥。
// 它做的事情很朴素：
// - 启动一个 `Lazy<T>`
// - 在完成回调里收集结果
// - 让当前线程阻塞到结果就绪
//
// 所以它适合作为 demo、测试或进程入口处的同步封装，
// 不适合放在执行器工作线程内部使用。
template <typename LazyType>
auto syncAwait(LazyType&& lazy) {
    auto executor = lazy.getExecutor();
    if (executor != nullptr) {
        logicAssert(!executor->currentThreadInExecutor(), "Do not syncAwait inside the bound executor thread");
    }

    // `Condition` 只负责把“异步完成”翻译成“当前线程可以继续执行”。
    util::Condition condition;

    using ValueType = typename std::decay_t<LazyType>::ValueType;
    Result<ValueType> result;

    // 这里刻意不直接操作协程句柄，而是沿用 `Lazy::start()` 这条正式启动路径。
    auto onCompleted = [&condition, &result](Result<ValueType> done) mutable {
        result = std::move(done);
        condition.release();
    };

    auto task = std::forward<LazyType>(lazy);
    std::move(task).start(std::move(onCompleted));

    // 当前线程在这里等待，直到协程完成回调释放条件变量。
    condition.acquire();

    if constexpr (std::is_void_v<ValueType>) {
        result.value();
    } else {
        return std::move(result).value();
    }
}

// 便捷重载：先补齐执行器绑定，再复用上面的同步桥接逻辑。
template <typename LazyType>
auto syncAwait(LazyType&& lazy, Executor* executor) {
    return syncAwait(std::move(std::forward<LazyType>(lazy)).via(executor));
}

}  // namespace async_simple::coro
