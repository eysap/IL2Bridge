#include "il2bridge/broker/unity_discovery.hpp"
#include <catch2/catch_test_macros.hpp>
#include <fstream>

using namespace il2bridge;
namespace fs = std::filesystem;

TEST_CASE("finds metadata following the Unity convention", "[unity_discovery]") {
    fs::path root = fs::temp_directory_path() / "il2bridge_test_game";
    fs::path data_dir = root / "MyGame_Data" / "il2cpp_data" / "Metadata";
    fs::create_directories(data_dir);
    std::ofstream(data_dir / "global-metadata.dat") << "fake";

    auto result = find_global_metadata(root / "MyGame.x64");
    REQUIRE(result.has_value());
    REQUIRE(fs::equivalent(*result, data_dir / "global-metadata.dat"));

    fs::remove_all(root);
}

TEST_CASE("returns nullopt when metadata doesn't exist", "[unity_discovery]") {
    auto result = find_global_metadata("/nonexistent/path/Game.x64");
    REQUIRE_FALSE(result.has_value());
}
