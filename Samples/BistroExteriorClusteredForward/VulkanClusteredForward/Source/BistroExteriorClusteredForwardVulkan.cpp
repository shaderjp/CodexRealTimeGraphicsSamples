#include "BistroExteriorClusteredForwardVulkan.h"
#include "..\..\Common\BistroTexture.h"
#include "..\..\Common\BistroResolution.h"

#include "imgui.h"
#include "backends/imgui_impl_vulkan.h"
#include "backends/imgui_impl_win32.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <set>
#include <stdexcept>

using namespace DirectX;

#ifndef BISTRO_CLUSTERED_FORWARD
#define BISTRO_CLUSTERED_FORWARD 0
#endif

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

namespace
{
    const std::vector<const char*> DeviceExtensions =
    {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    };

    VkFormat ToVkFormat(DXGI_FORMAT format)
    {
        switch (format)
        {
        case DXGI_FORMAT_R8G8B8A8_UNORM:
            return VK_FORMAT_R8G8B8A8_UNORM;
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
            return VK_FORMAT_R8G8B8A8_SRGB;
        case DXGI_FORMAT_BC1_UNORM:
            return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
        case DXGI_FORMAT_BC1_UNORM_SRGB:
            return VK_FORMAT_BC1_RGBA_SRGB_BLOCK;
        case DXGI_FORMAT_BC2_UNORM:
            return VK_FORMAT_BC2_UNORM_BLOCK;
        case DXGI_FORMAT_BC2_UNORM_SRGB:
            return VK_FORMAT_BC2_SRGB_BLOCK;
        case DXGI_FORMAT_BC3_UNORM:
            return VK_FORMAT_BC3_UNORM_BLOCK;
        case DXGI_FORMAT_BC3_UNORM_SRGB:
            return VK_FORMAT_BC3_SRGB_BLOCK;
        case DXGI_FORMAT_BC4_UNORM:
            return VK_FORMAT_BC4_UNORM_BLOCK;
        case DXGI_FORMAT_BC4_SNORM:
            return VK_FORMAT_BC4_SNORM_BLOCK;
        case DXGI_FORMAT_BC5_UNORM:
            return VK_FORMAT_BC5_UNORM_BLOCK;
        case DXGI_FORMAT_BC5_SNORM:
            return VK_FORMAT_BC5_SNORM_BLOCK;
        case DXGI_FORMAT_BC6H_UF16:
            return VK_FORMAT_BC6H_UFLOAT_BLOCK;
        case DXGI_FORMAT_BC6H_SF16:
            return VK_FORMAT_BC6H_SFLOAT_BLOCK;
        case DXGI_FORMAT_BC7_UNORM:
            return VK_FORMAT_BC7_UNORM_BLOCK;
        case DXGI_FORMAT_BC7_UNORM_SRGB:
            return VK_FORMAT_BC7_SRGB_BLOCK;
        default:
            throw std::runtime_error("Unsupported Vulkan texture format.");
        }
    }
}

bool BistroExteriorClusteredForwardVulkan::QueueFamilyIndices::IsComplete() const
{
    return graphicsFamily != UINT32_MAX && presentFamily != UINT32_MAX;
}

BistroExteriorClusteredForwardVulkan::BistroExteriorClusteredForwardVulkan(uint32_t width, uint32_t height, const wchar_t* title) :
    m_width(width),
    m_height(height),
    m_title(title)
{
}

BistroExteriorClusteredForwardVulkan::~BistroExteriorClusteredForwardVulkan()
{
    Cleanup();
}

int BistroExteriorClusteredForwardVulkan::Run(HINSTANCE instance, int showCommand)
{
    HRESULT coResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(coResult))
    {
        throw std::runtime_error("Failed to initialize COM for WIC texture loading.");
    }
    InitWindow(instance, showCommand);
    InitVulkan();
    MainLoop();
    WaitIdle();
    CoUninitialize();
    return 0;
}

LRESULT CALLBACK BistroExteriorClusteredForwardVulkan::WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hwnd, message, wParam, lParam))
    {
        return true;
    }

    BistroExteriorClusteredForwardVulkan* app = reinterpret_cast<BistroExteriorClusteredForwardVulkan*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE)
    {
        auto* createStruct = reinterpret_cast<CREATESTRUCT*>(lParam);
        app = reinterpret_cast<BistroExteriorClusteredForwardVulkan*>(createStruct->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }

    if (app)
    {
        if (message == WM_SETFOCUS) app->m_camera.SetActive(true);
        if (message == WM_KILLFOCUS) app->m_camera.SetActive(false);
        if (message == WM_RBUTTONDOWN || message == WM_RBUTTONUP) app->m_camera.OnMouseButton(message, wParam);
        if (message == WM_MOUSEMOVE) app->m_camera.OnMouseMove(lParam);
    }

    if (message == WM_DESTROY)
    {
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hwnd, message, wParam, lParam);
}

void BistroExteriorClusteredForwardVulkan::InitWindow(HINSTANCE instance, int showCommand)
{
    m_instanceHandle = instance;

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(WNDCLASSEXW);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    windowClass.lpszClassName = L"BistroExteriorClusteredForwardVulkanWindowClass";
    RegisterClassExW(&windowClass);

    RECT rect = { 0, 0, static_cast<LONG>(m_width), static_cast<LONG>(m_height) };
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);

    m_hwnd = CreateWindowExW(0, windowClass.lpszClassName, m_title.c_str(), WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr, instance, this);
    if (!m_hwnd)
    {
        throw std::runtime_error("Failed to create Win32 window.");
    }

    ShowWindow(m_hwnd, showCommand);
}

void BistroExteriorClusteredForwardVulkan::InitVulkan()
{
    LoadModel();
    CreateInstance();
    CreateSurface();
    PickPhysicalDevice();
    CreateLogicalDevice();
    CreateSwapChain();
    CreateImageViews();
    CreateRenderPass();
    CreateShadowRenderPass();
    CreateDescriptorSetLayout();
    CreateGraphicsPipeline();
    CreateComputePipeline();
    CreateDepthResources();
    CreateShadowResources();
    CreateFramebuffers();
    CreateCommandPool();
    CreateVertexBuffer();
    CreateIndexBuffer();
    CreateTextureImages();
    CreateTextureSampler();
    CreateUniformBuffer();
    CreateLightBuffer();
    CreateClusterBuffers();
    CreateDescriptorPool();
    CreateDescriptorSets();
    InitializeImGui();
    CreateCommandBuffers();
    CreateSyncObjects();
    m_lastUpdate = std::chrono::steady_clock::now();
}

void BistroExteriorClusteredForwardVulkan::MainLoop()
{
    MSG message{};
    while (message.message != WM_QUIT)
    {
        if (PeekMessage(&message, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&message);
            DispatchMessage(&message);
        }
        else
        {
            Render();
        }
    }
}

void BistroExteriorClusteredForwardVulkan::Render()
{
    vkWaitForFences(m_device, 1, &m_inFlightFences[m_currentFrame], VK_TRUE, UINT64_MAX);
    ReadbackClusterStats();
    BuildUI();
    if (m_shadowResourcesDirty)
    {
        RecreateShadowResources();
        m_shadowResourcesDirty = false;
    }
    UpdateUniformBuffer();

    uint32_t imageIndex = 0;
    VkResult acquireResult = vkAcquireNextImageKHR(m_device, m_swapChain, UINT64_MAX, m_imageAvailableSemaphores[m_currentFrame], VK_NULL_HANDLE, &imageIndex);
    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR)
    {
        return;
    }
    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR)
    {
        ThrowIfFailed(acquireResult, "Failed to acquire swapchain image.");
    }

    vkResetFences(m_device, 1, &m_inFlightFences[m_currentFrame]);
    vkResetCommandBuffer(m_commandBuffers[imageIndex], 0);
    RecordCommandBuffer(imageIndex);

    VkSemaphore waitSemaphores[] = { m_imageAvailableSemaphores[m_currentFrame] };
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    VkSemaphore signalSemaphores[] = { m_renderFinishedSemaphores[m_currentFrame] };

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_commandBuffers[imageIndex];
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;
    ThrowIfFailed(vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, m_inFlightFences[m_currentFrame]), "Failed to submit draw command buffer.");

    VkSwapchainKHR swapChains[] = { m_swapChain };
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &imageIndex;
    VkResult presentResult = vkQueuePresentKHR(m_presentQueue, &presentInfo);
    if (presentResult != VK_SUCCESS && presentResult != VK_SUBOPTIMAL_KHR && presentResult != VK_ERROR_OUT_OF_DATE_KHR)
    {
        ThrowIfFailed(presentResult, "Failed to present swapchain image.");
    }
    m_currentFrame = (m_currentFrame + 1) % MaxFramesInFlight;
}

void BistroExteriorClusteredForwardVulkan::SetOutputResolution(uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0 || (m_swapChainExtent.width == width && m_swapChainExtent.height == height))
    {
        return;
    }

    Bistro::ResizeClientArea(m_hwnd, width, height);
    RecreateSwapChain();
}

