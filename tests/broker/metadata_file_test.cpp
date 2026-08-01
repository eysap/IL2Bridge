#include "il2bridge/broker/metadata_file.hpp"
#include "byte_fixture_helpers.hpp"
#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <string>
#include <vector>

using namespace il2bridge;
using namespace il2bridge::test;

namespace {

// Fixture widths derived from one interface-offset record and small tables.
constexpr uint8_t kTypeWidth = 4;
constexpr uint8_t kTypeDefWidth = 1;
constexpr uint8_t kGenericContainerWidth = 1;
constexpr uint8_t kParameterWidth = 1;

// Minimal header accepted by the width computation.
Il2CppGlobalMetadataHeader make_valid_header() {
    Il2CppGlobalMetadataHeader header{};
    header.magicNumber = kIl2CppMetadataMagic;
    header.version = kIl2CppMetadataVersion;
    header.interfaceOffsets.count = 1;
    header.interfaceOffsets.size = 8;
    return header;
}

std::vector<uint8_t> make_valid_header_bytes() {
    Il2CppGlobalMetadataHeader header = make_valid_header();
    std::vector<uint8_t> bytes(sizeof(header));
    std::memcpy(bytes.data(), &header, sizeof(header));
    return bytes;
}

// Serializes `header` and appends `trailing` bytes after it, useful for
// building fixtures with real section data past the header.
std::vector<uint8_t> make_bytes(const Il2CppGlobalMetadataHeader& header,
                                 const std::vector<uint8_t>& trailing = {}) {
    std::vector<uint8_t> bytes(sizeof(header));
    std::memcpy(bytes.data(), &header, sizeof(header));
    bytes.insert(bytes.end(), trailing.begin(), trailing.end());
    return bytes;
}

// Appends one byte-exact Image record (34 bytes given kTypeDefWidth=1).
void push_image_record(std::vector<uint8_t>& b, int32_t nameIndex, int32_t assemblyIndex,
                        int32_t firstTypeIndex, uint32_t typeCount, int32_t exportedTypeStart,
                        uint32_t exportedTypeCount, int32_t entryPointIndex, uint32_t token,
                        int32_t customAttributeStart, uint32_t customAttributeCount) {
    push_i32(b, nameIndex);
    push_i32(b, assemblyIndex);
    push_variable_index(b, kTypeDefWidth, firstTypeIndex);
    push_u32(b, typeCount);
    push_variable_index(b, kTypeDefWidth, exportedTypeStart);
    push_u32(b, exportedTypeCount);
    push_i32(b, entryPointIndex);
    push_u32(b, token);
    push_i32(b, customAttributeStart);
    push_u32(b, customAttributeCount);
}

// Appends one byte-exact AssemblyDefinition record (68 bytes, fixed width --
// AssemblyDefinition/AssemblyNameDefinition have no variable-width fields).
void push_assembly_record(std::vector<uint8_t>& b, int32_t imageIndex, uint32_t token,
                           uint32_t moduleToken, int32_t referencedAssemblyStart,
                           int32_t referencedAssemblyCount, int32_t nameIndex,
                           int32_t cultureIndex, int32_t publicKeyIndex, uint32_t hashAlg,
                           int32_t hashLen, uint32_t flags, int32_t majorVersion,
                           int32_t minorVersion, int32_t buildNumber, int32_t revisionNumber,
                           uint64_t publicKeyToken) {
    push_i32(b, imageIndex);
    push_u32(b, token);
    push_u32(b, moduleToken);
    push_i32(b, referencedAssemblyStart);
    push_i32(b, referencedAssemblyCount);
    push_i32(b, nameIndex);
    push_i32(b, cultureIndex);
    push_i32(b, publicKeyIndex);
    push_u32(b, hashAlg);
    push_i32(b, hashLen);
    push_u32(b, flags);
    push_i32(b, majorVersion);
    push_i32(b, minorVersion);
    push_i32(b, buildNumber);
    push_i32(b, revisionNumber);
    push_u64(b, publicKeyToken);
}

// Appends one byte-exact TypeDefinition record (81 bytes given kTypeWidth=4,
// kGenericContainerWidth=1).
void push_type_record(std::vector<uint8_t>& b, int32_t nameIndex, int32_t namespaceIndex,
                       int32_t byvalTypeIndex, int32_t declaringTypeIndex, int32_t parentIndex,
                       int32_t genericContainerIndex, uint32_t flags, int32_t fieldStart,
                       int32_t methodStart, int32_t eventStart, int32_t propertyStart,
                       int32_t nestedTypesStart, int32_t interfacesStart, int32_t vtableStart,
                       int32_t interfaceOffsetsStart, uint16_t methodCount, uint16_t propertyCount,
                       uint16_t fieldCount, uint16_t eventCount, uint16_t nestedTypesCount,
                       uint16_t vtableCount, uint16_t interfacesCount, uint16_t interfaceOffsetsCount,
                       uint32_t bitfield, uint32_t token) {
    push_i32(b, nameIndex);
    push_i32(b, namespaceIndex);
    push_variable_index(b, kTypeWidth, byvalTypeIndex);
    push_variable_index(b, kTypeWidth, declaringTypeIndex);
    push_variable_index(b, kTypeWidth, parentIndex);
    push_variable_index(b, kGenericContainerWidth, genericContainerIndex);
    push_u32(b, flags);
    push_i32(b, fieldStart);
    push_i32(b, methodStart);
    push_i32(b, eventStart);
    push_i32(b, propertyStart);
    push_i32(b, nestedTypesStart);
    push_i32(b, interfacesStart);
    push_i32(b, vtableStart);
    push_i32(b, interfaceOffsetsStart);
    push_u16(b, methodCount);
    push_u16(b, propertyCount);
    push_u16(b, fieldCount);
    push_u16(b, eventCount);
    push_u16(b, nestedTypesCount);
    push_u16(b, vtableCount);
    push_u16(b, interfacesCount);
    push_u16(b, interfaceOffsetsCount);
    push_u32(b, bitfield);
    push_u32(b, token);
}

// Appends one byte-exact MethodDefinition record (27 bytes given
// kTypeDefWidth=1, kTypeWidth=4, kParameterWidth=1, kGenericContainerWidth=1).
void push_method_record(std::vector<uint8_t>& b, int32_t nameIndex, int32_t declaringTypeIdx,
                         int32_t returnTypeIdx, uint32_t returnParameterToken, int32_t parameterStart,
                         int32_t genericContainerIndex, uint32_t token, uint16_t flags,
                         uint16_t iflags, uint16_t slot, uint16_t parameterCount) {
    push_i32(b, nameIndex);
    push_variable_index(b, kTypeDefWidth, declaringTypeIdx);
    push_variable_index(b, kTypeWidth, returnTypeIdx);
    push_u32(b, returnParameterToken);
    push_variable_index(b, kParameterWidth, parameterStart);
    push_variable_index(b, kGenericContainerWidth, genericContainerIndex);
    push_u32(b, token);
    push_u16(b, flags);
    push_u16(b, iflags);
    push_u16(b, slot);
    push_u16(b, parameterCount);
}

// Appends one byte-exact FieldDefinition record (12 bytes given kTypeWidth=4).
void push_field_record(std::vector<uint8_t>& b, int32_t nameIndex, int32_t typeIndex, uint32_t token) {
    push_i32(b, nameIndex);
    push_variable_index(b, kTypeWidth, typeIndex);
    push_u32(b, token);
}

// Appends one byte-exact ParameterDefinition record (12 bytes given kTypeWidth=4).
void push_parameter_record(std::vector<uint8_t>& b, int32_t nameIndex, uint32_t token, int32_t typeIndex) {
    push_i32(b, nameIndex);
    push_u32(b, token);
    push_variable_index(b, kTypeWidth, typeIndex);
}

// One linked record per decoded section.
struct FullFixture {
    std::vector<uint8_t> bytes;
    int32_t image_name_idx;
    int32_t assembly_name_idx;
    int32_t type_name_idx;
    int32_t namespace_name_idx;
    int32_t method_name_idx;
    int32_t field_name_idx;
    int32_t parameter_name_idx;
};

FullFixture make_full_valid_fixture() {
    std::string strings;
    strings += '\0'; // offset 0 reserved/unused

    auto add_string = [&strings](const char* s) {
        int32_t idx = static_cast<int32_t>(strings.size());
        strings += s;
        strings += '\0';
        return idx;
    };

    int32_t image_name_idx = add_string("TestAssembly.dll");
    int32_t assembly_name_idx = add_string("TestAssemblyName");
    int32_t type_name_idx = add_string("MyClass");
    int32_t namespace_name_idx = add_string("MyGame");
    int32_t method_name_idx = add_string("MyMethod");
    int32_t field_name_idx = add_string("MyField");
    int32_t parameter_name_idx = add_string("MyParam");

    auto header = make_valid_header();
    // Drives type_definition_width/generic_container_width/parameter_width
    // to 1 byte each via get_index_width() (count <= 255) -- consistent
    // with kTypeDefWidth/kGenericContainerWidth/kParameterWidth above.
    header.typeDefinitions.count = 1;
    header.genericContainers.count = 1;
    header.parameters.count = 1;

    int32_t cursor = static_cast<int32_t>(sizeof(header));

    header.stringTable.offset = cursor;
    header.stringTable.size = static_cast<int32_t>(strings.size());
    cursor += static_cast<int32_t>(strings.size());

    header.images.offset = cursor;
    header.images.count = 1;
    std::vector<uint8_t> image_bytes;
    push_image_record(image_bytes,
                       /*nameIndex=*/image_name_idx,
                       /*assemblyIndex=*/0,
                       /*firstTypeIndex=*/0,
                       /*typeCount=*/1,
                       /*exportedTypeStart=*/-1,
                       /*exportedTypeCount=*/0,
                       /*entryPointIndex=*/0,
                       /*token=*/0x10000001u,
                       /*customAttributeStart=*/-1,
                       /*customAttributeCount=*/0);
    header.images.size = static_cast<int32_t>(image_bytes.size());
    cursor += static_cast<int32_t>(image_bytes.size());

    header.assemblies.offset = cursor;
    header.assemblies.count = 1;
    std::vector<uint8_t> assembly_bytes;
    push_assembly_record(assembly_bytes,
                          /*imageIndex=*/0,
                          /*token=*/0x20000001u,
                          /*moduleToken=*/0x30000001u,
                          /*referencedAssemblyStart=*/-1,
                          /*referencedAssemblyCount=*/0,
                          /*nameIndex=*/assembly_name_idx,
                          /*cultureIndex=*/-1,
                          /*publicKeyIndex=*/-1,
                          /*hashAlg=*/0,
                          /*hashLen=*/0,
                          /*flags=*/0,
                          /*majorVersion=*/1,
                          /*minorVersion=*/2,
                          /*buildNumber=*/3,
                          /*revisionNumber=*/4,
                          /*publicKeyToken=*/0xAABBCCDDEEFF0011ull);
    header.assemblies.size = static_cast<int32_t>(assembly_bytes.size());
    cursor += static_cast<int32_t>(assembly_bytes.size());

    header.typeDefinitions.offset = cursor;
    // count already == 1 (set above for width derivation)
    std::vector<uint8_t> type_bytes;
    push_type_record(type_bytes,
                      /*nameIndex=*/type_name_idx,
                      /*namespaceIndex=*/namespace_name_idx,
                      /*byvalTypeIndex=*/5,
                      /*declaringTypeIndex=*/-1,
                      /*parentIndex=*/-1,
                      /*genericContainerIndex=*/-1,
                      /*flags=*/0x21u,
                      /*fieldStart=*/0,
                      /*methodStart=*/0,
                      /*eventStart=*/-1,
                      /*propertyStart=*/-1,
                      /*nestedTypesStart=*/-1,
                      /*interfacesStart=*/-1,
                      /*vtableStart=*/-1,
                      /*interfaceOffsetsStart=*/-1,
                      /*methodCount=*/1,
                      /*propertyCount=*/0,
                      /*fieldCount=*/1,
                      /*eventCount=*/0,
                      /*nestedTypesCount=*/0,
                      /*vtableCount=*/0,
                      /*interfacesCount=*/0,
                      /*interfaceOffsetsCount=*/0,
                      /*bitfield=*/0x1234u,
                      /*token=*/0x02000001u);
    header.typeDefinitions.size = static_cast<int32_t>(type_bytes.size());
    cursor += static_cast<int32_t>(type_bytes.size());

    header.methods.offset = cursor;
    header.methods.count = 1;
    std::vector<uint8_t> method_bytes;
    push_method_record(method_bytes,
                        /*nameIndex=*/method_name_idx,
                        /*declaringTypeIdx=*/0,
                        /*returnTypeIdx=*/7,
                        /*returnParameterToken=*/0x0A000099u,
                        /*parameterStart=*/0,
                        /*genericContainerIndex=*/-1,
                        /*token=*/0x06000042u,
                        /*flags=*/0x0006,
                        /*iflags=*/0x0011,
                        /*slot=*/0x0003,
                        /*parameterCount=*/1);
    header.methods.size = static_cast<int32_t>(method_bytes.size());
    cursor += static_cast<int32_t>(method_bytes.size());

    header.fields.offset = cursor;
    header.fields.count = 1;
    std::vector<uint8_t> field_bytes;
    push_field_record(field_bytes, /*nameIndex=*/field_name_idx, /*typeIndex=*/9, /*token=*/0x04000010u);
    header.fields.size = static_cast<int32_t>(field_bytes.size());
    cursor += static_cast<int32_t>(field_bytes.size());

    header.parameters.offset = cursor;
    // count already == 1 (set above for width derivation)
    std::vector<uint8_t> parameter_bytes;
    push_parameter_record(parameter_bytes, /*nameIndex=*/parameter_name_idx, /*token=*/0x0A000001u, /*typeIndex=*/11);
    header.parameters.size = static_cast<int32_t>(parameter_bytes.size());
    cursor += static_cast<int32_t>(parameter_bytes.size());

    std::vector<uint8_t> bytes(static_cast<size_t>(cursor));
    std::memcpy(bytes.data(), &header, sizeof(header));
    std::memcpy(bytes.data() + header.stringTable.offset, strings.data(), strings.size());
    std::memcpy(bytes.data() + header.images.offset, image_bytes.data(), image_bytes.size());
    std::memcpy(bytes.data() + header.assemblies.offset, assembly_bytes.data(), assembly_bytes.size());
    std::memcpy(bytes.data() + header.typeDefinitions.offset, type_bytes.data(), type_bytes.size());
    std::memcpy(bytes.data() + header.methods.offset, method_bytes.data(), method_bytes.size());
    std::memcpy(bytes.data() + header.fields.offset, field_bytes.data(), field_bytes.size());
    std::memcpy(bytes.data() + header.parameters.offset, parameter_bytes.data(), parameter_bytes.size());

    return FullFixture{std::move(bytes), image_name_idx, assembly_name_idx, type_name_idx,
                        namespace_name_idx, method_name_idx, field_name_idx, parameter_name_idx};
}

} // namespace

