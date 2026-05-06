#include "stdafx.h"
#include "BistroExteriorMeshShaderCullingD3D12.h"
#include "..\..\Common\BistroTexture.h"

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
    constexpr UINT AmplificationThreadCount = 128;
    constexpr UINT MaxDispatchMeshThreadGroupsX = 65535;
    constexpr UINT MaxMeshletsPerDispatch = AmplificationThreadCount * MaxDispatchMeshThreadGroupsX;

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

    bool IsMeshletVisible(const Bistro::MeshletBounds& bounds, const XMFLOAT4 frustumPlanes[6], const XMFLOAT3& cameraPosition)
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

        const XMFLOAT3 centerToCamera(
            bounds.sphere.x - cameraPosition.x,
            bounds.sphere.y - cameraPosition.y,
            bounds.sphere.z - cameraPosition.z);
        const float length = std::sqrt(centerToCamera.x * centerToCamera.x + centerToCamera.y * centerToCamera.y + centerToCamera.z * centerToCamera.z);
        const float coneDistance =
            centerToCamera.x * bounds.coneAxis.x +
            centerToCamera.y * bounds.coneAxis.y +
            centerToCamera.z * bounds.coneAxis.z;
        return coneDistance < bounds.coneAxis.w * length + bounds.sphere.w;
    }
}

BistroExteriorMeshShaderCullingD3D12::BistroExteriorMeshShaderCullingD3D12(UINT width, UINT height, std::wstring name) :
    DXSample(width, height, name),
    m_viewport(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)),
    m_scissorRect(0, 0, static_cast<LONG>(width), static_cast<LONG>(height))
{
}

void BistroExteriorMeshShaderCullingD3D12::OnInit()
{
    LoadPipeline();
    LoadAssets();
    m_lastUpdate = std::chrono::steady_clock::now();
}

void BistroExteriorMeshShaderCullingD3D12::LoadPipeline()
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
        throw std::runtime_error("This GPU/driver does not support Direct3D 12 Mesh Shader Tier 1. BistroExteriorMeshShaderCullingD3D12 has no vertex-shader fallback.");
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
    dsvHeapDesc.NumDescriptors = 1;
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

void BistroExteriorMeshShaderCullingD3D12::LoadAssets()
{
    m_scene = Bistro::LoadScene(Bistro::FindAssetRoot());

    XMFLOAT3 center(
        (m_scene.boundsMin.x + m_scene.boundsMax.x) * 0.5f,
        (m_scene.boundsMin.y + m_scene.boundsMax.y) * 0.5f,
        (m_scene.boundsMin.z + m_scene.boundsMax.z) * 0.5f);
    const float radius = (std::max)((std::max)(m_scene.boundsMax.x - m_scene.boundsMin.x, m_scene.boundsMax.y - m_scene.boundsMin.y), m_scene.boundsMax.z - m_scene.boundsMin.z);
    m_defaultCameraPosition = XMFLOAT3(center.x, center.y + radius * 0.18f, m_scene.boundsMin.z - radius * 0.25f);
    m_defaultCameraYaw = 0.0f;
    m_defaultCameraPitch = -0.08f;
    ResetCameraView();

    CreateRootSignature();
    CreatePipelineState();

    const UINT materialCount = static_cast<UINT>(std::max<size_t>(1, m_scene.materials.size()));
    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.NumDescriptors = materialCount * Bistro::TextureSlotCount;
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(m_device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&m_srvHeap)));

    ThrowIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocator.Get(), m_pipelineState.Get(), IID_PPV_ARGS(&m_commandList)));
    CreateBuffers();
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

