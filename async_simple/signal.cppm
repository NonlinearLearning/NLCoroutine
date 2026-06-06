export module async_simple.signal;

import std;

export namespace async_simple {

// `SignalType` 用一个 64 位整数来表达信号类别。
// 当前教学版只用到两个值：
// - `Terminate`：通知"可以停止了"
// - `All`：表示默认不过滤，什么信号都接收
//
// 这里之所以不是 `bool canceled`，是为了后续能扩展成多种信号。
enum SignalType : std::uint64_t {
    None = 0,
    Terminate = 1,
    All = std::numeric_limits<std::uint64_t>::max(),
};

class Signal;
class Slot;

namespace detail {

// `SlotState` 是 `Slot` 背后的共享状态对象。
// 为什么要单独拆一个对象？
// 因为：
// - `Signal` 需要持有一批 slot
// - `Slot` 自己也要能访问同一份状态
// - 两边生命周期又不能互相硬绑定得太死
//
// 所以这里用 `shared_ptr` / `weak_ptr` 让它们共享同一份状态。
struct SlotState {
    // 反向引用所属 signal；signal 析构后这里会自然失效。
    std::weak_ptr<Signal> signal;
    // 当前 slot 愿意接收的信号类型掩码。
    SignalType filter = SignalType::All;
    // 每类信号对应的本地处理器。
    std::unordered_map<std::uint64_t, std::function<void(SignalType, Signal*)>> handlers;
    // 可选的转发回调，用于把收到的信号继续传给下游 signal。
    std::function<void(SignalType)> chainedSignal;
    // 保护 handler/filter/chainedSignal 的互斥锁。
    mutable std::mutex mutex;
};

}  // namespace detail

// `Signal` 是"发信号"的那一端。
// 你可以把它类比成一个广播器：
// - 它维护一批订阅它的 `Slot`
// - 当 `emits()` 被调用时，就把信号发给这些 `Slot`
class Signal : public std::enable_shared_from_this<Signal> {
public:
    struct PrivateConstructTag {};

    explicit Signal(PrivateConstructTag) {}

    template <typename T = Signal>
    static std::shared_ptr<T> create() {
        static_assert(std::is_base_of_v<Signal, T>);
        return std::make_shared<T>(PrivateConstructTag{});
    }

    // 发出一个信号。
    //
    // 这个函数分两阶段：
    // 1. 在锁内更新状态，并把活着的 slot 列表拷出来
    // 2. 在锁外逐个执行 handler
    //
    // 这样做的原因是：回调里可能继续操作其他对象，
    // 如果一直拿着 `_mutex` 执行用户回调，很容易放大锁竞争，甚至引入死锁风险。
    SignalType emits(SignalType type) noexcept {
        std::vector<std::shared_ptr<detail::SlotState>> slots;
        SignalType valid = type;
        {
            std::lock_guard lock(_mutex);
            if ((static_cast<std::uint64_t>(type) & static_cast<std::uint64_t>(Terminate)) != 0U &&
                (_state & static_cast<std::uint64_t>(Terminate)) != 0U) {
                valid = SignalType::None;
            } else {
                _state |= static_cast<std::uint64_t>(type);
            }
            slots.reserve(_slots.size());
            auto it = _slots.begin();
            while (it != _slots.end()) {
                if (auto slot = it->lock()) {
                    slots.push_back(std::move(slot));
                    ++it;
                } else {
                    it = _slots.erase(it);
                }
            }
        }
        if (valid == SignalType::None) {
            return valid;
        }

        // 这里每个 slot 都会复制出自己当前的 handler 表。
        // 教学版这样写更容易理解，也减少了执行回调时长期持锁的问题。
        for (const auto& slot : slots) {
            std::unordered_map<std::uint64_t, std::function<void(SignalType, Signal*)>> handlers;
            std::function<void(SignalType)> chained;
            {
                std::lock_guard lock(slot->mutex);
                if ((static_cast<std::uint64_t>(slot->filter) &
                     static_cast<std::uint64_t>(valid)) == 0U) {
                    continue;
                }
                handlers = slot->handlers;
                chained = slot->chainedSignal;
            }
            for (auto& [_, handler] : handlers) {
                if (handler) {
                    handler(valid, this);
                }
            }
            if (chained) {
                chained(valid);
            }
        }
        return valid;
    }