TEST_CASE("rejects buffer smaller than the header", "[metadata_file]") {
    std::vector<uint8_t> tiny(4, 0);
    REQUIRE_FALSE(Il2CppMetadataFile::from_bytes(tiny).has_value());
}

TEST_CASE("rejects wrong magic number", "[metadata_file]") {
    auto bytes = make_valid_header_bytes();
    // Corrupt the magic (first 4 bytes).
    bytes[0] = 0x00;
    REQUIRE_FALSE(Il2CppMetadataFile::from_bytes(bytes).has_value());
}

TEST_CASE("rejects wrong version", "[metadata_file]") {
    Il2CppGlobalMetadataHeader header{};
    header.magicNumber = kIl2CppMetadataMagic;
    header.version = 31; // Il2Bridge targets v39 only, YAGNI.
    std::vector<uint8_t> bytes(sizeof(header));
    std::memcpy(bytes.data(), &header, sizeof(header));

    REQUIRE_FALSE(Il2CppMetadataFile::from_bytes(bytes).has_value());
}

TEST_CASE("rejects unreasonable assembly count", "[metadata_file]") {
    Il2CppGlobalMetadataHeader header{};
    header.magicNumber = kIl2CppMetadataMagic;
    header.version = kIl2CppMetadataVersion;
    header.assemblies.count = 5000; // way above any real Unity game

    std::vector<uint8_t> bytes(sizeof(header));
    std::memcpy(bytes.data(), &header, sizeof(header));

    REQUIRE_FALSE(Il2CppMetadataFile::from_bytes(bytes).has_value());
}