void BistroExteriorClusteredForwardVulkan::RecreateSwapChain()
{
    if (m_device == VK_NULL_HANDLE || m_swapChain == VK_NULL_HANDLE)
    {
        return;
    }

    vkDeviceWaitIdle(m_device);

    if (!m_commandBuffers.empty())
    {
        vkFreeCommandBuffers(m_device, m_commandPool, static_cast<uint32_t>(m_commandBuffers.size()), m_commandBuffers.data());
        m_commandBuffers.clear();
    }

    for (VkFramebuffer framebuffer : m_framebuffers)
    {
        vkDestroyFramebuffer(m_device, framebuffer, nullptr);
    }
    m_framebuffers.clear();

    if (m_depthImageView)
    {
        vkDestroyImageView(m_device, m_depthImageView, nullptr);
        m_depthImageView = VK_NULL_HANDLE;
    }
    if (m_depthImage)
    {
        vkDestroyImage(m_device, m_depthImage, nullptr);
        m_depthImage = VK_NULL_HANDLE;
    }
    if (m_depthImageMemory)
    {
        vkFreeMemory(m_device, m_depthImageMemory, nullptr);
        m_depthImageMemory = VK_NULL_HANDLE;
    }

    if (m_graphicsPipeline)
    {
        vkDestroyPipeline(m_device, m_graphicsPipeline, nullptr);
        m_graphicsPipeline = VK_NULL_HANDLE;
    }
    if (m_shadowPipeline)
    {
        vkDestroyPipeline(m_device, m_shadowPipeline, nullptr);
        m_shadowPipeline = VK_NULL_HANDLE;
    }
    if (m_descriptorPool)
    {
        vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
        m_descriptorPool = VK_NULL_HANDLE;
        m_descriptorSets.clear();
    }
    if (m_clusterStatsBuffer)
    {
        vkDestroyBuffer(m_device, m_clusterStatsBuffer, nullptr);
        m_clusterStatsBuffer = VK_NULL_HANDLE;
    }
    if (m_clusterStatsBufferMemory)
    {
        vkFreeMemory(m_device, m_clusterStatsBufferMemory, nullptr);
        m_clusterStatsBufferMemory = VK_NULL_HANDLE;
    }
    if (m_clusterLightIndicesBuffer)
    {
        vkDestroyBuffer(m_device, m_clusterLightIndicesBuffer, nullptr);
        m_clusterLightIndicesBuffer = VK_NULL_HANDLE;
    }
    if (m_clusterLightIndicesBufferMemory)
    {
        vkFreeMemory(m_device, m_clusterLightIndicesBufferMemory, nullptr);
        m_clusterLightIndicesBufferMemory = VK_NULL_HANDLE;
    }
    if (m_clusterRecordsBuffer)
    {
        vkDestroyBuffer(m_device, m_clusterRecordsBuffer, nullptr);
        m_clusterRecordsBuffer = VK_NULL_HANDLE;
    }
    if (m_clusterRecordsBufferMemory)
    {
        vkFreeMemory(m_device, m_clusterRecordsBufferMemory, nullptr);
        m_clusterRecordsBufferMemory = VK_NULL_HANDLE;
    }
    if (m_clusterResetPipeline)
    {
        vkDestroyPipeline(m_device, m_clusterResetPipeline, nullptr);
        m_clusterResetPipeline = VK_NULL_HANDLE;
    }
    if (m_clusterBuildPipeline)
    {
        vkDestroyPipeline(m_device, m_clusterBuildPipeline, nullptr);
        m_clusterBuildPipeline = VK_NULL_HANDLE;
    }
    m_clusterStats = {};
    if (m_pipelineLayout)
    {
        vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
        m_pipelineLayout = VK_NULL_HANDLE;
    }

    for (VkImageView imageView : m_swapChainImageViews)
    {
        vkDestroyImageView(m_device, imageView, nullptr);
    }
    m_swapChainImageViews.clear();
    m_swapChainImages.clear();

    vkDestroySwapchainKHR(m_device, m_swapChain, nullptr);
    m_swapChain = VK_NULL_HANDLE;

    CreateSwapChain();
    m_width = m_swapChainExtent.width;
    m_height = m_swapChainExtent.height;
    CreateImageViews();
    CreateGraphicsPipeline();
    CreateDepthResources();
    CreateFramebuffers();
    CreateClusterBuffers();
    CreateDescriptorPool();
    CreateDescriptorSets();
    CreateCommandBuffers();
}

void BistroExteriorClusteredForwardVulkan::WaitIdle()
{
    if (m_device != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(m_device);
    }
}

void BistroExteriorClusteredForwardVulkan::Cleanup()
{
    if (m_device != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(m_device);
        ShutdownImGui();

        for (uint32_t i = 0; i < MaxFramesInFlight; ++i)
        {
            if (m_inFlightFences[i]) vkDestroyFence(m_device, m_inFlightFences[i], nullptr);
            if (m_renderFinishedSemaphores[i]) vkDestroySemaphore(m_device, m_renderFinishedSemaphores[i], nullptr);
            if (m_imageAvailableSemaphores[i]) vkDestroySemaphore(m_device, m_imageAvailableSemaphores[i], nullptr);
        }

        if (m_descriptorPool) vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
        if (m_clusterStatsBuffer) vkDestroyBuffer(m_device, m_clusterStatsBuffer, nullptr);
        if (m_clusterStatsBufferMemory) vkFreeMemory(m_device, m_clusterStatsBufferMemory, nullptr);
        if (m_clusterLightIndicesBuffer) vkDestroyBuffer(m_device, m_clusterLightIndicesBuffer, nullptr);
        if (m_clusterLightIndicesBufferMemory) vkFreeMemory(m_device, m_clusterLightIndicesBufferMemory, nullptr);
        if (m_clusterRecordsBuffer) vkDestroyBuffer(m_device, m_clusterRecordsBuffer, nullptr);
        if (m_clusterRecordsBufferMemory) vkFreeMemory(m_device, m_clusterRecordsBufferMemory, nullptr);
        if (m_lightBuffer) vkDestroyBuffer(m_device, m_lightBuffer, nullptr);
        if (m_lightBufferMemory) vkFreeMemory(m_device, m_lightBufferMemory, nullptr);
        if (m_sceneUniformBuffer) vkDestroyBuffer(m_device, m_sceneUniformBuffer, nullptr);
        if (m_sceneUniformBufferMemory) vkFreeMemory(m_device, m_sceneUniformBufferMemory, nullptr);
        if (m_materialUniformBuffer) vkDestroyBuffer(m_device, m_materialUniformBuffer, nullptr);
        if (m_materialUniformBufferMemory) vkFreeMemory(m_device, m_materialUniformBufferMemory, nullptr);
        if (m_textureSampler) vkDestroySampler(m_device, m_textureSampler, nullptr);
        for (GpuTexture& texture : m_textures)
        {
            if (texture.view) vkDestroyImageView(m_device, texture.view, nullptr);
            if (texture.image) vkDestroyImage(m_device, texture.image, nullptr);
            if (texture.memory) vkFreeMemory(m_device, texture.memory, nullptr);
        }
        if (m_indexBuffer) vkDestroyBuffer(m_device, m_indexBuffer, nullptr);
        if (m_indexBufferMemory) vkFreeMemory(m_device, m_indexBufferMemory, nullptr);
        if (m_vertexBuffer) vkDestroyBuffer(m_device, m_vertexBuffer, nullptr);
        if (m_vertexBufferMemory) vkFreeMemory(m_device, m_vertexBufferMemory, nullptr);
        if (m_commandPool) vkDestroyCommandPool(m_device, m_commandPool, nullptr);
        DestroyShadowResources();

        for (VkFramebuffer framebuffer : m_framebuffers)
        {
            vkDestroyFramebuffer(m_device, framebuffer, nullptr);
        }
        if (m_depthImageView) vkDestroyImageView(m_device, m_depthImageView, nullptr);
        if (m_depthImage) vkDestroyImage(m_device, m_depthImage, nullptr);
        if (m_depthImageMemory) vkFreeMemory(m_device, m_depthImageMemory, nullptr);
        if (m_graphicsPipeline) vkDestroyPipeline(m_device, m_graphicsPipeline, nullptr);
        if (m_shadowPipeline) vkDestroyPipeline(m_device, m_shadowPipeline, nullptr);
        if (m_clusterResetPipeline) vkDestroyPipeline(m_device, m_clusterResetPipeline, nullptr);
        if (m_clusterBuildPipeline) vkDestroyPipeline(m_device, m_clusterBuildPipeline, nullptr);
        if (m_pipelineLayout) vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
        if (m_descriptorSetLayout) vkDestroyDescriptorSetLayout(m_device, m_descriptorSetLayout, nullptr);
        if (m_renderPass) vkDestroyRenderPass(m_device, m_renderPass, nullptr);
        if (m_shadowRenderPass) vkDestroyRenderPass(m_device, m_shadowRenderPass, nullptr);

        for (VkImageView imageView : m_swapChainImageViews)
        {
            vkDestroyImageView(m_device, imageView, nullptr);
        }
        if (m_swapChain) vkDestroySwapchainKHR(m_device, m_swapChain, nullptr);
        vkDestroyDevice(m_device, nullptr);
        m_device = VK_NULL_HANDLE;
    }

    if (m_surface) vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
    if (m_instance) vkDestroyInstance(m_instance, nullptr);
    if (m_hwnd) DestroyWindow(m_hwnd);
}

void BistroExteriorClusteredForwardVulkan::CreateInstance()
{
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "BistroExteriorClusteredForward Vulkan";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "CodexdRealTimeGraphicsSamples";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

    const char* extensions[] = { VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME };
    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(std::size(extensions));
    createInfo.ppEnabledExtensionNames = extensions;
    ThrowIfFailed(vkCreateInstance(&createInfo, nullptr, &m_instance), "Failed to create Vulkan instance.");
}

void BistroExteriorClusteredForwardVulkan::CreateSurface()
{
    VkWin32SurfaceCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    createInfo.hinstance = m_instanceHandle;
    createInfo.hwnd = m_hwnd;
    ThrowIfFailed(vkCreateWin32SurfaceKHR(m_instance, &createInfo, nullptr, &m_surface), "Failed to create Win32 surface.");
}

void BistroExteriorClusteredForwardVulkan::PickPhysicalDevice()
{
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);
    if (deviceCount == 0)
    {
        throw std::runtime_error("No Vulkan-capable GPU was found.");
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data());
    for (VkPhysicalDevice device : devices)
    {
        if (IsDeviceSuitable(device))
        {
            m_physicalDevice = device;
            return;
        }
    }

    throw std::runtime_error("No suitable Vulkan device was found.");
}

void BistroExteriorClusteredForwardVulkan::CreateLogicalDevice()
{
    QueueFamilyIndices indices = FindQueueFamilies(m_physicalDevice);
    std::set<uint32_t> uniqueQueueFamilies = { indices.graphicsFamily, indices.presentFamily };
    float queuePriority = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;

    for (uint32_t queueFamily : uniqueQueueFamilies)
    {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }

    VkPhysicalDeviceFeatures supportedFeatures{};
    vkGetPhysicalDeviceFeatures(m_physicalDevice, &supportedFeatures);
    VkPhysicalDeviceProperties deviceProperties{};
    vkGetPhysicalDeviceProperties(m_physicalDevice, &deviceProperties);
    m_samplerAnisotropySupported = supportedFeatures.samplerAnisotropy == VK_TRUE;
    m_textureCompressionBcSupported = supportedFeatures.textureCompressionBC == VK_TRUE;
    m_maxSamplerAnisotropy = m_samplerAnisotropySupported ? deviceProperties.limits.maxSamplerAnisotropy : 1.0f;

    VkPhysicalDeviceFeatures deviceFeatures{};
    deviceFeatures.samplerAnisotropy = supportedFeatures.samplerAnisotropy;
    deviceFeatures.textureCompressionBC = m_textureCompressionBcSupported ? VK_TRUE : VK_FALSE;
    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = &deviceFeatures;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(DeviceExtensions.size());
    createInfo.ppEnabledExtensionNames = DeviceExtensions.data();
    ThrowIfFailed(vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device), "Failed to create Vulkan device.");
    vkGetDeviceQueue(m_device, indices.graphicsFamily, 0, &m_graphicsQueue);
    vkGetDeviceQueue(m_device, indices.presentFamily, 0, &m_presentQueue);
}

