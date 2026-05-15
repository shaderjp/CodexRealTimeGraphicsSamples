#include "stdafx.h"
#include "BistroExteriorMeshShaderShadowD3D12.h"
#include "..\..\Common\BistroTexture.h"
#include "..\..\Common\BistroResolution.h"

#include "imgui.h"
#include "backends/imgui_impl_dx12.h"
#include "backends/imgui_impl_win32.h"

#include <d3dx12.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>

using namespace DirectX;

namespace
{
    constexpr UINT ImGuiDescriptorCount = 64;
    constexpr UINT RootParameterSceneConstants = 0;
    constexpr UINT RootParameterMaterialConstants = 1;
    constexpr UINT RootParameterVertices = 2;
    constexpr UINT RootParameterMeshlets = 3;
    constexpr UINT RootParameterMeshletVertices = 4;
    constexpr UINT RootParameterMeshletTriangles = 5;
    constexpr UINT RootParameterMeshletBounds = 6;
    constexpr UINT RootParameterMeshletDrawConstants = 7;
    constexpr UINT RootParameterTextureBase = 8;
    constexpr UINT RootParameterShadowTexture = RootParameterTextureBase + Bistro::TextureSlotCount;
    constexpr UINT MaxDispatchMeshThreadGroupsX = 65535;
    constexpr UINT MaxMeshletsPerDispatch = MaxDispatchMeshThreadGroupsX;

    XMFLOAT4 NormalizePlane(float x, float y, float z, float w)
    {
        const float length = std::sqrt(x * x + y * y + z * z);
        if (length <= 0.000001f)
        {
            return XMFLOAT4(x, y, z, w);
        }
        const float invLength = 1.0f / length;
        return XMFLOAT4(x * invLength, y * invLength, z * invLength, w * invLength);
    }

    bool IsMeshletVisible(const Bistro::MeshletBounds& bounds, const XMFLOAT4 frustumPlanes[6], const XMFLOAT3* cameraPosition)
    {
        for (uint32_t planeIndex = 0; planeIndex < 6; ++planeIndex)
        {
            const XMFLOAT4& plane = frustumPlanes[planeIndex];
            const float distance =
                plane.x * bounds.sphere.x +
                plane.y * bounds.sphere.y +
                plane.z * bounds.sphere.z +
                plane.w;
            if (distance < -bounds.sphere.w)
            {
                return false;
            }
        }

        if (cameraPosition)
        {
            const XMFLOAT3 centerToCamera(
                bounds.sphere.x - cameraPosition->x,
                bounds.sphere.y - cameraPosition->y,
                bounds.sphere.z - cameraPosition->z);
            const float length = std::sqrt(centerToCamera.x * centerToCamera.x + centerToCamera.y * centerToCamera.y + centerToCamera.z * centerToCamera.z);
            const float coneDistance =
                centerToCamera.x * bounds.coneAxis.x +
                centerToCamera.y * bounds.coneAxis.y +
                centerToCamera.z * bounds.coneAxis.z;
            return coneDistance < bounds.coneAxis.w * length + bounds.sphere.w;
        }

        return true;
    }
}

BistroExteriorMeshShaderShadowD3D12::BistroExteriorMeshShaderShadowD3D12(UINT width, UINT height, std::wstring name) :
    DXSample(width, height, name),
    m_viewport(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)),
    m_scissorRect(0, 0, static_cast<LONG>(width), static_cast<LONG>(height))
{
}

void BistroExteriorMeshShaderShadowD3D12::OnInit()
{
    LoadPipeline();
    LoadAssets();
    m_lastUpdate = std::chrono::steady_clock::now();
}

void BistroExteriorMeshShaderShadowD3D12::LoadPipeline()
{
    UINT dxgiFactoryFlags = 0;
#if defined(_DEBUG)
    {
        ComPtr<ID3D12Debug> debugController;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
        {
            debugController->EnableDebugLayer();
            dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
        }
    }
#endif

    ComPtr<IDXGIFactory4> factory;
    ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)));
    ComPtr<IDXGIFactory5> factory5;
    if (SUCCEEDED(factory.As(&factory5)))
    {
        BOOL allowTearing = FALSE;
        if (SUCCEEDED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof(allowTearing))))
        {
            m_tearingSupported = allowTearing == TRUE;
        }
    }

    if (m_useWarpDevice)
    {
        ComPtr<IDXGIAdapter> warpAdapter;
        ThrowIfFailed(factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)));
        ThrowIfFailed(D3D12CreateDevice(warpAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device)));
    }
    else
    {
        ComPtr<IDXGIAdapter1> hardwareAdapter;
        GetHardwareAdapter(factory.Get(), &hardwareAdapter, true);
        ThrowIfFailed(D3D12CreateDevice(hardwareAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device)));
    }

    D3D12_FEATURE_DATA_D3D12_OPTIONS7 options7{};
    ThrowIfFailed(m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &options7, sizeof(options7)));
    if (options7.MeshShaderTier == D3D12_MESH_SHADER_TIER_NOT_SUPPORTED)
    {
        throw std::runtime_error("This GPU/driver does not support Direct3D 12 Mesh Shader Tier 1. BistroExteriorMeshShaderShadowD3D12 has no vertex-shader fallback.");
    }

    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ThrowIfFailed(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)));

    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.BufferCount = FrameCount;
    swapChainDesc.Width = m_width;
    swapChainDesc.Height = m_height;
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.Flags = m_tearingSupported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

    ComPtr<IDXGISwapChain1> swapChain;
    ThrowIfFailed(factory->CreateSwapChainForHwnd(m_commandQueue.Get(), Win32Application::GetHwnd(), &swapChainDesc, nullptr, nullptr, &swapChain));
    ThrowIfFailed(factory->MakeWindowAssociation(Win32Application::GetHwnd(), DXGI_MWA_NO_ALT_ENTER));
    ThrowIfFailed(swapChain.As(&m_swapChain));
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = FrameCount;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    ThrowIfFailed(m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap)));
    m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    m_srvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());
    for (UINT n = 0; n < FrameCount; n++)
    {
        ThrowIfFailed(m_swapChain->GetBuffer(n, IID_PPV_ARGS(&m_renderTargets[n])));
        m_device->CreateRenderTargetView(m_renderTargets[n].Get(), nullptr, rtvHandle);
        rtvHandle.Offset(1, m_rtvDescriptorSize);
    }

    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.NumDescriptors = 2;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    ThrowIfFailed(m_device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_dsvHeap)));

    D3D12_DESCRIPTOR_HEAP_DESC imguiHeapDesc = {};
    imguiHeapDesc.NumDescriptors = ImGuiDescriptorCount;
    imguiHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    imguiHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(m_device->CreateDescriptorHeap(&imguiHeapDesc, IID_PPV_ARGS(&m_imguiDescriptorHeap)));
    m_imguiDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocator)));
}