TEST_CASE("accepts a minimal valid header with all-empty sections", "[metadata_file]") {
    auto bytes = make_valid_header_bytes();
    auto md = Il2CppMetadataFile::from_bytes(bytes);
    REQUIRE(md.has_value());
    REQUIRE(md->header().version == 39);
    REQUIRE(md->image(0) == nullptr);
    REQUIRE(md->assembly(0) == nullptr);
    REQUIRE(md->type(0) == nullptr);
    REQUIRE(md->method(0) == nullptr);
    REQUIRE(md->field(0) == nullptr);
    REQUIRE(md->parameter(0) == nullptr);
}

TEST_CASE("rejects unreasonable methods/fields/parameters/string counts", "[metadata_file]") {
    {
        auto header = make_valid_header();
        header.methods.count = 10'000'000; // above the generous ceiling
        REQUIRE_FALSE(Il2CppMetadataFile::from_bytes(make_bytes(header)).has_value());
    }
    {
        auto header = make_valid_header();
        header.fields.count = 10'000'000;
        REQUIRE_FALSE(Il2CppMetadataFile::from_bytes(make_bytes(header)).has_value());
    }
    {
        auto header = make_valid_header();
        header.parameters.count = 10'000'000;
        REQUIRE_FALSE(Il2CppMetadataFile::from_bytes(make_bytes(header)).has_value());
    }
    {
        auto header = make_valid_header();
        header.stringTable.size = 1'000'000'000; // 1 GB, above the byte-length ceiling
        REQUIRE_FALSE(Il2CppMetadataFile::from_bytes(make_bytes(header)).has_value());
    }
}