void BistroExteriorClusteredForwardVulkan::CreateSwapChain()
{
    SwapChainSupport support = QuerySwapChainSupport(m_physicalDevice);
    VkSurfaceFormatKHR surfaceFormat = ChooseSwapSurfaceFormat(support.formats);
    VkPresentModeKHR presentMode = ChooseSwapPresentMode(support.presentModes);
    VkExtent2D extent = ChooseSwapExtent(support.capabilities);

    uint32_t imageCount = support.capabilities.minImageCount + 1;
    if (support.capabilities.maxImageCount > 0 && imageCount > support.capabilities.maxImageCount)
    {
        imageCount = support.capabilities.maxImageCount;
    }

    QueueFamilyIndices indices = FindQueueFamilies(m_physicalDevice);
    uint32_t queueFamilyIndices[] = { indices.graphicsFamily, indices.presentFamily };

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = m_surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    if (indices.graphicsFamily != indices.presentFamily)
    {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    }
    else
    {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    createInfo.preTransform = support.capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    ThrowIfFailed(vkCreateSwapchainKHR(m_device, &createInfo, nullptr, &m_swapChain), "Failed to create swapchain.");

    vkGetSwapchainImagesKHR(m_device, m_swapChain, &imageCount, nullptr);
    m_swapChainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(m_device, m_swapChain, &imageCount, m_swapChainImages.data());
    m_swapChainImageFormat = surfaceFormat.format;
    m_swapChainExtent = extent;
}

void BistroExteriorClusteredForwardVulkan::CreateImageViews()
{
    m_swapChainImageViews.resize(m_swapChainImages.size());
    for (size_t i = 0; i < m_swapChainImages.size(); ++i)
    {
        m_swapChainImageViews[i] = CreateImageView(m_swapChainImages[i], m_swapChainImageFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1);
    }
}

void BistroExteriorClusteredForwardVulkan::CreateRenderPass()
{
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = m_swapChainImageFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = DepthFormat;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    std::array<VkAttachmentDescription, 2> attachments = { colorAttachment, depthAttachment };
    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;
    ThrowIfFailed(vkCreateRenderPass(m_device, &renderPassInfo, nullptr, &m_renderPass), "Failed to create render pass.");
}

void BistroExteriorClusteredForwardVulkan::CreateShadowRenderPass()
{
    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = DepthFormat;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 0;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    std::array<VkSubpassDependency, 2> dependencies{};
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &depthAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
    renderPassInfo.pDependencies = dependencies.data();
    ThrowIfFailed(vkCreateRenderPass(m_device, &renderPassInfo, nullptr, &m_shadowRenderPass), "Failed to create shadow render pass.");
}

void BistroExteriorClusteredForwardVulkan::CreateDescriptorSetLayout()
{
    std::array<VkDescriptorSetLayoutBinding, 15> bindings{};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    for (uint32_t i = 0; i < Bistro::TextureSlotCount; ++i)
    {
        bindings[2 + i].binding = 2 + i;
        bindings[2 + i].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        bindings[2 + i].descriptorCount = 1;
        bindings[2 + i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }

    bindings[6].binding = 6;
    bindings[6].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    bindings[6].descriptorCount = 1;
    bindings[6].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    for (uint32_t i = 7; i <= 12; ++i)
    {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
    }

    bindings[13].binding = 13;
    bindings[13].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    bindings[13].descriptorCount = 1;
    bindings[13].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[14].binding = 14;
    bindings[14].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    bindings[14].descriptorCount = 1;
    bindings[14].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    ThrowIfFailed(vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_descriptorSetLayout), "Failed to create descriptor set layout.");
}

void BistroExteriorClusteredForwardVulkan::CreateGraphicsPipeline()
{
    std::wstring shaderDirectory = GetExecutableDirectory();
    VkShaderModule vertexShaderModule = CreateShaderModule(ReadFile(shaderDirectory + L"BistroExterior.vs.spv"));
    VkShaderModule pixelShaderModule = CreateShaderModule(ReadFile(shaderDirectory + L"BistroExterior.ps.spv"));

    VkPipelineShaderStageCreateInfo vertexShaderStageInfo{};
    vertexShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertexShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertexShaderStageInfo.module = vertexShaderModule;
    vertexShaderStageInfo.pName = "VSMain";

    VkPipelineShaderStageCreateInfo pixelShaderStageInfo{};
    pixelShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pixelShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    pixelShaderStageInfo.module = pixelShaderModule;
    pixelShaderStageInfo.pName = "PSMain";

    VkPipelineShaderStageCreateInfo shaderStages[] = { vertexShaderStageInfo, pixelShaderStageInfo };

    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(Bistro::Vertex);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 4> attributeDescriptions{};
    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[0].offset = offsetof(Bistro::Vertex, position);
    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[1].offset = offsetof(Bistro::Vertex, normal);
    attributeDescriptions[2].binding = 0;
    attributeDescriptions[2].location = 2;
    attributeDescriptions[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attributeDescriptions[2].offset = offsetof(Bistro::Vertex, tangent);
    attributeDescriptions[3].binding = 0;
    attributeDescriptions[3].location = 3;
    attributeDescriptions[3].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[3].offset = offsetof(Bistro::Vertex, texcoord);

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport viewport{};
    viewport.width = static_cast<float>(m_swapChainExtent.width);
    viewport.height = static_cast<float>(m_swapChainExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.extent = m_swapChainExtent;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &m_descriptorSetLayout;
    ThrowIfFailed(vkCreatePipelineLayout(m_device, &pipelineLayoutInfo, nullptr, &m_pipelineLayout), "Failed to create pipeline layout.");

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.layout = m_pipelineLayout;
    pipelineInfo.renderPass = m_renderPass;
    pipelineInfo.subpass = 0;
    ThrowIfFailed(vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_graphicsPipeline), "Failed to create graphics pipeline.");

    vkDestroyShaderModule(m_device, pixelShaderModule, nullptr);
    vkDestroyShaderModule(m_device, vertexShaderModule, nullptr);

    CreateShadowPipeline();
}

void BistroExteriorClusteredForwardVulkan::CreateShadowPipeline()
{
    if (m_shadowPipeline)
    {
        vkDestroyPipeline(m_device, m_shadowPipeline, nullptr);
        m_shadowPipeline = VK_NULL_HANDLE;
    }

    std::wstring shaderDirectory = GetExecutableDirectory();
    VkShaderModule vertexShaderModule = CreateShaderModule(ReadFile(shaderDirectory + L"BistroExterior.shadow.vs.spv"));
    VkShaderModule pixelShaderModule = CreateShaderModule(ReadFile(shaderDirectory + L"BistroExterior.shadow.ps.spv"));

    VkPipelineShaderStageCreateInfo vertexShaderStageInfo{};
    vertexShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertexShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertexShaderStageInfo.module = vertexShaderModule;
    vertexShaderStageInfo.pName = "VSShadowMain";

    VkPipelineShaderStageCreateInfo pixelShaderStageInfo{};
    pixelShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pixelShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    pixelShaderStageInfo.module = pixelShaderModule;
    pixelShaderStageInfo.pName = "PSShadowMain";

    VkPipelineShaderStageCreateInfo shaderStages[] = { vertexShaderStageInfo, pixelShaderStageInfo };

    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(Bistro::Vertex);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 4> attributeDescriptions{};
    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[0].offset = offsetof(Bistro::Vertex, position);
    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[1].offset = offsetof(Bistro::Vertex, normal);
    attributeDescriptions[2].binding = 0;
    attributeDescriptions[2].location = 2;
    attributeDescriptions[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attributeDescriptions[2].offset = offsetof(Bistro::Vertex, tangent);
    attributeDescriptions[3].binding = 0;
    attributeDescriptions[3].location = 3;
    attributeDescriptions[3].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[3].offset = offsetof(Bistro::Vertex, texcoord);

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;
    rasterizer.depthBiasEnable = VK_TRUE;
    rasterizer.depthBiasConstantFactor = 1.25f;
    rasterizer.depthBiasSlopeFactor = 2.0f;
    rasterizer.depthBiasClamp = 0.01f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

    VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(std::size(dynamicStates));
    dynamicState.pDynamicStates = dynamicStates;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = m_pipelineLayout;
    pipelineInfo.renderPass = m_shadowRenderPass;
    pipelineInfo.subpass = 0;
    ThrowIfFailed(vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_shadowPipeline), "Failed to create shadow pipeline.");

    vkDestroyShaderModule(m_device, pixelShaderModule, nullptr);
    vkDestroyShaderModule(m_device, vertexShaderModule, nullptr);
}

void BistroExteriorClusteredForwardVulkan::CreateComputePipeline()
{
#if BISTRO_CLUSTERED_FORWARD
    std::wstring shaderDirectory = GetExecutableDirectory();
    VkShaderModule resetShaderModule = CreateShaderModule(ReadFile(shaderDirectory + L"BistroExteriorClusteredForward.reset.cs.spv"));
    VkShaderModule buildShaderModule = CreateShaderModule(ReadFile(shaderDirectory + L"BistroExteriorClusteredForward.build.cs.spv"));

    VkComputePipelineCreateInfo resetInfo{};
    resetInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    resetInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    resetInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    resetInfo.stage.module = resetShaderModule;
    resetInfo.stage.pName = "CSResetStats";
    resetInfo.layout = m_pipelineLayout;
    ThrowIfFailed(vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &resetInfo, nullptr, &m_clusterResetPipeline), "Failed to create cluster reset compute pipeline.");

    VkComputePipelineCreateInfo buildInfo{};
    buildInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    buildInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    buildInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    buildInfo.stage.module = buildShaderModule;
    buildInfo.stage.pName = "CSBuildClusters";
    buildInfo.layout = m_pipelineLayout;
    ThrowIfFailed(vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &buildInfo, nullptr, &m_clusterBuildPipeline), "Failed to create cluster build compute pipeline.");

    vkDestroyShaderModule(m_device, buildShaderModule, nullptr);
    vkDestroyShaderModule(m_device, resetShaderModule, nullptr);
#endif
}

void BistroExteriorClusteredForwardVulkan::CreateDepthResources()
{
    CreateImage(m_swapChainExtent.width, m_swapChainExtent.height, 1, DepthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, m_depthImage, m_depthImageMemory);
    m_depthImageView = CreateImageView(m_depthImage, DepthFormat, VK_IMAGE_ASPECT_DEPTH_BIT, 1);
}

void BistroExteriorClusteredForwardVulkan::CreateShadowResources()
{
    CreateImage(
        m_shadowResolution,
        m_shadowResolution,
        1,
        DepthFormat,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        m_shadowDepthImage,
        m_shadowDepthImageMemory);
    m_shadowDepthImageView = CreateImageView(m_shadowDepthImage, DepthFormat, VK_IMAGE_ASPECT_DEPTH_BIT, 1);

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    samplerInfo.compareEnable = VK_TRUE;
    samplerInfo.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;
    ThrowIfFailed(vkCreateSampler(m_device, &samplerInfo, nullptr, &m_shadowSampler), "Failed to create shadow sampler.");

    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = m_shadowRenderPass;
    framebufferInfo.attachmentCount = 1;
    framebufferInfo.pAttachments = &m_shadowDepthImageView;
    framebufferInfo.width = m_shadowResolution;
    framebufferInfo.height = m_shadowResolution;
    framebufferInfo.layers = 1;
    ThrowIfFailed(vkCreateFramebuffer(m_device, &framebufferInfo, nullptr, &m_shadowFramebuffer), "Failed to create shadow framebuffer.");
}