void BistroExteriorMeshShaderShadowD3D12::LoadAssets()
{
    m_scene = Bistro::LoadScene(Bistro::FindAssetRoot());

    m_defaultCameraPosition = XMFLOAT3(-16.32f, 4.66f, -10.41f);
    m_defaultCameraYaw = XMConvertToRadians(18.1f);
    m_defaultCameraPitch = XMConvertToRadians(2.8f);
    ResetCameraView();

    CreateRootSignature();
    CreatePipelineState();

    const UINT materialCount = static_cast<UINT>(std::max<size_t>(1, m_scene.materials.size()));
    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    m_shadowSrvDescriptorIndex = materialCount * Bistro::TextureSlotCount;
    srvHeapDesc.NumDescriptors = materialCount * Bistro::TextureSlotCount + 1;
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(m_device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&m_srvHeap)));

    ThrowIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocator.Get(), m_pipelineState.Get(), IID_PPV_ARGS(&m_commandList)));
    CreateBuffers();
    CreateShadowResources();
    CreateTextures();
    ThrowIfFailed(m_commandList->Close());

    ID3D12CommandList* commandLists[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(_countof(commandLists), commandLists);

    ThrowIfFailed(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
    m_fenceValue = 1;
    m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (m_fenceEvent == nullptr)
    {
        ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
    }
    WaitForPreviousFrame();
    for (GpuTexture& texture : m_textures)
    {
        texture.upload.Reset();
    }
    InitializeImGui();
}

void BistroExteriorMeshShaderShadowD3D12::CreateRootSignature()
{
    CD3DX12_DESCRIPTOR_RANGE srvRanges[Bistro::TextureSlotCount];
    for (UINT slot = 0; slot < Bistro::TextureSlotCount; ++slot)
    {
        srvRanges[slot].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, slot);
    }
    CD3DX12_DESCRIPTOR_RANGE shadowRange;
    shadowRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 4);

    CD3DX12_ROOT_PARAMETER rootParameters[RootParameterShadowTexture + 1];
    rootParameters[RootParameterSceneConstants].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);
    rootParameters[RootParameterMaterialConstants].InitAsConstantBufferView(1, 0, D3D12_SHADER_VISIBILITY_PIXEL);
    rootParameters[RootParameterVertices].InitAsShaderResourceView(5, 0, D3D12_SHADER_VISIBILITY_ALL);
    rootParameters[RootParameterMeshlets].InitAsShaderResourceView(6, 0, D3D12_SHADER_VISIBILITY_ALL);
    rootParameters[RootParameterMeshletVertices].InitAsShaderResourceView(7, 0, D3D12_SHADER_VISIBILITY_ALL);
    rootParameters[RootParameterMeshletTriangles].InitAsShaderResourceView(8, 0, D3D12_SHADER_VISIBILITY_ALL);
    rootParameters[RootParameterMeshletBounds].InitAsShaderResourceView(9, 0, D3D12_SHADER_VISIBILITY_ALL);
    rootParameters[RootParameterMeshletDrawConstants].InitAsConstants(4, 2, 0, D3D12_SHADER_VISIBILITY_ALL);
    for (UINT slot = 0; slot < Bistro::TextureSlotCount; ++slot)
    {
        rootParameters[RootParameterTextureBase + slot].InitAsDescriptorTable(1, &srvRanges[slot], D3D12_SHADER_VISIBILITY_PIXEL);
    }
    rootParameters[RootParameterShadowTexture].InitAsDescriptorTable(1, &shadowRange, D3D12_SHADER_VISIBILITY_PIXEL);

    D3D12_STATIC_SAMPLER_DESC samplers[2] = {};
    samplers[0].Filter = D3D12_FILTER_ANISOTROPIC;
    samplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplers[0].MaxAnisotropy = 8;
    samplers[0].ShaderRegister = 0;
    samplers[0].RegisterSpace = 0;
    samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    samplers[0].MinLOD = 0.0f;
    samplers[0].MaxLOD = D3D12_FLOAT32_MAX;

    samplers[1].Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    samplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    samplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    samplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    samplers[1].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    samplers[1].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    samplers[1].ShaderRegister = 1;
    samplers[1].RegisterSpace = 0;
    samplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    samplers[1].MinLOD = 0.0f;
    samplers[1].MaxLOD = D3D12_FLOAT32_MAX;

    CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc;
    rootSignatureDesc.Init(_countof(rootParameters), rootParameters, _countof(samplers), samplers, D3D12_ROOT_SIGNATURE_FLAG_NONE);

    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> error;
    ThrowIfFailed(D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error));
    ThrowIfFailed(m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)));
}

