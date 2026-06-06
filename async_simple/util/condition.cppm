export module async_simple.util.condition;

import std;

export namespace async_simple::util {

// 这是一个最小同步原语：
// - `release()` 表示"发一个可消费的信号"
// - `acquire()` 表示"等待并消费一个信号"
//
// 这里固定使用"互斥锁 + 条件变量 + 计数器"实现，
// 避免不同标准库对 `<semaphore>` 支持不一致带来的模块兼容问题。
class Condition {
public:
    void release() {
        std::lock_guard lock(_mutex);
        ++_count;
        _condition.notify_one();
    }

    void acquire() {
        std::unique_lock lock(_mutex);
        _condition.wait(lock, [&] { return _count > 0; });
        --_count;
    }

private:
    std::mutex _mutex;
    std::condition_variable _condition;
    // 表示"还剩多少次 release 可以被 acquire 消耗"。
    std::size_t _count = 0;
};

}  // namespace async_simple::util
