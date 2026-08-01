#pragma once
#include "il2bridge/loader/api.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define IL2BRIDGE_EVENT_TYPE_SIZE 32
#define IL2BRIDGE_EVENT_PAYLOAD_SIZE 256
#define IL2BRIDGE_EVENT_CAPACITY 1024

typedef struct {
    uint64_t sequence;
    uint64_t timestamp_ns;
    uint32_t hook_slot;
    uint32_t thread_id;
    char type[IL2BRIDGE_EVENT_TYPE_SIZE];
    char payload[IL2BRIDGE_EVENT_PAYLOAD_SIZE];
} Il2BridgeEvent;

IL2BRIDGE_LOADER_API uint64_t event_stream_publish(uint32_t hook_slot, const char* type, const char* payload);
IL2BRIDGE_LOADER_API uint64_t event_stream_next_sequence(void);
IL2BRIDGE_LOADER_API uint64_t event_stream_oldest_sequence(void);
IL2BRIDGE_LOADER_API uint64_t event_stream_dropped(void);
IL2BRIDGE_LOADER_API size_t event_stream_read(uint64_t after, size_t limit, Il2BridgeEvent* out);
IL2BRIDGE_LOADER_API void event_stream_reset_for_testing(void);

#ifdef __cplusplus
}
#endif