void BistroExteriorClusteredForwardVulkan::DestroyShadowResources()
{
    if (m_shadowFramebuffer) vkDestroyFramebuffer(m_device, m_shadowFramebuffer, nullptr);
    m_shadowFramebuffer = VK_NULL_HANDLE;
    if (m_shadowSampler) vkDestroySampler(m_device, m_shadowSampler, nullptr);
    m_shadowSampler = VK_NULL_HANDLE;
    if (m_shadowDepthImageView) vkDestroyImageView(m_device, m_shadowDepthImageView, nullptr);
    m_shadowDepthImageView = VK_NULL_HANDLE;
    if (m_shadowDepthImage) vkDestroyImage(m_device, m_shadowDepthImage, nullptr);
    m_shadowDepthImage = VK_NULL_HANDLE;
    if (m_shadowDepthImageMemory) vkFreeMemory(m_device, m_shadowDepthImageMemory, nullptr);
    m_shadowDepthImageMemory = VK_NULL_HANDLE;
}

void BistroExteriorClusteredForwardVulkan::RecreateShadowResources()
{
    vkDeviceWaitIdle(m_device);
    DestroyShadowResources();
    CreateShadowResources();
    UpdateShadowDescriptorSets();
}

void BistroExteriorClusteredForwardVulkan::CreateFramebuffers()
{
    m_framebuffers.resize(m_swapChainImageViews.size());
    for (size_t i = 0; i < m_swapChainImageViews.size(); ++i)
    {
        std::array<VkImageView, 2> attachments = { m_swapChainImageViews[i], m_depthImageView };
        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = m_renderPass;
        framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        framebufferInfo.pAttachments = attachments.data();
        framebufferInfo.width = m_swapChainExtent.width;
        framebufferInfo.height = m_swapChainExtent.height;
        framebufferInfo.layers = 1;
        ThrowIfFailed(vkCreateFramebuffer(m_device, &framebufferInfo, nullptr, &m_framebuffers[i]), "Failed to create framebuffer.");
    }
}

void BistroExteriorClusteredForwardVulkan::CreateCommandPool()
{
    QueueFamilyIndices queueFamilyIndices = FindQueueFamilies(m_physicalDevice);
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    ThrowIfFailed(vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool), "Failed to create command pool.");
}

void BistroExteriorClusteredForwardVulkan::LoadModel()
{
    m_scene = Bistro::LoadScene(Bistro::FindAssetRoot());
    m_lightBuild = Bistro::BuildRasterLightList(m_scene);
    m_activeLightCount = static_cast<int>((std::min<size_t>)(DefaultActiveLightCount, m_lightBuild.lights.size()));
    m_defaultCameraPosition = XMFLOAT3(-16.32f, 4.66f, -10.41f);
    m_defaultCameraYaw = XMConvertToRadians(18.1f);
    m_defaultCameraPitch = XMConvertToRadians(2.8f);
    ResetCameraView();
}

void BistroExteriorClusteredForwardVulkan::CreateVertexBuffer()
{
    VkDeviceSize bufferSize = sizeof(Bistro::Vertex) * m_scene.vertices.size();
    CreateBuffer(bufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_vertexBuffer, m_vertexBufferMemory);
    void* data = nullptr;
    vkMapMemory(m_device, m_vertexBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, m_scene.vertices.data(), static_cast<size_t>(bufferSize));
    vkUnmapMemory(m_device, m_vertexBufferMemory);
}

void BistroExteriorClusteredForwardVulkan::CreateIndexBuffer()
{
    VkDeviceSize bufferSize = sizeof(uint32_t) * m_scene.indices.size();
    CreateBuffer(bufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_indexBuffer, m_indexBufferMemory);
    void* data = nullptr;
    vkMapMemory(m_device, m_indexBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, m_scene.indices.data(), static_cast<size_t>(bufferSize));
    vkUnmapMemory(m_device, m_indexBufferMemory);
}

uint32_t BistroExteriorClusteredForwardVulkan::CreateTextureResource(const std::wstring& path, bool srgb, const uint8_t fallback[4], std::map<std::wstring, uint32_t>& cache)
{
    std::wstring key = path.empty() ? (std::wstring(L"fallback:") + std::to_wstring(fallback[0]) + L"," + std::to_wstring(fallback[1]) + L"," + std::to_wstring(fallback[2]) + L"," + std::to_wstring(fallback[3]) + (srgb ? L":srgb" : L":linear")) : path + (srgb ? L":srgb" : L":linear");
    auto found = cache.find(key);
    if (found != cache.end())
    {
        return found->second;
    }

    Bistro::TextureData image = Bistro::LoadTextureVulkan(path, srgb, fallback, m_textureCompressionBcSupported);
    VkDeviceSize imageSize = static_cast<VkDeviceSize>(image.pixels.size());

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingBufferMemory = VK_NULL_HANDLE;
    CreateBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);
    void* data = nullptr;
    vkMapMemory(m_device, stagingBufferMemory, 0, imageSize, 0, &data);
    memcpy(data, image.pixels.data(), static_cast<size_t>(imageSize));
    vkUnmapMemory(m_device, stagingBufferMemory);

    GpuTexture texture;
    texture.path = path;
    texture.width = image.width;
    texture.height = image.height;
    texture.fallback = image.fallback;
    texture.format = ToVkFormat(image.format);
    texture.mipLevels = image.mipLevels;
    CreateImage(image.width, image.height, texture.mipLevels, texture.format, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, texture.image, texture.memory);
    TransitionImageLayout(texture.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, texture.mipLevels);
    CopyBufferToImage(stagingBuffer, texture.image, image);
    TransitionImageLayout(texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, texture.mipLevels);
    texture.view = CreateImageView(texture.image, texture.format, VK_IMAGE_ASPECT_COLOR_BIT, texture.mipLevels);

    vkDestroyBuffer(m_device, stagingBuffer, nullptr);
    vkFreeMemory(m_device, stagingBufferMemory, nullptr);

    const uint32_t index = static_cast<uint32_t>(m_textures.size());
    m_textures.push_back(texture);
    cache[key] = index;
    return index;
}

void BistroExteriorClusteredForwardVulkan::CreateTextureImages()
{
    const uint8_t white[] = { 255, 255, 255, 255 };
    const uint8_t normal[] = { 128, 128, 255, 255 };
    const uint8_t specular[] = { 255, 180, 0, 255 };
    const uint8_t black[] = { 0, 0, 0, 255 };
    std::map<std::wstring, uint32_t> textureCache;

    m_materialTextureIndices.resize(m_scene.materials.size());
    for (const Bistro::Material& material : m_scene.materials)
    {
        const size_t materialIndex = &material - m_scene.materials.data();
        m_materialTextureIndices[materialIndex][0] = CreateTextureResource(material.textures[Bistro::TextureSlotBaseColor], true, white, textureCache);
        m_materialTextureIndices[materialIndex][1] = CreateTextureResource(material.textures[Bistro::TextureSlotNormal], false, normal, textureCache);
        m_materialTextureIndices[materialIndex][2] = CreateTextureResource(material.textures[Bistro::TextureSlotSpecular], false, specular, textureCache);
        m_materialTextureIndices[materialIndex][3] = CreateTextureResource(material.textures[Bistro::TextureSlotEmissive], false, black, textureCache);
    }
}

void BistroExteriorClusteredForwardVulkan::CreateTextureSampler()
{
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 16.0f;
    samplerInfo.maxAnisotropy = m_samplerAnisotropySupported ? (std::min)(m_maxSamplerAnisotropy, 8.0f) : 1.0f;
    samplerInfo.anisotropyEnable = m_samplerAnisotropySupported ? VK_TRUE : VK_FALSE;
    ThrowIfFailed(vkCreateSampler(m_device, &samplerInfo, nullptr, &m_textureSampler), "Failed to create texture sampler.");
}

void BistroExteriorClusteredForwardVulkan::CreateUniformBuffer()
{
    CreateBuffer(sizeof(SceneConstants), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_sceneUniformBuffer, m_sceneUniformBufferMemory);

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(m_physicalDevice, &properties);
    uint32_t alignment = static_cast<uint32_t>(properties.limits.minUniformBufferOffsetAlignment);
    if (alignment == 0)
    {
        alignment = 256;
    }
    m_materialUniformStride = static_cast<uint32_t>((sizeof(MaterialConstants) + alignment - 1) & ~static_cast<size_t>(alignment - 1));
    VkDeviceSize materialBufferSize = static_cast<VkDeviceSize>(m_materialUniformStride) * m_scene.materials.size();
    CreateBuffer(materialBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_materialUniformBuffer, m_materialUniformBufferMemory);

    void* data = nullptr;
    vkMapMemory(m_device, m_materialUniformBufferMemory, 0, materialBufferSize, 0, &data);
    for (size_t i = 0; i < m_scene.materials.size(); ++i)
    {
        MaterialConstants material{};
        material.baseColorFactor = m_scene.materials[i].baseColorFactor;
        material.emissiveFactor = m_scene.materials[i].emissiveFactor;
        material.options = XMFLOAT4(0.0f, 0.0f, m_scene.materials[i].alphaCutoff, m_scene.materials[i].alphaMasked ? 1.0f : 0.0f);
        memcpy(static_cast<uint8_t*>(data) + i * m_materialUniformStride, &material, sizeof(material));
    }
    vkUnmapMemory(m_device, m_materialUniformBufferMemory);

    UpdateUniformBuffer();
}

void BistroExteriorClusteredForwardVulkan::CreateLightBuffer()
{
    VkDeviceSize bufferSize = sizeof(Bistro::RasterLight) * (std::max<size_t>)(1, m_lightBuild.lights.size());
    CreateBuffer(bufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_lightBuffer, m_lightBufferMemory);
    void* data = nullptr;
    vkMapMemory(m_device, m_lightBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, m_lightBuild.lights.data(), static_cast<size_t>(bufferSize));
    vkUnmapMemory(m_device, m_lightBufferMemory);
}

