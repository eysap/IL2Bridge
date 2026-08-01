#include "il2bridge/loader/events.h"
#include <catch2/catch_test_macros.hpp>
#include <string>

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
