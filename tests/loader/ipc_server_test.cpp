#include "il2bridge/loader/ipc.h"
#include "il2bridge/loader/bridge.h"
#include "il2bridge/loader/hooks.h"
#include "il2bridge/loader/events.h"
#include <catch2/catch_test_macros.hpp>
#include <dlfcn.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <chrono>

namespace {

std::string send_and_receive(const char* socket_path, const std::string& request) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return "";

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        close(fd);
        return "";
    }

    std::string line = request + "\n";
    write(fd, line.data(), line.size());

    std::string response;
    char buf[512];
    for (;;) {
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        response.append(buf, static_cast<size_t>(n));
    }
    close(fd);
    return response;
}

// Parses an opaque integer handle from an OK response.
int parse_ok_handle(const std::string& response) {
    if (response.rfind("OK ", 0) != 0) return -1;
    try {
        return std::stoi(response.substr(3));
    } catch (...) {
        return -1;
    }
}

} // namespace

TEST_CASE("ipc_socket_path builds the well-known per-pid path", "[ipc_server]") {
    char path[108];
    ipc_socket_path(1234, path, sizeof(path));
    const char* runtime = std::getenv("XDG_RUNTIME_DIR");
    const std::string expected = runtime && *runtime
        ? std::string(runtime) + "/il2bridge/1234.sock"
        : "/tmp/il2bridge-1234.sock";
    REQUIRE(std::string(path) == expected);
}

TEST_CASE("server responds OK PONG to PING over a real Unix socket", "[ipc_server]") {
    IpcServer* server = ipc_server_start();
    REQUIRE(server != nullptr);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    char path[64];
    ipc_socket_path(getpid(), path, sizeof(path));

    struct stat socket_status{};
    REQUIRE(stat(path, &socket_status) == 0);
    REQUIRE((socket_status.st_mode & 0777) == 0600);

    std::string response = send_and_receive(path, "PING");
    REQUIRE(response == "OK PONG\n");

    ipc_server_stop(server);
}

TEST_CASE("INFO exposes a generic versioned target identity", "[ipc_server]") {
    IpcServer* server = ipc_server_start();
    REQUIRE(server != nullptr);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    char path[64];
    ipc_socket_path(getpid(), path, sizeof(path));

    std::string response = send_and_receive(path, "INFO");
    REQUIRE(response.rfind("OK protocol=1 ", 0) == 0);
    REQUIRE(response.find("pid=" + std::to_string(getpid())) != std::string::npos);
    REQUIRE(response.find("features=info,list-hooks,resolve,resolve-token,hook,hook-stats,events") != std::string::npos);
    REQUIRE(response.find("start=0 ") == std::string::npos);
    ipc_server_stop(server);
}

TEST_CASE("LIST-HOOKS reports an empty authoritative registry", "[ipc_server]") {
    hook_registry_reset_for_testing();
    IpcServer* server = ipc_server_start();
    REQUIRE(server != nullptr);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    char path[64];
    ipc_socket_path(getpid(), path, sizeof(path));
    REQUIRE(send_and_receive(path, "LIST-HOOKS") == "OK 0\n");
    ipc_server_stop(server);
}

TEST_CASE("ipc_server_start fails gracefully if the path is already in use", "[ipc_server]") {
    IpcServer* first = ipc_server_start();
    REQUIRE(first != nullptr);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    IpcServer* second = ipc_server_start();
    REQUIRE(second == nullptr); // same pid -> same path -> bind fails, must not crash

    ipc_server_stop(first);
}

TEST_CASE("ipc_server_start reclaims an orphaned socket", "[ipc_server]") {
    char path[64];
    ipc_socket_path(getpid(), path, sizeof(path));
    unlink(path);

    int orphan = socket(AF_UNIX, SOCK_STREAM, 0);
    REQUIRE(orphan >= 0);
    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
    REQUIRE(bind(orphan, (struct sockaddr*)&addr, sizeof(addr)) == 0);
    close(orphan); // leaves a socket node with no listening endpoint

    IpcServer* server = ipc_server_start();
    REQUIRE(server != nullptr);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    REQUIRE(send_and_receive(path, "PING") == "OK PONG\n");
    ipc_server_stop(server);
}

TEST_CASE("ipc_server_stop removes the socket file so the same process can restart at the same path", "[ipc_server]") {
    IpcServer* first = ipc_server_start();
    REQUIRE(first != nullptr);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    ipc_server_stop(first);

    char path[64];
    ipc_socket_path(getpid(), path, sizeof(path));

    struct stat st{};
    REQUIRE(stat(path, &st) != 0);
    REQUIRE(errno == ENOENT);

    IpcServer* second = ipc_server_start();
    REQUIRE(second != nullptr); // must succeed now that the stale file is gone
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    REQUIRE(send_and_receive(path, "PING") == "OK PONG\n");

    ipc_server_stop(second);
}

