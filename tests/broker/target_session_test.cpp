#include "fake_loader.hpp"
#include "il2bridge/broker/target_session.hpp"
#include <catch2/catch_test_macros.hpp>
#include <unistd.h>
#include <string>
#include <vector>

using namespace il2bridge;
using il2bridge::testing::FakeLoader;

namespace {

// The identity parse_info() accepts: protocol 1, a non-zero start tick, and a
// pid/uid pair matching the peer credentials of this very process.
std::string valid_info() {
    return "OK protocol=1 pid=" + std::to_string(getpid()) +
           " uid=" + std::to_string(geteuid()) + " start=8877 features=hooks,events\n";
}

std::string session_id() {
    return std::to_string(getpid()) + ":8877";
}

MethodSelector selector() {
    MethodSelector value;
    value.assembly = "Fake.dll";
    value.namespaze = "FakeNamespace";
    value.type = "FakeClass";
    value.method = "FakeMethod";
    value.arity = 2;
    return value;
}

// Connects a session, consuming one queued INFO response.
TargetSession open(FakeLoader& loader) {
    loader.push_response(valid_info());
    auto session = TargetSession::connect(loader.socket_path());
    REQUIRE(session.has_value());
    return std::move(*session);
}

} // namespace

// --- connect / identity validation ------------------------------------------

TEST_CASE("connect accepts a well-formed INFO identity", "[target_session]") {
    FakeLoader loader;
    loader.push_response(valid_info());

    std::string error;
    auto session = TargetSession::connect(loader.socket_path(), &error);

    REQUIRE(session.has_value());
    REQUIRE(error.empty());
    REQUIRE(session->info().protocol == 1);
    REQUIRE(session->info().pid == getpid());
    REQUIRE(session->info().start_ticks == 8877);
    REQUIRE(session->info().features == std::vector<std::string>{"hooks", "events"});
}

TEST_CASE("connect surfaces an INFO error payload verbatim", "[target_session]") {
    FakeLoader loader;
    loader.push_response("ERR not ready\n");

    std::string error;
    auto session = TargetSession::connect(loader.socket_path(), &error);

    REQUIRE_FALSE(session.has_value());
    REQUIRE(error == "ERR not ready\n");
}

TEST_CASE("connect rejects an INFO response missing an identity field", "[target_session]") {
    FakeLoader loader;
    loader.push_response("OK protocol=1 uid=" + std::to_string(geteuid()) + " start=8877\n");

    std::string error;
    auto session = TargetSession::connect(loader.socket_path(), &error);

    REQUIRE_FALSE(session.has_value());
    REQUIRE(error == "INFO response is missing a valid identity field");
}

TEST_CASE("connect rejects an unsupported protocol version", "[target_session]") {
    FakeLoader loader;
    loader.push_response("OK protocol=2 pid=" + std::to_string(getpid()) +
                         " uid=" + std::to_string(geteuid()) + " start=8877\n");

    std::string error;
    auto session = TargetSession::connect(loader.socket_path(), &error);

    REQUIRE_FALSE(session.has_value());
    REQUIRE(error == "unsupported loader protocol 2");
}

TEST_CASE("connect rejects a zero process start time", "[target_session]") {
    FakeLoader loader;
    loader.push_response("OK protocol=1 pid=" + std::to_string(getpid()) +
                         " uid=" + std::to_string(geteuid()) + " start=0\n");

    std::string error;
    auto session = TargetSession::connect(loader.socket_path(), &error);

    REQUIRE_FALSE(session.has_value());
    REQUIRE(error == "loader did not provide a stable process start time");
}

TEST_CASE("connect rejects an identity that disagrees with the Unix peer credentials",
          "[target_session]") {
    FakeLoader loader;
    // The socket really is served by this process, so an advertised pid of
    // 999999 cannot match what SO_PEERCRED reports.
    loader.push_response("OK protocol=1 pid=999999 uid=" + std::to_string(geteuid()) +
                         " start=8877\n");

    std::string error;
    auto session = TargetSession::connect(loader.socket_path(), &error);

    REQUIRE_FALSE(session.has_value());
    REQUIRE(error == "loader identity does not match Unix peer credentials");
}

// --- resolve ----------------------------------------------------------------

