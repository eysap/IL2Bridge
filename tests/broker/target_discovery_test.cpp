#include "fake_loader.hpp"
#include "il2bridge/broker/target_discovery.hpp"
#include <catch2/catch_test_macros.hpp>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace il2bridge;
using il2bridge::testing::FakeLoader;

namespace {

// Points XDG_RUNTIME_DIR at a private directory for the duration of a test, so
// TargetDiscovery scans sockets this test controls instead of the real ones.
class ScopedRuntimeDir {
public:
    ScopedRuntimeDir() {
        if (const char* previous = std::getenv("XDG_RUNTIME_DIR")) {
            had_previous_ = true;
            previous_ = previous;
        }
        std::string pattern =
            (std::filesystem::temp_directory_path() / "il2bridge-runtime-XXXXXX").string();
        std::vector<char> buffer(pattern.begin(), pattern.end());
        buffer.push_back('\0');
        root_ = mkdtemp(buffer.data());
        std::filesystem::create_directory(root_ / "il2bridge");
        setenv("XDG_RUNTIME_DIR", root_.c_str(), 1);
    }

    ~ScopedRuntimeDir() {
        if (had_previous_) {
            setenv("XDG_RUNTIME_DIR", previous_.c_str(), 1);
        } else {
            unsetenv("XDG_RUNTIME_DIR");
        }
        std::error_code ignored;
        std::filesystem::remove_all(root_, ignored);
    }

    std::filesystem::path socket_dir() const { return root_ / "il2bridge"; }
    std::filesystem::path socket_for(int pid) const {
        return socket_dir() / (std::to_string(pid) + ".sock");
    }

private:
    std::filesystem::path root_;
    bool had_previous_ = false;
    std::string previous_;
};

std::string info_with_start(uint64_t start) {
    return "OK protocol=1 pid=" + std::to_string(getpid()) +
           " uid=" + std::to_string(geteuid()) + " start=" + std::to_string(start) + "\n";
}

bool contains_socket(const std::vector<TargetIdentity>& targets,
                     const std::filesystem::path& socket) {
    return std::any_of(targets.begin(), targets.end(),
                       [&](const TargetIdentity& t) { return t.socket_path == socket; });
}

} // namespace

TEST_CASE("from_socket connects to an explicit socket path", "[target_discovery]") {
    FakeLoader loader;
    loader.push_response(info_with_start(4242));

    std::string error;
    auto session = TargetDiscovery().from_socket(loader.socket_path(), &error);

    REQUIRE(session.has_value());
    REQUIRE(session->info().start_ticks == 4242);
    REQUIRE(session->socket_path() == loader.socket_path());
}

TEST_CASE("from_socket surfaces the connection error", "[target_discovery]") {
    FakeLoader loader;
    const auto missing = loader.socket_path().parent_path() / "absent.sock";

    std::string error;
    auto session = TargetDiscovery().from_socket(missing, &error);

    REQUIRE_FALSE(session.has_value());
    REQUIRE(error.starts_with("connect: "));
}

TEST_CASE("from_pid rejects a non-positive pid", "[target_discovery]") {
    std::string error;

    REQUIRE_FALSE(TargetDiscovery().from_pid(0, &error).has_value());
    REQUIRE(error == "PID must be positive");
    REQUIRE_FALSE(TargetDiscovery().from_pid(-1).has_value());
}

TEST_CASE("from_pid finds the well-known socket under XDG_RUNTIME_DIR", "[target_discovery]") {
    ScopedRuntimeDir runtime;
    FakeLoader loader(runtime.socket_for(getpid()));
    loader.always_respond(info_with_start(4242));

    std::string error;
    auto session = TargetDiscovery().from_pid(getpid(), &error);

    REQUIRE(session.has_value());
    REQUIRE(session->info().pid == getpid());
    REQUIRE(session->socket_path() == runtime.socket_for(getpid()));
}

TEST_CASE("from_pid reports no loader when nothing serves that pid", "[target_discovery]") {
    ScopedRuntimeDir runtime;

    std::string error;
    auto session = TargetDiscovery().from_pid(424242, &error);

    REQUIRE_FALSE(session.has_value());
    REQUIRE(error == "no Il2Bridge loader found for PID 424242");
}

TEST_CASE("from_pid rejects a loader whose identity disagrees with the socket name",
          "[target_discovery]") {
    ScopedRuntimeDir runtime;
    // The socket is named for pid 424242 but the loader behind it reports this
    // process, so the name must not be trusted on its own.
    FakeLoader loader(runtime.socket_for(424242));
    loader.always_respond(info_with_start(4242));

    std::string error;
    auto session = TargetDiscovery().from_pid(424242, &error);

    REQUIRE_FALSE(session.has_value());
    REQUIRE(error == "no Il2Bridge loader found for PID 424242");
}