void BistroExteriorMeshShaderShadowD3D12::CreatePipelineState()
{
    const std::wstring exeDir = GetAssetFullPath(L"");
    byte* meshShaderData = nullptr;
    byte* pixelShaderData = nullptr;
    byte* shadowMeshShaderData = nullptr;
    byte* shadowPixelShaderData = nullptr;
    UINT meshShaderSize = 0;
    UINT pixelShaderSize = 0;
    UINT shadowMeshShaderSize = 0;
    UINT shadowPixelShaderSize = 0;
    ThrowIfFailed(ReadDataFromFile((exeDir + L"BistroExteriorMeshShaderShadow.ms.cso").c_str(), &meshShaderData, &meshShaderSize));
    ThrowIfFailed(ReadDataFromFile((exeDir + L"BistroExteriorMeshShaderShadow.ps.cso").c_str(), &pixelShaderData, &pixelShaderSize));
    ThrowIfFailed(ReadDataFromFile((exeDir + L"BistroExteriorMeshShaderShadow.shadow.ms.cso").c_str(), &shadowMeshShaderData, &shadowMeshShaderSize));
    ThrowIfFailed(ReadDataFromFile((exeDir + L"BistroExteriorMeshShaderShadow.shadow.ps.cso").c_str(), &shadowPixelShaderData, &shadowPixelShaderSize));
    std::vector<UINT8> meshShader(meshShaderData, meshShaderData + meshShaderSize);
    std::vector<UINT8> pixelShader(pixelShaderData, pixelShaderData + pixelShaderSize);
    std::vector<UINT8> shadowMeshShader(shadowMeshShaderData, shadowMeshShaderData + shadowMeshShaderSize);
    std::vector<UINT8> shadowPixelShader(shadowPixelShaderData, shadowPixelShaderData + shadowPixelShaderSize);
    free(meshShaderData);
    free(pixelShaderData);
    free(shadowMeshShaderData);
    free(shadowPixelShaderData);

    struct MeshPipelineStateStream
    {
        CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE RootSignature;
        CD3DX12_PIPELINE_STATE_STREAM_MS MS;
        CD3DX12_PIPELINE_STATE_STREAM_PS PS;
        CD3DX12_PIPELINE_STATE_STREAM_RASTERIZER RasterizerState;
        CD3DX12_PIPELINE_STATE_STREAM_BLEND_DESC BlendState;
        CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL DepthStencilState;
        CD3DX12_PIPELINE_STATE_STREAM_SAMPLE_MASK SampleMask;
        CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY PrimitiveTopologyType;
        CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS RTVFormats;
        CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT DSVFormat;
        CD3DX12_PIPELINE_STATE_STREAM_SAMPLE_DESC SampleDesc;
    };

    CD3DX12_RASTERIZER_DESC rasterizer(D3D12_DEFAULT);
    rasterizer.CullMode = D3D12_CULL_MODE_NONE;

    D3D12_RT_FORMAT_ARRAY rtvFormats{};
    rtvFormats.NumRenderTargets = 1;
    rtvFormats.RTFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;

    MeshPipelineStateStream stream{};
    stream.RootSignature = m_rootSignature.Get();
    stream.MS = CD3DX12_SHADER_BYTECODE(meshShader.data(), meshShader.size());
    stream.PS = CD3DX12_SHADER_BYTECODE(pixelShader.data(), pixelShader.size());
    stream.RasterizerState = rasterizer;
    stream.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    stream.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    stream.SampleMask = UINT_MAX;
    stream.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    stream.RTVFormats = rtvFormats;
    stream.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    stream.SampleDesc = DXGI_SAMPLE_DESC{ 1, 0 };

    D3D12_PIPELINE_STATE_STREAM_DESC streamDesc{};
    streamDesc.SizeInBytes = sizeof(stream);
    streamDesc.pPipelineStateSubobjectStream = &stream;
    ThrowIfFailed(m_device->CreatePipelineState(&streamDesc, IID_PPV_ARGS(&m_pipelineState)));

    D3D12_RT_FORMAT_ARRAY shadowRtvFormats{};
    shadowRtvFormats.NumRenderTargets = 0;
    CD3DX12_RASTERIZER_DESC shadowRasterizer = rasterizer;
    shadowRasterizer.DepthBias = 1000;
    shadowRasterizer.SlopeScaledDepthBias = 2.0f;
    shadowRasterizer.DepthBiasClamp = 0.01f;

    MeshPipelineStateStream shadowStream = stream;
    shadowStream.MS = CD3DX12_SHADER_BYTECODE(shadowMeshShader.data(), shadowMeshShader.size());
    shadowStream.PS = CD3DX12_SHADER_BYTECODE(shadowPixelShader.data(), shadowPixelShader.size());
    shadowStream.RasterizerState = shadowRasterizer;
    shadowStream.RTVFormats = shadowRtvFormats;

    D3D12_PIPELINE_STATE_STREAM_DESC shadowStreamDesc{};
    shadowStreamDesc.SizeInBytes = sizeof(shadowStream);
    shadowStreamDesc.pPipelineStateSubobjectStream = &shadowStream;
    ThrowIfFailed(m_device->CreatePipelineState(&shadowStreamDesc, IID_PPV_ARGS(&m_shadowPipelineState)));
}