TEST_CASE("server handles multiple sequential connections without needing a restart", "[ipc_server]") {
    IpcServer* server = ipc_server_start();
    REQUIRE(server != nullptr);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    char path[64];
    ipc_socket_path(getpid(), path, sizeof(path));

    REQUIRE(send_and_receive(path, "PING") == "OK PONG\n");
    REQUIRE(send_and_receive(path, "PING") == "OK PONG\n");
    REQUIRE(send_and_receive(path, "PING") == "OK PONG\n");

    ipc_server_stop(server);
}

TEST_CASE("server replies with an error for an unrecognized command instead of hanging or closing silently", "[ipc_server]") {
    IpcServer* server = ipc_server_start();
    REQUIRE(server != nullptr);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    char path[64];
    ipc_socket_path(getpid(), path, sizeof(path));

    std::string response = send_and_receive(path, "TOTALLY-BOGUS-COMMAND");
    REQUIRE(response.rfind("ERR", 0) == 0);

    ipc_server_stop(server);
}

TEST_CASE("an overlong line does not crash the server and the connection gets an error reply", "[ipc_server]") {
    IpcServer* server = ipc_server_start();
    REQUIRE(server != nullptr);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    char path[64];
    ipc_socket_path(getpid(), path, sizeof(path));

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    REQUIRE(fd >= 0);
    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
    REQUIRE(connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == 0);

    std::string huge(2048, 'a');
    huge += "\n";
    ssize_t written = write(fd, huge.data(), huge.size());
    REQUIRE(written > 0);

    char buf[64] = {};
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    REQUIRE(n > 0);
    REQUIRE(std::string(buf, n).rfind("ERR", 0) == 0);

    REQUIRE(send_and_receive(path, "PING") == "OK PONG\n");

    ipc_server_stop(server);
}

TEST_CASE("ipc_server_stop tolerates a null server", "[ipc_server]") {
    ipc_server_stop(nullptr); // must not crash
}

TEST_CASE("ipc_server_stop returns promptly even when a client connects and sends nothing", "[ipc_server]") {
    // Keep a connection idle to exercise the receive timeout during stop().
    IpcServer* server = ipc_server_start();
    REQUIRE(server != nullptr);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    char path[64];
    ipc_socket_path(getpid(), path, sizeof(path));

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    REQUIRE(fd >= 0);
    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
    REQUIRE(connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == 0);

    // Allow accept() to enter the timed read before requesting shutdown.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    auto start = std::chrono::steady_clock::now();
    ipc_server_stop(server);
    auto elapsed = std::chrono::steady_clock::now() - start;

    REQUIRE(elapsed > std::chrono::milliseconds(500));
    REQUIRE(elapsed < std::chrono::seconds(10));

    close(fd);
}

// Exercises resolution and both hook transports through the Unix socket.

