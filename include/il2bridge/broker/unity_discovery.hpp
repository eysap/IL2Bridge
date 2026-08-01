#pragma once
#include <filesystem>
#include <optional>

namespace il2bridge {

// Derives the global-metadata.dat path from a Unity game executable path,
// following Unity's standard IL2CPP-on-Linux layout convention:
//   <dir>/<exe_stem>_Data/il2cpp_data/Metadata/global-metadata.dat
// Returns nullopt if the derived path doesn't exist.
std::optional<std::filesystem::path> find_global_metadata(const std::filesystem::path& executable_path);

} // namespace il2bridge
