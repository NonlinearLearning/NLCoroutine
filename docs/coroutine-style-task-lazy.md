# 任务型协程：`Lazy<T>` / `RescheduleLazy<T>`

任务型协程是这个仓库的主线，也是最值得先读透的一种风格。

如果只用一句话描述它，可以说：

`Lazy<T>` 表示“一个惰性的协程任务”；它不是值本身，而是一个未来可以被启动、被等待、
最终完成为 `T` 或异常的计算过程。

这篇文档从使用者视角解释它的语义。想顺着执行链进源码，请接着读
[reading-guide-lazy-framework.md](D:/ProjectItem/SourceCode/NLCoroutine/docs/reading-guide-lazy-framework.md:1)。

## 1. `Lazy<T>` 到底是什么

先把几个容易混淆的说法拆开：

- 它不是结果值
- 它不是“函数已经跑完后留下的句柄”
- 它也不是单纯的 future 结果槽位

它更像一份“还没开始或还没完成的协程任务描述”。

当一个协程函数返回 `Lazy<T>` 时，调用方拿到的是一个可以被组合的任务对象。这个对象可以：

- 被 `co_await`
- 被 `start(callback)` 非阻塞启动
- 被 `syncAwait(...)` 同步等待
- 被 `collectAllPara`、`collectAny`、`collectAllWindowed` 这类组合器批量协调

这也是为什么任务型协程通常适合做异步业务主线：它天然表达“先等 A，再做 B，再并发推进 C/D/E，最后汇总结果”。

## 2. 它和生成器、Future/Promise 有什么区别

### 和生成器的区别

生成器解决的是“逐个产出很多个值”。
任务型协程解决的是“最终完成一个值”。

所以：

- 生成器强调 `co_yield`
- 任务型协程强调 `co_await`

如果你关心的是“异步子任务怎么接起来”，应该看 `Lazy<T>`；如果你关心的是“如何按需枚举序列”，那是生成器的问题。

### 和 Future/Promise 的区别

Future/Promise 更像共享状态和完成通知。

任务型协程则更像结构化控制流。你在代码里写出的 `co_await` 链，本身就是控制流关系：

- 父协程等子协程
- 子协程完成后恢复父协程
- 异常沿着协程链传播

这使它比纯 Future/Promise 更适合承载“顺序但异步”的业务逻辑。

## 3. `Lazy<T>` 为什么是惰性的

这个仓库里，`Lazy<T>` 的默认语义是 lazy。

含义是：

- 调用协程函数时，只创建协程帧
- 函数体不会立刻往下执行
- 必须通过显式入口把它启动起来

这些入口主要有三类：

1. `co_await someLazy()`
2. `someLazy.start(callback)`
3. `syncAwait(someLazy)`

所以，看到一个返回 `Lazy<int>` 的函数时，你不能把它理解成“调用即执行”，而要理解成“调用即构造任务对象”。

这点和 `cppcoro::task<T>` 的常见语义是一致的，也是当前仓库所有组合器成立的前提。

## 4. 父子协程是怎么连起来的

当你写下：

```cpp
int value = co_await delayed_value(1, 10);
```

表面上像普通顺序代码，底层其实发生了两件关键的事：

1. 父协程在等待点挂起
2. 子协程记住“我结束后要恢复谁”

也就是说，任务型协程真正的核心不是“暂停”这一个动作，而是“暂停之后，控制流如何有序接回去”。

本仓库的实现把这条链建立在 continuation 上：

- `await_suspend()` 记录父协程 continuation
- `final_suspend()` 把控制权交还给 continuation

理解这条链以后，你再看一串 `co_await`，就不会把它当成“神秘魔法”，而会把它看成一串明确连接起来的协程帧。

## 5. `RescheduleLazy<T>` 比 `Lazy<T>` 多了什么

`RescheduleLazy<T>` 不是另一种完全不同的任务模型，它仍然是任务型协程。

区别只在于恢复路径。

### `Lazy<T>`

- 默认走更直接的对称转移
- 子协程被等待时，控制流可以更直接地转入子协程

### `RescheduleLazy<T>`

- 不直接沿用对称转移继续跑
- 恢复前会先把动作投递给 `Executor`

这意味着 `RescheduleLazy<T>` 的重点不是“它更异步”，而是“它的恢复动作显式经过调度器”。

在文档和代码里都要牢牢记住一句话：

`.via(executor)` 的本质不是“现在立刻切线程”，而是“修改后续恢复路径，让恢复动作交给执行器安排”。

## 6. `.via(executor)` 到底改变了什么

这是任务型协程最容易被误解的地方。

很多初学者会把 `.via(executor)` 理解成“调用到这里就切到线程池”。这个理解太粗了。

更准确的说法是：

