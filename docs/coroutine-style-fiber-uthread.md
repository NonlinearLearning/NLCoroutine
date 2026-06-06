# 纤程 / 用户态线程 / 栈式协程风格

这一类风格和 C++20 语言协程关系很近，但不是同一种东西。

最容易混淆的点是：

- `co_await`/`co_yield` 那套通常是 stackless coroutine
- fiber / uthread 通常是 stackful coroutine

WG21 N4134 对这件事有非常直接的定义：

- stackless coroutine 不包含调用栈
- stackful coroutine / fiber / user-mode thread 包含完整调用栈，可以从更深的嵌套帧里挂起

## 1. 这类风格的定义

fiber/uthread 的典型特征是：

- 每个执行单元有自己的栈
- 调度发生在用户态
- 一个线程上可以跑多个 fiber
- 切换时保存/恢复的是整套执行上下文，而不只是单个协程帧

Windows 官方文档对 fibers 的定义也很明确：

- 一个线程可以调度多个 fiber
- 在调度第一个 fiber 前，线程要先转换成 fiber

## 2. 为什么说它像线程

因为它有完整调用栈，所以它的编程体验常常更接近：

- “看起来像同步阻塞代码”
- 但阻塞点实际上是用户态切换

这和 stackless coroutine 的区别很大。
stackless coroutine 只能在显式协程边界 suspend。
fiber 则可以在更深的调用路径上切出去。

## 3. 本仓库里的对应实现

核心入口：

- [async_simple/uthread/uthread.cppm](D:/ProjectItem/SourceCode/NLCoroutine/async_simple/uthread/uthread.cppm:1)
- [async_simple/uthread/runtime.cppm](D:/ProjectItem/SourceCode/NLCoroutine/async_simple/uthread/runtime.cppm:1)
- [async_simple/uthread/async.cppm](D:/ProjectItem/SourceCode/NLCoroutine/async_simple/uthread/async.cppm:1)
- [async_simple/uthread/await.cppm](D:/ProjectItem/SourceCode/NLCoroutine/async_simple/uthread/await.cppm:1)
- [demo/uthread_guard_demo.cpp](D:/ProjectItem/SourceCode/NLCoroutine/demo/uthread_guard_demo.cpp:1)

从代码上看，这个实现有几个关键信号：

- `kUthreadAvailable` 目前只在 Windows 上为 `true`
- `Uthread` 构造时创建 `FiberContext`
- `resumeFiber()` / `suspendCurrent()` 负责切换
- `uthread::await(Future<T>&&)` 会在 fiber 内等待 future 完成，然后恢复当前 fiber

这说明它本质上不是语言协程语法糖，而是构建在 fiber/runtime 之上的用户态调度模型。

## 4. 它和 `Lazy<T>` 的差别

### `Lazy<T>` / task 风格

- stackless
- 依赖 `promise_type`、awaiter、continuation
- 挂起点必须是协程语义显式允许的地方

### `uthread` / fiber 风格

- stackful
- 依赖 fiber context / user-mode scheduling
- 更接近“同步代码写异步逻辑”

所以两者不是谁替代谁，而是取舍不同。

## 5. 这种风格适合什么场景

- 你想保留同步函数调用风格
- 你需要在更深的调用栈里挂起
- 你愿意为独立栈和运行时切换付出额外成本

它尤其适合那类“原本写成阻塞式更自然”的流程。

## 6. 优势和代价

优势：

- 编程模型接近普通同步代码
- 可以从嵌套调用栈中切换
- 迁移某些旧式阻塞逻辑时更自然

代价：

- 每个执行单元要有栈，内存成本更高
- 运行时和平台绑定更强
- 调度、同步、栈大小、可移植性都更复杂

WG21 早期设计里也专门讨论过 stackless 与 stackful 的权衡：
stackless 更容易扩展到海量并发，stackful 则在“保留完整调用栈”方面更强。

## 7. 在这个仓库里怎么读

建议顺序：

1. [demo/uthread_guard_demo.cpp](D:/ProjectItem/SourceCode/NLCoroutine/demo/uthread_guard_demo.cpp:1)
2. [async_simple/uthread/uthread.cppm](D:/ProjectItem/SourceCode/NLCoroutine/async_simple/uthread/uthread.cppm:1)
3. [async_simple/uthread/async.cppm](D:/ProjectItem/SourceCode/NLCoroutine/async_simple/uthread/async.cppm:1)
4. [async_simple/uthread/await.cppm](D:/ProjectItem/SourceCode/NLCoroutine/async_simple/uthread/await.cppm:1)
5. [async_simple/uthread/runtime.cppm](D:/ProjectItem/SourceCode/NLCoroutine/async_simple/uthread/runtime.cppm:1)

## 8. 推荐阅读资料

- Windows Fibers：
  https://learn.microsoft.com/en-us/windows/win32/procthread/fibers
- Boost.Coroutine2：
  https://www.boost.org/doc/libs/latest/libs/coroutine2/index.html
- Boost.Context / fiber：
  https://www.boost.org/library/latest/context/
- WG21 N4134：
  https://isocpp.org/files/papers/N4134.pdf
