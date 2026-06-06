export module async_simple.future;

import std;
import async_simple.common;
import async_simple.executor;
import async_simple.future_state;
import async_simple.local_state;
import async_simple.try_type;
import async_simple.unit;

export namespace async_simple {

template <typename T>
struct IsFuture : std::false_type {
    using Inner = T;
};

template <typename T>
class Promise;

template <typename T, typename F>
struct TryCallableResult {
    using Result = std::invoke_result_t<F, Try<T>&&>;
    using ReturnsFuture = IsFuture<Result>;
    static constexpr bool isTry = true;
};

template <typename T, typename F>
struct ValueCallableResult {
    using Result = std::invoke_result_t<F, T&&>;
    using ReturnsFuture = IsFuture<Result>;
    static constexpr bool isTry = false;
};

template <typename F>
struct ValueCallableResult<void, F> {
    using Result = std::invoke_result_t<F>;
    using ReturnsFuture = IsFuture<Result>;
    static constexpr bool isTry = false;
};

template <typename T>
class Future {
private:
    using inner_value_type = std::conditional_t<std::is_void_v<T>, Unit, T>;

public:
    using value_type = T;

    explicit Future(FutureState<inner_value_type>* futureState) : _sharedState(futureState) {
        if (_sharedState) {
            _sharedState->attachOne();
        }
    }

    explicit Future(Try<inner_value_type>&& value)
        : _sharedState(nullptr), _localState(std::move(value)) {}

    ~Future() {
        if (_sharedState) {
            _sharedState->detachOne();
        }
    }

    Future(const Future&) = delete;
    Future& operator=(const Future&) = delete;

    Future(Future&& other)
        : _sharedState(other._sharedState),
          _localState(std::move(other._localState)) {
        other._sharedState = nullptr;
    }

    Future& operator=(Future&& other) {
        if (this != &other) {
            std::swap(_sharedState, other._sharedState);
            _localState = std::move(other._localState);
        }
        return *this;
    }

    auto coAwait(Executor*) && noexcept {
        return std::move(*this);
    }

    bool valid() const {
        return _sharedState != nullptr || _localState.hasResult();
    }

    bool hasResult() const {
        return _localState.hasResult() || (_sharedState != nullptr && _sharedState->hasResult());
    }

    std::add_rvalue_reference_t<T> value() && {
        if constexpr (std::is_void_v<T>) {
            return result().value();
        } else {
            return std::move(result().value());
        }
    }

    std::add_lvalue_reference_t<T> value() & { return result().value(); }

    const std::add_lvalue_reference_t<T> value() const& { return result().value(); }

    Try<T>&& result() && requires(!std::is_void_v<T>) { return std::move(getTry(*this)); }
    Try<T>& result() & requires(!std::is_void_v<T>) { return getTry(*this); }
    const Try<T>& result() const& requires(!std::is_void_v<T>) { return getTry(*this); }

    Try<void> result() && requires(std::is_void_v<T>) { return getTry(*this); }
    Try<void> result() & requires(std::is_void_v<T>) { return getTry(*this); }
    Try<void> result() const& requires(std::is_void_v<T>) { return getTry(*this); }

    T get() && {
        wait();
        return std::move(*this).value();
    }

    void wait() {
        logicAssert(valid(), "Future is broken");
        if (hasResult()) {
            return;
        }

        logicAssert(!currentThreadInExecutor(), "wait in executor thread may deadlock");

        Promise<T> promise;
        auto future = promise.getFuture();

        _sharedState->setExecutor(nullptr);
        std::mutex mutex;
        std::condition_variable cv;
        std::atomic<bool> done{false};
        _sharedState->setContinuation(
            [&mutex, &cv, &done, promise = std::move(promise)](Try<T>&& value) mutable {
                std::unique_lock lock(mutex);
                promise.setValue(std::move(value));
                done.store(true, std::memory_order_relaxed);
                cv.notify_one();
            });
        std::unique_lock lock(mutex);
        cv.wait(lock, [&done]() { return done.load(std::memory_order_relaxed); });
        *this = std::move(future);
        logicAssert(_sharedState == nullptr || _sharedState->hasResult(), "Future wait failed");
    }

    Future<T> via(Executor* executor) && {
        setExecutor(executor);
        return Future<T>(std::move(*this));
    }

