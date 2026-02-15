// Reference: nvpro-samples/vk_gaussian_splatting
// https://github.com/nvpro-samples/vk_gaussian_splatting

#include "mesh-loader.h"

#include "log.h"

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

#include <filesystem>

namespace vk3dgrt {

bool loadMeshFromOBJ(const std::string& filePath,
                     std::vector<glm::vec3>& outVertices,
                     std::vector<glm::uvec3>& outIndices)
{
    outVertices.clear();
    outIndices.clear();

    if (!std::filesystem::exists(filePath))
    {
        Log::ERR("MeshLoader") << "File not found: " << filePath;
        return false;
    }

    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn;
    std::string err;

    bool ret = tinyobj::LoadObj(&attrib, &shapes, &materials,
                                &warn, &err, filePath.c_str());

    if (!warn.empty())
    {
        Log::WARN("MeshLoader") << warn;
    }

    if (!err.empty())
    {
        Log::ERR("MeshLoader") << err;
        return false;
    }

    if (!ret)
    {
        Log::ERR("MeshLoader") << "Failed to load OBJ: " << filePath;
        return false;
    }

    uint32_t vertexCount = static_cast<uint32_t>(attrib.vertices.size() / 3);
    outVertices.resize(vertexCount);

    for (uint32_t i = 0; i < vertexCount; ++i)
    {
        outVertices[i] = glm::vec3(
             attrib.vertices[i * 3 + 0],
             attrib.vertices[i * 3 + 1],
            -attrib.vertices[i * 3 + 2]
        );
    }

    for (const auto& shape : shapes)
    {
        size_t indexOffset = 0;

        for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); ++f)
        {
            int faceVerts = shape.mesh.num_face_vertices[f];

            if (faceVerts == 3)
            {
                glm::uvec3 tri(
                    shape.mesh.indices[indexOffset + 0].vertex_index,
                    shape.mesh.indices[indexOffset + 1].vertex_index,
                    shape.mesh.indices[indexOffset + 2].vertex_index
                );
                outIndices.push_back(tri);
            }
            else if (faceVerts > 3)
            {
                uint32_t v0 = shape.mesh.indices[indexOffset].vertex_index;
                for (int v = 1; v < faceVerts - 1; ++v)
                {
                    glm::uvec3 tri(
                        v0,
                        shape.mesh.indices[indexOffset + v].vertex_index,
                        shape.mesh.indices[indexOffset + v + 1].vertex_index
                    );
                    outIndices.push_back(tri);
                }
            }

            indexOffset += faceVerts;
        }
    }

    for (auto& tri : outIndices)
    {
        std::swap(tri.y, tri.z);
    }

    Log::INFO("MeshLoader") << "Loaded " << filePath
        << " (" << outVertices.size() << " vertices, "
        << outIndices.size() << " triangles)";

    return true;
}

}   // namespace vk3dgrt