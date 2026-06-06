export module async_simple.coro.semaphore;

import std;
import async_simple.common;
import async_simple.coro.condition_variable;
import async_simple.coro.lazy;
import async_simple.coro.mutex;

export namespace async_simple::coro {

template <std::size_t LeastMaxValue = std::numeric_limits<std::uint32_t>::max()>
class CountingSemaphore {
public:
    static_assert(LeastMaxValue <= std::numeric_limits<std::uint32_t>::max());

    explicit CountingSemaphore(std::size_t desired) : _count(desired) {}
    ~CountingSemaphore() = default;

    CountingSemaphore(const CountingSemaphore&) = delete;
    CountingSemaphore& operator=(const CountingSemaphore&) = delete;

    static constexpr std::size_t max() noexcept { return LeastMaxValue; }

    Lazy<void> acquire() noexcept {
        auto lock = co_await _mutex.coScopedLock();
        co_await _cv.wait(lock, [this] { return _count > 0; });
        --_count;
    }

    Lazy<void> release(std::size_t update = 1) noexcept {
        logicAssert(update <= LeastMaxValue && update != 0,
                    "Semaphore release update exceeds supported range");
        auto lock = co_await _mutex.coScopedLock();
        logicAssert(_count <= LeastMaxValue - update,
                    "Semaphore release would overflow count");
        _count += update;
        if (update > 1) {
            _cv.notifyAll();
        } else {
            _cv.notifyOne();
        }
    }

    Lazy<bool> tryAcquire() noexcept {
        auto lock = co_await _mutex.coScopedLock();
        if (_count == 0) {
            co_return false;
        }
        --_count;
        co_return true;
    }

private:
    // 保护信号量计数的异步互斥锁。
    Mutex _mutex;
    // 有可用许可时用于唤醒等待协程。
    ConditionVariable<Mutex> _cv;
    // 当前可用许可数。
    std::size_t _count;
};

using BinarySemaphore = CountingSemaphore<1>;

}  // namespace async_simple::coro