TEST_CASE("resolves image/class/method and installs+uninstalls both hook types end-to-end over IPC", "[ipc_server]") {
    event_stream_reset_for_testing();
    hook_registry_reset_for_testing();

    void* fixture = dlopen(FAKE_GAMEASSEMBLY_PATH, RTLD_NOW);
    REQUIRE(fixture != nullptr);
    REQUIRE(bridge_init(fixture));

    IpcServer* server = ipc_server_start();
    REQUIRE(server != nullptr);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    char path[64];
    ipc_socket_path(getpid(), path, sizeof(path));

    std::string image_resp = send_and_receive(path, "RESOLVE-IMAGE Fake.dll");
    int image_handle = parse_ok_handle(image_resp);
    REQUIRE(image_handle >= 0);
    REQUIRE(send_and_receive(path, "RESOLVE-IMAGE Missing.dll") == "ERR not-found\n");

    std::string class_resp = send_and_receive(
        path, "RESOLVE-CLASS " + std::to_string(image_handle) + " FakeNamespace FakeClass");
    int class_handle = parse_ok_handle(class_resp);
    REQUIRE(class_handle >= 0);

    std::string missing_class = send_and_receive(
        path, "RESOLVE-CLASS " + std::to_string(image_handle) + " Wrong Namespace");
    REQUIRE(missing_class.rfind("ERR", 0) == 0);

    std::string bad_image_handle = send_and_receive(path, "RESOLVE-CLASS 9999 FakeNamespace FakeClass");
    REQUIRE(bad_image_handle == "ERR bad-handle\n");

    std::string method_resp = send_and_receive(
        path, "RESOLVE-METHOD " + std::to_string(class_handle) + " FakeMethod 0");
    int method_handle = parse_ok_handle(method_resp);
    REQUIRE(method_handle >= 0);

    std::string bad_arg_count = send_and_receive(
        path, "RESOLVE-METHOD " + std::to_string(class_handle) + " FakeMethod notanumber");
    REQUIRE(bad_arg_count == "ERR bad-arg-count\n");

    std::string wrong_arity = send_and_receive(
        path, "RESOLVE-METHOD " + std::to_string(class_handle) + " FakeMethod 1");
    REQUIRE(wrong_arity.rfind("ERR", 0) == 0);

    std::string bad_hook_type = send_and_receive(
        path, "HOOK " + std::to_string(method_handle) + " replace not-a-real-type skip-return-void");
    REQUIRE(bad_hook_type == "ERR bad-hook-type\n");

    std::string bad_handler = send_and_receive(
        path, "HOOK " + std::to_string(method_handle) + " replace trampoline no-such-handler");
    REQUIRE(bad_handler == "ERR unknown-handler\n");

    std::string hook_resp = send_and_receive(
        path, "HOOK " + std::to_string(method_handle) + " replace trampoline skip-return-void");
    int trampoline_hook_handle = parse_ok_handle(hook_resp);
    REQUIRE(trampoline_hook_handle >= 0);

    std::string unhook_resp = send_and_receive(path, "UNHOOK " + std::to_string(trampoline_hook_handle));
    REQUIRE(unhook_resp == "OK\n");

    std::string double_unhook = send_and_receive(path, "UNHOOK " + std::to_string(trampoline_hook_handle));
    REQUIRE(double_unhook.rfind("ERR", 0) == 0);

    std::string breakpoint_hook_resp = send_and_receive(
        path, "HOOK " + std::to_string(method_handle) + " replace breakpoint skip-return-void");
    int breakpoint_hook_handle = parse_ok_handle(breakpoint_hook_resp);
    REQUIRE(breakpoint_hook_handle >= 0);

    std::string unhook_breakpoint_resp = send_and_receive(path, "UNHOOK " + std::to_string(breakpoint_hook_handle));
    REQUIRE(unhook_breakpoint_resp == "OK\n");

    // Around probes preserve the original body.
    std::string around_resp = send_and_receive(
        path, "HOOK " + std::to_string(method_handle) + " around trampoline count-calls");
    int around_handle = parse_ok_handle(around_resp);
    REQUIRE(around_handle >= 0);
    std::string listed = send_and_receive(path, "LIST-HOOKS");
    REQUIRE(listed.find("HOOK " + std::to_string(around_handle) +
                        " around trampoline count-calls Fake.dll!FakeNamespace.FakeClass::FakeMethod@0x06000001 0") != std::string::npos);
    auto call_fake_method = reinterpret_cast<void (*)()>(
        dlsym(fixture, "fake_gameassembly_call_method"));
    REQUIRE(call_fake_method != nullptr);
    call_fake_method();
    call_fake_method();
    REQUIRE(send_and_receive(path, "HOOK-STATS " + std::to_string(around_handle)) == "OK 2\n");
    std::string events = send_and_receive(path, "EVENTS-READ 0 256");
    INFO(events);
    REQUIRE(events.find("hook-hits delta=2,total=2") != std::string::npos);
    REQUIRE(send_and_receive(path, "UNHOOK " + std::to_string(around_handle)) == "OK\n");

    std::string replace_counter_resp = send_and_receive(
        path, "HOOK " + std::to_string(method_handle) + " replace breakpoint count-calls");
    int replace_counter_handle = parse_ok_handle(replace_counter_resp);
    REQUIRE(replace_counter_handle >= 0);
    call_fake_method();
    REQUIRE(send_and_receive(path, "HOOK-STATS " + std::to_string(replace_counter_handle)) == "OK 1\n");
    REQUIRE(send_and_receive(path, "UNHOOK " + std::to_string(replace_counter_handle)) == "OK\n");

    ipc_server_stop(server);
    dlclose(fixture);
}

TEST_CASE("dispatch validates argument counts and handles before touching bridge/hook state", "[ipc_server]") {
    IpcServer* server = ipc_server_start();
    REQUIRE(server != nullptr);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    char path[64];
    ipc_socket_path(getpid(), path, sizeof(path));

    REQUIRE(send_and_receive(path, "RESOLVE-IMAGE") == "ERR bad-args\n");
    REQUIRE(send_and_receive(path, "RESOLVE-CLASS 0 OnlyOneArg") == "ERR bad-args\n");
    REQUIRE(send_and_receive(path, "RESOLVE-METHOD 0 OnlyOneArg") == "ERR bad-args\n");
    REQUIRE(send_and_receive(path, "HOOK 0 replace trampoline") == "ERR bad-args\n");
    REQUIRE(send_and_receive(path, "HOOK-STATS") == "ERR bad-args\n");
    REQUIRE(send_and_receive(path, "UNHOOK") == "ERR bad-args\n");

    REQUIRE(send_and_receive(path, "RESOLVE-CLASS notanumber Foo Bar") == "ERR bad-handle\n");
    REQUIRE(send_and_receive(path, "RESOLVE-METHOD notanumber Foo 0") == "ERR bad-handle\n");
    REQUIRE(send_and_receive(path, "HOOK notanumber replace trampoline skip-return-void") == "ERR bad-handle\n");
    REQUIRE(send_and_receive(path, "HOOK 9999999 replace trampoline skip-return-void") == "ERR bad-handle\n");
    REQUIRE(send_and_receive(path, "UNHOOK notanumber") == "ERR bad-handle\n");
    REQUIRE(send_and_receive(path, "UNHOOK 9999999") == "ERR bad-handle\n");

    REQUIRE(send_and_receive(path, "CALL 0 whatever") == "ERR not-implemented\n");
    REQUIRE(send_and_receive(path, "CALL") == "ERR not-implemented\n");

    ipc_server_stop(server);
}
