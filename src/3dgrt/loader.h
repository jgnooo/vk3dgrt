#ifndef LOADER_H
#define LOADER_H

#include "data.h"

#include <filesystem>
#include <string>


namespace vk3dgrt {

class Loader
{
public:
    Loader()  = default;
    ~Loader() = default;

    // Disable copy, allow move
    Loader(const Loader&)            = delete;
    Loader& operator=(const Loader&) = delete;
    Loader(Loader&&)                 = default;
    Loader& operator=(Loader&&)      = default;

    /**
     * PLY properties:
     * - x, y, z: position
     * - scale_0, scale_1, scale_2: scale (log space, converted via exp)
     * - rot_0, rot_1, rot_2, rot_3: quaternion (normalized)
     * - opacity: density (converted via sigmoid)
     * - f_dc_0, f_dc_1, f_dc_2: SH DC coefficients (optional)
     * - f_rest_*: SH higher order coefficients (optional)
     */
    bool loadPLY(const std::filesystem::path& filePath, GaussianParticleData& outData);

    const std::string& getLastError() const
    {
        return lastError;
    }

private:
    std::string lastError;

    // Data transformation helpers
    static float sigmoid(float x);
    static glm::vec4 normalizeQuaternion(float x, float y, float z, float w);
};

}   // namespace vk3dgrt

#endif // LOADER_H