#include "ImGuiLightingVulkan.h"

#include "imgui.h"
#include "backends/imgui_impl_vulkan.h"
#include "backends/imgui_impl_win32.h"

#include <algorithm>
#include <fstream>
#include <set>
#include <stdexcept>

using namespace DirectX;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

namespace
{
    constexpr uint32_t HelmetIndexCount = 70074;
    constexpr uint32_t HelmetVertexCount = 70074;
    constexpr size_t IndexOffset = 0;
    constexpr size_t PositionOffset = 280296;
    constexpr size_t NormalOffset = 1121184;
    constexpr size_t TangentOffset = 1962072;
    constexpr size_t TexcoordOffset = 3083256;

    const std::array<const wchar_t*, ImGuiLightingVulkan::TextureCount> TextureNames =
    {
        L"SciFiHelmet_BaseColor.png",
        L"SciFiHelmet_MetallicRoughness.png",
        L"SciFiHelmet_Normal.png",
        L"SciFiHelmet_AmbientOcclusion.png",
    };
    const std::vector<const char*> DeviceExtensions =
    {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    };
}

bool ImGuiLightingVulkan::QueueFamilyIndices::IsComplete() const
{
    return graphicsFamily != UINT32_MAX && presentFamily != UINT32_MAX;
}

ImGuiLightingVulkan::ImGuiLightingVulkan(uint32_t width, uint32_t height, const wchar_t* title) :
    m_width(width),
    m_height(height),
    m_title(title)
{
}

ImGuiLightingVulkan::~ImGuiLightingVulkan()
{
    Cleanup();
}

int ImGuiLightingVulkan::Run(HINSTANCE instance, int showCommand)
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

LRESULT CALLBACK ImGuiLightingVulkan::WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hwnd, message, wParam, lParam))
    {
        return true;
    }

    if (message == WM_DESTROY)
    {
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hwnd, message, wParam, lParam);
}

void ImGuiLightingVulkan::InitWindow(HINSTANCE instance, int showCommand)
{
    m_instanceHandle = instance;

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(WNDCLASSEXW);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    windowClass.lpszClassName = L"ImGuiLightingVulkanWindowClass";
    RegisterClassExW(&windowClass);

    RECT rect = { 0, 0, static_cast<LONG>(m_width), static_cast<LONG>(m_height) };
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);

    m_hwnd = CreateWindowExW(0, windowClass.lpszClassName, m_title.c_str(), WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr, instance, nullptr);
    if (!m_hwnd)
    {
        throw std::runtime_error("Failed to create Win32 window.");
    }

    ShowWindow(m_hwnd, showCommand);
}

void ImGuiLightingVulkan::InitVulkan()
{
    LoadModel();
    CreateInstance();
    CreateSurface();
    PickPhysicalDevice();
    CreateLogicalDevice();
    CreateSwapChain();
    CreateImageViews();
    CreateRenderPass();
    CreateDescriptorSetLayout();
    CreateGraphicsPipeline();
    CreateDepthResources();
    CreateFramebuffers();
    CreateCommandPool();
    CreateVertexBuffer();
    CreateIndexBuffer();
    CreateTextureImages();
    CreateTextureSampler();
    CreateUniformBuffer();
    CreateDescriptorPool();
    CreateDescriptorSet();
    InitializeImGui();
    CreateCommandBuffers();
    CreateSyncObjects();
}

void ImGuiLightingVulkan::MainLoop()
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

