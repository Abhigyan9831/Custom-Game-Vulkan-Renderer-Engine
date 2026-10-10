#pragma once
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include "engine/assets/MeshData.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace cge {

class Window;
class VulkanContext;
class VulkanDevice;
class Swapchain;

struct Vertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec3 color;
    glm::vec2 uv;

    static VkVertexInputBindingDescription bindingDescription();
    static std::array<VkVertexInputAttributeDescription, 4> attributeDescriptions();
};

struct Ubo {
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 proj;
};

struct GpuMesh {
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory indexMemory = VK_NULL_HANDLE;
    uint32_t indexCount = 0;
};

struct GpuTexture {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
};

class Renderer {
public:
    explicit Renderer(Window* window);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void drawFrame();

private:
    void createRenderPass();
    void createDescriptorSetLayout();
    void createTextureSetLayout();
    void createGraphicsPipeline();
    void createFramebuffers();
    void createCommandBuffers();
    void createSyncObjects();
    void uploadMeshes(const std::vector<MeshData>& meshes);
    void createUniformBuffers();
    void createDescriptorPoolAndSets();
    void loadTextures(const ModelData& model);
    void allocateTextureSets(const ModelData& model, int fallbackIndex);
    void createTexture(const TextureData& data, GpuTexture& out);
    void transitionImageLayout(VkImage image, VkImageLayout oldLayout,
                              VkImageLayout newLayout);
    void copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);
    void updateUniformBuffer(uint32_t currentFrame);
    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                      VkMemoryPropertyFlags properties,
                      VkBuffer& buffer, VkDeviceMemory& bufferMemory);
    void copyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size);
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;
    static std::vector<char> readFile(const std::string& path);

    static constexpr int MAX_FRAMES_IN_FLIGHT = 2;

    std::unique_ptr<VulkanContext> m_context;
    std::unique_ptr<VulkanDevice> m_device;
    std::unique_ptr<Swapchain> m_swapchain;

    VkRenderPass m_renderPass = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_textureSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> m_framebuffers;
    std::vector<VkCommandBuffer> m_commandBuffers;

    std::vector<GpuMesh> m_meshes;
    std::vector<GpuTexture> m_textures;
    std::vector<VkDescriptorSet> m_textureSets;

    std::array<VkBuffer, MAX_FRAMES_IN_FLIGHT> m_uniformBuffers{};
    std::array<VkDeviceMemory, MAX_FRAMES_IN_FLIGHT> m_uniformMemories{};
    std::array<void*, MAX_FRAMES_IN_FLIGHT> m_uniformMapped{};

    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> m_descriptorSets{};

    std::vector<VkSemaphore> m_imageAvailable;
    std::vector<VkSemaphore> m_renderFinished;
    std::vector<VkFence> m_inFlight;

    uint32_t m_currentFrame = 0;
    std::chrono::steady_clock::time_point m_startTime;
};

} // namespace cge