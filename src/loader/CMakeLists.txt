enable_language(C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
# Required for constructor attributes and GNU/Linux context APIs.
set(CMAKE_C_EXTENSIONS ON)

find_package(Threads REQUIRED)

add_library(il2bridge-loader SHARED
    discovery.c
    bridge.c
    events.c
    hooks/registry.c
    hooks/breakpoint.c
    hooks/trampoline.c
    handlers/registry.c
    handlers/log_1string_arg.c
    handlers/skip_return_void.c
    handlers/skip_return_null_object.c
    ipc_protocol.c
    ipc_server.c
    late_init.c
    init.c
)

set_target_properties(il2bridge-loader PROPERTIES
    C_VISIBILITY_PRESET hidden
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
)

# Keep symbols from static dependencies out of the injected library's ABI.
target_link_options(il2bridge-loader PRIVATE -Wl,--exclude-libs,ALL)

target_include_directories(il2bridge-loader PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(il2bridge-loader PRIVATE Zydis ${CMAKE_DL_LIBS} Threads::Threads)
