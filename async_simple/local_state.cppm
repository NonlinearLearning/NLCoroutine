export module async_simple.local_state;

import std;
import async_simple.common;
import async_simple.executor;
import async_simple.try_type;

export namespace async_simple {

template <typename T>
class LocalState {
public:
    LocalState() : _executor(nullptr) {}
    LocalState(T&& value) : _tryValue(std::forward<T>(value)), _executor(nullptr) {}
    LocalState(Try<T>&& value) : _tryValue(std::move(value)), _executor(nullptr) {}

    LocalState(const LocalState&) = delete;
    LocalState& operator=(const LocalState&) = delete;

    LocalState(LocalState&& other)
        : _tryValue(std::move(other._tryValue)),
          _executor(std::exchange(other._executor, nullptr)) {}

    LocalState& operator=(LocalState&& other) {
        if (this != &other) {
            std::swap(_tryValue, other._tryValue);
            std::swap(_executor, other._executor);
        }
        return *this;
    }

    bool hasResult() const noexcept { return _tryValue.available(); }

    Try<T>& getTry() noexcept { return _tryValue; }
    const Try<T>& getTry() const noexcept { return _tryValue; }

    void setExecutor(Executor* executor) { _executor = executor; }
    Executor* getExecutor() { return _executor; }

    bool currentThreadInExecutor() const {
        return _executor != nullptr && _executor->currentThreadInExecutor();
    }

    template <typename F>
    void setContinuation(F&& continuation) {
        logicAssert(_tryValue.available(), "LocalState has no result");
        std::forward<F>(continuation)(std::move(_tryValue));
    }

private:
    Try<T> _tryValue;
    Executor* _executor;
};

}  // namespace async_simple
