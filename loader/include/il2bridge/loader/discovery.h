#pragma once
#include "il2bridge/loader/api.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

// Finds an exact module basename in /proc/self/maps and copies its full path.
IL2BRIDGE_LOADER_API bool discovery_find_module(const char* filename, char* out_path, size_t out_path_size);

// Testable stream-based form of discovery_find_module().
IL2BRIDGE_LOADER_API bool discovery_find_module_in_stream(FILE* maps_stream, const char* filename, char* out_path, size_t out_path_size);

#ifdef __cplusplus
}
#endif
