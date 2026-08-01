#include "il2bridge/loader/ipc.h"
#include <stddef.h>

static char* skip_spaces(char* p) {
    while (*p == ' ') {
        p++;
    }
    return p;
}

static char* skip_token(char* p) {
    while (*p != ' ' && *p != '\0') {
        p++;
    }
    return p;
}

int ipc_parse_line(char* line, const char** command_out, const char** args_out, int max_args) {
    if (line == NULL || command_out == NULL || args_out == NULL) {
        return -1;
    }

    char* p = skip_spaces(line);
    if (*p == '\0') {
        return -1;
    }

    *command_out = p;
    p = skip_token(p);

    int argc = 0;
    // p points to the separator after the previous token.
    while (*p != '\0') {
        *p = '\0'; // terminate the previous token in place
        p = skip_spaces(p + 1);
        if (*p == '\0') {
            break;
        }
        if (argc < max_args) {
            args_out[argc++] = p;
        }
        p = skip_token(p);
    }

    return argc;
}
