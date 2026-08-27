#include "cli_runner.hpp"
#include "fake_loader.hpp"
#include "il2bridge/broker/metadata_structs.hpp"
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <unistd.h>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using il2bridge::testing::FakeLoader;
using il2bridge::testing::run_cli;
using json = nlohmann::json;

namespace {

// The CLI runs as a child, so the peer it observes is this test process: the
// identity it accepts must therefore advertise our pid and uid.
std::string valid_info() {
    return "OK protocol=1 pid=" + std::to_string(getpid()) +
           " uid=" + std::to_string(geteuid()) + " start=8877 features=hooks,events\n";
}

std::string session_id() {
    return std::to_string(getpid()) + ":8877";
}

std::vector<std::string> with_socket(const FakeLoader& loader,
                                     std::vector<std::string> rest) {
    std::vector<std::string> arguments{"--socket", loader.socket_path().string()};
    arguments.insert(arguments.end(), rest.begin(), rest.end());
    return arguments;
}

} // namespace

// --- argument parsing and dispatch ------------------------------------------

TEST_CASE("no arguments prints usage and fails", "[cli]") {
    auto result = run_cli({});

    REQUIRE(result.exit_code == 1);
    REQUIRE(result.out.empty());
    REQUIRE(result.err.starts_with("Usage:"));
}

TEST_CASE("an unknown command prints usage and fails", "[cli]") {
    auto result = run_cli({"frobnicate"});

    REQUIRE(result.exit_code == 1);
    REQUIRE(result.err.starts_with("Usage:"));
}

TEST_CASE("--pid rejects a non-positive or non-numeric value", "[cli]") {
    for (const char* value : {"0", "-3", "abc", "12x"}) {
        auto result = run_cli({"--pid", value, "status"});
        REQUIRE(result.exit_code == 1);
        REQUIRE(result.err == "Error: --pid requires a positive integer\n");
    }
}

TEST_CASE("--pid and --socket are mutually exclusive", "[cli]") {
    auto result = run_cli({"--pid", "1", "--socket", "/tmp/nope.sock", "status"});

    REQUIRE(result.exit_code == 1);
    REQUIRE(result.err == "Error: --pid and --socket are mutually exclusive\n");
}

TEST_CASE("--json routes errors to stdout as a structured object", "[cli]") {
    auto result = run_cli({"--json", "--pid", "0", "status"});

    REQUIRE(result.exit_code == 1);
    REQUIRE(result.err.empty());
    auto parsed = json::parse(result.out);
    REQUIRE(parsed["ok"] == false);
    REQUIRE(parsed["error"] == "--pid requires a positive integer");
}

TEST_CASE("status rejects trailing arguments", "[cli]") {
    auto result = run_cli({"status", "extra"});

    REQUIRE(result.exit_code == 1);
    REQUIRE(result.err.starts_with("Usage:"));
}

TEST_CASE("an unreachable socket reports the connection error", "[cli]") {
    auto result = run_cli({"--json", "--socket", "/tmp/il2bridge-absent-test.sock", "status"});

    REQUIRE(result.exit_code == 1);
    auto parsed = json::parse(result.out);
    REQUIRE(parsed["ok"] == false);
    REQUIRE(std::string(parsed["error"]).starts_with("connect: "));
}

// --- status -----------------------------------------------------------------

TEST_CASE("status reports identity, hooks, and event counters as JSON", "[cli]") {
    FakeLoader loader;
    loader.on("INFO", valid_info());
    loader.on("LIST-HOOKS", "OK 1\nHOOK 0 replace breakpoint skip-return-void A.dll!N.C::M 5\n");
    loader.on("EVENTS-INFO", "OK next=10 oldest=1 capacity=256 dropped=2\n");

    auto result = run_cli(with_socket(loader, {"--json", "status"}));

    REQUIRE(result.exit_code == 0);
    auto parsed = json::parse(result.out);
    REQUIRE(parsed["ok"] == true);
    REQUIRE(parsed["session"] == session_id());
    REQUIRE(parsed["pid"] == getpid());
    REQUIRE(parsed["protocol"] == 1);
    REQUIRE(parsed["activeHooks"] == 1);
    REQUIRE(parsed["eventCapacity"] == 256);
    REQUIRE(parsed["droppedEvents"] == 2);
    REQUIRE(parsed["features"] == json::array({"hooks", "events"}));
}

