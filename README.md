# NLCoroutine

`NLCoroutine` 是一个可运行、可阅读、可顺着源码往下追的 C++23 协程教学仓库。

它不把自己定位成完整的工业级异步框架。这个仓库更聚焦，也更适合学习：用尽量小但足够完整的实现，把
C++20/23 协程语言机制如何落到具体库语义上讲清楚，例如惰性任务、continuation 串联、executor 控制恢复路径、
协程链上下文传播、取消信号以及多任务组合。

如果你想找一个既能先跑 demo、又能继续追到源码实现的协程仓库，这里就是入口。

## 为什么有这个仓库

很多协程示例只停在语法层：

- `co_await` 能用
- `co_return` 能用
- 写一个小 awaiter 可以编译通过

这还不够理解“一个协程库到底是怎么跑起来的”。

这个仓库再往前走了一层。它尽量保持实现规模可读，但又覆盖了真实协程库里最关键的几件事：

- 任务对象默认是 lazy 的，而不是调用即执行
- 父子协程通过 continuation 接起来
- 恢复动作可以显式交给 `Executor`
- 协程上下文不等于线程上下文
- 取消信号附着在协程链上
- `collectAll` / `collectAny` / `collectAllWindowed` 这类组合能力
- 额外保留一条 `uthread` 路线，用来对比 stackless 任务协程和 stackful 用户态线程

## 30 秒上手

### 你现在能跑什么

仓库当前包含这些 demo target：

- `lazy_demo`
- `future_promise_demo`
- `uthread_guard_demo`
- `test_module_demo`
- `minifuture_demo`

### 构建系统

项目使用 `xmake`，编译语言级别为 `C++23`。

当前的 `xmake.lua` 是围绕作者本机的 Windows + LLVM + MinGW 环境写的，里面直接用了类似这些本地路径：

- `D:/SystemEnvironment/LLVM/bin`
- `D:/SystemEnvironment/LLVM/share/libc++/v1/std.cppm`

所以这个仓库当前更适合被理解为“教学代码仓库”，其次才是“可移植的构建包”。

### 最短命令

只编译 `lazy_demo`：

```bash
xmake build lazy_demo
```

编译全部 demo：

```bash
xmake demos
```

刷新 `compile_commands.json` 和 clangd 需要的模块构建产物：

```bash
xmake clangd
```

如果你只打算从一个入口开始，请先看 `lazy_demo`。

## 当前实现了什么

### 任务型协程主线

- `Lazy<T>`：惰性任务对象
- `RescheduleLazy<T>`：恢复动作显式经由 `Executor` 重调度的任务对象
- `syncAwait()`：从普通同步调用者进入协程任务的同步桥
- `Yield`：主动挂起当前协程，并把后续恢复交还给执行器

### 多任务组合

- `collectAll`
- `collectAllPara`
- `collectAny`
- `collectAllWindowed`

### 协程链上下文与取消

- `LazyLocal`
- `CurrentLazyLocals<T>`
- `Signal` / `Slot`

### 协程同步原语

- coroutine mutex
- condition variable
- latch
- counting semaphore

### Future / Promise 路线

- `Future<T>` / `Promise<T>`
- 把 future 接入协程 `co_await`

### 栈式对照路线

- `uthread` 运行时及相关 demo

## 5 分钟技术地图

### 1. `Lazy<T>` 是仓库主抽象

这个仓库最核心的路径，是围绕 `Lazy<T>` 展开的任务型协程。

它的心智模型应该是：

- 调用协程函数，先得到一个任务对象
- 这个任务默认是 lazy 的
- 协程帧先创建
- 真正执行要等有人去驱动它

常见驱动入口有三种：

- `co_await someLazy()`
- `someLazy.start(callback)`
- `syncAwait(someLazy)`

这条语义主线，是整个仓库最该先看懂的部分。

### 2. continuation 决定协程链怎么接回去

协程真正关键的问题，不只是“怎么 suspend”，还包括“结束后恢复谁”。

这个仓库给出的答案是 continuation：

- 父协程在等待子协程时挂起
- 子协程把父协程记成 continuation
- 子协程在 `final_suspend()` 时把控制权交回 continuation

如果你想理解一串 `co_await` 为什么会表现得像结构化异步控制流，这就是第一条应该追的机制。

### 3. `.via(executor)` 改的是恢复路径，不是结果值

协程代码里最常见的误解之一，是把 `.via(executor)` 理解成“这里切线程”。

这个说法太粗。

在这个仓库里，更准确的理解是：

- 给任务绑定一个 `Executor`
- 之后恢复这个协程帧时，要先经过这个执行器
- 至于什么时候恢复、在哪条线程恢复，由执行器自己决定

这也是为什么实现里会区分 `Lazy<T>` 和 `RescheduleLazy<T>`。

### 4. `syncAwait()` 是边界适配器

`syncAwait()` 不是协程机制本身。

它的定位是：把同步调用者桥接进一个 lazy 协程任务。

它做的事情很简单：

- 启动任务
- 在回调里保存完成结果
- 当前调用者阻塞，直到结果可用

所以它适合用在进程入口、demo、测试和少量同步桥接场景。

### 5. `collect*` 展示了任务协程真正的价值

这个仓库不是只想演示一个单独的 `co_await`。

更有价值的部分，是多条协程链怎么被协调：

- 全部跑完再统一收集
- 谁先完成就先返回谁
- 限制同时推进的任务数

这时任务抽象才真正超越“只是比回调写法更顺眼”。

### 6. `LazyLocal` 和 `Signal` 属于协程链语义

