#include "common/mpmc_queue.h"
#include "common/types.h"

#include <catch2/catch_test_macros.hpp>

#include <thread>
#include <vector>

using namespace vfx;

TEST_CASE("queue rejects pushes when full and pops in order") {
    MpmcQueue<int> q(4);
    for (int i = 0; i < 4; ++i) REQUIRE(q.push(i));
    REQUIRE_FALSE(q.push(99));
    int v;
    for (int i = 0; i < 4; ++i) { REQUIRE(q.pop(v)); REQUIRE(v == i); }
    REQUIRE_FALSE(q.pop(v));
}

TEST_CASE("queue delivers every command from several producer threads") {
    MpmcQueue<Command> q(256);
    constexpr int kProducers = 4, kEach = 20000;
    std::atomic<bool> done{false};
    long long sum = 0;
    int count = 0;
    std::thread consumer([&] {
        Command c;
        while (!done || q.pop(c)) {
            if (q.pop(c)) { sum += c.slot; ++count; }
        }
    });
    std::vector<std::thread> producers;
    for (int p = 0; p < kProducers; ++p)
        producers.emplace_back([&q] {
            for (int i = 0; i < kEach; ++i) {
                Command c{Command::Play, PlayMode::Restart, int16_t(i % 100), 1.f};
                while (!q.push(c)) std::this_thread::yield();
            }
        });
    for (auto& t : producers) t.join();
    done = true;
    consumer.join();
    Command c;
    while (q.pop(c)) { sum += c.slot; ++count; }
    long long expected = 0;
    for (int i = 0; i < kEach; ++i) expected += i % 100;
    REQUIRE(count == kProducers * kEach);
    REQUIRE(sum == expected * kProducers);
}
