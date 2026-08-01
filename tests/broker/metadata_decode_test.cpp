#include "il2bridge/broker/metadata_decode.hpp"
#include "byte_fixture_helpers.hpp"
#include <catch2/catch_test_macros.hpp>
#include <vector>
#include <cstring>

using namespace il2bridge;
using namespace il2bridge::test;

namespace {

// Representative widths verified against a production metadata v39 file.
// (design doc 2026-07-30 section 3): type_width=4, type_definition_width=2,
// generic_container_width=2, parameter_width=4.
IndexWidths representative_v39_widths() {
    return IndexWidths{
        .type_width = 4,
        .type_definition_width = 2,
        .generic_container_width = 2,
        .parameter_width = 4,
    };
}

} // namespace

TEST_CASE("decodes an assembly name definition", "[metadata_decode]") {
    std::vector<uint8_t> bytes;
    push_i32(bytes, 10);   // nameIndex
    push_i32(bytes, 20);   // cultureIndex
    push_i32(bytes, 30);   // publicKeyIndex
    push_u32(bytes, 1);    // hashAlg
    push_i32(bytes, 0);    // hashLen
    push_u32(bytes, 0);    // flags
    push_i32(bytes, 1);    // majorVersion
    push_i32(bytes, 0);    // minorVersion
    push_i32(bytes, 0);    // buildNumber
    push_i32(bytes, 0);    // revisionNumber
    push_u64(bytes, 0xDEADBEEFCAFEBABEull); // publicKeyToken

    ByteCursor c(bytes, 0);
    auto result = decode_assembly_name(c);
    REQUIRE(c.valid());
    REQUIRE(result.has_value());
    REQUIRE(result->nameIndex == 10);
    REQUIRE(result->cultureIndex == 20);
    REQUIRE(result->publicKeyIndex == 30);
    REQUIRE(result->publicKeyToken == 0xDEADBEEFCAFEBABEull);
}

TEST_CASE("decodes an assembly definition with ModuleToken", "[metadata_decode]") {
    std::vector<uint8_t> bytes;
    push_i32(bytes, 1);      // imageIndex
    push_u32(bytes, 100);    // token
    push_u32(bytes, 200);    // moduleToken -- the previously-missing field
    push_i32(bytes, -1);     // referencedAssemblyStart
    push_i32(bytes, 0);      // referencedAssemblyCount
    // nested Il2CppAssemblyNameDefinition (48 bytes)
    push_i32(bytes, 5);   // nameIndex
    push_i32(bytes, -1);  // cultureIndex
    push_i32(bytes, -1);  // publicKeyIndex
    push_u32(bytes, 0);   // hashAlg
    push_i32(bytes, 0);   // hashLen
    push_u32(bytes, 0);   // flags
    push_i32(bytes, 1); push_i32(bytes, 0); push_i32(bytes, 0); push_i32(bytes, 0); // version
    push_u64(bytes, 0);   // publicKeyToken

    ByteCursor c(bytes, 0);
    auto result = decode_assembly_definition(c);
    REQUIRE(c.valid());
    REQUIRE(result.has_value());
    REQUIRE(result->imageIndex == 1);
    REQUIRE(result->token == 100);
    REQUIRE(result->moduleToken == 200);
    REQUIRE(result->assemblyName.nameIndex == 5);

    // Verify against the analytically-proven 68-byte record size (design doc
    // 2026-07-30 section 4): imageIndex(4) + token(4) + moduleToken(4) +
    // referencedAssemblyStart(4) + referencedAssemblyCount(4) + name(48) = 68.
    REQUIRE(c.position() == 68);
}