void BistroExteriorMeshShaderCullingD3D12::CreateRootSignature()
{
    CD3DX12_DESCRIPTOR_RANGE srvRanges[Bistro::TextureSlotCount];
    for (UINT slot = 0; slot < Bistro::TextureSlotCount; ++slot)
    {
        srvRanges[slot].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, slot);
    }

    CD3DX12_ROOT_PARAMETER rootParameters[RootParameterTextureBase + Bistro::TextureSlotCount];
    rootParameters[RootParameterSceneConstants].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);
    rootParameters[RootParameterMaterialConstants].InitAsConstantBufferView(1, 0, D3D12_SHADER_VISIBILITY_PIXEL);
    rootParameters[RootParameterVertices].InitAsShaderResourceView(4, 0, D3D12_SHADER_VISIBILITY_ALL);
    rootParameters[RootParameterMeshlets].InitAsShaderResourceView(5, 0, D3D12_SHADER_VISIBILITY_ALL);
    rootParameters[RootParameterMeshletVertices].InitAsShaderResourceView(6, 0, D3D12_SHADER_VISIBILITY_ALL);
    rootParameters[RootParameterMeshletTriangles].InitAsShaderResourceView(7, 0, D3D12_SHADER_VISIBILITY_ALL);
    rootParameters[RootParameterMeshletBounds].InitAsShaderResourceView(8, 0, D3D12_SHADER_VISIBILITY_ALL);
    rootParameters[RootParameterMeshletDrawConstants].InitAsConstants(4, 2, 0, D3D12_SHADER_VISIBILITY_ALL);
    for (UINT slot = 0; slot < Bistro::TextureSlotCount; ++slot)
    {
        rootParameters[RootParameterTextureBase + slot].InitAsDescriptorTable(1, &srvRanges[slot], D3D12_SHADER_VISIBILITY_PIXEL);
    }

    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_ANISOTROPIC;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.MaxAnisotropy = 8;
    sampler.ShaderRegister = 0;
    sampler.RegisterSpace = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    sampler.MinLOD = 0.0f;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;

    CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc;
    rootSignatureDesc.Init(_countof(rootParameters), rootParameters, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_NONE);

    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> error;
    ThrowIfFailed(D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error));
    ThrowIfFailed(m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)));
}

void BistroExteriorMeshShaderCullingD3D12::CreatePipelineState()
{
    const std::wstring exeDir = GetAssetFullPath(L"");
    byte* amplificationShaderData = nullptr;
    byte* meshShaderData = nullptr;
    byte* pixelShaderData = nullptr;
    UINT amplificationShaderSize = 0;
    UINT meshShaderSize = 0;
    UINT pixelShaderSize = 0;
    ThrowIfFailed(ReadDataFromFile((exeDir + L"BistroExteriorMeshShaderCulling.as.cso").c_str(), &amplificationShaderData, &amplificationShaderSize));
    ThrowIfFailed(ReadDataFromFile((exeDir + L"BistroExteriorMeshShaderCulling.ms.cso").c_str(), &meshShaderData, &meshShaderSize));
    ThrowIfFailed(ReadDataFromFile((exeDir + L"BistroExteriorMeshShaderCulling.ps.cso").c_str(), &pixelShaderData, &pixelShaderSize));
    std::vector<UINT8> amplificationShader(amplificationShaderData, amplificationShaderData + amplificationShaderSize);
    std::vector<UINT8> meshShader(meshShaderData, meshShaderData + meshShaderSize);
    std::vector<UINT8> pixelShader(pixelShaderData, pixelShaderData + pixelShaderSize);
    free(amplificationShaderData);
    free(meshShaderData);
    free(pixelShaderData);

    D3D12_RT_FORMAT_ARRAY rtvFormats{};
    rtvFormats.NumRenderTargets = 1;
    rtvFormats.RTFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;

    struct MeshPipelineStateStream
    {
        CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE RootSignature;
        CD3DX12_PIPELINE_STATE_STREAM_AS AS;
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
    } stream;

    CD3DX12_RASTERIZER_DESC rasterizer(D3D12_DEFAULT);
    rasterizer.CullMode = D3D12_CULL_MODE_NONE;

    stream.RootSignature = m_rootSignature.Get();
    stream.AS = CD3DX12_SHADER_BYTECODE(amplificationShader.data(), amplificationShader.size());
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
}

void BistroExteriorMeshShaderCullingD3D12::CreateBuffers()
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

UINT BistroExteriorMeshShaderCullingD3D12::CreateTextureResource(const std::wstring& path, bool srgb, const uint8_t fallback[4], std::map<std::wstring, UINT>& cache)
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

void BistroExteriorMeshShaderCullingD3D12::CreateTextures()
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

void BistroExteriorMeshShaderCullingD3D12::OnUpdate()
{
    BuildUI();

    auto now = std::chrono::steady_clock::now();
    float delta = std::chrono::duration<float>(now - m_lastUpdate).count();
    m_lastUpdate = now;
    delta = (std::min)(delta, 0.05f);
    UpdateConstantBuffer(delta);
}

