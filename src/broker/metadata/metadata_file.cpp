#include "il2bridge/broker/metadata_file.hpp"
#include "il2bridge/broker/byte_cursor.hpp"
#include "il2bridge/broker/index_widths.hpp"
#include "il2bridge/broker/metadata_decode.hpp"
#include <cstring>
#include <fstream>

namespace il2bridge {

namespace {

// Decode one record to derive the per-file record width, then validate the
// section's declared size and count before allocating the result vector.
template <typename T, typename DecodeFn>
std::optional<std::vector<T>> decode_section(const std::vector<uint8_t>& data,
                                               const Il2CppMetadataSectionHeader& section,
                                               DecodeFn decode_one) {
    if (section.count == 0) {
        return std::vector<T>{};
    }
    if (section.offset < 0 || section.size < 0 || section.count < 0) {
        return std::nullopt;
    }
    if (static_cast<size_t>(section.offset) + static_cast<size_t>(section.size) > data.size()) {
        return std::nullopt;
    }

    ByteCursor cursor(data, static_cast<size_t>(section.offset));
    auto first = decode_one(cursor);
    if (!cursor.valid() || !first) {
        return std::nullopt;
    }

    size_t element_size = cursor.position() - static_cast<size_t>(section.offset);
    if (element_size == 0 || static_cast<size_t>(section.size) % element_size != 0) {
        return std::nullopt;
    }

    size_t num_elements = static_cast<size_t>(section.size) / element_size;
    if (num_elements != static_cast<size_t>(section.count)) {
        return std::nullopt; // consistency guard: widths or layout are wrong
    }

    std::vector<T> result;
    result.reserve(num_elements);
    result.push_back(std::move(*first));

    for (size_t i = 1; i < num_elements; ++i) {
        auto next = decode_one(cursor);
        if (!cursor.valid() || !next) {
            return std::nullopt;
        }
        result.push_back(std::move(*next));
    }

    return result;
}

} // namespace

Il2CppMetadataFile::Il2CppMetadataFile(std::vector<uint8_t> data,
                                        Il2CppGlobalMetadataHeader header,
                                        std::vector<Il2CppImageDefinition> images,
                                        std::vector<Il2CppAssemblyDefinition> assemblies,
                                        std::vector<Il2CppTypeDefinition> types,
                                        std::vector<Il2CppMethodDefinition> methods,
                                        std::vector<Il2CppFieldDefinition> fields,
                                        std::vector<Il2CppParameterDefinition> parameters)
    : data_(std::move(data)), header_(header),
      images_(std::move(images)), assemblies_(std::move(assemblies)),
      types_(std::move(types)), methods_(std::move(methods)),
      fields_(std::move(fields)), parameters_(std::move(parameters)) {}

std::optional<Il2CppMetadataFile> Il2CppMetadataFile::from_bytes(std::vector<uint8_t> data) {
    if (data.size() < sizeof(Il2CppGlobalMetadataHeader)) {
        return std::nullopt;
    }

    Il2CppGlobalMetadataHeader header{};
    std::memcpy(&header, data.data(), sizeof(header));

    if (header.magicNumber != kIl2CppMetadataMagic) {
        return std::nullopt;
    }
    if (header.version != kIl2CppMetadataVersion) {
        return std::nullopt;
    }
    if (header.assemblies.count < 0 || header.assemblies.count > 1000) {
        return std::nullopt;
    }
    if (header.images.count < 0 || header.images.count > 1000) {
        return std::nullopt;
    }
    if (header.typeDefinitions.count < 0 || header.typeDefinitions.count > 1'000'000) {
        return std::nullopt;
    }
    // Methods/fields/parameters can legitimately number in the hundreds of
    // thousands for a large game; ceiling is generous headroom, not a tight
    // real-world estimate — it exists only to reject corrupt/negative counts.
    if (header.methods.count < 0 || header.methods.count > 5'000'000) {
        return std::nullopt;
    }
    if (header.fields.count < 0 || header.fields.count > 5'000'000) {
        return std::nullopt;
    }
    if (header.parameters.count < 0 || header.parameters.count > 5'000'000) {
        return std::nullopt;
    }
    // stringTable.size is a byte-length of the string blob, not a count of
    // strings, so its ceiling is sized in bytes (here: 200 MB).
    if (header.stringTable.size < 0 || header.stringTable.size > 200'000'000) {
        return std::nullopt;
    }

    auto widths = compute_index_widths(header);
    if (!widths) {
        return std::nullopt;
    }
    IndexWidths w = *widths;

    auto images = decode_section<Il2CppImageDefinition>(data, header.images,
        [w](ByteCursor& c) { return decode_image_definition(c, w); });
    auto assemblies = decode_section<Il2CppAssemblyDefinition>(data, header.assemblies,
        [](ByteCursor& c) { return decode_assembly_definition(c); });
    auto types = decode_section<Il2CppTypeDefinition>(data, header.typeDefinitions,
        [w](ByteCursor& c) { return decode_type_definition(c, w); });
    auto methods = decode_section<Il2CppMethodDefinition>(data, header.methods,
        [w](ByteCursor& c) { return decode_method_definition(c, w); });
    auto fields = decode_section<Il2CppFieldDefinition>(data, header.fields,
        [w](ByteCursor& c) { return decode_field_definition(c, w); });
    auto parameters = decode_section<Il2CppParameterDefinition>(data, header.parameters,
        [w](ByteCursor& c) { return decode_parameter_definition(c, w); });

    if (!images || !assemblies || !types || !methods || !fields || !parameters) {
        return std::nullopt;
    }

    return Il2CppMetadataFile(std::move(data), header,
                               std::move(*images), std::move(*assemblies), std::move(*types),
                               std::move(*methods), std::move(*fields), std::move(*parameters));
}

std::optional<Il2CppMetadataFile> Il2CppMetadataFile::from_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        return std::nullopt;
    }

    auto size = file.tellg();
    if (size < 0) {
        return std::nullopt;
    }

    std::vector<uint8_t> data(static_cast<size_t>(size));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(data.data()), size);
    if (!file) {
        return std::nullopt;
    }

    return from_bytes(std::move(data));
}

