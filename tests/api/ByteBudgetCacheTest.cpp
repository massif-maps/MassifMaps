// The cache under ContourTileDataSource's decoded DEM tiles: bounded by bytes, least recently used out
// first, and a load several threads ask for at once runs once.

#include "datasources/components/ByteBudgetCache.h"

#include "TestCheck.h"

#include <atomic>
#include <chrono>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace massif;

namespace {
    using Cache = ByteBudgetCache<std::vector<char> >;

    Cache makeCache(std::size_t maxBytes) {
        return Cache(maxBytes, [](const std::vector<char>& v) { return v.size(); });
    }

    Cache::Loader blob(std::size_t size, std::atomic<int>& loads) {
        return [size, &loads]() {
            loads++;
            return std::make_shared<std::vector<char> >(size);
        };
    }

    void testBudget() {
        std::atomic<int> loads(0);
        Cache cache = makeCache(100);
        cache.get(1, blob(40, loads));
        cache.get(2, blob(40, loads));
        cache.get(1, blob(40, loads));
        TEST_CHECK(loads == 2 && cache.getBytes() == 80, "a hit does not load again");
        cache.get(3, blob(40, loads)); // over by 20: the least recent, key 2, goes
        TEST_CHECK(cache.getCount() == 2 && cache.getBytes() == 80, "the cache stays under its byte budget");
        cache.get(1, blob(40, loads));
        TEST_CHECK(loads == 3, "a hit refreshes an entry, so it outlives an older one");
        cache.get(2, blob(40, loads));
        TEST_CHECK(loads == 4, "and the least recently used one was the one evicted");

        Cache tiny = makeCache(10);
        tiny.get(1, blob(50, loads));
        TEST_CHECK(tiny.getCount() == 1 && tiny.getBytes() == 50, "one value bigger than the budget is still kept");
        tiny.get(2, blob(50, loads));
        TEST_CHECK(tiny.getCount() == 1, "but alone");

        cache.clear();
        TEST_CHECK(cache.getCount() == 0 && cache.getBytes() == 0, "clear empties it");
    }

    void testFailures() {
        std::atomic<int> calls(0);
        Cache cache = makeCache(100);
        auto empty = [&]() { calls++; return std::shared_ptr<std::vector<char> >(); };
        TEST_CHECK(!cache.get(1, empty) && !cache.get(1, empty) && calls == 2, "a null load is not cached");
        auto thrower = [&]() -> std::shared_ptr<std::vector<char> > { calls++; throw std::runtime_error("decode"); };
        bool thrown = false;
        try {
            cache.get(2, thrower);
        } catch (const std::runtime_error&) {
            thrown = true;
        }
        std::atomic<int> loads(0);
        TEST_CHECK(thrown && cache.get(2, blob(10, loads)) && loads == 1, "an exception reaches the caller and does not wedge the key");
    }

    void testCoalescing() {
        std::atomic<int> loads(0);
        std::atomic<int> waiting(0);
        Cache cache = makeCache(1000);
        auto slow = [&]() {
            loads++;
            while (waiting < 4) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            return std::make_shared<std::vector<char> >(10);
        };
        std::vector<std::shared_ptr<std::vector<char> > > results(4);
        std::vector<std::thread> threads;
        for (int t = 0; t < 4; t++) {
            threads.emplace_back([&, t]() {
                waiting++;
                results[t] = cache.get(7, slow);
            });
        }
        for (std::thread& thread : threads) {
            thread.join();
        }
        TEST_CHECK(loads == 1, "four threads asking for one key load it once");
        TEST_CHECK(results[0] && results[0] == results[1] && results[1] == results[2] && results[2] == results[3], "and all get the same value");

        std::atomic<int> failedLoads(0);
        std::atomic<int> started(0);
        auto failing = [&]() {
            failedLoads++;
            while (started < 3) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            return std::shared_ptr<std::vector<char> >();
        };
        std::vector<std::thread> failing3;
        std::atomic<int> nulls(0);
        for (int t = 0; t < 3; t++) {
            failing3.emplace_back([&]() {
                started++;
                if (!cache.get(8, failing)) {
                    nulls++;
                }
            });
        }
        for (std::thread& thread : failing3) {
            thread.join();
        }
        TEST_CHECK(failedLoads == 1 && nulls == 3, "a failed load is shared by the threads that waited on it");
    }

    void testClearDuringLoad() {
        std::atomic<int> loads(0);
        Cache cache = makeCache(1000);
        std::atomic<bool> inside(false), release(false);
        std::thread loader([&]() {
            cache.get(1, [&]() {
                inside = true;
                while (!release) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                return std::make_shared<std::vector<char> >(10);
            });
        });
        while (!inside) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        cache.clear();
        release = true;
        loader.join();
        TEST_CHECK(cache.getCount() == 0, "a value loaded across a clear is not put back");
    }
}

void testByteBudgetCache() {
    testBudget();
    testFailures();
    testCoalescing();
    testClearDuringLoad();
}
