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

    if (std::filesystem::exists(plyPath))
    {
        if (engine.getSceneManager().loadGRTScene(plyPath))
        {
            auto* gs = engine.getSceneManager().getGRTScene();
            if (gs)
            {
                Log::OK("Scene") << "Ready — "
                    << Log::formatCount(gs->getParticleCount()) << " gaussians";
            }
        }
        else
        {
            Log::ERR("Main") << "Failed to load scene, continuing without GRT rendering";
        }
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