void BistroExteriorMeshShaderShadowD3D12::CreateBuffers()
{
    auto createUploadBuffer = [this](const void* sourceData, UINT64 bufferSize, ID3D12Resource** resource)
    {
        ThrowIfFailed(m_device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD), D3D12_HEAP_FLAG_NONE, &CD3DX12_RESOURCE_DESC::Buffer(bufferSize), D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(resource)));
        UINT8* dataBegin = nullptr;
        ThrowIfFailed((*resource)->Map(0, nullptr, reinterpret_cast<void**>(&dataBegin)));
        memcpy(dataBegin, sourceData, static_cast<size_t>(bufferSize));
        (*resource)->Unmap(0, nullptr);
    };

    const UINT64 vertexBufferSize = static_cast<UINT64>(m_scene.vertices.size() * sizeof(Bistro::Vertex));
    ThrowIfFailed(m_device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD), D3D12_HEAP_FLAG_NONE, &CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize), D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_vertexBuffer)));
    UINT8* vertexDataBegin = nullptr;
    ThrowIfFailed(m_vertexBuffer->Map(0, nullptr, reinterpret_cast<void**>(&vertexDataBegin)));
    memcpy(vertexDataBegin, m_scene.vertices.data(), vertexBufferSize);
    m_vertexBuffer->Unmap(0, nullptr);

    createUploadBuffer(m_scene.meshlets.data(), static_cast<UINT64>(m_scene.meshlets.size() * sizeof(Bistro::MeshletRecord)), m_meshletBuffer.GetAddressOf());
    createUploadBuffer(m_scene.meshletVertices.data(), static_cast<UINT64>(m_scene.meshletVertices.size() * sizeof(uint32_t)), m_meshletVertexBuffer.GetAddressOf());
    createUploadBuffer(m_scene.meshletTriangles.data(), static_cast<UINT64>(m_scene.meshletTriangles.size() * sizeof(uint32_t)), m_meshletTriangleBuffer.GetAddressOf());
    createUploadBuffer(m_scene.meshletBounds.data(), static_cast<UINT64>(m_scene.meshletBounds.size() * sizeof(Bistro::MeshletBounds)), m_meshletBoundsBuffer.GetAddressOf());

    ThrowIfFailed(m_device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD), D3D12_HEAP_FLAG_NONE, &CD3DX12_RESOURCE_DESC::Buffer(1024 * 64), D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_sceneConstantBuffer)));
    ThrowIfFailed(m_sceneConstantBuffer->Map(0, nullptr, reinterpret_cast<void**>(&m_mappedSceneConstants)));

    m_materialConstantStride = (sizeof(MaterialConstantBuffer) + 255) & ~255u;
    const UINT materialBufferSize = static_cast<UINT>(m_scene.materials.size()) * m_materialConstantStride;
    ThrowIfFailed(m_device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD), D3D12_HEAP_FLAG_NONE, &CD3DX12_RESOURCE_DESC::Buffer(materialBufferSize), D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_materialConstantBuffer)));
    ThrowIfFailed(m_materialConstantBuffer->Map(0, nullptr, reinterpret_cast<void**>(&m_mappedMaterialConstants)));

    for (size_t i = 0; i < m_scene.materials.size(); ++i)
    {
        MaterialConstantBuffer material{};
        material.baseColorFactor = m_scene.materials[i].baseColorFactor;
        material.options = XMFLOAT4(0.0f, 0.0f, m_scene.materials[i].alphaCutoff, m_scene.materials[i].alphaMasked ? 1.0f : 0.0f);
        memcpy(m_mappedMaterialConstants + i * m_materialConstantStride, &material, sizeof(material));
    }

    D3D12_RESOURCE_DESC depthDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D32_FLOAT, m_width, m_height, 1, 0, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
    D3D12_CLEAR_VALUE depthOptimizedClearValue = {};
    depthOptimizedClearValue.Format = DXGI_FORMAT_D32_FLOAT;
    depthOptimizedClearValue.DepthStencil.Depth = 1.0f;
    ThrowIfFailed(m_device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE, &depthDesc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthOptimizedClearValue, IID_PPV_ARGS(&m_depthStencil)));
    m_device->CreateDepthStencilView(m_depthStencil.Get(), nullptr, m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
}

void BistroExteriorMeshShaderShadowD3D12::CreateShadowResources()
{
    m_shadowViewport = CD3DX12_VIEWPORT(0.0f, 0.0f, static_cast<float>(m_shadowResolution), static_cast<float>(m_shadowResolution));
    m_shadowScissorRect = CD3DX12_RECT(0, 0, static_cast<LONG>(m_shadowResolution), static_cast<LONG>(m_shadowResolution));

    D3D12_RESOURCE_DESC shadowDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R32_TYPELESS, m_shadowResolution, m_shadowResolution, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
    D3D12_CLEAR_VALUE shadowClearValue = {};
    shadowClearValue.Format = DXGI_FORMAT_D32_FLOAT;
    shadowClearValue.DepthStencil.Depth = 1.0f;
    ThrowIfFailed(m_device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &shadowDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        &shadowClearValue,
        IID_PPV_ARGS(&m_shadowDepth)));

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(m_dsvHeap->GetCPUDescriptorHandleForHeapStart(), 1, m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV));
    m_device->CreateDepthStencilView(m_shadowDepth.Get(), &dsvDesc, dsvHandle);

    if (m_srvHeap)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(m_srvHeap->GetCPUDescriptorHandleForHeapStart(), m_shadowSrvDescriptorIndex, m_srvDescriptorSize);
        m_device->CreateShaderResourceView(m_shadowDepth.Get(), &srvDesc, srvHandle);
    }
}

UINT BistroExteriorMeshShaderShadowD3D12::CreateTextureResource(const std::wstring& path, bool srgb, const uint8_t fallback[4], std::map<std::wstring, UINT>& cache)
{
    std::wstring key = path.empty() ? (std::wstring(L"fallback:") + std::to_wstring(fallback[0]) + L"," + std::to_wstring(fallback[1]) + L"," + std::to_wstring(fallback[2]) + L"," + std::to_wstring(fallback[3]) + (srgb ? L":srgb" : L":linear")) : path + (srgb ? L":srgb" : L":linear");
    auto found = cache.find(key);
    if (found != cache.end())
    {
        return found->second;
    }

    Bistro::TextureData image = Bistro::LoadTextureD3D12(path, srgb, fallback);
    GpuTexture texture;
    texture.path = path;
    texture.width = image.width;
    texture.height = image.height;
    texture.mipLevels = image.mipLevels;
    texture.fallback = image.fallback;
    D3D12_RESOURCE_DESC textureDesc = CD3DX12_RESOURCE_DESC::Tex2D(image.format, image.width, image.height, 1, image.mipLevels);
    ThrowIfFailed(m_device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE, &textureDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&texture.resource)));
    const UINT64 uploadBufferSize = GetRequiredIntermediateSize(texture.resource.Get(), 0, image.mipLevels);
    ThrowIfFailed(m_device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD), D3D12_HEAP_FLAG_NONE, &CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize), D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&texture.upload)));

    std::vector<D3D12_SUBRESOURCE_DATA> subresources(image.mipLevels);
    for (uint32_t mipIndex = 0; mipIndex < image.mipLevels; ++mipIndex)
    {
        const Bistro::TextureMip& mip = image.mips[mipIndex];
        subresources[mipIndex].pData = image.pixels.data() + mip.offset;
        subresources[mipIndex].RowPitch = static_cast<LONG_PTR>(mip.rowPitch);
        subresources[mipIndex].SlicePitch = static_cast<LONG_PTR>(mip.slicePitch);
    }
    UpdateSubresources(m_commandList.Get(), texture.resource.Get(), texture.upload.Get(), 0, 0, image.mipLevels, subresources.data());
    m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(texture.resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));

    const UINT index = static_cast<UINT>(m_textures.size());
    m_textures.push_back(texture);
    cache[key] = index;
    return index;
}

