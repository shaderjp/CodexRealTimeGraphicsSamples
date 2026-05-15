#pragma once

#include "DXSample.h"
#include "..\..\Common\BistroCamera.h"
#include "..\..\Common\BistroScene.h"

#include <array>
#include <chrono>
#include <map>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

enum class BistroPathtracingMode
{
    Pathtracing,
    ReSTIR,
    ReSTIRDI
};

class BistroInteriorPathtracingD3D12 : public DXSample
{
public:
    BistroInteriorPathtracingD3D12(UINT width, UINT height, std::wstring name, BistroPathtracingMode mode);

    void OnInit() override;
    void OnUpdate() override;
    void OnRender() override;
    void OnDestroy() override;
    void OnKeyDown(UINT8 key) override;
    void OnWindowMessage(UINT message, WPARAM wParam, LPARAM lParam);

private:
    static const UINT FrameCount = 2;
    static const UINT TextureSlotCount = Bistro::TextureSlotCount;

    enum DescriptorSlot : UINT
    {
        DescriptorOutputUav = 0,
        DescriptorAccumulationUav,
        DescriptorRestirCurrentUav,
        DescriptorRestirHistoryUav,
        DescriptorRestirSpatialUav,
        DescriptorDenoiseAov0Uav,
        DescriptorDenoiseAov1Uav,
        DescriptorVertexBuffer,
        DescriptorIndexBuffer,
        DescriptorGeometryBuffer,
        DescriptorMaterialBuffer,
        DescriptorLightBuffer,
        DescriptorTextureBase
    };

    enum RootParameter : UINT
    {
        RootOutputTable = 0,
        RootAccelerationStructure,
        RootSceneConstants,
        RootSceneBuffers,
        RootTextureTable,
        RootParameterCount
    };

    struct SceneConstantBuffer
    {
        XMFLOAT4X4 inverseViewProjection;
        XMFLOAT4 cameraPosition;
        XMFLOAT4 lightDirection;
        XMFLOAT4 lightColor;
        XMFLOAT4 debugOptions;
        XMFLOAT4 skyColor;
        XMFLOAT4 skyHorizonColor;
        XMFLOAT4 skyZenithColor;
        XMFLOAT4 skyGroundColor;
        XMFLOAT4 skyOptions;
        XMFLOAT4 rayOptions;
        XMFLOAT4 frameOptions;
        XMFLOAT4 giOptions;
        XMFLOAT4 pathOptions;
        XMFLOAT4 restirOptions;
        XMFLOAT4 lightOptions;
        XMFLOAT4 environmentOptions;
        XMFLOAT4 denoiseOptions;
        XMFLOAT4 denoiseOptions2;
    };

    struct GpuTexture
    {
        ComPtr<ID3D12Resource> resource;
        ComPtr<ID3D12Resource> upload;
        std::wstring path;
        DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
        uint32_t width = 1;
        uint32_t height = 1;
        uint32_t mipLevels = 1;
        bool fallback = false;
    };

    struct AccelerationStructureBuffers
    {
        ComPtr<ID3D12Resource> scratch;
        ComPtr<ID3D12Resource> result;
        ComPtr<ID3D12Resource> instanceDesc;
    };

    struct ShaderTableInfo
    {
        ComPtr<ID3D12Resource> resource;
        UINT recordSize = 0;
        UINT recordCount = 0;
    };

