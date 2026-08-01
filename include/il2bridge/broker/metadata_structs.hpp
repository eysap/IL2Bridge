#pragma once
#include <cstdint>

namespace il2bridge {

constexpr uint32_t kIl2CppMetadataMagic = 0xFAB11BAFu;
constexpr int32_t kIl2CppMetadataVersion = 39;

#pragma pack(push, 1)

struct Il2CppMetadataSectionHeader {
    int32_t offset;
    int32_t size;   // byte length of the section
    int32_t count;  // element count (v38+ only; Il2Bridge targets v39 exclusively, so always present)
};

// Field order/inclusion resolved for metadata version 39 specifically, verified
// against LibCpp2IL's development-branch Il2CppGlobalMetadataHeader.cs (2026-07-29).
// Every section is now 3 fields (Offset, Size, Count) since v38, not 2 — see
// design doc section 11 addendum for the full derivation.
struct Il2CppGlobalMetadataHeader {
    uint32_t magicNumber;
    int32_t version;

    Il2CppMetadataSectionHeader stringLiteral;
    Il2CppMetadataSectionHeader stringLiteralData;
    Il2CppMetadataSectionHeader stringTable; // identifier/name strings (called "string" in LibCpp2IL; renamed here to avoid colliding with std::string as an identifier)
    Il2CppMetadataSectionHeader events;
    Il2CppMetadataSectionHeader properties;
    Il2CppMetadataSectionHeader methods;
    Il2CppMetadataSectionHeader parameterDefaultValues;
    Il2CppMetadataSectionHeader fieldDefaultValues;
    Il2CppMetadataSectionHeader fieldAndParameterDefaultValueData;
    Il2CppMetadataSectionHeader fieldMarshaledSizes;
    Il2CppMetadataSectionHeader parameters;
    Il2CppMetadataSectionHeader fields;
    Il2CppMetadataSectionHeader genericParameters;
    Il2CppMetadataSectionHeader genericParameterConstraints;
    Il2CppMetadataSectionHeader genericContainers;
    Il2CppMetadataSectionHeader nestedTypes;
    Il2CppMetadataSectionHeader interfaces;
    Il2CppMetadataSectionHeader vtableMethods;
    Il2CppMetadataSectionHeader interfaceOffsets;
    Il2CppMetadataSectionHeader typeDefinitions;
    // typeInlineArrays omitted: [Version(Min=104)], doesn't apply to v39
    // rgctxEntries omitted: [Version(Max=24.15)], removed long before v39
    Il2CppMetadataSectionHeader images;
    Il2CppMetadataSectionHeader assemblies;
    // metadataUsageLists/metadataUsagePairs omitted: [Version(Max=24.5)], removed in v27
    Il2CppMetadataSectionHeader fieldRefs;
    Il2CppMetadataSectionHeader referencedAssemblies;
    // attributesInfo/attributeTypes omitted: [Version(Max=27.9)], pre-v29 variant
    Il2CppMetadataSectionHeader attributeData;       // [Version(Min=27.9)], post-v29 variant, applies to v39
    Il2CppMetadataSectionHeader attributeDataRange;  // [Version(Min=27.9)]
    Il2CppMetadataSectionHeader unresolvedVirtualCallParameterTypes;
    Il2CppMetadataSectionHeader unresolvedVirtualCallParameterRanges;
    Il2CppMetadataSectionHeader windowsRuntimeTypeNames;   // [Version(Min=23)]
    Il2CppMetadataSectionHeader windowsRuntimeStrings;     // [Version(Min=27)]
    Il2CppMetadataSectionHeader exportedTypeDefinitions;   // [Version(Min=24)]
    // methodSpecsOnGenericType through staticConstructorTypeIndices all omitted: [Version(Min=108)], doesn't apply to v39
};

static_assert(sizeof(Il2CppGlobalMetadataHeader) == 380,
              "v39 header must be 8 (magic+version) + 31 sections * 12 bytes = 380");

#pragma pack(pop)

} // namespace il2bridge
