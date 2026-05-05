#pragma once

#include "DXSample.h"

using namespace DirectX;
using Microsoft::WRL::ComPtr;

class ImGuiLightingD3D12 : public DXSample
{
public:
    static const UINT TextureCount = 4;

    struct Vertex
    {
        XMFLOAT3 position;
        XMFLOAT3 normal;
        XMFLOAT4 tangent;
        XMFLOAT2 texcoord;
    };

    ImGuiLightingD3D12(UINT width, UINT height, std::wstring name);

    virtual void OnInit();
    virtual void OnUpdate();
    virtual void OnRender();
    virtual void OnDestroy();

private:
    static const UINT FrameCount = 2;
    static const UINT ImGuiDescriptorCount = 16;

    struct SceneConstantBuffer
    {
        XMFLOAT4X4 model;
        XMFLOAT4X4 modelViewProjection;
        XMFLOAT4 cameraPosition;
        XMFLOAT4 lightDirection;
        XMFLOAT4 lightColor;
    };

    struct ImageData
    {
        UINT width = 0;
        UINT height = 0;
        std::vector<uint8_t> pixels;
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
    ComPtr<ID3D12DescriptorHeap> m_cbvSrvHeap;
    ComPtr<ID3D12DescriptorHeap> m_imguiDescriptorHeap;
    ComPtr<ID3D12PipelineState> m_pipelineState;
    ComPtr<ID3D12GraphicsCommandList> m_commandList;
    ComPtr<ID3D12Resource> m_depthStencil;
    UINT m_rtvDescriptorSize;
    UINT m_cbvSrvDescriptorSize;
    UINT m_imguiDescriptorSize;
    UINT m_imguiDescriptorCursor;

    std::vector<Vertex> m_vertices;
    std::vector<uint32_t> m_indices;
    ComPtr<ID3D12Resource> m_vertexBuffer;
    ComPtr<ID3D12Resource> m_indexBuffer;
    ComPtr<ID3D12Resource> m_constantBuffer;
    std::array<ComPtr<ID3D12Resource>, TextureCount> m_textures;
    std::vector<ComPtr<ID3D12Resource>> m_textureUploadHeaps;
    D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;
    D3D12_INDEX_BUFFER_VIEW m_indexBufferView;
    UINT8* m_mappedConstantBuffer;
    SceneConstantBuffer m_constantBufferData;
    float m_rotationRadians;
    XMFLOAT3 m_cameraPosition;
    float m_lightDirection[3];
    float m_lightColor[3];
    float m_lightIntensity;

    UINT m_frameIndex;
    HANDLE m_fenceEvent;
    ComPtr<ID3D12Fence> m_fence;
    UINT64 m_fenceValue;

    void LoadPipeline();
    void LoadAssets();
    void LoadModel();
    ImageData LoadTexture(const std::wstring& path) const;
    void CreateTexture(UINT index, const ImageData& image);
    void InitializeImGui();
    void ShutdownImGui();
    void BuildUI();
    void ResetLight();
    void PopulateCommandList();
    void WaitForPreviousFrame();
    void UpdateConstantBuffer();
    static void AllocateImGuiDescriptor(struct ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* outCpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE* outGpuHandle);
    static void FreeImGuiDescriptor(struct ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle);
};
