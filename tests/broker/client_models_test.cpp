#include "il2bridge/broker/client_models.hpp"
#include <catch2/catch_test_macros.hpp>

using namespace il2bridge;

TEST_CASE("canonical method selectors parse and round-trip", "[broker_client]") {
    std::string error;
    auto selector = MethodSelector::parse("Fake.dll!FakeNamespace.FakeClass::FakeMethod/2", &error);
    REQUIRE(selector.has_value());
    REQUIRE(selector->assembly == "Fake.dll");
    REQUIRE(selector->namespaze == "FakeNamespace");
    REQUIRE(selector->type == "FakeClass");
    REQUIRE(selector->method == "FakeMethod");
    REQUIRE(selector->arity == 2);
    REQUIRE_FALSE(selector->token.has_value());
    REQUIRE(selector->canonical() == "Fake.dll!FakeNamespace.FakeClass::FakeMethod/2");
}

TEST_CASE("metadata-token selectors are authoritative and canonical", "[broker_client]") {
    auto selector = MethodSelector::parse("Fake.dll!FakeNamespace.FakeClass::FakeMethod@0x06000001");
    REQUIRE(selector.has_value());
    REQUIRE(selector->token == 0x06000001u);
    REQUIRE_FALSE(selector->arity.has_value());
    REQUIRE(selector->canonical() == "Fake.dll!FakeNamespace.FakeClass::FakeMethod@0x06000001");
}

TEST_CASE("canonical method selectors support the empty namespace", "[broker_client]") {
    auto selector = MethodSelector::parse("Fake.dll!FakeClass::FakeMethod/0");
    REQUIRE(selector.has_value());
    REQUIRE(selector->namespaze.empty());
    REQUIRE(selector->type == "FakeClass");
    REQUIRE(selector->canonical() == "Fake.dll!FakeClass::FakeMethod/0");
}

TEST_CASE("malformed selectors are rejected with a useful error", "[broker_client]") {
    for (const char* value : {"", "Fake.dll", "Fake.dll!Type::Method", "Fake.dll!Type::Method/-1",
                              "Fake.dll!Type::Method/256", "Fake.dll!Type::/0",
                              "Fake.dll!Type::Method/0@0x06000001"}) {
        std::string error;
        REQUIRE_FALSE(MethodSelector::parse(value, &error).has_value());
        REQUIRE_FALSE(error.empty());
    }
}

TEST_CASE("hook ids retain a process session and slot", "[broker_client]") {
    auto id = HookId::parse("1234:987654:7");
    REQUIRE(id.has_value());
    REQUIRE(id->session == "1234:987654");
    REQUIRE(id->slot == 7);
    REQUIRE(id->str() == "1234:987654:7");
    REQUIRE_FALSE(HookId::parse("7").has_value());
}
