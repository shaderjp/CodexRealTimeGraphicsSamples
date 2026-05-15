#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#ifndef VK_USE_PLATFORM_WIN32_KHR
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include <windows.h>

#include <vulkan/vulkan.h>
#include <DirectXMath.h>

#include "..\..\Common\BistroCamera.h"
#include "..\..\Common\BistroScene.h"
#include "..\..\Common\BistroTexture.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

class BistroExteriorClusteredForwardVulkan
{
public:
    BistroExteriorClusteredForwardVulkan(uint32_t width, uint32_t height, const wchar_t* title);
    ~BistroExteriorClusteredForwardVulkan();

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

    struct SceneConstants
    {
        DirectX::XMFLOAT4X4 viewProjection;
        DirectX::XMFLOAT4X4 view;
        DirectX::XMFLOAT4X4 projection;
        DirectX::XMFLOAT4X4 lightViewProjection;
        DirectX::XMFLOAT4 cameraPosition;
        DirectX::XMFLOAT4 lightDirection;
        DirectX::XMFLOAT4 lightColor;
        DirectX::XMFLOAT4 debugOptions;
        DirectX::XMFLOAT4 localLightOptions;
        DirectX::XMFLOAT4 clusterOptions;
        DirectX::XMFLOAT4 clusterOptions2;
        DirectX::XMFLOAT4 clusterOptions3;
        DirectX::XMFLOAT4 shadowOptions;
    };

    struct MaterialConstants
    {
        DirectX::XMFLOAT4 baseColorFactor;
        DirectX::XMFLOAT4 emissiveFactor;
        DirectX::XMFLOAT4 options;
    };

    struct GpuTexture
    {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
        uint32_t mipLevels = 1;
        std::wstring path;
        uint32_t width = 1;
        uint32_t height = 1;
        bool fallback = false;
    };

    static constexpr uint32_t MaxFramesInFlight = 2;
    static constexpr VkFormat DepthFormat = VK_FORMAT_D32_SFLOAT;
    static constexpr uint32_t ClusterTileSize = 16;
    static constexpr uint32_t MaxLightsPerCluster = 128;
    static constexpr uint32_t MaxClusterZSlices = 48;
    static constexpr uint32_t DefaultActiveLightCount = 512;

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
    void CreateShadowRenderPass();
    void CreateDescriptorSetLayout();
    void CreateGraphicsPipeline();
    void CreateShadowPipeline();
    void CreateComputePipeline();
    void CreateDepthResources();
    void CreateShadowResources();
    void DestroyShadowResources();
    void RecreateShadowResources();
    void CreateFramebuffers();
    void CreateCommandPool();
    void LoadModel();
    void CreateVertexBuffer();
    void CreateIndexBuffer();
    void CreateTextureImages();
    void CreateTextureSampler();
    void CreateUniformBuffer();
    void CreateLightBuffer();
    void CreateClusterBuffers();
    void CreateDescriptorPool();
    void CreateDescriptorSets();
    void UpdateShadowDescriptorSets();
    void InitializeImGui();
    void ShutdownImGui();
    void BuildUI();
    void BuildRendererStatsUI();
    void SetOutputResolution(uint32_t width, uint32_t height);
    void RecreateSwapChain();
    void ResetLight();
    void ResetCameraView();
    void ResetCameraSpeeds();
    void ResetShadowSettings();
    void CreateCommandBuffers();
    void RecordCommandBuffer(uint32_t imageIndex);
    void RecordClusterBuild(VkCommandBuffer commandBuffer);
    void CreateSyncObjects();
    void UpdateUniformBuffer();
    void ReadbackClusterStats();
    uint32_t CreateTextureResource(const std::wstring& path, bool srgb, const uint8_t fallback[4], std::map<std::wstring, uint32_t>& cache);