void BistroExteriorMeshShaderCullingD3D12::UpdateConstantBuffer(float deltaSeconds)
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
    XMFLOAT4X4 viewProjectionValues{};
    XMStoreFloat4x4(&viewProjectionValues, viewProjection);
    m_sceneConstants.frustumPlanes[0] = NormalizePlane(viewProjectionValues._14 + viewProjectionValues._11, viewProjectionValues._24 + viewProjectionValues._21, viewProjectionValues._34 + viewProjectionValues._31, viewProjectionValues._44 + viewProjectionValues._41);
    m_sceneConstants.frustumPlanes[1] = NormalizePlane(viewProjectionValues._14 - viewProjectionValues._11, viewProjectionValues._24 - viewProjectionValues._21, viewProjectionValues._34 - viewProjectionValues._31, viewProjectionValues._44 - viewProjectionValues._41);
    m_sceneConstants.frustumPlanes[2] = NormalizePlane(viewProjectionValues._14 + viewProjectionValues._12, viewProjectionValues._24 + viewProjectionValues._22, viewProjectionValues._34 + viewProjectionValues._32, viewProjectionValues._44 + viewProjectionValues._42);
    m_sceneConstants.frustumPlanes[3] = NormalizePlane(viewProjectionValues._14 - viewProjectionValues._12, viewProjectionValues._24 - viewProjectionValues._22, viewProjectionValues._34 - viewProjectionValues._32, viewProjectionValues._44 - viewProjectionValues._42);
    m_sceneConstants.frustumPlanes[4] = NormalizePlane(viewProjectionValues._13, viewProjectionValues._23, viewProjectionValues._33, viewProjectionValues._43);
    m_sceneConstants.frustumPlanes[5] = NormalizePlane(viewProjectionValues._14 - viewProjectionValues._13, viewProjectionValues._24 - viewProjectionValues._23, viewProjectionValues._34 - viewProjectionValues._33, viewProjectionValues._44 - viewProjectionValues._43);

    m_lastVisibleMeshlets = 0;
    for (const Bistro::MeshletBounds& bounds : m_scene.meshletBounds)
    {
        m_lastVisibleMeshlets += IsMeshletVisible(bounds, m_sceneConstants.frustumPlanes, cameraPosition) ? 1u : 0u;
    }
    memcpy(m_mappedSceneConstants, &m_sceneConstants, sizeof(m_sceneConstants));
}

void BistroExteriorMeshShaderCullingD3D12::OnRender()
{
    PopulateCommandList();
    ID3D12CommandList* commandLists[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(_countof(commandLists), commandLists);
    const UINT syncInterval = m_vsyncEnabled ? 1u : 0u;
    const UINT presentFlags = !m_vsyncEnabled && m_tearingSupported ? DXGI_PRESENT_ALLOW_TEARING : 0u;
    ThrowIfFailed(m_swapChain->Present(syncInterval, presentFlags));
    WaitForPreviousFrame();
}

void BistroExteriorMeshShaderCullingD3D12::PopulateCommandList()
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
    m_commandList->RSSetViewports(1, &m_viewport);
    m_commandList->RSSetScissorRects(1, &m_scissorRect);
    m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, m_rtvDescriptorSize);
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
    m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);
    const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
    m_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
    m_commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

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
            const UINT dispatchCount = (meshletCount + AmplificationThreadCount - 1) / AmplificationThreadCount;
            MeshletDrawConstants constants{};
            constants.meshletBase = meshletBase;
            constants.meshletCount = meshletCount;
            constants.debugMode = static_cast<UINT>(m_debugViewMode);
            m_commandList->SetGraphicsRoot32BitConstants(RootParameterMeshletDrawConstants, 4, &constants, 0);
            m_commandList->DispatchMesh(dispatchCount, 1, 1);
            meshletBase += meshletCount;
            meshletsRemaining -= meshletCount;
        }
    }

    ID3D12DescriptorHeap* imguiHeaps[] = { m_imguiDescriptorHeap.Get() };
    m_commandList->SetDescriptorHeaps(_countof(imguiHeaps), imguiHeaps);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), m_commandList.Get());

    m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));
    ThrowIfFailed(m_commandList->Close());
}