TEST_CASE("resolve chains image, class, and method lookups", "[target_session]") {
    FakeLoader loader;
    auto session = open(loader);
    loader.push_response("OK 7\n");
    loader.push_response("OK 21\n");
    loader.push_response("OK 42\n");

    std::string error;
    auto method = session.resolve(selector(), &error);

    REQUIRE(method.has_value());
    REQUIRE(*method == 42);
    const auto requests = loader.requests();
    REQUIRE(requests.size() == 4);
    REQUIRE(requests[1] == "RESOLVE-IMAGE Fake.dll");
    REQUIRE(requests[2] == "RESOLVE-CLASS 7 FakeNamespace FakeClass");
    REQUIRE(requests[3] == "RESOLVE-METHOD 21 FakeMethod 2");
}

TEST_CASE("resolve sends a dash for the empty namespace", "[target_session]") {
    FakeLoader loader;
    auto session = open(loader);
    loader.push_response("OK 7\n");
    loader.push_response("OK 21\n");
    loader.push_response("OK 42\n");

    MethodSelector global = selector();
    global.namespaze.clear();
    REQUIRE(session.resolve(global).has_value());

    REQUIRE(loader.requests()[2] == "RESOLVE-CLASS 7 - FakeClass");
}

TEST_CASE("a metadata-token selector resolves by token instead of arity", "[target_session]") {
    FakeLoader loader;
    auto session = open(loader);
    loader.push_response("OK 7\n");
    loader.push_response("OK 21\n");
    loader.push_response("OK 42\n");

    MethodSelector by_token = selector();
    by_token.token = 0x06000001;
    REQUIRE(session.resolve(by_token).has_value());

    REQUIRE(loader.requests()[3] == "RESOLVE-METHOD-TOKEN 21 100663297 FakeMethod");
}

TEST_CASE("resolve rejects a non-numeric OK payload", "[target_session]") {
    FakeLoader loader;
    auto session = open(loader);
    loader.push_response("OK notanumber\n");

    std::string error;
    auto method = session.resolve(selector(), &error);

    REQUIRE_FALSE(method.has_value());
    REQUIRE(error == "malformed numeric loader response");
}

TEST_CASE("resolve rejects a handle wider than int32", "[target_session]") {
    FakeLoader loader;
    auto session = open(loader);
    loader.push_response("OK 2147483648\n"); // INT32_MAX + 1

    REQUIRE_FALSE(session.resolve(selector()).has_value());
    // The chain stops at the image step rather than issuing RESOLVE-CLASS.
    REQUIRE(loader.requests().size() == 2);
}

TEST_CASE("resolve surfaces a loader error raised mid-chain", "[target_session]") {
    FakeLoader loader;
    auto session = open(loader);
    loader.push_response("OK 7\n");
    loader.push_response("ERR no such class\n");

    std::string error;
    auto method = session.resolve(selector(), &error);

    REQUIRE_FALSE(method.has_value());
    REQUIRE(error == "ERR no such class");
    REQUIRE(loader.requests().size() == 3);
}

// --- add_hook ---------------------------------------------------------------

TEST_CASE("add_hook returns a hook id scoped to the current session", "[target_session]") {
    FakeLoader loader;
    auto session = open(loader);
    loader.push_response("OK 7\n");
    loader.push_response("OK 21\n");
    loader.push_response("OK 42\n");
    loader.push_response("OK 3\n");

    std::string error;
    auto hook = session.add_hook(selector(), "replace", "breakpoint", "skip-return-void", &error);

    REQUIRE(hook.has_value());
    REQUIRE(hook->slot == 3);
    REQUIRE(hook->session == session_id());
    REQUIRE(loader.requests().back() == "HOOK 42 replace breakpoint skip-return-void");
}

TEST_CASE("add_hook rejects an invalid mode, transport, or handler without contacting the loader",
          "[target_session]") {
    FakeLoader loader;
    auto session = open(loader);
    const size_t before = loader.requests().size();

    std::string error;
    REQUIRE_FALSE(session.add_hook(selector(), "sideways", "breakpoint", "h", &error).has_value());
    REQUIRE(error == "invalid hook mode, transport, or handler");
    REQUIRE_FALSE(session.add_hook(selector(), "replace", "carrier-pigeon", "h").has_value());
    REQUIRE_FALSE(session.add_hook(selector(), "replace", "breakpoint", "").has_value());

    REQUIRE(loader.requests().size() == before);
}

