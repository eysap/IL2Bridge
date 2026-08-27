#pragma once
#include <atomic>
#include <deque>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace il2bridge::testing {

// A scripted stand-in for the in-process loader's IPC server.
//
// Binds a real Unix socket so TargetSession exercises the real IpcTransport,
// including SO_PEERCRED: because the fake runs in the test process, the
// credentials the client observes are the test's own pid/uid. That is what
// lets a test drive both the accepted and the rejected identity paths, by
// advertising a pid in the payload that does or doesn't match.
class FakeLoader {
public:
    // Binds inside a private temporary directory.
    FakeLoader();
    // Binds at a caller-chosen path, whose parent directory must exist. Lets a
    // test place sockets where TargetDiscovery will look for them.
    explicit FakeLoader(std::filesystem::path socket_path);
    ~FakeLoader();

    FakeLoader(const FakeLoader&) = delete;
    FakeLoader& operator=(const FakeLoader&) = delete;

    // Queues one payload, served to one connection, in FIFO order. A
    // connection arriving with the queue empty is closed without a reply.
    void push_response(std::string payload);

    // Serves this payload to every connection whose queue is empty, so a
    // caller that reconnects an unknown number of times still gets answered.
    void always_respond(std::string payload);

    // Answers any request whose first token is `command` with this payload.
    // Takes precedence over the queue, so a caller issuing several different
    // commands need not have its request order encoded in a test.
    void on(std::string command, std::string payload);

    // The request lines received so far, in arrival order.
    std::vector<std::string> requests() const;

    const std::filesystem::path& socket_path() const { return socket_path_; }

private:
    void bind_and_serve();
    void serve();

    // Empty when the caller supplied the path; only an owned directory is
    // removed on destruction.
    std::filesystem::path directory_;
    std::filesystem::path socket_path_;
    int listen_fd_ = -1;
    std::atomic<bool> stopping_{false};
    std::thread worker_;

    mutable std::mutex mutex_;
    std::deque<std::string> responses_;
    std::string standing_response_;
    std::map<std::string, std::string> by_command_;
    std::vector<std::string> requests_;
};

} // namespace il2bridge::testing
