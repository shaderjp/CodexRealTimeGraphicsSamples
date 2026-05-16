#include "BistroExteriorPathtracingVulkan.h"

#include "..\..\Common\BistroResolution.h"

#include "imgui.h"
#include "backends/imgui_impl_vulkan.h"
#include "backends/imgui_impl_win32.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <fstream>
#include <set>
#include <stdexcept>
#include <utility>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

namespace
{
    const std::vector<const char*> DeviceExtensions =
    {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
        VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
        VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
        VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
        VK_KHR_SPIRV_1_4_EXTENSION_NAME,
        VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME,
        VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME
    };

    constexpr VkFormat AccumulationFormat = VK_FORMAT_R32G32B32A32_SFLOAT;
    constexpr uint32_t TextureSlotCount = Bistro::TextureSlotCount;
    constexpr VkDeviceSize LegacyRestirReservoirStride = sizeof(DirectX::XMFLOAT4) * 2;
    constexpr VkDeviceSize EnhancedRestirReservoirStride = sizeof(DirectX::XMFLOAT4) * 4;
    constexpr VkDeviceSize EnhancedGBufferStride = sizeof(DirectX::XMFLOAT4) * 2 + sizeof(uint32_t) * 4;
    constexpr VkDeviceSize EnhancedDuplicationStride = sizeof(uint32_t);
    constexpr VkDeviceSize EnhancedReplayTaskStride = sizeof(uint32_t) * 4;
    constexpr int EnhancedReuseTextureSizes[] = { 254, 230, 210 };
    constexpr float EnhancedReuseTextureSigma = 16.0f;
    constexpr int EnhancedReuseTextureRadius = 30;

    VkFormat ToVkFormat(DXGI_FORMAT format)
    {
        switch (format)
        {
        case DXGI_FORMAT_R32G32B32A32_FLOAT:
            return VK_FORMAT_R32G32B32A32_SFLOAT;
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

    DirectX::XMFLOAT3 NormalizeFloat3(const float values[3])
    {
        DirectX::XMVECTOR v = DirectX::XMVector3Normalize(DirectX::XMVectorSet(values[0], values[1], values[2], 0.0f));
        DirectX::XMFLOAT3 result;
        DirectX::XMStoreFloat3(&result, v);
        return result;
    }

    bool UsesRestirReuse(BistroPathtracingMode mode)
    {
        return mode == BistroPathtracingMode::ReSTIR || mode == BistroPathtracingMode::ReSTIRDI || mode == BistroPathtracingMode::ReSTIRPTEnhanced;
    }

    bool UsesRestirPtEnhanced(BistroPathtracingMode mode)
    {
        return mode == BistroPathtracingMode::ReSTIRPTEnhanced;
    }

    VkDeviceSize RestirReservoirStride(BistroPathtracingMode mode)
    {
        return UsesRestirPtEnhanced(mode) ? EnhancedRestirReservoirStride : LegacyRestirReservoirStride;
    }

    uint32_t HashCpu(uint32_t value)
    {
        value ^= value >> 16;
        value *= 2246822519u;
        value ^= value >> 13;
        value *= 3266489917u;
        value ^= value >> 16;
        return value;
    }

    float RandomCpu(uint32_t& state)
    {
        state = HashCpu(state);
        return static_cast<float>(state & 0x00ffffffu) / 16777216.0f;
    }

    uint32_t PackSignedOffset(int x, int y)
    {
        const auto pack16 = [](int value)
        {
            return static_cast<uint32_t>(static_cast<uint16_t>(static_cast<int16_t>(value)));
        };
        return pack16(x) | (pack16(y) << 16u);
    }

    std::vector<uint32_t> BuildEnhancedReuseTextureOffsets()
    {
        std::vector<uint32_t> offsets;
        for (int size : EnhancedReuseTextureSizes)
        {
            const size_t base = offsets.size();
            offsets.resize(base + static_cast<size_t>(size) * static_cast<size_t>(size), 0u);
            std::vector<uint8_t> assigned(static_cast<size_t>(size) * static_cast<size_t>(size), 0u);
            for (int y = 0; y < size; ++y)
            {
                for (int x = 0; x < size; ++x)
                {
                    const size_t index = static_cast<size_t>(y) * static_cast<size_t>(size) + static_cast<size_t>(x);
                    if (assigned[index] != 0)
                    {
                        continue;
                    }

                    uint32_t state = HashCpu(static_cast<uint32_t>(x * 1973 ^ y * 9277 ^ size * 26699));
                    int targetX = x;
                    int targetY = y;
                    for (int attempt = 0; attempt < 24; ++attempt)
                    {
                        const float u0 = (std::max)(RandomCpu(state), 0.0001f);
                        const float u1 = RandomCpu(state);
                        const float radius = std::sqrt(-2.0f * std::log(u0)) * EnhancedReuseTextureSigma;
                        const float angle = u1 * DirectX::XM_2PI;
                        const int dx = static_cast<int>(std::round((std::min)(radius, static_cast<float>(EnhancedReuseTextureRadius)) * std::cos(angle)));
                        const int dy = static_cast<int>(std::round((std::min)(radius, static_cast<float>(EnhancedReuseTextureRadius)) * std::sin(angle)));
                        const int candidateX = x + dx;
                        const int candidateY = y + dy;
                        if (candidateX < 0 || candidateY < 0 || candidateX >= size || candidateY >= size)
                        {
                            continue;
                        }

                        const size_t candidateIndex = static_cast<size_t>(candidateY) * static_cast<size_t>(size) + static_cast<size_t>(candidateX);
                        if (assigned[candidateIndex] == 0)
                        {
                            targetX = candidateX;
                            targetY = candidateY;
                            break;
                        }
                    }

                    const size_t targetIndex = static_cast<size_t>(targetY) * static_cast<size_t>(size) + static_cast<size_t>(targetX);
                    offsets[base + index] = PackSignedOffset(targetX - x, targetY - y);
                    assigned[index] = 1u;
                    if (targetIndex != index && assigned[targetIndex] == 0)
                    {
                        offsets[base + targetIndex] = PackSignedOffset(x - targetX, y - targetY);
                        assigned[targetIndex] = 1u;
                    }
                }
            }
        }
        return offsets;
    }

    const char* PathtracingModeName(BistroPathtracingMode mode)
    {
        switch (mode)
        {
        case BistroPathtracingMode::ReSTIR:
            return "ReSTIR GI";
        case BistroPathtracingMode::ReSTIRDI:
            return "ReSTIR DI";
        case BistroPathtracingMode::ReSTIRPTEnhanced:
            return "ReSTIR PT Enhanced";
        default:
            return "Path Tracing";
        }
    }
}

bool BistroExteriorPathtracingVulkan::QueueFamilyIndices::IsComplete() const
{
    return graphicsFamily != UINT32_MAX && presentFamily != UINT32_MAX;
}

BistroExteriorPathtracingVulkan::BistroExteriorPathtracingVulkan(uint32_t width, uint32_t height, const wchar_t* title, BistroPathtracingMode mode) :
    m_width(width),
    m_height(height),
    m_title(title),
    m_mode(mode)
{
}

BistroExteriorPathtracingVulkan::~BistroExteriorPathtracingVulkan()
{
    Cleanup();
}

int BistroExteriorPathtracingVulkan::Run(HINSTANCE instance, int showCommand)
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

LRESULT CALLBACK BistroExteriorPathtracingVulkan::WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hwnd, message, wParam, lParam))
    {
        return true;
    }

    auto* app = reinterpret_cast<BistroExteriorPathtracingVulkan*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE)
    {
        auto* createStruct = reinterpret_cast<CREATESTRUCT*>(lParam);
        app = reinterpret_cast<BistroExteriorPathtracingVulkan*>(createStruct->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }

    switch (message)
    {
    case WM_CREATE:
    {
        auto* createStruct = reinterpret_cast<CREATESTRUCT*>(lParam);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(createStruct->lpCreateParams));
        return 0;
    }
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
        if (app) app->m_camera.OnMouseButton(message, wParam);
        return 0;
    case WM_MOUSEMOVE:
        if (app) app->m_camera.OnMouseMove(lParam);
        return 0;
    case WM_SETFOCUS:
        if (app) app->m_camera.SetActive(true);
        return 0;
    case WM_KILLFOCUS:
        if (app) app->m_camera.SetActive(false);
        return 0;
    case WM_KEYDOWN:
        if (app && wParam == VK_SPACE)
        {
            app->ResetCameraView();
            app->ResetAccumulation();
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProc(hwnd, message, wParam, lParam);
    }
}

void BistroExteriorPathtracingVulkan::InitWindow(HINSTANCE instance, int showCommand)
{
    m_instanceHandle = instance;
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(WNDCLASSEXW);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    windowClass.lpszClassName = L"BistroExteriorPathtracingVulkanWindow";
    RegisterClassExW(&windowClass);

    RECT windowRect = { 0, 0, static_cast<LONG>(m_width), static_cast<LONG>(m_height) };
    AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);
    m_hwnd = CreateWindowExW(0, windowClass.lpszClassName, m_title.c_str(), WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, windowRect.right - windowRect.left, windowRect.bottom - windowRect.top, nullptr, nullptr, instance, this);
    ShowWindow(m_hwnd, showCommand);
}

void BistroExteriorPathtracingVulkan::InitVulkan()
{
    CreateInstance();
    CreateSurface();
    PickPhysicalDevice();
    CreateLogicalDevice();
    LoadRayTracingFunctions();
    CreateSwapChain();
    CreateImageViews();
    CreateRenderPass();
    CreateFramebuffers();
    CreateCommandPool();
    LoadModel();
    CreateOutputImages();
    CreateSceneBuffers();
    CreateTextureImages();
    CreateTextureSampler();
    CreateDescriptorSetLayouts();
    CreateDescriptorPool();
    BuildAccelerationStructures();
    CreateDescriptorSets();
    CreateRayTracingPipeline();
    CreateRestirReusePipeline();
    CreateDenoisePipeline();
    CreateShaderBindingTables();
    CreateCommandBuffers();
    CreateSyncObjects();
    InitializeImGui();
    m_lastUpdate = std::chrono::steady_clock::now();
}

void BistroExteriorPathtracingVulkan::MainLoop()
{
    MSG msg{};
    while (msg.message != WM_QUIT)
    {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else
        {
            const auto now = std::chrono::steady_clock::now();
            const float deltaSeconds = std::chrono::duration<float>(now - m_lastUpdate).count();
            m_lastUpdate = now;
            m_camera.Update(deltaSeconds);
            UpdateUniformBuffer(deltaSeconds);
            BuildUI();
            Render();
        }
    }
}

void BistroExteriorPathtracingVulkan::Render()
{
    if (IsIconic(m_hwnd) || m_swapChainExtent.width == 0 || m_swapChainExtent.height == 0)
    {
        Sleep(16);
        return;
    }

    vkWaitForFences(m_device, 1, &m_inFlightFences[m_currentFrame], VK_TRUE, UINT64_MAX);

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
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR | VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
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
    ThrowIfFailed(vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, m_inFlightFences[m_currentFrame]), "Failed to submit command buffer.");

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &m_swapChain;
    presentInfo.pImageIndices = &imageIndex;
    VkResult presentResult = vkQueuePresentKHR(m_presentQueue, &presentInfo);
    if (presentResult != VK_SUCCESS && presentResult != VK_SUBOPTIMAL_KHR && presentResult != VK_ERROR_OUT_OF_DATE_KHR)
    {
        ThrowIfFailed(presentResult, "Failed to present swapchain image.");
    }

    m_currentFrame = (m_currentFrame + 1) % MaxFramesInFlight;
}

void BistroExteriorPathtracingVulkan::SetOutputResolution(uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0 || (m_swapChainExtent.width == width && m_swapChainExtent.height == height))
    {
        return;
    }

    Bistro::ResizeClientArea(m_hwnd, width, height);
    RecreateSwapChain();
}

void BistroExteriorPathtracingVulkan::RecreateSwapChain()
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

    if (m_descriptorPool && m_descriptorSet)
    {
        VkDescriptorSet descriptorSets[] = { m_descriptorSet, m_textureDescriptorSet };
        vkFreeDescriptorSets(m_device, m_descriptorPool, m_textureDescriptorSet ? 2u : 1u, descriptorSets);
        m_descriptorSet = VK_NULL_HANDLE;
        m_textureDescriptorSet = VK_NULL_HANDLE;
    }

    if (m_outputImageView)
    {
        vkDestroyImageView(m_device, m_outputImageView, nullptr);
        m_outputImageView = VK_NULL_HANDLE;
    }
    if (m_outputImage)
    {
        vkDestroyImage(m_device, m_outputImage, nullptr);
        m_outputImage = VK_NULL_HANDLE;
    }
    if (m_outputImageMemory)
    {
        vkFreeMemory(m_device, m_outputImageMemory, nullptr);
        m_outputImageMemory = VK_NULL_HANDLE;
    }
    if (m_accumulationImageView)
    {
        vkDestroyImageView(m_device, m_accumulationImageView, nullptr);
        m_accumulationImageView = VK_NULL_HANDLE;
    }
    if (m_accumulationImage)
    {
        vkDestroyImage(m_device, m_accumulationImage, nullptr);
        m_accumulationImage = VK_NULL_HANDLE;
    }
    if (m_accumulationImageMemory)
    {
        vkFreeMemory(m_device, m_accumulationImageMemory, nullptr);
        m_accumulationImageMemory = VK_NULL_HANDLE;
    }
    if (m_denoiseAov0ImageView)
    {
        vkDestroyImageView(m_device, m_denoiseAov0ImageView, nullptr);
        m_denoiseAov0ImageView = VK_NULL_HANDLE;
    }
    if (m_denoiseAov0Image)
    {
        vkDestroyImage(m_device, m_denoiseAov0Image, nullptr);
        m_denoiseAov0Image = VK_NULL_HANDLE;
    }
    if (m_denoiseAov0ImageMemory)
    {
        vkFreeMemory(m_device, m_denoiseAov0ImageMemory, nullptr);
        m_denoiseAov0ImageMemory = VK_NULL_HANDLE;
    }
    if (m_denoiseAov1ImageView)
    {
        vkDestroyImageView(m_device, m_denoiseAov1ImageView, nullptr);
        m_denoiseAov1ImageView = VK_NULL_HANDLE;
    }
    if (m_denoiseAov1Image)
    {
        vkDestroyImage(m_device, m_denoiseAov1Image, nullptr);
        m_denoiseAov1Image = VK_NULL_HANDLE;
    }
    if (m_denoiseAov1ImageMemory)
    {
        vkFreeMemory(m_device, m_denoiseAov1ImageMemory, nullptr);
        m_denoiseAov1ImageMemory = VK_NULL_HANDLE;
    }
    DestroyBuffer(m_restirReservoirCurrent);
    DestroyBuffer(m_restirReservoirHistory);
    DestroyBuffer(m_restirReservoirSpatial);
    DestroyBuffer(m_enhancedGBufferCurrent);
    DestroyBuffer(m_enhancedGBufferHistory);
    DestroyBuffer(m_enhancedDuplicationCurrent);
    DestroyBuffer(m_enhancedDuplicationHistory);
    DestroyBuffer(m_enhancedReplayTasks);
    for (VkImageView imageView : m_swapChainImageViews)
    {
        vkDestroyImageView(m_device, imageView, nullptr);
    }
    m_swapChainImageViews.clear();
    m_swapChainImages.clear();
    m_swapChainImageInitialized.clear();

    vkDestroySwapchainKHR(m_device, m_swapChain, nullptr);
    m_swapChain = VK_NULL_HANDLE;

    CreateSwapChain();
    m_width = m_swapChainExtent.width;
    m_height = m_swapChainExtent.height;
    CreateImageViews();
    CreateFramebuffers();
    CreateOutputImages();
    CreateDescriptorSets();
    CreateCommandBuffers();
    ResetAccumulation();
}

