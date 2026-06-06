import std;
import async_simple.minifuture;

int main() {
    async_simple::MiniFuture<int> future(7);
    return std::move(future).value() == 7 ? 0 : 1;
}
