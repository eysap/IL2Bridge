#pragma once
#include "il2bridge/broker/target_session.hpp"
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace il2bridge {

class TargetDiscovery {
public:
    std::vector<TargetIdentity> discover() const;
    std::optional<TargetSession> from_pid(int pid, std::string* error = nullptr) const;
    std::optional<TargetSession> from_socket(const std::filesystem::path& socket,
                                             std::string* error = nullptr) const;
    std::optional<TargetSession> select_one(std::string* error = nullptr) const;

private:
    std::vector<std::filesystem::path> candidate_sockets() const;
};

} // namespace il2bridge