TEST_CASE("field accessor resolves index == count - 1 and rejects index == count", "[metadata_file]") {
    auto header = make_valid_header();
    header.fields.offset = static_cast<int32_t>(sizeof(Il2CppGlobalMetadataHeader));

    std::vector<uint8_t> trailing;
    push_field_record(trailing, /*nameIndex=*/10, /*typeIndex=*/20, /*token=*/0x04000001u);
    push_field_record(trailing, /*nameIndex=*/11, /*typeIndex=*/21, /*token=*/0x04000002u);
    header.fields.size = static_cast<int32_t>(trailing.size());
    header.fields.count = 2;

    auto md = Il2CppMetadataFile::from_bytes(make_bytes(header, trailing));
    REQUIRE(md.has_value());

    const Il2CppFieldDefinition* last = md->field(1); // index == count - 1
    REQUIRE(last != nullptr);
    REQUIRE(last->nameIndex == 11);
    REQUIRE(last->typeIndex == 21);
    REQUIRE(last->token == 0x04000002u);

    REQUIRE(md->field(2) == nullptr);  // index == count, rejected
    REQUIRE(md->field(-1) == nullptr); // negative index, rejected
}

TEST_CASE("rejects a section offset pointing past the end of the buffer", "[metadata_file]") {
    // Under the eager-decode architecture, a corrupt/out-of-range section
    // offset fails the *entire* parse rather than just that one accessor --
    // every section is decoded up front at load time, so there's no lazy
    // per-access path left to fail individually.
    auto header = make_valid_header();
    header.fields.offset = 1'000'000; // far beyond the tiny buffer below
    header.fields.count = 1;
    header.fields.size = 12;

    REQUIRE_FALSE(Il2CppMetadataFile::from_bytes(make_bytes(header)).has_value());
}

