#include "il2bridge/loader/handlers.h"
#include <string.h>

void handler_log_1string_arg(void); // real signature differs per-handler; declared void(void) here
void handler_skip_return_void(void);
void handler_skip_return_null_object(void);

static const HandlerEntry g_handlers[] = {
    { "log-1string-arg", (void*)handler_log_1string_arg },
    { "skip-return-void", (void*)handler_skip_return_void },
    { "skip-return-null-object", (void*)handler_skip_return_null_object },
};

static const size_t g_handler_count = sizeof(g_handlers) / sizeof(g_handlers[0]);

const HandlerEntry* handler_lookup(const char* name) {
    if (!name) {
        return NULL;
    }
    for (size_t i = 0; i < g_handler_count; ++i) {
        if (strcmp(g_handlers[i].name, name) == 0) {
            return &g_handlers[i];
        }
    }
    return NULL;
}
