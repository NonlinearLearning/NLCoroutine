export module async_simple.uthread.await;

import std;
import async_simple.common;
import async_simple.executor;
import async_simple.future;
import async_simple.try_type;
import async_simple.coro.lazy;
import async_simple.uthread.uthread;
import async_simple.uthread.runtime;

export namespace async_simple::uthread {

template <class T>
T await(Future<T>&& future) {
    logicAssert(kUthreadAvailable, "uthread await is not supported on this platform");
    logicAssert(future.valid(), "Future is broken");
    if (future.hasResult()) {
        return std::move(future).value();
    }

    auto current = detail::currentContext();
    logicAssert(current != nullptr, "uthread await must run inside a uthread");
    Executor* executor = future.getExecutor();
    logicAssert(executor != nullptr, "Future not has Executor");
    logicAssert(executor->currentThreadInExecutor(), "await invoked not in Executor");
    Promise<T> promise;
    auto awaited = promise.getFuture().via(executor);
    promise.forceSched().checkout();

    awaited.setContinuation([current](auto&&) {
        detail::resumeFiber(current);
    });

    std::move(future).thenTry([promise = std::move(promise)](Try<T>&& value) mutable {
        promise.setValue(std::move(value));
    });

    while (!awaited.hasResult()) {
        detail::suspendCurrent();
    }
    return std::move(awaited).value();
}

template <class Fn, class... Args>
decltype(auto) await(Executor* ex, Fn&& fn, Args&&... args)
    requires std::is_invocable_v<Fn&&, Args&&...>
{
    using ValueType = typename std::invoke_result_t<Fn&&, Args&&...>::ValueType;
    Promise<ValueType> promise;
    auto future = promise.getFuture().via(ex);
    auto lazyLauncher =
        [promise = std::move(promise)]<typename... Ts>(Ts&&... values) mutable -> coro::Lazy<void> {
            if constexpr (std::is_void_v<ValueType>) {
                co_await std::invoke(std::forward<Ts>(values)...);
                promise.setValue();
            } else {
                promise.setValue(co_await std::invoke(std::forward<Ts>(values)...));
            }
            co_return;
        };
    lazyLauncher(std::forward<Fn>(fn), std::forward<Args>(args)...)
        .directlyStart([](auto&&) {}, ex);
    return await(std::move(future));
}

template <class T, class Fn>
T await(Executor* ex, Fn&& fn) {
    static_assert(
        std::is_invocable_v<Fn, Promise<T>>,
        "Callable of await is not support, eg: Callable(Promise<T>)");
    Promise<T> promise;
    auto future = promise.getFuture().via(ex);
    if (ex != nullptr) {
        promise.forceSched().checkout();
    }
    std::forward<Fn>(fn)(std::move(promise));
    return await(std::move(future));
}

}  // namespace async_simple::uthread