// GetStringFromIndex in LibCpp2IL (Metadata/Il2CppMetadata.cs:437) computes
// stringOffset + index directly and reads a null-terminated string there.
// `index` is a BYTE OFFSET into the string blob, not an indirection-table
// index. See design doc section 11, bug 4.
std::optional<std::string> Il2CppMetadataFile::string_from_index(int32_t index) const {
    if (header_.stringTable.offset < 0) {
        return std::nullopt;
    }
    if (index < 0 || static_cast<uint32_t>(index) >= static_cast<uint32_t>(header_.stringTable.size)) {
        return std::nullopt;
    }

    size_t addr = static_cast<size_t>(header_.stringTable.offset) + static_cast<size_t>(index);
    if (addr >= data_.size()) {
        return std::nullopt;
    }

    const char* start = reinterpret_cast<const char*>(data_.data() + addr);
    size_t max_len = data_.size() - addr;
    size_t len = strnlen(start, max_len);
    if (len == max_len) {
        return std::nullopt; // not null-terminated within the buffer
    }
    return std::string(start, len);
}

const Il2CppImageDefinition* Il2CppMetadataFile::image(int32_t index) const {
    if (index < 0 || static_cast<size_t>(index) >= images_.size()) return nullptr;
    return &images_[static_cast<size_t>(index)];
}

const Il2CppAssemblyDefinition* Il2CppMetadataFile::assembly(int32_t index) const {
    if (index < 0 || static_cast<size_t>(index) >= assemblies_.size()) return nullptr;
    return &assemblies_[static_cast<size_t>(index)];
}

const Il2CppTypeDefinition* Il2CppMetadataFile::type(int32_t index) const {
    if (index < 0 || static_cast<size_t>(index) >= types_.size()) return nullptr;
    return &types_[static_cast<size_t>(index)];
}

const Il2CppMethodDefinition* Il2CppMetadataFile::method(int32_t index) const {
    if (index < 0 || static_cast<size_t>(index) >= methods_.size()) return nullptr;
    return &methods_[static_cast<size_t>(index)];
}

const Il2CppFieldDefinition* Il2CppMetadataFile::field(int32_t index) const {
    if (index < 0 || static_cast<size_t>(index) >= fields_.size()) return nullptr;
    return &fields_[static_cast<size_t>(index)];
}

const Il2CppParameterDefinition* Il2CppMetadataFile::parameter(int32_t index) const {
    if (index < 0 || static_cast<size_t>(index) >= parameters_.size()) return nullptr;
    return &parameters_[static_cast<size_t>(index)];
}

} // namespace il2bridge
