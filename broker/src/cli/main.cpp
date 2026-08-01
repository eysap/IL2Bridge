#include "il2bridge/broker/metadata_file.hpp"
#include "il2bridge/broker/target_discovery.hpp"
#include "il2bridge/broker/unity_discovery.hpp"
#include <nlohmann/json.hpp>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <thread>
#include <vector>

using namespace il2bridge;
using json = nlohmann::json;

namespace {

struct GlobalOptions {
    bool json = false;
    std::optional<int> pid;
    std::optional<std::filesystem::path> socket;
};

int fail(const GlobalOptions& options, const std::string& message) {
    if (options.json) std::printf("%s\n", json{{"ok", false}, {"error", message}}.dump().c_str());
    else std::fprintf(stderr, "Error: %s\n", message.c_str());
    return 1;
}

std::optional<int> parse_int(const std::string& value) {
    int result = 0;
    auto [end, ec] = std::from_chars(value.data(), value.data() + value.size(), result);
    if (ec != std::errc{} || end != value.data() + value.size()) return std::nullopt;
    return result;
}

std::optional<uint64_t> parse_uint64(const std::string& value) {
    uint64_t result = 0;
    auto [end, ec] = std::from_chars(value.data(), value.data() + value.size(), result);
    if (ec != std::errc{} || end != value.data() + value.size()) return std::nullopt;
    return result;
}

std::optional<TargetSession> select_session(const GlobalOptions& options, std::string* error) {
    TargetDiscovery discovery;
    if (options.socket) return discovery.from_socket(*options.socket, error);
    if (options.pid) return discovery.from_pid(*options.pid, error);
    return discovery.select_one(error);
}

int run_enumerate(const std::filesystem::path& target) {
    std::filesystem::path metadata_path;

    // Accept either a path directly to global-metadata.dat, or a game
    // executable path (auto-discovery via Unity convention).
    if (target.filename() == "global-metadata.dat") {
        metadata_path = target;
    } else {
        auto found = find_global_metadata(target);
        if (!found) {
            std::fprintf(stderr, "Could not locate global-metadata.dat for '%s'\n", target.c_str());
            return 1;
        }
        metadata_path = *found;
    }

    auto md = Il2CppMetadataFile::from_file(metadata_path);
    if (!md) {
        std::fprintf(stderr, "Failed to parse metadata at '%s' (bad magic/version, or unsupported version — Il2Bridge targets v39 only)\n", metadata_path.c_str());
        return 1;
    }

    json out = json::array();

    for (int32_t i = 0; i < md->header().images.count; ++i) {
        auto* image = md->image(i);
        if (!image) continue;

        json image_json;
        image_json["name"] = md->string_from_index(image->nameIndex).value_or("<unknown>");
        image_json["types"] = json::array();

        for (uint32_t t = 0; t < image->typeCount; ++t) {
            int32_t type_index = image->firstTypeIndex + static_cast<int32_t>(t);
            auto* type = md->type(type_index);
            if (!type) continue;

            json type_json;
            type_json["name"] = md->string_from_index(type->nameIndex).value_or("<unknown>");
            type_json["namespace"] = md->string_from_index(type->namespaceIndex).value_or("");
            type_json["methodCount"] = type->methodCount;
            type_json["fieldCount"] = type->fieldCount;
            type_json["methods"] = json::array();

            for (uint16_t m = 0; m < type->methodCount; ++m) {
                auto* method = md->method(type->methodStart + static_cast<int32_t>(m));
                if (!method) continue;

                json method_json;
                method_json["name"] = md->string_from_index(method->nameIndex).value_or("<unknown>");
                method_json["arity"] = method->parameterCount;
                method_json["static"] = (method->flags & 0x0010u) != 0;
                method_json["token"] = method->token;
                type_json["methods"].push_back(method_json);
            }
            image_json["types"].push_back(type_json);
        }

        out.push_back(image_json);
    }

    std::printf("%s\n", out.dump(2).c_str());
    return 0;
}

int run_targets(const GlobalOptions& options) {
    TargetDiscovery discovery;
    auto targets = discovery.discover();
    if (options.json) {
        json out = json::array();
        for (const auto& target : targets) {
            out.push_back({{"session", target.info.session_id()}, {"pid", target.info.pid},
                           {"uid", target.info.uid}, {"protocol", target.info.protocol},
                           {"socket", target.socket_path.string()}, {"features", target.info.features}});
        }
        std::printf("%s\n", json{{"ok", true}, {"targets", out}}.dump().c_str());
    } else if (targets.empty()) {
        std::printf("No Il2Bridge targets found.\n");
    } else {
        for (const auto& target : targets) {
            std::printf("%s  pid=%d  socket=%s\n", target.info.session_id().c_str(),
                        target.info.pid, target.socket_path.c_str());
        }
    }
    return 0;
}

int run_status(const GlobalOptions& options) {
    std::string error;
    auto session = select_session(options, &error);
    if (!session) return fail(options, error);
    const TargetInfo& info = session->info();
    auto hooks = session->list_hooks(&error);
    if (!hooks) return fail(options, error);
    auto events = session->events_info(&error);
    if (!events) return fail(options, error);
    if (options.json) {
        std::printf("%s\n", json{{"ok", true}, {"session", info.session_id()}, {"pid", info.pid},
                                  {"uid", info.uid}, {"protocol", info.protocol},
                                  {"socket", session->socket_path().string()},
                                  {"features", info.features}, {"activeHooks", hooks->size()},
                                  {"eventCapacity", events->capacity}, {"droppedEvents", events->dropped},
                                  {"nextEventSequence", events->next_sequence}}.dump().c_str());
    } else {
        std::printf("Il2Bridge target %s\nPID: %d\nProtocol: %d\nSocket: %s\nActive hooks: %zu\nEvents: next=%llu capacity=%llu dropped=%llu\n",
                    info.session_id().c_str(), info.pid, info.protocol,
                    session->socket_path().c_str(), hooks->size(),
                    (unsigned long long)events->next_sequence,
                    (unsigned long long)events->capacity,
                    (unsigned long long)events->dropped);
    }
    return 0;
}

int run_hook_list(const GlobalOptions& options) {
    std::string error;
    auto session = select_session(options, &error);
    if (!session) return fail(options, error);
    auto hooks = session->list_hooks(&error);
    if (!hooks) return fail(options, error);
    if (options.json) {
        json values = json::array();
        for (const auto& hook : *hooks) {
            json value{{"id", HookId{session->info().session_id(), hook.slot}.str()}, {"slot", hook.slot},
                       {"mode", hook.mode}, {"transport", hook.transport}, {"handler", hook.handler},
                       {"method", hook.method}};
            value["hits"] = hook.hits ? json(*hook.hits) : json(nullptr);
            values.push_back(std::move(value));
        }
        std::printf("%s\n", json{{"ok", true}, {"hooks", values}}.dump().c_str());
    } else if (hooks->empty()) {
        std::printf("No active hooks.\n");
    } else {
        for (const auto& hook : *hooks) {
            std::string id = HookId{session->info().session_id(), hook.slot}.str();
            std::printf("%s  %s/%s  %s  %s", id.c_str(), hook.mode.c_str(), hook.transport.c_str(),
                        hook.handler.c_str(), hook.method.c_str());
            if (hook.hits) std::printf("  hits=%llu", (unsigned long long)*hook.hits);
            std::printf("\n");
        }
    }
    return 0;
}

int run_hook_add(const GlobalOptions& options, const std::vector<std::string>& args) {
    if (args.empty()) return fail(options, "hook add requires a method selector");
    std::string selector_error;
    auto selector = MethodSelector::parse(args[0], &selector_error);
    if (!selector) return fail(options, selector_error);
    std::string mode = "around";
    std::string handler = "count-calls";
    std::optional<std::string> transport;
    for (size_t i = 1; i < args.size(); ++i) {
        if ((args[i] == "--mode" || args[i] == "--handler" || args[i] == "--transport") && i + 1 < args.size()) {
            const std::string value = args[++i];
            if (args[i - 1] == "--mode") mode = value;
            else if (args[i - 1] == "--handler") handler = value;
            else transport = value;
        } else return fail(options, "unknown or incomplete hook add option: " + args[i]);
    }
    const std::string selected_transport = transport.value_or(mode == "around" ? "trampoline" : "breakpoint");
    std::string error;
    auto session = select_session(options, &error);
    if (!session) return fail(options, error);
    auto hook = session->add_hook(*selector, mode, selected_transport, handler, &error);
    if (!hook) return fail(options, error);
    if (options.json) {
        std::printf("%s\n", json{{"ok", true}, {"id", hook->str()}, {"method", selector->canonical()},
                                  {"mode", mode}, {"transport", selected_transport},
                                  {"handler", handler}}.dump().c_str());
    } else {
        std::printf("Hook %s installed\nMethod: %s\nMode: %s/%s\nHandler: %s\n",
                    hook->str().c_str(), selector->canonical().c_str(), mode.c_str(),
                    selected_transport.c_str(), handler.c_str());
    }
    return 0;
}

int run_hook_stats(const GlobalOptions& options, const std::string& id_text) {
    auto id = HookId::parse(id_text);
    if (!id) return fail(options, "invalid hook id; expected <pid>:<start>:<slot>");
    std::string error;
    auto session = select_session(options, &error);
    if (!session) return fail(options, error);
    auto hits = session->hook_stats(*id, &error);
    if (!hits) return fail(options, error);
    if (options.json) std::printf("%s\n", json{{"ok", true}, {"id", id->str()}, {"hits", *hits}}.dump().c_str());
    else std::printf("%s  hits=%llu\n", id->str().c_str(), (unsigned long long)*hits);
    return 0;
}

int run_hook_remove(const GlobalOptions& options, const std::string& id_text) {
    auto id = HookId::parse(id_text);
    if (!id) return fail(options, "invalid hook id; expected <pid>:<start>:<slot>");
    std::string error;
    auto session = select_session(options, &error);
    if (!session) return fail(options, error);
    if (!session->remove_hook(*id, &error)) return fail(options, error);
    if (options.json) std::printf("%s\n", json{{"ok", true}, {"id", id->str()}, {"removed", true}}.dump().c_str());
    else std::printf("Hook %s removed.\n", id->str().c_str());
    return 0;
}

void print_event(const GlobalOptions& options, const TargetSession& session, const Event& event) {
    const std::string hook_id = HookId{session.info().session_id(), event.hook_slot}.str();
    if (options.json) {
        std::printf("%s\n", json{{"sequence", event.sequence}, {"timestampNs", event.timestamp_ns},
                                  {"threadId", event.thread_id}, {"hookId", hook_id},
                                  {"type", event.type}, {"payload", event.payload}}.dump().c_str());
    } else {
        std::printf("%llu  %s  tid=%u  %s  %s\n", (unsigned long long)event.sequence,
                    event.type.c_str(), event.thread_id, hook_id.c_str(), event.payload.c_str());
    }
}

int run_events(const GlobalOptions& options, const std::string& action,
               const std::vector<std::string>& args, std::optional<HookId> filter = std::nullopt) {
    uint64_t after = 0;
    size_t limit = 128;
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--after" && i + 1 < args.size()) {
            auto value = parse_uint64(args[++i]);
            if (!value) return fail(options, "--after requires an unsigned sequence");
            after = *value;
        } else if (args[i] == "--limit" && i + 1 < args.size()) {
            auto value = parse_uint64(args[++i]);
            if (!value || *value < 1 || *value > 256) return fail(options, "--limit must be between 1 and 256");
            limit = static_cast<size_t>(*value);
        } else return fail(options, "unknown or incomplete events option: " + args[i]);
    }
    std::string error;
    auto session = select_session(options, &error);
    if (!session) return fail(options, error);
    if (filter && filter->session != session->info().session_id()) {
        return fail(options, "hook id belongs to another target session");
    }
    const bool watch = action == "watch";
    do {
        auto batch = session->read_events(after, limit, &error);
        if (!batch) return fail(options, error);
        std::vector<Event> selected;
        for (const Event& event : batch->events) {
            after = event.sequence;
            if (!filter || filter->slot == event.hook_slot) selected.push_back(event);
        }
        if (!watch) {
            if (options.json) {
                json values = json::array();
                for (const Event& event : selected) values.push_back({{"sequence", event.sequence},
                    {"timestampNs", event.timestamp_ns}, {"threadId", event.thread_id},
                    {"hookId", HookId{session->info().session_id(), event.hook_slot}.str()},
                    {"type", event.type}, {"payload", event.payload}});
                std::printf("%s\n", json{{"ok", true}, {"events", values},
                    {"nextSequence", batch->next_sequence}, {"dropped", batch->dropped}}.dump().c_str());
            } else if (selected.empty()) std::printf("No events. next=%llu dropped=%llu\n",
                    (unsigned long long)batch->next_sequence, (unsigned long long)batch->dropped);
            else for (const Event& event : selected) print_event(options, *session, event);
            return 0;
        }
        for (const Event& event : selected) print_event(options, *session, event);
        if (!selected.empty()) std::fflush(stdout);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    } while (true);
}

