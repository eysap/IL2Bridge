#include "il2bridge/loader/discovery.h"
#include <stdlib.h>
#include <string.h>

// Linux marks mapped files that were unlinked after loading with this suffix.
static const char kDeletedSuffix[] = " (deleted)";

bool discovery_find_module_in_stream(FILE* maps_stream, const char* filename, char* out_path, size_t out_path_size) {
    if (!maps_stream || !filename || !out_path || out_path_size == 0) {
        return false;
    }

    char* line = NULL;
    size_t line_capacity = 0;
    bool found = false;

    // Preserve complete mapped paths instead of truncating a long maps line.
    ssize_t read_len;
    while (!found && (read_len = getline(&line, &line_capacity, maps_stream)) != -1) {
        char* slash = strchr(line, '/');
        if (!slash) continue;

        char* newline = strchr(slash, '\n');
        if (newline) *newline = '\0';

        size_t slash_len = strlen(slash);
        size_t suffix_len = sizeof(kDeletedSuffix) - 1;
        if (slash_len >= suffix_len && strcmp(slash + slash_len - suffix_len, kDeletedSuffix) == 0) {
            slash[slash_len - suffix_len] = '\0';
        }

        const char* base = strrchr(slash, '/');
        base = base ? base + 1 : slash;

        if (strcmp(base, filename) == 0) {
            size_t len = strlen(slash);
            if (len < out_path_size) {
                memcpy(out_path, slash, len + 1);
                found = true;
            } else {
                found = false;
                break;
            }
        }
    }

    free(line);
    return found;
}

bool discovery_find_module(const char* filename, char* out_path, size_t out_path_size) {
    FILE* maps = fopen("/proc/self/maps", "r");
    if (!maps) {
        return false;
    }
    bool found = discovery_find_module_in_stream(maps, filename, out_path, out_path_size);
    fclose(maps);
    return found;
}
