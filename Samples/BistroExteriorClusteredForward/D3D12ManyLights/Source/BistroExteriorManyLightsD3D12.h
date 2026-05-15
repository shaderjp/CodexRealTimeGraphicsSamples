#pragma once

#include "DXSample.h"
#include "..\..\Common\BistroCamera.h"
#include "..\..\Common\BistroScene.h"

#include <array>
#include <chrono>
#include <map>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

class BistroExteriorManyLightsD3D12 : public DXSample
{
public:
    BistroExteriorManyLightsD3D12(UINT width, UINT height, std::wstring name);

    virtual void OnInit();
    virtual void OnUpdate();
    virtual void OnRender();
    virtual void OnDestroy();
    virtual void OnKeyDown(UINT8 key);
    void OnWindowMessage(UINT message, WPARAM wParam, LPARAM lParam);

private:
    static const UINT FrameCount = 2;
    static constexpr UINT ClusterTileSize = 16;
    static constexpr UINT MaxLightsPerCluster = 128;
    static constexpr UINT MaxClusterZSlices = 48;
    static constexpr UINT DefaultActiveLightCount = 512;

    struct SceneConstantBuffer
    {
        XMFLOAT4X4 viewProjection;
        XMFLOAT4X4 view;
        XMFLOAT4X4 projection;
        XMFLOAT4X4 lightViewProjection;
        XMFLOAT4 cameraPosition;
        XMFLOAT4 lightDirection;
        XMFLOAT4 lightColor;
        XMFLOAT4 debugOptions;
        XMFLOAT4 localLightOptions;
        XMFLOAT4 clusterOptions;
        XMFLOAT4 clusterOptions2;
        XMFLOAT4 clusterOptions3;
        XMFLOAT4 shadowOptions;
    };

    struct MaterialConstantBuffer
    {
        XMFLOAT4 baseColorFactor;
        XMFLOAT4 emissiveFactor;
        XMFLOAT4 options;
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
    ComPtr<ID3D12Device> m_device;
    ComPtr<ID3D12Resource> m_renderTargets[FrameCount];
    ComPtr<ID3D12CommandAllocator> m_commandAllocator;
    ComPtr<ID3D12CommandQueue> m_commandQueue;
    ComPtr<ID3D12RootSignature> m_rootSignature;
    ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
    ComPtr<ID3D12DescriptorHeap> m_srvHeap;
    ComPtr<ID3D12DescriptorHeap> m_imguiDescriptorHeap;
    ComPtr<ID3D12PipelineState> m_pipelineState;
    ComPtr<ID3D12PipelineState> m_shadowPipelineState;
    ComPtr<ID3D12PipelineState> m_clusterResetPipelineState;
    ComPtr<ID3D12PipelineState> m_clusterBuildPipelineState;
    ComPtr<ID3D12GraphicsCommandList> m_commandList;
    ComPtr<ID3D12Resource> m_depthStencil;
    ComPtr<ID3D12Resource> m_shadowDepth;
    UINT m_rtvDescriptorSize = 0;
    UINT m_srvDescriptorSize = 0;
    UINT m_imguiDescriptorSize = 0;
    UINT m_imguiDescriptorCursor = 0;
    UINT m_shadowSrvDescriptorIndex = 0;

    Bistro::Scene m_scene;
    Bistro::LightBuildResult m_lightBuild;
    std::vector<GpuTexture> m_textures;
    std::vector<UINT> m_materialTextureDescriptors;
    std::vector<std::array<UINT, Bistro::TextureSlotCount>> m_materialTextureIndices;
    ComPtr<ID3D12Resource> m_vertexBuffer;
    ComPtr<ID3D12Resource> m_indexBuffer;
    ComPtr<ID3D12Resource> m_sceneConstantBuffer;
    ComPtr<ID3D12Resource> m_materialConstantBuffer;
    ComPtr<ID3D12Resource> m_lightBuffer;
    ComPtr<ID3D12Resource> m_clusterRecordsBuffer;
    ComPtr<ID3D12Resource> m_clusterLightIndicesBuffer;
    ComPtr<ID3D12Resource> m_clusterStatsBuffer;
    ComPtr<ID3D12Resource> m_clusterStatsReadback;
    D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView{};
    D3D12_INDEX_BUFFER_VIEW m_indexBufferView{};
    UINT8* m_mappedSceneConstants = nullptr;
    UINT8* m_mappedMaterialConstants = nullptr;
    UINT m_materialConstantStride = 0;
    SceneConstantBuffer m_sceneConstants{};
    D3D12_RESOURCE_STATES m_clusterRecordsState = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES m_clusterIndicesState = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES m_clusterStatsState = D3D12_RESOURCE_STATE_COMMON;

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
    bool m_localLightsEnabled = true;
    bool m_emissiveProxyLightsEnabled = true;
    bool m_proceduralProxyLightsEnabled = true;
    int m_activeLightCount = DefaultActiveLightCount;
    float m_localLightIntensityScale = 6.0f;
    float m_lightRadiusScale = 1.0f;
    bool m_shadowsEnabled = true;
    int m_shadowResolutionIndex = 1;
    UINT m_shadowResolution = 2048;
    float m_shadowDepthBias = 0.002f;
    float m_shadowNormalBias = 0.05f;
    int m_shadowPcfRadius = 1;
    float m_shadowOrthoSize = 50.0f;
    float m_shadowFocusDistance = 25.0f;
    float m_shadowDepthRange = 160.0f;
    D3D12_VIEWPORT m_shadowViewport{};
    D3D12_RECT m_shadowScissorRect{};
    int m_clusterZSliceCount = 24;
    UINT m_clusterTileCountX = 1;
    UINT m_clusterTileCountY = 1;
    UINT m_clusterAllocatedCount = 1;
    Bistro::ClusterStats m_clusterStats{};
    bool m_vsyncEnabled = false;
    bool m_tearingSupported = false;

    UINT m_frameIndex = 0;
    HANDLE m_fenceEvent = nullptr;
    ComPtr<ID3D12Fence> m_fence;
    UINT64 m_fenceValue = 0;

    void LoadPipeline();
    void LoadAssets();
    void CreateRootSignature();
    void CreatePipelineState();
    void CreateComputePipelineStates();
    void CreateBuffers();
    void CreateTextures();
    void CreateShadowResources();
    void CreateLightAndClusterBuffers();
    void PopulateCommandList();
    void ResizeOutput(UINT width, UINT height);
    void DrawScene();
    void DispatchClusterBuild();
    void ReadbackClusterStats();
    void WaitForPreviousFrame();
    void UpdateConstantBuffer(float deltaSeconds);
    void InitializeImGui();
    void ShutdownImGui();
    void BuildUI();
    void BuildRendererStatsUI();
    void ResetLight();
    void ResetCameraView();
    void ResetCameraSpeeds();
    void ResetShadowSettings();
    UINT CreateTextureResource(const std::wstring& path, bool srgb, const uint8_t fallback[4], std::map<std::wstring, UINT>& cache);
    static void AllocateImGuiDescriptor(struct ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* outCpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE* outGpuHandle);
    static void FreeImGuiDescriptor(struct ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle);
};
