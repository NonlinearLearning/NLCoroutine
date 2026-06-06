export module async_simple.coro.latch;

import std;
import async_simple.common;
import async_simple.coro.condition_variable;
import async_simple.coro.lazy;
import async_simple.coro.mutex;

export namespace async_simple::coro {

class Latch {
public:
    explicit Latch(std::size_t count) : _count(count) {}
    ~Latch() = default;

    Latch(const Latch&) = delete;
    Latch& operator=(const Latch&) = delete;

    Lazy<void> countDown(std::size_t update = 1) {
        auto lock = co_await _mutex.coScopedLock();
        logicAssert(_count >= update, "Latch countDown underflow");
        _count -= update;
        if (_count == 0) {
            _cv.notifyAll();
        }
    }

    Lazy<bool> tryWait() {
        auto lock = co_await _mutex.coScopedLock();
        co_return _count == 0;
    }

    Lazy<void> wait() {
        auto lock = co_await _mutex.coScopedLock();
        co_await _cv.wait(lock, [&] { return _count == 0; });
    }

    Lazy<void> arriveAndWait(std::size_t update = 1) {
        auto lock = co_await _mutex.coScopedLock();
        logicAssert(_count >= update, "Latch arriveAndWait underflow");
        _count -= update;
        if (_count == 0) {
            _cv.notifyAll();
            co_return;
        }
        co_await _cv.wait(lock, [&] { return _count == 0; });
    }

private:
    // 保护 `_count` 的异步互斥锁。
    Mutex _mutex;
    // 计数归零时用来唤醒等待协程。
    ConditionVariable<Mutex> _cv;
    // 剩余尚未到达的计数。
    std::size_t _count;
};

}  // namespace async_simple::coro
