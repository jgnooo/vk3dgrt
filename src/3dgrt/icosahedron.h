#ifndef ICOSAHEDRON_H
#define ICOSAHEDRON_H

#include <glm/glm.hpp>

#include <cmath>
#include <cstdint>
#include <vector>


namespace vk3dgrt {
    
class Icosahedron
{
public:
    Icosahedron()
    {
        // Regular icosahedron with unit inscribed sphere
        // Scaled so that the inscribed sphere has radius 1.0
        const float rr = (3.0f + std::sqrt(5.0f)) / (2.0f * std::sqrt(3.0f));
        const float ss = 1.0f / rr;
        const float tt = (1.0f + std::sqrt(5.0f)) / (2.0f * rr);

        vertices = {
            {-ss,  tt, 0.0f}, { ss,  tt, 0.0f}, {-ss, -tt, 0.0f}, { ss, -tt, 0.0f},
            {0.0f, -ss,  tt}, {0.0f,  ss,  tt}, {0.0f, -ss, -tt}, {0.0f,  ss, -tt},
            { tt, 0.0f, -ss}, { tt, 0.0f,  ss}, {-tt, 0.0f, -ss}, {-tt, 0.0f,  ss}
        };

        indices = {
             0, 11,  5,   0,  5,  1,   0,  1,  7,   0,  7, 10,   0, 10, 11,
             1,  5,  9,   5, 11,  4,  11, 10,  2,  10,  7,  6,   7,  1,  8,
             3,  9,  4,   3,  4,  2,   3,  2,  6,   3,  6,  8,   3,  8,  9,
             4,  9,  5,   2,  4, 11,   6,  2, 10,   8,  6,  7,   9,  8,  1
        };
    }

    ~Icosahedron() = default;

    // Disable copy, allow move
    Icosahedron(const Icosahedron&)            = default;
    Icosahedron& operator=(const Icosahedron&) = default;
    Icosahedron(Icosahedron&&)                 = default;
    Icosahedron& operator=(Icosahedron&&)      = default;

    size_t getVertexCount() const
    {
        return vertices.size();
    }

    size_t getIndexCount() const
    {
        return indices.size();
    }

    size_t getTriangleCount() const
    {
        return indices.size() / 3;
    }

    size_t getVertexDataSize() const
    {
        return vertices.size() * sizeof(glm::vec3);
    }

    size_t getIndexDataSize() const
    {
        return indices.size() * sizeof(uint32_t);
    }

    const glm::vec3* getVertexData() const
    {
        return vertices.data();
    }

    const uint32_t* getIndexData() const
    {
        return indices.data();
    }


    std::vector<glm::vec3> vertices;   // 12 vertices
    std::vector<uint32_t>  indices;    // 60 indices (20 triangles)
};

}   // namespace vk3dgrt

#endif // ICOSAHEDRON_H