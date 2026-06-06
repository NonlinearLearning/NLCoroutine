import std;

import async_simple.executor;
import async_simple.future;
import async_simple.signal;
import async_simple.coro.lazy;
import async_simple.coro.future_awaiter;
import async_simple.coro.collect;
import async_simple.coro.condition_variable;
import async_simple.coro.latch;
import async_simple.coro.mutex;
import async_simple.coro.semaphore;
import async_simple.coro.sync_await;
import async_simple.executors.simple_executor;

namespace coro = async_simple::coro;

// `DemoLocal` 用来证明“协程上下文跟着协程链走，而不是跟着线程走”。
// 外层协程把它绑进 promise 后，子协程通过 `CurrentLazyLocals` 就能读到。
struct DemoLocal : public coro::LazyLocalBase {
    inline static char tag;

    explicit DemoLocal(int value, async_simple::Signal* signal = nullptr)
        : LazyLocalBase(&tag, signal), value(value) {}

    static bool classof(const coro::LazyLocalBase* base) {
        return base->getTypeTag() == &tag;
    }

    int value = 0;
};

// 最小示例：读协程 local，做一点耗时工作，然后返回结果。
coro::Lazy<int> delayed_value(int value, int delayMs) {
    auto* local = co_await coro::CurrentLazyLocals<DemoLocal>{};
    std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
    co_return value + (local ? local->value : 0);
}

// 这个任务专门演示 `Yield`：
// 协程主动挂起，把后续恢复重新交还给执行器安排。
coro::Lazy<int> yielded_value(int value, int yields) {
    for (int i = 0; i < yields; ++i) {
        co_await coro::Yield{};
    }
    co_return value;
}

// 并发收集一组 `Lazy<int>`，观察多条协程链如何汇总到一个外层等待点。
coro::Lazy<int> collect_demo(async_simple::Executor* executor) {
    std::vector<coro::Lazy<int>> tasks;
    tasks.push_back(delayed_value(1, 40));
    tasks.push_back(delayed_value(2, 20));
    tasks.push_back(delayed_value(3, 10));

    auto results = co_await coro::collectAllPara(std::move(tasks)).via(executor);
    int sum = 0;
    for (auto& result : results) {
        sum += std::move(result).value();
    }
    co_return sum;
}

// 窗口化收集：限制同时推进的任务数，分批驱动协程继续执行。
coro::Lazy<int> collect_windowed_demo(async_simple::Executor* executor) {
    std::vector<coro::Lazy<int>> tasks;
    tasks.push_back(delayed_value(1, 10));
    tasks.push_back(delayed_value(2, 10));
    tasks.push_back(delayed_value(3, 10));
    tasks.push_back(delayed_value(4, 10));

    auto results = co_await coro::collectAllWindowed(2, true, std::move(tasks)).via(executor);
    int sum = 0;
    for (auto& result : results) {
        sum += std::move(result).value();
    }
    co_return sum;
}

// `collectAny` 只关心第一条完成的协程链。
coro::Lazy<int> collect_any_demo(async_simple::Executor* executor) {
    std::vector<coro::RescheduleLazy<int>> tasks;
    tasks.push_back(yielded_value(10, 4).via(executor));
    tasks.push_back(yielded_value(20, 1).via(executor));
    tasks.push_back(yielded_value(30, 2).via(executor));
    auto first = co_await coro::collectAny(std::move(tasks));
    co_return std::move(first).value();
}

// `detach()` 代表根调用方不再保留返回值，只观察协程自己被调度和恢复。
coro::Lazy<void> detached_demo(async_simple::Executor* executor) {
    auto* ex = co_await async_simple::CurrentExecutor{};
    std::cout << "detached current executor bound = " << (ex == executor) << '\n';
    co_await coro::Yield{};
    std::cout << "detached resumed on executor thread = "
              << executor->currentThreadInExecutor() << '\n';
}

struct CvState {
    std::shared_ptr<coro::Mutex> mutex = std::make_shared<coro::Mutex>();
    std::shared_ptr<coro::ConditionVariable<coro::Mutex>> cv =
        std::make_shared<coro::ConditionVariable<coro::Mutex>>();
    int value = 0;
};

coro::Lazy<int> cv_waiter_task(std::shared_ptr<CvState> state) {
    auto lock = co_await state->mutex->coScopedLock();
    co_await state->cv->wait(lock, [&] { return state->value >= 3; });
    co_return state->value;
}

coro::Lazy<void> cv_producer_task(std::shared_ptr<CvState> state) {
    for (int i = 0; i < 3; ++i) {
        {
            auto lock = co_await state->mutex->coScopedLock();
            ++state->value;
            state->cv->notifyOne();
        }
        co_await coro::Yield{};
    }
}