这个仓库明确在演示一件事：协程上下文默认不该被简单等同成 thread-local。

这里的做法是：

- local 状态挂在协程链上
- 子协程继承这份 local
- 取消意图沿同一条协程链传播

这比只讲语法的协程示例更接近真实工程问题。

### 7. `uthread` 是对照，不是替代品

仓库额外保留了 `uthread` 路线，用来对比：

- 基于 `co_await` 的 stackless 任务协程
- 带独立运行时语义的 stackful 用户态线程

它们经常都被口头叫作“协程”，但执行模型不是一回事。

## 仓库地图

### 从这里开始

- [demo/lazy_demo.cpp](D:/ProjectItem/SourceCode/NLCoroutine/demo/lazy_demo.cpp:1)
- [docs/coroutine-style-task-lazy.md](D:/ProjectItem/SourceCode/NLCoroutine/docs/coroutine-style-task-lazy.md:1)
- [docs/reading-guide-lazy-framework.md](D:/ProjectItem/SourceCode/NLCoroutine/docs/reading-guide-lazy-framework.md:1)

### 主要实现区域

- [async_simple/coro/lazy.cppm](D:/ProjectItem/SourceCode/NLCoroutine/async_simple/coro/lazy.cppm:1)
- [async_simple/coro/sync_await.cppm](D:/ProjectItem/SourceCode/NLCoroutine/async_simple/coro/sync_await.cppm:1)
- [async_simple/coro/collect.cppm](D:/ProjectItem/SourceCode/NLCoroutine/async_simple/coro/collect.cppm:1)
- [async_simple/coro/lazy_local.cppm](D:/ProjectItem/SourceCode/NLCoroutine/async_simple/coro/lazy_local.cppm:1)
- [async_simple/executor.cppm](D:/ProjectItem/SourceCode/NLCoroutine/async_simple/executor.cppm:1)
- [async_simple/signal.cppm](D:/ProjectItem/SourceCode/NLCoroutine/async_simple/signal.cppm:1)
- [async_simple/uthread/uthread.cppm](D:/ProjectItem/SourceCode/NLCoroutine/async_simple/uthread/uthread.cppm:1)

### 配套文档

- [docs/coroutine-styles-overview.md](D:/ProjectItem/SourceCode/NLCoroutine/docs/coroutine-styles-overview.md:1)
- [docs/coroutine-style-future-promise.md](D:/ProjectItem/SourceCode/NLCoroutine/docs/coroutine-style-future-promise.md:1)
- [docs/coroutine-style-fiber-uthread.md](D:/ProjectItem/SourceCode/NLCoroutine/docs/coroutine-style-fiber-uthread.md:1)
- [docs/coroutine-style-generator.md](D:/ProjectItem/SourceCode/NLCoroutine/docs/coroutine-style-generator.md:1)
- [docs/coroutine-style-async-generator.md](D:/ProjectItem/SourceCode/NLCoroutine/docs/coroutine-style-async-generator.md:1)

## 推荐阅读路径

### 路线 A：先跑起来，再理解

1. 编译 `lazy_demo`
2. 阅读 [demo/lazy_demo.cpp](D:/ProjectItem/SourceCode/NLCoroutine/demo/lazy_demo.cpp:1)
3. 阅读 [docs/coroutine-style-task-lazy.md](D:/ProjectItem/SourceCode/NLCoroutine/docs/coroutine-style-task-lazy.md:1)
4. 再进入 [async_simple/coro/lazy.cppm](D:/ProjectItem/SourceCode/NLCoroutine/async_simple/coro/lazy.cppm:1)

### 路线 B：先建立实现模型

1. 阅读 [docs/coroutine-styles-overview.md](D:/ProjectItem/SourceCode/NLCoroutine/docs/coroutine-styles-overview.md:1)
2. 阅读 [docs/coroutine-style-task-lazy.md](D:/ProjectItem/SourceCode/NLCoroutine/docs/coroutine-style-task-lazy.md:1)
3. 阅读 [docs/reading-guide-lazy-framework.md](D:/ProjectItem/SourceCode/NLCoroutine/docs/reading-guide-lazy-framework.md:1)
4. 再按 `lazy.cppm -> sync_await.cppm -> executor.cppm -> collect.cppm` 的顺序往下追

## 当前边界

这个仓库有意保持“教学实现”的定位。

它当前想做好的事情是：

- 让协程运行时语义变得可见
- 保持 demo 可运行
- 把主抽象控制在可阅读规模内
- 把高层心智模型和具体源码文件对应起来

它当前没有宣称做到的事情是：

- 完整工业级可移植性
- 完整 async IO 集成
- 穷尽式工业 API 覆盖
- 成熟包管理器式安装体验

## 外部参考

- C++ coroutine language:
  https://en.cppreference.com/w/cpp/language/coroutines
- cppcoro:
  https://github.com/lewissbaker/cppcoro
- C++23 `std::generator`:
  https://en.cppreference.com/w/cpp/coroutine/generator
- Windows Fibers:
  https://learn.microsoft.com/en-us/windows/win32/procthread/fibers

## 当前状态

这个仓库正在持续往“更好的协程教学代码库”方向收敛。

如果你是学习者，建议把 `lazy_demo` 和 `docs/` 目录当作主路线。

如果你是库设计者，这个仓库最有价值的部分，是它把下面几层明确对应了起来：

- 对外语义
- demo 行为
- 协程帧机制
- executor 控制恢复路径

这个对应关系，才是这个项目真正想讲清楚的内容。