TEST_CASE("status renders a human-readable summary without --json", "[cli]") {
    FakeLoader loader;
    loader.on("INFO", valid_info());
    loader.on("LIST-HOOKS", "OK 0\n");
    loader.on("EVENTS-INFO", "OK next=10 oldest=1 capacity=256 dropped=0\n");

    auto result = run_cli(with_socket(loader, {"status"}));

    REQUIRE(result.exit_code == 0);
    REQUIRE(result.out.starts_with("Il2Bridge target " + session_id() + "\n"));
    REQUIRE(result.out.find("Active hooks: 0\n") != std::string::npos);
    REQUIRE(result.out.find("Events: next=10 capacity=256 dropped=0\n") != std::string::npos);
}

TEST_CASE("status fails when the loader rejects a follow-up query", "[cli]") {
    FakeLoader loader;
    loader.on("INFO", valid_info());
    loader.on("LIST-HOOKS", "ERR registry unavailable\n");

    auto result = run_cli(with_socket(loader, {"--json", "status"}));

    REQUIRE(result.exit_code == 1);
    REQUIRE(json::parse(result.out)["error"] == "ERR registry unavailable");
}

// --- hook list --------------------------------------------------------------

TEST_CASE("hook list emits one JSON entry per hook with a null absent counter", "[cli]") {
    FakeLoader loader;
    loader.on("INFO", valid_info());
    loader.on("LIST-HOOKS",
              "OK 2\n"
              "HOOK 0 replace breakpoint skip-return-void A.dll!N.C::M -\n"
              "HOOK 1 around trampoline log-1string-arg A.dll!N.C::Log 17\n");

    auto result = run_cli(with_socket(loader, {"--json", "hook", "list"}));

    REQUIRE(result.exit_code == 0);
    auto hooks = json::parse(result.out)["hooks"];
    REQUIRE(hooks.size() == 2);
    REQUIRE(hooks[0]["id"] == session_id() + ":0");
    REQUIRE(hooks[0]["hits"].is_null());
    REQUIRE(hooks[1]["id"] == session_id() + ":1");
    REQUIRE(hooks[1]["hits"] == 17);
    REQUIRE(hooks[1]["transport"] == "trampoline");
}

TEST_CASE("hook list says so when the registry is empty", "[cli]") {
    FakeLoader loader;
    loader.on("INFO", valid_info());
    loader.on("LIST-HOOKS", "OK 0\n");

    auto result = run_cli(with_socket(loader, {"hook", "list"}));

    REQUIRE(result.exit_code == 0);
    REQUIRE(result.out == "No active hooks.\n");
}

// --- hook add ---------------------------------------------------------------

TEST_CASE("hook add requires a method selector", "[cli]") {
    auto result = run_cli({"--json", "hook", "add"});

    REQUIRE(result.exit_code == 1);
    REQUIRE(result.err.starts_with("Usage:"));
}

TEST_CASE("hook add rejects a malformed selector before contacting a target", "[cli]") {
    auto result = run_cli({"--json", "--socket", "/tmp/il2bridge-absent-test.sock",
                           "hook", "add", "not-a-selector"});

    REQUIRE(result.exit_code == 1);
    // A connect error here would mean the selector was accepted.
    REQUIRE_FALSE(std::string(json::parse(result.out)["error"]).starts_with("connect: "));
}

TEST_CASE("hook add defaults to an around/trampoline hook", "[cli]") {
    FakeLoader loader;
    loader.on("INFO", valid_info());
    loader.on("RESOLVE-IMAGE", "OK 7\n");
    loader.on("RESOLVE-CLASS", "OK 21\n");
    loader.on("RESOLVE-METHOD", "OK 42\n");
    loader.on("HOOK", "OK 3\n");

    auto result = run_cli(with_socket(loader, {"--json", "hook", "add", "A.dll!N.C::M/0"}));

    REQUIRE(result.exit_code == 0);
    auto parsed = json::parse(result.out);
    REQUIRE(parsed["id"] == session_id() + ":3");
    REQUIRE(parsed["mode"] == "around");
    REQUIRE(parsed["transport"] == "trampoline");
    REQUIRE(parsed["handler"] == "count-calls");
    const auto requests = loader.requests();
    REQUIRE(requests.back() == "HOOK 42 around trampoline count-calls");
}