coro::Lazy<int> condition_variable_demo(async_simple::Executor* executor) {
    auto state = std::make_shared<CvState>();
    cv_producer_task(state).via(executor).detach();
    co_return co_await cv_waiter_task(std::move(state)).via(executor);
}

coro::Lazy<void> latch_countdown_task(std::shared_ptr<coro::Latch> latch) {
    co_await coro::Yield{};
    co_await latch->countDown();
}

coro::Lazy<int> latch_demo(async_simple::Executor* executor) {
    auto latch = std::make_shared<coro::Latch>(3);
    std::vector<coro::RescheduleLazy<void>> tasks;
    for (int i = 0; i < 3; ++i) {
        tasks.push_back(latch_countdown_task(latch).via(executor));
    }

    co_await coro::collectAll(std::move(tasks));
    auto done = co_await latch->tryWait();
    co_return done ? 1 : 0;
}

coro::Lazy<int> semaphore_consumer_task(
    std::shared_ptr<coro::CountingSemaphore<4>> semaphore) {
    int acquired = 0;
    for (int i = 0; i < 3; ++i) {
        co_await semaphore->acquire();
        ++acquired;
    }
    co_return acquired;
}

coro::Lazy<int> semaphore_producer_task(
    std::shared_ptr<coro::CountingSemaphore<4>> semaphore) {
    co_await coro::Yield{};
    co_await semaphore->release(2);
    co_await coro::Yield{};
    co_await semaphore->release();
    co_return 0;
}

coro::Lazy<int> semaphore_demo(async_simple::Executor* executor) {
    auto semaphore = std::make_shared<coro::CountingSemaphore<4>>(0);

    std::vector<coro::RescheduleLazy<int>> tasks;
    tasks.push_back(semaphore_consumer_task(semaphore).via(executor));
    tasks.push_back(semaphore_producer_task(semaphore).via(executor));
    auto results = co_await coro::collectAllPara(std::move(tasks));

    co_return std::move(results[0]).value();
}

coro::Lazy<int> future_bridge_demo(async_simple::Executor* executor) {
    async_simple::Promise<int> promise;
    auto future = promise.getFuture();
    std::thread producer([promise = std::move(promise)]() mutable {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        promise.setValue(99);
    });
    producer.detach();

    auto value = co_await std::move(future);
    auto* currentExecutor = co_await async_simple::CurrentExecutor{};
    co_return currentExecutor == executor ? value : -1;
}

int main() {
    // 这里准备两个执行器，分别服务普通并发 demo 和单线程顺序更稳定的 collectAny demo。
    async_simple::executors::SimpleExecutor executor(4);
    async_simple::executors::SimpleExecutor collectAnyExecutor(1);

    // local 绑定在协程链上，所以三个子任务都会继承 `DemoLocal(5)`。
    auto sum = coro::syncAwait(
        collect_demo(&executor).setLazyLocal<DemoLocal>(5).via(&executor));
    std::cout << "collectAllPara sum = " << sum << '\n';

    // 第一条完成的任务结果会直接返回给外层。
    auto first = coro::syncAwait(collect_any_demo(&collectAnyExecutor));
    std::cout << "collectAny first value = " << first << '\n';

    // 这里同样演示 local 继承，只是运行方式换成窗口化收集。
    auto windowed =
        coro::syncAwait(collect_windowed_demo(&executor).setLazyLocal<DemoLocal>(1).via(&executor));
    std::cout << "collectAllWindowed sum = " << windowed << '\n';

    auto cvValue = coro::syncAwait(condition_variable_demo(&executor));
    std::cout << "conditionVariable observed value = " << cvValue << '\n';

    auto latchDone = coro::syncAwait(latch_demo(&executor));
    std::cout << "latch completed = " << latchDone << '\n';

    auto semaphoreAcquired = coro::syncAwait(semaphore_demo(&executor));
    std::cout << "semaphore acquired count = " << semaphoreAcquired << '\n';

    auto futureBridgeValue = coro::syncAwait(future_bridge_demo(&executor).via(&executor));
    std::cout << "future bridge value = " << futureBridgeValue << '\n';

    // `directlyStart` 提供了“显式启动，但不阻塞等待”的根入口。
    auto onCompleted = [](auto result) {
        std::cout << "non-blocking callback value = " << std::move(result).value()
                  << '\n';
    };

    delayed_value(7, 5)
        .setLazyLocal<DemoLocal>(1)
        .directlyStart(std::move(onCompleted), &executor);

    // `detach()` 之后 main 不持有结果；这里只是为了让示例输出有机会刷出来。
    detached_demo(&executor).via(&executor).detach();

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    return 0;
}