    template <typename F, typename R = TryCallableResult<T, F>>
    Future<typename R::ReturnsFuture::Inner> thenTry(F&& func) && {
        return thenImpl<F, R>(std::forward<F>(func));
    }

    template <typename F, typename R = ValueCallableResult<T, F>>
    Future<typename R::ReturnsFuture::Inner> thenValue(F&& func) && {
        auto wrapper = [func = std::forward<F>(func)](Try<T>&& value) mutable {
            if constexpr (std::is_void_v<T>) {
                value.value();
                return std::forward<F>(func)();
            } else {
                return std::forward<F>(func)(std::move(value).value());
            }
        };
        using Wrapper = decltype(wrapper);
        return thenImpl<Wrapper, TryCallableResult<T, Wrapper>>(std::move(wrapper));
    }

    template <
        typename F,
        typename R = std::conditional_t<
            std::is_invocable_v<F, T>,
            ValueCallableResult<T, F>,
            TryCallableResult<T, F>>>
    Future<typename R::ReturnsFuture::Inner> then(F&& func) && {
        if constexpr (std::is_invocable_v<F, T>) {
            return std::move(*this).thenValue(std::forward<F>(func));
        } else {
            return std::move(*this).thenTry(std::forward<F>(func));
        }
    }

    void setExecutor(Executor* executor) {
        if (_sharedState) {
            _sharedState->setExecutor(executor);
        } else {
            _localState.setExecutor(executor);
        }
    }

    Executor* getExecutor() {
        if (_sharedState) {
            return _sharedState->getExecutor();
        }
        return _localState.getExecutor();
    }

    template <typename F>
    void setContinuation(F&& func) {
        logicAssert(valid(), "Future is broken");
        if (_sharedState) {
            _sharedState->setContinuation(std::forward<F>(func));
        } else {
            _localState.setContinuation(std::forward<F>(func));
        }
    }

    bool currentThreadInExecutor() const {
        logicAssert(valid(), "Future is broken");
        if (_sharedState) {
            return _sharedState->currentThreadInExecutor();
        }
        return _localState.currentThreadInExecutor();
    }

    bool TEST_hasLocalState() const {
        return _localState.hasResult();
    }

private:
    template <typename Clazz>
    static decltype(auto) getTry(Clazz& self) {
        logicAssert(self.valid(), "Future is broken");
        logicAssert(
            self._localState.hasResult() ||
                (self._sharedState != nullptr && self._sharedState->hasResult()),
            "Future is not ready");
        if (self._sharedState) {
            return self._sharedState->getTry();
        }
        return self._localState.getTry();
    }

    template <typename F, typename R>
    Future<typename R::ReturnsFuture::Inner> thenImpl(F&& func) {
        logicAssert(valid(), "Future is broken");
        using T2 = typename R::ReturnsFuture::Inner;

        if (!_sharedState) {
            if constexpr (R::ReturnsFuture::value) {
                try {
                    auto nextFuture = std::forward<F>(func)(std::move(_localState.getTry()));
                    if (!nextFuture.getExecutor()) {
                        nextFuture.setExecutor(_localState.getExecutor());
                    }
                    return nextFuture;
                } catch (...) {
                    return Future<T2>(Try<T2>(std::current_exception()));
                }
            } else {
                Future<T2> nextFuture(
                    makeTryCall(std::forward<F>(func), std::move(_localState.getTry())));
                nextFuture.setExecutor(_localState.getExecutor());
                return nextFuture;
            }
        }

        Promise<T2> promise;
        auto nextFuture = promise.getFuture();
        nextFuture.setExecutor(_sharedState->getExecutor());
        _sharedState->setContinuation(
            [promise = std::move(promise), func = std::forward<F>(func)](Try<T>&& value) mutable {
                if (!R::isTry && value.hasError()) {
                    promise.setException(value.getException());
                    return;
                }

                if constexpr (R::ReturnsFuture::value) {
                    try {
                        auto chained = func(std::move(value));
                        chained.setContinuation(
                            [promise = std::move(promise)](Try<T2>&& chainedValue) mutable {
                                promise.setValue(std::move(chainedValue));
                            });
                    } catch (...) {
                        promise.setException(std::current_exception());
                    }
                } else {
                    promise.setValue(makeTryCall(std::forward<F>(func), std::move(value)));
                }
            });
        return nextFuture;
    }

    FutureState<inner_value_type>* _sharedState = nullptr;
    LocalState<inner_value_type> _localState;