void BistroExteriorMeshShaderShadowD3D12::CreateTextures()
{
    const uint8_t white[] = { 255, 255, 255, 255 };
    const uint8_t normal[] = { 128, 128, 255, 255 };
    const uint8_t specular[] = { 255, 180, 0, 255 };
    const uint8_t black[] = { 0, 0, 0, 255 };
    std::map<std::wstring, UINT> textureCache;

    m_materialTextureIndices.resize(m_scene.materials.size());
    for (UINT materialIndex = 0; materialIndex < m_scene.materials.size(); ++materialIndex)
    {
        const Bistro::Material& material = m_scene.materials[materialIndex];
        UINT indices[Bistro::TextureSlotCount] = {};
        indices[0] = CreateTextureResource(material.textures[Bistro::TextureSlotBaseColor], true, white, textureCache);
        indices[1] = CreateTextureResource(material.textures[Bistro::TextureSlotNormal], false, normal, textureCache);
        indices[2] = CreateTextureResource(material.textures[Bistro::TextureSlotSpecular], false, specular, textureCache);
        indices[3] = CreateTextureResource(material.textures[Bistro::TextureSlotEmissive], false, black, textureCache);
        std::copy(std::begin(indices), std::end(indices), m_materialTextureIndices[materialIndex].begin());

        for (UINT slot = 0; slot < Bistro::TextureSlotCount; ++slot)
        {
            const D3D12_RESOURCE_DESC textureDesc = m_textures[indices[slot]].resource->GetDesc();
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Format = textureDesc.Format;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MipLevels = textureDesc.MipLevels;
            CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(m_srvHeap->GetCPUDescriptorHandleForHeapStart(), materialIndex * Bistro::TextureSlotCount + slot, m_srvDescriptorSize);
            m_device->CreateShaderResourceView(m_textures[indices[slot]].resource.Get(), &srvDesc, srvHandle);
        }
    }
}

void BistroExteriorMeshShaderShadowD3D12::OnUpdate()
{
    BuildUI();

    auto now = std::chrono::steady_clock::now();
    float delta = std::chrono::duration<float>(now - m_lastUpdate).count();
    m_lastUpdate = now;
    delta = (std::min)(delta, 0.05f);
    UpdateConstantBuffer(delta);
}

void BistroExteriorMeshShaderShadowD3D12::UpdateConstantBuffer(float deltaSeconds)
{
    m_camera.SetMoveSpeeds(m_baseMoveSpeed, m_fastMoveSpeed);
    m_camera.Update(deltaSeconds);
    XMMATRIX view = m_camera.GetViewMatrix();
    XMMATRIX projection = XMMatrixPerspectiveFovLH(XMConvertToRadians(60.0f), m_aspectRatio, 0.1f, 1000.0f);
    const XMMATRIX viewProjection = view * projection;
    XMStoreFloat4x4(&m_sceneConstants.viewProjection, viewProjection);
    XMFLOAT3 cameraPosition = m_camera.GetPosition();
    m_sceneConstants.cameraPosition = XMFLOAT4(cameraPosition.x, cameraPosition.y, cameraPosition.z, 1.0f);

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
    m_sceneConstants.lightDirection = XMFLOAT4(m_lightDirection[0], m_lightDirection[1], m_lightDirection[2], 0.0f);
    m_sceneConstants.lightColor = XMFLOAT4(m_lightColor[0], m_lightColor[1], m_lightColor[2], m_lightIntensity);
    m_sceneConstants.debugOptions = XMFLOAT4(static_cast<float>(m_debugViewMode), m_debugNormalMapYFlip ? 1.0f : 0.0f, static_cast<float>(m_debugNormalForceMip), m_debugNormalMipBias);
    m_sceneConstants.shadowOptions = XMFLOAT4(m_shadowsEnabled ? 1.0f : 0.0f, m_shadowDepthBias, m_shadowNormalBias, static_cast<float>(m_shadowPcfRadius));

    const float yaw = m_camera.GetYawRadians();
    const float pitch = m_camera.GetPitchRadians();
    XMVECTOR cameraForward = XMVector3Normalize(XMVectorSet(sinf(yaw) * cosf(pitch), sinf(pitch), cosf(yaw) * cosf(pitch), 0.0f));
    XMVECTOR cameraPos = XMLoadFloat3(&cameraPosition);
    XMVECTOR focus = cameraPos + cameraForward * m_shadowFocusDistance;
    XMVECTOR lightEye = focus - lightDirection * (m_shadowDepthRange * 0.5f);
    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    if (fabsf(XMVectorGetX(XMVector3Dot(lightDirection, up))) > 0.96f)
    {
        up = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
    }
    XMMATRIX lightView = XMMatrixLookAtLH(lightEye, focus, up);
    XMMATRIX lightProjection = XMMatrixOrthographicLH(m_shadowOrthoSize, m_shadowOrthoSize, 0.0f, m_shadowDepthRange);
    const XMMATRIX lightViewProjection = lightView * lightProjection;
    XMStoreFloat4x4(&m_sceneConstants.lightViewProjection, lightViewProjection);

    auto storeFrustumPlanes = [](const XMMATRIX& matrix, XMFLOAT4 planes[6])
    {
        XMFLOAT4X4 values{};
        XMStoreFloat4x4(&values, matrix);
        planes[0] = NormalizePlane(values._14 + values._11, values._24 + values._21, values._34 + values._31, values._44 + values._41);
        planes[1] = NormalizePlane(values._14 - values._11, values._24 - values._21, values._34 - values._31, values._44 - values._41);
        planes[2] = NormalizePlane(values._14 + values._12, values._24 + values._22, values._34 + values._32, values._44 + values._42);
        planes[3] = NormalizePlane(values._14 - values._12, values._24 - values._22, values._34 - values._32, values._44 - values._42);
        planes[4] = NormalizePlane(values._13, values._23, values._33, values._43);
        planes[5] = NormalizePlane(values._14 - values._13, values._24 - values._23, values._34 - values._33, values._44 - values._43);
    };

    storeFrustumPlanes(viewProjection, m_sceneConstants.frustumPlanes);
    storeFrustumPlanes(lightViewProjection, m_sceneConstants.lightFrustumPlanes);

    m_lastVisibleMeshlets = 0;
    m_lastShadowVisibleMeshlets = 0;
    for (const Bistro::MeshletBounds& bounds : m_scene.meshletBounds)
    {
        m_lastVisibleMeshlets += IsMeshletVisible(bounds, m_sceneConstants.frustumPlanes, &cameraPosition) ? 1u : 0u;
        m_lastShadowVisibleMeshlets += IsMeshletVisible(bounds, m_sceneConstants.lightFrustumPlanes, nullptr) ? 1u : 0u;
    }
    memcpy(m_mappedSceneConstants, &m_sceneConstants, sizeof(m_sceneConstants));
}