TEST_CASE("rejects a section whose declared count doesn't match size/element_size", "[metadata_file]") {
    // Reproduces the class of bug decode_section's consistency guard exists
    // to catch: a header lying about record count relative to the actual
    // byte length of the section (e.g. from a wrong width computation).
    auto header = make_valid_header();
    std::vector<uint8_t> field_bytes;
    push_field_record(field_bytes, /*nameIndex=*/1, /*typeIndex=*/2, /*token=*/3u); // 1 record, 12 bytes
    header.fields.offset = static_cast<int32_t>(sizeof(Il2CppGlobalMetadataHeader));
    header.fields.size = static_cast<int32_t>(field_bytes.size()); // only 1 record's worth of bytes
    header.fields.count = 2; // LIE: declares 2 records

    REQUIRE_FALSE(Il2CppMetadataFile::from_bytes(make_bytes(header, field_bytes)).has_value());
}

TEST_CASE("rejects a section whose declared size/count is internally consistent but exceeds the real buffer", "[metadata_file]") {
    // Header values agree with each other but exceed the backing buffer.
    auto header = make_valid_header();
    std::vector<uint8_t> field_bytes;
    push_field_record(field_bytes, /*nameIndex=*/1, /*typeIndex=*/2, /*token=*/3u); // 1 record, 12 bytes
    header.fields.offset = static_cast<int32_t>(sizeof(Il2CppGlobalMetadataHeader));
    header.fields.size = 12 * 100;   // internally consistent with count below (12 bytes/record)...
    header.fields.count = 100;       // ...but only 1 record's worth of bytes actually follows

    REQUIRE_FALSE(Il2CppMetadataFile::from_bytes(make_bytes(header, field_bytes)).has_value());
}

