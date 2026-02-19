#include "vulkan/vkengine.h"
#include "3dgrt/grt-scene.h"
#include "log.h"

#include <filesystem>


constexpr const char* DEFAULT_PLY_PATH = DATA_DIR "/cactus.ply";


int main(int argc, char* argv[])
{
    Log::init();
    Log::banner();

    VkEngine engine;
    engine.initialize();

    std::filesystem::path plyPath;
    if (argc > 1)
    {
        plyPath = argv[1];
    }
    else
    {
        plyPath = DEFAULT_PLY_PATH;
    }

    // Start async scene loading (GUI renders immediately with progress bar)
    if (std::filesystem::exists(plyPath))
    {
        engine.getSceneManager().loadGRTScene(plyPath);
    }
    else
    {
        Log::ERR("Main") << "PLY file not found: " << plyPath.string();
    }

    Log::INFO("Main") << "Entering render loop (close window to exit)";
    engine.run();
    engine.cleanup();
    Log::OK("Main") << "Exited";

    return 0;
}
