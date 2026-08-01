#pragma once
#include "il2bridge/loader/api.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char* name;
    void* function_pointer; // caller casts to the correct C function type for this signature shape
} HandlerEntry;

// Returns the entry for `name`, or NULL if no handler with that name exists.
IL2BRIDGE_LOADER_API const HandlerEntry* handler_lookup(const char* name);

#ifdef __cplusplus
}
#endif