TEST_CASE("string_from_index rejects a negative stringOffset instead of wrapping", "[metadata_file]") {
    // Reproduces the reviewer-verified overflow: stringOffset = -100 and
    // index = 100 previously wrapped to address 0 via unsigned arithmetic
    // and silently returned a string instead of being rejected.
    auto header = make_valid_header();
    header.stringTable.offset = -100;
    header.stringTable.size = 200;

    auto md = Il2CppMetadataFile::from_bytes(make_bytes(header));
    REQUIRE(md.has_value());
    REQUIRE_FALSE(md->string_from_index(100).has_value());
}

TEST_CASE("string_from_index rejects an unterminated string near the buffer end", "[metadata_file]") {
    auto header = make_valid_header();
    header.stringTable.offset = static_cast<int32_t>(sizeof(Il2CppGlobalMetadataHeader));
    header.stringTable.size = 8;

    std::vector<uint8_t> trailing = {'h', 'e', 'l', 'l', 'o'}; // no null terminator

    auto md = Il2CppMetadataFile::from_bytes(make_bytes(header, trailing));
    REQUIRE(md.has_value());
    REQUIRE_FALSE(md->string_from_index(0).has_value());
}

TEST_CASE("string_from_index resolves a valid null-terminated string", "[metadata_file]") {
    auto header = make_valid_header();
    header.stringTable.offset = static_cast<int32_t>(sizeof(Il2CppGlobalMetadataHeader));
    header.stringTable.size = 6;

    std::vector<uint8_t> trailing = {'h', 'e', 'l', 'l', 'o', '\0'};

    auto md = Il2CppMetadataFile::from_bytes(make_bytes(header, trailing));
    REQUIRE(md.has_value());
    auto str = md->string_from_index(0);
    REQUIRE(str.has_value());
    REQUIRE(*str == "hello");
}