void BistroExteriorPathtracingVulkan::WaitIdle()
{
    if (m_device != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(m_device);
    }
}

void BistroExteriorPathtracingVulkan::Cleanup()
{
    if (m_device == VK_NULL_HANDLE)
    {
        return;
    }
    vkDeviceWaitIdle(m_device);
    ShutdownImGui();

    for (auto fence : m_inFlightFences) if (fence) vkDestroyFence(m_device, fence, nullptr);
    for (auto semaphore : m_renderFinishedSemaphores) if (semaphore) vkDestroySemaphore(m_device, semaphore, nullptr);
    for (auto semaphore : m_imageAvailableSemaphores) if (semaphore) vkDestroySemaphore(m_device, semaphore, nullptr);

    DestroyBuffer(m_rayGenSbt.buffer);
    DestroyBuffer(m_missSbt.buffer);
    DestroyBuffer(m_hitSbt.buffer);
    if (m_vkDestroyAccelerationStructureKHR)
    {
        if (m_tlas.handle) m_vkDestroyAccelerationStructureKHR(m_device, m_tlas.handle, nullptr);
        if (m_blas.handle) m_vkDestroyAccelerationStructureKHR(m_device, m_blas.handle, nullptr);
    }
    DestroyBuffer(m_tlas.buffer);
    DestroyBuffer(m_blas.buffer);
    DestroyBuffer(m_instanceBuffer);
    DestroyBuffer(m_vertexBuffer);
    DestroyBuffer(m_indexBuffer);
    DestroyBuffer(m_geometryBuffer);
    DestroyBuffer(m_materialBuffer);
    DestroyBuffer(m_lightBuffer);
    DestroyBuffer(m_sceneUniformBuffer);
    DestroyBuffer(m_restirReservoirCurrent);
    DestroyBuffer(m_restirReservoirHistory);
    DestroyBuffer(m_restirReservoirSpatial);
    DestroyBuffer(m_enhancedGBufferCurrent);
    DestroyBuffer(m_enhancedGBufferHistory);
    DestroyBuffer(m_enhancedDuplicationCurrent);
    DestroyBuffer(m_enhancedDuplicationHistory);
    DestroyBuffer(m_enhancedReuseTextureOffsets);
    DestroyBuffer(m_enhancedReplayTasks);

    for (GpuTexture& texture : m_textures)
    {
        if (texture.view) vkDestroyImageView(m_device, texture.view, nullptr);
        if (texture.image) vkDestroyImage(m_device, texture.image, nullptr);
        if (texture.memory) vkFreeMemory(m_device, texture.memory, nullptr);
    }
    if (m_textureSampler) vkDestroySampler(m_device, m_textureSampler, nullptr);
    if (m_outputImageView) vkDestroyImageView(m_device, m_outputImageView, nullptr);
    if (m_outputImage) vkDestroyImage(m_device, m_outputImage, nullptr);
    if (m_outputImageMemory) vkFreeMemory(m_device, m_outputImageMemory, nullptr);
    if (m_accumulationImageView) vkDestroyImageView(m_device, m_accumulationImageView, nullptr);
    if (m_accumulationImage) vkDestroyImage(m_device, m_accumulationImage, nullptr);
    if (m_accumulationImageMemory) vkFreeMemory(m_device, m_accumulationImageMemory, nullptr);
    if (m_denoiseAov0ImageView) vkDestroyImageView(m_device, m_denoiseAov0ImageView, nullptr);
    if (m_denoiseAov0Image) vkDestroyImage(m_device, m_denoiseAov0Image, nullptr);
    if (m_denoiseAov0ImageMemory) vkFreeMemory(m_device, m_denoiseAov0ImageMemory, nullptr);
    if (m_denoiseAov1ImageView) vkDestroyImageView(m_device, m_denoiseAov1ImageView, nullptr);
    if (m_denoiseAov1Image) vkDestroyImage(m_device, m_denoiseAov1Image, nullptr);
    if (m_denoiseAov1ImageMemory) vkFreeMemory(m_device, m_denoiseAov1ImageMemory, nullptr);
    if (m_restirReusePipeline) vkDestroyPipeline(m_device, m_restirReusePipeline, nullptr);
    if (m_denoisePipeline) vkDestroyPipeline(m_device, m_denoisePipeline, nullptr);
    if (m_rayTracingPipeline) vkDestroyPipeline(m_device, m_rayTracingPipeline, nullptr);
    if (m_pipelineLayout) vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
    if (m_descriptorPool) vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
    if (m_descriptorSetLayout) vkDestroyDescriptorSetLayout(m_device, m_descriptorSetLayout, nullptr);
    if (m_textureDescriptorSetLayout) vkDestroyDescriptorSetLayout(m_device, m_textureDescriptorSetLayout, nullptr);
    if (m_commandPool) vkDestroyCommandPool(m_device, m_commandPool, nullptr);
    for (VkFramebuffer framebuffer : m_framebuffers) vkDestroyFramebuffer(m_device, framebuffer, nullptr);
    if (m_renderPass) vkDestroyRenderPass(m_device, m_renderPass, nullptr);
    for (VkImageView imageView : m_swapChainImageViews) vkDestroyImageView(m_device, imageView, nullptr);
    if (m_swapChain) vkDestroySwapchainKHR(m_device, m_swapChain, nullptr);
    vkDestroyDevice(m_device, nullptr);
    if (m_surface) vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
    if (m_instance) vkDestroyInstance(m_instance, nullptr);
    m_device = VK_NULL_HANDLE;
}

void BistroExteriorPathtracingVulkan::CreateInstance()
{
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "BistroExteriorPathtracing";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "CodexRealTimeGraphicsSamples";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_2;

    const char* extensions[] = { VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME };
    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = _countof(extensions);
    createInfo.ppEnabledExtensionNames = extensions;
    ThrowIfFailed(vkCreateInstance(&createInfo, nullptr, &m_instance), "Failed to create Vulkan instance.");
}

void BistroExteriorPathtracingVulkan::CreateSurface()
{
    VkWin32SurfaceCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    createInfo.hwnd = m_hwnd;
    createInfo.hinstance = m_instanceHandle;
    ThrowIfFailed(vkCreateWin32SurfaceKHR(m_instance, &createInfo, nullptr, &m_surface), "Failed to create Win32 surface.");
}

void BistroExteriorPathtracingVulkan::PickPhysicalDevice()
{
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);
    if (deviceCount == 0)
    {
        throw std::runtime_error("No Vulkan physical devices were found.");
    }
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data());
    for (VkPhysicalDevice device : devices)
    {
        if (IsDeviceSuitable(device))
        {
            m_physicalDevice = device;
            break;
        }
    }
    if (m_physicalDevice == VK_NULL_HANDLE)
    {
        throw std::runtime_error("No suitable Vulkan device was found. BistroExteriorPathtracingVulkan requires Vulkan 1.2, swapchain support, and KHR Path Tracing features.");
    }

    m_asProperties = {};
    m_asProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;
    m_rtPipelineProperties = {};
    m_rtPipelineProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
    m_rtPipelineProperties.pNext = &m_asProperties;
    VkPhysicalDeviceProperties2 properties2{};
    properties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    properties2.pNext = &m_rtPipelineProperties;
    vkGetPhysicalDeviceProperties2(m_physicalDevice, &properties2);

    VkPhysicalDeviceFeatures supportedFeatures{};
    vkGetPhysicalDeviceFeatures(m_physicalDevice, &supportedFeatures);
    m_samplerAnisotropySupported = supportedFeatures.samplerAnisotropy == VK_TRUE;
    m_textureCompressionBcSupported = supportedFeatures.textureCompressionBC == VK_TRUE;
    VkPhysicalDeviceProperties deviceProperties{};
    vkGetPhysicalDeviceProperties(m_physicalDevice, &deviceProperties);
    m_maxSamplerAnisotropy = m_samplerAnisotropySupported ? deviceProperties.limits.maxSamplerAnisotropy : 1.0f;
}

void BistroExteriorPathtracingVulkan::CreateLogicalDevice()
{
    QueueFamilyIndices indices = FindQueueFamilies(m_physicalDevice);
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    std::set<uint32_t> uniqueQueueFamilies = { indices.graphicsFamily, indices.presentFamily };
    float queuePriority = 1.0f;
    for (uint32_t queueFamily : uniqueQueueFamilies)
    {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }

    VkPhysicalDeviceAccelerationStructureFeaturesKHR asFeatures{};
    asFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
    asFeatures.accelerationStructure = VK_TRUE;
    VkPhysicalDeviceRayTracingPipelineFeaturesKHR rtFeatures{};
    rtFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
    rtFeatures.rayTracingPipeline = VK_TRUE;
    rtFeatures.pNext = &asFeatures;
    VkPhysicalDeviceBufferDeviceAddressFeatures bdaFeatures{};
    bdaFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
    bdaFeatures.bufferDeviceAddress = VK_TRUE;
    bdaFeatures.pNext = &rtFeatures;
    VkPhysicalDeviceDescriptorIndexingFeatures indexingFeatures{};
    indexingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;
    indexingFeatures.runtimeDescriptorArray = VK_TRUE;
    indexingFeatures.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    indexingFeatures.descriptorBindingPartiallyBound = VK_TRUE;
    indexingFeatures.descriptorBindingVariableDescriptorCount = VK_TRUE;
    indexingFeatures.pNext = &bdaFeatures;
    VkPhysicalDeviceFeatures2 deviceFeatures{};
    deviceFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    deviceFeatures.pNext = &indexingFeatures;
    deviceFeatures.features.samplerAnisotropy = m_samplerAnisotropySupported ? VK_TRUE : VK_FALSE;
    deviceFeatures.features.textureCompressionBC = m_textureCompressionBcSupported ? VK_TRUE : VK_FALSE;

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pNext = &deviceFeatures;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.enabledExtensionCount = static_cast<uint32_t>(DeviceExtensions.size());
    createInfo.ppEnabledExtensionNames = DeviceExtensions.data();
    ThrowIfFailed(vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device), "Failed to create Vulkan device.");
    vkGetDeviceQueue(m_device, indices.graphicsFamily, 0, &m_graphicsQueue);
    vkGetDeviceQueue(m_device, indices.presentFamily, 0, &m_presentQueue);
}

void BistroExteriorPathtracingVulkan::LoadRayTracingFunctions()
{
    m_vkGetBufferDeviceAddressKHR = reinterpret_cast<PFN_vkGetBufferDeviceAddressKHR>(vkGetDeviceProcAddr(m_device, "vkGetBufferDeviceAddressKHR"));
    m_vkCreateAccelerationStructureKHR = reinterpret_cast<PFN_vkCreateAccelerationStructureKHR>(vkGetDeviceProcAddr(m_device, "vkCreateAccelerationStructureKHR"));
    m_vkDestroyAccelerationStructureKHR = reinterpret_cast<PFN_vkDestroyAccelerationStructureKHR>(vkGetDeviceProcAddr(m_device, "vkDestroyAccelerationStructureKHR"));
    m_vkGetAccelerationStructureBuildSizesKHR = reinterpret_cast<PFN_vkGetAccelerationStructureBuildSizesKHR>(vkGetDeviceProcAddr(m_device, "vkGetAccelerationStructureBuildSizesKHR"));
    m_vkCmdBuildAccelerationStructuresKHR = reinterpret_cast<PFN_vkCmdBuildAccelerationStructuresKHR>(vkGetDeviceProcAddr(m_device, "vkCmdBuildAccelerationStructuresKHR"));
    m_vkGetAccelerationStructureDeviceAddressKHR = reinterpret_cast<PFN_vkGetAccelerationStructureDeviceAddressKHR>(vkGetDeviceProcAddr(m_device, "vkGetAccelerationStructureDeviceAddressKHR"));
    m_vkCreateRayTracingPipelinesKHR = reinterpret_cast<PFN_vkCreateRayTracingPipelinesKHR>(vkGetDeviceProcAddr(m_device, "vkCreateRayTracingPipelinesKHR"));
    m_vkGetRayTracingShaderGroupHandlesKHR = reinterpret_cast<PFN_vkGetRayTracingShaderGroupHandlesKHR>(vkGetDeviceProcAddr(m_device, "vkGetRayTracingShaderGroupHandlesKHR"));
    m_vkCmdTraceRaysKHR = reinterpret_cast<PFN_vkCmdTraceRaysKHR>(vkGetDeviceProcAddr(m_device, "vkCmdTraceRaysKHR"));
    if (!m_vkGetBufferDeviceAddressKHR || !m_vkCreateAccelerationStructureKHR || !m_vkDestroyAccelerationStructureKHR ||
        !m_vkGetAccelerationStructureBuildSizesKHR || !m_vkCmdBuildAccelerationStructuresKHR || !m_vkGetAccelerationStructureDeviceAddressKHR ||
        !m_vkCreateRayTracingPipelinesKHR || !m_vkGetRayTracingShaderGroupHandlesKHR || !m_vkCmdTraceRaysKHR)
    {
        throw std::runtime_error("A required Vulkan Path Tracing function pointer could not be loaded.");
    }
}