    bool IsDeviceSuitable(VkPhysicalDevice device) const;
    QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice device) const;
    SwapChainSupport QuerySwapChainSupport(VkPhysicalDevice device) const;
    VkSurfaceFormatKHR ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) const;
    VkPresentModeKHR ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& presentModes) const;
    VkExtent2D ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) const;
    uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;
    void CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& memory);
    void CreateImage(uint32_t width, uint32_t height, uint32_t mipLevels, VkFormat format, VkImageUsageFlags usage, VkImage& image, VkDeviceMemory& memory);
    VkImageView CreateImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, uint32_t mipLevels) const;
    VkCommandBuffer BeginSingleTimeCommands() const;
    void EndSingleTimeCommands(VkCommandBuffer commandBuffer) const;
    void CopyBufferToImage(VkBuffer buffer, VkImage image, const Bistro::TextureData& texture) const;
    void TransitionImageLayout(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels) const;

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
    VkRenderPass m_shadowRenderPass = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_graphicsPipeline = VK_NULL_HANDLE;
    VkPipeline m_shadowPipeline = VK_NULL_HANDLE;
    VkPipeline m_clusterResetPipeline = VK_NULL_HANDLE;
    VkPipeline m_clusterBuildPipeline = VK_NULL_HANDLE;
    VkImage m_depthImage = VK_NULL_HANDLE;
    VkDeviceMemory m_depthImageMemory = VK_NULL_HANDLE;
    VkImageView m_depthImageView = VK_NULL_HANDLE;
    VkImage m_shadowDepthImage = VK_NULL_HANDLE;
    VkDeviceMemory m_shadowDepthImageMemory = VK_NULL_HANDLE;
    VkImageView m_shadowDepthImageView = VK_NULL_HANDLE;
    VkFramebuffer m_shadowFramebuffer = VK_NULL_HANDLE;
    VkSampler m_shadowSampler = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> m_framebuffers;
    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> m_commandBuffers;
    VkBuffer m_vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_vertexBufferMemory = VK_NULL_HANDLE;
    VkBuffer m_indexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_indexBufferMemory = VK_NULL_HANDLE;
    VkBuffer m_sceneUniformBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_sceneUniformBufferMemory = VK_NULL_HANDLE;
    VkBuffer m_materialUniformBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_materialUniformBufferMemory = VK_NULL_HANDLE;
    VkBuffer m_lightBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_lightBufferMemory = VK_NULL_HANDLE;
    VkBuffer m_clusterRecordsBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_clusterRecordsBufferMemory = VK_NULL_HANDLE;
    VkBuffer m_clusterLightIndicesBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_clusterLightIndicesBufferMemory = VK_NULL_HANDLE;
    VkBuffer m_clusterStatsBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_clusterStatsBufferMemory = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> m_descriptorSets;

    Bistro::Scene m_scene;
    Bistro::LightBuildResult m_lightBuild;
    std::vector<GpuTexture> m_textures;
    std::vector<std::array<uint32_t, Bistro::TextureSlotCount>> m_materialTextureIndices;
    VkSampler m_textureSampler = VK_NULL_HANDLE;
    uint32_t m_materialUniformStride = 0;
    Bistro::FpsCamera m_camera;
    DirectX::XMFLOAT3 m_defaultCameraPosition = DirectX::XMFLOAT3(-16.32f, 4.66f, -10.41f);
    float m_defaultCameraYaw = DirectX::XMConvertToRadians(18.1f);
    float m_defaultCameraPitch = DirectX::XMConvertToRadians(2.8f);
    std::chrono::steady_clock::time_point m_lastUpdate;
    float m_lightDirection[3] = { -0.35f, -0.8f, 0.45f };
    float m_lightColor[3] = { 1.0f, 0.96f, 0.88f };
    float m_lightIntensity = 4.0f;
    float m_baseMoveSpeed = 17.0f;
    float m_fastMoveSpeed = 58.2f;
    int m_debugViewMode = 0;
    bool m_debugNormalMapYFlip = true;
    int m_debugNormalForceMip = 0;
    float m_debugNormalMipBias = 0.0f;
    bool m_localLightsEnabled = true;
    bool m_emissiveProxyLightsEnabled = true;
    bool m_proceduralProxyLightsEnabled = true;
    int m_activeLightCount = DefaultActiveLightCount;
    float m_localLightIntensityScale = 6.0f;
    float m_lightRadiusScale = 1.0f;
    bool m_shadowsEnabled = true;
    int m_shadowResolutionIndex = 1;
    uint32_t m_shadowResolution = 2048;
    float m_shadowDepthBias = 0.002f;
    float m_shadowNormalBias = 0.05f;
    int m_shadowPcfRadius = 1;
    float m_shadowOrthoSize = 50.0f;
    float m_shadowFocusDistance = 25.0f;
    float m_shadowDepthRange = 160.0f;
    bool m_shadowResourcesDirty = false;
    int m_clusterZSliceCount = 24;
    uint32_t m_clusterTileCountX = 1;
    uint32_t m_clusterTileCountY = 1;
    uint32_t m_clusterAllocatedCount = 1;
    Bistro::ClusterStats m_clusterStats{};
    bool m_samplerAnisotropySupported = false;
    bool m_textureCompressionBcSupported = false;
    float m_maxSamplerAnisotropy = 1.0f;

    std::array<VkSemaphore, MaxFramesInFlight> m_imageAvailableSemaphores{};
    std::array<VkSemaphore, MaxFramesInFlight> m_renderFinishedSemaphores{};
    std::array<VkFence, MaxFramesInFlight> m_inFlightFences{};
    uint32_t m_currentFrame = 0;
};