void BistroExteriorClusteredForwardVulkan::CreateClusterBuffers()
{
    m_clusterTileCountX = (m_swapChainExtent.width + ClusterTileSize - 1) / ClusterTileSize;
    m_clusterTileCountY = (m_swapChainExtent.height + ClusterTileSize - 1) / ClusterTileSize;
    m_clusterAllocatedCount = (std::max)(1u, m_clusterTileCountX * m_clusterTileCountY * MaxClusterZSlices);

    const VkDeviceSize recordsSize = sizeof(Bistro::ClusterRecord) * m_clusterAllocatedCount;
    const VkDeviceSize indicesSize = sizeof(uint32_t) * m_clusterAllocatedCount * MaxLightsPerCluster;
    const VkDeviceSize statsSize = sizeof(Bistro::ClusterStats);
    VkMemoryPropertyFlags storageMemory = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    CreateBuffer(recordsSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, storageMemory, m_clusterRecordsBuffer, m_clusterRecordsBufferMemory);
    CreateBuffer(indicesSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, storageMemory, m_clusterLightIndicesBuffer, m_clusterLightIndicesBufferMemory);
    CreateBuffer(statsSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, storageMemory, m_clusterStatsBuffer, m_clusterStatsBufferMemory);
}

void BistroExteriorClusteredForwardVulkan::CreateDescriptorPool()
{
    std::array<VkDescriptorPoolSize, 4> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = static_cast<uint32_t>(m_scene.materials.size() * 2);
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    poolSizes[1].descriptorCount = static_cast<uint32_t>(m_scene.materials.size() * (Bistro::TextureSlotCount + 1));
    poolSizes[2].type = VK_DESCRIPTOR_TYPE_SAMPLER;
    poolSizes[2].descriptorCount = static_cast<uint32_t>(m_scene.materials.size() * 2);
    poolSizes[3].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[3].descriptorCount = static_cast<uint32_t>(m_scene.materials.size() * 6);

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = static_cast<uint32_t>(m_scene.materials.size());
    ThrowIfFailed(vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_descriptorPool), "Failed to create descriptor pool.");
}

