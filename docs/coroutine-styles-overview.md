# 协程风格总览

这组文档不按语法关键字分，而是按“库对外提供什么语义”分。

C++20 协程语言本身只提供一套底层机制：`co_await`、`co_yield`、`co_return`、
`promise_type`、`coroutine_handle`。真正决定用户怎么写代码、怎么理解控制流的，
不是语言关键字本身，而是库在这套机制之上定义出来的抽象。

这个仓库的目标不是做一份“协程语法笔记”，而是把几种常见协程风格拆开，让你知道：

- 它们分别在解决什么问题
- 它们的运行模型有什么差异
- 当前仓库重点实现了哪一类
- 如果想继续深挖，应该从哪里进源码

## 仓库主线：任务型协程

如果你只打算先理解这个仓库，请把注意力集中在任务型协程上。

- 代表类型：`Lazy<T>`、`RescheduleLazy<T>`
- 核心操作：`co_await`
- 心智模型：返回“一个将来会完成的协程任务”
- 仓库入口：
  [async_simple/coro/lazy.cppm](D:/ProjectItem/SourceCode/NLCoroutine/async_simple/coro/lazy.cppm:1)
  [demo/lazy_demo.cpp](D:/ProjectItem/SourceCode/NLCoroutine/demo/lazy_demo.cpp:1)
- 主文档：
  [coroutine-style-task-lazy.md](D:/ProjectItem/SourceCode/NLCoroutine/docs/coroutine-style-task-lazy.md:1)
- 源码阅读指南：
  [reading-guide-lazy-framework.md](D:/ProjectItem/SourceCode/NLCoroutine/docs/reading-guide-lazy-framework.md:1)

这条主线覆盖了本仓库最重要的几个问题：

- 惰性协程什么时候真正开始执行
- 父子协程如何通过 continuation 接起来
- `.via(executor)` 到底改变了什么
- `syncAwait()` 如何把异步结果桥接回同步世界
- 多条协程链如何用 `collectAllPara`、`collectAny`、`collectAllWindowed` 组合

如果这条线读通了，仓库里其他协程风格就容易放到正确位置上理解。

## 1. 任务型协程

- 代表类型：`Lazy<T>`、`RescheduleLazy<T>`
- 关键词：`co_await`
- 对外语义：最终交付一个完成结果，或者一个异常
- 典型问题：如何描述一个异步计算，并把它和其他协程任务组合起来

这类风格最接近 `cppcoro::task<T>`、`folly::coro::Task<T>` 这类工业库里的主力抽象。
它不是“立刻求值”，而是“先形成一个任务对象，等被启动或被等待时再推进”。

在这个仓库里，`Lazy<T>` 是默认惰性的；`RescheduleLazy<T>` 则进一步把恢复路径显式交给
`Executor`。这也是整个实现里最值得反复确认的一条分层：

- 协程语言负责“帧可以挂起和恢复”
- `Lazy`/awaiter 负责“父子协程怎么接起来”
- `Executor` 负责“恢复动作由谁调度、在哪个线程发生”

## 2. Future/Promise 风格

- 代表类型：`Future<T>`、`Promise<T>`
- 关键词：共享状态、完成通知、生产者/消费者
- 对外语义：生产者写结果，消费者等待结果
- 入口：
  [async_simple/future.cppm](D:/ProjectItem/SourceCode/NLCoroutine/async_simple/future.cppm:1)
  [demo/future_promise_demo.cpp](D:/ProjectItem/SourceCode/NLCoroutine/demo/future_promise_demo.cpp:1)
- 文档：
  [coroutine-style-future-promise.md](D:/ProjectItem/SourceCode/NLCoroutine/docs/coroutine-style-future-promise.md:1)

它和任务型协程关系很近，但重点不同：

- 任务型更强调“控制流结构化组合”
- Future/Promise 更强调“结果槽位和完成同步”

在这个仓库里，`future_bridge_demo()` 还演示了两者之间的桥接：
一个 `Future<int>` 可以被协程 `co_await`，从而接回任务型协程链。

## 3. 同步生成器风格