TEST_CASE("discover reports a reachable target with its socket and identity",
          "[target_discovery]") {
    ScopedRuntimeDir runtime;
    FakeLoader loader(runtime.socket_for(getpid()));
    loader.always_respond(info_with_start(4242));

    auto targets = TargetDiscovery().discover();

    REQUIRE(contains_socket(targets, runtime.socket_for(getpid())));
    auto found = std::find_if(targets.begin(), targets.end(), [&](const TargetIdentity& t) {
        return t.socket_path == runtime.socket_for(getpid());
    });
    REQUIRE(found->info.start_ticks == 4242);
}

TEST_CASE("discover collapses two sockets serving the same session", "[target_discovery]") {
    ScopedRuntimeDir runtime;
    FakeLoader first(runtime.socket_dir() / "1000.sock");
    FakeLoader second(runtime.socket_dir() / "2000.sock");
    // Same pid and start tick: one loader, reachable at two paths.
    first.always_respond(info_with_start(4242));
    second.always_respond(info_with_start(4242));

    auto targets = TargetDiscovery().discover();

    const bool has_first = contains_socket(targets, runtime.socket_dir() / "1000.sock");
    const bool has_second = contains_socket(targets, runtime.socket_dir() / "2000.sock");
    REQUIRE((has_first != has_second));
}

// Note: this pins the end-to-end guarantee that an unreachable candidate is
// never reported. It does not isolate the S_ISSOCK filter in
// candidate_sockets(), which is masked by the connect() failure downstream and
// stays uncovered while that helper is private.
TEST_CASE("discover never reports a candidate it could not connect to", "[target_discovery]") {
    ScopedRuntimeDir runtime;
    const auto decoy = runtime.socket_dir() / "9999.sock";
    std::ofstream(decoy) << "a regular file wearing a socket name";
    FakeLoader reachable(runtime.socket_for(getpid()));
    reachable.always_respond(info_with_start(4242));

    auto targets = TargetDiscovery().discover();

    REQUIRE_FALSE(contains_socket(targets, decoy));
    // The reachable socket alongside it still is, so the filter is not blanket.
    REQUIRE(contains_socket(targets, runtime.socket_for(getpid())));
}

TEST_CASE("discover ignores runtime-dir entries without a .sock suffix", "[target_discovery]") {
    ScopedRuntimeDir runtime;
    const auto misnamed = runtime.socket_dir() / "loader.txt";
    FakeLoader loader(misnamed);
    loader.always_respond(info_with_start(4242));

    auto targets = TargetDiscovery().discover();

    REQUIRE_FALSE(contains_socket(targets, misnamed));
}

TEST_CASE("select_one refuses to choose between several targets", "[target_discovery]") {
    ScopedRuntimeDir runtime;
    FakeLoader first(runtime.socket_dir() / "1000.sock");
    FakeLoader second(runtime.socket_dir() / "2000.sock");
    // Distinct start ticks make these two separate sessions.
    first.always_respond(info_with_start(4242));
    second.always_respond(info_with_start(9999));

    std::string error;
    auto session = TargetDiscovery().select_one(&error);

    REQUIRE_FALSE(session.has_value());
    REQUIRE(error == "multiple Il2Bridge targets found; use --pid or --socket");
}

TEST_CASE("select_one returns the sole reachable target", "[target_discovery]") {
    ScopedRuntimeDir runtime;
    if (!TargetDiscovery().discover().empty()) {
        SKIP("a real Il2Bridge loader is reachable via /tmp on this machine");
    }
    FakeLoader loader(runtime.socket_for(getpid()));
    loader.always_respond(info_with_start(4242));

    std::string error;
    auto session = TargetDiscovery().select_one(&error);

    REQUIRE(session.has_value());
    REQUIRE(session->socket_path() == runtime.socket_for(getpid()));
    REQUIRE(session->info().start_ticks == 4242);
}

TEST_CASE("select_one reports when nothing is reachable", "[target_discovery]") {
    ScopedRuntimeDir runtime;
    if (!TargetDiscovery().discover().empty()) {
        SKIP("a real Il2Bridge loader is reachable via /tmp on this machine");
    }

    std::string error;
    auto session = TargetDiscovery().select_one(&error);

    REQUIRE_FALSE(session.has_value());
    REQUIRE(error == "no Il2Bridge targets found");
}
