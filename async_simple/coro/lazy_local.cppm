export module async_simple.coro.lazy_local;

import std;
import async_simple.signal;

export namespace async_simple::coro {

// `LazyLocalBase` 可以看成"协程版 thread_local 的基类"。
//
// 为什么需要它？
// 在普通多线程程序里，我们常用 `thread_local` 让一条线程携带上下文。
// 但协程会挂起、恢复、迁移到其他线程，所以"线程局部变量"已经不够用了。
//
// 这里的思路是：
// - 把一份"上下文对象"挂到协程 promise 上
// - 子协程在 co_await 链中自动继承它
//
// 这样无论协程之后在哪条线程恢复，逻辑上下文都还在。
class LazyLocalBase {
protected:
    // 这个构造函数用于"有具体子类型标签"的 local。
    // `typeTag` 不是 RTTI，而是我们自己约定的一个唯一地址，
    // 用来做非常轻量的类型识别。
    explicit LazyLocalBase(char* typeTag, Signal* signal = nullptr,
                           SignalType type = SignalType::All)
        : _typeTag(typeTag) {
        if (typeTag == nullptr) {
            std::terminate();
        }
        if (signal != nullptr) {
            _slot = std::make_unique<Slot>(signal, type);
        }
    }

public:
    // 这个构造函数用于"只想携带 Slot / Signal，不关心具体业务子类型"的场景。
    explicit LazyLocalBase(Signal* signal = nullptr, SignalType type = SignalType::All)
        : _typeTag(nullptr) {
        if (signal != nullptr) {
            _slot = std::make_unique<Slot>(signal, type);
        }
    }

    virtual ~LazyLocalBase() = default;

    // 返回类型标签。
    // 如果派生类自己实现了 `classof()`，就会拿这个标签做比较。
    const char* getTypeTag() const noexcept { return _typeTag; }

    // 返回这个 local 自己携带的 `Slot`。
    // `Slot` 是接收取消/终止信号的那一端。
    Slot* getSlot() const noexcept { return _slot.get(); }

    // 有些协程希望"继续继承业务 local，但不再接受 signal"，
    // 这时就把 slot 清掉。
    void forbidSignal() noexcept { _slot.reset(); }

    // collect 系列 API 会临时创建一层新的 local：
    // - 外层保留父协程上下文
    // - 内层额外挂一个 signal slot
    //
    // 所以这里需要一个"父指针链"，以便查找业务 local 时能向上回溯。
    void setParent(std::shared_ptr<LazyLocalBase> parent) {
        _parentOwner = std::move(parent);
        _parent = _parentOwner.get();
    }

    LazyLocalBase* parent() const noexcept { return _parent; }

private:
    // 轻量类型标签，用于不依赖 RTTI 的 local 类型识别。
    char* _typeTag = nullptr;
    // 当前 local 自己携带的信号槽位。
    std::unique_ptr<Slot> _slot;
    // 持有父 local 的所有权，维持整条 parent 链生命周期。
    std::shared_ptr<LazyLocalBase> _parentOwner;
    // 指向父 local，供动态向上查找业务上下文。
    LazyLocalBase* _parent = nullptr;
};

template <typename T>
const T* dynamicCast(const LazyLocalBase* base) noexcept {
    if (base == nullptr) {
        return nullptr;
    }

    // `if constexpr` 是 C++17 的"编译期分支"。
    // 和普通 `if` 不同，没走到的分支连编译都不会继续展开，
    // 很适合做模板代码里的类型分派。
    if constexpr (std::is_same_v<T, LazyLocalBase>) {
        return base;
    } else if (T::classof(base)) {
        return static_cast<const T*>(base);
    } else if (base->parent() != nullptr) {
        // 如果当前这层不是目标类型，就继续向父 local 查。
        return dynamicCast<T>(base->parent());
    }
    return nullptr;
}

template <typename T>
T* dynamicCast(LazyLocalBase* base) noexcept {
    if (base == nullptr) {
        return nullptr;
    }
    if constexpr (std::is_same_v<T, LazyLocalBase>) {
        return base;
    } else if (T::classof(base)) {
        return static_cast<T*>(base);
    } else if (base->parent() != nullptr) {
        return dynamicCast<T>(base->parent());
    }
    return nullptr;
}

}  // namespace async_simple::coro
