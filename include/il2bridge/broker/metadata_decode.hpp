#pragma once
#include "il2bridge/broker/byte_cursor.hpp"
#include "il2bridge/broker/index_widths.hpp"
#include "il2bridge/broker/metadata_records.hpp"
#include <optional>

namespace il2bridge {

std::optional<Il2CppAssemblyNameDefinition> decode_assembly_name(ByteCursor& cursor);
std::optional<Il2CppAssemblyDefinition> decode_assembly_definition(ByteCursor& cursor);
std::optional<Il2CppImageDefinition> decode_image_definition(ByteCursor& cursor, const IndexWidths& widths);
std::optional<Il2CppTypeDefinition> decode_type_definition(ByteCursor& cursor, const IndexWidths& widths);
std::optional<Il2CppMethodDefinition> decode_method_definition(ByteCursor& cursor, const IndexWidths& widths);
std::optional<Il2CppFieldDefinition> decode_field_definition(ByteCursor& cursor, const IndexWidths& widths);
std::optional<Il2CppParameterDefinition> decode_parameter_definition(ByteCursor& cursor, const IndexWidths& widths);

} // namespace il2bridge
