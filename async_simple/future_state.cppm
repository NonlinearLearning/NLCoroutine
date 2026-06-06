export module async_simple.future_state;

import std;
import async_simple.common;
import async_simple.executor;
import async_simple.try_type;
import async_simple.util.move_only_function;

export namespace async_simple {

namespace detail {

enum class FutureStateTag : std::uint8_t {
    START = 0,
    ONLY_RESULT = 1 << 0,
    ONLY_CONTINUATION = 1 << 1,
    DONE = 1 << 5,
};

constexpr FutureStateTag operator|(FutureStateTag lhs, FutureStateTag rhs) {
    return static_cast<FutureStateTag>(
        static_cast<std::uint8_t>(lhs) | static_cast<std::uint8_t>(rhs));
}

constexpr FutureStateTag operator&(FutureStateTag lhs, FutureStateTag rhs) {
    return static_cast<FutureStateTag>(
        static_cast<std::uint8_t>(lhs) & static_cast<std::uint8_t>(rhs));
}

}  // namespace detail

template <typename T>
class FutureState {
private:
    using Continuation = util::move_only_function<void(Try<T>&&)>;

    class ContinuationReference {
    public:
        ContinuationReference() = default;

        explicit ContinuationReference(FutureState<T>* futureState) : _futureState(futureState) {
            attach();
        }

        ~ContinuationReference() { detach(); }

        ContinuationReference(const ContinuationReference& other) : _futureState(other._futureState) {
            attach();
        }

        ContinuationReference& operator=(const ContinuationReference&) = delete;

        ContinuationReference(ContinuationReference&& other) : _futureState(other._futureState) {
            other._futureState = nullptr;
        }

        ContinuationReference& operator=(ContinuationReference&&) = delete;

        FutureState* getFutureState() const noexcept { return _futureState; }

    private:
        void attach() {
            if (_futureState) {
                _futureState->attachOne();
                _futureState->refContinuation();
            }
        }

        void detach() {
            if (_futureState) {
                _futureState->derefContinuation();
                _futureState->detachOne();
            }
        }

        FutureState<T>* _futureState = nullptr;
    };

public:
    FutureState()
        : _state(detail::FutureStateTag::START),
          _attached(0),
          _continuationRef(0),
          _executor(nullptr),
          _context(Executor::NULLCTX),
          _promiseRef(0),
          _forceSched(false) {}

    ~FutureState() {}

    bool hasResult() const noexcept {
        constexpr auto allow =
            detail::FutureStateTag::DONE | detail::FutureStateTag::ONLY_RESULT;
        auto state = _state.load(std::memory_order_acquire);
        return (state & allow) != detail::FutureStateTag{};
    }

    bool hasContinuation() const noexcept {
        constexpr auto allow =
            detail::FutureStateTag::DONE | detail::FutureStateTag::ONLY_CONTINUATION;
        auto state = _state.load(std::memory_order_acquire);
        return (state & allow) != detail::FutureStateTag{};
    }

    void attachOne() {
        _attached.fetch_add(1, std::memory_order_relaxed);
    }

    void detachOne() {
        auto old = _attached.fetch_sub(1, std::memory_order_acq_rel);
        logicAssert(old >= 1, "FutureState detached too many times");
        if (old == 1) {
            delete this;
        }
    }

    void attachPromise() {
        _promiseRef.fetch_add(1, std::memory_order_relaxed);
        attachOne();
    }

    void detachPromise() {
        auto old = _promiseRef.fetch_sub(1, std::memory_order_acq_rel);
        logicAssert(old >= 1, "Promise detached too many times");
        if (!hasResult() && old == 1) {
            try {
                throw std::runtime_error("Promise is broken");
            } catch (...) {
                setResult(Try<T>(std::current_exception()));
            }
        }
        detachOne();
    }

    Try<T>& getTry() noexcept { return _tryValue; }
    const Try<T>& getTry() const noexcept { return _tryValue; }

    void setExecutor(Executor* executor) { _executor = executor; }
    Executor* getExecutor() { return _executor; }

    void checkout() {
        if (_executor) {
            _context = _executor->checkout();
        }
    }

