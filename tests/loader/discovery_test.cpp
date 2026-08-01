#include "il2bridge/loader/discovery.h"
#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <string>

TEST_CASE("finds a module by exact basename match", "[discovery]") {
    const char* maps =
        "00400000-00401000 r-xp 00000000 08:01 123 /usr/bin/somebinary\n"
        "7f0000000000-7f0000100000 r-xp 00000000 08:01 456 /home/user/game/GameAssembly.so\n"
        "7f0000100000-7f0000200000 r--p 00000000 08:01 789 /lib/x86_64-linux-gnu/libc.so.6\n";
    FILE* stream = fmemopen((void*)maps, strlen(maps), "r");
    REQUIRE(stream != nullptr);

    char path[512];
    bool found = discovery_find_module_in_stream(stream, "GameAssembly.so", path, sizeof(path));
    fclose(stream);

    REQUIRE(found);
    REQUIRE(std::string(path) == "/home/user/game/GameAssembly.so");
}

TEST_CASE("returns false when the module isn't present", "[discovery]") {
    const char* maps = "00400000-00401000 r-xp 00000000 08:01 123 /usr/bin/somebinary\n";
    FILE* stream = fmemopen((void*)maps, strlen(maps), "r");
    char path[512];
    bool found = discovery_find_module_in_stream(stream, "GameAssembly.so", path, sizeof(path));
    fclose(stream);
    REQUIRE_FALSE(found);
}

TEST_CASE("returns false when the output buffer is too small", "[discovery]") {
    const char* maps = "7f0000000000-7f0000100000 r-xp 00000000 08:01 456 /home/user/game/GameAssembly.so\n";
    FILE* stream = fmemopen((void*)maps, strlen(maps), "r");
    char path[8]; // too small for the real path
    bool found = discovery_find_module_in_stream(stream, "GameAssembly.so", path, sizeof(path));
    fclose(stream);
    REQUIRE_FALSE(found);
}

TEST_CASE("matches by basename, not by substring of the full path", "[discovery]") {
    // A module named "NotGameAssembly.so" must not match a search for "GameAssembly.so".
    const char* maps = "7f0000000000-7f0000100000 r-xp 00000000 08:01 456 /home/user/game/NotGameAssembly.so\n";
    FILE* stream = fmemopen((void*)maps, strlen(maps), "r");
    char path[512];
    bool found = discovery_find_module_in_stream(stream, "GameAssembly.so", path, sizeof(path));
    fclose(stream);
    REQUIRE_FALSE(found);
}

TEST_CASE("handles multiple mappings of the same module and returns the first", "[discovery]") {
    // A .so is typically mapped multiple times (code, data, etc segments).
    const char* maps =
        "7f0000000000-7f0000100000 r-xp 00000000 08:01 456 /home/user/game/GameAssembly.so\n"
        "7f0000100000-7f0000200000 rw-p 00100000 08:01 456 /home/user/game/GameAssembly.so\n";
    FILE* stream = fmemopen((void*)maps, strlen(maps), "r");
    char path[512];
    bool found = discovery_find_module_in_stream(stream, "GameAssembly.so", path, sizeof(path));
    fclose(stream);
    REQUIRE(found);
    REQUIRE(std::string(path) == "/home/user/game/GameAssembly.so");
}

TEST_CASE("strips a trailing (deleted) suffix before matching", "[discovery]") {
    // The kernel appends this when the backing file was removed while still
    // mapped -- can happen with Proton prefixes, atomic updaters, etc.
    const char* maps = "7f0000000000-7f0000100000 r-xp 00000000 08:01 456 /home/user/game/GameAssembly.so (deleted)\n";
    FILE* stream = fmemopen((void*)maps, strlen(maps), "r");
    char path[512];
    bool found = discovery_find_module_in_stream(stream, "GameAssembly.so", path, sizeof(path));
    fclose(stream);
    REQUIRE(found);
    REQUIRE(std::string(path) == "/home/user/game/GameAssembly.so");
}

TEST_CASE("handles a maps line far longer than a fixed 1024-byte buffer would allow", "[discovery]") {
    // A deeply nested path (long username, Steam/Proton compatdata,
    // sandboxed mount points) can push a real maps line well past what a
    // naive fixed-size fgets() buffer would hold without truncating.
    std::string deep_dir;
    for (int i = 0; i < 100; ++i) {
        deep_dir += "/a-fairly-long-directory-segment";
    }
    std::string maps = "7f0000000000-7f0000100000 r-xp 00000000 08:01 456 " + deep_dir + "/GameAssembly.so\n";
    FILE* stream = fmemopen((void*)maps.data(), maps.size(), "r");

    std::string path(deep_dir.size() + 64, '\0');
    bool found = discovery_find_module_in_stream(stream, "GameAssembly.so", path.data(), path.size());
    fclose(stream);

    REQUIRE(found);
    REQUIRE(std::string(path.c_str()) == deep_dir + "/GameAssembly.so");
}
