#include "il2bridge/broker/metadata_decode.hpp"

namespace il2bridge {

std::optional<Il2CppAssemblyNameDefinition> decode_assembly_name(ByteCursor& cursor) {
    Il2CppAssemblyNameDefinition v{};
    v.nameIndex = cursor.read_i32();
    v.cultureIndex = cursor.read_i32();
    v.publicKeyIndex = cursor.read_i32();
    v.hashAlg = cursor.read_u32();
    v.hashLen = cursor.read_i32();
    v.flags = cursor.read_u32();
    v.majorVersion = cursor.read_i32();
    v.minorVersion = cursor.read_i32();
    v.buildNumber = cursor.read_i32();
    v.revisionNumber = cursor.read_i32();
    v.publicKeyToken = cursor.read_u64();
    if (!cursor.valid()) return std::nullopt;
    return v;
}

std::optional<Il2CppAssemblyDefinition> decode_assembly_definition(ByteCursor& cursor) {
    Il2CppAssemblyDefinition v{};
    v.imageIndex = cursor.read_i32();
    v.token = cursor.read_u32();
    v.moduleToken = cursor.read_u32();
    v.referencedAssemblyStart = cursor.read_i32();
    v.referencedAssemblyCount = cursor.read_i32();
    auto name = decode_assembly_name(cursor);
    if (!cursor.valid() || !name) return std::nullopt;
    v.assemblyName = *name;
    return v;
}

std::optional<Il2CppImageDefinition> decode_image_definition(ByteCursor& cursor, const IndexWidths& widths) {
    Il2CppImageDefinition v{};
    v.nameIndex = cursor.read_i32();
    v.assemblyIndex = cursor.read_i32();
    v.firstTypeIndex = cursor.read_variable_index(widths.type_definition_width);
    v.typeCount = cursor.read_u32();
    v.exportedTypeStart = cursor.read_variable_index(widths.type_definition_width);
    v.exportedTypeCount = cursor.read_u32();
    v.entryPointIndex = cursor.read_i32(); // Method-width, fixed 4 bytes at v39
    v.token = cursor.read_u32();
    v.customAttributeStart = cursor.read_i32();
    v.customAttributeCount = cursor.read_u32();
    if (!cursor.valid()) return std::nullopt;
    return v;
}

std::optional<Il2CppTypeDefinition> decode_type_definition(ByteCursor& cursor, const IndexWidths& widths) {
    Il2CppTypeDefinition v{};
    v.nameIndex = cursor.read_i32();
    v.namespaceIndex = cursor.read_i32();
    v.byvalTypeIndex = cursor.read_variable_index(widths.type_width);
    v.declaringTypeIndex = cursor.read_variable_index(widths.type_width);
    v.parentIndex = cursor.read_variable_index(widths.type_width);
    v.genericContainerIndex = cursor.read_variable_index(widths.generic_container_width);
    v.flags = cursor.read_u32();
    v.fieldStart = cursor.read_i32();
    v.methodStart = cursor.read_i32();
    v.eventStart = cursor.read_i32();
    v.propertyStart = cursor.read_i32();
    v.nestedTypesStart = cursor.read_i32();
    v.interfacesStart = cursor.read_i32();
    v.vtableStart = cursor.read_i32();
    v.interfaceOffsetsStart = cursor.read_i32();
    v.methodCount = cursor.read_u16();
    v.propertyCount = cursor.read_u16();
    v.fieldCount = cursor.read_u16();
    v.eventCount = cursor.read_u16();
    v.nestedTypesCount = cursor.read_u16();
    v.vtableCount = cursor.read_u16();
    v.interfacesCount = cursor.read_u16();
    v.interfaceOffsetsCount = cursor.read_u16();
    v.bitfield = cursor.read_u32();
    v.token = cursor.read_u32();
    if (!cursor.valid()) return std::nullopt;
    return v;
}

std::optional<Il2CppMethodDefinition> decode_method_definition(ByteCursor& cursor, const IndexWidths& widths) {
    Il2CppMethodDefinition v{};
    v.nameIndex = cursor.read_i32();
    v.declaringTypeIdx = cursor.read_variable_index(widths.type_definition_width);
    v.returnTypeIdx = cursor.read_variable_index(widths.type_width);
    v.returnParameterToken = cursor.read_u32();
    v.parameterStart = cursor.read_variable_index(widths.parameter_width);
    v.genericContainerIndex = cursor.read_variable_index(widths.generic_container_width);
    v.token = cursor.read_u32();
    v.flags = cursor.read_u16();
    v.iflags = cursor.read_u16();
    v.slot = cursor.read_u16();
    v.parameterCount = cursor.read_u16();
    if (!cursor.valid()) return std::nullopt;
    return v;
}

std::optional<Il2CppFieldDefinition> decode_field_definition(ByteCursor& cursor, const IndexWidths& widths) {
    Il2CppFieldDefinition v{};
    v.nameIndex = cursor.read_i32();
    v.typeIndex = cursor.read_variable_index(widths.type_width);
    v.token = cursor.read_u32();
    if (!cursor.valid()) return std::nullopt;
    return v;
}

std::optional<Il2CppParameterDefinition> decode_parameter_definition(ByteCursor& cursor, const IndexWidths& widths) {
    Il2CppParameterDefinition v{};
    v.nameIndex = cursor.read_i32();
    v.token = cursor.read_u32();
    v.typeIndex = cursor.read_variable_index(widths.type_width);
    if (!cursor.valid()) return std::nullopt;
    return v;
}

} // namespace il2bridge
