#pragma once
#include "il2bridge/broker/metadata_structs.hpp"
#include "il2bridge/broker/metadata_records.hpp"
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace il2bridge {

class Il2CppMetadataFile {
public:
    static std::optional<Il2CppMetadataFile> from_bytes(std::vector<uint8_t> data);
    static std::optional<Il2CppMetadataFile> from_file(const std::filesystem::path& path);

    const Il2CppGlobalMetadataHeader& header() const { return header_; }

    std::optional<std::string> string_from_index(int32_t index) const;

    const Il2CppAssemblyDefinition* assembly(int32_t index) const;
    const Il2CppImageDefinition* image(int32_t index) const;
    const Il2CppTypeDefinition* type(int32_t index) const;
    const Il2CppMethodDefinition* method(int32_t index) const;
    const Il2CppFieldDefinition* field(int32_t index) const;
    const Il2CppParameterDefinition* parameter(int32_t index) const;

private:
    Il2CppMetadataFile(std::vector<uint8_t> data,
                        Il2CppGlobalMetadataHeader header,
                        std::vector<Il2CppImageDefinition> images,
                        std::vector<Il2CppAssemblyDefinition> assemblies,
                        std::vector<Il2CppTypeDefinition> types,
                        std::vector<Il2CppMethodDefinition> methods,
                        std::vector<Il2CppFieldDefinition> fields,
                        std::vector<Il2CppParameterDefinition> parameters);

    std::vector<uint8_t> data_;   // still needed for string_from_index (raw string blob)
    Il2CppGlobalMetadataHeader header_;
    std::vector<Il2CppImageDefinition> images_;
    std::vector<Il2CppAssemblyDefinition> assemblies_;
    std::vector<Il2CppTypeDefinition> types_;
    std::vector<Il2CppMethodDefinition> methods_;
    std::vector<Il2CppFieldDefinition> fields_;
    std::vector<Il2CppParameterDefinition> parameters_;
};

} // namespace il2bridge
