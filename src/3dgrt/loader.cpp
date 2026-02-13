#include "loader.h"

#define TINYPLY_IMPLEMENTATION
#include <tinyply.h>

#include <glm/glm.hpp>

#include <cmath>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>


namespace vk3dgrt {

// --------------------------------------------------- //
//  Helper functions
// --------------------------------------------------- //

float Loader::sigmoid(float x)
{
    return 1.0f / (1.0f + std::exp(-x));
}


glm::vec4 Loader::normalizeQuaternion(float x, float y, float z, float w)
{
    float length = std::sqrt(w * w + x * x + y * y + z * z);
    if (length < 1e-8f)
    {
        // Return identity quaternion if length is too small
        return glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
    }
    float invLength = 1.0f / length;
    // Return in (W, X, Y, Z) order
    return glm::vec4(w * invLength, x * invLength, y * invLength, z * invLength);
}


// --------------------------------------------------- //
//  GaussianLoader Implementation
// --------------------------------------------------- //

bool Loader::loadPLY(const std::filesystem::path& filePath, GaussianData& outData)
{
    outData.clear();
    lastError.clear();

    if (!std::filesystem::exists(filePath))
    {
        lastError = "[GaussianLoader] File not found: " + filePath.string();
        return false;
    }

    std::ifstream fileStream(filePath, std::ios::binary);
    if (!fileStream.is_open())
    {
        lastError = "[GaussianLoader] Failed to open file: " + filePath.string();
        return false;
    }

    try
    {
        tinyply::PlyFile plyFile;

        if (!plyFile.parse_header(fileStream))
        {
            lastError = "[GaussianLoader] Failed to parse PLY header";
            return false;
        }

        std::cout << "[GaussianLoader] Loading: " << filePath.filename().string() << std::endl;
        std::cout << "[GaussianLoader] Format: " << (plyFile.is_binary_file() ? "binary" : "ascii") << std::endl;

        std::shared_ptr<tinyply::PlyData> positions;
        std::shared_ptr<tinyply::PlyData> scales;
        std::shared_ptr<tinyply::PlyData> rotations;
        std::shared_ptr<tinyply::PlyData> opacities;
        std::shared_ptr<tinyply::PlyData> shDC;

        try
        {
            positions = plyFile.request_properties_from_element("vertex", {"x", "y", "z"});
        }
        catch (const std::exception& e)
        {
            lastError = "[GaussianLoader] Missing required properties (x, y, z): " + std::string(e.what());
            return false;
        }

        try
        {
            scales = plyFile.request_properties_from_element("vertex", {"scale_0", "scale_1", "scale_2"});
        }
        catch (const std::exception&)
        {
            std::cout << "[GaussianLoader] Warning: scale properties not found, using default" << std::endl;
        }

        try
        {
            rotations = plyFile.request_properties_from_element("vertex", {"rot_0", "rot_1", "rot_2", "rot_3"});
        }
        catch (const std::exception&)
        {
            std::cout << "[GaussianLoader] Warning: rotation properties not found, using identity" << std::endl;
        }

        try
        {
            opacities = plyFile.request_properties_from_element("vertex", {"opacity"});
        }
        catch (const std::exception&)
        {
            std::cout << "[GaussianLoader] Warning: opacity property not found, using default" << std::endl;
        }

        try
        {
            shDC = plyFile.request_properties_from_element("vertex", {"f_dc_0", "f_dc_1", "f_dc_2"});
        }
        catch (const std::exception&)
        {
            // SH coefficients are optional
        }

        // SH rest coefficients (optional, for degree 1-3)
        // 3DGS stores: f_rest_0 to f_rest_44 (15 coeffs × 3 channels = 45 floats)
        std::vector<std::shared_ptr<tinyply::PlyData>> shRestChannels;
        bool hasFullSH = true;

        // Try to load all 45 rest coefficients
        for (int i = 0; i < 45; ++i)
        {
            try
            {
                std::string propName = "f_rest_" + std::to_string(i);
                auto shRest = plyFile.request_properties_from_element("vertex", {propName});
                shRestChannels.push_back(shRest);
            }
            catch (const std::exception&)
            {
                hasFullSH = false;
                break;
            }
        }

        if (hasFullSH && shRestChannels.size() == 45)
        {
            std::cout << "[GaussianLoader] Found full SH coefficients (degree 3)" << std::endl;
        }
        else
        {
            shRestChannels.clear();
            hasFullSH = false;
        }

        plyFile.read(fileStream);

        const size_t particleCount = positions->count;
        std::cout << "[GaussianLoader] Particle count: " << particleCount << std::endl;

        if (particleCount == 0)
        {
            lastError = "[GaussianLoader] No particles found in file";
            return false;
        }

        outData.particles.resize(particleCount);

        const float* posData     = reinterpret_cast<const float*>(positions->buffer.get());
        const float* scaleData   = scales ? reinterpret_cast<const float*>(scales->buffer.get()) : nullptr;
        const float* rotData     = rotations ? reinterpret_cast<const float*>(rotations->buffer.get()) : nullptr;
        const float* opacityData = opacities ? reinterpret_cast<const float*>(opacities->buffer.get()) : nullptr;
        const float* shDCData    = shDC ? reinterpret_cast<const float*>(shDC->buffer.get()) : nullptr;

        for (size_t i = 0; i < particleCount; ++i)
        {
            GaussianParticle& particle = outData.particles[i];

            // Position: RDF (COLMAP) -> RUB conversion (negate Y, Z)
            particle.position = glm::vec3(
                posData[i * 3 + 0],
                -posData[i * 3 + 1],
                -posData[i * 3 + 2]
            );

            if (scaleData)
            {
                particle.scale = glm::vec3(
                    std::exp(scaleData[i * 3 + 0]),
                    std::exp(scaleData[i * 3 + 1]),
                    std::exp(scaleData[i * 3 + 2])
                );
            }
            else
            {
                particle.scale = glm::vec3(0.01f);
            }

            // Rotation quaternion (normalize + RDF->RUB conversion)
            // RDF->RUB: negate Y and Z components of quaternion
            // This is equivalent to R' = F * R * F where F = diag(1, -1, -1)
            if (rotData)
            {
                particle.quaternion = normalizeQuaternion(
                    rotData[i * 4 + 1],   // X (unchanged)
                    -rotData[i * 4 + 2],  // -Y
                    -rotData[i * 4 + 3],  // -Z
                    rotData[i * 4 + 0]    // W (unchanged)
                );
            }
            else
            {
                particle.quaternion = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);  // Identity
            }

            if (opacityData)
            {
                particle.opacity = sigmoid(opacityData[i]);
            }
            else
            {
                particle.opacity = 1.0f;
            }

            // Padding
            particle.padding = 0.0f;
        }

        if (shDCData)
        {
            outData.shCoeffsDC.resize(particleCount);
            for (size_t i = 0; i < particleCount; ++i)
            {
                outData.shCoeffsDC[i] = glm::vec3(
                    shDCData[i * 3 + 0],
                    shDCData[i * 3 + 1],
                    shDCData[i * 3 + 2]
                );
            }

            // Pack full SH coefficients (16 vec3 = 48 floats per particle)
            // RDF->RUB: negate SH coefficients whose basis functions change sign
            // under (y -> -y, z -> -z) transformation.
            // Indices to negate: 1, 2, 4, 7, 9, 11, 12, 14
            if (hasFullSH && shRestChannels.size() == 45)
            {
                outData.shDegree = 3;
                outData.shCoeffsFull.resize(particleCount * 48);  // 16 coeffs × 3 channels

                // SH coefficients that need sign flip for RDF->RUB
                // l=1: sh[1](-y), sh[2](z)
                // l=2: sh[4](xy), sh[7](xz)
                // l=3: sh[9](y(3x²-y²)), sh[11](y(4z²-x²-y²)),
                //       sh[12](z(2z²-3x²-3y²)), sh[14](z(x²-y²))
                constexpr bool shFlip[16] = {
                    false, true,  true,  false,   // 0-3
                    true,  false, false, true,    // 4-7
                    false, true,  false, true,    // 8-11
                    true,  false, true,  false    // 12-15
                };

                for (size_t i = 0; i < particleCount; ++i)
                {
                    size_t baseIdx = i * 48;

                    // SH coefficient 0 (DC) - no flip
                    outData.shCoeffsFull[baseIdx + 0] = shDCData[i * 3 + 0];
                    outData.shCoeffsFull[baseIdx + 1] = shDCData[i * 3 + 1];
                    outData.shCoeffsFull[baseIdx + 2] = shDCData[i * 3 + 2];

                    // SH coefficients 1-15 (from f_rest)
                    for (int shIdx = 1; shIdx < 16; ++shIdx)
                    {
                        int restIdx = shIdx - 1;
                        const float* restR = reinterpret_cast<const float*>(shRestChannels[restIdx]->buffer.get());
                        const float* restG = reinterpret_cast<const float*>(shRestChannels[restIdx + 15]->buffer.get());
                        const float* restB = reinterpret_cast<const float*>(shRestChannels[restIdx + 30]->buffer.get());

                        float sign = shFlip[shIdx] ? -1.0f : 1.0f;
                        outData.shCoeffsFull[baseIdx + shIdx * 3 + 0] = restR[i] * sign;
                        outData.shCoeffsFull[baseIdx + shIdx * 3 + 1] = restG[i] * sign;
                        outData.shCoeffsFull[baseIdx + shIdx * 3 + 2] = restB[i] * sign;
                    }
                }
            }
            else
            {
                // DC only - pack into shCoeffsFull for consistent buffer layout
                outData.shDegree = 0;
                outData.shCoeffsFull.resize(particleCount * 48, 0.0f);  // Zero-initialize

                for (size_t i = 0; i < particleCount; ++i)
                {
                    size_t baseIdx = i * 48;
                    outData.shCoeffsFull[baseIdx + 0] = shDCData[i * 3 + 0];
                    outData.shCoeffsFull[baseIdx + 1] = shDCData[i * 3 + 1];
                    outData.shCoeffsFull[baseIdx + 2] = shDCData[i * 3 + 2];
                }
            }
        }

        std::cout << "[GaussianLoader] Successfully loaded " << particleCount << " particles" << std::endl;
        std::cout << "[GaussianLoader] Data size: " << (outData.getDataSizeBytes() / 1024.0 / 1024.0) << " MB" << std::endl;
        
        return true;
    }
    catch (const std::exception& e)
    {
        lastError = "[GaussianLoader] Exception during loading: " + std::string(e.what());
        return false;
    }
}

}   // namespace vk3dgrt