void BistroExteriorClusteredForwardVulkan::CreateDescriptorSets()
{
    m_descriptorSets.resize(m_scene.materials.size());
    std::vector<VkDescriptorSetLayout> layouts(m_scene.materials.size(), m_descriptorSetLayout);
    VkDescriptorSetAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocateInfo.descriptorPool = m_descriptorPool;
    allocateInfo.descriptorSetCount = static_cast<uint32_t>(m_descriptorSets.size());
    allocateInfo.pSetLayouts = layouts.data();
    ThrowIfFailed(vkAllocateDescriptorSets(m_device, &allocateInfo, m_descriptorSets.data()), "Failed to allocate descriptor sets.");

    for (uint32_t materialIndex = 0; materialIndex < m_descriptorSets.size(); ++materialIndex)
    {
        VkDescriptorBufferInfo sceneInfo{};
        sceneInfo.buffer = m_sceneUniformBuffer;
        sceneInfo.offset = 0;
        sceneInfo.range = sizeof(SceneConstants);

        VkDescriptorBufferInfo materialInfo{};
        materialInfo.buffer = m_materialUniformBuffer;
        materialInfo.offset = static_cast<VkDeviceSize>(materialIndex) * m_materialUniformStride;
        materialInfo.range = sizeof(MaterialConstants);

        std::array<VkDescriptorImageInfo, Bistro::TextureSlotCount> imageInfos{};
        for (uint32_t i = 0; i < Bistro::TextureSlotCount; ++i)
        {
            imageInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageInfos[i].imageView = m_textures[m_materialTextureIndices[materialIndex][i]].view;
        }

        VkDescriptorImageInfo samplerInfo{};
        samplerInfo.sampler = m_textureSampler;

        VkDescriptorImageInfo shadowImageInfo{};
        shadowImageInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        shadowImageInfo.imageView = m_shadowDepthImageView;

        VkDescriptorImageInfo shadowSamplerInfo{};
        shadowSamplerInfo.sampler = m_shadowSampler;

        VkDescriptorBufferInfo lightInfo{};
        lightInfo.buffer = m_lightBuffer;
        lightInfo.offset = 0;
        lightInfo.range = sizeof(Bistro::RasterLight) * (std::max<size_t>)(1, m_lightBuild.lights.size());

        VkDescriptorBufferInfo clusterRecordsInfo{};
        clusterRecordsInfo.buffer = m_clusterRecordsBuffer;
        clusterRecordsInfo.offset = 0;
        clusterRecordsInfo.range = sizeof(Bistro::ClusterRecord) * m_clusterAllocatedCount;

        VkDescriptorBufferInfo clusterIndicesInfo{};
        clusterIndicesInfo.buffer = m_clusterLightIndicesBuffer;
        clusterIndicesInfo.offset = 0;
        clusterIndicesInfo.range = sizeof(uint32_t) * m_clusterAllocatedCount * MaxLightsPerCluster;

        VkDescriptorBufferInfo clusterStatsInfo{};
        clusterStatsInfo.buffer = m_clusterStatsBuffer;
        clusterStatsInfo.offset = 0;
        clusterStatsInfo.range = sizeof(Bistro::ClusterStats);

        std::array<VkWriteDescriptorSet, 15> descriptorWrites{};
        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = m_descriptorSets[materialIndex];
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[0].pBufferInfo = &sceneInfo;

        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = m_descriptorSets[materialIndex];
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[1].pBufferInfo = &materialInfo;

        for (uint32_t i = 0; i < Bistro::TextureSlotCount; ++i)
        {
            descriptorWrites[2 + i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[2 + i].dstSet = m_descriptorSets[materialIndex];
            descriptorWrites[2 + i].dstBinding = 2 + i;
            descriptorWrites[2 + i].descriptorCount = 1;
            descriptorWrites[2 + i].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            descriptorWrites[2 + i].pImageInfo = &imageInfos[i];
        }

        descriptorWrites[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[6].dstSet = m_descriptorSets[materialIndex];
        descriptorWrites[6].dstBinding = 6;
        descriptorWrites[6].descriptorCount = 1;
        descriptorWrites[6].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        descriptorWrites[6].pImageInfo = &samplerInfo;

        VkDescriptorBufferInfo* storageInfos[] =
        {
            &lightInfo,
            &clusterRecordsInfo,
            &clusterIndicesInfo,
            &clusterRecordsInfo,
            &clusterIndicesInfo,
            &clusterStatsInfo
        };
        for (uint32_t i = 0; i < 6; ++i)
        {
            descriptorWrites[7 + i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[7 + i].dstSet = m_descriptorSets[materialIndex];
            descriptorWrites[7 + i].dstBinding = 7 + i;
            descriptorWrites[7 + i].descriptorCount = 1;
            descriptorWrites[7 + i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            descriptorWrites[7 + i].pBufferInfo = storageInfos[i];
        }

        descriptorWrites[13].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[13].dstSet = m_descriptorSets[materialIndex];
        descriptorWrites[13].dstBinding = 13;
        descriptorWrites[13].descriptorCount = 1;
        descriptorWrites[13].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        descriptorWrites[13].pImageInfo = &shadowImageInfo;

        descriptorWrites[14].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[14].dstSet = m_descriptorSets[materialIndex];
        descriptorWrites[14].dstBinding = 14;
        descriptorWrites[14].descriptorCount = 1;
        descriptorWrites[14].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        descriptorWrites[14].pImageInfo = &shadowSamplerInfo;
        vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
    }
}

void BistroExteriorClusteredForwardVulkan::UpdateShadowDescriptorSets()
{
    for (VkDescriptorSet descriptorSet : m_descriptorSets)
    {
        VkDescriptorImageInfo shadowImageInfo{};
        shadowImageInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        shadowImageInfo.imageView = m_shadowDepthImageView;

        VkDescriptorImageInfo shadowSamplerInfo{};
        shadowSamplerInfo.sampler = m_shadowSampler;

        std::array<VkWriteDescriptorSet, 2> descriptorWrites{};
        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = descriptorSet;
        descriptorWrites[0].dstBinding = 13;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        descriptorWrites[0].pImageInfo = &shadowImageInfo;

        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = descriptorSet;
        descriptorWrites[1].dstBinding = 14;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        descriptorWrites[1].pImageInfo = &shadowSamplerInfo;

        vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
    }
}

void BistroExteriorClusteredForwardVulkan::InitializeImGui()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;

    ImGui_ImplWin32_Init(m_hwnd);

    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.ApiVersion = VK_API_VERSION_1_0;
    initInfo.Instance = m_instance;
    initInfo.PhysicalDevice = m_physicalDevice;
    initInfo.Device = m_device;
    initInfo.QueueFamily = FindQueueFamilies(m_physicalDevice).graphicsFamily;
    initInfo.Queue = m_graphicsQueue;
    initInfo.DescriptorPoolSize = 64;
    initInfo.MinImageCount = MaxFramesInFlight;
    initInfo.ImageCount = static_cast<uint32_t>(m_swapChainImages.size());
    initInfo.PipelineInfoMain.RenderPass = m_renderPass;
    initInfo.PipelineInfoMain.Subpass = 0;
    initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    ThrowIfFailed(ImGui_ImplVulkan_Init(&initInfo) ? VK_SUCCESS : VK_ERROR_INITIALIZATION_FAILED, "Failed to initialize ImGui Vulkan backend.");
}

void BistroExteriorClusteredForwardVulkan::ShutdownImGui()
{
    if (ImGui::GetCurrentContext())
    {
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
    }
}

void BistroExteriorClusteredForwardVulkan::BuildUI()
{
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2(24.0f, 48.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(390.0f, 450.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Bistro Controls");
    ImGui::PushItemWidth(190.0f);
    ImGui::TextUnformatted("Directional Light");
    ImGui::DragFloat3("Light Direction", m_lightDirection, 0.01f, -1.0f, 1.0f, "%.2f");
    ImGui::ColorEdit3("Light Color", m_lightColor);
    ImGui::SliderFloat("Light Intensity", &m_lightIntensity, 0.0f, 10.0f, "%.2f");
    if (ImGui::Button("Reset Light"))
    {
        ResetLight();
    }
    ImGui::Separator();
    ImGui::TextUnformatted("Camera");
    XMFLOAT3 cameraPosition = m_camera.GetPosition();
    float cameraAngles[] =
    {
        XMConvertToDegrees(m_camera.GetYawRadians()),
        XMConvertToDegrees(m_camera.GetPitchRadians())
    };
    bool cameraChanged = false;
    cameraChanged |= ImGui::DragFloat3("Position", &cameraPosition.x, 0.1f, -10000.0f, 10000.0f, "%.2f");
    if (ImGui::DragFloat2("Yaw / Pitch", cameraAngles, 0.25f, -360.0f, 360.0f, "%.1f deg"))
    {
        cameraAngles[1] = std::clamp(cameraAngles[1], -83.0f, 83.0f);
        cameraChanged = true;
    }
    if (cameraChanged)
    {
        m_camera.Reset(cameraPosition, XMConvertToRadians(cameraAngles[0]), XMConvertToRadians(cameraAngles[1]));
    }
    if (ImGui::Button("Reset Camera View"))
    {
        ResetCameraView();
    }
    ImGui::SliderFloat("Move Speed", &m_baseMoveSpeed, 0.1f, 30.0f, "%.1f");
    ImGui::SliderFloat("Fast Speed", &m_fastMoveSpeed, 0.1f, 80.0f, "%.1f");
    if (ImGui::Button("Reset Camera Speed"))
    {
        ResetCameraSpeeds();
    }
    ImGui::Separator();
    ImGui::TextUnformatted("Many Lights");
    ImGui::Checkbox("Local Lights Enabled", &m_localLightsEnabled);
    ImGui::Checkbox("Emissive Proxy Lights Enabled", &m_emissiveProxyLightsEnabled);
    ImGui::Checkbox("Procedural Proxy Lights Enabled", &m_proceduralProxyLightsEnabled);
    int generatedLightCount = static_cast<int>(m_lightBuild.lights.size());
    ImGui::SliderInt("Active Light Count", &m_activeLightCount, 0, generatedLightCount);
    ImGui::SliderFloat("Local Light Intensity Scale", &m_localLightIntensityScale, 0.0f, 40.0f, "%.2f");
    ImGui::SliderFloat("Light Radius Scale", &m_lightRadiusScale, 0.1f, 8.0f, "%.2f");
    ImGui::Separator();
    ImGui::TextUnformatted("Shadow Map");
    ImGui::Checkbox("Enable Shadows", &m_shadowsEnabled);
    const char* shadowResolutions[] = { "1024", "2048", "4096" };
    if (ImGui::Combo("Resolution", &m_shadowResolutionIndex, shadowResolutions, _countof(shadowResolutions)))
    {
        const uint32_t values[] = { 1024, 2048, 4096 };
        m_shadowResolution = values[std::clamp(m_shadowResolutionIndex, 0, 2)];
        m_shadowResourcesDirty = true;
    }
    ImGui::SliderFloat("Depth Bias", &m_shadowDepthBias, 0.0f, 0.02f, "%.4f");
    ImGui::SliderFloat("Normal Bias", &m_shadowNormalBias, 0.0f, 0.5f, "%.3f");
    ImGui::SliderInt("PCF Radius", &m_shadowPcfRadius, 0, 3);
    ImGui::SliderFloat("Ortho Size", &m_shadowOrthoSize, 10.0f, 200.0f, "%.1f");
    ImGui::SliderFloat("Focus Distance", &m_shadowFocusDistance, 1.0f, 100.0f, "%.1f");
    ImGui::SliderFloat("Depth Range", &m_shadowDepthRange, 20.0f, 500.0f, "%.1f");
    if (ImGui::Button("Reset Shadow"))
    {
        ResetShadowSettings();
    }
#if BISTRO_CLUSTERED_FORWARD
    ImGui::Separator();
    ImGui::TextUnformatted("Clustered Forward");
    ImGui::SliderInt("Z Slices", &m_clusterZSliceCount, 8, static_cast<int>(MaxClusterZSlices));
    ImGui::Text("Cluster Grid: %u x %u x %d", m_clusterTileCountX, m_clusterTileCountY, m_clusterZSliceCount);
    ImGui::Text("Max Lights / Cluster: %u", MaxLightsPerCluster);
#endif
    ImGui::Separator();
    ImGui::TextUnformatted("Debug View");
    const char* debugModes[] =
    {
        "Final",
        "Base Color",
        "World Normal",
        "Normal Texture Raw",
        "Normal Texture Decoded",
        "AO / Roughness / Metallic",
        "NdotL",
        "Specular Texture Raw",
        "Emissive Texture",
        "Vertex Normal",
        "Vertex Tangent",
        "Vertex Bitangent",
        "Tangent Handedness",
        "Normal Mip Level",
        "UV",
        "Alpha",
        "Normal Texture Status",
        "Directional Only",
        "Local Light Contribution",
        "Cluster Light Count",
        "Cluster Slice",
        "Cluster Overflow",
        "Shadow Map Depth",
        "Shadow Factor",
        "Light Space Depth"
    };
    ImGui::Combo("Mode", &m_debugViewMode, debugModes, _countof(debugModes));
    ImGui::Checkbox("Normal Map Y Flip", &m_debugNormalMapYFlip);
    ImGui::SliderInt("Force Normal Mip", &m_debugNormalForceMip, -1, 10);
    ImGui::SliderFloat("Normal Mip Bias", &m_debugNormalMipBias, -4.0f, 4.0f, "%.2f");
    if (ImGui::Button("Reset Normal Sampling"))
    {
        m_debugNormalForceMip = 0;
        m_debugNormalMipBias = 0.0f;
    }
    ImGui::Text("Force -1 uses sampler LOD");
    ImGui::PopItemWidth();
    ImGui::End();

    BuildRendererStatsUI();

    ImGui::Render();
}

void BistroExteriorClusteredForwardVulkan::BuildRendererStatsUI()
{
    int normalTextureCount = 0;
    int normalFallbackCount = 0;
    int normalOnePixelCount = 0;
    for (const Bistro::Material& material : m_scene.materials)
    {
        const size_t materialIndex = &material - m_scene.materials.data();
        if (!material.textures[Bistro::TextureSlotNormal].empty())
        {
            ++normalTextureCount;
        }
        if (materialIndex < m_materialTextureIndices.size())
        {
            const GpuTexture& normalTexture = m_textures[m_materialTextureIndices[materialIndex][Bistro::TextureSlotNormal]];
            normalFallbackCount += normalTexture.fallback ? 1 : 0;
            normalOnePixelCount += normalTexture.width <= 1 || normalTexture.height <= 1 ? 1 : 0;
        }
    }

    uint64_t primitiveCount = 0;
    uint64_t submittedIndexCount = 0;
    for (const Bistro::DrawItem& draw : m_scene.draws)
    {
        submittedIndexCount += draw.indexCount;
        primitiveCount += draw.indexCount / 3;
    }

    const ImGuiIO& io = ImGui::GetIO();
    const float fps = io.Framerate;
    const float frameTimeMs = fps > 0.0f ? 1000.0f / fps : 0.0f;

    ImGui::SetNextWindowPos(ImVec2(430.0f, 48.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(300.0f, 300.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Renderer Stats");
    ImGui::TextUnformatted("Frame");
    ImGui::Text("API: Vulkan");
    ImGui::Text("FPS: %.1f", fps);
    ImGui::Text("Frame Time: %.3f ms", frameTimeMs);
    uint32_t outputWidth = m_swapChainExtent.width;
    uint32_t outputHeight = m_swapChainExtent.height;
    if (Bistro::DrawResolutionCombo(m_swapChainExtent.width, m_swapChainExtent.height, outputWidth, outputHeight))
    {
        SetOutputResolution(outputWidth, outputHeight);
    }
    ImGui::Text("Output: %ux%u", m_swapChainExtent.width, m_swapChainExtent.height);
    ImGui::Separator();
    ImGui::TextUnformatted("Scene");
    ImGui::Text("Materials: %zu", m_scene.materials.size());
    ImGui::Text("Draw Calls: %zu", m_scene.draws.size());
    ImGui::Text("Vertices: %zu", m_scene.vertices.size());
    ImGui::Text("Indices: %zu", m_scene.indices.size());
    ImGui::Text("Submitted Indices: %llu", static_cast<unsigned long long>(submittedIndexCount));
    ImGui::Text("Primitives: %llu", static_cast<unsigned long long>(primitiveCount));
    ImGui::Text("Textures: %zu", m_textures.size());
    ImGui::Separator();
    ImGui::TextUnformatted("Many Lights");
    ImGui::Text("Generated Lights: %zu", m_lightBuild.lights.size());
    ImGui::Text("Active Lights: %d", m_activeLightCount);
    ImGui::Text("Emissive Proxy Lights: %u", m_lightBuild.emissiveProxyCount);
    ImGui::Text("Procedural Proxy Lights: %u", m_lightBuild.proceduralProxyCount);
    ImGui::Separator();
    ImGui::TextUnformatted("Shadow");
    ImGui::Text("Enabled: %s", m_shadowsEnabled ? "Yes" : "No");
    ImGui::Text("Resolution: %u x %u", m_shadowResolution, m_shadowResolution);
    ImGui::Text("Bias: %.4f / Normal %.3f", m_shadowDepthBias, m_shadowNormalBias);
    ImGui::Text("PCF Radius: %d", m_shadowPcfRadius);
#if BISTRO_CLUSTERED_FORWARD
    ImGui::Separator();
    ImGui::TextUnformatted("Clusters");
    ImGui::Text("Grid: %u x %u x %d", m_clusterTileCountX, m_clusterTileCountY, m_clusterZSliceCount);
    ImGui::Text("Total Clusters: %u", m_clusterTileCountX * m_clusterTileCountY * static_cast<uint32_t>(m_clusterZSliceCount));
    ImGui::Text("Max Lights / Cluster: %u", MaxLightsPerCluster);
    ImGui::Text("Active Clusters: %u", m_clusterStats.activeClusters);
    ImGui::Text("Assigned Light Refs: %u", m_clusterStats.assignedLightRefs);
    ImGui::Text("Max Observed Lights / Cluster: %u", m_clusterStats.maxLightsInCluster);
    ImGui::Text("Overflow Clusters: %u", m_clusterStats.overflowClusters);
#endif
    ImGui::Separator();
    ImGui::TextUnformatted("Texture Diagnostics");
    ImGui::Text("Normal Maps: %d / %zu", normalTextureCount, m_scene.materials.size());
    ImGui::Text("Normal Fallbacks: %d", normalFallbackCount);
    ImGui::Text("Normal 1x1 SRVs: %d", normalOnePixelCount);
    ImGui::End();
}

void BistroExteriorClusteredForwardVulkan::ResetLight()
{
    m_lightDirection[0] = -0.35f;
    m_lightDirection[1] = -0.8f;
    m_lightDirection[2] = 0.45f;
    m_lightColor[0] = 1.0f;
    m_lightColor[1] = 0.96f;
    m_lightColor[2] = 0.88f;
    m_lightIntensity = 4.0f;
}

void BistroExteriorClusteredForwardVulkan::ResetCameraView()
{
    m_camera.Reset(m_defaultCameraPosition, m_defaultCameraYaw, m_defaultCameraPitch);
}

void BistroExteriorClusteredForwardVulkan::ResetCameraSpeeds()
{
    m_baseMoveSpeed = 17.0f;
    m_fastMoveSpeed = 58.2f;
}

void BistroExteriorClusteredForwardVulkan::ResetShadowSettings()
{
    m_shadowsEnabled = true;
    m_shadowDepthBias = 0.002f;
    m_shadowNormalBias = 0.05f;
    m_shadowPcfRadius = 1;
    m_shadowOrthoSize = 50.0f;
    m_shadowFocusDistance = 25.0f;
    m_shadowDepthRange = 160.0f;
}

void BistroExteriorClusteredForwardVulkan::CreateCommandBuffers()
{
    m_commandBuffers.resize(m_framebuffers.size());
    VkCommandBufferAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocateInfo.commandPool = m_commandPool;
    allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocateInfo.commandBufferCount = static_cast<uint32_t>(m_commandBuffers.size());
    ThrowIfFailed(vkAllocateCommandBuffers(m_device, &allocateInfo, m_commandBuffers.data()), "Failed to allocate command buffers.");
}

void BistroExteriorClusteredForwardVulkan::RecordCommandBuffer(uint32_t imageIndex)
{
    VkCommandBuffer commandBuffer = m_commandBuffers[imageIndex];
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    ThrowIfFailed(vkBeginCommandBuffer(commandBuffer, &beginInfo), "Failed to begin command buffer.");

    {
        VkClearValue shadowClear{};
        shadowClear.depthStencil = { 1.0f, 0 };

        VkRenderPassBeginInfo shadowPassInfo{};
        shadowPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        shadowPassInfo.renderPass = m_shadowRenderPass;
        shadowPassInfo.framebuffer = m_shadowFramebuffer;
        shadowPassInfo.renderArea.extent = { m_shadowResolution, m_shadowResolution };
        shadowPassInfo.clearValueCount = 1;
        shadowPassInfo.pClearValues = &shadowClear;

        vkCmdBeginRenderPass(commandBuffer, &shadowPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_shadowPipeline);

        VkViewport shadowViewport{};
        shadowViewport.width = static_cast<float>(m_shadowResolution);
        shadowViewport.height = static_cast<float>(m_shadowResolution);
        shadowViewport.minDepth = 0.0f;
        shadowViewport.maxDepth = 1.0f;
        vkCmdSetViewport(commandBuffer, 0, 1, &shadowViewport);

        VkRect2D shadowScissor{};
        shadowScissor.extent = { m_shadowResolution, m_shadowResolution };
        vkCmdSetScissor(commandBuffer, 0, 1, &shadowScissor);

        VkBuffer vertexBuffers[] = { m_vertexBuffer };
        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(commandBuffer, m_indexBuffer, 0, VK_INDEX_TYPE_UINT32);

        for (const Bistro::DrawItem& draw : m_scene.draws)
        {
            const uint32_t materialIndex = (std::min)(draw.materialIndex, static_cast<uint32_t>(m_descriptorSets.size() - 1));
            VkDescriptorSet descriptorSet = m_descriptorSets[materialIndex];
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
            vkCmdDrawIndexed(commandBuffer, draw.indexCount, 1, draw.startIndex, draw.baseVertex, 0);
        }

        vkCmdEndRenderPass(commandBuffer);

        VkImageMemoryBarrier shadowBarrier{};
        shadowBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        shadowBarrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        shadowBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        shadowBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        shadowBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        shadowBarrier.image = m_shadowDepthImage;
        shadowBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        shadowBarrier.subresourceRange.baseMipLevel = 0;
        shadowBarrier.subresourceRange.levelCount = 1;
        shadowBarrier.subresourceRange.baseArrayLayer = 0;
        shadowBarrier.subresourceRange.layerCount = 1;
        shadowBarrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        shadowBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0,
            0,
            nullptr,
            0,
            nullptr,
            1,
            &shadowBarrier);
    }

    RecordClusterBuild(commandBuffer);

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = { { 0.0f, 0.2f, 0.4f, 1.0f } };
    clearValues[1].depthStencil = { 1.0f, 0 };

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = m_renderPass;
    renderPassInfo.framebuffer = m_framebuffers[imageIndex];
    renderPassInfo.renderArea.extent = m_swapChainExtent;
    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_graphicsPipeline);
    VkBuffer vertexBuffers[] = { m_vertexBuffer };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(commandBuffer, m_indexBuffer, 0, VK_INDEX_TYPE_UINT32);

    for (const Bistro::DrawItem& draw : m_scene.draws)
    {
        const uint32_t materialIndex = (std::min)(draw.materialIndex, static_cast<uint32_t>(m_descriptorSets.size() - 1));
        VkDescriptorSet descriptorSet = m_descriptorSets[materialIndex];
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
        vkCmdDrawIndexed(commandBuffer, draw.indexCount, 1, draw.startIndex, draw.baseVertex, 0);
    }

    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);

    vkCmdEndRenderPass(commandBuffer);
    ThrowIfFailed(vkEndCommandBuffer(commandBuffer), "Failed to record command buffer.");
}

void BistroExteriorClusteredForwardVulkan::RecordClusterBuild(VkCommandBuffer commandBuffer)
{
#if BISTRO_CLUSTERED_FORWARD
    VkDescriptorSet descriptorSet = m_descriptorSets.empty() ? VK_NULL_HANDLE : m_descriptorSets[0];
    if (descriptorSet == VK_NULL_HANDLE)
    {
        return;
    }

    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_clusterResetPipeline);
    vkCmdDispatch(commandBuffer, 1, 1, 1);

    VkBufferMemoryBarrier resetBarrier{};
    resetBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    resetBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    resetBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    resetBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    resetBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    resetBarrier.buffer = m_clusterStatsBuffer;
    resetBarrier.offset = 0;
    resetBarrier.size = sizeof(Bistro::ClusterStats);
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1, &resetBarrier, 0, nullptr);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_clusterBuildPipeline);
    vkCmdDispatch(commandBuffer, (m_clusterTileCountX + 3) / 4, (m_clusterTileCountY + 3) / 4, (static_cast<uint32_t>(m_clusterZSliceCount) + 3) / 4);

    std::array<VkBufferMemoryBarrier, 3> barriers{};
    VkBuffer buffers[] = { m_clusterRecordsBuffer, m_clusterLightIndicesBuffer, m_clusterStatsBuffer };
    VkDeviceSize sizes[] =
    {
        sizeof(Bistro::ClusterRecord) * m_clusterAllocatedCount,
        sizeof(uint32_t) * m_clusterAllocatedCount * MaxLightsPerCluster,
        sizeof(Bistro::ClusterStats)
    };
    for (uint32_t i = 0; i < static_cast<uint32_t>(barriers.size()); ++i)
    {
        barriers[i].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        barriers[i].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barriers[i].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_HOST_READ_BIT;
        barriers[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[i].buffer = buffers[i];
        barriers[i].offset = 0;
        barriers[i].size = sizes[i];
    }
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, static_cast<uint32_t>(barriers.size()), barriers.data(), 0, nullptr);
#endif
}

void BistroExteriorClusteredForwardVulkan::CreateSyncObjects()
{
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (uint32_t i = 0; i < MaxFramesInFlight; ++i)
    {
        ThrowIfFailed(vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_imageAvailableSemaphores[i]), "Failed to create image available semaphore.");
        ThrowIfFailed(vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_renderFinishedSemaphores[i]), "Failed to create render finished semaphore.");
        ThrowIfFailed(vkCreateFence(m_device, &fenceInfo, nullptr, &m_inFlightFences[i]), "Failed to create frame fence.");
    }
}