void BistroExteriorPathtracingVulkan::CreateSwapChain()
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

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = m_surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    QueueFamilyIndices indices = FindQueueFamilies(m_physicalDevice);
    uint32_t queueFamilyIndices[] = { indices.graphicsFamily, indices.presentFamily };
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
    m_swapChainImageInitialized.assign(imageCount, false);

    VkFormatProperties formatProperties{};
    vkGetPhysicalDeviceFormatProperties(m_physicalDevice, m_swapChainImageFormat, &formatProperties);
    if ((formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) == 0)
    {
        throw std::runtime_error("The selected swapchain format does not support storage images for Path Tracing output.");
    }
}

void BistroExteriorPathtracingVulkan::CreateImageViews()
{
    m_swapChainImageViews.resize(m_swapChainImages.size());
    for (size_t i = 0; i < m_swapChainImages.size(); ++i)
    {
        m_swapChainImageViews[i] = CreateImageView(m_swapChainImages[i], m_swapChainImageFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1);
    }
}

void BistroExteriorPathtracingVulkan::CreateRenderPass()
{
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = m_swapChainImageFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    ThrowIfFailed(vkCreateRenderPass(m_device, &renderPassInfo, nullptr, &m_renderPass), "Failed to create render pass.");
}

void BistroExteriorPathtracingVulkan::CreateFramebuffers()
{
    m_framebuffers.resize(m_swapChainImageViews.size());
    for (size_t i = 0; i < m_swapChainImageViews.size(); ++i)
    {
        VkImageView attachments[] = { m_swapChainImageViews[i] };
        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = m_renderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = m_swapChainExtent.width;
        framebufferInfo.height = m_swapChainExtent.height;
        framebufferInfo.layers = 1;
        ThrowIfFailed(vkCreateFramebuffer(m_device, &framebufferInfo, nullptr, &m_framebuffers[i]), "Failed to create framebuffer.");
    }
}

void BistroExteriorPathtracingVulkan::CreateCommandPool()
{
    QueueFamilyIndices queueFamilyIndices = FindQueueFamilies(m_physicalDevice);
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily;
    ThrowIfFailed(vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool), "Failed to create command pool.");
}

void BistroExteriorPathtracingVulkan::LoadModel()
{
    m_scene = Bistro::LoadScene(Bistro::FindAssetRoot());
    if (m_scene.draws.empty())
    {
        throw std::runtime_error("BistroExterior.fbx did not produce Path Tracing geometries.");
    }
    Bistro::LightBuildResult lightBuild = Bistro::BuildLightList(m_scene);
    m_lights = std::move(lightBuild.lights);
    m_activeLightCount = lightBuild.activeLightCount;
    m_emissiveTriangleLightCount = lightBuild.emissiveTriangleCount;
    m_proceduralAreaLightCount = lightBuild.proceduralAreaCount;
    m_environmentTexturePath = Bistro::FindEnvironmentMapPath(m_scene.assetRoot);
    m_environmentMapEnabled = !m_environmentTexturePath.empty();

    m_geometryRecords.reserve(m_scene.draws.size());
    for (const Bistro::DrawItem& draw : m_scene.draws)
    {
        Bistro::RtGeometryRecord record{};
        record.indexOffset = draw.startIndex;
        record.indexCount = draw.indexCount;
        record.baseVertex = draw.baseVertex;
        record.materialIndex = draw.materialIndex;
        m_geometryRecords.push_back(record);
    }

    m_defaultCameraPosition = DirectX::XMFLOAT3(-16.32f, 4.66f, -10.41f);
    m_defaultCameraYaw = DirectX::XMConvertToRadians(18.1f);
    m_defaultCameraPitch = DirectX::XMConvertToRadians(2.8f);
    ResetCameraView();
    ResetCameraSpeeds();
}

void BistroExteriorPathtracingVulkan::CreateOutputImages()
{
    CreateImage(m_swapChainExtent.width, m_swapChainExtent.height, 1, m_swapChainImageFormat, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, m_outputImage, m_outputImageMemory);
    m_outputImageView = CreateImageView(m_outputImage, m_swapChainImageFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1);
    TransitionImageLayoutImmediate(m_outputImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT);

    CreateImage(m_swapChainExtent.width, m_swapChainExtent.height, 1, AccumulationFormat, VK_IMAGE_USAGE_STORAGE_BIT, m_accumulationImage, m_accumulationImageMemory);
    m_accumulationImageView = CreateImageView(m_accumulationImage, AccumulationFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1);
    TransitionImageLayoutImmediate(m_accumulationImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT);

    CreateImage(m_swapChainExtent.width, m_swapChainExtent.height, 1, AccumulationFormat, VK_IMAGE_USAGE_STORAGE_BIT, m_denoiseAov0Image, m_denoiseAov0ImageMemory);
    m_denoiseAov0ImageView = CreateImageView(m_denoiseAov0Image, AccumulationFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1);
    TransitionImageLayoutImmediate(m_denoiseAov0Image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT);

    CreateImage(m_swapChainExtent.width, m_swapChainExtent.height, 1, AccumulationFormat, VK_IMAGE_USAGE_STORAGE_BIT, m_denoiseAov1Image, m_denoiseAov1ImageMemory);
    m_denoiseAov1ImageView = CreateImageView(m_denoiseAov1Image, AccumulationFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1);
    TransitionImageLayoutImmediate(m_denoiseAov1Image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT);

    m_restirReservoirElementCount = (std::max)(1u, m_swapChainExtent.width * m_swapChainExtent.height);
    m_restirReservoirBufferSize = static_cast<VkDeviceSize>(m_restirReservoirElementCount) * RestirReservoirStride(m_mode);
    m_enhancedGBufferSize = static_cast<VkDeviceSize>(m_restirReservoirElementCount) * EnhancedGBufferStride;
    m_enhancedDuplicationMapSize = static_cast<VkDeviceSize>(m_restirReservoirElementCount) * EnhancedDuplicationStride;
    m_enhancedReplayTaskBufferSize = static_cast<VkDeviceSize>(m_restirReservoirElementCount) * EnhancedReplayTaskStride;
    VkBufferUsageFlags outputBufferUsage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    CreateBuffer(m_restirReservoirBufferSize, outputBufferUsage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_restirReservoirCurrent);
    CreateBuffer(m_restirReservoirBufferSize, outputBufferUsage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_restirReservoirHistory);
    CreateBuffer(m_restirReservoirBufferSize, outputBufferUsage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_restirReservoirSpatial);
    CreateBuffer(m_enhancedGBufferSize, outputBufferUsage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_enhancedGBufferCurrent);
    CreateBuffer(m_enhancedGBufferSize, outputBufferUsage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_enhancedGBufferHistory);
    CreateBuffer(m_enhancedDuplicationMapSize, outputBufferUsage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_enhancedDuplicationCurrent);
    CreateBuffer(m_enhancedDuplicationMapSize, outputBufferUsage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_enhancedDuplicationHistory);
    CreateBuffer(m_enhancedReplayTaskBufferSize, outputBufferUsage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_enhancedReplayTasks);
}

