add_library(il2bridge_metadata
    metadata/metadata_file.cpp
    metadata/unity_discovery.cpp
    metadata/byte_cursor.cpp
    metadata/index_widths.cpp
    metadata/metadata_decode.cpp
)
target_include_directories(il2bridge_metadata PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(il2bridge_metadata PUBLIC nlohmann_json::nlohmann_json)

add_library(il2bridge_client
    client/client_models.cpp
    client/ipc_transport.cpp
    client/target_session.cpp
    client/target_discovery.cpp
)
target_include_directories(il2bridge_client PUBLIC ${PROJECT_SOURCE_DIR}/include)

add_executable(il2bridge
    cli/main.cpp
)
target_link_libraries(il2bridge PRIVATE il2bridge_metadata il2bridge_client)
set_target_properties(il2bridge PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
