import std;

import async_simple.collect;
import async_simple.future;
import async_simple.try_type;
import async_simple.executors.simple_executor;

int main() {
    async_simple::executors::SimpleExecutor executor(2);
    if (async_simple::runFuturePromiseSelfTest(&executor) != 0) {
        return 1;
    }

    std::vector<async_simple::Future<int>> pendingFutures;
    std::vector<async_simple::Promise<int>> pendingPromises;
    pendingFutures.reserve(3);
    pendingPromises.reserve(3);
    for (int i = 0; i < 3; ++i) {
        async_simple::Promise<int> promise;
        pendingFutures.push_back(promise.getFuture().via(&executor));
        pendingPromises.push_back(std::move(promise));
    }

    auto collectedFuture =
        async_simple::collectAll(pendingFutures.begin(), pendingFutures.end())
            .thenValue([](std::vector<async_simple::Try<int>>&& results) {
                int sum = 0;
                for (auto& result : results) {
                    sum += result.value();
                }
                return sum;
            });

    pendingPromises[0].setValue(10);
    pendingPromises[1].setValue(20);
    pendingPromises[2].setValue(30);

    if (std::move(collectedFuture).get() != 60) {
        return 1;
    }

    std::vector<async_simple::Future<int>> readyFutures;
    readyFutures.push_back(async_simple::makeReadyFuture(1));
    readyFutures.push_back(async_simple::makeReadyFuture(2));
    auto readyCollected = async_simple::collectAll(readyFutures.begin(), readyFutures.end());
    if (!readyCollected.TEST_hasLocalState()) {
        return 1;
    }

    auto readyResults = std::move(readyCollected).get();
    if (readyResults.size() != 2 || readyResults[0].value() != 1 || readyResults[1].value() != 2) {
        return 1;
    }

    return 0;
}
