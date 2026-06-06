# 异步生成器风格

异步生成器可以看成“任务型协程”和“生成器型协程”的结合体：

- 它能 `co_await`，所以可以异步等待
- 它也能 `co_yield`，所以可以逐个产出元素

这类风格特别适合流式数据。

## 1. 这类风格的定义

cppcoro 对 `async_generator<T>` 的描述很清楚：

- 它表示一个 lazily produced sequence
- 值可能异步地产生
- coroutine body 同时允许 `co_await` 和 `co_yield`

这正是它和同步生成器的分界线。

## 2. 心智模型

同步生成器像：

- “你来拉，我给一个值”

异步生成器更像：

- “你来拉下一个值，但我可能得先异步等一会儿才能给你”

所以它天然适合：

- 流式网络读取
- 消息队列消费
- 批量分页获取
- 文件/数据库逐块扫描

## 3. 它和任务型协程的差别

任务型协程：

- 最终只有一个完成结果
- 重点是最终完成

异步生成器：

- 会多次产出结果
- 重点是边等待边产生元素

任务型更像 `Future<T>` 的结构化版本。
异步生成器更像“异步迭代器”。

## 4. 它和同步生成器的差别

同步生成器：

- `co_yield` 可以
- `co_await` 通常不可以

异步生成器：

- `co_yield` 可以
- `co_await` 也可以

这意味着异步生成器可以在两次产出之间经历真正的异步挂起。

## 5. 本仓库里的现状

当前仓库没有 `async_generator<T>` 实现。

但如果以后想表达下面这类场景，它会比 `collectAll` 或 `Future<std::vector<T>>` 更自然：

- 一边从 socket/pipe 拉数据，一边按块交付
- 一边分页请求接口，一边把每页元素吐给消费端
- 一边等待生产者填充，一边逐条消费消息

如果强行用任务型协程表示，常见结果是：

- 先把所有数据攒成容器
- 最后一次性返回

这会丢掉“流式交付”的语义优势。

## 6. 如果以后在本仓库里引入它，最可能依赖什么

概念上会依赖：

- 现有 `Executor`
- 现有取消/Signal 机制
- 新的 `yield_value()` 路径
- 一套异步迭代消费接口

它和现有 `Lazy<T>` 共享的是：

- promise/handle/continuation 这些底层协程机制

不同的是：

- 对外返回值不再是“一个最终结果”
- 而是“一个异步可枚举序列”

## 7. 优势和代价

优势：

- 节省峰值内存
- 消费端更早拿到第一批数据
- 很适合流式和管道式场景

代价：

- API 设计比 `task<T>` 更复杂
- 取消、提前终止、背压语义需要更仔细设计
- 调试难度通常比同步生成器更高

cppcoro 文档还特别提到了一点：当 `async_generator` 被析构时，会请求取消底层协程。
这说明“提前停止消费”是这种风格的核心设计问题之一。

## 8. 推荐阅读资料

- cppcoro `async_generator<T>`：
  https://github.com/lewissbaker/cppcoro
- C++20 coroutine language：
  https://en.cppreference.com/w/cpp/language/coroutines
- C++23 `std::generator`（可作为同步版本对照）：
  https://en.cppreference.com/w/cpp/coroutine/generator