TEST_CASE("full fixture resolves all six accessors end-to-end with cross references", "[metadata_file][accessors]") {
    auto fixture = make_full_valid_fixture();
    auto md = Il2CppMetadataFile::from_bytes(fixture.bytes);
    REQUIRE(md.has_value());

    auto* image = md->image(0);
    REQUIRE(image != nullptr);
    REQUIRE(md->string_from_index(image->nameIndex) == "TestAssembly.dll");
    REQUIRE(image->assemblyIndex == 0);
    REQUIRE(image->firstTypeIndex == 0);
    REQUIRE(image->typeCount == 1);
    REQUIRE(image->exportedTypeStart == -1);
    REQUIRE(image->exportedTypeCount == 0);
    REQUIRE(image->entryPointIndex == 0);
    REQUIRE(image->token == 0x10000001u);
    REQUIRE(image->customAttributeStart == -1);
    REQUIRE(image->customAttributeCount == 0);

    auto* assembly = md->assembly(image->assemblyIndex);
    REQUIRE(assembly != nullptr);
    REQUIRE(assembly->imageIndex == 0);
    REQUIRE(assembly->token == 0x20000001u);
    REQUIRE(assembly->moduleToken == 0x30000001u);
    REQUIRE(assembly->referencedAssemblyStart == -1);
    REQUIRE(assembly->referencedAssemblyCount == 0);
    REQUIRE(md->string_from_index(assembly->assemblyName.nameIndex) == "TestAssemblyName");
    REQUIRE(assembly->assemblyName.cultureIndex == -1);
    REQUIRE(assembly->assemblyName.publicKeyIndex == -1);
    REQUIRE(assembly->assemblyName.majorVersion == 1);
    REQUIRE(assembly->assemblyName.minorVersion == 2);
    REQUIRE(assembly->assemblyName.buildNumber == 3);
    REQUIRE(assembly->assemblyName.revisionNumber == 4);
    REQUIRE(assembly->assemblyName.publicKeyToken == 0xAABBCCDDEEFF0011ull);

    auto* type = md->type(image->firstTypeIndex);
    REQUIRE(type != nullptr);
    REQUIRE(md->string_from_index(type->nameIndex) == "MyClass");
    REQUIRE(md->string_from_index(type->namespaceIndex) == "MyGame");
    REQUIRE(type->byvalTypeIndex == 5);
    REQUIRE(type->declaringTypeIndex == -1);
    REQUIRE(type->parentIndex == -1);
    REQUIRE(type->genericContainerIndex == -1);
    REQUIRE(type->flags == 0x21u);
    REQUIRE(type->fieldStart == 0);
    REQUIRE(type->methodStart == 0);
    REQUIRE(type->methodCount == 1);
    REQUIRE(type->fieldCount == 1);
    REQUIRE(type->bitfield == 0x1234u);
    REQUIRE(type->token == 0x02000001u);

    auto* method = md->method(type->methodStart);
    REQUIRE(method != nullptr);
    REQUIRE(md->string_from_index(method->nameIndex) == "MyMethod");
    REQUIRE(method->declaringTypeIdx == 0);
    REQUIRE(method->returnTypeIdx == 7);
    REQUIRE(method->returnParameterToken == 0x0A000099u);
    REQUIRE(method->parameterStart == 0);
    REQUIRE(method->genericContainerIndex == -1);
    REQUIRE(method->token == 0x06000042u);
    REQUIRE(method->flags == 0x0006u);
    REQUIRE(method->iflags == 0x0011u);
    REQUIRE(method->slot == 0x0003u);
    REQUIRE(method->parameterCount == 1);

    auto* field = md->field(type->fieldStart);
    REQUIRE(field != nullptr);
    REQUIRE(md->string_from_index(field->nameIndex) == "MyField");
    REQUIRE(field->typeIndex == 9);
    REQUIRE(field->token == 0x04000010u);

    auto* parameter = md->parameter(method->parameterStart);
    REQUIRE(parameter != nullptr);
    REQUIRE(md->string_from_index(parameter->nameIndex) == "MyParam");
    REQUIRE(parameter->token == 0x0A000001u);
    REQUIRE(parameter->typeIndex == 11);
}

TEST_CASE("out-of-range indices return nullptr for all six accessors", "[metadata_file][accessors]") {
    auto fixture = make_full_valid_fixture();
    auto md = Il2CppMetadataFile::from_bytes(fixture.bytes);
    REQUIRE(md.has_value());

    REQUIRE(md->image(1) == nullptr);
    REQUIRE(md->image(-1) == nullptr);
    REQUIRE(md->assembly(1) == nullptr);
    REQUIRE(md->assembly(-1) == nullptr);
    REQUIRE(md->type(1) == nullptr);
    REQUIRE(md->type(-1) == nullptr);
    REQUIRE(md->method(1) == nullptr);
    REQUIRE(md->method(-1) == nullptr);
    REQUIRE(md->field(1) == nullptr);
    REQUIRE(md->field(-1) == nullptr);
    REQUIRE(md->parameter(1) == nullptr);
    REQUIRE(md->parameter(-1) == nullptr);
    REQUIRE_FALSE(md->string_from_index(999999).has_value());
}
