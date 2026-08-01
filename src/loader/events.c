#define _GNU_SOURCE
#include "il2bridge/loader/events.h"
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    uint64_t published_sequence;
    Il2BridgeEvent event;
} EventSlot;

static EventSlot g_slots[IL2BRIDGE_EVENT_CAPACITY];
static uint64_t g_next_sequence = 1;
static uint64_t g_dropped;

static uint64_t monotonic_ns(void) {
    struct timespec value;
    return clock_gettime(CLOCK_MONOTONIC, &value) == 0
        ? (uint64_t)value.tv_sec * 1000000000ull + (uint64_t)value.tv_nsec : 0;
}

uint64_t event_stream_publish(uint32_t hook_slot, const char* type, const char* payload) {
    uint64_t sequence = __atomic_fetch_add(&g_next_sequence, 1, __ATOMIC_RELAXED);
    EventSlot* slot = &g_slots[(sequence - 1) % IL2BRIDGE_EVENT_CAPACITY];
    uint64_t observed = __atomic_load_n(&slot->published_sequence, __ATOMIC_ACQUIRE);
    if (observed == UINT64_MAX ||
        !__atomic_compare_exchange_n(&slot->published_sequence, &observed, UINT64_MAX,
                                     false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        __atomic_fetch_add(&g_dropped, 1, __ATOMIC_RELAXED);
        return sequence;
    }
    if (observed != 0) __atomic_fetch_add(&g_dropped, 1, __ATOMIC_RELAXED);
    memset(&slot->event, 0, sizeof(slot->event));
    slot->event.sequence = sequence;
    slot->event.timestamp_ns = monotonic_ns();
    slot->event.hook_slot = hook_slot;
    slot->event.thread_id = (uint32_t)syscall(SYS_gettid);
    snprintf(slot->event.type, sizeof(slot->event.type), "%s", type ? type : "unknown");
    snprintf(slot->event.payload, sizeof(slot->event.payload), "%s", payload ? payload : "-");
    __atomic_store_n(&slot->published_sequence, sequence, __ATOMIC_RELEASE);
    return sequence;
}

uint64_t event_stream_next_sequence(void) {
    return __atomic_load_n(&g_next_sequence, __ATOMIC_ACQUIRE);
}

uint64_t event_stream_oldest_sequence(void) {
    uint64_t next = event_stream_next_sequence();
    return next > IL2BRIDGE_EVENT_CAPACITY ? next - IL2BRIDGE_EVENT_CAPACITY : 1;
}

uint64_t event_stream_dropped(void) {
    return __atomic_load_n(&g_dropped, __ATOMIC_RELAXED);
}

size_t event_stream_read(uint64_t after, size_t limit, Il2BridgeEvent* out) {
    if (!out || limit == 0) return 0;
    uint64_t next = event_stream_next_sequence();
    uint64_t sequence = after == UINT64_MAX ? UINT64_MAX : after + 1;
    uint64_t oldest = event_stream_oldest_sequence();
    if (sequence < oldest) sequence = oldest;
    size_t count = 0;
    while (sequence < next && count < limit) {
        EventSlot* slot = &g_slots[(sequence - 1) % IL2BRIDGE_EVENT_CAPACITY];
        if (__atomic_load_n(&slot->published_sequence, __ATOMIC_ACQUIRE) == sequence) {
            out[count++] = slot->event;
        }
        sequence++;
    }
    return count;
}

void event_stream_reset_for_testing(void) {
    memset(g_slots, 0, sizeof(g_slots));
    __atomic_store_n(&g_next_sequence, 1, __ATOMIC_RELEASE);
    __atomic_store_n(&g_dropped, 0, __ATOMIC_RELEASE);
}
