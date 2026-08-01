#pragma once
// Shared between breakpoint.c and trampoline.c only -- not part of the
// public loader API (no api.h visibility tag, no extern "C" guard: nothing
// outside src/loader/hooks includes this).

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/mman.h>
#include <unistd.h>

typedef struct {
    uint64_t hit_count;
} CounterProbeState;

// Widens the read/write/execute protection of the page(s) covering
// [addr, addr+len) so a hook installer can patch live code. Both hook
// implementations need this identically; kept in one place so a future
// third hook type doesn't end up with a third copy to keep in sync.
static inline bool unprotect_code(void* addr, size_t len) {
    long page_size = sysconf(_SC_PAGESIZE);
    uintptr_t page_start = (uintptr_t)addr & ~(uintptr_t)(page_size - 1);
    size_t page_len = ((uintptr_t)addr + len - page_start + page_size - 1) & ~(uintptr_t)(page_size - 1);
    return mprotect((void*)page_start, page_len, PROT_READ | PROT_WRITE | PROT_EXEC) == 0;
}
