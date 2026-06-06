# Lazy 协程阅读指南

这份指南不重复解释“任务型协程是什么”，而是回答另一个问题：

文档里提到的那些语义，在这个仓库的代码里到底是怎么落地的？

如果你还没有建立 `Lazy<T>` / `RescheduleLazy<T>` 的使用者心智模型，先读
[coroutine-style-task-lazy.md](D:/ProjectItem/SourceCode/NLCoroutine/docs/coroutine-style-task-lazy.md:1)。

这里默认你已经接受三件事：

- `Lazy<T>` 表示一个惰性协程任务
- 父子协程通过 continuation 接起来
- `.via(executor)` 修改的是恢复路径，而不是“调用点立刻切线程”

在这套前提下，再顺着执行链读代码，很多实现细节就不会显得零碎。

## 1. 从 demo 开始，不要先扎进模板

第一入口仍然是
[demo/lazy_demo.cpp](D:/ProjectItem/SourceCode/NLCoroutine/demo/lazy_demo.cpp:1)。

这份 demo 基本把当前主线能力都串了一遍：

- `delayed_value()`：最小 `Lazy<int>` 任务
- `yielded_value()`：演示 `Yield`
- `collect_demo()`：并发收集
- `collect_windowed_demo()`：窗口化收集
- `collect_any_demo()`：谁先完成谁先返回
- `detached_demo()`：根协程 fire-and-forget
- `condition_variable_demo()` / `latch_demo()` / `semaphore_demo()`：协程同步原语
- `future_bridge_demo()`：Future 和协程之间的桥接

第一遍不要急着逐行抠模板实现，只回答下面几个问题：

- 为什么调用协程函数返回的是 `Lazy<T>` 而不是值
- 为什么 `.via(&executor)` 后恢复路径会经过执行器
- 为什么 `setLazyLocal<DemoLocal>(...)` 设置的上下文能被子协程看到
- 为什么 `syncAwait(...)` 能把协程结果变回同步返回值

这些问题明确以后，再进实现文件不会迷路。

## 2. 先看 `Lazy` 的骨架，而不是所有功能点

核心文件是
[async_simple/coro/lazy.cppm](D:/ProjectItem/SourceCode/NLCoroutine/async_simple/coro/lazy.cppm:1)。

建议第二步只盯四个部件：

1. `LazyPromiseBase`
2. `LazyPromise<T>` / `LazyPromise<void>`
3. `LazyBase<T, Reschedule>`
4. `Lazy<T>` / `RescheduleLazy<T>`

先把职责分层看清楚：

- `promise_type` 负责协程帧内状态
- awaiter 负责等待协议和 continuation 连接
- `LazyBase` 负责任务对象的持有、启动和转换
- `Lazy<T>` / `RescheduleLazy<T>` 负责对外语义差异

如果先从这个层次读，后面遇到 `Yield`、`CurrentLazyLocals`、`Result<T>` 都更容易定位。

## 3. `promise_type` 是协程帧的运行时接口

`LazyPromiseBase` 是最关键的“帧头部”。

这里集中保存了三类状态：

- `_continuation`：当前协程结束后恢复谁
- `_executor`：当前协程恢复前交给哪个执行器
- `_lazyLocal` / `_lazyLocalOwner`：当前协程链上的本地上下文

看这部分代码时，不要把它理解成“某个普通业务对象的成员”，而要把它理解成：

编译器生成的协程状态机，在运行时会反复访问这里。

这也是为什么文档里一直强调 `promise_type` 是语言和库之间的接口边界。

## 4. `initial_suspend()` 决定了惰性

`LazyPromiseBase::initial_suspend()` 返回的是 `std::suspend_always`。

这意味着：

- 调用协程函数时，先构造协程帧
- 函数体不会立刻执行到第一个业务语句
- 必须等显式启动入口推进它

这个实现细节直接支撑了文档里的语义描述：`Lazy<T>` 是一个惰性任务对象，而不是“调用即执行”的异步函数。

你读到这里时，应该能把“文档里的 lazy 语义”与“代码里的 `initial_suspend()`”一一对上。

## 5. `operator co_await()` 把任务接入等待协议

`LazyBase` 里的 `operator co_await()` 是任务对象进入语言 await 协议的入口。

当外层写：

```cpp
co_await someLazy();
```

真正参与运行的是 awaiter，而不是 `Lazy` 对象本身。需要重点看三段：

- `await_ready()`
- `await_suspend()`
- `await_resume()`

在这个仓库里，`LazyAwaiterBase` 负责结果提取和句柄生命周期；
`AwaiterBase` 则负责 continuation、executor 继承和是否重调度。

这一层是整个实现的控制流核心。

## 6. continuation 真正建立在 `await_suspend()` 和 `final_suspend()` 之间

如果只允许你盯住两处代码，那就是这两处：

1. `AwaiterBase::await_suspend()`
2. `LazyPromiseBase::FinalAwaiter::await_suspend()`

第一处做的是：

- 父协程准备挂起
- 把父协程句柄记到子协程 promise 的 `_continuation`
- 必要时把 executor 和 local 沿协程链向下继承

第二处做的是：

- 子协程执行完
- 从 promise 里取出 `_continuation`
- 把控制流交还给 continuation

这两步连起来，就是“一串 `co_await` 为什么能自动接成一条任务链”的真实原因。

## 7. `Lazy` 和 `RescheduleLazy` 的分叉点在哪里

很多语义差异都汇聚在 `LazyBase<T, Reschedule>::AwaiterBase::await_suspend()` 里。

这里有一条关键分支：

- `Reschedule == false`：直接返回子协程句柄，走更直接的控制流转移
- `Reschedule == true`：把恢复动作投递给 `Executor`，然后返回 `true`