void BistroExteriorPathtracingVulkan::CreateSceneBuffers()
{
    UploadBuffer(m_scene.vertices.data(), sizeof(Bistro::Vertex) * m_scene.vertices.size(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, m_vertexBuffer);
    UploadBuffer(m_scene.indices.data(), sizeof(uint32_t) * m_scene.indices.size(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, m_indexBuffer);
    UploadBuffer(m_geometryRecords.data(), sizeof(Bistro::RtGeometryRecord) * m_geometryRecords.size(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, m_geometryBuffer);
    UploadBuffer(m_lights.data(), sizeof(Bistro::RtLight) * m_lights.size(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, m_lightBuffer);

    CreateBuffer(sizeof(SceneConstants), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_sceneUniformBuffer);

    std::vector<uint32_t> reuseOffsets = BuildEnhancedReuseTextureOffsets();
    m_enhancedReuseTextureElementCount = static_cast<uint32_t>(reuseOffsets.size());
    UploadBuffer(
        reuseOffsets.data(),
        static_cast<VkDeviceSize>(reuseOffsets.size()) * sizeof(uint32_t),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        m_enhancedReuseTextureOffsets);
}

uint32_t BistroExteriorPathtracingVulkan::CreateTextureResource(const std::wstring& path, bool srgb, const uint8_t fallback[4], std::map<std::wstring, uint32_t>& cache)
{
    std::wstring key = path.empty() ? (std::wstring(L"fallback:") + std::to_wstring(fallback[0]) + L"," + std::to_wstring(fallback[1]) + L"," + std::to_wstring(fallback[2]) + L"," + std::to_wstring(fallback[3]) + (srgb ? L":srgb" : L":linear")) : path + (srgb ? L":srgb" : L":linear");
    auto found = cache.find(key);
    if (found != cache.end())
    {
        return found->second;
    }

    Bistro::TextureData textureData = Bistro::LoadTextureVulkan(path, srgb, fallback, m_textureCompressionBcSupported);
    GpuTexture texture;
    texture.path = path;
    texture.width = textureData.width;
    texture.height = textureData.height;
    texture.mipLevels = textureData.mipLevels;
    texture.format = ToVkFormat(textureData.format);
    texture.fallback = textureData.fallback;

    GpuBuffer staging;
    CreateBuffer(textureData.pixels.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging);
    void* mapped = nullptr;
    vkMapMemory(m_device, staging.memory, 0, staging.size, 0, &mapped);
    memcpy(mapped, textureData.pixels.data(), textureData.pixels.size());
    vkUnmapMemory(m_device, staging.memory);

    CreateImage(textureData.width, textureData.height, textureData.mipLevels, texture.format, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, texture.image, texture.memory);
    TransitionImageLayoutImmediate(texture.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
    CopyBufferToImage(staging.buffer, texture.image, textureData);
    TransitionImageLayoutImmediate(texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
    DestroyBuffer(staging);

    texture.view = CreateImageView(texture.image, texture.format, VK_IMAGE_ASPECT_COLOR_BIT, texture.mipLevels);
    const uint32_t index = static_cast<uint32_t>(m_textures.size());
    m_textures.push_back(texture);
    cache[key] = index;
    return index;
}

void BistroExteriorPathtracingVulkan::CreateTextureImages()
{
    const uint8_t white[] = { 255, 255, 255, 255 };
    const uint8_t normal[] = { 128, 128, 255, 255 };
    const uint8_t black[] = { 0, 0, 0, 255 };
    const uint8_t environmentFallback[] = { 35, 68, 110, 255 };
    std::map<std::wstring, uint32_t> cache;
    m_materialTextureIndices.resize(m_scene.materials.size());
    m_rtMaterials.resize(m_scene.materials.size());

    for (size_t materialIndex = 0; materialIndex < m_scene.materials.size(); ++materialIndex)
    {
        const Bistro::Material& material = m_scene.materials[materialIndex];
        auto& indices = m_materialTextureIndices[materialIndex];
        indices[Bistro::TextureSlotBaseColor] = CreateTextureResource(material.textures[Bistro::TextureSlotBaseColor], true, white, cache);
        indices[Bistro::TextureSlotNormal] = CreateTextureResource(material.textures[Bistro::TextureSlotNormal], false, normal, cache);
        indices[Bistro::TextureSlotSpecular] = CreateTextureResource(material.textures[Bistro::TextureSlotSpecular], false, white, cache);
        indices[Bistro::TextureSlotEmissive] = CreateTextureResource(material.textures[Bistro::TextureSlotEmissive], true, black, cache);

        Bistro::RtMaterial rtMaterial{};
        rtMaterial.baseColorFactor = material.baseColorFactor;
        rtMaterial.emissiveFactor = material.emissiveFactor;
        rtMaterial.textureBaseIndex = static_cast<uint32_t>(materialIndex * Bistro::TextureSlotCount);
        rtMaterial.alphaMasked = material.alphaMasked ? 1u : 0u;
        rtMaterial.alphaCutoff = material.alphaCutoff;
        m_rtMaterials[materialIndex] = rtMaterial;
    }

    m_environmentTextureIndex = CreateTextureResource(m_environmentTexturePath, false, environmentFallback, cache);
    m_environmentDescriptorIndex = static_cast<uint32_t>(m_scene.materials.size() * TextureSlotCount);

    UploadBuffer(m_rtMaterials.data(), sizeof(Bistro::RtMaterial) * m_rtMaterials.size(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, m_materialBuffer);
}

void BistroExteriorPathtracingVulkan::CreateTextureSampler()
{
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = m_samplerAnisotropySupported ? VK_TRUE : VK_FALSE;
    samplerInfo.maxAnisotropy = m_samplerAnisotropySupported ? m_maxSamplerAnisotropy : 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
    ThrowIfFailed(vkCreateSampler(m_device, &samplerInfo, nullptr, &m_textureSampler), "Failed to create texture sampler.");
}

void BistroExteriorPathtracingVulkan::CreateDescriptorSetLayouts()
{
    const VkShaderStageFlags rtStages =
        VK_SHADER_STAGE_RAYGEN_BIT_KHR |
        VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR |
        VK_SHADER_STAGE_ANY_HIT_BIT_KHR |
        VK_SHADER_STAGE_MISS_BIT_KHR;
    const VkShaderStageFlags rtAndComputeStages = rtStages | VK_SHADER_STAGE_COMPUTE_BIT;

    std::array<VkDescriptorSetLayoutBinding, 21> bindings{};
    bindings[0] = { 0, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1, rtStages, nullptr };
    bindings[1] = { 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, rtAndComputeStages, nullptr };
    bindings[2] = { 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, rtAndComputeStages, nullptr };
    bindings[3] = { 3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, rtAndComputeStages, nullptr };
    bindings[4] = { 4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, rtStages, nullptr };
    bindings[5] = { 5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, rtStages, nullptr };
    bindings[6] = { 6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, rtStages, nullptr };
    bindings[7] = { 7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, rtStages, nullptr };
    bindings[8] = { 8, VK_DESCRIPTOR_TYPE_SAMPLER, 1, rtStages, nullptr };
    bindings[9] = { 9, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, rtAndComputeStages, nullptr };
    bindings[10] = { 10, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, rtAndComputeStages, nullptr };
    bindings[11] = { 11, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, rtAndComputeStages, nullptr };
    bindings[12] = { 12, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, rtStages, nullptr };
    bindings[13] = { 13, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, rtAndComputeStages, nullptr };
    bindings[14] = { 14, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, rtAndComputeStages, nullptr };
    bindings[15] = { 15, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, rtAndComputeStages, nullptr };
    bindings[16] = { 16, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, rtAndComputeStages, nullptr };
    bindings[17] = { 17, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, rtAndComputeStages, nullptr };
    bindings[18] = { 18, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, rtAndComputeStages, nullptr };
    bindings[19] = { 19, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, rtAndComputeStages, nullptr };
    bindings[20] = { 20, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, rtAndComputeStages, nullptr };

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    ThrowIfFailed(vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_descriptorSetLayout), "Failed to create Path Tracing descriptor set layout.");

    const uint32_t textureDescriptorCount = (std::max)(1u, static_cast<uint32_t>(m_scene.materials.size() * TextureSlotCount + 1u));
    VkDescriptorSetLayoutBinding textureBinding{};
    textureBinding.binding = 0;
    textureBinding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    textureBinding.descriptorCount = textureDescriptorCount;
    textureBinding.stageFlags = rtStages;

    VkDescriptorBindingFlags textureBindingFlags =
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
        VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;
    VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlags{};
    bindingFlags.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
    bindingFlags.bindingCount = 1;
    bindingFlags.pBindingFlags = &textureBindingFlags;

    VkDescriptorSetLayoutCreateInfo textureLayoutInfo{};
    textureLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    textureLayoutInfo.pNext = &bindingFlags;
    textureLayoutInfo.bindingCount = 1;
    textureLayoutInfo.pBindings = &textureBinding;
    ThrowIfFailed(vkCreateDescriptorSetLayout(m_device, &textureLayoutInfo, nullptr, &m_textureDescriptorSetLayout), "Failed to create bindless texture descriptor set layout.");
}

void BistroExteriorPathtracingVulkan::CreateDescriptorPool()
{
    const uint32_t textureDescriptorCount = (std::max)(1u, static_cast<uint32_t>(m_scene.materials.size() * TextureSlotCount + 1u));
    std::array<VkDescriptorPoolSize, 7> poolSizes{};
    poolSizes[0] = { VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1 };
    poolSizes[1] = { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 4 };
    poolSizes[2] = { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 };
    poolSizes[3] = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 14 };
    poolSizes[4] = { VK_DESCRIPTOR_TYPE_SAMPLER, 1 };
    poolSizes[5] = { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, textureDescriptorCount };
    poolSizes[6] = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 64 };

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 68;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    ThrowIfFailed(vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_descriptorPool), "Failed to create descriptor pool.");
}

void BistroExteriorPathtracingVulkan::CreateDescriptorSets()
{
    VkDescriptorSetAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocateInfo.descriptorPool = m_descriptorPool;
    allocateInfo.descriptorSetCount = 1;
    allocateInfo.pSetLayouts = &m_descriptorSetLayout;
    ThrowIfFailed(vkAllocateDescriptorSets(m_device, &allocateInfo, &m_descriptorSet), "Failed to allocate Path Tracing descriptor set.");

    const uint32_t textureDescriptorCount = (std::max)(1u, static_cast<uint32_t>(m_scene.materials.size() * TextureSlotCount + 1u));
    VkDescriptorSetVariableDescriptorCountAllocateInfo variableCount{};
    variableCount.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO;
    variableCount.descriptorSetCount = 1;
    variableCount.pDescriptorCounts = &textureDescriptorCount;

    VkDescriptorSetAllocateInfo textureAllocateInfo{};
    textureAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    textureAllocateInfo.pNext = &variableCount;
    textureAllocateInfo.descriptorPool = m_descriptorPool;
    textureAllocateInfo.descriptorSetCount = 1;
    textureAllocateInfo.pSetLayouts = &m_textureDescriptorSetLayout;
    ThrowIfFailed(vkAllocateDescriptorSets(m_device, &textureAllocateInfo, &m_textureDescriptorSet), "Failed to allocate texture descriptor set.");

    VkWriteDescriptorSetAccelerationStructureKHR asInfo{};
    asInfo.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
    asInfo.accelerationStructureCount = 1;
    asInfo.pAccelerationStructures = &m_tlas.handle;

    VkDescriptorImageInfo outputInfo{};
    outputInfo.imageView = m_outputImageView;
    outputInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkDescriptorImageInfo accumulationInfo{};
    accumulationInfo.imageView = m_accumulationImageView;
    accumulationInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    VkDescriptorImageInfo denoiseAov0Info{};
    denoiseAov0Info.imageView = m_denoiseAov0ImageView;
    denoiseAov0Info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    VkDescriptorImageInfo denoiseAov1Info{};
    denoiseAov1Info.imageView = m_denoiseAov1ImageView;
    denoiseAov1Info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkDescriptorBufferInfo uniformInfo{ m_sceneUniformBuffer.buffer, 0, sizeof(SceneConstants) };
    VkDescriptorBufferInfo vertexInfo{ m_vertexBuffer.buffer, 0, m_vertexBuffer.size };
    VkDescriptorBufferInfo indexInfo{ m_indexBuffer.buffer, 0, m_indexBuffer.size };
    VkDescriptorBufferInfo geometryInfo{ m_geometryBuffer.buffer, 0, m_geometryBuffer.size };
    VkDescriptorBufferInfo materialInfo{ m_materialBuffer.buffer, 0, m_materialBuffer.size };
    VkDescriptorBufferInfo lightInfo{ m_lightBuffer.buffer, 0, m_lightBuffer.size };
    VkDescriptorBufferInfo restirCurrentInfo{ m_restirReservoirCurrent.buffer, 0, m_restirReservoirBufferSize };
    VkDescriptorBufferInfo restirHistoryInfo{ m_restirReservoirHistory.buffer, 0, m_restirReservoirBufferSize };
    VkDescriptorBufferInfo restirSpatialInfo{ m_restirReservoirSpatial.buffer, 0, m_restirReservoirBufferSize };
    VkDescriptorBufferInfo enhancedGBufferCurrentInfo{ m_enhancedGBufferCurrent.buffer, 0, m_enhancedGBufferSize };
    VkDescriptorBufferInfo enhancedGBufferHistoryInfo{ m_enhancedGBufferHistory.buffer, 0, m_enhancedGBufferSize };
    VkDescriptorBufferInfo enhancedDuplicationCurrentInfo{ m_enhancedDuplicationCurrent.buffer, 0, m_enhancedDuplicationMapSize };
    VkDescriptorBufferInfo enhancedDuplicationHistoryInfo{ m_enhancedDuplicationHistory.buffer, 0, m_enhancedDuplicationMapSize };
    VkDescriptorBufferInfo enhancedReuseTextureInfo{ m_enhancedReuseTextureOffsets.buffer, 0, m_enhancedReuseTextureOffsets.size };
    VkDescriptorBufferInfo enhancedReplayTasksInfo{ m_enhancedReplayTasks.buffer, 0, m_enhancedReplayTaskBufferSize };
    VkDescriptorImageInfo samplerInfo{};
    samplerInfo.sampler = m_textureSampler;

    std::array<VkWriteDescriptorSet, 21> writes{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].pNext = &asInfo;
    writes[0].dstSet = m_descriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    writes[1] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_descriptorSet, 1, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &outputInfo, nullptr, nullptr };
    writes[2] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_descriptorSet, 2, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &accumulationInfo, nullptr, nullptr };
    writes[3] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_descriptorSet, 3, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &uniformInfo, nullptr };
    writes[4] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_descriptorSet, 4, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &vertexInfo, nullptr };
    writes[5] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_descriptorSet, 5, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &indexInfo, nullptr };
    writes[6] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_descriptorSet, 6, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &geometryInfo, nullptr };
    writes[7] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_descriptorSet, 7, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &materialInfo, nullptr };
    writes[8] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_descriptorSet, 8, 0, 1, VK_DESCRIPTOR_TYPE_SAMPLER, &samplerInfo, nullptr, nullptr };
    writes[9] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_descriptorSet, 9, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &restirCurrentInfo, nullptr };
    writes[10] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_descriptorSet, 10, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &restirHistoryInfo, nullptr };
    writes[11] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_descriptorSet, 11, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &restirSpatialInfo, nullptr };
    writes[12] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_descriptorSet, 12, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &lightInfo, nullptr };
    writes[13] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_descriptorSet, 13, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &denoiseAov0Info, nullptr, nullptr };
    writes[14] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_descriptorSet, 14, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &denoiseAov1Info, nullptr, nullptr };
    writes[15] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_descriptorSet, 15, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &enhancedGBufferCurrentInfo, nullptr };
    writes[16] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_descriptorSet, 16, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &enhancedGBufferHistoryInfo, nullptr };
    writes[17] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_descriptorSet, 17, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &enhancedDuplicationCurrentInfo, nullptr };
    writes[18] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_descriptorSet, 18, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &enhancedDuplicationHistoryInfo, nullptr };
    writes[19] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_descriptorSet, 19, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &enhancedReuseTextureInfo, nullptr };
    writes[20] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_descriptorSet, 20, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &enhancedReplayTasksInfo, nullptr };
    vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

    std::vector<VkDescriptorImageInfo> textureInfos(textureDescriptorCount);
    for (size_t materialIndex = 0; materialIndex < m_scene.materials.size(); ++materialIndex)
    {
        for (uint32_t slot = 0; slot < TextureSlotCount; ++slot)
        {
            const uint32_t descriptorIndex = static_cast<uint32_t>(materialIndex * TextureSlotCount + slot);
            const uint32_t textureIndex = m_materialTextureIndices[materialIndex][slot];
            textureInfos[descriptorIndex].imageView = m_textures[textureIndex].view;
            textureInfos[descriptorIndex].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }
    }
    if (m_scene.materials.empty() && !m_textures.empty())
    {
        textureInfos[0].imageView = m_textures[0].view;
        textureInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    if (m_environmentDescriptorIndex < textureInfos.size() && m_environmentTextureIndex < m_textures.size())
    {
        const GpuTexture& environment = m_textures[m_environmentTextureIndex];
        textureInfos[m_environmentDescriptorIndex].imageView = environment.view;
        textureInfos[m_environmentDescriptorIndex].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    VkWriteDescriptorSet textureWrite{};
    textureWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    textureWrite.dstSet = m_textureDescriptorSet;
    textureWrite.dstBinding = 0;
    textureWrite.descriptorCount = textureDescriptorCount;
    textureWrite.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    textureWrite.pImageInfo = textureInfos.data();
    vkUpdateDescriptorSets(m_device, 1, &textureWrite, 0, nullptr);
}

void BistroExteriorPathtracingVulkan::CreateRayTracingPipeline()
{
    if (m_rtPipelineProperties.maxRayRecursionDepth < MaxTraceRecursionDepth())
    {
        throw std::runtime_error("The selected Vulkan device does not support the ray recursion depth required by this Path Tracing variant.");
    }

    std::vector<char> shaderCode = ReadFile(GetExecutableDirectory() + ShaderFileName());
    VkShaderModule shaderModule = CreateShaderModule(shaderCode);

    struct ShaderStageDesc
    {
        VkShaderStageFlagBits stage;
        const char* entry;
    };
    const ShaderStageDesc stageDescs[] =
    {
        { VK_SHADER_STAGE_RAYGEN_BIT_KHR, "RayGen" },
        { VK_SHADER_STAGE_MISS_BIT_KHR, "Miss" },
        { VK_SHADER_STAGE_MISS_BIT_KHR, "ShadowMiss" },
        { VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, "ClosestHit" },
        { VK_SHADER_STAGE_ANY_HIT_BIT_KHR, "AnyHit" },
        { VK_SHADER_STAGE_ANY_HIT_BIT_KHR, "ShadowAnyHit" },
    };

    std::array<VkPipelineShaderStageCreateInfo, 6> stages{};
    for (size_t i = 0; i < stages.size(); ++i)
    {
        stages[i].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[i].stage = stageDescs[i].stage;
        stages[i].module = shaderModule;
        stages[i].pName = stageDescs[i].entry;
    }

    std::array<VkRayTracingShaderGroupCreateInfoKHR, 5> groups{};
    groups[0].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
    groups[0].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    groups[0].generalShader = 0;
    groups[0].closestHitShader = VK_SHADER_UNUSED_KHR;
    groups[0].anyHitShader = VK_SHADER_UNUSED_KHR;
    groups[0].intersectionShader = VK_SHADER_UNUSED_KHR;

    groups[1].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
    groups[1].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    groups[1].generalShader = 1;
    groups[1].closestHitShader = VK_SHADER_UNUSED_KHR;
    groups[1].anyHitShader = VK_SHADER_UNUSED_KHR;
    groups[1].intersectionShader = VK_SHADER_UNUSED_KHR;

    groups[2].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
    groups[2].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    groups[2].generalShader = 2;
    groups[2].closestHitShader = VK_SHADER_UNUSED_KHR;
    groups[2].anyHitShader = VK_SHADER_UNUSED_KHR;
    groups[2].intersectionShader = VK_SHADER_UNUSED_KHR;

    groups[3].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
    groups[3].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
    groups[3].generalShader = VK_SHADER_UNUSED_KHR;
    groups[3].closestHitShader = 3;
    groups[3].anyHitShader = 4;
    groups[3].intersectionShader = VK_SHADER_UNUSED_KHR;

    groups[4].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
    groups[4].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
    groups[4].generalShader = VK_SHADER_UNUSED_KHR;
    groups[4].closestHitShader = VK_SHADER_UNUSED_KHR;
    groups[4].anyHitShader = 5;
    groups[4].intersectionShader = VK_SHADER_UNUSED_KHR;

    VkDescriptorSetLayout setLayouts[] = { m_descriptorSetLayout, m_textureDescriptorSetLayout };
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = _countof(setLayouts);
    layoutInfo.pSetLayouts = setLayouts;
    ThrowIfFailed(vkCreatePipelineLayout(m_device, &layoutInfo, nullptr, &m_pipelineLayout), "Failed to create Path Tracing pipeline layout.");

    VkRayTracingPipelineCreateInfoKHR pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
    pipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
    pipelineInfo.pStages = stages.data();
    pipelineInfo.groupCount = static_cast<uint32_t>(groups.size());
    pipelineInfo.pGroups = groups.data();
    pipelineInfo.maxPipelineRayRecursionDepth = MaxTraceRecursionDepth();
    pipelineInfo.layout = m_pipelineLayout;
    ThrowIfFailed(m_vkCreateRayTracingPipelinesKHR(m_device, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_rayTracingPipeline), "Failed to create Path Tracing pipeline.");

    vkDestroyShaderModule(m_device, shaderModule, nullptr);
}

void BistroExteriorPathtracingVulkan::CreateRestirReusePipeline()
{
    if (!UsesRestirReuse(m_mode))
    {
        return;
    }

    std::vector<char> shaderCode = ReadFile(GetExecutableDirectory() + RestirReuseShaderFileName());
    VkShaderModule shaderModule = CreateShaderModule(shaderCode);

    VkPipelineShaderStageCreateInfo stageInfo{};
    stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = shaderModule;
    stageInfo.pName = "RestirTemporalSpatialCS";

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = m_pipelineLayout;
    ThrowIfFailed(vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_restirReusePipeline), "Failed to create ReSTIR reuse compute pipeline.");

    vkDestroyShaderModule(m_device, shaderModule, nullptr);
}

