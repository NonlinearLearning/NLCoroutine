module;

export module async_simple.util.queue;

import std;

export namespace async_simple::util {

// 这是一个非常小的线程安全队列。
//
// 它的职责很单纯：
// - 生产者线程往里塞任务
// - 消费者线程从里取任务
// - 多线程同时访问时保持数据一致
//
// 这里选用"互斥锁 + 条件变量"的传统写法，
// 因为它最适合教学，不追求花哨的无锁技巧。
template <typename T>
requires std::is_move_assignable_v<T>
class Queue {
public:
    // 往队列尾部放一个元素。
    // `T&&` 是右值引用，常见于"我要把对象资源转移进来"的场景。
    void push(T&& item) {
        {
            // `std::scoped_lock` 构造时加锁，离开作用域自动解锁。
            std::scoped_lock guard(_mutex);
            _queue.push(std::move(item));
        }
        // 放入元素后，唤醒一个等待中的消费者。
        _condition.notify_one();
    }

    // 尝试 push，但拿不到锁就直接失败，不会阻塞等待。
    bool try_push(const T& item) {
        // `std::unique_lock` 比 `scoped_lock` 更灵活，
        // 这里配合 `std::try_to_lock` 使用，表示"只试一次"。
        std::unique_lock lock(_mutex, std::try_to_lock);
        if (!lock) {
            return false;
        }
        _queue.push(item);
        lock.unlock();
        _condition.notify_one();
        return true;
    }

    // 阻塞式 pop：
    // - 队列空时进入等待
    // - 有元素或 stop 后再醒来
    bool pop(T& item) {
        std::unique_lock lock(_mutex);
        // `wait(lock, predicate)` 会在内部反复检查条件，
        // 可以正确处理"伪唤醒"。
        _condition.wait(lock, [&] { return _stop || !_queue.empty(); });
        if (_queue.empty()) {
            return false;
        }
        item = std::move(_queue.front());
        _queue.pop();
        return true;
    }

    // 非阻塞 pop：拿不到锁或者队列为空都直接失败。
    bool try_pop(T& item) {
        std::unique_lock lock(_mutex, std::try_to_lock);
        if (!lock || _queue.empty()) {
            return false;
        }
        item = std::move(_queue.front());
        _queue.pop();
        return true;
    }

    // 带额外条件判断的非阻塞 pop。
    // 线程池会用它实现"只偷允许被偷的任务"。
    bool try_pop_if(T& item, bool (*predict)(T&) = nullptr) {
        std::unique_lock lock(_mutex, std::try_to_lock);
        if (!lock || _queue.empty()) {
            return false;
        }
        if (predict && !predict(_queue.front())) {
            return false;
        }
        item = std::move(_queue.front());
        _queue.pop();
        return true;
    }

    std::size_t size() const {
        std::scoped_lock guard(_mutex);
        return _queue.size();
    }

    // 通知所有等待中的线程：这个队列准备结束了。
    void stop() {
        {
            std::scoped_lock guard(_mutex);
            _stop = true;
        }
        _condition.notify_all();
    }

private:
    std::queue<T> _queue;
    mutable std::mutex _mutex;
    std::condition_variable _condition;
    bool _stop = false;
};

}  // namespace async_simple::util
