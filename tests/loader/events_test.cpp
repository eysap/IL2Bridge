#include "il2bridge/loader/events.h"
#include <catch2/catch_test_macros.hpp>
#include <unistd.h>
#include <atomic>
#include <string>
#include <thread>
#include <vector>

TEST_CASE("event stream preserves sequence and bounded copied payloads", "[events]") {
    event_stream_reset_for_testing();
    REQUIRE(event_stream_publish(3, "hook-added", "handler=count-calls") == 1);
    REQUIRE(event_stream_publish(3, "hook-removed", "-") == 2);
    Il2BridgeEvent events[4]{};
    REQUIRE(event_stream_read(0, 4, events) == 2);
    REQUIRE(events[0].sequence == 1);
    REQUIRE(events[0].hook_slot == 3);
    REQUIRE(std::string(events[0].payload) == "handler=count-calls");
    REQUIRE(events[1].sequence == 2);
    REQUIRE(event_stream_read(2, 4, events) == 0);
}

TEST_CASE("event stream reports overwritten history", "[events]") {
    event_stream_reset_for_testing();
    for (uint64_t i = 0; i < IL2BRIDGE_EVENT_CAPACITY + 2; ++i) {
        event_stream_publish(0, "sample", "-");
    }
    REQUIRE(event_stream_oldest_sequence() == 3);
    REQUIRE(event_stream_dropped() == 2);
    Il2BridgeEvent event{};
    REQUIRE(event_stream_read(0, 1, &event) == 1);
    REQUIRE(event.sequence == 3);
}

TEST_CASE("a freshly reset stream starts at sequence 1 and has nothing to read", "[events]") {
    event_stream_reset_for_testing();

    REQUIRE(event_stream_next_sequence() == 1);
    REQUIRE(event_stream_oldest_sequence() == 1);
    REQUIRE(event_stream_dropped() == 0);
    Il2BridgeEvent event{};
    REQUIRE(event_stream_read(0, 1, &event) == 0);
}

TEST_CASE("reads reject a null buffer or a zero limit", "[events]") {
    event_stream_reset_for_testing();
    event_stream_publish(0, "sample", "-");
    Il2BridgeEvent event{};

    REQUIRE(event_stream_read(0, 4, nullptr) == 0);
    REQUIRE(event_stream_read(0, 0, &event) == 0);
}

TEST_CASE("a read stops at its limit and resumes from the last sequence seen", "[events]") {
    event_stream_reset_for_testing();
    for (int i = 0; i < 10; ++i) event_stream_publish(0, "sample", "-");

    Il2BridgeEvent events[10]{};
    REQUIRE(event_stream_read(0, 3, events) == 3);
    REQUIRE(events[0].sequence == 1);
    REQUIRE(events[2].sequence == 3);

    REQUIRE(event_stream_read(3, 10, events) == 7);
    REQUIRE(events[0].sequence == 4);
    REQUIRE(events[6].sequence == 10);
}

TEST_CASE("a read starting past the newest sequence returns nothing", "[events]") {
    event_stream_reset_for_testing();
    event_stream_publish(0, "sample", "-");

    Il2BridgeEvent events[4]{};
    REQUIRE(event_stream_read(1, 4, events) == 0);
    REQUIRE(event_stream_read(99, 4, events) == 0);
}

TEST_CASE("a read at the maximum sequence returns nothing instead of wrapping to the start",
          "[events]") {
    event_stream_reset_for_testing();
    for (int i = 0; i < 4; ++i) event_stream_publish(0, "sample", "-");

    Il2BridgeEvent events[4]{};
    // after + 1 would overflow to 0 and re-read the whole ring.
    REQUIRE(event_stream_read(UINT64_MAX, 4, events) == 0);
}

TEST_CASE("a read from before the oldest retained sequence starts at the oldest", "[events]") {
    event_stream_reset_for_testing();
    for (uint64_t i = 0; i < IL2BRIDGE_EVENT_CAPACITY + 5; ++i) {
        event_stream_publish(0, "sample", "-");
    }

    std::vector<Il2BridgeEvent> events(IL2BRIDGE_EVENT_CAPACITY);
    // Sequence 1 is long gone; asking for it must not resurrect a stale slot.
    const size_t count = event_stream_read(0, events.size(), events.data());

    REQUIRE(count == IL2BRIDGE_EVENT_CAPACITY);
    REQUIRE(events[0].sequence == event_stream_oldest_sequence());
    REQUIRE(events[count - 1].sequence == event_stream_next_sequence() - 1);
    // The retained window is contiguous.
    for (size_t i = 1; i < count; ++i) {
        REQUIRE(events[i].sequence == events[i - 1].sequence + 1);
    }
}

