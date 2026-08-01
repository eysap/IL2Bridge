enable_language(C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
# GNU extensions required starting with the trampoline/hooks work (typeof,
# __attribute__((constructor)) and friends) -- explicit, not left to
# CMake's implicit ON default, since the sibling C++ target next door
# deliberately sets CXX_EXTENSIONS OFF and a reader could wrongly assume
# the same convention applies here.
set(CMAKE_C_EXTENSIONS ON)

# pthread_once in bridge.c (bridge_init's thread-safety guard) is the
# loader's first pthread usage; glibc's merged libc doesn't guarantee the
# symbol is available without linking against the threads library on every
# target platform, so link explicitly rather than relying on it being pulled
# in transitively.
find_package(Threads REQUIRED)

add_library(il2bridge-loader SHARED
    src/discovery.c
    src/bridge.c
    src/events.c
    src/hooks/registry.c
    src/hooks/breakpoint.c
    src/hooks/trampoline.c
    src/handlers/registry.c
    src/handlers/log_1string_arg.c
    src/handlers/skip_return_void.c
    src/handlers/skip_return_null_object.c
    src/ipc_protocol.c
    src/ipc_server.c
    src/late_init.c
    src/init.c
)

set_target_properties(il2bridge-loader PROPERTIES
    C_VISIBILITY_PRESET hidden
)

# Defense in depth beyond CMAKE_C_VISIBILITY_PRESET (set in
# cmake/Dependencies.cmake before Zydis is fetched): strips exported symbols
# coming from any statically-linked archive regardless of how that archive's
# own build configured visibility -- doesn't depend on every future
# dependency cooperating.
target_link_options(il2bridge-loader PRIVATE -Wl,--exclude-libs,ALL)

target_include_directories(il2bridge-loader PUBLIC include)
target_link_libraries(il2bridge-loader PRIVATE Zydis ${CMAKE_DL_LIBS} Threads::Threads)
