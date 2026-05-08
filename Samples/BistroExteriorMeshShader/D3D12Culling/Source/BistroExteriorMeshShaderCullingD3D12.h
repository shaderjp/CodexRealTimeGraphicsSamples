#pragma once

#include "DXSample.h"
#include "..\..\Common\BistroCamera.h"
#include "..\..\Common\BistroScene.h"

#include <array>
#include <chrono>
#include <map>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

class BistroExteriorMeshShaderCullingD3D12 : public DXSample
{
public:
    BistroExteriorMeshShaderCullingD3D12(UINT width, UINT height, std::wstring name);

    virtual void OnInit();
    virtual void OnUpdate();
    virtual void OnRender();
    virtual void OnDestroy();
    virtual void OnKeyDown(UINT8 key);
    void OnWindowMessage(UINT message, WPARAM wParam, LPARAM lParam);

private:
    static const UINT FrameCount = 2;

    struct SceneConstantBuffer
    {
        XMFLOAT4X4 viewProjection;
        XMFLOAT4 cameraPosition;
        XMFLOAT4 lightDirection;
        XMFLOAT4 lightColor;
        XMFLOAT4 debugOptions;
        XMFLOAT4 frustumPlanes[6];
    };

    struct MaterialConstantBuffer
    {
        XMFLOAT4 baseColorFactor;
        XMFLOAT4 options;
    };

    struct MeshletDrawConstants
    {
        UINT meshletBase = 0;
        UINT meshletCount = 0;
        UINT debugMode = 0;
        UINT padding = 0;
    };

    struct GpuTexture
    {
        ComPtr<ID3D12Resource> resource;
        ComPtr<ID3D12Resource> upload;
        std::wstring path;
        uint32_t width = 1;
        uint32_t height = 1;
        uint32_t mipLevels = 1;
        bool fallback = false;
    };

    CD3DX12_VIEWPORT m_viewport;
    CD3DX12_RECT m_scissorRect;
    ComPtr<IDXGISwapChain3> m_swapChain;
    ComPtr<ID3D12Device2> m_device;
    ComPtr<ID3D12Resource> m_renderTargets[FrameCount];
    ComPtr<ID3D12CommandAllocator> m_commandAllocator;
    ComPtr<ID3D12CommandQueue> m_commandQueue;
    ComPtr<ID3D12RootSignature> m_rootSignature;
    ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
    ComPtr<ID3D12DescriptorHeap> m_srvHeap;
    ComPtr<ID3D12DescriptorHeap> m_imguiDescriptorHeap;
    ComPtr<ID3D12PipelineState> m_pipelineState;
    ComPtr<ID3D12GraphicsCommandList6> m_commandList;
    ComPtr<ID3D12Resource> m_depthStencil;
    UINT m_rtvDescriptorSize = 0;
    UINT m_srvDescriptorSize = 0;
    UINT m_imguiDescriptorSize = 0;
    UINT m_imguiDescriptorCursor = 0;

    Bistro::Scene m_scene;
    std::vector<GpuTexture> m_textures;
    std::vector<UINT> m_materialTextureDescriptors;
    std::vector<std::array<UINT, Bistro::TextureSlotCount>> m_materialTextureIndices;
    ComPtr<ID3D12Resource> m_vertexBuffer;
    ComPtr<ID3D12Resource> m_meshletBuffer;
    ComPtr<ID3D12Resource> m_meshletVertexBuffer;
    ComPtr<ID3D12Resource> m_meshletTriangleBuffer;
    ComPtr<ID3D12Resource> m_meshletBoundsBuffer;
    ComPtr<ID3D12Resource> m_sceneConstantBuffer;
    ComPtr<ID3D12Resource> m_materialConstantBuffer;
    UINT8* m_mappedSceneConstants = nullptr;
    UINT8* m_mappedMaterialConstants = nullptr;
    UINT m_materialConstantStride = 0;
    SceneConstantBuffer m_sceneConstants{};

    Bistro::FpsCamera m_camera;
    XMFLOAT3 m_defaultCameraPosition = XMFLOAT3(-16.32f, 4.66f, -10.41f);
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
    bool m_vsyncEnabled = false;
    bool m_tearingSupported = false;
    uint32_t m_lastVisibleMeshlets = 0;

    UINT m_frameIndex = 0;
    HANDLE m_fenceEvent = nullptr;
    ComPtr<ID3D12Fence> m_fence;
    UINT64 m_fenceValue = 0;

    void LoadPipeline();
    void LoadAssets();
    void CreateRootSignature();
    void CreatePipelineState();
    void CreateBuffers();
    void CreateTextures();
    void PopulateCommandList();
    void WaitForPreviousFrame();
    void UpdateConstantBuffer(float deltaSeconds);
    void InitializeImGui();
    void ShutdownImGui();
    void BuildUI();
    void BuildRendererStatsUI();
    void ResetLight();
    void ResetCameraView();
    void ResetCameraSpeeds();
    UINT CreateTextureResource(const std::wstring& path, bool srgb, const uint8_t fallback[4], std::map<std::wstring, UINT>& cache);
    static void AllocateImGuiDescriptor(struct ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* outCpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE* outGpuHandle);
    static void FreeImGuiDescriptor(struct ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle);
};
