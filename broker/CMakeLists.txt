add_library(il2bridge_metadata
    src/metadata/metadata_file.cpp
    src/metadata/unity_discovery.cpp
    src/metadata/byte_cursor.cpp
    src/metadata/index_widths.cpp
    src/metadata/metadata_decode.cpp
)
target_include_directories(il2bridge_metadata PUBLIC include)
target_link_libraries(il2bridge_metadata PUBLIC nlohmann_json::nlohmann_json)

add_library(il2bridge_client
    src/client/client_models.cpp
    src/client/ipc_transport.cpp
    src/client/target_session.cpp
    src/client/target_discovery.cpp
)
target_include_directories(il2bridge_client PUBLIC include)

add_executable(il2bridge
    src/cli/main.cpp
)
target_link_libraries(il2bridge PRIVATE il2bridge_metadata il2bridge_client)