void BistroExteriorClusteredForwardVulkan::UpdateUniformBuffer()
{
    auto now = std::chrono::steady_clock::now();
    float delta = std::chrono::duration<float>(now - m_lastUpdate).count();
    m_lastUpdate = now;
    delta = (std::min)(delta, 0.05f);
    m_camera.SetMoveSpeeds(m_baseMoveSpeed, m_fastMoveSpeed);
    m_camera.Update(delta);

    float aspectRatio = static_cast<float>(m_swapChainExtent.width) / static_cast<float>(m_swapChainExtent.height);
    XMMATRIX view = m_camera.GetViewMatrix();
    XMMATRIX projection = XMMatrixPerspectiveFovLH(XMConvertToRadians(60.0f), aspectRatio, 0.1f, 1000.0f);
    projection.r[1] = XMVectorNegate(projection.r[1]);

    SceneConstants constants{};
    XMStoreFloat4x4(&constants.viewProjection, view * projection);
    XMStoreFloat4x4(&constants.view, view);
    XMStoreFloat4x4(&constants.projection, projection);
    XMFLOAT3 cameraPosition = m_camera.GetPosition();
    constants.cameraPosition = XMFLOAT4(cameraPosition.x, cameraPosition.y, cameraPosition.z, 1.0f);
    XMVECTOR lightDirection = XMVectorSet(m_lightDirection[0], m_lightDirection[1], m_lightDirection[2], 0.0f);
    if (XMVectorGetX(XMVector3LengthSq(lightDirection)) < 0.0001f)
    {
        lightDirection = XMVectorSet(-0.35f, -0.8f, 0.45f, 0.0f);
    }
    lightDirection = XMVector3Normalize(lightDirection);
    XMFLOAT3 normalizedLightDirection;
    XMStoreFloat3(&normalizedLightDirection, lightDirection);
    m_lightDirection[0] = normalizedLightDirection.x;
    m_lightDirection[1] = normalizedLightDirection.y;
    m_lightDirection[2] = normalizedLightDirection.z;
    constants.lightDirection = XMFLOAT4(m_lightDirection[0], m_lightDirection[1], m_lightDirection[2], 0.0f);
    constants.lightColor = XMFLOAT4(m_lightColor[0], m_lightColor[1], m_lightColor[2], m_lightIntensity);
    constants.debugOptions = XMFLOAT4(static_cast<float>(m_debugViewMode), m_debugNormalMapYFlip ? 1.0f : 0.0f, static_cast<float>(m_debugNormalForceMip), m_debugNormalMipBias);
    m_activeLightCount = std::clamp(m_activeLightCount, 0, static_cast<int>(m_lightBuild.lights.size()));
    m_clusterZSliceCount = std::clamp(m_clusterZSliceCount, 1, static_cast<int>(MaxClusterZSlices));
    constants.localLightOptions = XMFLOAT4(m_localLightsEnabled ? 1.0f : 0.0f, m_emissiveProxyLightsEnabled ? 1.0f : 0.0f, m_proceduralProxyLightsEnabled ? 1.0f : 0.0f, static_cast<float>(m_activeLightCount));
    constants.clusterOptions = XMFLOAT4(static_cast<float>(m_swapChainExtent.width), static_cast<float>(m_swapChainExtent.height), static_cast<float>(ClusterTileSize), static_cast<float>(m_clusterZSliceCount));
    constants.clusterOptions2 = XMFLOAT4(0.1f, 1000.0f, static_cast<float>(MaxLightsPerCluster), m_localLightIntensityScale);
    constants.clusterOptions3 = XMFLOAT4(m_lightRadiusScale, static_cast<float>(m_clusterTileCountX), static_cast<float>(m_clusterTileCountY), static_cast<float>(m_clusterTileCountX * m_clusterTileCountY * static_cast<uint32_t>((std::max)(1, m_clusterZSliceCount))));
    constants.shadowOptions = XMFLOAT4(m_shadowsEnabled ? 1.0f : 0.0f, m_shadowDepthBias, m_shadowNormalBias, static_cast<float>(m_shadowPcfRadius));

    const float yaw = m_camera.GetYawRadians();
    const float pitch = m_camera.GetPitchRadians();
    XMVECTOR cameraForward = XMVectorSet(std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch), 0.0f);
    cameraForward = XMVector3Normalize(cameraForward);
    XMVECTOR cameraPositionVector = XMLoadFloat3(&cameraPosition);
    XMVECTOR focus = cameraPositionVector + cameraForward * m_shadowFocusDistance;
    XMVECTOR lightEye = focus - lightDirection * (m_shadowDepthRange * 0.5f);
    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    if (std::abs(XMVectorGetX(XMVector3Dot(lightDirection, up))) > 0.95f)
    {
        up = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
    }
    XMMATRIX lightView = XMMatrixLookAtLH(lightEye, focus, up);
    XMMATRIX lightProjection = XMMatrixOrthographicLH(m_shadowOrthoSize, m_shadowOrthoSize, 0.0f, m_shadowDepthRange);
    XMStoreFloat4x4(&constants.lightViewProjection, lightView * lightProjection);

    void* data = nullptr;
    vkMapMemory(m_device, m_sceneUniformBufferMemory, 0, sizeof(constants), 0, &data);
    memcpy(data, &constants, sizeof(constants));
    vkUnmapMemory(m_device, m_sceneUniformBufferMemory);
}

