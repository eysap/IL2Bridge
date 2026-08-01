#pragma once
#include <filesystem>
#include <optional>
#include <string>

namespace il2bridge {

struct IpcResponse {
    std::string payload;
    int peer_pid = 0;
    int peer_uid = 0;
};

class IpcTransport {
public:
    explicit IpcTransport(std::filesystem::path socket_path);
    std::optional<IpcResponse> request(const std::string& command, std::string* error = nullptr) const;
    const std::filesystem::path& socket_path() const { return socket_path_; }

private:
    std::filesystem::path socket_path_;
};

} // namespace il2bridge