void BistroExteriorPathtracingVulkan::CreateDenoisePipeline()
{
    std::vector<char> shaderCode = ReadFile(GetExecutableDirectory() + DenoiseShaderFileName());
    VkShaderModule shaderModule = CreateShaderModule(shaderCode);

    VkPipelineShaderStageCreateInfo stageInfo{};
    stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = shaderModule;
    stageInfo.pName = "DenoiseCS";

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = m_pipelineLayout;
    ThrowIfFailed(vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_denoisePipeline), "Failed to create Path Tracing denoise compute pipeline.");

    vkDestroyShaderModule(m_device, shaderModule, nullptr);
}

void BistroExteriorPathtracingVulkan::CreateShaderBindingTables()
{
    const uint32_t groupCount = 5;
    const uint32_t handleSize = m_rtPipelineProperties.shaderGroupHandleSize;
    const uint32_t handleAlignment = m_rtPipelineProperties.shaderGroupHandleAlignment;
    const uint32_t groupBaseAlignment = m_rtPipelineProperties.shaderGroupBaseAlignment;
    const uint32_t recordSize = static_cast<uint32_t>(Align(handleSize, handleAlignment));
    std::vector<uint8_t> handles(static_cast<size_t>(groupCount) * handleSize);
    ThrowIfFailed(m_vkGetRayTracingShaderGroupHandlesKHR(m_device, m_rayTracingPipeline, 0, groupCount, handles.size(), handles.data()), "Failed to read shader group handles.");

    auto createTable = [&](uint32_t firstGroup, uint32_t recordCount, ShaderBindingTable& table)
    {
        const VkDeviceSize bufferSize = Align(static_cast<VkDeviceSize>(recordSize) * recordCount, groupBaseAlignment);
        CreateBuffer(bufferSize, VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, table.buffer);
        uint8_t* mapped = nullptr;
        vkMapMemory(m_device, table.buffer.memory, 0, bufferSize, 0, reinterpret_cast<void**>(&mapped));
        memset(mapped, 0, static_cast<size_t>(bufferSize));
        for (uint32_t record = 0; record < recordCount; ++record)
        {
            memcpy(mapped + record * recordSize, handles.data() + (firstGroup + record) * handleSize, handleSize);
        }
        vkUnmapMemory(m_device, table.buffer.memory);
        table.region.deviceAddress = table.buffer.address;
        table.region.stride = recordSize;
        table.region.size = static_cast<VkDeviceSize>(recordSize) * recordCount;
        table.recordCount = recordCount;
    };

    createTable(0, 1, m_rayGenSbt);
    createTable(1, 2, m_missSbt);
    createTable(3, 2, m_hitSbt);
}

void BistroExteriorPathtracingVulkan::BuildAccelerationStructures()
{
    std::vector<VkAccelerationStructureGeometryKHR> geometries(m_scene.draws.size());
    std::vector<VkAccelerationStructureBuildRangeInfoKHR> ranges(m_scene.draws.size());
    std::vector<uint32_t> primitiveCounts(m_scene.draws.size());

    for (size_t i = 0; i < m_scene.draws.size(); ++i)
    {
        const Bistro::DrawItem& draw = m_scene.draws[i];
        const bool alphaMasked = m_scene.materials[draw.materialIndex].alphaMasked;

        VkAccelerationStructureGeometryTrianglesDataKHR triangles{};
        triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
        triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
        triangles.vertexData.deviceAddress = m_vertexBuffer.address;
        triangles.vertexStride = sizeof(Bistro::Vertex);
        triangles.maxVertex = static_cast<uint32_t>(m_scene.vertices.size() - 1);
        triangles.indexType = VK_INDEX_TYPE_UINT32;
        triangles.indexData.deviceAddress = m_indexBuffer.address + static_cast<VkDeviceAddress>(draw.startIndex) * sizeof(uint32_t);

        geometries[i].sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        geometries[i].geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
        geometries[i].flags = alphaMasked ? 0 : VK_GEOMETRY_OPAQUE_BIT_KHR;
        geometries[i].geometry.triangles = triangles;

        ranges[i].primitiveCount = draw.indexCount / 3;
        ranges[i].primitiveOffset = 0;
        ranges[i].firstVertex = 0;
        ranges[i].transformOffset = 0;
        primitiveCounts[i] = ranges[i].primitiveCount;
    }

    VkAccelerationStructureBuildGeometryInfoKHR blasBuildInfo{};
    blasBuildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    blasBuildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    blasBuildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    blasBuildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    blasBuildInfo.geometryCount = static_cast<uint32_t>(geometries.size());
    blasBuildInfo.pGeometries = geometries.data();

    VkAccelerationStructureBuildSizesInfoKHR blasSizeInfo{};
    blasSizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    m_vkGetAccelerationStructureBuildSizesKHR(m_device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &blasBuildInfo, primitiveCounts.data(), &blasSizeInfo);

    CreateBuffer(blasSizeInfo.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_blas.buffer);
    VkAccelerationStructureCreateInfoKHR blasCreateInfo{};
    blasCreateInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    blasCreateInfo.buffer = m_blas.buffer.buffer;
    blasCreateInfo.size = blasSizeInfo.accelerationStructureSize;
    blasCreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    ThrowIfFailed(m_vkCreateAccelerationStructureKHR(m_device, &blasCreateInfo, nullptr, &m_blas.handle), "Failed to create BLAS.");

    GpuBuffer blasScratch;
    CreateBuffer(blasSizeInfo.buildScratchSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, blasScratch);
    blasBuildInfo.dstAccelerationStructure = m_blas.handle;
    blasBuildInfo.scratchData.deviceAddress = blasScratch.address;

    VkCommandBuffer commandBuffer = BeginSingleTimeCommands();
    const VkAccelerationStructureBuildRangeInfoKHR* blasRangeData = ranges.data();
    m_vkCmdBuildAccelerationStructuresKHR(commandBuffer, 1, &blasBuildInfo, &blasRangeData);
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, 0, 1, &barrier, 0, nullptr, 0, nullptr);
    EndSingleTimeCommands(commandBuffer);
    DestroyBuffer(blasScratch);

    VkAccelerationStructureDeviceAddressInfoKHR blasAddressInfo{};
    blasAddressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    blasAddressInfo.accelerationStructure = m_blas.handle;
    m_blas.address = m_vkGetAccelerationStructureDeviceAddressKHR(m_device, &blasAddressInfo);

    VkAccelerationStructureInstanceKHR instance{};
    instance.transform.matrix[0][0] = 1.0f;
    instance.transform.matrix[1][1] = 1.0f;
    instance.transform.matrix[2][2] = 1.0f;
    instance.instanceCustomIndex = 0;
    instance.mask = 0xff;
    instance.instanceShaderBindingTableRecordOffset = 0;
    instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
    instance.accelerationStructureReference = m_blas.address;
    UploadBuffer(&instance, sizeof(instance), VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, m_instanceBuffer);

    VkAccelerationStructureGeometryInstancesDataKHR instancesData{};
    instancesData.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    instancesData.arrayOfPointers = VK_FALSE;
    instancesData.data.deviceAddress = m_instanceBuffer.address;

    VkAccelerationStructureGeometryKHR tlasGeometry{};
    tlasGeometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    tlasGeometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    tlasGeometry.geometry.instances = instancesData;

    VkAccelerationStructureBuildRangeInfoKHR tlasRange{};
    tlasRange.primitiveCount = 1;
    uint32_t tlasPrimitiveCount = 1;

    VkAccelerationStructureBuildGeometryInfoKHR tlasBuildInfo{};
    tlasBuildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    tlasBuildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    tlasBuildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    tlasBuildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    tlasBuildInfo.geometryCount = 1;
    tlasBuildInfo.pGeometries = &tlasGeometry;

    VkAccelerationStructureBuildSizesInfoKHR tlasSizeInfo{};
    tlasSizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    m_vkGetAccelerationStructureBuildSizesKHR(m_device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &tlasBuildInfo, &tlasPrimitiveCount, &tlasSizeInfo);

    CreateBuffer(tlasSizeInfo.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_tlas.buffer);
    VkAccelerationStructureCreateInfoKHR tlasCreateInfo{};
    tlasCreateInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    tlasCreateInfo.buffer = m_tlas.buffer.buffer;
    tlasCreateInfo.size = tlasSizeInfo.accelerationStructureSize;
    tlasCreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    ThrowIfFailed(m_vkCreateAccelerationStructureKHR(m_device, &tlasCreateInfo, nullptr, &m_tlas.handle), "Failed to create TLAS.");

    GpuBuffer tlasScratch;
    CreateBuffer(tlasSizeInfo.buildScratchSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, tlasScratch);
    tlasBuildInfo.dstAccelerationStructure = m_tlas.handle;
    tlasBuildInfo.scratchData.deviceAddress = tlasScratch.address;

    commandBuffer = BeginSingleTimeCommands();
    const VkAccelerationStructureBuildRangeInfoKHR* tlasRangeData = &tlasRange;
    m_vkCmdBuildAccelerationStructuresKHR(commandBuffer, 1, &tlasBuildInfo, &tlasRangeData);
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, 0, 1, &barrier, 0, nullptr, 0, nullptr);
    EndSingleTimeCommands(commandBuffer);
    DestroyBuffer(tlasScratch);

    VkAccelerationStructureDeviceAddressInfoKHR tlasAddressInfo{};
    tlasAddressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    tlasAddressInfo.accelerationStructure = m_tlas.handle;
    m_tlas.address = m_vkGetAccelerationStructureDeviceAddressKHR(m_device, &tlasAddressInfo);
}

void BistroExteriorPathtracingVulkan::CreateCommandBuffers()
{
    m_commandBuffers.resize(m_framebuffers.size());
    VkCommandBufferAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocateInfo.commandPool = m_commandPool;
    allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocateInfo.commandBufferCount = static_cast<uint32_t>(m_commandBuffers.size());
    ThrowIfFailed(vkAllocateCommandBuffers(m_device, &allocateInfo, m_commandBuffers.data()), "Failed to allocate command buffers.");
}

void BistroExteriorPathtracingVulkan::RecordCommandBuffer(uint32_t imageIndex)
{
    VkCommandBuffer commandBuffer = m_commandBuffers[imageIndex];
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    ThrowIfFailed(vkBeginCommandBuffer(commandBuffer, &beginInfo), "Failed to begin command buffer.");

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_rayTracingPipeline);
    VkDescriptorSet descriptorSets[] = { m_descriptorSet, m_textureDescriptorSet };
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_pipelineLayout, 0, _countof(descriptorSets), descriptorSets, 0, nullptr);
    VkStridedDeviceAddressRegionKHR callableRegion{};
    m_vkCmdTraceRaysKHR(commandBuffer, &m_rayGenSbt.region, &m_missSbt.region, &m_hitSbt.region, &callableRegion, m_swapChainExtent.width, m_swapChainExtent.height, 1);
    RecordRestirReusePass(commandBuffer);
    RecordDenoisePass(commandBuffer);

    VkImageMemoryBarrier outputToCopy{};
    outputToCopy.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    outputToCopy.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    outputToCopy.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    outputToCopy.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    outputToCopy.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    outputToCopy.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    outputToCopy.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    outputToCopy.image = m_outputImage;
    outputToCopy.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    outputToCopy.subresourceRange.levelCount = 1;
    outputToCopy.subresourceRange.layerCount = 1;

    VkImageMemoryBarrier swapToCopy{};
    swapToCopy.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    swapToCopy.srcAccessMask = 0;
    swapToCopy.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    swapToCopy.oldLayout = m_swapChainImageInitialized[imageIndex] ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_UNDEFINED;
    swapToCopy.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    swapToCopy.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    swapToCopy.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    swapToCopy.image = m_swapChainImages[imageIndex];
    swapToCopy.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    swapToCopy.subresourceRange.levelCount = 1;
    swapToCopy.subresourceRange.layerCount = 1;

    VkImageMemoryBarrier copyBarriers[] = { outputToCopy, swapToCopy };
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, _countof(copyBarriers), copyBarriers);

    VkImageCopy region{};
    region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.srcSubresource.layerCount = 1;
    region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.dstSubresource.layerCount = 1;
    region.extent = { m_swapChainExtent.width, m_swapChainExtent.height, 1 };
    vkCmdCopyImage(commandBuffer, m_outputImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_swapChainImages[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    VkImageMemoryBarrier outputToGeneral = outputToCopy;
    outputToGeneral.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    outputToGeneral.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    outputToGeneral.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    outputToGeneral.newLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkImageMemoryBarrier swapToColor = swapToCopy;
    swapToColor.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    swapToColor.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    swapToColor.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    swapToColor.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkImageMemoryBarrier afterCopyBarriers[] = { outputToGeneral, swapToColor };
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, _countof(afterCopyBarriers), afterCopyBarriers);

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = m_renderPass;
    renderPassInfo.framebuffer = m_framebuffers[imageIndex];
    renderPassInfo.renderArea.extent = m_swapChainExtent;
    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
    vkCmdEndRenderPass(commandBuffer);

    m_swapChainImageInitialized[imageIndex] = true;
    ThrowIfFailed(vkEndCommandBuffer(commandBuffer), "Failed to record command buffer.");
}

