#pragma once
#include <atomic>
#include <deque>
#include <filesystem>
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
    FakeLoader();
    ~FakeLoader();

    FakeLoader(const FakeLoader&) = delete;
    FakeLoader& operator=(const FakeLoader&) = delete;

    // Queues one payload, served to one connection, in FIFO order. A
    // connection arriving with the queue empty is closed without a reply.
    void push_response(std::string payload);

    // The request lines received so far, in arrival order.
    std::vector<std::string> requests() const;

    const std::filesystem::path& socket_path() const { return socket_path_; }

private:
    void serve();

    std::filesystem::path directory_;
    std::filesystem::path socket_path_;
    int listen_fd_ = -1;
    std::atomic<bool> stopping_{false};
    std::thread worker_;

    mutable std::mutex mutex_;
    std::deque<std::string> responses_;
    std::vector<std::string> requests_;
};

} // namespace il2bridge::testing
