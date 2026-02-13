#ifndef BUFFERS_H
#define BUFFERS_H

#include "data.h"
#include "icosahedron.h"

#include "vulkan/vkbuffer.h"
#include "vulkan/vkcontext.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <vector>


namespace vk3dgrt {

class GaussianParticleBuffers
{
    bool     initialized   = false;
    uint32_t particleCount = 0;
    bool     hasSH         = false;
    uint32_t shDegree      = 0;

    // SoA particle GPU Buffers
    AllocatedBuffer positionBuffer;           // vec3 per particle (positions)
    AllocatedBuffer colorBuffer;              // vec4 per particle (DC color RGB + opacity)
    AllocatedBuffer quaternionBuffer;         // vec4 per particle (quaternions)
    AllocatedBuffer scaleBuffer;              // vec3 per particle (scales)
    AllocatedBuffer shBuffer;                 // SH coefficients storage buffer
    AllocatedBuffer icosahedronVertexBuffer;  // Shared vertex buffer
    AllocatedBuffer icosahedronIndexBuffer;   // Shared index buffer

    // Icosahedron mesh (CPU-side data for reference)
    Icosahedron icosahedron;

public:
    GaussianParticleBuffers()  = default;
    ~GaussianParticleBuffers() = default;

    // Disable copy, allow move
    GaussianParticleBuffers(const GaussianParticleBuffers&)            = delete;
    GaussianParticleBuffers& operator=(const GaussianParticleBuffers&) = delete;
    GaussianParticleBuffers(GaussianParticleBuffers&&)                 = default;
    GaussianParticleBuffers& operator=(GaussianParticleBuffers&&)      = default;

    bool initialize(VkContext* context,
                    const GaussianData& data,
                    VkQueue transferQueue,
                    VkCommandPool transferCommandPool);

    void cleanup(VmaAllocator allocator);

    bool isInitialized() const { return initialized; }

    uint32_t getParticleCount() const { return particleCount; }

    AllocatedBuffer& getPositionBuffer() { return positionBuffer; }
    const AllocatedBuffer& getPositionBuffer() const { return positionBuffer; }

    AllocatedBuffer& getColorBuffer() { return colorBuffer; }
    const AllocatedBuffer& getColorBuffer() const { return colorBuffer; }

    AllocatedBuffer& getQuaternionBuffer() { return quaternionBuffer; }
    const AllocatedBuffer& getQuaternionBuffer() const { return quaternionBuffer; }

    AllocatedBuffer& getScaleBuffer() { return scaleBuffer; }
    const AllocatedBuffer& getScaleBuffer() const { return scaleBuffer; }

    AllocatedBuffer& getSHBuffer() { return shBuffer; }
    const AllocatedBuffer& getSHBuffer() const { return shBuffer; }

    AllocatedBuffer& getIcosahedronVertexBuffer() { return icosahedronVertexBuffer; }
    const AllocatedBuffer& getIcosahedronVertexBuffer() const { return icosahedronVertexBuffer; }

    AllocatedBuffer& getIcosahedronIndexBuffer() { return icosahedronIndexBuffer; }
    const AllocatedBuffer& getIcosahedronIndexBuffer() const { return icosahedronIndexBuffer; }

    VkDeviceAddress getPositionBufferAddress() const { return positionBuffer.deviceAddress; }
    VkDeviceAddress getColorBufferAddress() const { return colorBuffer.deviceAddress; }
    VkDeviceAddress getQuaternionBufferAddress() const { return quaternionBuffer.deviceAddress; }
    VkDeviceAddress getScaleBufferAddress() const { return scaleBuffer.deviceAddress; }
    VkDeviceAddress getSHBufferAddress() const { return shBuffer.deviceAddress; }
    VkDeviceAddress getVertexBufferAddress() const { return icosahedronVertexBuffer.deviceAddress; }
    VkDeviceAddress getIndexBufferAddress() const { return icosahedronIndexBuffer.deviceAddress; }

    uint32_t getIcosahedronVertexCount() const { return static_cast<uint32_t>(icosahedron.getVertexCount()); }
    uint32_t getIcosahedronIndexCount() const { return static_cast<uint32_t>(icosahedron.getIndexCount()); }
    uint32_t getIcosahedronTriangleCount() const { return static_cast<uint32_t>(icosahedron.getTriangleCount()); }

    bool hasSHCoefficients() const { return hasSH; }
    uint32_t getSHDegree() const { return shDegree; }

private:
    bool uploadParticleData(VkContext* context,
                            const GaussianData& data,
                            VkQueue transferQueue,
                            VkCommandPool commandPool);

    bool uploadSHData(VkContext* context,
                      const GaussianData& data,
                      VkQueue transferQueue,
                      VkCommandPool commandPool);

    bool uploadIcosahedronData(VkContext* context,
                               VkQueue transferQueue,
                               VkCommandPool commandPool);

    // Staging buffer upload utility
    bool stageAndUpload(VkContext* context,
                        AllocatedBuffer& dstBuffer,
                        const void* data,
                        VkDeviceSize dataSize,
                        VkQueue transferQueue,
                        VkCommandPool commandPool);
};

}   // namespace vk3dgrt

#endif // BUFFERS_H