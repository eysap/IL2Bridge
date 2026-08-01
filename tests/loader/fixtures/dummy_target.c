// End-to-end watcher fixture. The module is mapped after process startup.
#include <dlfcn.h>
#include <stdio.h>
#include <unistd.h>

__attribute__((noinline)) void dummy_target_function(void) {
    fprintf(stderr, "dummy_target_function: running\n");
}

int main(void) {
    usleep(300000);

    void* handle = dlopen(DUMMY_TARGET_GAMEASSEMBLY_PATH, RTLD_NOW);
    if (!handle) {
        fprintf(stderr, "dummy_target: failed to dlopen GameAssembly.so: %s\n", dlerror());
        return 1;
    }

    for (int i = 0; i < 20; ++i) {
        dummy_target_function();
        usleep(100000);
    }
    return 0;
}
