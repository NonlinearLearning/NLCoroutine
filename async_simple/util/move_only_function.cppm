export module async_simple.util.move_only_function;

import std;

export namespace async_simple::util {

template <typename Signature>
class move_only_function;

namespace detail {

template <typename ResultType, typename RetType, bool = std::is_void_v<RetType>, typename = void>
struct RetTypeCheck : std::false_type {};

template <typename ResultType, typename RetType>
struct RetTypeCheck<ResultType, RetType, true, std::void_t<typename ResultType::type>> : std::true_type {};

template <typename ResultType, typename RetType>
struct RetTypeCheck<ResultType, RetType, false, std::void_t<typename ResultType::type>>
    : std::true_type {
private:
    static typename ResultType::type get();

    template <typename T>
    static void convert(T);

    template <typename T, typename = decltype(convert<T>(get()))>
    static std::true_type test(int);

    template <typename T>
    static std::false_type test(...);

public:
    using type = decltype(test<RetType>(1));
};

class undefined_class;

union no_copy_types {
    void* object;
    const void* const_object;
    void (*function_pointer)();
    void (undefined_class::*member_pointer)();
};

union [[gnu::may_alias]] any_data {
    void* access() { return &pod_data[0]; }
    const void* access() const { return &pod_data[0]; }

    template <typename T>
    T& access() {
        return *static_cast<T*>(access());
    }

    template <typename T>
    const T& access() const {
        return *static_cast<const T*>(access());
    }

    no_copy_types unused;
    char pod_data[sizeof(no_copy_types)];
};

enum class manager_operation : std::uint8_t {
    destroy_functor,
};

class function_base {
public:
    static constexpr std::size_t max_size = sizeof(no_copy_types);
    static constexpr std::size_t max_align = alignof(no_copy_types);

    template <typename Functor>
    class base_manager {
    protected:
        static constexpr bool stored_locally =
            std::is_trivially_copyable_v<Functor> &&
            sizeof(Functor) <= max_size &&
            alignof(Functor) <= max_align &&
            (max_align % alignof(Functor) == 0);

        using local_storage = std::integral_constant<bool, stored_locally>;

        static Functor* get_pointer(const any_data& source) {
            if constexpr (stored_locally) {
                const Functor& functor = source.access<Functor>();
                return const_cast<Functor*>(std::addressof(functor));
            }
            return source.access<Functor*>();
        }

        static void destroy(any_data& victim, std::true_type) {
            victim.access<Functor>().~Functor();
        }

        static void destroy(any_data& victim, std::false_type) {
            delete victim.access<Functor*>();
        }

    public:
        static void manager(any_data& dest, const any_data&, manager_operation operation) {
            switch (operation) {
            case manager_operation::destroy_functor:
                destroy(dest, local_storage());
                break;
            }
        }

        static void init_functor(any_data& functor, Functor&& value) {
            init_functor(functor, std::move(value), local_storage());
        }

        static void init_functor(any_data& functor, const Functor& value) {
            init_functor(functor, value, local_storage());
        }

        template <typename Signature>
        static bool not_empty_function(const move_only_function<Signature>& function) {
            return static_cast<bool>(function);
        }

        template <typename T>
        static bool not_empty_function(T* function_pointer) {
            return function_pointer != nullptr;
        }

        template <typename Class, typename T>
        static bool not_empty_function(T Class::*member_pointer) {
            return member_pointer != nullptr;
        }

        template <typename T>
        static bool not_empty_function(const T&) {
            return true;
        }

    private:
        static void init_functor(any_data& functor, Functor&& value, std::true_type) {
            ::new (functor.access()) Functor(std::move(value));
        }

        static void init_functor(any_data& functor, Functor&& value, std::false_type) {
            functor.access<Functor*>() = new Functor(std::move(value));
        }

        static void init_functor(any_data& functor, const Functor& value, std::true_type) {
            ::new (functor.access()) Functor(value);
        }

        static void init_functor(any_data& functor, const Functor& value, std::false_type) {
            functor.access<Functor*>() = new Functor(value);
        }
    };

    function_base() : manager_fn(nullptr) {}

    ~function_base() {
        if (manager_fn) {
            manager_fn(functor, functor, manager_operation::destroy_functor);
        }
    }

    bool empty() const { return !manager_fn; }

    using manager_type = void (*)(any_data&, const any_data&, manager_operation);

    any_data functor;
    manager_type manager_fn;
};

template <typename Signature, typename Functor>
class FunctionHandler;

template <typename Res, typename Functor, typename... ArgTypes>
class FunctionHandler<Res(ArgTypes...), Functor> : public function_base::base_manager<Functor> {
    using BaseType = typename function_base::base_manager<Functor>;

public:
    static void manager(any_data& dest, const any_data& source, manager_operation operation) {
        return BaseType::manager(dest, source, operation);
    }

