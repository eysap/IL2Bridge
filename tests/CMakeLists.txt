find_package(Threads REQUIRED)

add_executable(il2bridge_tests
    broker/metadata_file_test.cpp
    broker/unity_discovery_test.cpp
    broker/byte_cursor_test.cpp
    broker/index_widths_test.cpp
    broker/metadata_decode_test.cpp
    broker/client_models_test.cpp
    broker/target_session_test.cpp
    broker/ipc_transport_test.cpp
    broker/target_discovery_test.cpp
    broker/cli_test.cpp
    broker/cli_runner.cpp
    broker/fake_loader.cpp
)
target_link_libraries(il2bridge_tests PRIVATE il2bridge_metadata il2bridge_client Catch2::Catch2WithMain Threads::Threads)
# cli_test.cpp drives the real CLI binary as a child process.
add_dependencies(il2bridge_tests il2bridge)
target_compile_definitions(il2bridge_tests PRIVATE
    IL2BRIDGE_CLI_PATH="$<TARGET_FILE:il2bridge>"
)
add_test(NAME il2bridge_tests COMMAND il2bridge_tests)

# Test fixtures include C targets in this directory scope.
enable_language(C)

add_library(fake_gameassembly SHARED loader/fixtures/fake_gameassembly.c)
set_target_properties(fake_gameassembly PROPERTIES
    C_STANDARD 11
    # dlsym() must see the fixture's il2cpp_* exports.
    C_VISIBILITY_PRESET default
)

# Same core lookup ABI, deliberately without any optional capability group.
add_library(core_only_gameassembly SHARED loader/fixtures/fake_gameassembly.c)
set_target_properties(core_only_gameassembly PROPERTIES
    C_STANDARD 11
    C_VISIBILITY_PRESET default
)
target_compile_definitions(core_only_gameassembly PRIVATE FAKE_GAMEASSEMBLY_CORE_ONLY=1)

# Watcher fixture with the exact basename searched in /proc/self/maps.
add_library(gameassembly_dut SHARED loader/fixtures/fake_gameassembly.c)
set_target_properties(gameassembly_dut PROPERTIES
    C_STANDARD 11
    C_VISIBILITY_PRESET default # see fake_gameassembly's comment above; same reason
    PREFIX ""
    OUTPUT_NAME "GameAssembly"
)

# End-to-end target that maps the watcher fixture after startup.
add_executable(dummy_target loader/fixtures/dummy_target.c)
add_dependencies(dummy_target gameassembly_dut)
target_link_libraries(dummy_target PRIVATE ${CMAKE_DL_LIBS})
target_compile_definitions(dummy_target PRIVATE
    DUMMY_TARGET_GAMEASSEMBLY_PATH="$<TARGET_FILE:gameassembly_dut>"
)

add_executable(il2bridge_loader_tests
    loader/discovery_test.cpp
    loader/bridge_test.cpp
    loader/events_test.cpp
    loader/hooks_registry_test.cpp
    loader/hooks_breakpoint_test.cpp
    loader/hooks_trampoline_test.cpp
    loader/handlers_test.cpp
    loader/ipc_protocol_test.cpp
    loader/ipc_server_test.cpp
    loader/late_init_test.cpp
)
target_link_libraries(il2bridge_loader_tests PRIVATE il2bridge-loader Catch2::Catch2WithMain)
target_link_libraries(il2bridge_loader_tests PRIVATE ${CMAKE_DL_LIBS} Threads::Threads)
target_compile_definitions(il2bridge_loader_tests PRIVATE
    FAKE_GAMEASSEMBLY_PATH="$<TARGET_FILE:fake_gameassembly>"
    CORE_ONLY_GAMEASSEMBLY_PATH="$<TARGET_FILE:core_only_gameassembly>"
    GAMEASSEMBLY_DUT_PATH="$<TARGET_FILE:gameassembly_dut>"
)
add_test(NAME il2bridge_loader_tests COMMAND il2bridge_loader_tests)

# Unit tests drive individual watcher iterations synchronously. The separate
# integration test below exercises the real background watcher.
set_tests_properties(il2bridge_loader_tests PROPERTIES
    ENVIRONMENT "IL2BRIDGE_DISABLE_WATCHER=1"
)

# Real LD_PRELOAD integration test, driven out of process.
add_test(
    NAME il2bridge_loader_integration
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/loader/integration_test.sh
            $<TARGET_FILE:il2bridge-loader>
            $<TARGET_FILE:dummy_target>
            $<TARGET_FILE:il2bridge>
)
