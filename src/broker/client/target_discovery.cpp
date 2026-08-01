#include "il2bridge/broker/target_discovery.hpp"
#include <algorithm>
#include <cstdlib>
#include <sys/stat.h>
#include <unordered_set>
#include <unistd.h>

namespace il2bridge {
namespace fs = std::filesystem;

std::vector<fs::path> TargetDiscovery::candidate_sockets() const {
    std::vector<fs::path> candidates;
    std::vector<fs::path> directories{fs::path("/tmp")};
    if (const char* runtime = std::getenv("XDG_RUNTIME_DIR")) {
        if (*runtime) directories.insert(directories.begin(), fs::path(runtime) / "il2bridge");
    }
    for (const fs::path& directory : directories) {
        std::error_code ec;
        for (fs::directory_iterator it(directory, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
            const std::string filename = it->path().filename().string();
            bool legacy = directory == fs::path("/tmp") && filename.starts_with("il2bridge-") && filename.ends_with(".sock");
            bool runtime = directory != fs::path("/tmp") && filename.ends_with(".sock");
            if (!legacy && !runtime) continue;
            struct stat status{};
            if (lstat(it->path().c_str(), &status) == 0 && S_ISSOCK(status.st_mode) && status.st_uid == geteuid()) {
                candidates.push_back(it->path());
            }
        }
    }
    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
    return candidates;
}

std::vector<TargetIdentity> TargetDiscovery::discover() const {
    std::vector<TargetIdentity> targets;
    std::unordered_set<std::string> sessions;
    for (const fs::path& socket : candidate_sockets()) {
        std::string ignored;
        auto session = TargetSession::connect(socket, &ignored);
        if (session && sessions.insert(session->info().session_id()).second) {
            targets.push_back(TargetIdentity{socket, session->info()});
        }
    }
    std::sort(targets.begin(), targets.end(), [](const TargetIdentity& a, const TargetIdentity& b) {
        return a.info.pid < b.info.pid;
    });
    return targets;
}

std::optional<TargetSession> TargetDiscovery::from_socket(const fs::path& socket, std::string* error) const {
    return TargetSession::connect(socket, error);
}

std::optional<TargetSession> TargetDiscovery::from_pid(int pid, std::string* error) const {
    if (pid <= 0) {
        if (error) *error = "PID must be positive";
        return std::nullopt;
    }
    std::vector<fs::path> candidates;
    if (const char* runtime = std::getenv("XDG_RUNTIME_DIR")) {
        if (*runtime) candidates.push_back(fs::path(runtime) / "il2bridge" / (std::to_string(pid) + ".sock"));
    }
    candidates.push_back(fs::path("/tmp") / ("il2bridge-" + std::to_string(pid) + ".sock"));
    for (const fs::path& socket : candidates) {
        std::string attempt_error;
        auto session = TargetSession::connect(socket, &attempt_error);
        if (session && session->info().pid == pid) return session;
    }
    if (error) *error = "no Il2Bridge loader found for PID " + std::to_string(pid);
    return std::nullopt;
}

std::optional<TargetSession> TargetDiscovery::select_one(std::string* error) const {
    auto targets = discover();
    if (targets.empty()) {
        if (error) *error = "no Il2Bridge targets found";
        return std::nullopt;
    }
    if (targets.size() > 1) {
        if (error) *error = "multiple Il2Bridge targets found; use --pid or --socket";
        return std::nullopt;
    }
    return TargetSession::connect(targets[0].socket_path, error);
}

} // namespace il2bridge
