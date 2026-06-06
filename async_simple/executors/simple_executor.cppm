export module async_simple.executors.simple_executor;

import std;
import async_simple.executor;
import async_simple.util.thread_pool;

export namespace async_simple::executors {

// `SimpleExecutor` 是一个教学用的最小执行器：
// - 底层就是一个小线程池
// - 对上暴露统一的 `Executor` 接口
//
// 它的作用不是追求最强调度能力，而是让上层 `Lazy` 先拥有
// "可绑定执行器 / 可切换线程恢复"的运行时语义。
class SimpleExecutor : public Executor {
public:
    // 这里把底层线程池开成工作窃取模式（第二个参数为 true），
    // 这样多个线程在教学 demo 中更容易有"并发执行"的效果。
    explicit SimpleExecutor(std::size_t threadNum)
        : Executor("simple"), _pool(threadNum, true) {}

    // 真正的调度动作就是"把任务扔进线程池"。
    bool schedule(Func func) override {
        return _pool.scheduleById(std::move(func)) == util::ThreadPool::ERROR_NONE;
    }

    // 如果线程池能识别出"当前线程就是它自己的 worker"，
    // 就返回 true。
    bool currentThreadInExecutor() const override {
        return _pool.getCurrentId() != -1;
    }

    // 返回队列里还剩多少待执行任务。
    ExecutorStat stat() const override {
        return ExecutorStat{_pool.getItemCount()};
    }

private:
    util::ThreadPool _pool;
};

}  // namespace async_simple::executors