void BistroExteriorMeshShaderCullingD3D12::OnDestroy()
{
    WaitForPreviousFrame();
    ShutdownImGui();
    if (m_sceneConstantBuffer) m_sceneConstantBuffer->Unmap(0, nullptr);
    if (m_materialConstantBuffer) m_materialConstantBuffer->Unmap(0, nullptr);
    CloseHandle(m_fenceEvent);
}

void BistroExteriorMeshShaderCullingD3D12::WaitForPreviousFrame()
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

void BistroExteriorMeshShaderCullingD3D12::OnKeyDown(UINT8 key)
{
    if (key == VK_ESCAPE)
    {
        PostMessage(Win32Application::GetHwnd(), WM_CLOSE, 0, 0);
    }
}

void BistroExteriorMeshShaderCullingD3D12::OnWindowMessage(UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_SETFOCUS) m_camera.SetActive(true);
    if (message == WM_KILLFOCUS) m_camera.SetActive(false);
    if (message == WM_RBUTTONDOWN || message == WM_RBUTTONUP) m_camera.OnMouseButton(message, wParam);
    if (message == WM_MOUSEMOVE) m_camera.OnMouseMove(lParam);
}

void BistroExteriorMeshShaderCullingD3D12::InitializeImGui()
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

void BistroExteriorMeshShaderCullingD3D12::ShutdownImGui()
{
    if (ImGui::GetCurrentContext())
    {
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
    }
}

void BistroExteriorMeshShaderCullingD3D12::BuildUI()
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
        "Meshlet Color"
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

void BistroExteriorMeshShaderCullingD3D12::BuildRendererStatsUI()
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
    ImGui::Text("Tearing: %s", m_tearingSupported ? "Supported" : "Unsupported");
    ImGui::Separator();
    ImGui::TextUnformatted("Scene");
    ImGui::Text("Materials: %zu", m_scene.materials.size());
    ImGui::Text("Draw Calls: %zu", m_scene.draws.size());
    ImGui::Text("Meshlet Dispatches: %zu", m_scene.meshletDispatchRanges.size());
    ImGui::Text("Meshlets: %zu", m_scene.meshlets.size());
    ImGui::Text("Visible Meshlets: %u", m_lastVisibleMeshlets);
    ImGui::Text("Culled Meshlets: %u", static_cast<uint32_t>(m_scene.meshlets.size()) - m_lastVisibleMeshlets);
    ImGui::Text("Vertices: %zu", m_scene.vertices.size());
    ImGui::Text("Indices: %zu", m_scene.indices.size());
    ImGui::Text("Submitted Indices: %llu", static_cast<unsigned long long>(submittedIndexCount));
    ImGui::Text("Primitives: %llu", static_cast<unsigned long long>(primitiveCount));
    ImGui::Text("Textures: %zu", m_textures.size());
    ImGui::Separator();
    ImGui::TextUnformatted("Texture Diagnostics");
    ImGui::Text("Normal Maps: %d / %zu", normalTextureCount, m_scene.materials.size());
    ImGui::Text("Normal Fallbacks: %d", normalFallbackCount);
    ImGui::Text("Normal 1x1 SRVs: %d", normalOnePixelCount);
    ImGui::End();
}

void BistroExteriorMeshShaderCullingD3D12::ResetLight()
{
    m_lightDirection[0] = -0.35f;
    m_lightDirection[1] = -0.8f;
    m_lightDirection[2] = 0.45f;
    m_lightColor[0] = 1.0f;
    m_lightColor[1] = 0.96f;
    m_lightColor[2] = 0.88f;
    m_lightIntensity = 4.0f;
}

void BistroExteriorMeshShaderCullingD3D12::ResetCameraView()
{
    m_camera.Reset(m_defaultCameraPosition, m_defaultCameraYaw, m_defaultCameraPitch);
}

void BistroExteriorMeshShaderCullingD3D12::ResetCameraSpeeds()
{
    m_baseMoveSpeed = 5.0f;
    m_fastMoveSpeed = 18.0f;
}

void BistroExteriorMeshShaderCullingD3D12::AllocateImGuiDescriptor(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* outCpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE* outGpuHandle)
{
    auto* sample = static_cast<BistroExteriorMeshShaderCullingD3D12*>(info->UserData);
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

void BistroExteriorMeshShaderCullingD3D12::FreeImGuiDescriptor(ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE)
{
}
