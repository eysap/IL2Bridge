#include "il2bridge/broker/unity_discovery.hpp"
#include <system_error>

namespace il2bridge {

std::optional<std::filesystem::path> find_global_metadata(const std::filesystem::path& executable_path) {
    auto dir = executable_path.parent_path();
    auto stem = executable_path.stem(); // e.g. "MyGame" from "MyGame.x64"

    auto candidate = dir / (stem.string() + "_Data") / "il2cpp_data" / "Metadata" / "global-metadata.dat";

    std::error_code ec;
    if (!std::filesystem::exists(candidate, ec) || ec) {
        return std::nullopt;
    }

    return candidate;
}

} // namespace il2bridge