TEST_CASE("hook add in replace mode defaults to the breakpoint transport", "[cli]") {
    FakeLoader loader;
    loader.on("INFO", valid_info());
    loader.on("RESOLVE-IMAGE", "OK 7\n");
    loader.on("RESOLVE-CLASS", "OK 21\n");
    loader.on("RESOLVE-METHOD", "OK 42\n");
    loader.on("HOOK", "OK 3\n");

    auto result = run_cli(with_socket(
        loader, {"--json", "hook", "add", "A.dll!N.C::M/0", "--mode", "replace",
                 "--handler", "skip-return-void"}));

    REQUIRE(result.exit_code == 0);
    REQUIRE(json::parse(result.out)["transport"] == "breakpoint");
    REQUIRE(loader.requests().back() == "HOOK 42 replace breakpoint skip-return-void");
}

TEST_CASE("an explicit --transport overrides the mode default", "[cli]") {
    // replace defaults to breakpoint, so asking for trampoline is observable.
    FakeLoader loader;
    loader.on("INFO", valid_info());
    loader.on("RESOLVE-IMAGE", "OK 7\n");
    loader.on("RESOLVE-CLASS", "OK 21\n");
    loader.on("RESOLVE-METHOD", "OK 42\n");
    loader.on("HOOK", "OK 3\n");

    auto result = run_cli(with_socket(
        loader, {"--json", "hook", "add", "A.dll!N.C::M/0", "--mode", "replace",
                 "--transport", "trampoline", "--handler", "skip-return-void"}));

    REQUIRE(result.exit_code == 0);
    REQUIRE(json::parse(result.out)["transport"] == "trampoline");
    REQUIRE(loader.requests().back() == "HOOK 42 replace trampoline skip-return-void");
}

TEST_CASE("hook add rejects an unknown or dangling option", "[cli]") {
    auto result = run_cli({"--json", "hook", "add", "A.dll!N.C::M/0", "--frobnicate", "x"});
    REQUIRE(result.exit_code == 1);
    REQUIRE(json::parse(result.out)["error"] == "unknown or incomplete hook add option: --frobnicate");

    auto dangling = run_cli({"--json", "hook", "add", "A.dll!N.C::M/0", "--mode"});
    REQUIRE(dangling.exit_code == 1);
    REQUIRE(json::parse(dangling.out)["error"] == "unknown or incomplete hook add option: --mode");
}

// --- hook stats and remove --------------------------------------------------

TEST_CASE("hook stats and remove reject a malformed hook id", "[cli]") {
    for (const char* action : {"stats", "remove"}) {
        auto result = run_cli({"--json", "hook", action, "nonsense"});
        REQUIRE(result.exit_code == 1);
        REQUIRE(json::parse(result.out)["error"] ==
                "invalid hook id; expected <pid>:<start>:<slot>");
    }
}

TEST_CASE("hook stats reports the counter for a hook of this session", "[cli]") {
    FakeLoader loader;
    loader.on("INFO", valid_info());
    loader.on("HOOK-STATS", "OK 1234\n");

    auto result = run_cli(with_socket(loader, {"--json", "hook", "stats", session_id() + ":3"}));

    REQUIRE(result.exit_code == 0);
    auto parsed = json::parse(result.out);
    REQUIRE(parsed["hits"] == 1234);
    REQUIRE(parsed["id"] == session_id() + ":3");
    REQUIRE(loader.requests().back() == "HOOK-STATS 3");
}

TEST_CASE("hook remove acknowledges a removal", "[cli]") {
    FakeLoader loader;
    loader.on("INFO", valid_info());
    loader.on("UNHOOK", "OK\n");

    auto result = run_cli(with_socket(loader, {"hook", "remove", session_id() + ":3"}));

    REQUIRE(result.exit_code == 0);
    REQUIRE(result.out == "Hook " + session_id() + ":3 removed.\n");
    REQUIRE(loader.requests().back() == "UNHOOK 3");
}

TEST_CASE("hook operations refuse an id from another session", "[cli]") {
    FakeLoader loader;
    loader.on("INFO", valid_info());

    auto result = run_cli(with_socket(loader, {"--json", "hook", "stats", "4242:1111:3"}));

    REQUIRE(result.exit_code == 1);
    REQUIRE(std::string(json::parse(result.out)["error"]).starts_with("hook belongs to target session"));
}

// --- events -----------------------------------------------------------------

