export module async_simple.coro.future_awaiter;

import std;
import async_simple.executor;
import async_simple.future;
import async_simple.try_type;
import async_simple.experimental.coroutine;
import async_simple.coro.lazy;

export namespace async_simple::coro::detail {

template <typename T>
struct FutureAwaiter {
    Future<T> future_;

    bool await_ready() {
        return future_.hasResult();
    }

    template <typename PromiseType>
    bool await_suspend(CoroHandle<PromiseType> continuation) {
        static_assert(
            std::is_base_of_v<LazyPromiseBase, PromiseType>,
            "FutureAwaiter is only allowed to be called by Lazy");

        Executor* executor = continuation.promise()._executor;
        Executor::Context context = Executor::NULLCTX;
        if (executor != nullptr) {
            context = executor->checkout();
        }

        future_.setContinuation([continuation, executor, context](Try<T>&&) mutable {
            if (executor != nullptr) {
                executor->checkin(continuation, context);
            } else {
                continuation.resume();
            }
        });
        return true;
    }

    decltype(auto) await_resume() {
        if constexpr (std::is_void_v<T>) {
            std::move(future_).value();
        } else {
            return std::move(future_).value();
        }
    }
};

}  // namespace async_simple::coro::detail

export namespace async_simple {

template <typename T>
auto operator co_await(Future<T>&& future) {
    return coro::detail::FutureAwaiter<T>{std::move(future)};
}

template <typename T>
[[deprecated("Require an rvalue future.")]]
auto operator co_await(T&& future)
    requires IsFuture<std::decay_t<T>>::value
{
    return operator co_await(std::move(future));
}

}  // namespace async_simple
