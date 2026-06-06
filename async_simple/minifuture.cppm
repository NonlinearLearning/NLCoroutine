export module async_simple.minifuture;

import std;

export namespace async_simple {

template <typename T>
class MiniFuture {
public:
    using value_type = T;

    explicit MiniFuture(T value) : _value(std::move(value)) {}

    T value() && {
        return std::move(_value);
    }

private:
    T _value;
};

}