TEST_CASE("decodes an image definition using TypeDefinition-width indices", "[metadata_decode]") {
    auto widths = representative_v39_widths(); // type_definition_width = 2
    std::vector<uint8_t> bytes;
    push_i32(bytes, 7);                              // nameIndex
    push_i32(bytes, 0);                               // assemblyIndex
    push_variable_index(bytes, widths.type_definition_width, 1000); // firstTypeIndex
    push_u32(bytes, 50);                               // typeCount
    push_variable_index(bytes, widths.type_definition_width, 1050); // exportedTypeStart
    push_u32(bytes, 5);                                // exportedTypeCount
    push_i32(bytes, 42);                               // entryPointIndex (fixed 4 bytes)
    push_u32(bytes, 999);                               // token
    push_i32(bytes, -1);                                // customAttributeStart
    push_u32(bytes, 0);                                 // customAttributeCount

    ByteCursor c(bytes, 0);
    auto result = decode_image_definition(c, widths);
    REQUIRE(c.valid());
    REQUIRE(result.has_value());
    REQUIRE(result->nameIndex == 7);
    REQUIRE(result->firstTypeIndex == 1000);
    REQUIRE(result->typeCount == 50);
    REQUIRE(result->exportedTypeStart == 1050);
    REQUIRE(result->entryPointIndex == 42);

    // Analytically-proven 36-byte record size (design doc section 4).
    REQUIRE(c.position() == 36);
}

TEST_CASE("decode functions reject truncated input instead of returning garbage", "[metadata_decode]") {
    std::vector<uint8_t> truncated = {0x01, 0x02, 0x03}; // way too short for any record
    ByteCursor c(truncated, 0);
    REQUIRE_FALSE(decode_assembly_name(c).has_value());
}

TEST_CASE("decode_assembly_definition rejects truncation inside the nested name", "[metadata_decode]") {
    std::vector<uint8_t> bytes;
    push_i32(bytes, 1);   // imageIndex
    push_u32(bytes, 100); // token
    push_u32(bytes, 200); // moduleToken
    push_i32(bytes, -1);  // referencedAssemblyStart
    push_i32(bytes, 0);   // referencedAssemblyCount
    push_i32(bytes, 5);   // nested name: nameIndex only, then nothing -- truncated

    ByteCursor c(bytes, 0);
    REQUIRE_FALSE(decode_assembly_definition(c).has_value());
    REQUIRE_FALSE(c.valid());
}

TEST_CASE("decode_image_definition rejects truncated input", "[metadata_decode]") {
    auto widths = representative_v39_widths();
    std::vector<uint8_t> bytes;
    push_i32(bytes, 7);  // nameIndex
    push_i32(bytes, 0);  // assemblyIndex -- then nothing, truncated well before firstTypeIndex

    ByteCursor c(bytes, 0);
    REQUIRE_FALSE(decode_image_definition(c, widths).has_value());
    REQUIRE_FALSE(c.valid());
}

TEST_CASE("decodes a type definition using Type and GenericContainer widths", "[metadata_decode]") {
    // Every field gets a distinct, non-degenerate value so a future field
    // reorder/transposition (e.g. swapping two adjacent *Start fields) fails
    // an assertion here instead of silently passing with an unchanged byte count.
    auto widths = representative_v39_widths(); // type_width=4, generic_container_width=2
    std::vector<uint8_t> bytes;
    push_i32(bytes, 100);    // nameIndex
    push_i32(bytes, 200);    // namespaceIndex
    push_variable_index(bytes, widths.type_width, 5);    // byvalTypeIndex
    push_variable_index(bytes, widths.type_width, -1);   // declaringTypeIndex (null)
    push_variable_index(bytes, widths.type_width, 3);    // parentIndex
    push_variable_index(bytes, widths.generic_container_width, 7); // genericContainerIndex
    push_u32(bytes, 0x00000021); // flags
    push_i32(bytes, 10); // fieldStart
    push_i32(bytes, 20); // methodStart
    push_i32(bytes, 30); // eventStart
    push_i32(bytes, 40); // propertyStart
    push_i32(bytes, 50); // nestedTypesStart
    push_i32(bytes, 60); // interfacesStart
    push_i32(bytes, 70); // vtableStart
    push_i32(bytes, 80); // interfaceOffsetsStart
    push_u16(bytes, 3);  // methodCount
    push_u16(bytes, 4);  // propertyCount
    push_u16(bytes, 2);  // fieldCount
    push_u16(bytes, 5);  // eventCount
    push_u16(bytes, 6);  // nestedTypesCount
    push_u16(bytes, 7);  // vtableCount
    push_u16(bytes, 8);  // interfacesCount
    push_u16(bytes, 9);  // interfaceOffsetsCount
    push_u32(bytes, 0x1234); // bitfield
    push_u32(bytes, 0x02000001); // token

    ByteCursor c(bytes, 0);
    auto result = decode_type_definition(c, widths);
    REQUIRE(c.valid());
    REQUIRE(result.has_value());
    REQUIRE(result->nameIndex == 100);
    REQUIRE(result->namespaceIndex == 200);
    REQUIRE(result->byvalTypeIndex == 5);
    REQUIRE(result->declaringTypeIndex == -1);
    REQUIRE(result->parentIndex == 3);
    REQUIRE(result->genericContainerIndex == 7);
    REQUIRE(result->flags == 0x21u);
    REQUIRE(result->fieldStart == 10);
    REQUIRE(result->methodStart == 20);
    REQUIRE(result->eventStart == 30);
    REQUIRE(result->propertyStart == 40);
    REQUIRE(result->nestedTypesStart == 50);
    REQUIRE(result->interfacesStart == 60);
    REQUIRE(result->vtableStart == 70);
    REQUIRE(result->interfaceOffsetsStart == 80);
    REQUIRE(result->methodCount == 3);
    REQUIRE(result->propertyCount == 4);
    REQUIRE(result->fieldCount == 2);
    REQUIRE(result->eventCount == 5);
    REQUIRE(result->nestedTypesCount == 6);
    REQUIRE(result->vtableCount == 7);
    REQUIRE(result->interfacesCount == 8);
    REQUIRE(result->interfaceOffsetsCount == 9);
    REQUIRE(result->bitfield == 0x1234u);
    REQUIRE(result->token == 0x02000001u);

    // Analytically-proven 82-byte record size for these widths (design doc section 4).
    REQUIRE(c.position() == 82);
}

