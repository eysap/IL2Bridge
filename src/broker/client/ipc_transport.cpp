#include "il2bridge/broker/ipc_transport.hpp"
#include <cerrno>
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
#include <unistd.h>

namespace il2bridge {

IpcTransport::IpcTransport(std::filesystem::path socket_path)
    : socket_path_(std::move(socket_path)) {}

std::optional<IpcResponse> IpcTransport::request(const std::string& command, std::string* error) const {
    auto fail = [&](const std::string& message) -> std::optional<IpcResponse> {
        if (error) *error = message;
        return std::nullopt;
    };
    if (command.empty() || command.find('\n') != std::string::npos) return fail("invalid IPC command");
    const std::string path = socket_path_.string();

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return fail(std::string("socket: ") + std::strerror(errno));
    struct Guard { int fd; ~Guard() { if (fd >= 0) close(fd); } } guard{fd};

    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (path.size() >= sizeof(address.sun_path)) return fail("socket path is too long");
    std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
    if (connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        return fail(std::string("connect: ") + std::strerror(errno));
    }

    timeval timeout{2, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    ucred credentials{};
    socklen_t credentials_size = sizeof(credentials);
    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &credentials, &credentials_size) != 0) {
        return fail(std::string("SO_PEERCRED: ") + std::strerror(errno));
    }

    std::string request = command + "\n";
    size_t written = 0;
    while (written < request.size()) {
        ssize_t count = write(fd, request.data() + written, request.size() - written);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return fail(std::string("write: ") + std::strerror(errno));
        written += static_cast<size_t>(count);
    }

    std::string payload;
    char buffer[4096];
    for (;;) {
        ssize_t count = read(fd, buffer, sizeof(buffer));
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) return fail(std::string("read: ") + std::strerror(errno));
        if (count == 0) break;
        payload.append(buffer, static_cast<size_t>(count));
        if (payload.size() > 1024 * 1024) return fail("IPC response exceeds 1 MiB");
    }
    return IpcResponse{std::move(payload), static_cast<int>(credentials.pid), static_cast<int>(credentials.uid)};
}

} // namespace il2bridge
