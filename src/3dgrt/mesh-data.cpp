#include "mesh-data.h"
#include "mesh-loader.h"

#include "log.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>

#include <cmath>
#include <cstdint>
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


static void generatePlane(std::vector<glm::vec3>& vertices,
                          std::vector<glm::vec3>& normals,
                          std::vector<glm::uvec3>& indices,
                          uint32_t subdivisions = 10)
{
    const uint32_t vertsPerSide = subdivisions + 1;
    const float    step         = 1.0f / static_cast<float>(subdivisions);

    vertices.reserve(vertsPerSide * vertsPerSide);
    normals.reserve(vertsPerSide * vertsPerSide);

    // Generate vertices on XY plane (standing upright), centered at origin, Z=0
    for (uint32_t row = 0; row < vertsPerSide; ++row)
    {
        for (uint32_t col = 0; col < vertsPerSide; ++col)
        {
            float px = -0.5f + static_cast<float>(col) * step;
            float py = -0.5f + static_cast<float>(row) * step;
            vertices.emplace_back(px, py, 0.0f);
            normals.emplace_back(0.0f, 0.0f, 1.0f);
        }
    }

    // Generate triangle indices (two triangles per quad)
    indices.reserve(subdivisions * subdivisions * 2);
    for (uint32_t row = 0; row < subdivisions; ++row)
    {
        for (uint32_t col = 0; col < subdivisions; ++col)
        {
            uint32_t topLeft     = row * vertsPerSide + col;
            uint32_t topRight    = topLeft + 1;
            uint32_t bottomLeft  = (row + 1) * vertsPerSide + col;
            uint32_t bottomRight = bottomLeft + 1;

            indices.emplace_back(topLeft, bottomLeft, topRight);
            indices.emplace_back(topRight, bottomLeft, bottomRight);
        }
    }
}


static void generateSphere(std::vector<glm::vec3>& vertices,
                            std::vector<glm::vec3>& normals,
                            std::vector<glm::uvec3>& indices,
                            uint32_t stacks = 32,
                            uint32_t slices = 64)
{
    const float radius = 0.5f;

    vertices.reserve((stacks + 1) * (slices + 1));
    normals.reserve((stacks + 1) * (slices + 1));

    // Generate vertices using spherical coordinates
    for (uint32_t i = 0; i <= stacks; ++i)
    {
        float phi = glm::pi<float>() * static_cast<float>(i) / static_cast<float>(stacks);
        float y   = std::cos(phi);
        float r   = std::sin(phi);

        for (uint32_t j = 0; j <= slices; ++j)
        {
            float theta = 2.0f * glm::pi<float>() * static_cast<float>(j) / static_cast<float>(slices);
            float x     = r * std::cos(theta);
            float z     = r * std::sin(theta);

            glm::vec3 normal(x, y, z);
            vertices.push_back(normal * radius);
            normals.push_back(normal);
        }
    }

    // Generate triangle indices
    indices.reserve(stacks * slices * 2);
    for (uint32_t i = 0; i < stacks; ++i)
    {
        for (uint32_t j = 0; j < slices; ++j)
        {
            uint32_t current = i * (slices + 1) + j;
            uint32_t next    = current + slices + 1;

            if (i != 0)
            {
                indices.emplace_back(current, next, current + 1);
            }
            if (i != stacks - 1)
            {
                indices.emplace_back(current + 1, next, next + 1);
            }
        }
    }
}


MeshInstance createPresetMesh(MeshPreset preset,
                              const glm::vec3& position,
                              const glm::vec3& scale)
{
    MeshInstance mesh;

    switch (preset)
    {
        case MeshPreset::PLANE:
        {
            mesh.name = "Plane";

            generatePlane(mesh.vertices, mesh.normals, mesh.indices);

            mesh.localCenter = computeLocalAABBCenter(mesh.vertices);

            mesh.material.type         = MeshMaterialType::DIFFUSE;
            mesh.material.color        = glm::vec3(0.6f, 0.6f, 0.6f);
            mesh.material.reflectivity = 0.85f;

            mesh.meshTransform.position = position;
            mesh.meshTransform.rotation = glm::vec3(0.0f);
            mesh.meshTransform.scale    = scale;
            mesh.transform              = mesh.meshTransform.toMatrix();
            break;
        }

        case MeshPreset::SPHERE:
        {
            mesh.name = "Sphere";

            generateSphere(mesh.vertices, mesh.normals, mesh.indices);

            mesh.localCenter = computeLocalAABBCenter(mesh.vertices);

            mesh.material.type         = MeshMaterialType::DIFFUSE;
            mesh.material.color        = glm::vec3(0.6f, 0.6f, 0.6f);
            mesh.material.reflectivity = 0.85f;

            mesh.meshTransform.position = position;
            mesh.meshTransform.rotation = glm::vec3(0.0f);
            mesh.meshTransform.scale    = scale;
            mesh.transform              = mesh.meshTransform.toMatrix();
            break;
        }

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
            break;
        }
    }

    return mesh;
}


MeshMaterialGPU toGPUMaterial(const MeshMaterial& mat, uint32_t indexOffset)
{
    MeshMaterialGPU gpu{};
    gpu.color        = mat.color;
    gpu.materialType = static_cast<uint32_t>(mat.type);
    gpu.reflectivity = mat.reflectivity;
    gpu.indexOffset   = indexOffset;
    gpu._pad[0]      = 0.0f;
    gpu._pad[1]      = 0.0f;
    return gpu;
}

}   // namespace vk3dgrt