TEST_CASE("decode_type_definition with 2-byte Type width produces a smaller record", "[metadata_decode]") {
    // Distinct values again here, and byvalTypeIndex is null this time
    // (declaringTypeIndex was null above) so both fields get null-sentinel
    // coverage across the two tests, not just one of the four variable-width fields.
    IndexWidths widths{.type_width = 2, .type_definition_width = 2, .generic_container_width = 1, .parameter_width = 2};
    std::vector<uint8_t> bytes;
    push_i32(bytes, 1);  // nameIndex
    push_i32(bytes, 2);  // namespaceIndex
    push_variable_index(bytes, widths.type_width, -1); // byvalTypeIndex (null)
    push_variable_index(bytes, widths.type_width, 11); // declaringTypeIndex
    push_variable_index(bytes, widths.type_width, 12); // parentIndex
    push_variable_index(bytes, widths.generic_container_width, -1); // genericContainerIndex (null)
    push_u32(bytes, 0xAA); // flags
    for (int i = 0; i < 8; ++i) push_i32(bytes, 100 + i); // 8 x distinct int32 start fields
    for (int i = 0; i < 8; ++i) push_u16(bytes, 1 + i);   // 8 x distinct uint16 count fields
    push_u32(bytes, 0x55); // bitfield
    push_u32(bytes, 0x99); // token

    ByteCursor c(bytes, 0);
    auto result = decode_type_definition(c, widths);
    REQUIRE(c.valid());
    REQUIRE(result.has_value());
    REQUIRE(result->nameIndex == 1);
    REQUIRE(result->namespaceIndex == 2);
    REQUIRE(result->byvalTypeIndex == -1);
    REQUIRE(result->declaringTypeIndex == 11);
    REQUIRE(result->parentIndex == 12);
    REQUIRE(result->genericContainerIndex == -1);
    REQUIRE(result->flags == 0xAAu);
    REQUIRE(result->fieldStart == 100);
    REQUIRE(result->methodStart == 101);
    REQUIRE(result->eventStart == 102);
    REQUIRE(result->propertyStart == 103);
    REQUIRE(result->nestedTypesStart == 104);
    REQUIRE(result->interfacesStart == 105);
    REQUIRE(result->vtableStart == 106);
    REQUIRE(result->interfaceOffsetsStart == 107);
    REQUIRE(result->methodCount == 1);
    REQUIRE(result->propertyCount == 2);
    REQUIRE(result->fieldCount == 3);
    REQUIRE(result->eventCount == 4);
    REQUIRE(result->nestedTypesCount == 5);
    REQUIRE(result->vtableCount == 6);
    REQUIRE(result->interfacesCount == 7);
    REQUIRE(result->interfaceOffsetsCount == 8);
    REQUIRE(result->bitfield == 0x55u);
    REQUIRE(result->token == 0x99u);

    // 8 + 2+2+2 (Type width 2 x3) + 1 (GenericContainer width 1) + 4 (flags)
    // + 32 (8 x int32 starts) + 16 (8 x uint16 counts) + 4 (bitfield) + 4 (token) = 75
    REQUIRE(c.position() == 75);
}

