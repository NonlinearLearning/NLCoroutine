import std;

import async_simple.executors.simple_executor;
import async_simple.future;
import async_simple.uthread.async;
import async_simple.uthread.await;
import async_simple.uthread.uthread;

int main() {
    async_simple::executors::SimpleExecutor executor(2);

    try {
        std::cerr << "case1-begin" << std::endl;
        auto future = async_simple::uthread::async(
            async_simple::uthread::Launch::Schedule,
            async_simple::uthread::Attribute{&executor},
            []() { return 7; });
        if (std::move(future).get() != 7) {
            return 1;
        }
        std::cerr << "case1-ok" << std::endl;
    } catch (const std::exception& ex) {
        std::cerr << "case1-exception: " << ex.what() << std::endl;
        return 2;
    }

    try {
        std::cerr << "case2-begin" << std::endl;
        auto awaited = async_simple::uthread::async(
            async_simple::uthread::Launch::Schedule,
            async_simple::uthread::Attribute{&executor},
            [&executor]() {
                async_simple::Promise<int> promise;
                auto future = promise.getFuture().via(&executor);
                async_simple::uthread::async<async_simple::uthread::Launch::Schedule>(
                    [promise = std::move(promise)]() mutable {
                        promise.setValue(21);
                    },
                    &executor);
                return async_simple::uthread::await(std::move(future));
            });
        if (std::move(awaited).get() != 21) {
            return 1;
        }
        std::cerr << "case2-ok" << std::endl;
    } catch (const std::exception& ex) {
        std::cerr << "case2-exception: " << ex.what() << std::endl;
        return 3;
    }

    return 0;
}
