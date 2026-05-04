#pragma once

#include "DXSample.h"
#include "..\..\Shared\SkinningData.h"

using namespace DirectX;
using Microsoft::WRL::ComPtr;

class SkinningD3D12 : public DXSample
{
public:
    struct Vertex
    {
        XMFLOAT3 position;
        XMFLOAT3 normal;
        XMFLOAT2 texcoord;
        XMUINT4 joints;
        XMFLOAT4 weights;
    };

    SkinningD3D12(UINT width, UINT height, std::wstring name);

    virtual void OnInit();
    virtual void OnUpdate();
    virtual void OnRender();
    virtual void OnDestroy();

private:
    static const UINT FrameCount = 2;

    struct SceneConstantBuffer
    {
        XMFLOAT4X4 model;
        XMFLOAT4X4 modelViewProjection;
        XMFLOAT4 lightDirection;
        XMFLOAT4 lightColor;
    };

    struct JointConstantBuffer
    {
        XMFLOAT4X4 joints[64];
    };

    struct ImageData
    {
        UINT width = 0;
        UINT height = 0;
        std::vector<uint8_t> pixels;
    };

    struct NodePose
    {
        XMFLOAT3 translation;
        XMFLOAT4 rotation;
        XMFLOAT3 scale;
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
    ComPtr<ID3D12PipelineState> m_pipelineState;
    ComPtr<ID3D12GraphicsCommandList> m_commandList;
    ComPtr<ID3D12Resource> m_depthStencil;
    UINT m_rtvDescriptorSize;
    UINT m_cbvSrvDescriptorSize;

    std::vector<Vertex> m_vertices;
    std::vector<uint16_t> m_indices;
    std::array<XMFLOAT4X4, SkinningData::JointCount> m_inverseBindMatrices;
    std::array<NodePose, SkinningData::NodeCount> m_nodePoses;
    std::array<XMFLOAT4X4, SkinningData::NodeCount> m_globalNodeMatrices;
    ComPtr<ID3D12Resource> m_vertexBuffer;
    ComPtr<ID3D12Resource> m_indexBuffer;
    ComPtr<ID3D12Resource> m_sceneConstantBuffer;
    ComPtr<ID3D12Resource> m_jointConstantBuffer;
    ComPtr<ID3D12Resource> m_texture;
    ComPtr<ID3D12Resource> m_textureUploadHeap;
    D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;
    D3D12_INDEX_BUFFER_VIEW m_indexBufferView;
    UINT8* m_mappedSceneConstantBuffer;
    UINT8* m_mappedJointConstantBuffer;
    SceneConstantBuffer m_sceneConstantBufferData;
    JointConstantBuffer m_jointConstantBufferData;
    float m_animationTime;

    UINT m_frameIndex;
    HANDLE m_fenceEvent;
    ComPtr<ID3D12Fence> m_fence;
    UINT64 m_fenceValue;

    void LoadPipeline();
    void LoadAssets();
    void LoadModel();
    ImageData LoadTexture(const std::wstring& path) const;
    void CreateTexture(const ImageData& image);
    void PopulateCommandList();
    void WaitForPreviousFrame();
    void UpdateAnimation();
    void UpdateConstantBuffers();
    XMMATRIX GetLocalMatrix(UINT nodeIndex) const;
};
