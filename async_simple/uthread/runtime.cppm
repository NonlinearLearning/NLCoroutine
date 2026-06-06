module;

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

export module async_simple.uthread.runtime;

import std;
import async_simple.common;
import async_simple.executor;
import async_simple.future;

export namespace async_simple::uthread::detail {

class FiberContext;

inline thread_local FiberContext* currentFiberContext = nullptr;
inline thread_local void* threadFiber = nullptr;

void* ensureThreadFiber();

class FiberContext {
public:
    FiberContext(Executor* executor, std::size_t stackSize, std::function<void()> func)
        : _executor(executor),
          _func(std::move(func)) {
        _fiber.reset(::CreateFiber(stackSize, &FiberContext::fiberMain, this));
        logicAssert(_fiber != nullptr, "CreateFiber failed");
    }

    void bindSelf(std::shared_ptr<FiberContext> self) {
        _self = std::move(self);
    }

    std::shared_ptr<FiberContext> self() const {
        return _self.lock();
    }

    Executor* executor() const noexcept {
        return _executor;
    }

    Promise<bool>& donePromise() noexcept {
        return _done;
    }

    void resume() {
        ensureThreadFiber();
        _callerFiber = ::GetCurrentFiber();
        ::SwitchToFiber(_fiber.get());
    }

    void suspend() {
        logicAssert(_callerFiber != nullptr, "Fiber has no caller");
        ::SwitchToFiber(_callerFiber);
    }

private:
    struct FiberDeleter {
        void operator()(void* fiber) const noexcept {
            if (fiber != nullptr) {
                ::DeleteFiber(fiber);
            }
        }
    };

    static void __stdcall fiberMain(void* rawContext) {
        static_cast<FiberContext*>(rawContext)->run();
    }

    void run() {
        currentFiberContext = this;
        try {
            _func();
            _done.setValue(true);
        } catch (...) {
            _done.setException(std::current_exception());
        }
        currentFiberContext = nullptr;
        auto callerFiber = _callerFiber;
        _callerFiber = nullptr;
        logicAssert(callerFiber != nullptr, "Fiber finished without caller");
        ::SwitchToFiber(callerFiber);
    }

    Executor* _executor = nullptr;
    std::function<void()> _func;
    std::unique_ptr<void, FiberDeleter> _fiber;
    void* _callerFiber = nullptr;
    Promise<bool> _done;
    std::weak_ptr<FiberContext> _self;
};

inline void* ensureThreadFiber() {
    if (threadFiber != nullptr) {
        return threadFiber;
    }
    if (::IsThreadAFiber()) {
        threadFiber = ::GetCurrentFiber();
    } else {
        threadFiber = ::ConvertThreadToFiber(nullptr);
        logicAssert(threadFiber != nullptr, "ConvertThreadToFiber failed");
    }
    return threadFiber;
}

inline std::shared_ptr<FiberContext> currentContext() {
    if (currentFiberContext == nullptr) {
        return nullptr;
    }
    return currentFiberContext->self();
}

inline void suspendCurrent() {
    logicAssert(currentFiberContext != nullptr, "No running uthread context");
    currentFiberContext->suspend();
}

inline void resumeFiber(const std::shared_ptr<FiberContext>& context) {
    logicAssert(context != nullptr, "Fiber context is null");
    context->resume();
}

}  // namespace async_simple::uthread::detail