- 代表类型：`generator<T>`、`std::generator<T>`
- 关键词：`co_yield`
- 对外语义：按需逐个产出值
- 文档：
  [coroutine-style-generator.md](D:/ProjectItem/SourceCode/NLCoroutine/docs/coroutine-style-generator.md:1)

生成器和任务型协程很容易被新读者混在一起，但它们解决的是不同问题：

- 任务型协程关心“最终完成一个结果”
- 生成器关心“逐步产出一串值”

这类风格适合迭代、遍历、数据流拆分，不适合直接拿来表达“等一个异步任务完成”。

## 4. 异步生成器风格

- 代表类型：`async_generator<T>`
- 关键词：`co_await` + `co_yield`
- 对外语义：一边异步等待，一边逐个产出元素
- 文档：
  [coroutine-style-async-generator.md](D:/ProjectItem/SourceCode/NLCoroutine/docs/coroutine-style-async-generator.md:1)

这类风格经常出现在流式 IO、分页抓取、消息管道、增量解析场景里。

当前仓库没有实现完整的异步生成器，但理解它的语义边界有助于你区分：

- “一个任务最后完成一次”
- “一个源不断地产出很多次”

## 5. 纤程 / 用户态线程 / 栈式协程风格

- 代表类型：fiber、uthread、stackful coroutine
- 关键词：单独的运行时、上下文切换、独立调用栈
- 对外语义：更像轻量线程，而不是 `co_await` 链
- 入口：
  [async_simple/uthread/uthread.cppm](D:/ProjectItem/SourceCode/NLCoroutine/async_simple/uthread/uthread.cppm:1)
  [demo/uthread_guard_demo.cpp](D:/ProjectItem/SourceCode/NLCoroutine/demo/uthread_guard_demo.cpp:1)
- 文档：
  [coroutine-style-fiber-uthread.md](D:/ProjectItem/SourceCode/NLCoroutine/docs/coroutine-style-fiber-uthread.md:1)

它和 C++20 语言协程不是一回事。

这里最重要的区分是：

- `Lazy<T>` 这类是 stackless coroutine，控制流围绕协程帧和 awaiter 展开
- fiber/uthread 是 stackful 协作式调度，任务拥有自己的调用栈

两者都常被统称“协程”，但工程上的运行模型差很多。

## 推荐阅读顺序

如果你主要想理解这个仓库，建议按下面顺序读：

1. [coroutine-style-task-lazy.md](D:/ProjectItem/SourceCode/NLCoroutine/docs/coroutine-style-task-lazy.md:1)
2. [reading-guide-lazy-framework.md](D:/ProjectItem/SourceCode/NLCoroutine/docs/reading-guide-lazy-framework.md:1)
3. [coroutine-style-future-promise.md](D:/ProjectItem/SourceCode/NLCoroutine/docs/coroutine-style-future-promise.md:1)
4. [coroutine-style-fiber-uthread.md](D:/ProjectItem/SourceCode/NLCoroutine/docs/coroutine-style-fiber-uthread.md:1)
5. [coroutine-style-generator.md](D:/ProjectItem/SourceCode/NLCoroutine/docs/coroutine-style-generator.md:1)
6. [coroutine-style-async-generator.md](D:/ProjectItem/SourceCode/NLCoroutine/docs/coroutine-style-async-generator.md:1)

如果你主要想理解 C++ 协程的设计空间，建议顺序是：

1. 先读这篇总览，建立语义分类
2. 再读任务型和生成器型，对比 `co_await` 与 `co_yield`
3. 最后读 Future/Promise 与 fiber，明确“语言协程”和“用户态线程”并不等价

## 参考资料

- C++20 coroutine language：
  https://en.cppreference.com/w/cpp/language/coroutines
- WG21 N4134《Core Coroutines》：
  https://isocpp.org/files/papers/N4134.pdf
- cppcoro：
  https://github.com/lewissbaker/cppcoro
- C++23 `std::generator`：
  https://en.cppreference.com/w/cpp/coroutine/generator
- Windows Fibers：
  https://learn.microsoft.com/en-us/windows/win32/procthread/fibers
- Boost.Coroutine2：
  https://www.boost.org/doc/libs/latest/libs/coroutine2/index.html