TEST_CASE("decodes a method definition using TypeDefinition/Type/ParameterDefinition/GenericContainer widths", "[metadata_decode]") {
    auto widths = representative_v39_widths(); // type_definition=2, type=4, parameter=4, generic_container=2
    std::vector<uint8_t> bytes;
    push_i32(bytes, 55); // nameIndex
    push_variable_index(bytes, widths.type_definition_width, 12); // declaringTypeIdx
    push_variable_index(bytes, widths.type_width, 7);             // returnTypeIdx
    push_u32(bytes, 0xABCD); // returnParameterToken
    push_variable_index(bytes, widths.parameter_width, 100);      // parameterStart
    push_variable_index(bytes, widths.generic_container_width, -1); // genericContainerIndex (null)
    push_u32(bytes, 0x06000042); // token
    push_u16(bytes, 0x0006); // flags
    push_u16(bytes, 0x0011); // iflags
    push_u16(bytes, 0x0022); // slot
    push_u16(bytes, 2);      // parameterCount

    ByteCursor c(bytes, 0);
    auto result = decode_method_definition(c, widths);
    REQUIRE(c.valid());
    REQUIRE(result.has_value());
    REQUIRE(result->nameIndex == 55);
    REQUIRE(result->declaringTypeIdx == 12);
    REQUIRE(result->returnTypeIdx == 7);
    REQUIRE(result->returnParameterToken == 0xABCDu);
    REQUIRE(result->parameterStart == 100);
    REQUIRE(result->genericContainerIndex == -1);
    REQUIRE(result->token == 0x06000042u);
    REQUIRE(result->flags == 0x0006u);
    REQUIRE(result->iflags == 0x0011u);
    REQUIRE(result->slot == 0x0022u);
    REQUIRE(result->parameterCount == 2);

    // Analytically-proven 32-byte record size (design doc section 4).
    REQUIRE(c.position() == 32);
}

TEST_CASE("decode_method_definition with different widths and null declaringTypeIdx", "[metadata_decode]") {
    IndexWidths widths{.type_width = 2, .type_definition_width = 1, .generic_container_width = 4, .parameter_width = 2};
    std::vector<uint8_t> bytes;
    push_i32(bytes, 9);  // nameIndex
    push_variable_index(bytes, widths.type_definition_width, -1); // declaringTypeIdx (null)
    push_variable_index(bytes, widths.type_width, 33);            // returnTypeIdx
    push_u32(bytes, 0x1111); // returnParameterToken
    push_variable_index(bytes, widths.parameter_width, 44);       // parameterStart
    push_variable_index(bytes, widths.generic_container_width, 5); // genericContainerIndex
    push_u32(bytes, 0x2222); // token
    push_u16(bytes, 1); // flags
    push_u16(bytes, 2); // iflags
    push_u16(bytes, 3); // slot
    push_u16(bytes, 4); // parameterCount

    ByteCursor c(bytes, 0);
    auto result = decode_method_definition(c, widths);
    REQUIRE(c.valid());
    REQUIRE(result.has_value());
    REQUIRE(result->nameIndex == 9);
    REQUIRE(result->declaringTypeIdx == -1);
    REQUIRE(result->returnTypeIdx == 33);
    REQUIRE(result->returnParameterToken == 0x1111u);
    REQUIRE(result->parameterStart == 44);
    REQUIRE(result->genericContainerIndex == 5);
    REQUIRE(result->token == 0x2222u);
    REQUIRE(result->flags == 1);
    REQUIRE(result->iflags == 2);
    REQUIRE(result->slot == 3);
    REQUIRE(result->parameterCount == 4);
    // nameIndex(4) + declaringTypeIdx(1) + returnTypeIdx(2) + returnParameterToken(4)
    // + parameterStart(2) + genericContainerIndex(4) + token(4) + flags/iflags/slot/parameterCount(2x4=8) = 29
    REQUIRE(c.position() == 29);
}

