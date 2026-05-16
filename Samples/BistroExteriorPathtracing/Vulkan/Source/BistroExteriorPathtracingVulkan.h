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

enum class BistroPathtracingMode
{
    Pathtracing,
    ReSTIR,
    ReSTIRDI,
    ReSTIRPTEnhanced
};

class BistroExteriorPathtracingVulkan
{
public:
    BistroExteriorPathtracingVulkan(uint32_t width, uint32_t height, const wchar_t* title, BistroPathtracingMode mode);
    ~BistroExteriorPathtracingVulkan();

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
        DirectX::XMFLOAT4X4 inverseViewProjection;
        DirectX::XMFLOAT4 cameraPosition;
        DirectX::XMFLOAT4 lightDirection;
        DirectX::XMFLOAT4 lightColor;
        DirectX::XMFLOAT4 debugOptions;
        DirectX::XMFLOAT4 skyColor;
        DirectX::XMFLOAT4 skyHorizonColor;
        DirectX::XMFLOAT4 skyZenithColor;
        DirectX::XMFLOAT4 skyGroundColor;
        DirectX::XMFLOAT4 skyOptions;
        DirectX::XMFLOAT4 rayOptions;
        DirectX::XMFLOAT4 frameOptions;
        DirectX::XMFLOAT4 giOptions;
        DirectX::XMFLOAT4 pathOptions;
        DirectX::XMFLOAT4 restirOptions;
        DirectX::XMFLOAT4 lightOptions;
        DirectX::XMFLOAT4 environmentOptions;
        DirectX::XMFLOAT4 denoiseOptions;
        DirectX::XMFLOAT4 denoiseOptions2;
        DirectX::XMFLOAT4 restirEnhancedOptions0;
        DirectX::XMFLOAT4 restirEnhancedOptions1;
        DirectX::XMFLOAT4 restirEnhancedOptions2;
        DirectX::XMFLOAT4X4 previousViewProjection;
    };

    struct GpuBuffer
    {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkDeviceAddress address = 0;
        VkDeviceSize size = 0;
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

    struct AccelerationStructure
    {
        VkAccelerationStructureKHR handle = VK_NULL_HANDLE;
        GpuBuffer buffer;
        VkDeviceAddress address = 0;
    };

    struct ShaderBindingTable
    {
        GpuBuffer buffer;
        VkStridedDeviceAddressRegionKHR region{};
        uint32_t recordCount = 0;
    };

    static constexpr uint32_t MaxFramesInFlight = 2;
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
    void LoadRayTracingFunctions();
    void CreateSwapChain();
    void CreateImageViews();
    void CreateRenderPass();
    void CreateFramebuffers();
    void CreateCommandPool();
    void LoadModel();
    void CreateOutputImages();
    void CreateSceneBuffers();
    void CreateTextureImages();
    void CreateTextureSampler();
    void CreateDescriptorSetLayouts();
    void CreateDescriptorPool();
    void CreateDescriptorSets();
    void CreateRayTracingPipeline();
    void CreateRestirReusePipeline();
    void CreateDenoisePipeline();
    void CreateShaderBindingTables();
    void BuildAccelerationStructures();
    void CreateCommandBuffers();
    void RecordCommandBuffer(uint32_t imageIndex);
    void RecordRestirReusePass(VkCommandBuffer commandBuffer);
    void RecordDenoisePass(VkCommandBuffer commandBuffer);
    void CreateSyncObjects();
    void InitializeImGui();
    void ShutdownImGui();
    void BuildUI();
    void BuildRendererStatsUI();
    void SetOutputResolution(uint32_t width, uint32_t height);
    void RecreateSwapChain();
    void ResetLight();
    void ResetCameraView();
    void ResetCameraSpeeds();
    void ResetAccumulation();
    bool HasAccumulationStateChanged();
    void UpdateUniformBuffer(float deltaSeconds);

    bool IsDeviceSuitable(VkPhysicalDevice device) const;
    QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice device) const;
    SwapChainSupport QuerySwapChainSupport(VkPhysicalDevice device) const;
    VkSurfaceFormatKHR ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) const;
    VkPresentModeKHR ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& presentModes) const;
    VkExtent2D ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) const;
    uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;
    void CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, GpuBuffer& buffer);
    void DestroyBuffer(GpuBuffer& buffer);
    void UploadBuffer(const void* data, VkDeviceSize size, VkBufferUsageFlags usage, GpuBuffer& destination);
    VkDeviceAddress GetBufferAddress(VkBuffer buffer) const;
    void CreateImage(uint32_t width, uint32_t height, uint32_t mipLevels, VkFormat format, VkImageUsageFlags usage, VkImage& image, VkDeviceMemory& memory);
    VkImageView CreateImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, uint32_t mipLevels) const;
    VkCommandBuffer BeginSingleTimeCommands() const;
    void EndSingleTimeCommands(VkCommandBuffer commandBuffer) const;
    void CopyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size) const;
    void CopyBufferToImage(VkBuffer buffer, VkImage image, const Bistro::TextureData& texture) const;
    void TransitionImageLayout(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout, VkImageAspectFlags aspectMask, VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage) const;
    void TransitionImageLayoutImmediate(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout, VkImageAspectFlags aspectMask) const;
    uint32_t CreateTextureResource(const std::wstring& path, bool srgb, const uint8_t fallback[4], std::map<std::wstring, uint32_t>& cache);
    std::vector<char> ReadFile(const std::wstring& path) const;
    VkShaderModule CreateShaderModule(const std::vector<char>& code) const;
    std::wstring GetExecutableDirectory() const;
    std::wstring ShaderFileName() const;
    std::wstring RestirReuseShaderFileName() const;
    std::wstring DenoiseShaderFileName() const;
    uint32_t MaxTraceRecursionDepth() const;
    VkDeviceSize Align(VkDeviceSize value, VkDeviceSize alignment) const;
    void ThrowIfFailed(VkResult result, const char* message) const;

    uint32_t m_width;
    uint32_t m_height;
    std::wstring m_title;
    BistroPathtracingMode m_mode = BistroPathtracingMode::Pathtracing;
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
    std::vector<bool> m_swapChainImageInitialized;
    VkFormat m_swapChainImageFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D m_swapChainExtent{};
    VkRenderPass m_renderPass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> m_framebuffers;
    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> m_commandBuffers;

    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_textureDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;
    VkDescriptorSet m_textureDescriptorSet = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_rayTracingPipeline = VK_NULL_HANDLE;
    VkPipeline m_restirReusePipeline = VK_NULL_HANDLE;
    VkPipeline m_denoisePipeline = VK_NULL_HANDLE;
    VkSampler m_textureSampler = VK_NULL_HANDLE;

    VkImage m_outputImage = VK_NULL_HANDLE;
    VkDeviceMemory m_outputImageMemory = VK_NULL_HANDLE;
    VkImageView m_outputImageView = VK_NULL_HANDLE;
    VkImage m_accumulationImage = VK_NULL_HANDLE;
    VkDeviceMemory m_accumulationImageMemory = VK_NULL_HANDLE;
    VkImageView m_accumulationImageView = VK_NULL_HANDLE;
    VkImage m_denoiseAov0Image = VK_NULL_HANDLE;
    VkDeviceMemory m_denoiseAov0ImageMemory = VK_NULL_HANDLE;
    VkImageView m_denoiseAov0ImageView = VK_NULL_HANDLE;
    VkImage m_denoiseAov1Image = VK_NULL_HANDLE;
    VkDeviceMemory m_denoiseAov1ImageMemory = VK_NULL_HANDLE;
    VkImageView m_denoiseAov1ImageView = VK_NULL_HANDLE;

    GpuBuffer m_vertexBuffer;
    GpuBuffer m_indexBuffer;
    GpuBuffer m_geometryBuffer;
    GpuBuffer m_materialBuffer;
    GpuBuffer m_lightBuffer;
    GpuBuffer m_sceneUniformBuffer;
    GpuBuffer m_restirReservoirCurrent;
    GpuBuffer m_restirReservoirHistory;
    GpuBuffer m_restirReservoirSpatial;
    GpuBuffer m_enhancedGBufferCurrent;
    GpuBuffer m_enhancedGBufferHistory;
    GpuBuffer m_enhancedDuplicationCurrent;
    GpuBuffer m_enhancedDuplicationHistory;
    GpuBuffer m_enhancedReuseTextureOffsets;
    GpuBuffer m_enhancedReplayTasks;
    VkDeviceSize m_restirReservoirBufferSize = 0;
    VkDeviceSize m_enhancedGBufferSize = 0;
    VkDeviceSize m_enhancedDuplicationMapSize = 0;
    VkDeviceSize m_enhancedReplayTaskBufferSize = 0;
    uint32_t m_enhancedReuseTextureElementCount = 0;
    uint32_t m_restirReservoirElementCount = 1;
    AccelerationStructure m_blas;
    AccelerationStructure m_tlas;
    GpuBuffer m_instanceBuffer;
    ShaderBindingTable m_rayGenSbt;
    ShaderBindingTable m_missSbt;
    ShaderBindingTable m_hitSbt;

    Bistro::Scene m_scene;
    std::vector<Bistro::RtGeometryRecord> m_geometryRecords;
    std::vector<Bistro::RtMaterial> m_rtMaterials;
    std::vector<GpuTexture> m_textures;
    std::vector<std::array<uint32_t, Bistro::TextureSlotCount>> m_materialTextureIndices;
    Bistro::FpsCamera m_camera;
    DirectX::XMFLOAT3 m_defaultCameraPosition = DirectX::XMFLOAT3(-16.32f, 4.66f, -10.41f);
    float m_defaultCameraYaw = DirectX::XMConvertToRadians(18.1f);
    float m_defaultCameraPitch = DirectX::XMConvertToRadians(2.8f);
    std::chrono::steady_clock::time_point m_lastUpdate;
    float m_lightDirection[3] = { -0.35f, -0.8f, 0.45f };
    float m_lightColor[3] = { 1.0f, 0.96f, 0.88f };
    float m_lightIntensity = 4.0f;
    float m_skyColor[3] = { 0.015f, 0.08f, 0.16f };
    float m_skyHorizonColor[3] = { 0.42f, 0.63f, 0.86f };
    float m_skyZenithColor[3] = { 0.05f, 0.20f, 0.52f };
    float m_skyGroundColor[3] = { 0.025f, 0.035f, 0.045f };
    float m_skyIntensity = 1.0f;
    float m_sunIntensity = 8.0f;
    float m_sunAngularRadius = 0.012f;
    float m_skyGroundBlend = 0.35f;
    float m_emissiveLightIntensity = 4.0f;
    float m_proceduralLightIntensity = 12.0f;
    float m_environmentIntensity = 1.0f;
    float m_environmentRotation = 0.0f;
    float m_rayTMin = 0.03f;
    float m_rayTMax = 10000.0f;
    float m_giStrength = 0.6f;
    int m_giSamplesPerFrame = 2;
    float m_giRadianceClamp = 8.0f;
    float m_giTemporalClampScale = 1.5f;
    float m_giTemporalClampMin = 0.25f;
    int m_maxPathBounces = 4;
    int m_minPathBounces = 2;
    int m_maxAccumulatedFrames = 256;
    uint32_t m_accumulatedFrames = 0;
    uint32_t m_frameCounter = 0;
    bool m_freezeAccumulation = false;
    bool m_resetAccumulationRequested = true;
    float m_baseMoveSpeed = 17.0f;
    float m_fastMoveSpeed = 58.2f;
    int m_debugViewMode = 0;
    bool m_debugNormalMapYFlip = true;
    bool m_shadowEnabled = true;
    bool m_skyNeeEnabled = true;
    bool m_emissiveLightsEnabled = true;
    bool m_proceduralLightsEnabled = true;
    bool m_environmentMapEnabled = false;
    bool m_restirTemporalReuse = true;
    int m_restirSpatialReusePasses = 2;
    int m_restirSpatialRadius = 16;
    int m_restirCandidateSamples = 1;
    float m_restirMClamp = 20.0f;
    bool m_enhancedPairedSpatial = true;
    bool m_enhancedDuplicationDecorrelate = true;
    bool m_enhancedColorReuse = true;
    bool m_enhancedRussianRoulette = true;
    bool m_enhancedReplayCompaction = true;
    bool m_enhancedForcedNeeReconnection = true;
    float m_enhancedFootprintC = 0.02f;
    float m_enhancedRoughnessAlphaMin = 0.2f;
    int m_enhancedPrimaryRisCandidates = 32;
    float m_enhancedTemporalCapDefault = 20.0f;
    float m_enhancedTemporalCapMin = 1.0f;
    float m_enhancedDuplicationAlpha = 0.1f;
    DirectX::XMFLOAT4X4 m_previousViewProjection = {};
    bool m_hasPreviousViewProjection = false;
    bool m_denoiserEnabled = true;
    int m_denoiserSpatialIterations = 2;
    float m_denoiserNormalSigma = 0.25f;
    float m_denoiserDepthSigma = 0.02f;
    float m_denoiserLuminanceSigma = 1.5f;
    float m_denoiserAlbedoSigma = 0.35f;
    float m_denoiserStrength = 0.85f;
    bool m_skyEnabled = true;
    DirectX::XMFLOAT4 m_lastCameraAndYaw = DirectX::XMFLOAT4(0, 0, 0, 0);
    DirectX::XMFLOAT4 m_lastLighting = DirectX::XMFLOAT4(0, 0, 0, 0);
    DirectX::XMFLOAT4 m_lastGiOptions = DirectX::XMFLOAT4(0, 0, 0, 0);
    DirectX::XMFLOAT4 m_lastPathOptions = DirectX::XMFLOAT4(0, 0, 0, 0);
    DirectX::XMFLOAT4 m_lastRestirOptions = DirectX::XMFLOAT4(0, 0, 0, 0);
    DirectX::XMFLOAT4 m_lastLightSystemOptions = DirectX::XMFLOAT4(0, 0, 0, 0);
    std::wstring m_environmentTexturePath;
    uint32_t m_environmentDescriptorIndex = 0;
    uint32_t m_environmentTextureIndex = 0;
    uint32_t m_activeLightCount = 0;
    uint32_t m_emissiveTriangleLightCount = 0;
    uint32_t m_proceduralAreaLightCount = 0;
    std::vector<Bistro::RtLight> m_lights;
    bool m_samplerAnisotropySupported = false;
    bool m_textureCompressionBcSupported = false;
    float m_maxSamplerAnisotropy = 1.0f;

    VkPhysicalDeviceRayTracingPipelinePropertiesKHR m_rtPipelineProperties{};
    VkPhysicalDeviceAccelerationStructurePropertiesKHR m_asProperties{};

    PFN_vkGetBufferDeviceAddressKHR m_vkGetBufferDeviceAddressKHR = nullptr;
    PFN_vkCreateAccelerationStructureKHR m_vkCreateAccelerationStructureKHR = nullptr;
    PFN_vkDestroyAccelerationStructureKHR m_vkDestroyAccelerationStructureKHR = nullptr;
    PFN_vkGetAccelerationStructureBuildSizesKHR m_vkGetAccelerationStructureBuildSizesKHR = nullptr;
    PFN_vkCmdBuildAccelerationStructuresKHR m_vkCmdBuildAccelerationStructuresKHR = nullptr;
    PFN_vkGetAccelerationStructureDeviceAddressKHR m_vkGetAccelerationStructureDeviceAddressKHR = nullptr;
    PFN_vkCreateRayTracingPipelinesKHR m_vkCreateRayTracingPipelinesKHR = nullptr;
    PFN_vkGetRayTracingShaderGroupHandlesKHR m_vkGetRayTracingShaderGroupHandlesKHR = nullptr;
    PFN_vkCmdTraceRaysKHR m_vkCmdTraceRaysKHR = nullptr;

    std::array<VkSemaphore, MaxFramesInFlight> m_imageAvailableSemaphores{};
    std::array<VkSemaphore, MaxFramesInFlight> m_renderFinishedSemaphores{};
    std::array<VkFence, MaxFramesInFlight> m_inFlightFences{};
    uint32_t m_currentFrame = 0;
};
