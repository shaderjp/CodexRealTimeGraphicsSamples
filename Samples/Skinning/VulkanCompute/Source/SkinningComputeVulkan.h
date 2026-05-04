#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#ifndef VK_USE_PLATFORM_WIN32_KHR
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include <windows.h>

#include <vulkan/vulkan.h>
#include <DirectXMath.h>
#include <wincodec.h>

#include <array>
#include <cstdint>
#include <wrl.h>
#include <string>
#include <vector>
#include "..\..\Shared\SkinningData.h"

class SkinningComputeVulkan
{
public:
    static constexpr uint32_t TextureCount = 1;

    struct Vertex
    {
        float positionTexcoordX[4];
        float normalTexcoordY[4];
        uint32_t joints[4];
        float weights[4];
    };

    struct SkinnedVertex
    {
        float positionTexcoordX[4];
        float normalTexcoordY[4];
    };

    struct SceneConstants
    {
        DirectX::XMFLOAT4X4 model;
        DirectX::XMFLOAT4X4 modelViewProjection;
        DirectX::XMFLOAT4 lightDirection;
        DirectX::XMFLOAT4 lightColor;
    };

    struct JointConstants
    {
        DirectX::XMFLOAT4X4 joints[64];
    };

    struct NodePose
    {
        DirectX::XMFLOAT3 translation;
        DirectX::XMFLOAT4 rotation;
        DirectX::XMFLOAT3 scale;
    };

    SkinningComputeVulkan(uint32_t width, uint32_t height, const wchar_t* title);
    ~SkinningComputeVulkan();

    int Run(HINSTANCE instance, int showCommand);

private:
    struct QueueFamilyIndices
    {
        uint32_t graphicsFamily = UINT32_MAX;
        uint32_t presentFamily = UINT32_MAX;

        bool IsComplete() const;
    };

    struct SwapChainSupport
    {
        VkSurfaceCapabilitiesKHR capabilities{};
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR> presentModes;
    };

    static constexpr uint32_t MaxFramesInFlight = 2;
    static constexpr VkFormat DepthFormat = VK_FORMAT_D32_SFLOAT;
    static constexpr VkFormat TextureFormat = VK_FORMAT_R8G8B8A8_UNORM;

    struct ImageData
    {
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<uint8_t> pixels;
    };

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    void InitWindow(HINSTANCE instance, int showCommand);
    void InitVulkan();
    void MainLoop();
    void Render();
    void WaitIdle();
    void Cleanup();

    void CreateInstance();
    void CreateSurface();
    void PickPhysicalDevice();
    void CreateLogicalDevice();
    void CreateSwapChain();
    void CreateImageViews();
    void CreateRenderPass();
    void CreateDescriptorSetLayout();
    void CreateGraphicsPipeline();
    void CreateDepthResources();
    void CreateFramebuffers();
    void CreateCommandPool();
    void LoadModel();
    void CreateVertexBuffer();
    void CreateIndexBuffer();
    void CreateTextureImages();
    void CreateTextureSampler();
    void CreateUniformBuffer();
    void CreateDescriptorPool();
    void CreateDescriptorSet();
    void CreateCommandBuffers();
    void CreateSyncObjects();
    void UpdateAnimation();
    void UpdateUniformBuffer();
    DirectX::XMMATRIX GetLocalMatrix(uint32_t nodeIndex) const;

    bool IsDeviceSuitable(VkPhysicalDevice device) const;
    QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice device) const;
    SwapChainSupport QuerySwapChainSupport(VkPhysicalDevice device) const;
    VkSurfaceFormatKHR ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) const;
    VkPresentModeKHR ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& presentModes) const;
    VkExtent2D ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) const;
    uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;
    void CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& memory);
    void CreateImage(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage, VkImage& image, VkDeviceMemory& memory);
    VkImageView CreateImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags) const;
    VkCommandBuffer BeginSingleTimeCommands() const;
    void EndSingleTimeCommands(VkCommandBuffer commandBuffer) const;
    void CopyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) const;
    void TransitionImageLayout(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout) const;
    ImageData LoadTexture(const std::wstring& path) const;

    std::vector<char> ReadFile(const std::wstring& path) const;
    VkShaderModule CreateShaderModule(const std::vector<char>& code) const;
    std::wstring GetExecutableDirectory() const;
    void ThrowIfFailed(VkResult result, const char* message) const;

    uint32_t m_width;
    uint32_t m_height;
    std::wstring m_title;
    HWND m_hwnd = nullptr;
    HINSTANCE m_instanceHandle = nullptr;

    VkInstance m_instance = VK_NULL_HANDLE;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VkQueue m_graphicsQueue = VK_NULL_HANDLE;
    VkQueue m_presentQueue = VK_NULL_HANDLE;
    VkSwapchainKHR m_swapChain = VK_NULL_HANDLE;
    std::vector<VkImage> m_swapChainImages;
    std::vector<VkImageView> m_swapChainImageViews;
    VkFormat m_swapChainImageFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D m_swapChainExtent{};
    VkRenderPass m_renderPass = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_graphicsPipeline = VK_NULL_HANDLE;
    VkPipeline m_computePipeline = VK_NULL_HANDLE;
    VkImage m_depthImage = VK_NULL_HANDLE;
    VkDeviceMemory m_depthImageMemory = VK_NULL_HANDLE;
    VkImageView m_depthImageView = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> m_framebuffers;
    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> m_commandBuffers;
    VkBuffer m_sourceVertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_sourceVertexBufferMemory = VK_NULL_HANDLE;
    VkBuffer m_skinnedVertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_skinnedVertexBufferMemory = VK_NULL_HANDLE;
    VkBuffer m_indexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_indexBufferMemory = VK_NULL_HANDLE;
    std::vector<Vertex> m_vertices;
    std::vector<uint16_t> m_indices;
    std::array<VkImage, TextureCount> m_textureImages{};
    std::array<VkDeviceMemory, TextureCount> m_textureImageMemories{};
    std::array<VkImageView, TextureCount> m_textureImageViews{};
    VkSampler m_textureSampler = VK_NULL_HANDLE;
    VkBuffer m_sceneUniformBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_sceneUniformBufferMemory = VK_NULL_HANDLE;
    VkBuffer m_jointUniformBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_jointUniformBufferMemory = VK_NULL_HANDLE;
    JointConstants m_jointConstants{};
    std::array<DirectX::XMFLOAT4X4, SkinningData::JointCount> m_inverseBindMatrices;
    std::array<NodePose, SkinningData::NodeCount> m_nodePoses;
    std::array<DirectX::XMFLOAT4X4, SkinningData::NodeCount> m_globalNodeMatrices;
    std::vector<uint8_t> m_animationData;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;
    std::array<VkSemaphore, MaxFramesInFlight> m_imageAvailableSemaphores{};
    std::array<VkSemaphore, MaxFramesInFlight> m_renderFinishedSemaphores{};
    std::array<VkFence, MaxFramesInFlight> m_inFlightFences{};
    uint32_t m_currentFrame = 0;
    float m_animationTime = 0.0f;
};
