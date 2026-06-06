export module async_simple.common;

import std;

export namespace async_simple {

inline void logicAssert(bool ok, const char* message) {
    if (ok) {
        return;
    }
    throw std::logic_error(message);
}

}  // namespace async_simple
