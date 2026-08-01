#include <stdint.h>
#include <stdio.h>

typedef struct Il2CppString Il2CppString;

// Internal ABI assumption: Il2CppString stores its length before inline UTF-16.
typedef struct {
    void* klass;
    void* monitor;
    int32_t length;
    uint16_t chars[1];
} Il2CppStringLayout;

// Bounds reads if the layout or object is invalid.
#define IL2BRIDGE_MAX_LOGGED_STRING_CHARS 65536

void handler_log_1string_arg(Il2CppString* message) {
    if (!message) {
        fprintf(stderr, "[il2bridge] log-1string-arg: null string\n");
        return;
    }

    const Il2CppStringLayout* str = (const Il2CppStringLayout*)message;
    int32_t count = str->length;
    if (count < 0) {
        count = 0;
    } else if (count > IL2BRIDGE_MAX_LOGGED_STRING_CHARS) {
        count = IL2BRIDGE_MAX_LOGGED_STRING_CHARS;
    }

    fprintf(stderr, "[il2bridge] ");
    for (int32_t i = 0; i < count; ++i) {
        uint16_t c = str->chars[i];
        if (c < 0x80) {
            fputc((char)c, stderr);
        } else {
            fputc('?', stderr);
        }
    }
    fputc('\n', stderr);
}