void ImGuiLightingVulkan::Render()
{
    vkWaitForFences(m_device, 1, &m_inFlightFences[m_currentFrame], VK_TRUE, UINT64_MAX);
    BuildUI();
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
    ThrowIfFailed(vkResetCommandBuffer(m_commandBuffers[imageIndex], 0), "Failed to reset command buffer.");
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

void ImGuiLightingVulkan::WaitIdle()
{
    if (m_device != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(m_device);
    }
}

void ImGuiLightingVulkan::Cleanup()
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
        if (m_uniformBuffer) vkDestroyBuffer(m_device, m_uniformBuffer, nullptr);
        if (m_uniformBufferMemory) vkFreeMemory(m_device, m_uniformBufferMemory, nullptr);
        if (m_textureSampler) vkDestroySampler(m_device, m_textureSampler, nullptr);
        for (uint32_t i = 0; i < TextureCount; ++i)
        {
            if (m_textureImageViews[i]) vkDestroyImageView(m_device, m_textureImageViews[i], nullptr);
            if (m_textureImages[i]) vkDestroyImage(m_device, m_textureImages[i], nullptr);
            if (m_textureImageMemories[i]) vkFreeMemory(m_device, m_textureImageMemories[i], nullptr);
        }
        if (m_indexBuffer) vkDestroyBuffer(m_device, m_indexBuffer, nullptr);
        if (m_indexBufferMemory) vkFreeMemory(m_device, m_indexBufferMemory, nullptr);
        if (m_vertexBuffer) vkDestroyBuffer(m_device, m_vertexBuffer, nullptr);
        if (m_vertexBufferMemory) vkFreeMemory(m_device, m_vertexBufferMemory, nullptr);
        if (m_commandPool) vkDestroyCommandPool(m_device, m_commandPool, nullptr);

        for (VkFramebuffer framebuffer : m_framebuffers)
        {
            vkDestroyFramebuffer(m_device, framebuffer, nullptr);
        }
        if (m_depthImageView) vkDestroyImageView(m_device, m_depthImageView, nullptr);
        if (m_depthImage) vkDestroyImage(m_device, m_depthImage, nullptr);
        if (m_depthImageMemory) vkFreeMemory(m_device, m_depthImageMemory, nullptr);
        if (m_graphicsPipeline) vkDestroyPipeline(m_device, m_graphicsPipeline, nullptr);
        if (m_pipelineLayout) vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
        if (m_descriptorSetLayout) vkDestroyDescriptorSetLayout(m_device, m_descriptorSetLayout, nullptr);
        if (m_renderPass) vkDestroyRenderPass(m_device, m_renderPass, nullptr);

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

void ImGuiLightingVulkan::CreateInstance()
{
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "ImGuiLighting Vulkan";
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

void ImGuiLightingVulkan::CreateSurface()
{
    VkWin32SurfaceCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    createInfo.hinstance = m_instanceHandle;
    createInfo.hwnd = m_hwnd;
    ThrowIfFailed(vkCreateWin32SurfaceKHR(m_instance, &createInfo, nullptr, &m_surface), "Failed to create Win32 surface.");
}

void ImGuiLightingVulkan::PickPhysicalDevice()
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

void ImGuiLightingVulkan::CreateLogicalDevice()
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

    VkPhysicalDeviceFeatures deviceFeatures{};
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

void ImGuiLightingVulkan::CreateSwapChain()
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

void ImGuiLightingVulkan::CreateImageViews()
{
    m_swapChainImageViews.resize(m_swapChainImages.size());
    for (size_t i = 0; i < m_swapChainImages.size(); ++i)
    {
        m_swapChainImageViews[i] = CreateImageView(m_swapChainImages[i], m_swapChainImageFormat, VK_IMAGE_ASPECT_COLOR_BIT);
    }
}

void ImGuiLightingVulkan::CreateRenderPass()
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

void ImGuiLightingVulkan::CreateDescriptorSetLayout()
{
    std::array<VkDescriptorSetLayoutBinding, 6> bindings{};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    for (uint32_t i = 0; i < TextureCount; ++i)
    {
        bindings[1 + i].binding = 1 + i;
        bindings[1 + i].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        bindings[1 + i].descriptorCount = 1;
        bindings[1 + i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }

    bindings[5].binding = 5;
    bindings[5].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    bindings[5].descriptorCount = 1;
    bindings[5].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    ThrowIfFailed(vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_descriptorSetLayout), "Failed to create descriptor set layout.");
}

void ImGuiLightingVulkan::CreateGraphicsPipeline()
{
    std::wstring shaderDirectory = GetExecutableDirectory();
    VkShaderModule vertexShaderModule = CreateShaderModule(ReadFile(shaderDirectory + L"ImGuiLighting.vs.spv"));
    VkShaderModule pixelShaderModule = CreateShaderModule(ReadFile(shaderDirectory + L"ImGuiLighting.ps.spv"));

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
    bindingDescription.stride = sizeof(Vertex);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 4> attributeDescriptions{};
    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[0].offset = offsetof(Vertex, position);
    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[1].offset = offsetof(Vertex, normal);
    attributeDescriptions[2].binding = 0;
    attributeDescriptions[2].location = 2;
    attributeDescriptions[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attributeDescriptions[2].offset = offsetof(Vertex, tangent);
    attributeDescriptions[3].binding = 0;
    attributeDescriptions[3].location = 3;
    attributeDescriptions[3].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[3].offset = offsetof(Vertex, texcoord);

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
}

void ImGuiLightingVulkan::CreateDepthResources()
{
    CreateImage(m_swapChainExtent.width, m_swapChainExtent.height, DepthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, m_depthImage, m_depthImageMemory);
    m_depthImageView = CreateImageView(m_depthImage, DepthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);
}

void ImGuiLightingVulkan::CreateFramebuffers()
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

void ImGuiLightingVulkan::CreateCommandPool()
{
    QueueFamilyIndices queueFamilyIndices = FindQueueFamilies(m_physicalDevice);
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily;
    ThrowIfFailed(vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool), "Failed to create command pool.");
}

void ImGuiLightingVulkan::LoadModel()
{
    std::wstring path = GetExecutableDirectory() + L"..\\..\\..\\..\\Assets\\SciFiHelmet\\SciFiHelmet.bin";
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
    {
        throw std::runtime_error("Failed to open SciFiHelmet.bin.");
    }

    const size_t fileSize = static_cast<size_t>(file.tellg());
    std::vector<uint8_t> data(fileSize);
    file.seekg(0);
    file.read(reinterpret_cast<char*>(data.data()), fileSize);

    const auto* indices = reinterpret_cast<const uint32_t*>(data.data() + IndexOffset);
    const auto* positions = reinterpret_cast<const XMFLOAT3*>(data.data() + PositionOffset);
    const auto* normals = reinterpret_cast<const XMFLOAT3*>(data.data() + NormalOffset);
    const auto* tangents = reinterpret_cast<const XMFLOAT4*>(data.data() + TangentOffset);
    const auto* texcoords = reinterpret_cast<const XMFLOAT2*>(data.data() + TexcoordOffset);

    m_indices.assign(indices, indices + HelmetIndexCount);
    m_vertices.resize(HelmetVertexCount);
    for (uint32_t i = 0; i < HelmetVertexCount; ++i)
    {
        memcpy(m_vertices[i].position, &positions[i], sizeof(float) * 3);
        memcpy(m_vertices[i].normal, &normals[i], sizeof(float) * 3);
        memcpy(m_vertices[i].tangent, &tangents[i], sizeof(float) * 4);
        memcpy(m_vertices[i].texcoord, &texcoords[i], sizeof(float) * 2);
    }
}

void ImGuiLightingVulkan::CreateVertexBuffer()
{
    VkDeviceSize bufferSize = sizeof(Vertex) * m_vertices.size();
    CreateBuffer(bufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_vertexBuffer, m_vertexBufferMemory);
    void* data = nullptr;
    vkMapMemory(m_device, m_vertexBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, m_vertices.data(), static_cast<size_t>(bufferSize));
    vkUnmapMemory(m_device, m_vertexBufferMemory);
}

void ImGuiLightingVulkan::CreateIndexBuffer()
{
    VkDeviceSize bufferSize = sizeof(uint32_t) * m_indices.size();
    CreateBuffer(bufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_indexBuffer, m_indexBufferMemory);
    void* data = nullptr;
    vkMapMemory(m_device, m_indexBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, m_indices.data(), static_cast<size_t>(bufferSize));
    vkUnmapMemory(m_device, m_indexBufferMemory);
}

void ImGuiLightingVulkan::CreateTextureImages()
{
    for (uint32_t i = 0; i < TextureCount; ++i)
    {
        ImageData image = LoadTexture(GetExecutableDirectory() + L"..\\..\\..\\..\\Assets\\SciFiHelmet\\" + TextureNames[i]);
        VkDeviceSize imageSize = static_cast<VkDeviceSize>(image.pixels.size());

        VkBuffer stagingBuffer = VK_NULL_HANDLE;
        VkDeviceMemory stagingBufferMemory = VK_NULL_HANDLE;
        CreateBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);

        void* data = nullptr;
        vkMapMemory(m_device, stagingBufferMemory, 0, imageSize, 0, &data);
        memcpy(data, image.pixels.data(), static_cast<size_t>(imageSize));
        vkUnmapMemory(m_device, stagingBufferMemory);

        CreateImage(image.width, image.height, TextureFormat, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, m_textureImages[i], m_textureImageMemories[i]);
        TransitionImageLayout(m_textureImages[i], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        CopyBufferToImage(stagingBuffer, m_textureImages[i], image.width, image.height);
        TransitionImageLayout(m_textureImages[i], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_textureImageViews[i] = CreateImageView(m_textureImages[i], TextureFormat, VK_IMAGE_ASPECT_COLOR_BIT);

        vkDestroyBuffer(m_device, stagingBuffer, nullptr);
        vkFreeMemory(m_device, stagingBufferMemory, nullptr);
    }
}

void ImGuiLightingVulkan::CreateTextureSampler()
{
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.maxLod = 1.0f;
    ThrowIfFailed(vkCreateSampler(m_device, &samplerInfo, nullptr, &m_textureSampler), "Failed to create texture sampler.");
}

void ImGuiLightingVulkan::CreateUniformBuffer()
{
    CreateBuffer(sizeof(SceneConstants), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_uniformBuffer, m_uniformBufferMemory);
    UpdateUniformBuffer();
}

void ImGuiLightingVulkan::CreateDescriptorPool()
{
    std::array<VkDescriptorPoolSize, 3> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = 1;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    poolSizes[1].descriptorCount = TextureCount;
    poolSizes[2].type = VK_DESCRIPTOR_TYPE_SAMPLER;
    poolSizes[2].descriptorCount = 1;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = 1;
    ThrowIfFailed(vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_descriptorPool), "Failed to create descriptor pool.");
}

void ImGuiLightingVulkan::CreateDescriptorSet()
{
    VkDescriptorSetAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocateInfo.descriptorPool = m_descriptorPool;
    allocateInfo.descriptorSetCount = 1;
    allocateInfo.pSetLayouts = &m_descriptorSetLayout;
    ThrowIfFailed(vkAllocateDescriptorSets(m_device, &allocateInfo, &m_descriptorSet), "Failed to allocate descriptor set.");

    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = m_uniformBuffer;
    bufferInfo.offset = 0;
    bufferInfo.range = sizeof(SceneConstants);

    std::array<VkDescriptorImageInfo, TextureCount> imageInfos{};
    for (uint32_t i = 0; i < TextureCount; ++i)
    {
        imageInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfos[i].imageView = m_textureImageViews[i];
    }

    VkDescriptorImageInfo samplerInfo{};
    samplerInfo.sampler = m_textureSampler;

    std::array<VkWriteDescriptorSet, 6> descriptorWrites{};
    descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[0].dstSet = m_descriptorSet;
    descriptorWrites[0].dstBinding = 0;
    descriptorWrites[0].descriptorCount = 1;
    descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrites[0].pBufferInfo = &bufferInfo;

    for (uint32_t i = 0; i < TextureCount; ++i)
    {
        descriptorWrites[1 + i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1 + i].dstSet = m_descriptorSet;
        descriptorWrites[1 + i].dstBinding = 1 + i;
        descriptorWrites[1 + i].descriptorCount = 1;
        descriptorWrites[1 + i].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        descriptorWrites[1 + i].pImageInfo = &imageInfos[i];
    }

    descriptorWrites[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[5].dstSet = m_descriptorSet;
    descriptorWrites[5].dstBinding = 5;
    descriptorWrites[5].descriptorCount = 1;
    descriptorWrites[5].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    descriptorWrites[5].pImageInfo = &samplerInfo;
    vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
}

void ImGuiLightingVulkan::CreateCommandBuffers()
{
    m_commandBuffers.resize(m_framebuffers.size());
    VkCommandBufferAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocateInfo.commandPool = m_commandPool;
    allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocateInfo.commandBufferCount = static_cast<uint32_t>(m_commandBuffers.size());
    ThrowIfFailed(vkAllocateCommandBuffers(m_device, &allocateInfo, m_commandBuffers.data()), "Failed to allocate command buffers.");
}

void ImGuiLightingVulkan::InitializeImGui()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

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

void ImGuiLightingVulkan::ShutdownImGui()
{
    if (ImGui::GetCurrentContext())
    {
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
    }
}

void ImGuiLightingVulkan::BuildUI()
{
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2(24.0f, 48.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(320.0f, 170.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Directional Light");
    ImGui::PushItemWidth(180.0f);
    ImGui::DragFloat3("Direction", m_lightDirection, 0.01f, -1.0f, 1.0f, "%.2f");
    ImGui::ColorEdit3("Color", m_lightColor);
    ImGui::SliderFloat("Intensity", &m_lightIntensity, 0.0f, 10.0f, "%.2f");
    ImGui::PopItemWidth();
    if (ImGui::Button("Reset"))
    {
        ResetLight();
    }
    ImGui::End();

    ImGui::Render();
}

void ImGuiLightingVulkan::ResetLight()
{
    m_lightDirection[0] = -0.35f;
    m_lightDirection[1] = -0.8f;
    m_lightDirection[2] = 0.45f;
    m_lightColor[0] = 1.0f;
    m_lightColor[1] = 0.96f;
    m_lightColor[2] = 0.88f;
    m_lightIntensity = 5.0f;
}

void ImGuiLightingVulkan::RecordCommandBuffer(uint32_t imageIndex)
{
    VkCommandBuffer commandBuffer = m_commandBuffers[imageIndex];

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    ThrowIfFailed(vkBeginCommandBuffer(commandBuffer, &beginInfo), "Failed to begin command buffer.");

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
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &m_descriptorSet, 0, nullptr);
    VkBuffer vertexBuffers[] = { m_vertexBuffer };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(commandBuffer, m_indexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(m_indices.size()), 1, 0, 0, 0);

    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);

    vkCmdEndRenderPass(commandBuffer);

    ThrowIfFailed(vkEndCommandBuffer(commandBuffer), "Failed to record command buffer.");
}

void ImGuiLightingVulkan::CreateSyncObjects()
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

void ImGuiLightingVulkan::UpdateUniformBuffer()
{
    float aspectRatio = static_cast<float>(m_swapChainExtent.width) / static_cast<float>(m_swapChainExtent.height);
    m_rotationRadians += 0.004f;
    XMMATRIX model = XMMatrixScaling(1.2f, 1.2f, 1.2f) * XMMatrixRotationY(m_rotationRadians);
    XMMATRIX view = XMMatrixLookAtLH(XMLoadFloat3(&m_cameraPosition), XMVectorSet(0.0f, 0.12f, 0.0f, 1.0f), XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
    XMMATRIX projection = XMMatrixPerspectiveFovLH(XMConvertToRadians(45.0f), aspectRatio, 0.1f, 100.0f);
    projection.r[1] = XMVectorNegate(projection.r[1]);

    SceneConstants constants{};
    XMStoreFloat4x4(&constants.model, model);
    XMStoreFloat4x4(&constants.modelViewProjection, model * view * projection);
    constants.cameraPosition = XMFLOAT4(m_cameraPosition.x, m_cameraPosition.y, m_cameraPosition.z, 1.0f);
    XMVECTOR lightDirection = XMVectorSet(m_lightDirection[0], m_lightDirection[1], m_lightDirection[2], 0.0f);
    if (XMVectorGetX(XMVector3LengthSq(lightDirection)) < 0.0001f)
    {
        lightDirection = XMVectorSet(-0.35f, -0.8f, 0.45f, 0.0f);
    }
    XMFLOAT3 normalizedDirection;
    XMStoreFloat3(&normalizedDirection, XMVector3Normalize(lightDirection));
    constants.lightDirection = XMFLOAT4(normalizedDirection.x, normalizedDirection.y, normalizedDirection.z, 0.0f);
    constants.lightColor = XMFLOAT4(m_lightColor[0], m_lightColor[1], m_lightColor[2], m_lightIntensity);

    void* data = nullptr;
    vkMapMemory(m_device, m_uniformBufferMemory, 0, sizeof(constants), 0, &data);
    memcpy(data, &constants, sizeof(constants));
    vkUnmapMemory(m_device, m_uniformBufferMemory);
}

bool ImGuiLightingVulkan::IsDeviceSuitable(VkPhysicalDevice device) const
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

ImGuiLightingVulkan::QueueFamilyIndices ImGuiLightingVulkan::FindQueueFamilies(VkPhysicalDevice device) const
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

ImGuiLightingVulkan::SwapChainSupport ImGuiLightingVulkan::QuerySwapChainSupport(VkPhysicalDevice device) const
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

VkSurfaceFormatKHR ImGuiLightingVulkan::ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) const
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

VkPresentModeKHR ImGuiLightingVulkan::ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& presentModes) const
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

VkExtent2D ImGuiLightingVulkan::ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) const
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

uint32_t ImGuiLightingVulkan::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const
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

void ImGuiLightingVulkan::CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& memory)
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

