#include "mesh-data.h"
#include "mesh-loader.h"

#include "log.h"

#include <glm/gtc/matrix_transform.hpp>


namespace vk3dgrt {

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
            if (!loadMeshFromOBJ(objPath, mesh.vertices, mesh.indices))
            {
                Log::ERR("MeshData") << "Failed to load teapot.obj";
                break;
            }

            mesh.material.type         = MeshMaterialType::REFLECTIVE;
            mesh.material.color        = glm::vec3(0.8f, 0.8f, 0.9f);
            mesh.material.reflectivity = 0.85f;

            mesh.transform = glm::translate(glm::mat4(1.0f), position)
                           * glm::scale(glm::mat4(1.0f), scale);
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