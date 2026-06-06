# 同步生成器风格

生成器风格不是“等一个最终结果”，而是“按需产出一串值”。

这是最适合理解 `co_yield` 的协程风格。

## 1. 这类风格的定义

WG21 早期协程提案把 generator 定义为：

- 协程逐个提供一串值
- `yield` 一个值时挂起
- 消费者下一次 pull 时再恢复

到了 C++23，标准库给出了 `std::generator`，cppreference 的描述是：
它是一个用于 ranges 的同步协程生成器。

## 2. 它和任务型协程最核心的区别

任务型协程关心的是：

- 最终完成
- 一个结果
- `co_await`

生成器型协程关心的是：

- 中途多次产出
- 一串结果
- `co_yield`

所以最重要的心智模型是：

- task 像一次性异步函数调用
- generator 像一个按需推进的惰性序列

## 3. 典型运行方式

以 `generator<T>` 为例，调用协程函数后通常发生的是：

1. 创建协程帧
2. 初始挂起
3. 消费者调用 `begin()` 或等价入口
4. 协程跑到第一个 `co_yield`
5. 消费者取当前值
6. 消费者 `++iterator`，协程恢复，跑到下一个 `co_yield`

这是一种典型的 pull model。

cppcoro 的 `generator<T>` 文档也明确写了：

- `generator<T>` 产生的是 lazily and synchronously 的值序列
- coroutine body 可以 `co_yield`
- 但不能 `co_await`

## 4. 为什么说它是“同步生成器”

因为它的值生产是通过调用方推进的：

- 调用方要下一个值
- 协程就继续执行一点
- 直到产出下一个值或结束

换句话说，它没有“后台继续跑”的语义。

## 5. 本仓库里的现状

当前仓库没有实现一个 `generator<T>` 类型。

但你仍然应该理解这类风格，原因有两个：

### 它解释了 `co_yield` 的真正用途

很多人第一次学协程时把 `co_yield` 误当成“另一个版的 `co_await`”，这不对。

在生成器风格里：

- `co_yield` 是把当前值交给消费者
- 然后暂停自己

### 它能帮助你区分“单结果协程”和“值流协程”

这个仓库主要实现的是单结果任务型协程。
如果以后你要加流式遍历、树遍历、惰性枚举器，生成器会是更自然的模型。

## 6. 如果把它映射回这个仓库

可以这样理解：

- `Lazy<T>` 更像“完成一次异步任务”
- `generator<T>` 更像“每次拿一个元素”

如果以后要在这个仓库里新增同步生成器，概念上会更接近：

- `co_yield value`
- `begin()/end()`
- 输入迭代器式消费

而不是 `syncAwait()`、`collectAll()` 这套任务型 API。

## 7. 适用场景

- 惰性遍历
- 树/图/目录递归展开
- 逐个产生测试数据
- 把复杂状态机改写成线性代码

## 8. 优势和代价

优势：

- 写法接近顺序代码
- 不需要预先把整个结果序列存进容器
- 很适合“边算边取”

代价：

- 同步生成器通常不能直接表达异步等待
- 生命周期、引用返回和值缓存语义要讲清楚
- 它解决的是“序列生成”，不是“并发任务组合”

## 9. 推荐阅读资料

- C++23 `std::generator`：
  https://en.cppreference.com/w/cpp/coroutine/generator
- C++20 coroutine language：
  https://en.cppreference.com/w/cpp/language/coroutines
- cppcoro `generator<T>`：
  https://github.com/lewissbaker/cppcoro
- WG21 N4134：
  https://isocpp.org/files/papers/N4134.pdf