void ImGuiLightingVulkan::CreateImage(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage, VkImage& image, VkDeviceMemory& memory)
{
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
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

VkImageView ImGuiLightingVulkan::CreateImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags) const
{
    VkImageViewCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    createInfo.image = image;
    createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    createInfo.format = format;
    createInfo.subresourceRange.aspectMask = aspectFlags;
    createInfo.subresourceRange.baseMipLevel = 0;
    createInfo.subresourceRange.levelCount = 1;
    createInfo.subresourceRange.baseArrayLayer = 0;
    createInfo.subresourceRange.layerCount = 1;

    VkImageView imageView = VK_NULL_HANDLE;
    ThrowIfFailed(vkCreateImageView(m_device, &createInfo, nullptr, &imageView), "Failed to create Vulkan image view.");
    return imageView;
}

VkCommandBuffer ImGuiLightingVulkan::BeginSingleTimeCommands() const
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

void ImGuiLightingVulkan::EndSingleTimeCommands(VkCommandBuffer commandBuffer) const
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

void ImGuiLightingVulkan::CopyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) const
{
    VkCommandBuffer commandBuffer = BeginSingleTimeCommands();

    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = { width, height, 1 };
    vkCmdCopyBufferToImage(commandBuffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    EndSingleTimeCommands(commandBuffer);
}

void ImGuiLightingVulkan::TransitionImageLayout(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout) const
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
    barrier.subresourceRange.levelCount = 1;
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

ImGuiLightingVulkan::ImageData ImGuiLightingVulkan::LoadTexture(const std::wstring& path) const
{
    Microsoft::WRL::ComPtr<IWICImagingFactory2> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))))
    {
        throw std::runtime_error("Failed to create WIC factory.");
    }

    Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder)))
    {
        throw std::runtime_error("Failed to load ImGuiLighting texture.");
    }

    Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
    decoder->GetFrame(0, &frame);

    ImageData image;
    frame->GetSize(&image.width, &image.height);
    image.pixels.resize(static_cast<size_t>(image.width) * image.height * 4);

    Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
    factory->CreateFormatConverter(&converter);
    converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
    converter->CopyPixels(nullptr, image.width * 4, static_cast<UINT>(image.pixels.size()), image.pixels.data());
    return image;
}

std::vector<char> ImGuiLightingVulkan::ReadFile(const std::wstring& path) const
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

VkShaderModule ImGuiLightingVulkan::CreateShaderModule(const std::vector<char>& code) const
{
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shaderModule = VK_NULL_HANDLE;
    ThrowIfFailed(vkCreateShaderModule(m_device, &createInfo, nullptr, &shaderModule), "Failed to create shader module.");
    return shaderModule;
}

std::wstring ImGuiLightingVulkan::GetExecutableDirectory() const
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

void ImGuiLightingVulkan::ThrowIfFailed(VkResult result, const char* message) const
{
    if (result != VK_SUCCESS)
    {
        throw std::runtime_error(message);
    }
}
