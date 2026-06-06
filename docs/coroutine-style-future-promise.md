# Future/Promise 风格

`Future/Promise` 不是 C++20 语言协程独有的风格，但它和协程生态高度相关，因为它表达的是
“最终结果会在未来就绪”的语义。

和 `Lazy<T>` 这类任务型协程相比，`Future/Promise` 更像共享状态协议，而不是控制流协议。

## 1. 这类风格的定义

标准库对 `std::future` 的定义很直接：异步操作通过共享状态把结果交给 `future`，调用方可以
查询、等待或提取该结果。

所以它的核心不是“协程帧怎么接起来”，而是：

- 生产者什么时候 `set_value`
- 消费者什么时候 `wait/get`
- 中间共享状态如何保存值或异常

## 2. 这种风格的心智模型

可以把它理解成：

- `Promise<T>`：结果写端
- `Future<T>`：结果读端
- Shared State：结果槽位 + 完成状态 + 异常

它更像“邮箱”或“单次兑现的票据”，而不是“协程本身”。

## 3. 本仓库里的对应实现

核心入口：

- [async_simple/future.cppm](D:/ProjectItem/SourceCode/NLCoroutine/async_simple/future.cppm:1)
- [async_simple/future_state.cppm](D:/ProjectItem/SourceCode/NLCoroutine/async_simple/future_state.cppm:1)
- [async_simple/local_state.cppm](D:/ProjectItem/SourceCode/NLCoroutine/async_simple/local_state.cppm:1)
- [demo/future_promise_demo.cpp](D:/ProjectItem/SourceCode/NLCoroutine/demo/future_promise_demo.cpp:1)

这个实现有几个关键点：

### 共享状态分两类

- 已经就绪时，结果可以落在 `LocalState`
- 尚未就绪时，结果放在 `FutureState`

这让 ready future 和 pending future 的开销路径不完全一样。

### continuation 风格是显式的

`Future<T>::thenValue()`、`thenTry()`、`then()` 都是在共享状态上注册 continuation。

这和 `Lazy<T>` 的区别很大：

- `Lazy<T>` 的 continuation 是 coroutine-to-coroutine
- `Future<T>` 的 continuation 更像 future-to-callback/future-to-future

### `via(executor)` 是调度绑定，不是结果变更

在这个实现里，`Future<T>::via(executor)` 只是给共享状态附一个执行器，让后续 continuation
在合适的地方恢复。

## 4. 它和任务型协程的关系

这两种风格经常互相桥接，但不应该混为一谈。

### 相同点

- 都表达“未来完成”的语义
- 都需要保存值、异常和完成状态
- 都可能与执行器绑定

### 不同点

- `Future/Promise` 偏共享状态模型
- `Lazy/task` 偏协程控制流模型

在这个仓库里，`future_bridge_demo()` 正是在演示两者桥接：

- 外部线程 `promise.setValue(99)`
- 协程里 `co_await future`
- 最终结果进入 `Lazy<int>` 的控制流

见 [demo/lazy_demo.cpp](D:/ProjectItem/SourceCode/NLCoroutine/demo/lazy_demo.cpp:1)。

## 5. 这种风格适合什么场景

- 生产者和消费者不在同一条协程链上
- 结果由外部事件、线程或回调完成
- 你更关心“结果何时可得”，而不是“协程调用栈如何连接”
- 需要 then-style 链式变换

## 6. 优势和代价

优势：

- 和线程、回调、外部事件集成自然
- 容易表达“还没完成 / 已完成 / 异常完成”
- 不要求生产端本身必须是协程

代价：

- 容易退化成 callback 链
- 控制流可读性通常不如 `co_await`
- 多次组合时，需要特别小心异常传播和 executor 绑定语义

## 7. 在这个仓库里怎么读

建议顺序：

1. [demo/future_promise_demo.cpp](D:/ProjectItem/SourceCode/NLCoroutine/demo/future_promise_demo.cpp:1)
2. [async_simple/future.cppm](D:/ProjectItem/SourceCode/NLCoroutine/async_simple/future.cppm:1)
3. [async_simple/future_state.cppm](D:/ProjectItem/SourceCode/NLCoroutine/async_simple/future_state.cppm:1)
4. [async_simple/collect.cppm](D:/ProjectItem/SourceCode/NLCoroutine/async_simple/collect.cppm:1)

## 8. 推荐阅读资料

- `std::future`：
  https://www.cppreference.com/w/cpp/thread/future
- C++20 coroutine language：
  https://en.cppreference.com/w/cpp/language/coroutines
- WG21 N4134（task-like promise / generator promise 的早期分工说明）：
  https://isocpp.org/files/papers/N4134.pdf
