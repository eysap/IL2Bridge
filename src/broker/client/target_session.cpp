#include "il2bridge/broker/target_session.hpp"
#include <charconv>
#include <sstream>
#include <unordered_map>
#include <unistd.h>

namespace il2bridge {
namespace {

void set_error(std::string* error, const std::string& value) {
    if (error) *error = value;
}

std::optional<unsigned long long> parse_ok_number(const std::string& response, std::string* error) {
    if (!response.starts_with("OK ")) {
        set_error(error, response.empty() ? "empty loader response" : response.substr(0, response.find('\n')));
        return std::nullopt;
    }
    std::string value = response.substr(3);
    if (!value.empty() && value.back() == '\n') value.pop_back();
    unsigned long long number = 0;
    auto [end, ec] = std::from_chars(value.data(), value.data() + value.size(), number);
    if (ec != std::errc{} || end != value.data() + value.size()) {
        set_error(error, "malformed numeric loader response");
        return std::nullopt;
    }
    return number;
}

std::optional<TargetInfo> parse_info(const IpcResponse& response, std::string* error) {
    std::istringstream stream(response.payload);
    std::string status;
    stream >> status;
    if (status != "OK") {
        set_error(error, response.payload.empty() ? "empty INFO response" : response.payload);
        return std::nullopt;
    }
    std::unordered_map<std::string, std::string> values;
    std::string token;
    while (stream >> token) {
        size_t equals = token.find('=');
        if (equals != std::string::npos) values[token.substr(0, equals)] = token.substr(equals + 1);
    }
    TargetInfo info;
    try {
        info.protocol = std::stoi(values.at("protocol"));
        info.pid = std::stoi(values.at("pid"));
        info.uid = std::stoi(values.at("uid"));
        info.start_ticks = std::stoull(values.at("start"));
    } catch (...) {
        set_error(error, "INFO response is missing a valid identity field");
        return std::nullopt;
    }
    if (auto it = values.find("features"); it != values.end()) {
        std::istringstream feature_stream(it->second);
        while (std::getline(feature_stream, token, ',')) if (!token.empty()) info.features.push_back(token);
    }
    if (info.protocol != 1) {
        set_error(error, "unsupported loader protocol " + std::to_string(info.protocol));
        return std::nullopt;
    }
    if (info.start_ticks == 0) {
        set_error(error, "loader did not provide a stable process start time");
        return std::nullopt;
    }
    if (info.pid != response.peer_pid || info.uid != response.peer_uid || info.uid != static_cast<int>(geteuid())) {
        set_error(error, "loader identity does not match Unix peer credentials");
        return std::nullopt;
    }
    return info;
}

} // namespace

TargetSession::TargetSession(IpcTransport transport, TargetInfo info)
    : transport_(std::move(transport)), info_(std::move(info)) {}

std::optional<TargetSession> TargetSession::connect(const std::filesystem::path& socket_path,
                                                    std::string* error) {
    IpcTransport transport(socket_path);
    auto response = transport.request("INFO", error);
    if (!response) return std::nullopt;
    auto info = parse_info(*response, error);
    if (!info) return std::nullopt;
    return TargetSession(std::move(transport), std::move(*info));
}

std::optional<int> TargetSession::resolve(const MethodSelector& selector, std::string* error) const {
    auto image_response = transport_.request("RESOLVE-IMAGE " + selector.assembly, error);
    if (!image_response) return std::nullopt;
    auto image = parse_ok_number(image_response->payload, error);
    if (!image || *image > INT32_MAX) return std::nullopt;

    auto class_response = transport_.request(
        "RESOLVE-CLASS " + std::to_string(*image) + " " +
        (selector.namespaze.empty() ? "-" : selector.namespaze) + " " + selector.type, error);
    if (!class_response) return std::nullopt;
    auto klass = parse_ok_number(class_response->payload, error);
    if (!klass || *klass > INT32_MAX) return std::nullopt;

    const std::string method_command = selector.token
        ? "RESOLVE-METHOD-TOKEN " + std::to_string(*klass) + " " + std::to_string(*selector.token) +
          " " + selector.method
        : "RESOLVE-METHOD " + std::to_string(*klass) + " " + selector.method + " " +
          std::to_string(selector.arity.value_or(0));
    auto method_response = transport_.request(method_command, error);
    if (!method_response) return std::nullopt;
    auto method = parse_ok_number(method_response->payload, error);
    if (!method || *method > INT32_MAX) return std::nullopt;
    return static_cast<int>(*method);
}

std::optional<HookId> TargetSession::add_hook(const MethodSelector& selector, const std::string& mode,
                                              const std::string& transport, const std::string& handler,
                                              std::string* error) const {
    if ((mode != "replace" && mode != "around") ||
        (transport != "breakpoint" && transport != "trampoline") || handler.empty()) {
        set_error(error, "invalid hook mode, transport, or handler");
        return std::nullopt;
    }
    if (mode == "around" && transport != "trampoline") {
        set_error(error, "around hooks require trampoline transport");
        return std::nullopt;
    }
    auto method = resolve(selector, error);
    if (!method) return std::nullopt;
    auto response = transport_.request("HOOK " + std::to_string(*method) + " " + mode + " " +
                                       transport + " " + handler, error);
    if (!response) return std::nullopt;
    auto slot = parse_ok_number(response->payload, error);
    if (!slot || *slot > UINT32_MAX) return std::nullopt;
    return HookId{info_.session_id(), static_cast<uint32_t>(*slot)};
}

bool TargetSession::validate_hook_id(const HookId& hook, std::string* error) const {
    if (hook.session != info_.session_id()) {
        set_error(error, "hook belongs to target session " + hook.session +
                         ", current session is " + info_.session_id());
        return false;
    }
    return true;
}

std::optional<uint64_t> TargetSession::hook_stats(const HookId& hook, std::string* error) const {
    if (!validate_hook_id(hook, error)) return std::nullopt;
    auto response = transport_.request("HOOK-STATS " + std::to_string(hook.slot), error);
    if (!response) return std::nullopt;
    auto count = parse_ok_number(response->payload, error);
    if (!count) return std::nullopt;
    return static_cast<uint64_t>(*count);
}

bool TargetSession::remove_hook(const HookId& hook, std::string* error) const {
    if (!validate_hook_id(hook, error)) return false;
    auto response = transport_.request("UNHOOK " + std::to_string(hook.slot), error);
    if (!response) return false;
    if (response->payload != "OK\n") {
        set_error(error, response->payload.empty() ? "empty loader response" : response->payload);
        return false;
    }
    return true;
}

std::optional<std::vector<HookInfo>> TargetSession::list_hooks(std::string* error) const {
    auto response = transport_.request("LIST-HOOKS", error);
    if (!response) return std::nullopt;
    std::istringstream stream(response->payload);
    std::string line;
    if (!std::getline(stream, line) || !line.starts_with("OK ")) {
        set_error(error, line.empty() ? "malformed LIST-HOOKS response" : line);
        return std::nullopt;
    }
    auto advertised_count = parse_ok_number(line + "\n", error);
    if (!advertised_count) return std::nullopt;
    std::vector<HookInfo> result;
    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        std::istringstream fields(line);
        std::string kind;
        std::string hit_text;
        HookInfo hook;
        fields >> kind >> hook.slot >> hook.mode >> hook.transport >> hook.handler >> hook.method >> hit_text;
        if (kind != "HOOK" || hook.method.empty() || hit_text.empty()) {
            set_error(error, "malformed hook entry: " + line);
            return std::nullopt;
        }
        if (hit_text != "-") {
            uint64_t hits = 0;
            auto [end, ec] = std::from_chars(hit_text.data(), hit_text.data() + hit_text.size(), hits);
            if (ec != std::errc{} || end != hit_text.data() + hit_text.size()) {
                set_error(error, "malformed hook counter: " + hit_text);
                return std::nullopt;
            }
            hook.hits = hits;
        }
        result.push_back(std::move(hook));
    }
    if (result.size() != *advertised_count) {
        set_error(error, "LIST-HOOKS count does not match its entries");
        return std::nullopt;
    }
    return result;
}

