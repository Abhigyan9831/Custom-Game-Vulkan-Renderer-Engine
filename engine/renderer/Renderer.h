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
    glm::vec4 lightDir;
    glm::vec4 cameraPos;
    glm::vec4 sunColor;             
    glm::vec4 pointLights[8];       
    glm::vec4 pointLightCount;
    glm::mat4 lightSpaceMatrix;
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
    void panCamera(float dx, float dy);
    void rotateCamera(float dYaw, float dPitch);
    void zoomCamera(float dDist);

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
    void allocateTextureSets(const ModelData& model, int whiteIdx, int mrIdx, int normalIdx);
    void createTexture(const TextureData& data, GpuTexture& out);
    void transitionImageLayout(VkImage image, VkImageLayout oldLayout,
                               VkImageLayout newLayout);
    void copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);
    void updateUniformBuffer(uint32_t currentFrame);
    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                      VkMemoryPropertyFlags properties,
                      VkBuffer& buffer, VkDeviceMemory& bufferMemory);
    void copyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size);
    void createDepthResources();
    void createShadowResources();
    void recordShadowPass(VkCommandBuffer cmd);
    [[nodiscard]] VkFormat findDepthFormat() const;
    [[nodiscard]] glm::mat4 computeLightSpaceMatrix() const;

    
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
    VkRenderPass m_shadowRenderPass = VK_NULL_HANDLE;
    VkFramebuffer m_shadowFramebuffer = VK_NULL_HANDLE;
    VkPipeline m_shadowPipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_shadowPipelineLayout = VK_NULL_HANDLE;
    VkImage m_shadowImage = VK_NULL_HANDLE;
    VkDeviceMemory m_shadowMemory = VK_NULL_HANDLE;
    VkImageView m_shadowView = VK_NULL_HANDLE;
    VkSampler m_shadowSampler = VK_NULL_HANDLE;
    glm::mat4 m_lightSpaceMatrix = glm::mat4(1.0f);
    VkBuffer m_shadowUboBuffer[MAX_FRAMES_IN_FLIGHT]{};
    VkDeviceMemory m_shadowUboMemory[MAX_FRAMES_IN_FLIGHT]{};
    void* m_shadowUboMapped[MAX_FRAMES_IN_FLIGHT]{};
    std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> m_shadowDescSets{};
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

    std::vector<VkImage> m_depthImages;
    std::vector<VkDeviceMemory> m_depthMemories;
    std::vector<VkImageView> m_depthViews;

    uint32_t m_currentFrame = 0;
    std::chrono::steady_clock::time_point m_startTime;

    // Continuous Skeletal Animation Cycle (Idle -> Breathing -> Skill_Roar)
    int m_mutantMeshIdx = -1;
    VkBuffer m_mutantDynVb[MAX_FRAMES_IN_FLIGHT]{};
    VkDeviceMemory m_mutantDynMem[MAX_FRAMES_IN_FLIGHT]{};
    void* m_mutantMapped[MAX_FRAMES_IN_FLIGHT]{};
    std::vector<std::vector<glm::vec3>> m_bakedPositions;
    std::vector<std::vector<glm::vec3>> m_bakedNormals;
    std::vector<Vertex> m_mutantBaseVertices;
    float m_animTime = 0.0f;
    float m_animDuration = 14.10f;
    float m_animFps = 10.0f;
    int m_animTotalFrames = 0;
    float m_lastTime = 0.0f;

    // Camera control (DOTA 2 style RMB drag & zoom)
    glm::vec3 m_camTarget{0.0f, 0.8f, -1.3f};
    float m_camDistance = 6.0f;
    float m_camPitch = 40.0f; // degrees down
    float m_camYaw = 0.0f;    // degrees
};

} // namespace cge