    static Res invoke(const any_data& functor, ArgTypes&&... args) {
        if constexpr (std::is_same_v<Res, void>) {
            std::invoke(*BaseType::get_pointer(functor), std::forward<ArgTypes>(args)...);
        } else {
            return std::invoke(*BaseType::get_pointer(functor), std::forward<ArgTypes>(args)...);
        }
    }
};

template <>
class FunctionHandler<void, void> {
public:
    static void manager(any_data&, const any_data&, manager_operation) {}
};

template <typename>
struct move_only_function_guide_helper {};

template <typename Res, typename T, typename... Args>
struct move_only_function_guide_helper<Res (T::*)(Args...)> {
    using type = Res(Args...);
};

template <typename Res, typename T, typename... Args>
struct move_only_function_guide_helper<Res (T::*)(Args...)&> {
    using type = Res(Args...);
};

template <typename Res, typename T, typename... Args>
struct move_only_function_guide_helper<Res (T::*)(Args...) const> {
    using type = Res(Args...);
};

template <typename Res, typename T, typename... Args>
struct move_only_function_guide_helper<Res (T::*)(Args...) const&> {
    using type = Res(Args...);
};

}  // namespace detail

template <typename RetType, typename... ArgTypes>
class move_only_function<RetType(ArgTypes...)> : private detail::function_base {
    template <typename Func, typename Res2 = std::invoke_result<Func, ArgTypes...>>
    struct NotMoveOnlyCallable : public detail::RetTypeCheck<Res2, RetType>::type {};

    template <typename T>
    struct NotMoveOnlyCallable<move_only_function, T> : public std::false_type {};

    template <typename T>
    struct IsCStyleFunction : public std::false_type {};

    template <typename Ret, typename... Args>
    struct IsCStyleFunction<Ret (&)(Args...)> : public std::true_type {};

    template <typename Cond, typename T>
    using Requires = typename std::enable_if<Cond::value, T>::type;

public:
    using result_type = RetType;

    move_only_function() noexcept : detail::function_base(), _invoker(nullptr) {}
    move_only_function(std::nullptr_t) noexcept : detail::function_base(), _invoker(nullptr) {}
    move_only_function(const move_only_function&) = delete;

    move_only_function(move_only_function&& other) noexcept
        : detail::function_base(), _invoker(nullptr) {
        other.swap(*this);
    }

    template <
        typename Functor,
        typename =
            Requires<std::negation<std::is_same<std::remove_reference_t<Functor>, move_only_function>>,
                     void>,
        typename = Requires<std::negation<IsCStyleFunction<Functor>>, void>,
        typename = Requires<NotMoveOnlyCallable<Functor>, void>>
    move_only_function(Functor&& value) : detail::function_base(), _invoker(nullptr) {
        using MyHandler =
            detail::FunctionHandler<RetType(ArgTypes...), std::remove_reference_t<Functor>>;
        if (MyHandler::not_empty_function(value)) {
            MyHandler::init_functor(functor, std::forward<Functor>(value));
            _invoker = &MyHandler::invoke;
            manager_fn = &MyHandler::manager;
        }
    }

    template <typename Res, typename... Args>
    move_only_function(Res (&value)(Args...)) : detail::function_base(), _invoker(nullptr) {
        using MyHandler = detail::FunctionHandler<RetType(ArgTypes...), Res (*)(Args...)>;
        if (MyHandler::not_empty_function(&value)) {
            MyHandler::init_functor(functor, &value);
            _invoker = &MyHandler::invoke;
            manager_fn = &MyHandler::manager;
        }
    }

    move_only_function& operator=(const move_only_function&) = delete;

    move_only_function& operator=(move_only_function&& other) noexcept {
        move_only_function(std::move(other)).swap(*this);
        return *this;
    }

    move_only_function& operator=(std::nullptr_t) noexcept {
        if (manager_fn) {
            manager_fn(functor, functor, detail::manager_operation::destroy_functor);
            manager_fn = nullptr;
            _invoker = nullptr;
        }
        return *this;
    }

    template <typename Functor>
    Requires<NotMoveOnlyCallable<typename std::decay<Functor>::type>, move_only_function&>
    operator=(Functor&& value) {
        move_only_function(std::forward<Functor>(value)).swap(*this);
        return *this;
    }

    void swap(move_only_function& other) noexcept {
        std::swap(functor, other.functor);
        std::swap(manager_fn, other.manager_fn);
        std::swap(_invoker, other._invoker);
    }

    explicit operator bool() const noexcept { return !empty(); }

    RetType operator()(ArgTypes... args) const {
        if (empty()) {
            throw std::bad_function_call();
        }
        return _invoker(functor, std::forward<ArgTypes>(args)...);
    }

private:
    using InvokerType = RetType (*)(const detail::any_data&, ArgTypes&&...);
    InvokerType _invoker;
};

template <
    typename Functor,
    typename Signature =
        typename detail::move_only_function_guide_helper<decltype(&Functor::operator())>::type>
move_only_function(Functor) -> move_only_function<Signature>;

template <typename Res, typename... Args>
inline void swap(move_only_function<Res(Args...)>& lhs,
                 move_only_function<Res(Args...)>& rhs) noexcept {
    lhs.swap(rhs);
}

template <typename Res, typename... Args>
inline bool operator==(const move_only_function<Res(Args...)>& function,
                       std::nullptr_t) noexcept {
    return !static_cast<bool>(function);
}

}  // namespace async_simple::util
