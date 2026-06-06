export module async_simple.uthread.async;

import std;
import async_simple.common;
import async_simple.executor;
import async_simple.future;
import async_simple.uthread.uthread;

export namespace async_simple::uthread {

enum class Launch {
    Prompt,
    Schedule,
    Current,
};

template <Launch policy, class F>
requires(policy == Launch::Prompt)
Uthread async(F&& f, Executor* ex) {
    logicAssert(kUthreadAvailable, "uthread async is not supported on this platform");
    return Uthread(Attribute{ex}, std::forward<F>(f));
}

template <Launch policy, class F>
requires(policy == Launch::Schedule)
void async(F&& f, Executor* ex) {
    logicAssert(kUthreadAvailable, "uthread async is not supported on this platform");
    if (ex == nullptr) {
        return;
    }
    ex->schedule([func = std::forward<F>(f), ex]() mutable {
        Uthread uth(Attribute{ex}, std::move(func));
        uth.detach();
    });
}

template <Launch policy, class F, class C>
requires(policy == Launch::Schedule)
void async(F&& f, C&& c, Executor* ex) {
    logicAssert(kUthreadAvailable, "uthread async is not supported on this platform");
    if (ex == nullptr) {
        return;
    }
    ex->schedule([func = std::forward<F>(f), callback = std::forward<C>(c), ex]() mutable {
        Uthread uth(Attribute{ex}, std::move(func));
        uth.join(std::move(callback));
    });
}

template <Launch policy, class F>
requires(policy == Launch::Current)
void async(F&& f, Executor* ex) {
    logicAssert(kUthreadAvailable, "uthread async is not supported on this platform");
    Uthread uth(Attribute{ex}, std::forward<F>(f));
    uth.detach();
}

template <class F, class... Args, typename R = std::invoke_result_t<F&&, Args&&...>>
Future<R> async(Launch policy, Attribute attr, F&& f, Args&&... args) {
    logicAssert(kUthreadAvailable, "uthread async is not supported on this platform");
    if (policy == Launch::Schedule) {
        logicAssert(attr.ex != nullptr, "Schedule launch policy requires an executor");
    }

    Promise<R> promise;
    auto result = promise.getFuture().via(attr.ex);
    auto process = [promise = std::move(promise), ex = attr.ex, func = std::forward<F>(f),
                    argsTuple = std::make_tuple(std::forward<Args>(args)...)]() mutable {
        if (ex != nullptr) {
            promise.forceSched().checkout();
        }
        if constexpr (std::is_void_v<R>) {
            std::apply(func, std::move(argsTuple));
            promise.setValue();
        } else {
            promise.setValue(std::apply(func, std::move(argsTuple)));
        }
    };

    if (policy == Launch::Schedule) {
        attr.ex->schedule([process = std::move(process), attr]() mutable {
            Uthread(attr, std::move(process)).detach();
        });
    } else if (policy == Launch::Current) {
        Uthread(attr, std::move(process)).detach();
    } else {
        logicAssert(false, "Prompt launch policy is not supported for Future-returning async");
    }

    return result;
}

}  // namespace async_simple::uthread