- 当前 `Lazy` 绑定了一个 `Executor`
- 后续需要恢复该协程帧时，恢复动作会先交给这个执行器
- 至于什么时候恢复、在哪个线程恢复，由执行器决定

所以 `.via(executor)` 解决的是“恢复路径的调度归属”，不是一个瞬时的“切线程指令”。

这也是为什么本仓库把 `Executor` 放在非常明确的库层位置上：

- 协程语言本身不提供线程池或事件循环
- `Executor` 是这个项目补出来的运行时接口

## 7. `LazyLocal` 和取消信号属于哪一层

任务型协程一旦能挂起和恢复，就会出现一个工程问题：

如果协程会跨线程恢复，那业务上下文还应该挂在线程上吗？

这个仓库给出的答案是：不要只依赖线程上下文，而要把上下文挂到协程链上。

因此：

- `LazyLocal` 负责沿 `co_await` 链传播本地上下文
- `CurrentLazyLocals<T>` 负责读取当前协程链上的 local
- `Signal/Slot` 负责把取消意图附着到这条协程链里

这也是 `demo/lazy_demo.cpp` 里 `DemoLocal` 的演示重点：
子协程能读到外层协程设置的 local，不是因为它们恰好跑在同一个线程，而是因为它们属于同一条协程链。

## 8. 组合能力才是任务型协程真正的价值

只看单个 `co_await`，任务型协程像是“写法更顺序的异步函数”。

但它真正强的地方在组合。

这个仓库里最值得关注的几个组合 API 是：

- `collectAllPara`：并发推进一组任务，全部完成后统一返回
- `collectAny`：谁先完成就先把谁的结果交出来
- `collectAllWindowed`：限制同时推进的任务数，窗口化执行

这些 API 说明了一件很重要的事：

任务型协程不是为了“把回调换成 `co_await` 写法”这么简单，它本质上是在提供一种更结构化的异步控制流拼装方式。

## 9. `syncAwait()` 应该怎么理解

`syncAwait()` 很常见，也最容易被误用。

在这个仓库里，它的定位非常明确：

- 它不是协程机制的一部分
- 它不是调度器
- 它只是“同步世界进入协程世界”的桥

它做的事很简单：

1. 启动一个 `Lazy`
2. 在完成回调里保存结果
3. 当前线程阻塞，直到结果就绪

所以它适合：

- demo
- 测试
- `main()` 入口
- 少量同步桥接场景

它不适合被当成“协程内部常规调用方式”，更不适合在已经绑定的执行器工作线程里再去阻塞等待自己。

## 10. 一个最小阅读地图

如果你只想快速建立正确心智模型，建议顺着下面顺序看：

1. [demo/lazy_demo.cpp](D:/ProjectItem/SourceCode/NLCoroutine/demo/lazy_demo.cpp:1)
2. [async_simple/coro/lazy.cppm](D:/ProjectItem/SourceCode/NLCoroutine/async_simple/coro/lazy.cppm:1)
3. [async_simple/coro/sync_await.cppm](D:/ProjectItem/SourceCode/NLCoroutine/async_simple/coro/sync_await.cppm:1)
4. [async_simple/executor.cppm](D:/ProjectItem/SourceCode/NLCoroutine/async_simple/executor.cppm:1)
5. [reading-guide-lazy-framework.md](D:/ProjectItem/SourceCode/NLCoroutine/docs/reading-guide-lazy-framework.md:1)

看这条线时，重点确认五个问题：

- 协程函数返回 `Lazy<T>` 时，什么时候真正执行函数体
- 父协程在等待子协程时，谁记录 continuation
- 子协程结束时，谁负责恢复父协程
- `.via(executor)` 改变的是哪一层语义
- `syncAwait()` 为什么只是同步桥，而不是协程核心机制

## 11. 这类风格适合什么场景

任务型协程特别适合这些场景：

- 异步控制流有明确先后依赖
- 想把异步逻辑写成接近同步的顺序代码
- 需要清楚控制“何时开始执行”
- 需要把恢复路径交给执行器控制
- 需要在一条协程链里传播上下文、取消状态和组合结果

如果你的问题本质上是“一个异步任务最终会完成什么”，那么 `Lazy<T>` 这类抽象通常比裸 Future/Promise 或手写回调更合适。

## 12. 当前实现的定位

最后要记住一个边界：这个仓库是教学型、可运行、可追踪的最小实现，不是完整工业协程框架。

它现在重点讲清的是：

- 惰性任务对象
- continuation 驱动的父子协程连接
- executor 参与恢复路径
- local / cancel / collect 这些围绕协程链展开的工程语义

如果你把这些主线理解透，再去看成熟工业库里的 `task<T>`、`Task<T>`、`generator<T>`、`async_generator<T>`，迁移成本会低很多。