    void setForceSched(bool force = true) {
        if (force && _executor == nullptr) {
            return;
        }
        _forceSched = force;
    }

    void setResult(Try<T>&& value) {
        _tryValue = std::move(value);

        auto state = _state.load(std::memory_order_acquire);
        switch (state) {
        case detail::FutureStateTag::START:
            if (_state.compare_exchange_strong(
                    state,
                    detail::FutureStateTag::ONLY_RESULT,
                    std::memory_order_release)) {
                return;
            }
            [[fallthrough]];
        case detail::FutureStateTag::ONLY_CONTINUATION:
            if (_state.compare_exchange_strong(
                    state,
                    detail::FutureStateTag::DONE,
                    std::memory_order_release)) {
                scheduleContinuation(false);
                return;
            }
            [[fallthrough]];
        default:
            logicAssert(false, "State Transfer Error");
        }
    }

    template <typename F>
    void setContinuation(F&& continuation) {
        logicAssert(!hasContinuation(), "FutureState already has a continuation");
        new (&_continuation) Continuation(
            [continuation = std::move(continuation)](Try<T>&& value) mutable {
                continuation(std::forward<Try<T>>(value));
            });

        auto state = _state.load(std::memory_order_acquire);
        switch (state) {
        case detail::FutureStateTag::START:
            if (_state.compare_exchange_strong(
                    state,
                    detail::FutureStateTag::ONLY_CONTINUATION,
                    std::memory_order_release)) {
                return;
            }
            [[fallthrough]];
        case detail::FutureStateTag::ONLY_RESULT:
            if (_state.compare_exchange_strong(
                    state,
                    detail::FutureStateTag::DONE,
                    std::memory_order_release)) {
                scheduleContinuation(true);
                return;
            }
            [[fallthrough]];
        default:
            logicAssert(false, "State Transfer Error");
        }
    }

    bool currentThreadInExecutor() const {
        return _executor != nullptr && _executor->currentThreadInExecutor();
    }

private:
    void scheduleContinuation(bool triggerByContinuation) {
        logicAssert(
            _state.load(std::memory_order_relaxed) == detail::FutureStateTag::DONE,
            "FutureState is not DONE");

        if (!_forceSched &&
            (!_executor || triggerByContinuation || currentThreadInExecutor())) {
            ContinuationReference guard(this);
            _continuation(std::move(_tryValue));
            return;
        }

        ContinuationReference guard(this);
        ContinuationReference exceptionGuard(this);

        try {
            bool scheduled = false;
            if (_context == Executor::NULLCTX) {
                scheduled = _executor->schedule(
                    [futureStateRef = std::move(guard)]() mutable {
                        auto ref = std::move(futureStateRef);
                        auto* futureState = ref.getFutureState();
                        futureState->_continuation(std::move(futureState->_tryValue));
                    });
            } else {
                ScheduleOptions options;
                options.prompt = !_forceSched;
                scheduled = _executor->checkin(
                    [futureStateRef = std::move(guard)]() mutable {
                        auto ref = std::move(futureStateRef);
                        auto* futureState = ref.getFutureState();
                        futureState->_continuation(std::move(futureState->_tryValue));
                    },
                    _context,
                    options);
            }

            if (!scheduled) {
                throw std::runtime_error("schedule continuation in executor failed");
            }
        } catch (...) {
            _continuation(std::move(_tryValue));
        }
    }

    void refContinuation() {
        _continuationRef.fetch_add(1, std::memory_order_relaxed);
    }

    void derefContinuation() {
        auto old = _continuationRef.fetch_sub(1, std::memory_order_relaxed);
        logicAssert(old >= 1, "Continuation detached too many times");
        if (old == 1) {
            _continuation.~Continuation();
        }
    }

    std::atomic<detail::FutureStateTag> _state;
    std::atomic<std::uint8_t> _attached;
    std::atomic<std::uint8_t> _continuationRef;
    Try<T> _tryValue;
    union {
        Continuation _continuation;
    };
    Executor* _executor;
    Executor::Context _context;
    std::atomic<std::size_t> _promiseRef;
    bool _forceSched;
};

}  // namespace async_simple