    CD3DX12_VIEWPORT m_viewport;
    CD3DX12_RECT m_scissorRect;
    ComPtr<IDXGISwapChain3> m_swapChain;
    ComPtr<ID3D12Device5> m_device;
    ComPtr<ID3D12GraphicsCommandList4> m_commandList;
    ComPtr<ID3D12CommandAllocator> m_commandAllocator;
    ComPtr<ID3D12CommandQueue> m_commandQueue;
    ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    ComPtr<ID3D12DescriptorHeap> m_descriptorHeap;
    ComPtr<ID3D12DescriptorHeap> m_imguiDescriptorHeap;
    ComPtr<ID3D12RootSignature> m_globalRootSignature;
    ComPtr<ID3D12StateObject> m_stateObject;
    ComPtr<ID3D12StateObjectProperties> m_stateObjectProperties;
    ComPtr<ID3D12PipelineState> m_restirReusePipeline;
    ComPtr<ID3D12PipelineState> m_denoisePipeline;
    ComPtr<ID3D12Resource> m_renderTargets[FrameCount];
    ComPtr<ID3D12Resource> m_PathtracingOutput;
    ComPtr<ID3D12Resource> m_accumulationOutput;
    ComPtr<ID3D12Resource> m_denoiseAov0;
    ComPtr<ID3D12Resource> m_denoiseAov1;
    ComPtr<ID3D12Resource> m_restirReservoirCurrent;
    ComPtr<ID3D12Resource> m_restirReservoirHistory;
    ComPtr<ID3D12Resource> m_restirReservoirSpatial;
    ComPtr<ID3D12Resource> m_sceneConstantBuffer;
    UINT8* m_mappedSceneConstants = nullptr;
    UINT m_frameIndex = 0;
    UINT m_rtvDescriptorSize = 0;
    UINT m_descriptorSize = 0;
    UINT m_imguiDescriptorSize = 0;
    UINT m_imguiDescriptorCursor = 0;
    UINT m_descriptorCount = 0;
    UINT m_restirReservoirElementCount = 1;
    UINT64 m_restirReservoirBufferSize = 0;

    BistroPathtracingMode m_mode = BistroPathtracingMode::Pathtracing;
    Bistro::Scene m_scene;
    std::vector<Bistro::RtGeometryRecord> m_geometryRecords;
    std::vector<Bistro::RtMaterial> m_rtMaterials;
    std::vector<GpuTexture> m_textures;
    std::vector<std::array<UINT, Bistro::TextureSlotCount>> m_materialTextureIndices;
    ComPtr<ID3D12Resource> m_vertexBuffer;
    ComPtr<ID3D12Resource> m_indexBuffer;
    ComPtr<ID3D12Resource> m_geometryBuffer;
    ComPtr<ID3D12Resource> m_materialBuffer;
    ComPtr<ID3D12Resource> m_lightBuffer;
    std::vector<ComPtr<ID3D12Resource>> m_uploadBuffers;
    AccelerationStructureBuffers m_bottomLevelAs;
    AccelerationStructureBuffers m_topLevelAs;
    ShaderTableInfo m_rayGenTable;
    ShaderTableInfo m_missTable;
    ShaderTableInfo m_hitGroupTable;

