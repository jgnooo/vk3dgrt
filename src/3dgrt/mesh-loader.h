#ifndef MESH_LOADER_H
#define MESH_LOADER_H

#include <glm/glm.hpp>

#include <string>
#include <vector>


namespace vk3dgrt {

bool loadMeshFromOBJ(const std::string& filePath,
                     std::vector<glm::vec3>& outVertices,
                     std::vector<glm::uvec3>& outIndices);

}   // namespace vk3dgrt

#endif // MESH_LOADER_H