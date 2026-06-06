export module async_simple.try_type;

import std;
import async_simple.common;
import async_simple.unit;

export namespace async_simple {

template <typename T>
class Try;

template <>
class Try<void>;

template <typename T>
class Try {
public:
    Try() = default;
    ~Try() = default;

    Try(Try&&) = default;

    template <typename T2 = T>
    Try(std::enable_if_t<std::is_same_v<Unit, T2>, const Try<void>&> other) {
        if (other.hasError()) {
            _value.template emplace<std::exception_ptr>(other._error);
        } else {
            _value.template emplace<T>();
        }
    }

    Try& operator=(Try&&) = default;

    Try& operator=(std::exception_ptr error) {
        if (std::holds_alternative<std::exception_ptr>(_value) &&
            std::get<std::exception_ptr>(_value) == error) {
            return *this;
        }
        _value.template emplace<std::exception_ptr>(error);
        return *this;
    }

    template <class... U>
    Try(U&&... value) requires std::is_constructible_v<T, U...>
        : _value(std::in_place_type<T>, std::forward<U>(value)...) {}

    Try(std::exception_ptr error) : _value(error) {}

    Try(const Try&) = delete;
    Try& operator=(const Try&) = delete;

    bool available() const noexcept {
        return !std::holds_alternative<std::monostate>(_value);
    }

    bool hasError() const noexcept {
        return std::holds_alternative<std::exception_ptr>(_value);
    }

    const T& value() const& {
        checkHasTry();
        return std::get<T>(_value);
    }

    T& value() & {
        checkHasTry();
        return std::get<T>(_value);
    }

    T&& value() && {
        checkHasTry();
        return std::move(std::get<T>(_value));
    }

    const T&& value() const&& {
        checkHasTry();
        return std::move(std::get<T>(_value));
    }

    template <class... Args>
    T& emplace(Args&&... args) {
        return _value.template emplace<T>(std::forward<Args>(args)...);
    }

    void setException(std::exception_ptr error) {
        if (std::holds_alternative<std::exception_ptr>(_value) &&
            std::get<std::exception_ptr>(_value) == error) {
            return;
        }
        _value.template emplace<std::exception_ptr>(error);
    }

    std::exception_ptr getException() const {
        logicAssert(std::holds_alternative<std::exception_ptr>(_value),
                    "Try object do not has an error");
        return std::get<std::exception_ptr>(_value);
    }

    operator Try<void>() const;

private:
    void checkHasTry() const {
        if (std::holds_alternative<T>(_value)) {
            return;
        }
        if (std::holds_alternative<std::exception_ptr>(_value)) {
            std::rethrow_exception(std::get<std::exception_ptr>(_value));
        }
        throw std::logic_error("Try object is empty");
    }

    std::variant<std::monostate, T, std::exception_ptr> _value;

    friend Try<Unit>;
};

template <>
class Try<void> {
public:
    Try() = default;
    Try(std::exception_ptr error) : _error(error) {}

    Try& operator=(std::exception_ptr error) {
        _error = error;
        return *this;
    }

    Try(Try&& other) : _error(std::move(other._error)) {}

    Try& operator=(Try&& other) {
        if (this != &other) {
            std::swap(_error, other._error);
        }
        return *this;
    }

    void value() {
        if (_error) {
            std::rethrow_exception(_error);
        }
    }

    bool hasError() const { return _error.operator bool(); }

    void setException(std::exception_ptr error) { _error = error; }

    std::exception_ptr getException() const { return _error; }

private:
    std::exception_ptr _error;

    friend Try<Unit>;
};

template <class T>
Try<T>::operator Try<void>() const {
    if (hasError()) {
        return Try<void>(getException());
    }
    return Try<void>();
}

template <class T>
Try(T) -> Try<T>;

template <typename F, typename... Args>
auto makeTryCall(F&& f, Args&&... args) {
    using T = std::invoke_result_t<F, Args...>;
    try {
        if constexpr (std::is_void_v<T>) {
            std::invoke(std::forward<F>(f), std::forward<Args>(args)...);
            return Try<void>();
        } else {
            return Try<T>(
                std::invoke(std::forward<F>(f), std::forward<Args>(args)...));
        }
    } catch (...) {
        return Try<T>(std::current_exception());
    }
}

}  // namespace async_simple