    template <typename U>
    friend class Promise;
};

template <typename T>
struct IsFuture<Future<T>> : std::true_type {
    using Inner = T;
};

template <typename T>
Future<T> makeReadyFuture(T&& value) {
    return Future<T>(Try<T>(std::forward<T>(value)));
}

template <typename T>
Future<T> makeReadyFuture(Try<T>&& value) {
    return Future<T>(std::move(value));
}

template <typename T>
Future<T> makeReadyFuture(std::exception_ptr error) {
    return Future<T>(Try<T>(error));
}

inline Future<void> makeReadyFuture() {
    return Future<void>(Try<Unit>(Unit()));
}

template <typename T>
class Promise {
public:
    using value_type = std::conditional_t<std::is_void_v<T>, Unit, T>;

    Promise() : _sharedState(new FutureState<value_type>()), _hasFuture(false) {
        _sharedState->attachPromise();
    }

    ~Promise() {
        if (_sharedState) {
            _sharedState->detachPromise();
        }
    }

    Promise(const Promise& other) {
        _sharedState = other._sharedState;
        _hasFuture = other._hasFuture;
        _sharedState->attachPromise();
    }

    Promise& operator=(const Promise& other) {
        if (this == &other) {
            return *this;
        }
        this->~Promise();
        _sharedState = other._sharedState;
        _hasFuture = other._hasFuture;
        _sharedState->attachPromise();
        return *this;
    }

    Promise(Promise&& other)
        : _sharedState(other._sharedState), _hasFuture(other._hasFuture) {
        other._sharedState = nullptr;
        other._hasFuture = false;
    }

    Promise& operator=(Promise&& other) {
        std::swap(_sharedState, other._sharedState);
        std::swap(_hasFuture, other._hasFuture);
        return *this;
    }

    Future<T> getFuture() {
        logicAssert(valid(), "Promise is broken");
        logicAssert(!_hasFuture, "Promise already has a future");
        _hasFuture = true;
        return Future<T>(_sharedState);
    }

    bool valid() const { return _sharedState != nullptr; }

    Promise& checkout() {
        if (_sharedState) {
            _sharedState->checkout();
        }
        return *this;
    }

    Promise& forceSched() {
        if (_sharedState) {
            _sharedState->setForceSched();
        }
        return *this;
    }

    void setException(std::exception_ptr error) {
        logicAssert(valid(), "Promise is broken");
        _sharedState->setResult(Try<value_type>(error));
    }

    void setValue(value_type&& value) requires(!std::is_void_v<T>) {
        logicAssert(valid(), "Promise is broken");
        _sharedState->setResult(Try<value_type>(std::forward<T>(value)));
    }

    void setValue(Try<value_type>&& value) {
        logicAssert(valid(), "Promise is broken");
        _sharedState->setResult(std::move(value));
    }

    void setValue() requires(std::is_void_v<T>) {
        logicAssert(valid(), "Promise is broken");
        _sharedState->setResult(Try<value_type>(Unit()));
    }

private:
    FutureState<value_type>* _sharedState = nullptr;
    bool _hasFuture = false;
};

inline int runFuturePromiseSelfTest(Executor* executor) {
    Promise<int> promise;
    auto future = promise.getFuture().via(executor);
    promise.setValue(41);

    auto value = std::move(future)
        .thenValue([](int input) {
            return input + 1;
        })
        .thenTry([](Try<int>&& result) {
            if (result.hasError()) {
                return -1;
            }
            return result.value();
        })
        .get();

    auto ready = makeReadyFuture(std::string("ready"));
    auto readyValue = std::move(ready).get();

    Promise<void> voidPromise;
    auto voidFuture = voidPromise.getFuture().via(executor);
    auto voidChained = std::move(voidFuture)
        .thenValue([]() {
            return 5;
        });
    voidPromise.setValue();
    auto voidValue = std::move(voidChained).get();

    Promise<int> brokenPromise;
    auto brokenFuture = brokenPromise.getFuture();
    brokenPromise = Promise<int>();
    auto brokenObserved = std::move(brokenFuture)
        .thenTry([](Try<int>&& result) {
            return result.hasError();
        })
        .get();

    if (value != 42 || readyValue != "ready" || voidValue != 5 || !brokenObserved) {
        return 1;
    }
    return 0;
}

}  // namespace async_simple