void print_usage(const char* program) {
    std::fprintf(stderr,
        "Usage:\n"
        "  %s [--json] [--pid PID | --socket PATH] status\n"
        "  %s [--json] targets list\n"
        "  %s [--json] [target options] hook add SELECTOR [--mode MODE] [--transport TYPE] [--handler NAME]\n"
        "  %s [--json] [target options] hook list|stats ID|remove ID\n"
        "  %s [--json] [target options] events read|watch [--after SEQ] [--limit N]\n"
        "  %s [--json] [target options] hook watch ID [--after SEQ] [--limit N]\n"
        "  %s enumerate <path-to-executable-or-global-metadata.dat>\n",
        program, program, program, program, program, program, program);
}

} // namespace

int main(int argc, char** argv) {
    GlobalOptions options;
    int index = 1;
    while (index < argc) {
        std::string argument = argv[index];
        if (argument == "--json") {
            options.json = true;
            ++index;
        } else if ((argument == "--pid" || argument == "--socket") && index + 1 < argc) {
            std::string value = argv[index + 1];
            if (argument == "--pid") {
                auto pid = parse_int(value);
                if (!pid || *pid <= 0) return fail(options, "--pid requires a positive integer");
                options.pid = *pid;
            } else options.socket = value;
            index += 2;
        } else break;
    }
    if (index >= argc) {
        print_usage(argv[0]);
        return 1;
    }
    if (options.pid && options.socket) return fail(options, "--pid and --socket are mutually exclusive");
    std::string command = argv[index++];
    if (command == "enumerate" && index < argc) return run_enumerate(argv[index]);
    if (command == "targets" && index < argc && std::string(argv[index]) == "list") return run_targets(options);
    if (command == "status" && index == argc) return run_status(options);
    if (command == "events" && index < argc) {
        std::string action = argv[index++];
        if (action == "read" || action == "watch") {
            std::vector<std::string> args;
            while (index < argc) args.emplace_back(argv[index++]);
            return run_events(options, action, args);
        }
    }
    if (command == "hook" && index < argc) {
        std::string action = argv[index++];
        if (action == "list" && index == argc) return run_hook_list(options);
        if ((action == "stats" || action == "remove") && index + 1 == argc) {
            return action == "stats" ? run_hook_stats(options, argv[index]) : run_hook_remove(options, argv[index]);
        }
        if (action == "add" && index < argc) {
            std::vector<std::string> args;
            while (index < argc) args.emplace_back(argv[index++]);
            return run_hook_add(options, args);
        }
        if (action == "watch" && index < argc) {
            auto id = HookId::parse(argv[index++]);
            if (!id) return fail(options, "invalid hook id; expected <pid>:<start>:<slot>");
            std::vector<std::string> args;
            while (index < argc) args.emplace_back(argv[index++]);
            return run_events(options, "watch", args, id);
        }
    }
    print_usage(argv[0]);
    return 1;
}