void BistroExteriorMeshShaderShadowD3D12::OnRender()
{
    PopulateCommandList();
    ID3D12CommandList* commandLists[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(_countof(commandLists), commandLists);
    const UINT syncInterval = m_vsyncEnabled ? 1u : 0u;
    const UINT presentFlags = !m_vsyncEnabled && m_tearingSupported ? DXGI_PRESENT_ALLOW_TEARING : 0u;
    ThrowIfFailed(m_swapChain->Present(syncInterval, presentFlags));
    WaitForPreviousFrame();
}

void BistroExteriorMeshShaderShadowD3D12::PopulateCommandList()
{
    ThrowIfFailed(m_commandAllocator->Reset());
    ThrowIfFailed(m_commandList->Reset(m_commandAllocator.Get(), m_pipelineState.Get()));

    ID3D12DescriptorHeap* descriptorHeaps[] = { m_srvHeap.Get() };
    m_commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
    m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
    m_commandList->SetGraphicsRootConstantBufferView(RootParameterSceneConstants, m_sceneConstantBuffer->GetGPUVirtualAddress());
    m_commandList->SetGraphicsRootShaderResourceView(RootParameterVertices, m_vertexBuffer->GetGPUVirtualAddress());
    m_commandList->SetGraphicsRootShaderResourceView(RootParameterMeshlets, m_meshletBuffer->GetGPUVirtualAddress());
    m_commandList->SetGraphicsRootShaderResourceView(RootParameterMeshletVertices, m_meshletVertexBuffer->GetGPUVirtualAddress());
    m_commandList->SetGraphicsRootShaderResourceView(RootParameterMeshletTriangles, m_meshletTriangleBuffer->GetGPUVirtualAddress());
    m_commandList->SetGraphicsRootShaderResourceView(RootParameterMeshletBounds, m_meshletBoundsBuffer->GetGPUVirtualAddress());

    m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_shadowDepth.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE));
    CD3DX12_CPU_DESCRIPTOR_HANDLE shadowDsvHandle(m_dsvHeap->GetCPUDescriptorHandleForHeapStart(), 1, m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV));
    m_commandList->SetPipelineState(m_shadowPipelineState.Get());
    m_commandList->RSSetViewports(1, &m_shadowViewport);
    m_commandList->RSSetScissorRects(1, &m_shadowScissorRect);
    m_commandList->OMSetRenderTargets(0, nullptr, FALSE, &shadowDsvHandle);
    m_commandList->ClearDepthStencilView(shadowDsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    DrawScene(true);
    m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_shadowDepth.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));

    m_commandList->SetPipelineState(m_pipelineState.Get());
    m_commandList->RSSetViewports(1, &m_viewport);
    m_commandList->RSSetScissorRects(1, &m_scissorRect);
    m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, m_rtvDescriptorSize);
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
    m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);
    const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
    m_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
    m_commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    CD3DX12_GPU_DESCRIPTOR_HANDLE shadowSrvHandle(m_srvHeap->GetGPUDescriptorHandleForHeapStart(), m_shadowSrvDescriptorIndex, m_srvDescriptorSize);
    m_commandList->SetGraphicsRootDescriptorTable(RootParameterShadowTexture, shadowSrvHandle);
    DrawScene(false);

    ID3D12DescriptorHeap* imguiHeaps[] = { m_imguiDescriptorHeap.Get() };
    m_commandList->SetDescriptorHeaps(_countof(imguiHeaps), imguiHeaps);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), m_commandList.Get());

    m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));
    ThrowIfFailed(m_commandList->Close());
}