TEST_CASE("one publish short of a full ring still reports sequence 1 as the oldest", "[events]") {
    event_stream_reset_for_testing();
    for (uint64_t i = 0; i < IL2BRIDGE_EVENT_CAPACITY - 1; ++i) {
        event_stream_publish(0, "sample", "-");
    }

    // A zero here would be reported over EVENTS-INFO, which the broker client
    // rejects as malformed.
    REQUIRE(event_stream_oldest_sequence() == 1);
    REQUIRE(event_stream_dropped() == 0);
}

TEST_CASE("filling the ring exactly drops nothing and retains every event", "[events]") {
    event_stream_reset_for_testing();
    for (uint64_t i = 0; i < IL2BRIDGE_EVENT_CAPACITY; ++i) {
        event_stream_publish(0, "sample", "-");
    }

    REQUIRE(event_stream_dropped() == 0);
    REQUIRE(event_stream_oldest_sequence() == 1);
    std::vector<Il2BridgeEvent> events(IL2BRIDGE_EVENT_CAPACITY);
    REQUIRE(event_stream_read(0, events.size(), events.data()) == IL2BRIDGE_EVENT_CAPACITY);
    REQUIRE(events[0].sequence == 1);
}

TEST_CASE("an overlong type or payload is truncated, not overflowed", "[events]") {
    event_stream_reset_for_testing();
    const std::string long_type(IL2BRIDGE_EVENT_TYPE_SIZE * 2, 'T');
    const std::string long_payload(IL2BRIDGE_EVENT_PAYLOAD_SIZE * 2, 'P');
    event_stream_publish(0, long_type.c_str(), long_payload.c_str());

    Il2BridgeEvent event{};
    REQUIRE(event_stream_read(0, 1, &event) == 1);
    // snprintf keeps room for the terminator, so one byte short of each field.
    REQUIRE(std::string(event.type).size() == IL2BRIDGE_EVENT_TYPE_SIZE - 1);
    REQUIRE(std::string(event.payload).size() == IL2BRIDGE_EVENT_PAYLOAD_SIZE - 1);
    REQUIRE(event.type[IL2BRIDGE_EVENT_TYPE_SIZE - 1] == '\0');
    REQUIRE(event.payload[IL2BRIDGE_EVENT_PAYLOAD_SIZE - 1] == '\0');
}

TEST_CASE("a null type or payload records a placeholder instead of dereferencing", "[events]") {
    event_stream_reset_for_testing();
    event_stream_publish(0, nullptr, nullptr);

    Il2BridgeEvent event{};
    REQUIRE(event_stream_read(0, 1, &event) == 1);
    REQUIRE(std::string(event.type) == "unknown");
    REQUIRE(std::string(event.payload) == "-");
}

TEST_CASE("an event records the publishing thread and a monotonic timestamp", "[events]") {
    event_stream_reset_for_testing();
    event_stream_publish(0, "from-main", "-");

    std::thread worker([] { event_stream_publish(0, "from-worker", "-"); });
    worker.join();

    Il2BridgeEvent events[2]{};
    REQUIRE(event_stream_read(0, 2, events) == 2);
    // In the main thread the kernel tid equals the process id.
    REQUIRE(events[0].thread_id == static_cast<uint32_t>(getpid()));
    REQUIRE(events[1].thread_id != events[0].thread_id);
    REQUIRE(events[0].timestamp_ns > 0);
    REQUIRE(events[1].timestamp_ns >= events[0].timestamp_ns);
}

TEST_CASE("reset clears retained events and the drop counter", "[events]") {
    event_stream_reset_for_testing();
    for (uint64_t i = 0; i < IL2BRIDGE_EVENT_CAPACITY + 2; ++i) {
        event_stream_publish(0, "sample", "-");
    }
    REQUIRE(event_stream_dropped() == 2);

    event_stream_reset_for_testing();

    REQUIRE(event_stream_dropped() == 0);
    REQUIRE(event_stream_next_sequence() == 1);
    Il2BridgeEvent event{};
    REQUIRE(event_stream_read(0, 1, &event) == 0);
}

TEST_CASE("concurrent publishers hand out unique sequences", "[events]") {
    event_stream_reset_for_testing();
    constexpr int kThreads = 4;
    constexpr int kPerThread = 64;

    std::vector<std::thread> workers;
    for (int t = 0; t < kThreads; ++t) {
        workers.emplace_back([] {
            for (int i = 0; i < kPerThread; ++i) event_stream_publish(0, "sample", "-");
        });
    }
    for (std::thread& worker : workers) worker.join();

    REQUIRE(event_stream_next_sequence() == kThreads * kPerThread + 1);
    std::vector<Il2BridgeEvent> events(kThreads * kPerThread);
    const size_t count = event_stream_read(0, events.size(), events.data());
    REQUIRE(count == static_cast<size_t>(kThreads * kPerThread));
    for (size_t i = 0; i < count; ++i) REQUIRE(events[i].sequence == i + 1);
}
