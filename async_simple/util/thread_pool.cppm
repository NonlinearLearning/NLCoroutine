module;

export module async_simple.util.thread_pool;

import std;
import async_simple.util.queue;

export namespace async_simple::util {

// 这是教学版最小线程池。
//
// 它负责：
// - 创建若干 worker 线程
// - 给每个 worker 一个任务队列
// - 接受外部提交的任务
// - 由 worker 线程不断取任务执行
class ThreadPool {
public:
    struct WorkItem {
        // 是否允许被别的 worker"顺手偷走"。
        bool canSteal = false;
        std::function<void()> fn = nullptr;
    };

    enum ERROR_TYPE {
        ERROR_NONE = 0,
        ERROR_POOL_HAS_STOP,
        ERROR_POOL_ITEM_IS_NULL,
    };

    explicit ThreadPool(std::size_t threadNum = std::thread::hardware_concurrency(),
                        bool enableWorkSteal = false)
        : _threadNum(threadNum ? static_cast<std::int32_t>(threadNum)
                               : static_cast<std::int32_t>(std::thread::hardware_concurrency())),
          _queues(static_cast<std::size_t>(_threadNum)),
          _enableWorkSteal(enableWorkSteal) {
        // 每个 worker 线程执行同一个循环：
        // 1. 尝试偷任务
        // 2. 偷不到就从自己的队列阻塞等待
        // 3. 拿到任务后执行
        auto worker = [this](std::size_t id) {
            auto current = getCurrent();
            current->first = id;
            current->second = this;
            while (true) {
                WorkItem item;
                if (_enableWorkSteal) {
                    auto canStealCandidate = [](auto& candidate) {
                        return candidate.canSteal;
                    };
                    // 从自己和相邻队列里尝试偷"允许被偷"的任务。
                    for (auto n = 0; n < _threadNum * 2; ++n) {
                        if (_queues[(static_cast<std::size_t>(id) +
                                     static_cast<std::size_t>(n)) %
                                    static_cast<std::size_t>(_threadNum)]
                                .try_pop_if(item, canStealCandidate)) {
                            break;
                        }
                    }
                }
                // 如果没偷到任务，就阻塞等待自己队列里的新任务。
                if (!item.fn && !_queues[id].pop(item)) {
                    if (_stop.load(std::memory_order_acquire)) {
                        break;
                    }
                    continue;
                }
                if (item.fn) {
                    item.fn();
                }
            }
        };

        _threads.reserve(static_cast<std::size_t>(_threadNum));
        for (auto i = 0; i < _threadNum; ++i) {
            _threads.emplace_back(worker, static_cast<std::size_t>(i));
        }
    }

    // 析构时先停掉队列，再 join 全部 worker。
    ~ThreadPool() {
        _stop.store(true, std::memory_order_release);
        for (auto& queue : _queues) {
            queue.stop();
        }
        for (auto& thread : _threads) {
            if (thread.joinable()) {
                thread.join();
            }
        }
    }

    // 把任务提交给某个指定 worker，或者随机选一个 worker。
    ERROR_TYPE scheduleById(std::function<void()> fn, std::int32_t id = -1) {
        if (!fn) {
            return ERROR_POOL_ITEM_IS_NULL;
        }
        if (_stop.load(std::memory_order_acquire)) {
            return ERROR_POOL_HAS_STOP;
        }
        if (id == -1) {
            if (_enableWorkSteal) {
                // 快速路径：先尽量 try_push 到一个暂时空闲的队列里。
                WorkItem item{true, fn};
                for (auto n = 0; n < _threadNum * 2; ++n) {
                    if (_queues[static_cast<std::size_t>(n % _threadNum)].try_push(item)) {
                        return ERROR_NONE;
                    }
                }
            }
            // 如果快速路径失败，就随机挑一个队列。
            id = static_cast<std::int32_t>(std::rand() % _threadNum);
        } else {
            // 模块里尽量避免依赖宏风格 `assert`，
            // 直接做运行时保护更直观。
            if (id >= _threadNum) {
                std::terminate();
            }
        }
        _queues[static_cast<std::size_t>(id)].push(
            WorkItem{_enableWorkSteal && id == -1, std::move(fn)});
        return ERROR_NONE;
    }

    std::int32_t getCurrentId() const {
        auto current = getCurrent();
        if (current->second == this) {
            return static_cast<std::int32_t>(current->first);
        }
        return -1;
    }

    std::size_t getItemCount() const {
        std::size_t count = 0;
        for (const auto& queue : _queues) {
            count += queue.size();
        }
        return count;
    }

private:
    using Current = std::pair<std::size_t, ThreadPool*>;

    // `thread_local` 表示"每条线程各有一份自己的变量副本"。
    // 这里用它记录：当前线程是否属于这个线程池，以及它的 worker 编号。
    static Current* getCurrent() {
        static thread_local Current current{static_cast<std::size_t>(-1), nullptr};
        return &current;
    }

    std::int32_t _threadNum;
    std::vector<Queue<WorkItem>> _queues;
    std::vector<std::thread> _threads;
    std::atomic<bool> _stop = false;
    bool _enableWorkSteal = false;
};

}  // namespace async_simple::util