void BistroExteriorClusteredForwardVulkan::ReadbackClusterStats()
{
#if BISTRO_CLUSTERED_FORWARD
    if (m_clusterStatsBufferMemory == VK_NULL_HANDLE)
    {
        return;
    }

    void* data = nullptr;
    if (vkMapMemory(m_device, m_clusterStatsBufferMemory, 0, sizeof(Bistro::ClusterStats), 0, &data) == VK_SUCCESS)
    {
        memcpy(&m_clusterStats, data, sizeof(m_clusterStats));
        vkUnmapMemory(m_device, m_clusterStatsBufferMemory);
    }
#else
    m_clusterStats = {};
#endif
}

bool BistroExteriorClusteredForwardVulkan::IsDeviceSuitable(VkPhysicalDevice device) const
{
    QueueFamilyIndices indices = FindQueueFamilies(device);
    if (!indices.IsComplete())
    {
        return false;
    }

    uint32_t extensionCount = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

    std::set<std::string> requiredExtensions(DeviceExtensions.begin(), DeviceExtensions.end());
    for (const VkExtensionProperties& extension : availableExtensions)
    {
        requiredExtensions.erase(extension.extensionName);
    }

    if (!requiredExtensions.empty())
    {
        return false;
    }

    SwapChainSupport support = QuerySwapChainSupport(device);
    return !support.formats.empty() && !support.presentModes.empty();
}

BistroExteriorClusteredForwardVulkan::QueueFamilyIndices BistroExteriorClusteredForwardVulkan::FindQueueFamilies(VkPhysicalDevice device) const
{
    QueueFamilyIndices indices;
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    for (uint32_t i = 0; i < queueFamilyCount; ++i)
    {
        if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
        {
            indices.graphicsFamily = i;
        }

        VkBool32 presentSupport = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_surface, &presentSupport);
        if (presentSupport)
        {
            indices.presentFamily = i;
        }

        if (indices.IsComplete())
        {
            break;
        }
    }

    return indices;
}

BistroExteriorClusteredForwardVulkan::SwapChainSupport BistroExteriorClusteredForwardVulkan::QuerySwapChainSupport(VkPhysicalDevice device) const
{
    SwapChainSupport support;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, m_surface, &support.capabilities);

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_surface, &formatCount, nullptr);
    support.formats.resize(formatCount);
    if (formatCount > 0)
    {
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_surface, &formatCount, support.formats.data());
    }

    uint32_t presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_surface, &presentModeCount, nullptr);
    support.presentModes.resize(presentModeCount);
    if (presentModeCount > 0)
    {
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_surface, &presentModeCount, support.presentModes.data());
    }

    return support;
}

VkSurfaceFormatKHR BistroExteriorClusteredForwardVulkan::ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) const
{
    for (const VkSurfaceFormatKHR& format : formats)
    {
        if (format.format == VK_FORMAT_B8G8R8A8_UNORM && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            return format;
        }
    }

    return formats[0];
}

VkPresentModeKHR BistroExteriorClusteredForwardVulkan::ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& presentModes) const
{
    for (VkPresentModeKHR presentMode : presentModes)
    {
        if (presentMode == VK_PRESENT_MODE_MAILBOX_KHR)
        {
            return presentMode;
        }
    }

    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D BistroExteriorClusteredForwardVulkan::ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) const
{
    if (capabilities.currentExtent.width != UINT32_MAX)
    {
        return capabilities.currentExtent;
    }

    VkExtent2D actualExtent = { m_width, m_height };
    actualExtent.width = std::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
    actualExtent.height = std::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
    return actualExtent;
}

uint32_t BistroExteriorClusteredForwardVulkan::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const
{
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memoryProperties);

    for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i)
    {
        if ((typeFilter & (1 << i)) && (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties)
        {
            return i;
        }
    }

    throw std::runtime_error("Failed to find suitable Vulkan memory type.");
}

void BistroExteriorClusteredForwardVulkan::CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& memory)
{
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ThrowIfFailed(vkCreateBuffer(m_device, &bufferInfo, nullptr, &buffer), "Failed to create Vulkan buffer.");

    VkMemoryRequirements memoryRequirements{};
    vkGetBufferMemoryRequirements(m_device, buffer, &memoryRequirements);

    VkMemoryAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.allocationSize = memoryRequirements.size;
    allocateInfo.memoryTypeIndex = FindMemoryType(memoryRequirements.memoryTypeBits, properties);
    ThrowIfFailed(vkAllocateMemory(m_device, &allocateInfo, nullptr, &memory), "Failed to allocate Vulkan buffer memory.");
    vkBindBufferMemory(m_device, buffer, memory, 0);
}

void BistroExteriorClusteredForwardVulkan::CreateImage(uint32_t width, uint32_t height, uint32_t mipLevels, VkFormat format, VkImageUsageFlags usage, VkImage& image, VkDeviceMemory& memory)
{
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = mipLevels;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ThrowIfFailed(vkCreateImage(m_device, &imageInfo, nullptr, &image), "Failed to create Vulkan image.");

    VkMemoryRequirements memoryRequirements{};
    vkGetImageMemoryRequirements(m_device, image, &memoryRequirements);

    VkMemoryAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.allocationSize = memoryRequirements.size;
    allocateInfo.memoryTypeIndex = FindMemoryType(memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    ThrowIfFailed(vkAllocateMemory(m_device, &allocateInfo, nullptr, &memory), "Failed to allocate Vulkan image memory.");
    vkBindImageMemory(m_device, image, memory, 0);
}

VkImageView BistroExteriorClusteredForwardVulkan::CreateImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, uint32_t mipLevels) const
{
    VkImageViewCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    createInfo.image = image;
    createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    createInfo.format = format;
    createInfo.subresourceRange.aspectMask = aspectFlags;
    createInfo.subresourceRange.baseMipLevel = 0;
    createInfo.subresourceRange.levelCount = mipLevels;
    createInfo.subresourceRange.baseArrayLayer = 0;
    createInfo.subresourceRange.layerCount = 1;

    VkImageView imageView = VK_NULL_HANDLE;
    ThrowIfFailed(vkCreateImageView(m_device, &createInfo, nullptr, &imageView), "Failed to create Vulkan image view.");
    return imageView;
}

VkCommandBuffer BistroExteriorClusteredForwardVulkan::BeginSingleTimeCommands() const
{
    VkCommandBufferAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocateInfo.commandPool = m_commandPool;
    allocateInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    ThrowIfFailed(vkAllocateCommandBuffers(m_device, &allocateInfo, &commandBuffer), "Failed to allocate one-time command buffer.");

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    ThrowIfFailed(vkBeginCommandBuffer(commandBuffer, &beginInfo), "Failed to begin one-time command buffer.");
    return commandBuffer;
}

void BistroExteriorClusteredForwardVulkan::EndSingleTimeCommands(VkCommandBuffer commandBuffer) const
{
    ThrowIfFailed(vkEndCommandBuffer(commandBuffer), "Failed to end one-time command buffer.");

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    ThrowIfFailed(vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE), "Failed to submit one-time command buffer.");
    vkQueueWaitIdle(m_graphicsQueue);
    vkFreeCommandBuffers(m_device, m_commandPool, 1, &commandBuffer);
}

void BistroExteriorClusteredForwardVulkan::CopyBufferToImage(VkBuffer buffer, VkImage image, const Bistro::TextureData& texture) const
{
    VkCommandBuffer commandBuffer = BeginSingleTimeCommands();

    std::vector<VkBufferImageCopy> regions(texture.mipLevels);
    for (uint32_t mipIndex = 0; mipIndex < texture.mipLevels; ++mipIndex)
    {
        const Bistro::TextureMip& mip = texture.mips[mipIndex];
        VkBufferImageCopy& region = regions[mipIndex];
        region.bufferOffset = static_cast<VkDeviceSize>(mip.offset);
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = mipIndex;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = { mip.width, mip.height, 1 };
    }
    vkCmdCopyBufferToImage(commandBuffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, static_cast<uint32_t>(regions.size()), regions.data());

    EndSingleTimeCommands(commandBuffer);
}

void BistroExteriorClusteredForwardVulkan::TransitionImageLayout(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels) const
{
    VkCommandBuffer commandBuffer = BeginSingleTimeCommands();

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = mipLevels;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    else
    {
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    }

    vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    EndSingleTimeCommands(commandBuffer);
}

std::vector<char> BistroExteriorClusteredForwardVulkan::ReadFile(const std::wstring& path) const
{
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open())
    {
        throw std::runtime_error("Failed to open shader file.");
    }

    size_t fileSize = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    return buffer;
}

VkShaderModule BistroExteriorClusteredForwardVulkan::CreateShaderModule(const std::vector<char>& code) const
{
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shaderModule = VK_NULL_HANDLE;
    ThrowIfFailed(vkCreateShaderModule(m_device, &createInfo, nullptr, &shaderModule), "Failed to create shader module.");
    return shaderModule;
}

std::wstring BistroExteriorClusteredForwardVulkan::GetExecutableDirectory() const
{
    wchar_t path[MAX_PATH]{};
    DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (length == 0 || length == MAX_PATH)
    {
        throw std::runtime_error("Failed to resolve executable path.");
    }

    wchar_t* lastSlash = wcsrchr(path, L'\\');
    if (lastSlash)
    {
        *(lastSlash + 1) = L'\0';
    }

    return path;
}

void BistroExteriorClusteredForwardVulkan::ThrowIfFailed(VkResult result, const char* message) const
{
    if (result != VK_SUCCESS)
    {
        throw std::runtime_error(message);
    }
}
