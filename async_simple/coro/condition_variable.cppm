export module async_simple.coro.condition_variable;

import std;
import async_simple.coro.lazy;
import async_simple.coro.mutex;

export namespace async_simple::coro {

template <class Lock>
class ConditionVariableAwaiter;

template <class Lock>
class ConditionVariable {
public:
    ConditionVariable() noexcept = default;
    ~ConditionVariable() = default;

    ConditionVariable(const ConditionVariable&) = delete;
    ConditionVariable& operator=(const ConditionVariable&) = delete;

    void notify() noexcept { notifyAll(); }
    void notifyOne() noexcept;
    void notifyAll() noexcept;

    template <class Pred>
    Lazy<void> wait(std::unique_lock<Lock>& lock, Pred&& pred) noexcept;

private:
    void resumeWaiters(ConditionVariableAwaiter<Lock>* awaiters);

    friend class ConditionVariableAwaiter<Lock>;
    // 等待该条件变量的协程链表头。
    std::atomic<ConditionVariableAwaiter<Lock>*> _awaiters = nullptr;
};

template <class Lock>
class ConditionVariableAwaiter {
public:
    ConditionVariableAwaiter(ConditionVariable<Lock>* cv,
                             std::unique_lock<Lock>& lock) noexcept
        : _cv(cv), _lock(lock) {}

    bool await_ready() const noexcept { return false; }

    void await_suspend(std::coroutine_handle<> continuation) noexcept {
        _continuation = continuation;
        _lock.unlock();

        auto awaiters = _cv->_awaiters.load(std::memory_order_relaxed);
        do {
            _next = awaiters;
        } while (!_cv->_awaiters.compare_exchange_weak(
            awaiters, this, std::memory_order_acquire,
            std::memory_order_relaxed));
    }

    void await_resume() const noexcept {}

private:
    friend class ConditionVariable<Lock>;

    // 自己所属的条件变量。
    ConditionVariable<Lock>* _cv;
    // 外层传入的锁引用；挂起前解锁，恢复后重新加锁。
    std::unique_lock<Lock>& _lock;
    // 单向链表中的下一个等待者。
    ConditionVariableAwaiter<Lock>* _next = nullptr;
    // 条件满足后要恢复的协程。
    std::coroutine_handle<> _continuation;
};

template <class Lock>
template <class Pred>
inline Lazy<void> ConditionVariable<Lock>::wait(std::unique_lock<Lock>& lock,
                                                Pred&& pred) noexcept {
    while (!pred()) {
        co_await ConditionVariableAwaiter<Lock>{this, lock};
        lock = co_await lock.mutex()->coScopedLock();
    }
}

template <class Lock>
inline void ConditionVariable<Lock>::notifyAll() noexcept {
    auto awaiters = _awaiters.load(std::memory_order_relaxed);
    while (!_awaiters.compare_exchange_weak(awaiters, nullptr,
                                            std::memory_order_release,
                                            std::memory_order_relaxed)) {
    }
    resumeWaiters(awaiters);
}

template <class Lock>
inline void ConditionVariable<Lock>::notifyOne() noexcept {
    auto awaiters = _awaiters.load(std::memory_order_relaxed);
    if (awaiters == nullptr) {
        return;
    }
    while (!_awaiters.compare_exchange_weak(awaiters, awaiters->_next,
                                            std::memory_order_release,
                                            std::memory_order_relaxed)) {
        if (awaiters == nullptr) {
            return;
        }
    }
    awaiters->_next = nullptr;
    resumeWaiters(awaiters);
}

template <class Lock>
inline void ConditionVariable<Lock>::resumeWaiters(
    ConditionVariableAwaiter<Lock>* awaiters) {
    while (awaiters != nullptr) {
        auto* current = awaiters;
        awaiters = awaiters->_next;
        current->_continuation.resume();
    }
}

template <>
class ConditionVariableAwaiter<void>;

template <>
class ConditionVariable<void> {
public:
    using pointer_type = void*;

    ConditionVariable() noexcept = default;
    ~ConditionVariable() = default;

    ConditionVariable(const ConditionVariable&) = delete;
    ConditionVariable& operator=(const ConditionVariable&) = delete;

    void notify() noexcept;
    ConditionVariableAwaiter<void> wait() noexcept;
    void reset() noexcept;

private:
    void resumeWaiters(ConditionVariableAwaiter<void>* awaiters);

    friend class ConditionVariableAwaiter<void>;
    // `void` 特化版本的等待者链表头。
    std::atomic<pointer_type> _awaiters = nullptr;
};

template <>
class ConditionVariableAwaiter<void> {
public:
    using pointer_type = void*;

    explicit ConditionVariableAwaiter(ConditionVariable<void>* cv) noexcept : _cv(cv) {}

    bool await_ready() const noexcept {
        return static_cast<pointer_type>(_cv) ==
               _cv->_awaiters.load(std::memory_order_acquire);
    }

    bool await_suspend(std::coroutine_handle<> continuation) noexcept {
        _continuation = continuation;
        pointer_type awaiters = _cv->_awaiters.load(std::memory_order_acquire);
        do {
            if (awaiters == static_cast<pointer_type>(_cv)) {
                return false;
            }
            _next = static_cast<ConditionVariableAwaiter<void>*>(awaiters);
        } while (!_cv->_awaiters.compare_exchange_weak(
            awaiters, static_cast<pointer_type>(this),
            std::memory_order_release, std::memory_order_acquire));
        return true;
    }

    void await_resume() const noexcept {}

private:
    friend class ConditionVariable<void>;

    // 自己所属的条件变量。
    ConditionVariable<void>* _cv;
    // 单向链表中的下一个等待者。
    ConditionVariableAwaiter<void>* _next = nullptr;
    // 通知到来时需要恢复的协程。
    std::coroutine_handle<> _continuation;
};

inline ConditionVariableAwaiter<void> ConditionVariable<void>::wait() noexcept {
    return ConditionVariableAwaiter<void>{this};
}

inline void ConditionVariable<void>::notify() noexcept {
    pointer_type self = static_cast<pointer_type>(this);
    pointer_type awaiters = _awaiters.exchange(self, std::memory_order_acq_rel);
    if (awaiters != self) {
        resumeWaiters(static_cast<ConditionVariableAwaiter<void>*>(awaiters));
    }
}

inline void ConditionVariable<void>::reset() noexcept {
    pointer_type self = static_cast<pointer_type>(this);
    _awaiters.compare_exchange_strong(self, nullptr, std::memory_order_relaxed);
}

inline void ConditionVariable<void>::resumeWaiters(
    ConditionVariableAwaiter<void>* awaiters) {
    while (awaiters != nullptr) {
        auto* current = awaiters;
        awaiters = awaiters->_next;
        current->_continuation.resume();
    }
}

using Notifier = ConditionVariable<void>;

}  // namespace async_simple::coro
