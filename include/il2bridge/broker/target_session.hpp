#pragma once
#include "il2bridge/broker/client_models.hpp"
#include "il2bridge/broker/ipc_transport.hpp"
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace il2bridge {

class TargetSession {
public:
    static std::optional<TargetSession> connect(const std::filesystem::path& socket_path,
                                                std::string* error = nullptr);

    const TargetInfo& info() const { return info_; }
    const std::filesystem::path& socket_path() const { return transport_.socket_path(); }

    std::optional<int> resolve(const MethodSelector& selector, std::string* error = nullptr) const;
    std::optional<HookId> add_hook(const MethodSelector& selector, const std::string& mode,
                                   const std::string& transport, const std::string& handler,
                                   std::string* error = nullptr) const;
    std::optional<uint64_t> hook_stats(const HookId& hook, std::string* error = nullptr) const;
    bool remove_hook(const HookId& hook, std::string* error = nullptr) const;
    std::optional<std::vector<HookInfo>> list_hooks(std::string* error = nullptr) const;
    std::optional<EventInfo> events_info(std::string* error = nullptr) const;
    std::optional<EventBatch> read_events(uint64_t after, size_t limit,
                                          std::string* error = nullptr) const;

private:
    TargetSession(IpcTransport transport, TargetInfo info);
    bool validate_hook_id(const HookId& hook, std::string* error) const;

    IpcTransport transport_;
    TargetInfo info_;
};

} // namespace il2bridge
