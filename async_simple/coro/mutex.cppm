export module async_simple.coro.mutex;

import std;
import async_simple.common;
import async_simple.experimental.coroutine;

export namespace async_simple::coro {

class Mutex {
private:
    class ScopedLockAwaiter;
    class LockAwaiter;

public:
    Mutex() noexcept : _state(unlockedState()), _waiters(nullptr) {}

    Mutex(const Mutex&) = delete;
    Mutex(Mutex&&) = delete;
    Mutex& operator=(const Mutex&) = delete;
    Mutex& operator=(Mutex&&) = delete;

    ~Mutex() {
        logicAssert(_state.load(std::memory_order_relaxed) == unlockedState() ||
                        _state.load(std::memory_order_relaxed) == nullptr,
                    "Destroying a locked async mutex");
        logicAssert(_waiters == nullptr, "Destroying a mutex with queued waiters");
    }

    bool tryLock() noexcept {
        void* oldValue = unlockedState();
        return _state.compare_exchange_strong(oldValue, nullptr,
                                              std::memory_order_acquire,
                                              std::memory_order_relaxed);
    }

    [[nodiscard]] ScopedLockAwaiter coScopedLock() noexcept;
    [[nodiscard]] LockAwaiter coLock() noexcept;

    void lock() { std::terminate(); }

    void unlock() noexcept {
        logicAssert(_state.load(std::memory_order_relaxed) != unlockedState(),
                    "Unlocking a mutex that is not locked");
        auto* waitersHead = _waiters;
        if (waitersHead == nullptr) {
            void* currentState = _state.load(std::memory_order_relaxed);
            if (currentState == nullptr) {
                const bool releasedLock = _state.compare_exchange_strong(
                    currentState, unlockedState(), std::memory_order_release,
                    std::memory_order_relaxed);
                if (releasedLock) {
                    return;
                }
            }

            currentState = _state.exchange(nullptr, std::memory_order_acquire);
            logicAssert(currentState != unlockedState(),
                        "Mutex waiter state unexpectedly marked unlocked");
            logicAssert(currentState != nullptr,
                        "Mutex waiter state unexpectedly empty");
            auto* waiter = static_cast<LockAwaiter*>(currentState);
            do {
                auto* temp = waiter->_next;
                waiter->_next = waitersHead;
                waitersHead = waiter;
                waiter = temp;
            } while (waiter != nullptr);
        }

        logicAssert(waitersHead != nullptr, "Mutex waiter queue unexpectedly empty");
        _waiters = waitersHead->_next;
        waitersHead->_awaitingCoroutine.resume();
    }

private:
    class LockAwaiter {
    public:
        explicit LockAwaiter(Mutex& mutex) noexcept : _mutex(mutex) {}

        bool await_ready() noexcept { return _mutex.tryLock(); }

        bool await_suspend(std::coroutine_handle<> awaitingCoroutine) noexcept {
            _awaitingCoroutine = awaitingCoroutine;
            return _mutex.lockAsyncImpl(this);
        }

        void await_resume() noexcept {}

    protected:
        Mutex& _mutex;

    private:
        friend class Mutex;

        // 拿到锁后要恢复的协程。
        std::coroutine_handle<> _awaitingCoroutine;
        // 等待队列中的下一个节点。
        LockAwaiter* _next = nullptr;
    };

    class ScopedLockAwaiter : public LockAwaiter {
    public:
        using LockAwaiter::LockAwaiter;

        [[nodiscard]] std::unique_lock<Mutex> await_resume() noexcept {
            return std::unique_lock<Mutex>{this->_mutex, std::adopt_lock};
        }
    };

    void* unlockedState() noexcept { return this; }

    bool lockAsyncImpl(LockAwaiter* awaiter) noexcept {
        void* oldValue = _state.load(std::memory_order_relaxed);
        while (true) {
            if (oldValue == unlockedState()) {
                void* newValue = nullptr;
                if (_state.compare_exchange_weak(oldValue, newValue,
                                                 std::memory_order_acquire,
                                                 std::memory_order_relaxed)) {
                    return false;
                }
            } else {
                void* newValue = awaiter;
                awaiter->_next = static_cast<LockAwaiter*>(oldValue);
                if (_state.compare_exchange_weak(oldValue, newValue,
                                                 std::memory_order_release,
                                                 std::memory_order_relaxed)) {
                    return true;
                }
            }
        }
    }

    // 原子状态位：`this` 表示未加锁，`nullptr` 表示已加锁无等待者，其余值表示等待栈。
    std::atomic<void*> _state;
    // 释放锁时用于公平恢复的等待队列头。
    LockAwaiter* _waiters;
};

inline Mutex::ScopedLockAwaiter Mutex::coScopedLock() noexcept {
    return ScopedLockAwaiter(*this);
}

inline Mutex::LockAwaiter Mutex::coLock() noexcept {
    return LockAwaiter(*this);
}

}  // namespace async_simple::coro