这就是 `Lazy<T>` 与 `RescheduleLazy<T>` 的本质差异。

它不是两个完全不同的抽象体系，而是同一类任务对象在“恢复动作是否显式重调度”上的差别。

## 8. `.via(executor)` 只是绑定恢复归属

`Lazy<T>::via(Executor*)` 的实现很短，但语义非常重：

- 先把执行器绑定到当前 promise
- 再把任务对象包装成 `RescheduleLazy<T>`

这里最值得确认的是：`.via()` 自身没有把协程立即跑起来。

它只是提前写好一个约束：

后面谁来恢复这个协程帧，先走这个执行器。

所以你在读实现时，不要把 `.via()` 看成“执行动作”，而要看成“恢复策略声明”。

## 9. `Yield` 是库层扩展，不是语言关键字

`Yield` 在这个仓库里很重要，因为它把“当前协程主动让出执行机会，再由执行器继续安排恢复”这件事显式化了。

实现上有两层：

1. `await_transform(Yield)` 把它转成 `YieldAwaiter`
2. `YieldAwaiter::await_suspend()` 把当前协程句柄重新投递给执行器

这里再次说明一件事：

协程语言只提供 suspend/resume 机制；
“让出一次执行并以较低优先级恢复”这种行为，是库在运行时层额外定义出来的。

## 10. `syncAwait()` 是同步桥，不是协程核心

文件在
[async_simple/coro/sync_await.cppm](D:/ProjectItem/SourceCode/NLCoroutine/async_simple/coro/sync_await.cppm:1)。

读它时要刻意保持分层意识：

- 它不负责协程帧的生成
- 不负责 continuation 链接
- 不负责 executor 设计

它只做同步桥接：

1. 准备一个条件变量式等待器
2. 调用 `Lazy::start(callback)` 启动任务
3. 回调里保存结果并唤醒当前线程
4. 当前线程阻塞等待直到结果就绪

如果把这层职责想清楚，你就不会把 `syncAwait()` 误看成“协程内部不可缺少的核心机制”。

## 11. `start(callback)` 说明裸 `Lazy` 不会自己跑

`LazyBase::start()` 值得单独看一遍。

它的含义是：

- 当前只有一个 `Lazy` 任务对象
- 它还没有被外层协程 `co_await`
- 需要一个根入口把它启动起来

这个实现通过一个 `DetachedCoroutine` 根协程去等待 `self.coAwaitResult()`，最后把结果交给回调。

这能很好地验证文档里的一个说法：

单独的 `Lazy` 只有“协程帧 + 任务对象”还不够，它需要被某个入口真正驱动起来。

## 12. `Executor` 是运行时层，不是语言层

文件在
[async_simple/executor.cppm](D:/ProjectItem/SourceCode/NLCoroutine/async_simple/executor.cppm:1)。

这份代码最重要的阅读目标不是 API 细节，而是分层定位：

- 协程语言并不自带线程池和调度器
- `Executor` 是这个教学框架补上的恢复投递接口

重点看：

- `schedule(Func)`
- `schedule(coroutine_handle, scheduleInfo)`
- `currentThreadInExecutor()`
- `after(...)`

这些接口共同表达的是：

协程帧什么时候、在哪恢复，是库运行时层的责任，而不是语言关键字直接决定的。

## 13. `LazyLocal` 证明上下文是“跟协程链走”，不是“跟线程走”

如果你想验证文档里关于 local 的说法，建议对照两处看：

1. `demo/lazy_demo.cpp` 里的 `DemoLocal`
2. `lazy.cppm` 里 promise 对 `_lazyLocal` / `_lazyLocalOwner` 的处理

关键观察点是：

- 外层通过 `setLazyLocal<DemoLocal>(...)` 绑定 local
- 子协程通过 `co_await CurrentLazyLocals<DemoLocal>{}` 读取
- await_suspend 里会把 local 沿 `co_await` 链向下继承

这条链证明确实不是 thread-local 语义，而是 coroutine-local 语义。

## 14. 一条推荐的“执行链阅读法”

当你已经理解模块职责后，第三遍建议按真实控制流走：

1. 从 `main()` 里的某个 `syncAwait(...)` 开始
2. 进入 `syncAwait()`
3. 进入 `LazyBase::start()`
4. 跟到根协程等待 `coAwaitResult()`
5. 进入 `operator co_await()` / awaiter
6. 看 `AwaiterBase::await_suspend()`
7. 看子协程 `final_suspend()`
8. 回到完成回调，再回到 `syncAwait()`

沿这条线，重点确认四件事：

- 协程帧是什么时候创建的
- 真正开始执行是什么时候
- 父子协程在哪里接起来
- 结果是怎么回到同步调用者手里的

## 15. 读完这条线后再扩展到组合器

只有在主线打通之后，再去看这些模块才最有效：

- [async_simple/coro/collect.cppm](D:/ProjectItem/SourceCode/NLCoroutine/async_simple/coro/collect.cppm:1)
- [async_simple/coro/lazy_local.cppm](D:/ProjectItem/SourceCode/NLCoroutine/async_simple/coro/lazy_local.cppm:1)
- [async_simple/signal.cppm](D:/ProjectItem/SourceCode/NLCoroutine/async_simple/signal.cppm:1)

否则你会被很多“配套能力”淹没，看不出主线。

正确顺序应该是：

1. 先确认单条 `Lazy` 协程链如何运行
2. 再确认 executor 如何参与恢复
3. 最后再看 local、cancel、collect 如何围绕这条链扩展

这样阅读，整个仓库会从“很多模板和小技巧”变成“一条完整、可验证的协程运行时主链”。
