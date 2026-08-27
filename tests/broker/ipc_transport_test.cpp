#include "fake_loader.hpp"
#include "il2bridge/broker/ipc_transport.hpp"
#include <catch2/catch_test_macros.hpp>
#include <unistd.h>
#include <string>

using namespace il2bridge;
using il2bridge::testing::FakeLoader;

TEST_CASE("request delivers the command and reports the peer credentials", "[ipc_transport]") {
    FakeLoader loader;
    loader.push_response("OK PONG\n");

    IpcTransport transport(loader.socket_path());
    std::string error;
    auto response = transport.request("PING", &error);

    REQUIRE(response.has_value());
    REQUIRE(error.empty());
    REQUIRE(response->payload == "OK PONG\n");
    // The fake serves from this process, so SO_PEERCRED must report it.
    REQUIRE(response->peer_pid == getpid());
    REQUIRE(response->peer_uid == static_cast<int>(geteuid()));
    REQUIRE(loader.requests() == std::vector<std::string>{"PING"});
}

TEST_CASE("request rejects an empty command without opening a connection", "[ipc_transport]") {
    FakeLoader loader;

    IpcTransport transport(loader.socket_path());
    std::string error;

    REQUIRE_FALSE(transport.request("", &error).has_value());
    REQUIRE(error == "invalid IPC command");
    REQUIRE(loader.requests().empty());
}

TEST_CASE("request rejects a command containing a newline", "[ipc_transport]") {
    FakeLoader loader;

    IpcTransport transport(loader.socket_path());
    std::string error;

    // A smuggled newline would let a caller inject a second protocol command.
    REQUIRE_FALSE(transport.request("PING\nUNHOOK 0", &error).has_value());
    REQUIRE(error == "invalid IPC command");
    REQUIRE(loader.requests().empty());
}

TEST_CASE("request reports a connect failure when nothing is listening", "[ipc_transport]") {
    FakeLoader loader;
    const std::filesystem::path missing = loader.socket_path().parent_path() / "absent.sock";

    IpcTransport transport(missing);
    std::string error;

    REQUIRE_FALSE(transport.request("PING", &error).has_value());
    REQUIRE(error.starts_with("connect: "));
}

TEST_CASE("request rejects a path too long for sockaddr_un", "[ipc_transport]") {
    // sun_path holds 108 bytes including the terminator.
    IpcTransport transport(std::filesystem::path("/tmp/" + std::string(110, 'x')));
    std::string error;

    REQUIRE_FALSE(transport.request("PING", &error).has_value());
    REQUIRE(error == "socket path is too long");
}

TEST_CASE("request refuses a response larger than 1 MiB", "[ipc_transport]") {
    FakeLoader loader;
    loader.push_response(std::string(1024 * 1024 + 64, 'A'));

    IpcTransport transport(loader.socket_path());
    std::string error;

    REQUIRE_FALSE(transport.request("PING", &error).has_value());
    REQUIRE(error == "IPC response exceeds 1 MiB");
}

TEST_CASE("a peer that closes without replying yields an empty payload", "[ipc_transport]") {
    FakeLoader loader; // no response queued: the connection is accepted, then closed

    IpcTransport transport(loader.socket_path());
    auto response = transport.request("PING");

    REQUIRE(response.has_value());
    REQUIRE(response->payload.empty());
}

TEST_CASE("each request uses a fresh connection", "[ipc_transport]") {
    FakeLoader loader;
    loader.push_response("OK 1\n");
    loader.push_response("OK 2\n");

    IpcTransport transport(loader.socket_path());

    REQUIRE(transport.request("FIRST")->payload == "OK 1\n");
    REQUIRE(transport.request("SECOND")->payload == "OK 2\n");
    REQUIRE(loader.requests() == std::vector<std::string>{"FIRST", "SECOND"});
}
