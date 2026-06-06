export module async_simple.experimental.coroutine;

import std;

export namespace async_simple::coro {

template <typename Promise = void>
//  std::coroutine_handle<LazyPromise<int>>
//   表示“一个指向返回 Lazy<int> 的协程实例的句柄”。
using CoroHandle = std::coroutine_handle<Promise>;//是 C++20 协程的“句柄类型”，你可以把它理解成

}  
