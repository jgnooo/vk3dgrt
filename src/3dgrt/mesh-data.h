#ifndef MESH_DATA_H
#define MESH_DATA_H

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <string>
#include <vector>


namespace vk3dgrt {


struct MeshTransform
{
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 rotation = glm::vec3(0.0f);   // Euler angles in degrees (XYZ order)
    glm::vec3 scale    = glm::vec3(1.0f);

    glm::mat4 toMatrix() const;
    static MeshTransform fromMatrix(const glm::mat4& mat);
};


enum class MeshMaterialType : uint32_t
{
    DIFFUSE    = 0,
    REFLECTIVE = 1,
};


struct MeshMaterial
{
    glm::vec3        color        = glm::vec3(0.8f);
    MeshMaterialType type         = MeshMaterialType::DIFFUSE;
    float            reflectivity = 0.8f;
};


struct MeshMaterialGPU
{
    glm::vec3 color;          // 12B
    uint32_t  materialType;   //  4B  (0=diffuse, 1=reflective)
    float     reflectivity;   //  4B
    float     _pad[3];        // 12B
};


static_assert(sizeof(MeshMaterialGPU) == 32, "MeshMaterialGPU must be 32 bytes");


enum class MeshPreset
{
    // TODO: Add more presets (e.g., cube, plane, sphere)
    // PLANE,
    // SPHERE,
    // CUBE,
    TEAPOT,
};


struct MeshInstance
{
    std::string             name;
    std::vector<glm::vec3>  vertices;
    std::vector<glm::vec3>  normals;
    std::vector<glm::uvec3> indices;
    MeshMaterial            material;
    glm::mat4               transform     = glm::mat4(1.0f);
    MeshTransform           meshTransform;
    glm::vec3               localCenter   = glm::vec3(0.0f);   // AABB center in local space

    uint32_t getVertexCount() const { return static_cast<uint32_t>(vertices.size()); }
    uint32_t getTriangleCount() const { return static_cast<uint32_t>(indices.size()); }

    glm::vec3 getWorldCenter() const
    {
        return glm::vec3(transform * glm::vec4(localCenter, 1.0f));
    }
};


MeshInstance createPresetMesh(MeshPreset preset,
                              const glm::vec3& position = glm::vec3(0.0f),
                              const glm::vec3& scale    = glm::vec3(1.0f));


MeshMaterialGPU toGPUMaterial(const MeshMaterial& mat);

}   // namespace vk3dgrt

#endif // MESH_DATA_H