TEST_CASE("decodes a field definition using Type width", "[metadata_decode]") {
    auto widths = representative_v39_widths();
    std::vector<uint8_t> bytes;
    push_i32(bytes, 9);
    push_variable_index(bytes, widths.type_width, 4);
    push_u32(bytes, 0x04000010);

    ByteCursor c(bytes, 0);
    auto result = decode_field_definition(c, widths);
    REQUIRE(c.valid());
    REQUIRE(result.has_value());
    REQUIRE(result->nameIndex == 9);
    REQUIRE(result->typeIndex == 4);
    REQUIRE(result->token == 0x04000010u);
    // Analytically-proven 12-byte record size.
    REQUIRE(c.position() == 12);
}

TEST_CASE("decode_field_definition with 1-byte Type width and null typeIndex", "[metadata_decode]") {
    IndexWidths widths{.type_width = 1, .type_definition_width = 1, .generic_container_width = 1, .parameter_width = 1};
    std::vector<uint8_t> bytes;
    push_i32(bytes, 77);
    push_variable_index(bytes, widths.type_width, -1); // null
    push_u32(bytes, 0x1234);

    ByteCursor c(bytes, 0);
    auto result = decode_field_definition(c, widths);
    REQUIRE(c.valid());
    REQUIRE(result.has_value());
    REQUIRE(result->nameIndex == 77);
    REQUIRE(result->typeIndex == -1);
    REQUIRE(result->token == 0x1234u);
    // nameIndex(4) + typeIndex(1) + token(4) = 9
    REQUIRE(c.position() == 9);
}

TEST_CASE("decodes a parameter definition using Type width", "[metadata_decode]") {
    auto widths = representative_v39_widths();
    std::vector<uint8_t> bytes;
    push_i32(bytes, 3);
    push_u32(bytes, 0x0A000001);
    push_variable_index(bytes, widths.type_width, 6);

    ByteCursor c(bytes, 0);
    auto result = decode_parameter_definition(c, widths);
    REQUIRE(c.valid());
    REQUIRE(result.has_value());
    REQUIRE(result->nameIndex == 3);
    REQUIRE(result->token == 0x0A000001u);
    REQUIRE(result->typeIndex == 6);
    // Analytically-proven 12-byte record size.
    REQUIRE(c.position() == 12);
}

TEST_CASE("decode_parameter_definition with 2-byte Type width and null typeIndex", "[metadata_decode]") {
    IndexWidths widths{.type_width = 2, .type_definition_width = 2, .generic_container_width = 2, .parameter_width = 2};
    std::vector<uint8_t> bytes;
    push_i32(bytes, 88);
    push_u32(bytes, 0x5678);
    push_variable_index(bytes, widths.type_width, -1); // null

    ByteCursor c(bytes, 0);
    auto result = decode_parameter_definition(c, widths);
    REQUIRE(c.valid());
    REQUIRE(result.has_value());
    REQUIRE(result->nameIndex == 88);
    REQUIRE(result->token == 0x5678u);
    REQUIRE(result->typeIndex == -1);
    // nameIndex(4) + token(4) + typeIndex(2) = 10
    REQUIRE(c.position() == 10);
}

TEST_CASE("decode_method/field/parameter reject truncated input", "[metadata_decode]") {
    std::vector<uint8_t> truncated = {0x01, 0x02};
    auto widths = representative_v39_widths();
    {
        ByteCursor c(truncated, 0);
        REQUIRE_FALSE(decode_method_definition(c, widths).has_value());
    }
    {
        ByteCursor c(truncated, 0);
        REQUIRE_FALSE(decode_field_definition(c, widths).has_value());
    }
    {
        ByteCursor c(truncated, 0);
        REQUIRE_FALSE(decode_parameter_definition(c, widths).has_value());
    }
}
