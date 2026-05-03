#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#ifndef VK_USE_PLATFORM_WIN32_KHR
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include <windows.h>

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

class TriangleVulkan
{
public:
    struct Vertex
    {
        float position[3];
        float color[4];
    };

    TriangleVulkan(uint32_t width, uint32_t height, const wchar_t* title);
    ~TriangleVulkan();

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
    void CreateGraphicsPipeline();
    void CreateFramebuffers();
    void CreateCommandPool();
    void CreateVertexBuffer();
    void CreateCommandBuffers();
    void CreateSyncObjects();

    bool IsDeviceSuitable(VkPhysicalDevice device) const;
    QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice device) const;
    SwapChainSupport QuerySwapChainSupport(VkPhysicalDevice device) const;
    VkSurfaceFormatKHR ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) const;
    VkPresentModeKHR ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& presentModes) const;
    VkExtent2D ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) const;
    uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;

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
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_graphicsPipeline = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> m_framebuffers;
    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> m_commandBuffers;
    VkBuffer m_vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_vertexBufferMemory = VK_NULL_HANDLE;
    std::array<VkSemaphore, MaxFramesInFlight> m_imageAvailableSemaphores{};
    std::array<VkSemaphore, MaxFramesInFlight> m_renderFinishedSemaphores{};
    std::array<VkFence, MaxFramesInFlight> m_inFlightFences{};
    uint32_t m_currentFrame = 0;
};
