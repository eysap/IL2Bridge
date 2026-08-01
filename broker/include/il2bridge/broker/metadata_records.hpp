#pragma once
#include <cstdint>

namespace il2bridge {

// Decoded value types. Variable-width on-disk indices are normalized to
// int32_t; -1 represents a null index.

struct Il2CppAssemblyNameDefinition {
    int32_t nameIndex;
    int32_t cultureIndex;
    int32_t publicKeyIndex;
    uint32_t hashAlg;
    int32_t hashLen;
    uint32_t flags;
    int32_t majorVersion;
    int32_t minorVersion;
    int32_t buildNumber;
    int32_t revisionNumber;
    uint64_t publicKeyToken;
};

struct Il2CppAssemblyDefinition {
    int32_t imageIndex;
    uint32_t token;
    uint32_t moduleToken;              // added at metadata v38 — was missing before this fix
    int32_t referencedAssemblyStart;
    int32_t referencedAssemblyCount;
    Il2CppAssemblyNameDefinition assemblyName;
};

struct Il2CppImageDefinition {
    int32_t nameIndex;
    int32_t assemblyIndex;
    int32_t firstTypeIndex;      // TypeDefinition-width
    uint32_t typeCount;
    int32_t exportedTypeStart;   // TypeDefinition-width
    uint32_t exportedTypeCount;
    int32_t entryPointIndex;     // Method-width (fixed 4 bytes at v39)
    uint32_t token;
    int32_t customAttributeStart;
    uint32_t customAttributeCount;
};

struct Il2CppTypeDefinition {
    int32_t nameIndex;
    int32_t namespaceIndex;
    int32_t byvalTypeIndex;        // Type-width
    int32_t declaringTypeIndex;    // Type-width
    int32_t parentIndex;           // Type-width
    // ElementTypeIndex intentionally absent: removed at metadata v35, doesn't apply to v39.
    int32_t genericContainerIndex; // GenericContainer-width
    uint32_t flags;
    int32_t fieldStart;
    int32_t methodStart;
    int32_t eventStart;
    int32_t propertyStart;
    int32_t nestedTypesStart;
    int32_t interfacesStart;
    int32_t vtableStart;
    int32_t interfaceOffsetsStart;
    uint16_t methodCount;
    uint16_t propertyCount;
    uint16_t fieldCount;
    uint16_t eventCount;
    uint16_t nestedTypesCount;
    uint16_t vtableCount;
    uint16_t interfacesCount;
    uint16_t interfaceOffsetsCount;
    uint32_t bitfield;
    uint32_t token;
};

struct Il2CppMethodDefinition {
    int32_t nameIndex;
    int32_t declaringTypeIdx;      // TypeDefinition-width
    int32_t returnTypeIdx;         // Type-width
    uint32_t returnParameterToken;
    int32_t parameterStart;        // ParameterDefinition-width
    int32_t genericContainerIndex; // GenericContainer-width
    uint32_t token;
    uint16_t flags;
    uint16_t iflags;
    uint16_t slot;
    uint16_t parameterCount;
};

struct Il2CppFieldDefinition {
    int32_t nameIndex;
    int32_t typeIndex; // Type-width
    uint32_t token;
};

struct Il2CppParameterDefinition {
    int32_t nameIndex;
    uint32_t token;
    int32_t typeIndex; // Type-width
};

} // namespace il2bridge
