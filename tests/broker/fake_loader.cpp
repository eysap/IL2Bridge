#include "fake_loader.hpp"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cstring>

namespace il2bridge::testing {
namespace {

std::filesystem::path make_temp_directory() {
    std::string pattern = (std::filesystem::temp_directory_path() / "il2bridge-fake-XXXXXX").string();
    std::vector<char> buffer(pattern.begin(), pattern.end());
    buffer.push_back('\0');
    const char* created = mkdtemp(buffer.data());
    return created ? std::filesystem::path(created) : std::filesystem::path();
}

} // namespace

FakeLoader::FakeLoader()
    : directory_(make_temp_directory()), socket_path_(directory_ / "loader.sock") {
    bind_and_serve();
}

FakeLoader::FakeLoader(std::filesystem::path socket_path)
    : socket_path_(std::move(socket_path)) {
    bind_and_serve();
}

void FakeLoader::bind_and_serve() {
    listen_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listen_fd_ < 0) return;

    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    const std::string path = socket_path_.string();
    std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
    if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
        listen(listen_fd_, 8) != 0) {
        close(listen_fd_);
        listen_fd_ = -1;
        return;
    }
    worker_ = std::thread([this] { serve(); });
}

FakeLoader::~FakeLoader() {
    stopping_ = true;
    // Closing the listening fd makes the blocked accept() return.
    if (listen_fd_ >= 0) shutdown(listen_fd_, SHUT_RDWR);
    if (worker_.joinable()) worker_.join();
    if (listen_fd_ >= 0) close(listen_fd_);
    std::error_code ignored;
    if (directory_.empty()) {
        std::filesystem::remove(socket_path_, ignored);
    } else {
        std::filesystem::remove_all(directory_, ignored);
    }
}

void FakeLoader::serve() {
    while (!stopping_) {
        int client = accept(listen_fd_, nullptr, nullptr);
        if (client < 0) {
            if (errno == EINTR) continue;
            return;
        }

        // The client writes exactly one newline-terminated command.
        std::string request;
        char buffer[512];
        while (request.find('\n') == std::string::npos) {
            ssize_t count = read(client, buffer, sizeof(buffer));
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) break;
            request.append(buffer, static_cast<size_t>(count));
        }
        if (!request.empty() && request.back() == '\n') request.pop_back();

        std::string response;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            requests_.push_back(request);
            if (!responses_.empty()) {
                response = std::move(responses_.front());
                responses_.pop_front();
            } else {
                response = standing_response_;
            }
        }

        size_t written = 0;
        while (written < response.size()) {
            ssize_t count = write(client, response.data() + written, response.size() - written);
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) break;
            written += static_cast<size_t>(count);
        }
        // IpcTransport reads until EOF, so the reply ends when we close.
        close(client);
    }
}

void FakeLoader::push_response(std::string payload) {
    std::lock_guard<std::mutex> lock(mutex_);
    responses_.push_back(std::move(payload));
}

void FakeLoader::always_respond(std::string payload) {
    std::lock_guard<std::mutex> lock(mutex_);
    standing_response_ = std::move(payload);
}

std::vector<std::string> FakeLoader::requests() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return requests_;
}

} // namespace il2bridge::testing
