export module async_simple.collect;

import std;
import async_simple.future;
import async_simple.try_type;

export namespace async_simple {

template <std::input_iterator Iterator>
Future<std::vector<
    Try<typename std::iterator_traits<Iterator>::value_type::value_type>>>
collectAll(Iterator begin, Iterator end) {
    using FutureType = typename std::iterator_traits<Iterator>::value_type;
    using T = typename FutureType::value_type;

    const auto count = static_cast<std::size_t>(std::distance(begin, end));

    bool allReady = true;
    for (auto iter = begin; iter != end; ++iter) {
        if (!iter->hasResult()) {
            allReady = false;
            break;
        }
    }

    if (allReady) {
        std::vector<Try<T>> results;
        results.reserve(count);
        for (auto iter = begin; iter != end; ++iter) {
            results.push_back(std::move(iter->result()));
        }
        return Future<std::vector<Try<T>>>(std::move(results));
    }

    Promise<std::vector<Try<T>>> promise;
    auto future = promise.getFuture();

    struct Context {
        Context(std::size_t size, Promise<std::vector<Try<T>>>&& inputPromise)
            : results(size),
              promise(std::move(inputPromise)) {}

        ~Context() {
            promise.setValue(std::move(results));
        }

        std::vector<Try<T>> results;
        Promise<std::vector<Try<T>>> promise;
    };

    auto context = std::make_shared<Context>(count, std::move(promise));
    for (std::size_t index = 0; index < count; ++index, ++begin) {
        if (begin->hasResult()) {
            context->results[index] = std::move(begin->result());
        } else {
            begin->setContinuation([context, index](Try<T>&& result) mutable {
                context->results[index] = std::move(result);
            });
        }
    }

    return future;
}

}  // namespace async_simple
