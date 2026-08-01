#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace il2bridge {

struct MethodSelector {
    std::string assembly;
    std::string namespaze;
    std::string type;
    std::string method;
    std::optional<int> arity;
    std::optional<uint32_t> token;

    static std::optional<MethodSelector> parse(const std::string& value, std::string* error = nullptr);
    std::string canonical() const;
};

struct TargetInfo {
    int protocol = 0;
    int pid = 0;
    int uid = 0;
    uint64_t start_ticks = 0;
    std::vector<std::string> features;

    std::string session_id() const;
};

struct TargetIdentity {
    std::filesystem::path socket_path;
    TargetInfo info;
};

struct HookInfo {
    uint32_t slot = 0;
    std::string mode;
    std::string transport;
    std::string handler;
    std::string method;
    std::optional<uint64_t> hits;
};

struct HookId {
    std::string session;
    uint32_t slot = 0;

    static std::optional<HookId> parse(const std::string& value);
    std::string str() const;
};

struct EventInfo {
    uint64_t next_sequence = 0;
    uint64_t oldest_sequence = 0;
    uint64_t capacity = 0;
    uint64_t dropped = 0;
};

struct Event {
    uint64_t sequence = 0;
    uint64_t timestamp_ns = 0;
    uint32_t thread_id = 0;
    uint32_t hook_slot = 0;
    std::string type;
    std::string payload;
};

struct EventBatch {
    uint64_t next_sequence = 0;
    uint64_t dropped = 0;
    std::vector<Event> events;
};

} // namespace il2bridge