    Bistro::FpsCamera m_camera;
    XMFLOAT3 m_defaultCameraPosition = XMFLOAT3(0.0f, 2.0f, -5.0f);
    float m_defaultCameraYaw = 0.0f;
    float m_defaultCameraPitch = DirectX::XMConvertToRadians(-1.5f);
    std::chrono::steady_clock::time_point m_lastUpdate;
    float m_lightDirection[3] = { -0.25f, -0.85f, 0.35f };
    float m_lightColor[3] = { 1.0f, 0.96f, 0.88f };
    float m_lightIntensity = 0.75f;
    float m_skyColor[3] = { 0.015f, 0.08f, 0.16f };
    float m_skyHorizonColor[3] = { 0.42f, 0.63f, 0.86f };
    float m_skyZenithColor[3] = { 0.05f, 0.20f, 0.52f };
    float m_skyGroundColor[3] = { 0.025f, 0.035f, 0.045f };
    float m_skyIntensity = 0.25f;
    float m_sunIntensity = 0.75f;
    float m_sunAngularRadius = 0.012f;
    float m_skyGroundBlend = 0.35f;
    float m_emissiveLightIntensity = 8.0f;
    float m_proceduralLightIntensity = 18.0f;
    float m_environmentIntensity = 0.5f;
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
    float m_baseMoveSpeed = 4.0f;
    float m_fastMoveSpeed = 14.0f;
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
    bool m_denoiserEnabled = true;
    int m_denoiserSpatialIterations = 2;
    float m_denoiserNormalSigma = 0.25f;
    float m_denoiserDepthSigma = 0.02f;
    float m_denoiserLuminanceSigma = 1.5f;
    float m_denoiserAlbedoSigma = 0.35f;
    float m_denoiserStrength = 0.85f;
    bool m_skyEnabled = true;
    bool m_vsyncEnabled = false;
    bool m_tearingSupported = false;
    D3D12_RAYTRACING_TIER m_raytracingTier = D3D12_RAYTRACING_TIER_NOT_SUPPORTED;
    XMFLOAT4 m_lastCameraAndYaw = XMFLOAT4(0, 0, 0, 0);
    XMFLOAT4 m_lastLighting = XMFLOAT4(0, 0, 0, 0);
    XMFLOAT4 m_lastGiOptions = XMFLOAT4(0, 0, 0, 0);
    XMFLOAT4 m_lastPathOptions = XMFLOAT4(0, 0, 0, 0);
    XMFLOAT4 m_lastRestirOptions = XMFLOAT4(0, 0, 0, 0);
    XMFLOAT4 m_lastLightSystemOptions = XMFLOAT4(0, 0, 0, 0);
    std::wstring m_environmentTexturePath;
    UINT m_environmentDescriptorIndex = 0;
    uint32_t m_activeLightCount = 0;
    uint32_t m_emissiveTriangleLightCount = 0;
    uint32_t m_proceduralAreaLightCount = 0;
    std::vector<Bistro::RtLight> m_lights;

    HANDLE m_fenceEvent = nullptr;
    ComPtr<ID3D12Fence> m_fence;
    UINT64 m_fenceValue = 0;

    void LoadPipeline();
    void LoadAssets();
    void CreateDescriptorHeap();
    void CreateOutputResources();
    void CreateGlobalRootSignature();
    void CreatePathtracingStateObject();
    void CreateSceneBuffers();
    void CreateTextures();
    void BuildAccelerationStructures();
    void CreateShaderTables();
    void CreateRestirReusePipeline();
    void CreateDenoisePipeline();
    void PopulateCommandList();
    void ResizeOutput(UINT width, UINT height);
    void DispatchRays();
    void RunRestirReusePass();
    void RunDenoisePass();
    void CopyOutputToBackBuffer();
    void WaitForPreviousFrame();
    void UpdateConstantBuffer(float deltaSeconds);
    void InitializeImGui();
    void ShutdownImGui();
    void BuildUI();
    void BuildRendererStatsUI();
    void ResetLight();
    void ResetCameraView();
    void ResetCameraSpeeds();
    void ResetAccumulation();
    bool HasAccumulationStateChanged();
    UINT CreateTextureResource(const std::wstring& path, bool srgb, const uint8_t fallback[4], std::map<std::wstring, UINT>& cache);
    ComPtr<ID3D12Resource> CreateDefaultBuffer(const void* data, UINT64 size, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES finalState, const wchar_t* name);
    ComPtr<ID3D12Resource> CreateUploadBuffer(const void* data, UINT64 size, const wchar_t* name);
    ComPtr<ID3D12Resource> CreateUavBuffer(UINT64 size, D3D12_RESOURCE_STATES initialState, const wchar_t* name);
    D3D12_CPU_DESCRIPTOR_HANDLE CpuDescriptor(UINT index) const;
    D3D12_GPU_DESCRIPTOR_HANDLE GpuDescriptor(UINT index) const;
    std::wstring ShaderFileName() const;
    std::wstring RestirReuseShaderFileName() const;
    std::wstring DenoiseShaderFileName() const;
    UINT MaxTraceRecursionDepth() const;
    static UINT Align(UINT value, UINT alignment);
    static void AllocateImGuiDescriptor(struct ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* outCpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE* outGpuHandle);
    static void FreeImGuiDescriptor(struct ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle);
};