TEST_CASE("around hooks require the trampoline transport", "[target_session]") {
    FakeLoader loader;
    auto session = open(loader);

    std::string error;
    auto hook = session.add_hook(selector(), "around", "breakpoint", "log-1string-arg", &error);

    REQUIRE_FALSE(hook.has_value());
    REQUIRE(error == "around hooks require trampoline transport");
}

TEST_CASE("add_hook rejects a slot wider than uint32", "[target_session]") {
    FakeLoader loader;
    auto session = open(loader);
    loader.push_response("OK 7\n");
    loader.push_response("OK 21\n");
    loader.push_response("OK 42\n");
    loader.push_response("OK 4294967296\n"); // UINT32_MAX + 1

    REQUIRE_FALSE(
        session.add_hook(selector(), "replace", "breakpoint", "skip-return-void").has_value());
}

// --- hook id validation, stats, removal -------------------------------------

TEST_CASE("hook operations reject an id belonging to another session", "[target_session]") {
    FakeLoader loader;
    auto session = open(loader);
    const HookId foreign{"4242:1111", 0};
    const size_t before = loader.requests().size();

    std::string error;
    REQUIRE_FALSE(session.hook_stats(foreign, &error).has_value());
    REQUIRE(error == "hook belongs to target session 4242:1111, current session is " + session_id());
    REQUIRE_FALSE(session.remove_hook(foreign));

    REQUIRE(loader.requests().size() == before);
}

TEST_CASE("hook_stats returns the loader's hit counter", "[target_session]") {
    FakeLoader loader;
    auto session = open(loader);
    loader.push_response("OK 1234\n");

    auto hits = session.hook_stats(HookId{session_id(), 3});

    REQUIRE(hits.has_value());
    REQUIRE(*hits == 1234);
    REQUIRE(loader.requests().back() == "HOOK-STATS 3");
}

TEST_CASE("remove_hook accepts only a bare OK acknowledgement", "[target_session]") {
    FakeLoader loader;
    auto session = open(loader);
    loader.push_response("OK\n");
    REQUIRE(session.remove_hook(HookId{session_id(), 3}));
    REQUIRE(loader.requests().back() == "UNHOOK 3");

    loader.push_response("OK 3\n");
    std::string error;
    REQUIRE_FALSE(session.remove_hook(HookId{session_id(), 3}, &error));
    REQUIRE(error == "OK 3\n");
}

// --- list_hooks -------------------------------------------------------------

TEST_CASE("list_hooks parses entries and distinguishes an absent counter", "[target_session]") {
    FakeLoader loader;
    auto session = open(loader);
    loader.push_response(
        "OK 2\n"
        "HOOK 0 replace breakpoint skip-return-void Fake.dll!Ns.C::M@0x06000001 -\n"
        "HOOK 1 around trampoline log-1string-arg Fake.dll!Ns.C::Log@0x06000002 17\n");

    auto hooks = session.list_hooks();

    REQUIRE(hooks.has_value());
    REQUIRE(hooks->size() == 2);
    REQUIRE((*hooks)[0].slot == 0);
    REQUIRE((*hooks)[0].transport == "breakpoint");
    REQUIRE_FALSE((*hooks)[0].hits.has_value());
    REQUIRE((*hooks)[1].mode == "around");
    REQUIRE((*hooks)[1].method == "Fake.dll!Ns.C::Log@0x06000002");
    REQUIRE((*hooks)[1].hits == 17);
}

TEST_CASE("list_hooks rejects a count that disagrees with its entries", "[target_session]") {
    FakeLoader loader;
    auto session = open(loader);
    loader.push_response("OK 2\n"
                         "HOOK 0 replace breakpoint skip-return-void Fake.dll!Ns.C::M -\n");

    std::string error;
    auto hooks = session.list_hooks(&error);

    REQUIRE_FALSE(hooks.has_value());
    REQUIRE(error == "LIST-HOOKS count does not match its entries");
}

TEST_CASE("list_hooks rejects a truncated hook entry", "[target_session]") {
    FakeLoader loader;
    auto session = open(loader);
    loader.push_response("OK 1\n"
                         "HOOK 0 replace breakpoint\n");

    std::string error;
    auto hooks = session.list_hooks(&error);

    REQUIRE_FALSE(hooks.has_value());
    REQUIRE(error == "malformed hook entry: HOOK 0 replace breakpoint");
}

