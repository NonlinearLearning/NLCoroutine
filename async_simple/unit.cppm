export module async_simple.unit;

export namespace async_simple {

struct Unit {
    constexpr bool operator==(const Unit&) const noexcept { return true; }
    constexpr bool operator!=(const Unit&) const noexcept { return false; }
};

}  // namespace async_simple