void BistroExteriorMeshShaderShadowD3D12::DrawScene(bool shadowPass)
{
    for (const Bistro::MeshletDispatchRange& range : m_scene.meshletDispatchRanges)
    {
        const UINT materialIndex = std::min<UINT>(range.materialIndex, static_cast<UINT>(m_scene.materials.size() - 1));
        m_commandList->SetGraphicsRootConstantBufferView(RootParameterMaterialConstants, m_materialConstantBuffer->GetGPUVirtualAddress() + materialIndex * m_materialConstantStride);
        for (UINT slot = 0; slot < Bistro::TextureSlotCount; ++slot)
        {
            CD3DX12_GPU_DESCRIPTOR_HANDLE srvHandle(m_srvHeap->GetGPUDescriptorHandleForHeapStart(), materialIndex * Bistro::TextureSlotCount + slot, m_srvDescriptorSize);
            m_commandList->SetGraphicsRootDescriptorTable(RootParameterTextureBase + slot, srvHandle);
        }

        UINT meshletBase = range.firstMeshlet;
        UINT meshletsRemaining = range.meshletCount;
        while (meshletsRemaining > 0)
        {
            const UINT meshletCount = (std::min)(meshletsRemaining, MaxMeshletsPerDispatch);
            MeshletDrawConstants constants{};
            constants.meshletBase = meshletBase;
            constants.meshletCount = meshletCount;
            constants.debugMode = static_cast<UINT>(m_debugViewMode);
            constants.cullAgainstLight = shadowPass ? 1u : 0u;
            m_commandList->SetGraphicsRoot32BitConstants(RootParameterMeshletDrawConstants, 4, &constants, 0);
            m_commandList->DispatchMesh(meshletCount, 1, 1);
            meshletBase += meshletCount;
            meshletsRemaining -= meshletCount;
        }
    }
}

void BistroExteriorMeshShaderShadowD3D12::OnDestroy()
{
    WaitForPreviousFrame();
    ShutdownImGui();
    if (m_sceneConstantBuffer) m_sceneConstantBuffer->Unmap(0, nullptr);
    if (m_materialConstantBuffer) m_materialConstantBuffer->Unmap(0, nullptr);
    CloseHandle(m_fenceEvent);
}

void BistroExteriorMeshShaderShadowD3D12::WaitForPreviousFrame()
{
    const UINT64 fence = m_fenceValue;
    ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), fence));
    m_fenceValue++;
    if (m_fence->GetCompletedValue() < fence)
    {
        ThrowIfFailed(m_fence->SetEventOnCompletion(fence, m_fenceEvent));
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
}

void BistroExteriorMeshShaderShadowD3D12::ResizeOutput(UINT width, UINT height)
{
    if (width == 0 || height == 0 || !m_swapChain || (m_width == width && m_height == height))
    {
        return;
    }

    WaitForPreviousFrame();
    for (ComPtr<ID3D12Resource>& renderTarget : m_renderTargets)
    {
        renderTarget.Reset();
    }
    m_depthStencil.Reset();

    m_width = width;
    m_height = height;
    m_aspectRatio = static_cast<float>(m_width) / static_cast<float>(m_height);
    m_viewport = CD3DX12_VIEWPORT(0.0f, 0.0f, static_cast<float>(m_width), static_cast<float>(m_height));
    m_scissorRect = CD3DX12_RECT(0, 0, static_cast<LONG>(m_width), static_cast<LONG>(m_height));

    ThrowIfFailed(m_swapChain->ResizeBuffers(FrameCount, m_width, m_height, DXGI_FORMAT_R8G8B8A8_UNORM, m_tearingSupported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0));
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());
    for (UINT n = 0; n < FrameCount; ++n)
    {
        ThrowIfFailed(m_swapChain->GetBuffer(n, IID_PPV_ARGS(&m_renderTargets[n])));
        m_device->CreateRenderTargetView(m_renderTargets[n].Get(), nullptr, rtvHandle);
        rtvHandle.Offset(1, m_rtvDescriptorSize);
    }

    D3D12_RESOURCE_DESC depthDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D32_FLOAT, m_width, m_height, 1, 0, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
    D3D12_CLEAR_VALUE depthOptimizedClearValue = {};
    depthOptimizedClearValue.Format = DXGI_FORMAT_D32_FLOAT;
    depthOptimizedClearValue.DepthStencil.Depth = 1.0f;
    ThrowIfFailed(m_device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE, &depthDesc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthOptimizedClearValue, IID_PPV_ARGS(&m_depthStencil)));
    m_device->CreateDepthStencilView(m_depthStencil.Get(), nullptr, m_dsvHeap->GetCPUDescriptorHandleForHeapStart());

    Bistro::ResizeClientArea(Win32Application::GetHwnd(), m_width, m_height);
}

void BistroExteriorMeshShaderShadowD3D12::OnKeyDown(UINT8 key)
{
    if (key == VK_ESCAPE)
    {
        PostMessage(Win32Application::GetHwnd(), WM_CLOSE, 0, 0);
    }
}

void BistroExteriorMeshShaderShadowD3D12::OnWindowMessage(UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_SETFOCUS) m_camera.SetActive(true);
    if (message == WM_KILLFOCUS) m_camera.SetActive(false);
    if (message == WM_RBUTTONDOWN || message == WM_RBUTTONUP) m_camera.OnMouseButton(message, wParam);
    if (message == WM_MOUSEMOVE) m_camera.OnMouseMove(lParam);
}

void BistroExteriorMeshShaderShadowD3D12::InitializeImGui()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;

    ImGui_ImplWin32_Init(Win32Application::GetHwnd());

    ImGui_ImplDX12_InitInfo initInfo = {};
    initInfo.Device = m_device.Get();
    initInfo.CommandQueue = m_commandQueue.Get();
    initInfo.NumFramesInFlight = FrameCount;
    initInfo.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    initInfo.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    initInfo.UserData = this;
    initInfo.SrvDescriptorHeap = m_imguiDescriptorHeap.Get();
    initInfo.SrvDescriptorAllocFn = AllocateImGuiDescriptor;
    initInfo.SrvDescriptorFreeFn = FreeImGuiDescriptor;
    ThrowIfFailed(ImGui_ImplDX12_Init(&initInfo) ? S_OK : E_FAIL);
}

void BistroExteriorMeshShaderShadowD3D12::ShutdownImGui()
{
    if (ImGui::GetCurrentContext())
    {
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
    }
}

