#include "mesh-data.h"
#include "mesh-loader.h"

#include "log.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>

#include <cmath>
#include <limits>


namespace vk3dgrt {


static glm::vec3 computeLocalAABBCenter(const std::vector<glm::vec3>& vertices)
{
    if (vertices.empty())
    {
        return glm::vec3(0.0f);
    }

    glm::vec3 minV(std::numeric_limits<float>::max());
    glm::vec3 maxV(std::numeric_limits<float>::lowest());

    for (const auto& v : vertices)
    {
        minV = glm::min(minV, v);
        maxV = glm::max(maxV, v);
    }

    return (minV + maxV) * 0.5f;
}

glm::mat4 MeshTransform::toMatrix() const
{
    glm::mat4 T = glm::translate(glm::mat4(1.0f), position);

    glm::mat4 R = glm::eulerAngleXYZ(
        glm::radians(rotation.x),
        glm::radians(rotation.y),
        glm::radians(rotation.z)
    );

    glm::mat4 S = glm::scale(glm::mat4(1.0f), scale);

    return T * R * S;
}


MeshTransform MeshTransform::fromMatrix(const glm::mat4& mat)
{
    MeshTransform result;

    // Position: 4th column
    result.position = glm::vec3(mat[3]);

    // Scale: length of each column vector (first 3 columns)
    result.scale.x = glm::length(glm::vec3(mat[0]));
    result.scale.y = glm::length(glm::vec3(mat[1]));
    result.scale.z = glm::length(glm::vec3(mat[2]));

    // Rotation: extract from rotation matrix (columns normalized by scale)
    glm::vec3 safeScale = glm::max(result.scale, glm::vec3(0.0001f));

    glm::mat3 rotMat(
        glm::vec3(mat[0]) / safeScale.x,
        glm::vec3(mat[1]) / safeScale.y,
        glm::vec3(mat[2]) / safeScale.z
    );

    // Extract euler angles (XYZ order) from rotation matrix
    float sy = -rotMat[0][2];
    if (std::abs(sy) < 0.99999f)
    {
        result.rotation.x = glm::degrees(std::atan2(rotMat[1][2], rotMat[2][2]));
        result.rotation.y = glm::degrees(std::asin(sy));
        result.rotation.z = glm::degrees(std::atan2(rotMat[0][1], rotMat[0][0]));
    }
    else
    {
        // Gimbal lock
        result.rotation.x = glm::degrees(std::atan2(-rotMat[2][1], rotMat[1][1]));
        result.rotation.y = glm::degrees(std::asin(sy));
        result.rotation.z = 0.0f;
    }

    return result;
}


MeshInstance createPresetMesh(MeshPreset preset,
                              const glm::vec3& position,
                              const glm::vec3& scale)
{
    MeshInstance mesh;

    switch (preset)
    {
        case MeshPreset::TEAPOT:
        {
            mesh.name = "Teapot";

            std::string objPath = std::string(DATA_DIR) + "/teapot.obj";
            if (!loadMeshFromOBJ(objPath, mesh.vertices, mesh.normals, mesh.indices))
            {
                Log::ERR("MeshData") << "Failed to load teapot.obj";
                break;
            }

            mesh.localCenter = computeLocalAABBCenter(mesh.vertices);

            mesh.material.type         = MeshMaterialType::DIFFUSE;
            mesh.material.color        = glm::vec3(0.6f, 0.6f, 0.6f);
            mesh.material.reflectivity = 0.85f;

            // Base scale 0.3 for teapot model (original geometry is large)
            glm::vec3 effectiveScale = glm::vec3(0.3f) * scale;

            mesh.meshTransform.position = position;
            mesh.meshTransform.rotation = glm::vec3(0.0f);
            mesh.meshTransform.scale    = effectiveScale;
            mesh.transform              = mesh.meshTransform.toMatrix();
        }
    }

    return mesh;
}


MeshMaterialGPU toGPUMaterial(const MeshMaterial& mat)
{
    MeshMaterialGPU gpu{};
    gpu.color        = mat.color;
    gpu.materialType = static_cast<uint32_t>(mat.type);
    gpu.reflectivity = mat.reflectivity;
    gpu._pad[0]      = 0.0f;
    gpu._pad[1]      = 0.0f;
    gpu._pad[2]      = 0.0f;
    return gpu;
}

}   // namespace vk3dgrt