std::optional<EventInfo> TargetSession::events_info(std::string* error) const {
    auto response = transport_.request("EVENTS-INFO", error);
    if (!response) return std::nullopt;
    std::istringstream stream(response->payload);
    std::string status;
    stream >> status;
    if (status != "OK") {
        set_error(error, response->payload);
        return std::nullopt;
    }
    EventInfo info;
    std::string field;
    while (stream >> field) {
        size_t equals = field.find('=');
        if (equals == std::string::npos) continue;
        uint64_t value = 0;
        auto text = field.substr(equals + 1);
        auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
        if (ec != std::errc{} || end != text.data() + text.size()) continue;
        if (field.starts_with("next=")) info.next_sequence = value;
        else if (field.starts_with("oldest=")) info.oldest_sequence = value;
        else if (field.starts_with("capacity=")) info.capacity = value;
        else if (field.starts_with("dropped=")) info.dropped = value;
    }
    if (info.next_sequence == 0 || info.oldest_sequence == 0 || info.capacity == 0) {
        set_error(error, "malformed EVENTS-INFO response");
        return std::nullopt;
    }
    return info;
}

std::optional<EventBatch> TargetSession::read_events(uint64_t after, size_t limit,
                                                      std::string* error) const {
    if (limit == 0 || limit > 256) {
        set_error(error, "event read limit must be between 1 and 256");
        return std::nullopt;
    }
    auto response = transport_.request("EVENTS-READ " + std::to_string(after) + " " +
                                       std::to_string(limit), error);
    if (!response) return std::nullopt;
    std::istringstream stream(response->payload);
    std::string line;
    if (!std::getline(stream, line)) {
        set_error(error, "empty EVENTS-READ response");
        return std::nullopt;
    }
    std::istringstream header(line);
    std::string status, next_field, dropped_field;
    size_t advertised = 0;
    header >> status >> advertised >> next_field >> dropped_field;
    EventBatch batch;
    if (status != "OK" || !next_field.starts_with("next=") || !dropped_field.starts_with("dropped=")) {
        set_error(error, line);
        return std::nullopt;
    }
    try {
        batch.next_sequence = std::stoull(next_field.substr(5));
        batch.dropped = std::stoull(dropped_field.substr(8));
    } catch (...) {
        set_error(error, "malformed EVENTS-READ header");
        return std::nullopt;
    }
    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        std::istringstream fields(line);
        std::string kind;
        Event event;
        fields >> kind >> event.sequence >> event.timestamp_ns >> event.thread_id >> event.hook_slot
               >> event.type >> event.payload;
        if (kind != "EVENT" || event.sequence == 0 || event.type.empty() || event.payload.empty()) {
            set_error(error, "malformed event entry: " + line);
            return std::nullopt;
        }
        batch.events.push_back(std::move(event));
    }
    if (batch.events.size() != advertised) {
        set_error(error, "EVENTS-READ count does not match its entries");
        return std::nullopt;
    }
    return batch;
}

} // namespace il2bridge