void BistroExteriorPathtracingVulkan::RecordRestirReusePass(VkCommandBuffer commandBuffer)
{
    if (!UsesRestirReuse(m_mode) || m_restirReusePipeline == VK_NULL_HANDLE)
    {
        return;
    }

    VkBufferMemoryBarrier toCompute[8]{};
    VkBuffer buffers[] =
    {
        m_restirReservoirCurrent.buffer,
        m_restirReservoirHistory.buffer,
        m_restirReservoirSpatial.buffer,
        m_enhancedGBufferCurrent.buffer,
        m_enhancedGBufferHistory.buffer,
        m_enhancedDuplicationCurrent.buffer,
        m_enhancedDuplicationHistory.buffer,
        m_enhancedReplayTasks.buffer
    };
    VkDeviceSize bufferSizes[] =
    {
        m_restirReservoirBufferSize,
        m_restirReservoirBufferSize,
        m_restirReservoirBufferSize,
        m_enhancedGBufferSize,
        m_enhancedGBufferSize,
        m_enhancedDuplicationMapSize,
        m_enhancedDuplicationMapSize,
        m_enhancedReplayTaskBufferSize
    };
    for (uint32_t i = 0; i < _countof(toCompute); ++i)
    {
        toCompute[i].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        toCompute[i].srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        toCompute[i].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        toCompute[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toCompute[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toCompute[i].buffer = buffers[i];
        toCompute[i].offset = 0;
        toCompute[i].size = bufferSizes[i];
    }
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, _countof(toCompute), toCompute, 0, nullptr);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_restirReusePipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelineLayout, 0, 1, &m_descriptorSet, 0, nullptr);
    vkCmdDispatch(commandBuffer, (m_swapChainExtent.width + 7) / 8, (m_swapChainExtent.height + 7) / 8, 1);

    VkBufferMemoryBarrier toCopy[2]{};
    toCopy[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    toCopy[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    toCopy[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    toCopy[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toCopy[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toCopy[0].buffer = m_restirReservoirSpatial.buffer;
    toCopy[0].offset = 0;
    toCopy[0].size = m_restirReservoirBufferSize;
    toCopy[1].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    toCopy[1].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    toCopy[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toCopy[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toCopy[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toCopy[1].buffer = m_restirReservoirHistory.buffer;
    toCopy[1].offset = 0;
    toCopy[1].size = m_restirReservoirBufferSize;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, _countof(toCopy), toCopy, 0, nullptr);

    VkBufferCopy copyRegion{};
    copyRegion.size = m_restirReservoirBufferSize;
    vkCmdCopyBuffer(commandBuffer, m_restirReservoirSpatial.buffer, m_restirReservoirHistory.buffer, 1, &copyRegion);

    VkBufferMemoryBarrier afterCopy[2]{};
    afterCopy[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    afterCopy[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    afterCopy[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    afterCopy[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    afterCopy[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    afterCopy[0].buffer = m_restirReservoirSpatial.buffer;
    afterCopy[0].offset = 0;
    afterCopy[0].size = m_restirReservoirBufferSize;
    afterCopy[1].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    afterCopy[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    afterCopy[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    afterCopy[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    afterCopy[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    afterCopy[1].buffer = m_restirReservoirHistory.buffer;
    afterCopy[1].offset = 0;
    afterCopy[1].size = m_restirReservoirBufferSize;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, _countof(afterCopy), afterCopy, 0, nullptr);

    if (UsesRestirPtEnhanced(m_mode))
    {
        VkBufferMemoryBarrier enhancedToCopy[4]{};
        VkBuffer enhancedBuffers[] =
        {
            m_enhancedGBufferCurrent.buffer,
            m_enhancedGBufferHistory.buffer,
            m_enhancedDuplicationCurrent.buffer,
            m_enhancedDuplicationHistory.buffer
        };
        VkDeviceSize enhancedSizes[] =
        {
            m_enhancedGBufferSize,
            m_enhancedGBufferSize,
            m_enhancedDuplicationMapSize,
            m_enhancedDuplicationMapSize
        };
        for (uint32_t i = 0; i < _countof(enhancedToCopy); ++i)
        {
            enhancedToCopy[i].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            enhancedToCopy[i].srcAccessMask = (i % 2u == 0u) ? VK_ACCESS_SHADER_WRITE_BIT : VK_ACCESS_SHADER_READ_BIT;
            enhancedToCopy[i].dstAccessMask = (i % 2u == 0u) ? VK_ACCESS_TRANSFER_READ_BIT : VK_ACCESS_TRANSFER_WRITE_BIT;
            enhancedToCopy[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            enhancedToCopy[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            enhancedToCopy[i].buffer = enhancedBuffers[i];
            enhancedToCopy[i].offset = 0;
            enhancedToCopy[i].size = enhancedSizes[i];
        }
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, _countof(enhancedToCopy), enhancedToCopy, 0, nullptr);

        VkBufferCopy gbufferCopy{};
        gbufferCopy.size = m_enhancedGBufferSize;
        vkCmdCopyBuffer(commandBuffer, m_enhancedGBufferCurrent.buffer, m_enhancedGBufferHistory.buffer, 1, &gbufferCopy);
        VkBufferCopy duplicationCopy{};
        duplicationCopy.size = m_enhancedDuplicationMapSize;
        vkCmdCopyBuffer(commandBuffer, m_enhancedDuplicationCurrent.buffer, m_enhancedDuplicationHistory.buffer, 1, &duplicationCopy);

        VkBufferMemoryBarrier enhancedAfterCopy[4]{};
        for (uint32_t i = 0; i < _countof(enhancedAfterCopy); ++i)
        {
            enhancedAfterCopy[i].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            enhancedAfterCopy[i].srcAccessMask = (i % 2u == 0u) ? VK_ACCESS_TRANSFER_READ_BIT : VK_ACCESS_TRANSFER_WRITE_BIT;
            enhancedAfterCopy[i].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            enhancedAfterCopy[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            enhancedAfterCopy[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            enhancedAfterCopy[i].buffer = enhancedBuffers[i];
            enhancedAfterCopy[i].offset = 0;
            enhancedAfterCopy[i].size = enhancedSizes[i];
        }
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, _countof(enhancedAfterCopy), enhancedAfterCopy, 0, nullptr);
    }
}

void BistroExteriorPathtracingVulkan::RecordDenoisePass(VkCommandBuffer commandBuffer)
{
    if (!m_denoiserEnabled || m_debugViewMode != 0 || m_denoisePipeline == VK_NULL_HANDLE)
    {
        return;
    }

    VkImage images[] =
    {
        m_outputImage,
        m_accumulationImage,
        m_denoiseAov0Image,
        m_denoiseAov1Image
    };
    VkImageMemoryBarrier toCompute[_countof(images)]{};
    for (uint32_t i = 0; i < _countof(images); ++i)
    {
        toCompute[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toCompute[i].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        toCompute[i].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        toCompute[i].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        toCompute[i].newLayout = VK_IMAGE_LAYOUT_GENERAL;
        toCompute[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toCompute[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toCompute[i].image = images[i];
        toCompute[i].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        toCompute[i].subresourceRange.levelCount = 1;
        toCompute[i].subresourceRange.layerCount = 1;
    }
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, _countof(toCompute), toCompute);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_denoisePipeline);
    VkDescriptorSet descriptorSets[] = { m_descriptorSet, m_textureDescriptorSet };
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelineLayout, 0, _countof(descriptorSets), descriptorSets, 0, nullptr);
    vkCmdDispatch(commandBuffer, (m_swapChainExtent.width + 7) / 8, (m_swapChainExtent.height + 7) / 8, 1);
}

void BistroExteriorPathtracingVulkan::CreateSyncObjects()
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

void BistroExteriorPathtracingVulkan::InitializeImGui()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;

    ImGui_ImplWin32_Init(m_hwnd);
    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.ApiVersion = VK_API_VERSION_1_2;
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

void BistroExteriorPathtracingVulkan::ShutdownImGui()
{
    if (ImGui::GetCurrentContext())
    {
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
    }
}

void BistroExteriorPathtracingVulkan::BuildUI()
{
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2(24.0f, 48.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(390.0f, 520.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Bistro Controls");
    ImGui::PushItemWidth(190.0f);
    ImGui::TextUnformatted("Directional Light");
    if (ImGui::SliderFloat3("Light Direction", m_lightDirection, -1.0f, 1.0f)) ResetAccumulation();
    if (ImGui::ColorEdit3("Light Color", m_lightColor)) ResetAccumulation();
    if (ImGui::SliderFloat("Light Intensity", &m_lightIntensity, 0.0f, 20.0f, "%.2f")) ResetAccumulation();
    if (ImGui::Button("Reset Light")) { ResetLight(); ResetAccumulation(); }
    ImGui::Separator();
    ImGui::TextUnformatted("Camera");
    DirectX::XMFLOAT3 cameraPosition = m_camera.GetPosition();
    float cameraAngles[] =
    {
        DirectX::XMConvertToDegrees(m_camera.GetYawRadians()),
        DirectX::XMConvertToDegrees(m_camera.GetPitchRadians())
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
        m_camera.Reset(cameraPosition, DirectX::XMConvertToRadians(cameraAngles[0]), DirectX::XMConvertToRadians(cameraAngles[1]));
        ResetAccumulation();
    }
    if (ImGui::Button("Reset Camera View")) { ResetCameraView(); ResetAccumulation(); }
    if (ImGui::SliderFloat("Move Speed", &m_baseMoveSpeed, 0.1f, 50.0f, "%.1f")) m_camera.SetMoveSpeeds(m_baseMoveSpeed, m_fastMoveSpeed);
    if (ImGui::SliderFloat("Fast Speed", &m_fastMoveSpeed, 0.1f, 100.0f, "%.1f")) m_camera.SetMoveSpeeds(m_baseMoveSpeed, m_fastMoveSpeed);
    if (ImGui::Button("Reset Camera Speed")) ResetCameraSpeeds();
    ImGui::Separator();
    ImGui::TextUnformatted("Path Tracing");
    const char* debugModes[] =
    {
        "Final", "Base Color", "World Normal", "Normal Texture", "Roughness", "Metallic", "Emissive",
        "Hit Distance", "Direct NEE", "Indirect", "Bounce Count", "Accumulation Samples", "Sky",
        "Reservoir Weight", "Temporal Reuse", "Spatial Reuse", "Enhanced Reservoir", "Enhanced Path Depth",
        "Enhanced Reconnection", "Enhanced Paired Spatial", "Enhanced Duplication", "Enhanced Replay Tasks",
        "Enhanced Replay Valid", "Enhanced Replay Radiance", "Enhanced Replay Ratio", "Enhanced Forced NEE"
    };
    if (ImGui::Combo("Debug View", &m_debugViewMode, debugModes, _countof(debugModes))) ResetAccumulation();
    if (ImGui::Checkbox("Normal Map Y Flip", &m_debugNormalMapYFlip)) ResetAccumulation();
    if (ImGui::SliderFloat("Ray Bias / TMin", &m_rayTMin, 0.001f, 0.25f, "%.3f")) ResetAccumulation();
    if (ImGui::Checkbox("Sky Enabled", &m_skyEnabled)) ResetAccumulation();
    if (ImGui::ColorEdit3("Sky Color", m_skyColor)) ResetAccumulation();
    if (ImGui::ColorEdit3("Sky Horizon Color", m_skyHorizonColor)) ResetAccumulation();
    if (ImGui::ColorEdit3("Sky Zenith Color", m_skyZenithColor)) ResetAccumulation();
    if (ImGui::ColorEdit3("Sky Ground Color", m_skyGroundColor)) ResetAccumulation();
    if (ImGui::SliderFloat("Sky Intensity", &m_skyIntensity, 0.0f, 10.0f, "%.2f")) ResetAccumulation();
    if (ImGui::SliderFloat("Sun Intensity", &m_sunIntensity, 0.0f, 50.0f, "%.2f")) ResetAccumulation();
    if (ImGui::SliderFloat("Sun Size", &m_sunAngularRadius, 0.001f, 0.08f, "%.3f")) ResetAccumulation();
    if (ImGui::Checkbox("Environment Map", &m_environmentMapEnabled)) ResetAccumulation();
    if (ImGui::SliderFloat("Environment Intensity", &m_environmentIntensity, 0.0f, 10.0f, "%.2f")) ResetAccumulation();
    if (ImGui::SliderFloat("Environment Rotation", &m_environmentRotation, -3.14159f, 3.14159f, "%.2f")) ResetAccumulation();
    if (ImGui::Checkbox("Sun NEE", &m_shadowEnabled)) ResetAccumulation();
    if (ImGui::Checkbox("Sky NEE", &m_skyNeeEnabled)) ResetAccumulation();
    if (ImGui::Checkbox("Emissive Triangle Lights", &m_emissiveLightsEnabled)) ResetAccumulation();
    if (ImGui::SliderFloat("Emissive Intensity", &m_emissiveLightIntensity, 0.0f, 30.0f, "%.2f")) ResetAccumulation();
    if (ImGui::Checkbox("Procedural Area Lights", &m_proceduralLightsEnabled)) ResetAccumulation();
    if (ImGui::SliderFloat("Area Light Intensity", &m_proceduralLightIntensity, 0.0f, 50.0f, "%.2f")) ResetAccumulation();
    ImGui::Separator();
    ImGui::TextUnformatted("Path Tracing");
    if (ImGui::SliderInt("Samples / Frame", &m_giSamplesPerFrame, 1, 8)) ResetAccumulation();
    if (ImGui::SliderInt("Max Bounces", &m_maxPathBounces, 1, 8)) ResetAccumulation();
    if (ImGui::SliderInt("Min Bounces", &m_minPathBounces, 0, 4)) ResetAccumulation();
    if (m_minPathBounces > m_maxPathBounces) m_minPathBounces = m_maxPathBounces;
    if (ImGui::SliderFloat("Radiance Clamp", &m_giRadianceClamp, 1.0f, 100.0f, "%.1f")) ResetAccumulation();
    if (ImGui::SliderFloat("Temporal Clamp", &m_giTemporalClampScale, 0.25f, 4.0f, "%.2f")) ResetAccumulation();
    if (ImGui::SliderInt("Max Accum Samples", &m_maxAccumulatedFrames, 1, 4096)) ResetAccumulation();
    ImGui::Checkbox("Freeze Accumulation", &m_freezeAccumulation);
    if (ImGui::Button("Reset Accumulation")) ResetAccumulation();
    ImGui::Separator();
    ImGui::TextUnformatted("Denoiser");
    ImGui::Checkbox("Denoiser Enabled", &m_denoiserEnabled);
    ImGui::SliderInt("Spatial Iterations", &m_denoiserSpatialIterations, 0, 4);
    ImGui::SliderFloat("Normal Sigma", &m_denoiserNormalSigma, 0.05f, 1.0f, "%.2f");
    ImGui::SliderFloat("Depth Sigma", &m_denoiserDepthSigma, 0.002f, 0.10f, "%.3f");
    ImGui::SliderFloat("Luminance Sigma", &m_denoiserLuminanceSigma, 0.1f, 8.0f, "%.2f");
    ImGui::SliderFloat("Albedo Sigma", &m_denoiserAlbedoSigma, 0.05f, 1.0f, "%.2f");
    ImGui::SliderFloat("Denoiser Strength", &m_denoiserStrength, 0.0f, 1.0f, "%.2f");
    if (UsesRestirReuse(m_mode))
    {
        ImGui::Separator();
        ImGui::TextUnformatted(PathtracingModeName(m_mode));
        if (ImGui::Checkbox("Temporal Reuse", &m_restirTemporalReuse)) ResetAccumulation();
        if (ImGui::SliderInt("Spatial Reuse Passes", &m_restirSpatialReusePasses, 0, UsesRestirPtEnhanced(m_mode) ? 3 : 4)) ResetAccumulation();
        if (ImGui::SliderInt("Spatial Radius", &m_restirSpatialRadius, 1, 64)) ResetAccumulation();
        if (ImGui::SliderInt("Candidate Samples / Pixel", &m_restirCandidateSamples, 1, 4)) ResetAccumulation();
        if (ImGui::SliderFloat("Reservoir M Clamp", &m_restirMClamp, 1.0f, 64.0f, "%.1f")) ResetAccumulation();
        if (UsesRestirPtEnhanced(m_mode))
        {
            if (ImGui::Checkbox("Paired Spatial Reuse", &m_enhancedPairedSpatial)) ResetAccumulation();
            if (ImGui::Checkbox("Duplication Decorrelation", &m_enhancedDuplicationDecorrelate)) ResetAccumulation();
            if (ImGui::Checkbox("Vector Color Reuse", &m_enhancedColorReuse)) ResetAccumulation();
            if (ImGui::Checkbox("Initial Russian Roulette", &m_enhancedRussianRoulette)) ResetAccumulation();
            if (ImGui::Checkbox("Replay Compaction", &m_enhancedReplayCompaction)) ResetAccumulation();
            if (ImGui::Checkbox("Forced NEE Reconnection", &m_enhancedForcedNeeReconnection)) ResetAccumulation();
            if (ImGui::SliderFloat("Footprint C", &m_enhancedFootprintC, 0.001f, 0.08f, "%.3f")) ResetAccumulation();
            if (ImGui::SliderFloat("Roughness Alpha Min", &m_enhancedRoughnessAlphaMin, 0.02f, 0.8f, "%.2f")) ResetAccumulation();
            if (ImGui::SliderInt("Primary RIS Candidates", &m_enhancedPrimaryRisCandidates, 1, 32)) ResetAccumulation();
            if (ImGui::SliderFloat("Temporal Cap Default", &m_enhancedTemporalCapDefault, 1.0f, 64.0f, "%.1f")) ResetAccumulation();
            if (ImGui::SliderFloat("Temporal Cap Min", &m_enhancedTemporalCapMin, 1.0f, 16.0f, "%.1f")) ResetAccumulation();
            if (ImGui::SliderFloat("Duplication Alpha", &m_enhancedDuplicationAlpha, 0.05f, 1.0f, "%.2f")) ResetAccumulation();
        }
        if (ImGui::Button("Reset Reservoirs")) ResetAccumulation();
    }
    ImGui::PopItemWidth();
    ImGui::End();

    BuildRendererStatsUI();
    ImGui::Render();
}

void BistroExteriorPathtracingVulkan::BuildRendererStatsUI()
{
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
    ImGui::SetNextWindowSize(ImVec2(360.0f, 360.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Renderer Stats");
    ImGui::Text("API: Vulkan Path Tracing");
    ImGui::Text("FPS: %.1f", fps);
    ImGui::Text("Frame Time: %.3f ms", frameTimeMs);
    uint32_t outputWidth = m_swapChainExtent.width;
    uint32_t outputHeight = m_swapChainExtent.height;
    if (Bistro::DrawResolutionCombo(m_swapChainExtent.width, m_swapChainExtent.height, outputWidth, outputHeight))
    {
        SetOutputResolution(outputWidth, outputHeight);
    }
    ImGui::Separator();
    ImGui::Text("Materials: %zu", m_scene.materials.size());
    ImGui::Text("Textures: %zu", m_textures.size());
    ImGui::Text("Vertices: %zu", m_scene.vertices.size());
    ImGui::Text("Indices: %zu", m_scene.indices.size());
    ImGui::Text("Submitted Indices: %llu", static_cast<unsigned long long>(submittedIndexCount));
    ImGui::Text("Primitives: %llu", static_cast<unsigned long long>(primitiveCount));
    ImGui::Text("BLAS Geometries: %zu", m_geometryRecords.size());
    ImGui::Text("TLAS Instances: 1");
    ImGui::Text("SBT Records: %u", m_rayGenSbt.recordCount + m_missSbt.recordCount + m_hitSbt.recordCount);
    ImGui::Text("Light List: %u", m_activeLightCount);
    ImGui::Text("Emissive Tri Lights: %u", m_emissiveTriangleLightCount);
    ImGui::Text("Procedural Area Lights: %u", m_proceduralAreaLightCount);
    ImGui::Text("Environment: %s", m_environmentTexturePath.empty() ? "Procedural Sky" : "Texture");
    ImGui::Text("Output: %ux%u", m_swapChainExtent.width, m_swapChainExtent.height);
    ImGui::Text("Accumulated Samples: %u", m_accumulatedFrames);
    ImGui::Text("Mode: %s", PathtracingModeName(m_mode));
    if (UsesRestirPtEnhanced(m_mode))
    {
        const double gpuMegabytes =
            static_cast<double>(m_restirReservoirBufferSize * 3u + m_enhancedGBufferSize * 2u + m_enhancedDuplicationMapSize * 2u + m_enhancedReplayTaskBufferSize) / (1024.0 * 1024.0);
        ImGui::Text("Enhanced GPU Buffers: %.2f MB", gpuMegabytes);
        ImGui::Text("Reuse Texture Entries: %u", m_enhancedReuseTextureElementCount);
        ImGui::Text("Replay Task Slots: %u", m_restirReservoirElementCount);
    }
    ImGui::Text("Denoiser: %s (%d pass%s)", m_denoiserEnabled ? "On" : "Off", m_denoiserSpatialIterations, m_denoiserSpatialIterations == 1 ? "" : "es");
    ImGui::Separator();
    ImGui::Text("Max Recursion: %u", MaxTraceRecursionDepth());
    ImGui::Text("Shader Group Handle: %u bytes", m_rtPipelineProperties.shaderGroupHandleSize);
    ImGui::Text("Max Ray Recursion: %u", m_rtPipelineProperties.maxRayRecursionDepth);
    ImGui::End();
}

void BistroExteriorPathtracingVulkan::ResetLight()
{
    m_lightDirection[0] = -0.35f;
    m_lightDirection[1] = -0.8f;
    m_lightDirection[2] = 0.45f;
    m_lightColor[0] = 1.0f;
    m_lightColor[1] = 0.96f;
    m_lightColor[2] = 0.88f;
    m_lightIntensity = 4.0f;
}

void BistroExteriorPathtracingVulkan::ResetCameraView()
{
    m_camera.Reset(m_defaultCameraPosition, m_defaultCameraYaw, m_defaultCameraPitch);
}

void BistroExteriorPathtracingVulkan::ResetCameraSpeeds()
{
    m_baseMoveSpeed = 17.0f;
    m_fastMoveSpeed = 58.2f;
    m_camera.SetMoveSpeeds(m_baseMoveSpeed, m_fastMoveSpeed);
}

void BistroExteriorPathtracingVulkan::ResetAccumulation()
{
    m_accumulatedFrames = 0;
    m_resetAccumulationRequested = true;
    m_hasPreviousViewProjection = false;
}

bool BistroExteriorPathtracingVulkan::HasAccumulationStateChanged()
{
    DirectX::XMFLOAT3 cameraPosition = m_camera.GetPosition();
    DirectX::XMFLOAT4 cameraAndYaw(cameraPosition.x, cameraPosition.y, cameraPosition.z, m_camera.GetYawRadians() + m_camera.GetPitchRadians());
    DirectX::XMFLOAT4 lighting(m_lightDirection[0], m_lightDirection[1], m_lightDirection[2], m_lightIntensity + static_cast<float>(m_debugViewMode) + (m_shadowEnabled ? 1.0f : 0.0f) + (m_skyNeeEnabled ? 2.0f : 0.0f));
    DirectX::XMFLOAT4 giOptions(static_cast<float>(m_giSamplesPerFrame), m_giRadianceClamp, m_giTemporalClampScale, m_giTemporalClampMin);
    DirectX::XMFLOAT4 pathOptions(static_cast<float>(m_maxPathBounces), static_cast<float>(m_minPathBounces), static_cast<float>(m_restirCandidateSamples), UsesRestirReuse(m_mode) ? 1.0f : 0.0f);
    DirectX::XMFLOAT4 restirOptions(m_restirTemporalReuse ? 1.0f : 0.0f, static_cast<float>(m_restirSpatialReusePasses), static_cast<float>(m_restirSpatialRadius), m_restirMClamp);
    DirectX::XMFLOAT4 lightSystemOptions(
        (m_emissiveLightsEnabled ? m_emissiveLightIntensity : 0.0f) + (m_proceduralLightsEnabled ? m_proceduralLightIntensity : 0.0f),
        m_environmentMapEnabled ? m_environmentIntensity : 0.0f,
        m_environmentRotation,
        static_cast<float>(m_activeLightCount));
    const bool changed =
        m_resetAccumulationRequested ||
        memcmp(&cameraAndYaw, &m_lastCameraAndYaw, sizeof(DirectX::XMFLOAT4)) != 0 ||
        memcmp(&lighting, &m_lastLighting, sizeof(DirectX::XMFLOAT4)) != 0 ||
        memcmp(&giOptions, &m_lastGiOptions, sizeof(DirectX::XMFLOAT4)) != 0 ||
        memcmp(&pathOptions, &m_lastPathOptions, sizeof(DirectX::XMFLOAT4)) != 0 ||
        memcmp(&restirOptions, &m_lastRestirOptions, sizeof(DirectX::XMFLOAT4)) != 0 ||
        memcmp(&lightSystemOptions, &m_lastLightSystemOptions, sizeof(DirectX::XMFLOAT4)) != 0;
    m_lastCameraAndYaw = cameraAndYaw;
    m_lastLighting = lighting;
    m_lastGiOptions = giOptions;
    m_lastPathOptions = pathOptions;
    m_lastRestirOptions = restirOptions;
    m_lastLightSystemOptions = lightSystemOptions;
    m_resetAccumulationRequested = false;
    return changed;
}

void BistroExteriorPathtracingVulkan::UpdateUniformBuffer(float)
{
    if (HasAccumulationStateChanged())
    {
        ResetAccumulation();
    }

    const float aspectRatio = static_cast<float>(m_swapChainExtent.width) / static_cast<float>(m_swapChainExtent.height);
    DirectX::XMMATRIX view = m_camera.GetViewMatrix();
    DirectX::XMMATRIX projection = DirectX::XMMatrixPerspectiveFovLH(DirectX::XMConvertToRadians(60.0f), aspectRatio, 0.1f, 10000.0f);
    DirectX::XMMATRIX viewProjection = view * projection;
    DirectX::XMMATRIX inverseViewProjection = DirectX::XMMatrixInverse(nullptr, viewProjection);
    if (!m_hasPreviousViewProjection)
    {
        DirectX::XMStoreFloat4x4(&m_previousViewProjection, viewProjection);
        m_hasPreviousViewProjection = true;
    }

    SceneConstants constants{};
    DirectX::XMStoreFloat4x4(&constants.inverseViewProjection, inverseViewProjection);
    DirectX::XMFLOAT3 cameraPosition = m_camera.GetPosition();
    constants.cameraPosition = DirectX::XMFLOAT4(cameraPosition.x, cameraPosition.y, cameraPosition.z, 1.0f);
    DirectX::XMFLOAT3 lightDirection = NormalizeFloat3(m_lightDirection);
    constants.lightDirection = DirectX::XMFLOAT4(lightDirection.x, lightDirection.y, lightDirection.z, 0.0f);
    constants.lightColor = DirectX::XMFLOAT4(m_lightColor[0], m_lightColor[1], m_lightColor[2], m_lightIntensity);
    constants.debugOptions = DirectX::XMFLOAT4(static_cast<float>(m_debugViewMode), m_debugNormalMapYFlip ? 1.0f : 0.0f, m_shadowEnabled ? 1.0f : 0.0f, m_skyNeeEnabled ? 1.0f : 0.0f);
    constants.skyColor = DirectX::XMFLOAT4(m_skyColor[0], m_skyColor[1], m_skyColor[2], m_skyIntensity);
    constants.skyHorizonColor = DirectX::XMFLOAT4(m_skyHorizonColor[0], m_skyHorizonColor[1], m_skyHorizonColor[2], 0.0f);
    constants.skyZenithColor = DirectX::XMFLOAT4(m_skyZenithColor[0], m_skyZenithColor[1], m_skyZenithColor[2], 0.0f);
    constants.skyGroundColor = DirectX::XMFLOAT4(m_skyGroundColor[0], m_skyGroundColor[1], m_skyGroundColor[2], 0.0f);
    constants.skyOptions = DirectX::XMFLOAT4(m_sunIntensity, m_sunAngularRadius, m_skyGroundBlend, m_skyEnabled ? 1.0f : 0.0f);
    constants.rayOptions = DirectX::XMFLOAT4(m_rayTMin, m_rayTMax, static_cast<float>(m_swapChainExtent.width), static_cast<float>(m_swapChainExtent.height));
    constants.frameOptions = DirectX::XMFLOAT4(static_cast<float>(m_accumulatedFrames), static_cast<float>(m_maxAccumulatedFrames), m_freezeAccumulation ? 1.0f : 0.0f, static_cast<float>(m_frameCounter));
    constants.giOptions = DirectX::XMFLOAT4(static_cast<float>(m_giSamplesPerFrame), m_giRadianceClamp, m_giTemporalClampScale, m_giTemporalClampMin);
    constants.pathOptions = DirectX::XMFLOAT4(static_cast<float>(m_maxPathBounces), static_cast<float>(m_minPathBounces), static_cast<float>(m_restirCandidateSamples), UsesRestirReuse(m_mode) ? 1.0f : 0.0f);
    constants.restirOptions = DirectX::XMFLOAT4(m_restirTemporalReuse ? 1.0f : 0.0f, static_cast<float>(m_restirSpatialReusePasses), static_cast<float>(m_restirSpatialRadius), m_restirMClamp);
    constants.lightOptions = DirectX::XMFLOAT4(static_cast<float>(m_activeLightCount), m_emissiveLightsEnabled ? m_emissiveLightIntensity : 0.0f, m_proceduralLightsEnabled ? m_proceduralLightIntensity : 0.0f, static_cast<float>(m_environmentDescriptorIndex));
    constants.environmentOptions = DirectX::XMFLOAT4(m_environmentMapEnabled ? 1.0f : 0.0f, m_environmentIntensity, m_environmentRotation, 0.0f);
    constants.denoiseOptions = DirectX::XMFLOAT4(m_denoiserEnabled ? 1.0f : 0.0f, static_cast<float>(m_denoiserSpatialIterations), m_denoiserNormalSigma, m_denoiserDepthSigma);
    constants.denoiseOptions2 = DirectX::XMFLOAT4(m_denoiserLuminanceSigma, m_denoiserAlbedoSigma, m_denoiserStrength, 0.0f);
    constants.restirEnhancedOptions0 = DirectX::XMFLOAT4(m_enhancedPairedSpatial ? 1.0f : 0.0f, m_enhancedDuplicationDecorrelate ? 1.0f : 0.0f, m_enhancedColorReuse ? 1.0f : 0.0f, m_enhancedRussianRoulette ? 1.0f : 0.0f);
    constants.restirEnhancedOptions1 = DirectX::XMFLOAT4(m_enhancedReplayCompaction ? 1.0f : 0.0f, m_enhancedFootprintC, m_enhancedRoughnessAlphaMin, static_cast<float>(m_enhancedPrimaryRisCandidates));
    constants.restirEnhancedOptions2 = DirectX::XMFLOAT4(m_enhancedForcedNeeReconnection ? 1.0f : 0.0f, m_enhancedTemporalCapDefault, m_enhancedTemporalCapMin, m_enhancedDuplicationAlpha);
    constants.previousViewProjection = m_previousViewProjection;

    void* mapped = nullptr;
    vkMapMemory(m_device, m_sceneUniformBuffer.memory, 0, sizeof(constants), 0, &mapped);
    memcpy(mapped, &constants, sizeof(constants));
    vkUnmapMemory(m_device, m_sceneUniformBuffer.memory);
    DirectX::XMStoreFloat4x4(&m_previousViewProjection, viewProjection);

    if (!m_freezeAccumulation)
    {
        m_accumulatedFrames = (std::min)(m_accumulatedFrames + 1u, static_cast<uint32_t>((std::max)(m_maxAccumulatedFrames, 1)));
        ++m_frameCounter;
    }
}

bool BistroExteriorPathtracingVulkan::IsDeviceSuitable(VkPhysicalDevice device) const
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

    VkPhysicalDeviceAccelerationStructureFeaturesKHR asFeatures{};
    asFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
    VkPhysicalDeviceRayTracingPipelineFeaturesKHR rtFeatures{};
    rtFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
    rtFeatures.pNext = &asFeatures;
    VkPhysicalDeviceBufferDeviceAddressFeatures bdaFeatures{};
    bdaFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
    bdaFeatures.pNext = &rtFeatures;
    VkPhysicalDeviceDescriptorIndexingFeatures indexingFeatures{};
    indexingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;
    indexingFeatures.pNext = &bdaFeatures;
    VkPhysicalDeviceFeatures2 features{};
    features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features.pNext = &indexingFeatures;
    vkGetPhysicalDeviceFeatures2(device, &features);

    if (asFeatures.accelerationStructure != VK_TRUE ||
        rtFeatures.rayTracingPipeline != VK_TRUE ||
        bdaFeatures.bufferDeviceAddress != VK_TRUE ||
        indexingFeatures.runtimeDescriptorArray != VK_TRUE ||
        indexingFeatures.descriptorBindingPartiallyBound != VK_TRUE ||
        indexingFeatures.descriptorBindingVariableDescriptorCount != VK_TRUE ||
        indexingFeatures.shaderSampledImageArrayNonUniformIndexing != VK_TRUE)
    {
        return false;
    }

    SwapChainSupport support = QuerySwapChainSupport(device);
    return !support.formats.empty() && !support.presentModes.empty();
}

BistroExteriorPathtracingVulkan::QueueFamilyIndices BistroExteriorPathtracingVulkan::FindQueueFamilies(VkPhysicalDevice device) const
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

BistroExteriorPathtracingVulkan::SwapChainSupport BistroExteriorPathtracingVulkan::QuerySwapChainSupport(VkPhysicalDevice device) const
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

VkSurfaceFormatKHR BistroExteriorPathtracingVulkan::ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) const
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

VkPresentModeKHR BistroExteriorPathtracingVulkan::ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& presentModes) const
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

VkExtent2D BistroExteriorPathtracingVulkan::ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) const
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

uint32_t BistroExteriorPathtracingVulkan::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const
{
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memoryProperties);
    for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i)
    {
        if ((typeFilter & (1u << i)) && (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties)
        {
            return i;
        }
    }
    throw std::runtime_error("Failed to find suitable Vulkan memory type.");
}

void BistroExteriorPathtracingVulkan::CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, GpuBuffer& buffer)
{
    buffer.size = size;
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ThrowIfFailed(vkCreateBuffer(m_device, &bufferInfo, nullptr, &buffer.buffer), "Failed to create Vulkan buffer.");

    VkMemoryRequirements memoryRequirements{};
    vkGetBufferMemoryRequirements(m_device, buffer.buffer, &memoryRequirements);

    VkMemoryAllocateFlagsInfo allocateFlags{};
    allocateFlags.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    allocateFlags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

    VkMemoryAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.pNext = (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) ? &allocateFlags : nullptr;
    allocateInfo.allocationSize = memoryRequirements.size;
    allocateInfo.memoryTypeIndex = FindMemoryType(memoryRequirements.memoryTypeBits, properties);
    ThrowIfFailed(vkAllocateMemory(m_device, &allocateInfo, nullptr, &buffer.memory), "Failed to allocate Vulkan buffer memory.");
    ThrowIfFailed(vkBindBufferMemory(m_device, buffer.buffer, buffer.memory, 0), "Failed to bind Vulkan buffer memory.");

    if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
    {
        buffer.address = GetBufferAddress(buffer.buffer);
    }
}

void BistroExteriorPathtracingVulkan::DestroyBuffer(GpuBuffer& buffer)
{
    if (buffer.buffer) vkDestroyBuffer(m_device, buffer.buffer, nullptr);
    if (buffer.memory) vkFreeMemory(m_device, buffer.memory, nullptr);
    buffer = {};
}

void BistroExteriorPathtracingVulkan::UploadBuffer(const void* data, VkDeviceSize size, VkBufferUsageFlags usage, GpuBuffer& destination)
{
    GpuBuffer staging;
    CreateBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging);
    void* mapped = nullptr;
    vkMapMemory(m_device, staging.memory, 0, size, 0, &mapped);
    memcpy(mapped, data, static_cast<size_t>(size));
    vkUnmapMemory(m_device, staging.memory);

    CreateBuffer(size, usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, destination);
    CopyBuffer(staging.buffer, destination.buffer, size);
    DestroyBuffer(staging);
}

VkDeviceAddress BistroExteriorPathtracingVulkan::GetBufferAddress(VkBuffer buffer) const
{
    VkBufferDeviceAddressInfo addressInfo{};
    addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    addressInfo.buffer = buffer;
    return m_vkGetBufferDeviceAddressKHR(m_device, &addressInfo);
}

void BistroExteriorPathtracingVulkan::CreateImage(uint32_t width, uint32_t height, uint32_t mipLevels, VkFormat format, VkImageUsageFlags usage, VkImage& image, VkDeviceMemory& memory)
{
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = { width, height, 1 };
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
    ThrowIfFailed(vkBindImageMemory(m_device, image, memory, 0), "Failed to bind Vulkan image memory.");
}

VkImageView BistroExteriorPathtracingVulkan::CreateImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, uint32_t mipLevels) const
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

VkCommandBuffer BistroExteriorPathtracingVulkan::BeginSingleTimeCommands() const
{
    VkCommandBufferAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocateInfo.commandPool = m_commandPool;
    allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocateInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    ThrowIfFailed(vkAllocateCommandBuffers(m_device, &allocateInfo, &commandBuffer), "Failed to allocate one-time command buffer.");
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    ThrowIfFailed(vkBeginCommandBuffer(commandBuffer, &beginInfo), "Failed to begin one-time command buffer.");
    return commandBuffer;
}

void BistroExteriorPathtracingVulkan::EndSingleTimeCommands(VkCommandBuffer commandBuffer) const
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

void BistroExteriorPathtracingVulkan::CopyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size) const
{
    VkCommandBuffer commandBuffer = BeginSingleTimeCommands();
    VkBufferCopy copyRegion{};
    copyRegion.size = size;
    vkCmdCopyBuffer(commandBuffer, src, dst, 1, &copyRegion);
    EndSingleTimeCommands(commandBuffer);
}

void BistroExteriorPathtracingVulkan::CopyBufferToImage(VkBuffer buffer, VkImage image, const Bistro::TextureData& texture) const
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

void BistroExteriorPathtracingVulkan::TransitionImageLayout(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout, VkImageAspectFlags aspectMask, VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage) const
{
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = aspectMask;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
    {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_GENERAL)
    {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
    }
    vkCmdPipelineBarrier(commandBuffer, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void BistroExteriorPathtracingVulkan::TransitionImageLayoutImmediate(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout, VkImageAspectFlags aspectMask) const
{
    VkCommandBuffer commandBuffer = BeginSingleTimeCommands();
    VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    if (newLayout == VK_IMAGE_LAYOUT_GENERAL)
    {
        dstStage = VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;
    }
    else if (newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dstStage = VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;
    }
    TransitionImageLayout(commandBuffer, image, oldLayout, newLayout, aspectMask, srcStage, dstStage);
    EndSingleTimeCommands(commandBuffer);
}

std::vector<char> BistroExteriorPathtracingVulkan::ReadFile(const std::wstring& path) const
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

VkShaderModule BistroExteriorPathtracingVulkan::CreateShaderModule(const std::vector<char>& code) const
{
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule shaderModule = VK_NULL_HANDLE;
    ThrowIfFailed(vkCreateShaderModule(m_device, &createInfo, nullptr, &shaderModule), "Failed to create shader module.");
    return shaderModule;
}

std::wstring BistroExteriorPathtracingVulkan::GetExecutableDirectory() const
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

std::wstring BistroExteriorPathtracingVulkan::ShaderFileName() const
{
    if (m_mode == BistroPathtracingMode::ReSTIR)
    {
        return L"BistroExteriorPathtracingReSTIR.spv";
    }
    if (m_mode == BistroPathtracingMode::ReSTIRDI)
    {
        return L"BistroExteriorPathtracingReSTIRDI.spv";
    }
    if (m_mode == BistroPathtracingMode::ReSTIRPTEnhanced)
    {
        return L"BistroExteriorPathtracingReSTIRPTEnhanced.spv";
    }
    return L"BistroExteriorPathtracing.spv";
}

std::wstring BistroExteriorPathtracingVulkan::RestirReuseShaderFileName() const
{
    if (m_mode == BistroPathtracingMode::ReSTIRPTEnhanced)
    {
        return L"BistroExteriorPathtracingReSTIRPTEnhancedResolve.spv";
    }
    return L"BistroExteriorPathtracingReSTIRResolve.spv";
}

std::wstring BistroExteriorPathtracingVulkan::DenoiseShaderFileName() const
{
    return L"BistroExteriorPathtracingDenoise.spv";
}

uint32_t BistroExteriorPathtracingVulkan::MaxTraceRecursionDepth() const
{
    return 1u;
}

VkDeviceSize BistroExteriorPathtracingVulkan::Align(VkDeviceSize value, VkDeviceSize alignment) const
{
    return (value + alignment - 1) & ~(alignment - 1);
}

void BistroExteriorPathtracingVulkan::ThrowIfFailed(VkResult result, const char* message) const
{
    if (result != VK_SUCCESS)
    {
        throw std::runtime_error(message);
    }
}