    SignalType state() const noexcept {
        std::lock_guard lock(_mutex);
        return static_cast<SignalType>(_state);
    }

private:
    friend class Slot;

    // 注册一个新的 slot。
    // 返回的是共享状态对象，而不是直接返回 `Slot` 本身。
    std::shared_ptr<detail::SlotState> registSlot(SignalType filter) {
        auto slot = std::make_shared<detail::SlotState>();
        slot->signal = shared_from_this();
        slot->filter = filter;
        std::lock_guard lock(_mutex);
        _slots.emplace_back(slot);
        return slot;
    }

    // 保护 `_state` 和 `_slots` 的全局锁。
    mutable std::mutex _mutex;
    // signal 已经发出过的状态位集合。
    std::uint64_t _state = 0;
    // 当前挂在该 signal 上的所有 slot 弱引用。
    std::vector<std::weak_ptr<detail::SlotState>> _slots;
};

// `Slot` 是"收信号"的那一端。
// 一个协程通常持有自己的 `Slot`，用于感知"是否被取消/终止"。
class Slot {
public:
    explicit Slot(Signal* signal, SignalType filter = SignalType::All)
        : _state(signal->registSlot(filter)) {}

    Slot(const Slot&) = delete;
    Slot& operator=(const Slot&) = delete;
    Slot(Slot&&) noexcept = default;
    Slot& operator=(Slot&&) noexcept = default;

    // 注册某种信号对应的处理逻辑。
    //
    // 注意这里把 handler 存进了 `std::function`：
    // 这意味着我们可以传 lambda，而且可以捕获外部变量。
    template <typename F>
    bool emplace(SignalType type, F&& handler) {
        std::lock_guard lock(_state->mutex);
        if (type == SignalType::Terminate && (state() & SignalType::Terminate) != 0U) {
            return false;
        }
        _state->handlers[static_cast<std::uint64_t>(type)] =
            std::function<void(SignalType, Signal*)>(std::forward<F>(handler));
        return true;
    }

    bool clear(SignalType type) {
        std::lock_guard lock(_state->mutex);
        return _state->handlers.erase(static_cast<std::uint64_t>(type)) > 0;
    }

    // 把当前 slot 接收到的信号继续转发给另一个 signal。
    // 这在"父任务取消时，子任务也跟着收到终止信号"的场景里很有用。
    void chainedSignal(const std::shared_ptr<Signal>& signal) {
        std::weak_ptr<Signal> weakSignal = signal;
        auto forwardSignal = [weakSignal](SignalType type) {
            if (auto locked = weakSignal.lock()) {
                locked->emits(type);
            }
        };
        std::lock_guard lock(_state->mutex);
        _state->chainedSignal = std::move(forwardSignal);
    }

    SignalType state() const noexcept {
        if (auto signal = _state->signal.lock()) {
            return signal->state();
        }
        return SignalType::None;
    }

    Signal* signal() const noexcept {
        if (auto signal = _state->signal.lock()) {
            return signal.get();
        }
        return nullptr;
    }

    void setFilter(SignalType filter) {
        std::lock_guard lock(_state->mutex);
        _state->filter = filter;
    }

private:
    // 当前 slot 共享的内部状态。
    std::shared_ptr<detail::SlotState> _state;
};

// `signalHelper` 只是一个很薄的工具包装。
// 这么写的目的，是让上层代码看起来更像：
//
//     signalHelper{Terminate}.hasCanceled(slot)
//
// 而不是到处重复位运算逻辑。
struct signalHelper {
    // 这个 helper 当前关注的目标信号类型。
    SignalType type;

    bool hasCanceled(Slot* slot) const noexcept {
        if (slot == nullptr) {
            return false;
        }
        return (static_cast<std::uint64_t>(slot->state()) & static_cast<std::uint64_t>(type)) != 0U;
    }

    template <typename F>
    bool tryEmplace(Slot* slot, F&& handler) const {
        if (slot == nullptr) {
            return true;
        }
        return slot->emplace(type, std::forward<F>(handler));
    }

    void checkHasCanceled(Slot* slot, const char* message) const {
        if (hasCanceled(slot)) {
            throw std::runtime_error(message);
        }
    }
};

}  // namespace async_simple
