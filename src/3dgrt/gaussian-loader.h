#ifndef GAUSSIAN_LOADER_H
#define GAUSSIAN_LOADER_H

#include "data.h"

#include <filesystem>
#include <string>


namespace vk3dgrt {

class GaussianLoader
{
public:
    GaussianLoader()  = default;
    ~GaussianLoader() = default;

    // Disable copy, allow move
    GaussianLoader(const GaussianLoader&)            = delete;
    GaussianLoader& operator=(const GaussianLoader&) = delete;
    GaussianLoader(GaussianLoader&&)                 = default;
    GaussianLoader& operator=(GaussianLoader&&)      = default;

    /**
     * PLY properties:
     * - x, y, z: position
     * - scale_0, scale_1, scale_2: scale (log space, converted via exp)
     * - rot_0, rot_1, rot_2, rot_3: quaternion (normalized)
     * - opacity: density (converted via sigmoid)
     * - f_dc_0, f_dc_1, f_dc_2: SH DC coefficients (optional)
     * - f_rest_*: SH higher order coefficients (optional)
     */
    bool loadPLY(const std::filesystem::path& filePath, GaussianData& outData);

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

#endif // GAUSSIAN_LOADER_H