#include "il2bridge/broker/client_models.hpp"
#include <charconv>
#include <cstdio>

namespace il2bridge {

static void set_error(std::string* error, const std::string& value) {
    if (error) *error = value;
}

std::optional<MethodSelector> MethodSelector::parse(const std::string& value, std::string* error) {
    if (value.empty() || value.find_first_of(" \t\r\n") != std::string::npos) {
        set_error(error, "method selector must be a non-empty token");
        return std::nullopt;
    }
    const size_t bang = value.find('!');
    const size_t method_sep = value.find("::", bang == std::string::npos ? 0 : bang + 1);
    const size_t slash = value.rfind('/');
    const size_t at = value.rfind('@');
    const bool by_token = at != std::string::npos && (slash == std::string::npos || at > slash);
    if ((by_token && slash != std::string::npos) || (!by_token && at != std::string::npos)) {
        set_error(error, "method selector must use exactly one identity: arity or token");
        return std::nullopt;
    }
    const size_t identity_sep = by_token ? at : slash;
    if (bang == std::string::npos || method_sep == std::string::npos || identity_sep == std::string::npos ||
        bang == 0 || method_sep <= bang + 1 || identity_sep <= method_sep + 2 || identity_sep + 1 >= value.size()) {
        set_error(error, "expected Assembly!Namespace.Type::Method/arity or Method@0xTOKEN");
        return std::nullopt;
    }

    MethodSelector result;
    result.assembly = value.substr(0, bang);
    const std::string qualified_type = value.substr(bang + 1, method_sep - bang - 1);
    const size_t type_sep = qualified_type.rfind('.');
    if (type_sep == std::string::npos) result.type = qualified_type;
    else {
        result.namespaze = qualified_type.substr(0, type_sep);
        result.type = qualified_type.substr(type_sep + 1);
    }
    result.method = value.substr(method_sep + 2, identity_sep - method_sep - 2);
    if (result.type.empty() || result.method.empty()) {
        set_error(error, "type and method names must not be empty");
        return std::nullopt;
    }
    if (by_token) {
        const std::string token_text = value.substr(at + 1);
        if (token_text.size() < 3 || token_text.substr(0, 2) != "0x") {
            set_error(error, "metadata token must use hexadecimal 0x notation");
            return std::nullopt;
        }
        uint32_t token = 0;
        auto [end, ec] = std::from_chars(token_text.data() + 2, token_text.data() + token_text.size(), token, 16);
        if (ec != std::errc{} || end != token_text.data() + token_text.size() || token == 0) {
            set_error(error, "metadata token must be a non-zero 32-bit hexadecimal value");
            return std::nullopt;
        }
        result.token = token;
        return result;
    }
    const std::string arity_text = value.substr(slash + 1);
    int arity = -1;
    auto [end, ec] = std::from_chars(arity_text.data(), arity_text.data() + arity_text.size(), arity);
    if (ec != std::errc{} || end != arity_text.data() + arity_text.size() || arity < 0 || arity > 255) {
        set_error(error, "arity must be an integer between 0 and 255");
        return std::nullopt;
    }
    result.arity = arity;
    return result;
}

std::string MethodSelector::canonical() const {
    return assembly + "!" + (namespaze.empty() ? "" : namespaze + ".") +
           type + "::" + method + (token ? "@0x" + [&] {
               char value[9]; std::snprintf(value, sizeof(value), "%08x", *token); return std::string(value);
           }() : "/" + std::to_string(arity.value_or(0)));
}

std::string TargetInfo::session_id() const {
    return std::to_string(pid) + ":" + std::to_string(start_ticks);
}

std::optional<HookId> HookId::parse(const std::string& value) {
    size_t separator = value.rfind(':');
    if (separator == std::string::npos || separator == 0 || separator + 1 >= value.size()) return std::nullopt;
    uint32_t slot = 0;
    const char* begin = value.data() + separator + 1;
    const char* end_ptr = value.data() + value.size();
    auto [end, ec] = std::from_chars(begin, end_ptr, slot);
    if (ec != std::errc{} || end != end_ptr) return std::nullopt;
    return HookId{value.substr(0, separator), slot};
}

std::string HookId::str() const {
    return session + ":" + std::to_string(slot);
}

} // namespace il2bridge
