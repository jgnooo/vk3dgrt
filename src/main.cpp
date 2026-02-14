#include "vulkan/vkengine.h"
#include "3dgrt/grt-scene.h"

#include <filesystem>
#include <iostream>


constexpr const char* DEFAULT_PLY_PATH = DATA_DIR "/cactus.ply";


int main(int argc, char* argv[])
{
    VkEngine engine;

    std::filesystem::path plyPath;
    if (argc > 1)
    {
        plyPath = argv[1];
    }
    else
    {
        plyPath = DEFAULT_PLY_PATH;
    }

    if (std::filesystem::exists(plyPath))
    {
        if (engine.getSceneManager().loadGRTScene(plyPath))
        {
        }
        else
        {
            std::cerr << "[Main] Failed to load scene, continuing without GRT rendering" << std::endl;
        }
    }
    else
    {
        std::cerr << "[Main] PLY file not found: " << plyPath.string() << std::endl;
    }

    engine.run();
    engine.cleanup();

    return 0;
}