TEST_CASE("events read emits the batch and its sequence counters", "[cli]") {
    FakeLoader loader;
    loader.on("INFO", valid_info());
    loader.on("EVENTS-READ",
              "OK 2 next=7 dropped=1\n"
              "EVENT 5 1700000000 91 0 hook-added A.dll!N.C::M\n"
              "EVENT 6 1700000001 91 1 hook-removed A.dll!N.C::M\n");

    auto result = run_cli(with_socket(loader, {"--json", "events", "read"}));

    REQUIRE(result.exit_code == 0);
    auto parsed = json::parse(result.out);
    REQUIRE(parsed["nextSequence"] == 7);
    REQUIRE(parsed["dropped"] == 1);
    REQUIRE(parsed["events"].size() == 2);
    REQUIRE(parsed["events"][0]["hookId"] == session_id() + ":0");
    REQUIRE(parsed["events"][1]["type"] == "hook-removed");
    REQUIRE(loader.requests().back() == "EVENTS-READ 0 128");
}

TEST_CASE("events read passes --after and --limit through to the loader", "[cli]") {
    FakeLoader loader;
    loader.on("INFO", valid_info());
    loader.on("EVENTS-READ", "OK 0 next=7 dropped=0\n");

    auto result = run_cli(with_socket(loader, {"events", "read", "--after", "4", "--limit", "16"}));

    REQUIRE(result.exit_code == 0);
    REQUIRE(result.out == "No events. next=7 dropped=0\n");
    REQUIRE(loader.requests().back() == "EVENTS-READ 4 16");
}

TEST_CASE("events read validates --after and --limit", "[cli]") {
    auto bad_after = run_cli({"--json", "events", "read", "--after", "-1"});
    REQUIRE(bad_after.exit_code == 1);
    REQUIRE(json::parse(bad_after.out)["error"] == "--after requires an unsigned sequence");

    for (const char* limit : {"0", "257"}) {
        auto result = run_cli({"--json", "events", "read", "--limit", limit});
        REQUIRE(result.exit_code == 1);
        REQUIRE(json::parse(result.out)["error"] == "--limit must be between 1 and 256");
    }
}

TEST_CASE("hook watch refuses an id belonging to another session", "[cli]") {
    FakeLoader loader;
    loader.on("INFO", valid_info());

    auto result = run_cli(with_socket(loader, {"--json", "hook", "watch", "4242:1111:3"}));

    REQUIRE(result.exit_code == 1);
    REQUIRE(json::parse(result.out)["error"] == "hook id belongs to another target session");
}

// --- targets ----------------------------------------------------------------

TEST_CASE("targets list emits a JSON array", "[cli]") {
    auto result = run_cli({"--json", "targets", "list"});

    REQUIRE(result.exit_code == 0);
    auto parsed = json::parse(result.out);
    REQUIRE(parsed["ok"] == true);
    REQUIRE(parsed["targets"].is_array());
}

// --- enumerate --------------------------------------------------------------

TEST_CASE("enumerate reports a metadata file it cannot locate", "[cli]") {
    auto result = run_cli({"enumerate", "/nonexistent/Game.x86_64"});

    REQUIRE(result.exit_code == 1);
    REQUIRE(result.err.starts_with("Could not locate global-metadata.dat"));
}

TEST_CASE("enumerate reports a metadata file it cannot parse", "[cli]") {
    const auto directory = std::filesystem::temp_directory_path() / "il2bridge-cli-bad";
    std::filesystem::create_directories(directory);
    const auto path = directory / "global-metadata.dat";
    {
        std::ofstream file(path, std::ios::binary);
        const std::string junk(256, '\0');
        file.write(junk.data(), static_cast<std::streamsize>(junk.size()));
    }

    auto result = run_cli({"enumerate", path.string()});

    REQUIRE(result.exit_code == 1);
    REQUIRE(result.err.starts_with("Failed to parse metadata"));
    std::filesystem::remove_all(directory);
}

TEST_CASE("enumerate emits an empty array for metadata with no images", "[cli]") {
    // A header-only v39 file: valid to parse, with every section empty.
    il2bridge::Il2CppGlobalMetadataHeader header{};
    header.magicNumber = il2bridge::kIl2CppMetadataMagic;
    header.version = il2bridge::kIl2CppMetadataVersion;
    // The index-width computation divides by this section, so it cannot be zero.
    header.interfaceOffsets.count = 1;
    header.interfaceOffsets.size = 8;
    const auto directory = std::filesystem::temp_directory_path() / "il2bridge-cli-empty";
    std::filesystem::create_directories(directory);
    const auto path = directory / "global-metadata.dat";
    {
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(&header), sizeof(header));
    }

    auto result = run_cli({"enumerate", path.string()});

    REQUIRE(result.exit_code == 0);
    REQUIRE(json::parse(result.out).is_array());
    REQUIRE(json::parse(result.out).empty());
    std::filesystem::remove_all(directory);
}
