#pragma once

#include "DXSample.h"
#include "..\..\Common\BistroCamera.h"
#include "..\..\Common\BistroScene.h"

#include <chrono>
#include <map>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

class BistroExteriorD3D12 : public DXSample
{
public:
    BistroExteriorD3D12(UINT width, UINT height, std::wstring name);

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
    };

    struct MaterialConstantBuffer
    {
        XMFLOAT4 baseColorFactor;
        XMFLOAT4 options;
    };

    struct GpuTexture
    {
        ComPtr<ID3D12Resource> resource;
        ComPtr<ID3D12Resource> upload;
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
    ComPtr<ID3D12GraphicsCommandList> m_commandList;
    ComPtr<ID3D12Resource> m_depthStencil;
    UINT m_rtvDescriptorSize = 0;
    UINT m_srvDescriptorSize = 0;
    UINT m_imguiDescriptorSize = 0;
    UINT m_imguiDescriptorCursor = 0;

    Bistro::Scene m_scene;
    std::vector<GpuTexture> m_textures;
    std::vector<UINT> m_materialTextureDescriptors;
    ComPtr<ID3D12Resource> m_vertexBuffer;
    ComPtr<ID3D12Resource> m_indexBuffer;
    ComPtr<ID3D12Resource> m_sceneConstantBuffer;
    ComPtr<ID3D12Resource> m_materialConstantBuffer;
    D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView{};
    D3D12_INDEX_BUFFER_VIEW m_indexBufferView{};
    UINT8* m_mappedSceneConstants = nullptr;
    UINT8* m_mappedMaterialConstants = nullptr;
    UINT m_materialConstantStride = 0;
    SceneConstantBuffer m_sceneConstants{};

    Bistro::FpsCamera m_camera;
    XMFLOAT3 m_defaultCameraPosition = XMFLOAT3(0.0f, 3.0f, -10.0f);
    float m_defaultCameraYaw = 0.0f;
    float m_defaultCameraPitch = 0.0f;
    std::chrono::steady_clock::time_point m_lastUpdate;
    float m_lightDirection[3] = { -0.35f, -0.8f, 0.45f };
    float m_lightColor[3] = { 1.0f, 0.96f, 0.88f };
    float m_lightIntensity = 4.0f;
    float m_baseMoveSpeed = 5.0f;
    float m_fastMoveSpeed = 18.0f;
    int m_debugViewMode = 0;
    bool m_debugNormalMapYFlip = true;

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
    void ResetLight();
    void ResetCameraView();
    void ResetCameraSpeeds();
    UINT CreateTextureResource(const std::wstring& path, bool srgb, const uint8_t fallback[4], std::map<std::wstring, UINT>& cache);
    static void AllocateImGuiDescriptor(struct ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* outCpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE* outGpuHandle);
    static void FreeImGuiDescriptor(struct ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle);
};
