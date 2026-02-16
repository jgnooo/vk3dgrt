// Reference: nvpro-samples/vk_gaussian_splatting
// https://github.com/nvpro-samples/vk_gaussian_splatting

#include "mesh-loader.h"

#include "log.h"

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

#include <cmath>
#include <filesystem>
#include <unordered_map>

namespace vk3dgrt {

// Compute area-weighted smooth vertex normals from positions and indices.
// Each face contributes its (unnormalized) cross-product normal to all three
// vertices; the accumulated result is normalized per-vertex, producing a
// smooth surface appearance.
static void computeSmoothNormals(const std::vector<glm::vec3>& vertices,
                                 const std::vector<glm::uvec3>& indices,
                                 std::vector<glm::vec3>& outNormals)
{
    outNormals.assign(vertices.size(), glm::vec3(0.0f));

    for (const auto& tri : indices)
    {
        const glm::vec3& v0 = vertices[tri.x];
        const glm::vec3& v1 = vertices[tri.y];
        const glm::vec3& v2 = vertices[tri.z];

        glm::vec3 edge1  = v1 - v0;
        glm::vec3 edge2  = v2 - v0;
        glm::vec3 faceNormal = glm::cross(edge1, edge2);

        // The magnitude of the cross product is proportional to
        // the triangle area, so this naturally area-weights the
        // contribution.
        outNormals[tri.x] += faceNormal;
        outNormals[tri.y] += faceNormal;
        outNormals[tri.z] += faceNormal;
    }

    for (auto& n : outNormals)
    {
        float len = glm::length(n);
        if (len > 1e-8f)
        {
            n /= len;
        }
        else
        {
            n = glm::vec3(0.0f, 1.0f, 0.0f);
        }
    }
}


bool loadMeshFromOBJ(const std::string& filePath,
                     std::vector<glm::vec3>& outVertices,
                     std::vector<glm::vec3>& outNormals,
                     std::vector<glm::uvec3>& outIndices)
{
    outVertices.clear();
    outNormals.clear();
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

    bool hasNormals = !attrib.normals.empty();

    if (hasNormals)
    {
        // OBJ files may have different vertex/normal indices per face corner,
        // so we need to de-duplicate (vertex_index, normal_index) pairs into
        // a single vertex stream.
        struct PairHash
        {
            size_t operator()(const std::pair<int, int>& p) const
            {
                return std::hash<int>()(p.first) ^ (std::hash<int>()(p.second) << 16);
            }
        };

        std::unordered_map<std::pair<int, int>, uint32_t, PairHash> uniqueVertexMap;

        for (const auto& shape : shapes)
        {
            size_t indexOffset = 0;

            for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); ++f)
            {
                int faceVerts = shape.mesh.num_face_vertices[f];

                // Collect unique (vertex, normal) pairs for this face
                std::vector<uint32_t> faceIndices;
                faceIndices.reserve(faceVerts);

                for (int v = 0; v < faceVerts; ++v)
                {
                    const auto& idx = shape.mesh.indices[indexOffset + v];
                    auto key = std::make_pair(idx.vertex_index, idx.normal_index);

                    auto it = uniqueVertexMap.find(key);
                    if (it != uniqueVertexMap.end())
                    {
                        faceIndices.push_back(it->second);
                    }
                    else
                    {
                        uint32_t newIdx = static_cast<uint32_t>(outVertices.size());
                        uniqueVertexMap[key] = newIdx;

                        outVertices.push_back(glm::vec3(
                             attrib.vertices[idx.vertex_index * 3 + 0],
                             attrib.vertices[idx.vertex_index * 3 + 1],
                            -attrib.vertices[idx.vertex_index * 3 + 2]
                        ));

                        outNormals.push_back(glm::vec3(
                             attrib.normals[idx.normal_index * 3 + 0],
                             attrib.normals[idx.normal_index * 3 + 1],
                            -attrib.normals[idx.normal_index * 3 + 2]
                        ));

                        faceIndices.push_back(newIdx);
                    }
                }

                // Triangulate
                if (faceVerts == 3)
                {
                    outIndices.push_back(glm::uvec3(
                        faceIndices[0], faceIndices[1], faceIndices[2]
                    ));
                }
                else if (faceVerts > 3)
                {
                    for (int v = 1; v < faceVerts - 1; ++v)
                    {
                        outIndices.push_back(glm::uvec3(
                            faceIndices[0], faceIndices[v], faceIndices[v + 1]
                        ));
                    }
                }

                indexOffset += faceVerts;
            }
        }

        // Flip winding order (Z-axis flip)
        for (auto& tri : outIndices)
        {
            std::swap(tri.y, tri.z);
        }
    }
    else
    {
        // No normals in OBJ: use original position-only path, then compute
        // smooth normals from the mesh topology.
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

        computeSmoothNormals(outVertices, outIndices, outNormals);
    }

    Log::INFO("MeshLoader") << "Loaded " << filePath
        << " (" << outVertices.size() << " vertices, "
        << outIndices.size() << " triangles, "
        << (hasNormals ? "file normals" : "computed smooth normals") << ")";

    return true;
}

}   // namespace vk3dgrt
