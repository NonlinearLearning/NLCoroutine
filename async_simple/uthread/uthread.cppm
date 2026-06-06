export module async_simple.uthread.uthread;

import std;
import async_simple.common;
import async_simple.executor;
import async_simple.future;
import async_simple.uthread.runtime;

export namespace async_simple::uthread {

inline constexpr bool kUthreadAvailable =
#if defined(_WIN32)
    true;
#else
    false;
#endif

struct Attribute {
    Executor* ex = nullptr;
    std::size_t stack_size = 0;
};

class Uthread {
public:
    Uthread() = default;

    template <class Func>
    Uthread(Attribute attr, Func&& func)
        : _attr(std::move(attr)) {
        logicAssert(kUthreadAvailable, "uthread is not supported on this platform");
        _context = std::make_shared<detail::FiberContext>(
            _attr.ex,
            _attr.stack_size,
            std::function<void()>(std::forward<Func>(func)));
        _context->bindSelf(_context);
        detail::resumeFiber(_context);
    }

    Uthread(Uthread&&) noexcept = default;
    Uthread& operator=(Uthread&&) noexcept = default;

    template <class Callback>
    bool join(Callback&& callback) {
        logicAssert(kUthreadAvailable, "uthread is not supported on this platform");
        if (_context == nullptr || _joined) {
            return false;
        }
        _joined = true;

        auto future = _context->donePromise().getFuture().via(_attr.ex);
        if (future.hasResult()) {
            std::forward<Callback>(callback)();
            return true;
        }

        if (_attr.ex != nullptr) {
            _context->donePromise().forceSched().checkout();
        }
        std::move(future).setContinuation(
            [callback = std::forward<Callback>(callback), self = _context](auto&&) mutable {
                (void)self;
                callback();
            });
        return true;
    }

    void detach() {
        (void)join([]() {});
    }

private:
    Attribute _attr;
    std::shared_ptr<detail::FiberContext> _context;
    bool _joined = false;
};

inline int runUthreadAvailabilitySelfTest() {
    return kUthreadAvailable ? 1 : 0;
}

}  // namespace async_simple::uthread