TEST_CASE("list_hooks rejects a non-numeric hit counter", "[target_session]") {
    FakeLoader loader;
    auto session = open(loader);
    loader.push_response("OK 1\n"
                         "HOOK 0 replace breakpoint skip-return-void Fake.dll!Ns.C::M many\n");

    std::string error;
    auto hooks = session.list_hooks(&error);

    REQUIRE_FALSE(hooks.has_value());
    REQUIRE(error == "malformed hook counter: many");
}

// --- events -----------------------------------------------------------------

TEST_CASE("events_info parses the ring buffer counters", "[target_session]") {
    FakeLoader loader;
    auto session = open(loader);
    loader.push_response("OK next=10 oldest=1 capacity=256 dropped=4\n");

    auto info = session.events_info();

    REQUIRE(info.has_value());
    REQUIRE(info->next_sequence == 10);
    REQUIRE(info->oldest_sequence == 1);
    REQUIRE(info->capacity == 256);
    REQUIRE(info->dropped == 4);
}

TEST_CASE("events_info rejects a response with a zeroed counter", "[target_session]") {
    FakeLoader loader;
    auto session = open(loader);
    loader.push_response("OK next=10 oldest=1 capacity=0 dropped=0\n");

    std::string error;
    REQUIRE_FALSE(session.events_info(&error).has_value());
    REQUIRE(error == "malformed EVENTS-INFO response");
}

TEST_CASE("read_events rejects a limit outside 1..256 without contacting the loader",
          "[target_session]") {
    FakeLoader loader;
    auto session = open(loader);
    const size_t before = loader.requests().size();

    std::string error;
    REQUIRE_FALSE(session.read_events(0, 0, &error).has_value());
    REQUIRE(error == "event read limit must be between 1 and 256");
    REQUIRE_FALSE(session.read_events(0, 257).has_value());

    REQUIRE(loader.requests().size() == before);
}

TEST_CASE("read_events parses a batch header and its entries", "[target_session]") {
    FakeLoader loader;
    auto session = open(loader);
    loader.push_response("OK 2 next=7 dropped=1\n"
                         "EVENT 5 1700000000 91 0 hook-added Fake.dll!Ns.C::M\n"
                         "EVENT 6 1700000001 91 0 hook-removed Fake.dll!Ns.C::M\n");

    auto batch = session.read_events(4, 16);

    REQUIRE(batch.has_value());
    REQUIRE(batch->next_sequence == 7);
    REQUIRE(batch->dropped == 1);
    REQUIRE(batch->events.size() == 2);
    REQUIRE(batch->events[0].sequence == 5);
    REQUIRE(batch->events[0].timestamp_ns == 1700000000);
    REQUIRE(batch->events[0].thread_id == 91);
    REQUIRE(batch->events[1].type == "hook-removed");
    REQUIRE(loader.requests().back() == "EVENTS-READ 4 16");
}

TEST_CASE("read_events rejects a header missing its next/dropped fields", "[target_session]") {
    FakeLoader loader;
    auto session = open(loader);
    loader.push_response("OK 0 nxt=7 dropped=1\n");

    std::string error;
    REQUIRE_FALSE(session.read_events(0, 16, &error).has_value());
    REQUIRE(error == "OK 0 nxt=7 dropped=1");
}

TEST_CASE("read_events rejects a count that disagrees with its entries", "[target_session]") {
    FakeLoader loader;
    auto session = open(loader);
    loader.push_response("OK 3 next=7 dropped=0\n"
                         "EVENT 5 1700000000 91 0 hook-added Fake.dll!Ns.C::M\n");

    std::string error;
    REQUIRE_FALSE(session.read_events(0, 16, &error).has_value());
    REQUIRE(error == "EVENTS-READ count does not match its entries");
}

TEST_CASE("read_events rejects a truncated event entry", "[target_session]") {
    FakeLoader loader;
    auto session = open(loader);
    loader.push_response("OK 1 next=7 dropped=0\n"
                         "EVENT 5 1700000000 91\n");

    std::string error;
    REQUIRE_FALSE(session.read_events(0, 16, &error).has_value());
    REQUIRE(error == "malformed event entry: EVENT 5 1700000000 91");
}