void BistroExteriorMeshShaderShadowD3D12::BuildUI()
{
    ImGui_ImplDX12_NewFrame();
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
    ImGui::TextUnformatted("Shadow Map");
    ImGui::Checkbox("Enable Shadows", &m_shadowsEnabled);
    const char* shadowResolutions[] = { "1024", "2048", "4096" };
    int previousShadowResolutionIndex = m_shadowResolutionIndex;
    ImGui::Combo("Resolution", &m_shadowResolutionIndex, shadowResolutions, _countof(shadowResolutions));
    if (m_shadowResolutionIndex != previousShadowResolutionIndex)
    {
        const UINT values[] = { 1024, 2048, 4096 };
        m_shadowResolution = values[std::clamp(m_shadowResolutionIndex, 0, 2)];
        WaitForPreviousFrame();
        CreateShadowResources();
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
        "Meshlet Color",
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

void BistroExteriorMeshShaderShadowD3D12::BuildRendererStatsUI()
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
    ImGui::Text("API: Direct3D 12");
    ImGui::Text("FPS: %.1f", fps);
    ImGui::Text("Frame Time: %.3f ms", frameTimeMs);
    ImGui::Checkbox("VSync", &m_vsyncEnabled);
    uint32_t outputWidth = m_width;
    uint32_t outputHeight = m_height;
    if (Bistro::DrawResolutionCombo(m_width, m_height, outputWidth, outputHeight))
    {
        ResizeOutput(outputWidth, outputHeight);
    }
    ImGui::Text("Output: %ux%u", m_width, m_height);
    ImGui::Text("Tearing: %s", m_tearingSupported ? "Supported" : "Unsupported");
    ImGui::Separator();
    ImGui::TextUnformatted("Scene");
    ImGui::Text("Materials: %zu", m_scene.materials.size());
    ImGui::Text("Draw Calls: %zu", m_scene.draws.size());
    ImGui::Text("Meshlet Dispatches: %zu", m_scene.meshletDispatchRanges.size());
    ImGui::Text("Meshlets: %zu", m_scene.meshlets.size());
    ImGui::Text("Submitted Meshlets: %zu", m_scene.meshlets.size());
    ImGui::Text("Vertices: %zu", m_scene.vertices.size());
    ImGui::Text("Indices: %zu", m_scene.indices.size());
    ImGui::Text("Submitted Indices: %llu", static_cast<unsigned long long>(submittedIndexCount));
    ImGui::Text("Primitives: %llu", static_cast<unsigned long long>(primitiveCount));
    ImGui::Text("Textures: %zu", m_textures.size());
    ImGui::Separator();
    ImGui::TextUnformatted("Shadow");
    ImGui::Text("Enabled: %s", m_shadowsEnabled ? "Yes" : "No");
    ImGui::Text("Resolution: %u x %u", m_shadowResolution, m_shadowResolution);
    ImGui::Text("Meshlet Dispatches: %zu", m_scene.meshletDispatchRanges.size());
    ImGui::Text("Submitted Meshlets: %zu", m_scene.meshlets.size());
    ImGui::Text("Primitives: %llu", static_cast<unsigned long long>(primitiveCount));
    ImGui::Text("Bias: %.4f / Normal %.3f", m_shadowDepthBias, m_shadowNormalBias);
    ImGui::Text("PCF Radius: %d", m_shadowPcfRadius);
    ImGui::Separator();
    ImGui::TextUnformatted("Texture Diagnostics");
    ImGui::Text("Normal Maps: %d / %zu", normalTextureCount, m_scene.materials.size());
    ImGui::Text("Normal Fallbacks: %d", normalFallbackCount);
    ImGui::Text("Normal 1x1 SRVs: %d", normalOnePixelCount);
    ImGui::End();
}

void BistroExteriorMeshShaderShadowD3D12::ResetLight()
{
    m_lightDirection[0] = -0.35f;
    m_lightDirection[1] = -0.8f;
    m_lightDirection[2] = 0.45f;
    m_lightColor[0] = 1.0f;
    m_lightColor[1] = 0.96f;
    m_lightColor[2] = 0.88f;
    m_lightIntensity = 4.0f;
}

void BistroExteriorMeshShaderShadowD3D12::ResetCameraView()
{
    m_camera.Reset(m_defaultCameraPosition, m_defaultCameraYaw, m_defaultCameraPitch);
}

void BistroExteriorMeshShaderShadowD3D12::ResetCameraSpeeds()
{
    m_baseMoveSpeed = 17.0f;
    m_fastMoveSpeed = 58.2f;
}

void BistroExteriorMeshShaderShadowD3D12::ResetShadowSettings()
{
    m_shadowsEnabled = true;
    m_shadowDepthBias = 0.002f;
    m_shadowNormalBias = 0.05f;
    m_shadowPcfRadius = 1;
    m_shadowOrthoSize = 50.0f;
    m_shadowFocusDistance = 25.0f;
    m_shadowDepthRange = 160.0f;
}

void BistroExteriorMeshShaderShadowD3D12::AllocateImGuiDescriptor(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* outCpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE* outGpuHandle)
{
    auto* sample = static_cast<BistroExteriorMeshShaderShadowD3D12*>(info->UserData);
    const UINT descriptorIndex = sample->m_imguiDescriptorCursor++;
    if (descriptorIndex >= ImGuiDescriptorCount)
    {
        ThrowIfFailed(E_OUTOFMEMORY);
    }

    CD3DX12_CPU_DESCRIPTOR_HANDLE cpuHandle(sample->m_imguiDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), descriptorIndex, sample->m_imguiDescriptorSize);
    CD3DX12_GPU_DESCRIPTOR_HANDLE gpuHandle(sample->m_imguiDescriptorHeap->GetGPUDescriptorHandleForHeapStart(), descriptorIndex, sample->m_imguiDescriptorSize);
    *outCpuHandle = cpuHandle;
    *outGpuHandle = gpuHandle;
}

void BistroExteriorMeshShaderShadowD3D12::FreeImGuiDescriptor(ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE)
{
}
