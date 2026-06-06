export module async_simple.coro.detached_coroutine;

import std;
import async_simple.experimental.coroutine;

export namespace async_simple::coro {

namespace detail {

// `DetachedCoroutine` 是一种"分离协程"：
// - 创建后立刻开始执行
// - 没有调用者再去等待它的结果
// - 更像一个异步根任务，而不是一个要被别人 `co_await` 的子任务
struct DetachedCoroutine {
    struct promise_type {
        // `suspend_never` 的意思是：来到这个挂起点时不要真的挂起，继续往下跑。
        std::suspend_never initial_suspend() noexcept { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        void return_void() noexcept {}

        // DetachedCoroutine 没有上层调用者来接异常，所以这里直接终止程序。
        void unhandled_exception() {
            try {
                std::rethrow_exception(std::current_exception());
            } catch (const std::exception& error) {
                std::cerr << "DetachedCoroutine exception: " << error.what() << '\n';
                std::cerr << std::flush;
                std::terminate();
            } catch (...) {
                std::cerr << "DetachedCoroutine unknown exception\n";
                std::cerr << std::flush;
                std::terminate();
            }
        }
        DetachedCoroutine get_return_object() noexcept { return {}; }

        // 这两个成员主要是为了和上层 Lazy 运行时的数据布局保持一致。
        // 保留 continuation 槽位，方便与 Lazy promise 结构对齐。
        std::coroutine_handle<> _continuation = nullptr;
        // 保留 local 槽位，方便与 Lazy promise 结构对齐。
        void* _lazy_local = nullptr;
    };
};

}  // namespace detail

// `ReadyAwaiter<T>` 是一个"立即就绪"的 awaiter。
// 它能把一个普通值包装成一个可以 `co_await` 的对象。
template <typename T>
struct ReadyAwaiter {
    explicit ReadyAwaiter(T value) : _value(std::move(value)) {}

    bool await_ready() const noexcept { return true; }
    void await_suspend(CoroHandle<>) const noexcept {}
    T await_resume() noexcept { return std::move(_value); }

    T _value;
};

template <>
struct ReadyAwaiter<void> {
    bool await_ready() const noexcept { return true; }
    void await_suspend(CoroHandle<>) const noexcept {}
    void await_resume() const noexcept {}
};

}  // namespace async_simple::coro
