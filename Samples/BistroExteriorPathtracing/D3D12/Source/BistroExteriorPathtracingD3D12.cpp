#include "stdafx.h"
#include "BistroExteriorPathtracingD3D12.h"

#include "imgui.h"
#include "backends/imgui_impl_dx12.h"
#include "backends/imgui_impl_win32.h"
#include "..\..\Common\BistroTexture.h"

#include <DirectXTex.h>

#include <algorithm>
#include <stdexcept>
#include <utility>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace
{
    constexpr UINT ImGuiDescriptorCount = 64;
    constexpr DXGI_FORMAT BackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    constexpr DXGI_FORMAT AccumulationFormat = DXGI_FORMAT_R32G32B32A32_FLOAT;
    constexpr UINT RestirReservoirStride = sizeof(XMFLOAT4) * 2;
    const wchar_t* RayGenShaderName = L"RayGen";
    const wchar_t* MissShaderName = L"Miss";
    const wchar_t* ShadowMissShaderName = L"ShadowMiss";
    const wchar_t* ClosestHitShaderName = L"ClosestHit";
    const wchar_t* AnyHitShaderName = L"AnyHit";
    const wchar_t* ShadowAnyHitShaderName = L"ShadowAnyHit";
    const wchar_t* HitGroupName = L"HitGroup";
    const wchar_t* ShadowHitGroupName = L"ShadowHitGroup";

    XMFLOAT3 NormalizeFloat3(const float values[3])
    {
        XMVECTOR v = XMVector3Normalize(XMVectorSet(values[0], values[1], values[2], 0.0f));
        XMFLOAT3 result;
        XMStoreFloat3(&result, v);
        return result;
    }

    std::wstring PathtracingTierName(D3D12_RAYTRACING_TIER tier)
    {
        switch (static_cast<int>(tier))
        {
        case 10: return L"1.0";
        case 11: return L"1.1";
        case 12: return L"1.2";
        default:
            return tier == D3D12_RAYTRACING_TIER_NOT_SUPPORTED ? L"Not supported" : L"Unknown";
        }
    }

    bool UsesRestirReuse(BistroPathtracingMode mode)
    {
        return mode == BistroPathtracingMode::ReSTIR || mode == BistroPathtracingMode::ReSTIRDI;
    }

    const char* PathtracingModeName(BistroPathtracingMode mode)
    {
        switch (mode)
        {
        case BistroPathtracingMode::ReSTIR:
            return "ReSTIR GI";
        case BistroPathtracingMode::ReSTIRDI:
            return "ReSTIR DI";
        default:
            return "Path Tracing";
        }
    }
}

BistroExteriorPathtracingD3D12::BistroExteriorPathtracingD3D12(UINT width, UINT height, std::wstring name, BistroPathtracingMode mode) :
    DXSample(width, height, name),
    m_viewport(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)),
    m_scissorRect(0, 0, static_cast<LONG>(width), static_cast<LONG>(height)),
    m_mode(mode)
{
}

void BistroExteriorPathtracingD3D12::OnInit()
{
    LoadPipeline();
    LoadAssets();
    m_lastUpdate = std::chrono::steady_clock::now();
}

void BistroExteriorPathtracingD3D12::LoadPipeline()
{
    UINT dxgiFactoryFlags = 0;
#if defined(_DEBUG)
    ComPtr<ID3D12Debug> debugController;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
    {
        debugController->EnableDebugLayer();
        dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
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

    ComPtr<IDXGIAdapter1> hardwareAdapter;
    GetHardwareAdapter(factory.Get(), &hardwareAdapter);
    ComPtr<ID3D12Device2> baseDevice;
    ThrowIfFailed(D3D12CreateDevice(hardwareAdapter.Get(), D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&baseDevice)));
    ThrowIfFailed(baseDevice.As(&m_device));

    D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5{};
    ThrowIfFailed(m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5)));
    m_raytracingTier = options5.RaytracingTier;
    if (m_raytracingTier == D3D12_RAYTRACING_TIER_NOT_SUPPORTED)
    {
        throw std::runtime_error("This GPU/driver does not support DirectX Pathtracing. BistroExteriorPathtracingD3D12 has no raster fallback.");
    }

    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ThrowIfFailed(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)));

    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.BufferCount = FrameCount;
    swapChainDesc.Width = m_width;
    swapChainDesc.Height = m_height;
    swapChainDesc.Format = BackBufferFormat;
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
    m_descriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());
    for (UINT n = 0; n < FrameCount; ++n)
    {
        ThrowIfFailed(m_swapChain->GetBuffer(n, IID_PPV_ARGS(&m_renderTargets[n])));
        m_device->CreateRenderTargetView(m_renderTargets[n].Get(), nullptr, rtvHandle);
        rtvHandle.Offset(1, m_rtvDescriptorSize);
    }

    D3D12_DESCRIPTOR_HEAP_DESC imguiHeapDesc = {};
    imguiHeapDesc.NumDescriptors = ImGuiDescriptorCount;
    imguiHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    imguiHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(m_device->CreateDescriptorHeap(&imguiHeapDesc, IID_PPV_ARGS(&m_imguiDescriptorHeap)));
    m_imguiDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocator)));
    ThrowIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocator.Get(), nullptr, IID_PPV_ARGS(&m_commandList)));
    ThrowIfFailed(m_commandList->Close());

    ThrowIfFailed(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
    m_fenceValue = 1;
    m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (m_fenceEvent == nullptr)
    {
        ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
    }
}

void BistroExteriorPathtracingD3D12::LoadAssets()
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

    m_defaultCameraPosition = XMFLOAT3(-16.32f, 4.66f, -10.41f);
    m_defaultCameraYaw = XMConvertToRadians(18.1f);
    m_defaultCameraPitch = XMConvertToRadians(2.8f);
    ResetCameraView();
    ResetCameraSpeeds();

    ThrowIfFailed(m_commandAllocator->Reset());
    ThrowIfFailed(m_commandList->Reset(m_commandAllocator.Get(), nullptr));

    CreateDescriptorHeap();
    CreateOutputResources();
    CreateGlobalRootSignature();
    CreateSceneBuffers();
    CreateTextures();
    CreatePathtracingStateObject();
    CreateRestirReusePipeline();
    CreateDenoisePipeline();
    BuildAccelerationStructures();
    CreateShaderTables();

    ThrowIfFailed(m_commandList->Close());
    ID3D12CommandList* commandLists[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(_countof(commandLists), commandLists);
    WaitForPreviousFrame();
    m_uploadBuffers.clear();

    InitializeImGui();
}

void BistroExteriorPathtracingD3D12::CreateDescriptorHeap()
{
    m_descriptorCount = DescriptorTextureBase + static_cast<UINT>(m_scene.materials.size()) * TextureSlotCount + 1u;
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.NumDescriptors = m_descriptorCount;
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(m_device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_descriptorHeap)));
}

void BistroExteriorPathtracingD3D12::CreateOutputResources()
{
    D3D12_RESOURCE_DESC outputDesc = CD3DX12_RESOURCE_DESC::Tex2D(BackBufferFormat, m_width, m_height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    ThrowIfFailed(m_device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE, &outputDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&m_PathtracingOutput)));
    m_PathtracingOutput->SetName(L"PathtracingOutput");

    D3D12_RESOURCE_DESC accumulationDesc = CD3DX12_RESOURCE_DESC::Tex2D(AccumulationFormat, m_width, m_height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    ThrowIfFailed(m_device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE, &accumulationDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&m_accumulationOutput)));
    m_accumulationOutput->SetName(L"PathtracingAccumulation");
    ThrowIfFailed(m_device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE, &accumulationDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&m_denoiseAov0)));
    m_denoiseAov0->SetName(L"PathtracingDenoiseAov0");
    ThrowIfFailed(m_device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE, &accumulationDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&m_denoiseAov1)));
    m_denoiseAov1->SetName(L"PathtracingDenoiseAov1");

    D3D12_UNORDERED_ACCESS_VIEW_DESC outputUav = {};
    outputUav.Format = BackBufferFormat;
    outputUav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    m_device->CreateUnorderedAccessView(m_PathtracingOutput.Get(), nullptr, &outputUav, CpuDescriptor(DescriptorOutputUav));

    D3D12_UNORDERED_ACCESS_VIEW_DESC accumulationUav = {};
    accumulationUav.Format = AccumulationFormat;
    accumulationUav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    m_device->CreateUnorderedAccessView(m_accumulationOutput.Get(), nullptr, &accumulationUav, CpuDescriptor(DescriptorAccumulationUav));
    m_device->CreateUnorderedAccessView(m_denoiseAov0.Get(), nullptr, &accumulationUav, CpuDescriptor(DescriptorDenoiseAov0Uav));
    m_device->CreateUnorderedAccessView(m_denoiseAov1.Get(), nullptr, &accumulationUav, CpuDescriptor(DescriptorDenoiseAov1Uav));

    m_restirReservoirElementCount = (std::max)(1u, m_width * m_height);
    m_restirReservoirBufferSize = static_cast<UINT64>(m_restirReservoirElementCount) * RestirReservoirStride;
    m_restirReservoirCurrent = CreateUavBuffer(m_restirReservoirBufferSize, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, L"RestirReservoirCurrent");
    m_restirReservoirHistory = CreateUavBuffer(m_restirReservoirBufferSize, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, L"RestirReservoirHistory");
    m_restirReservoirSpatial = CreateUavBuffer(m_restirReservoirBufferSize, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, L"RestirReservoirSpatial");

    D3D12_UNORDERED_ACCESS_VIEW_DESC reservoirUav = {};
    reservoirUav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    reservoirUav.Buffer.NumElements = m_restirReservoirElementCount;
    reservoirUav.Buffer.StructureByteStride = RestirReservoirStride;
    m_device->CreateUnorderedAccessView(m_restirReservoirCurrent.Get(), nullptr, &reservoirUav, CpuDescriptor(DescriptorRestirCurrentUav));
    m_device->CreateUnorderedAccessView(m_restirReservoirHistory.Get(), nullptr, &reservoirUav, CpuDescriptor(DescriptorRestirHistoryUav));
    m_device->CreateUnorderedAccessView(m_restirReservoirSpatial.Get(), nullptr, &reservoirUav, CpuDescriptor(DescriptorRestirSpatialUav));
}

void BistroExteriorPathtracingD3D12::CreateGlobalRootSignature()
{
    CD3DX12_DESCRIPTOR_RANGE uavRange;
    uavRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 7, 0, 0);
    CD3DX12_DESCRIPTOR_RANGE sceneBufferRange;
    sceneBufferRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 5, 1, 0);
    CD3DX12_DESCRIPTOR_RANGE textureRange;
    textureRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, static_cast<UINT>(m_scene.materials.size()) * TextureSlotCount + 1u, 0, 1);

    CD3DX12_ROOT_PARAMETER rootParameters[RootParameterCount];
    rootParameters[RootOutputTable].InitAsDescriptorTable(1, &uavRange, D3D12_SHADER_VISIBILITY_ALL);
    rootParameters[RootAccelerationStructure].InitAsShaderResourceView(0, 0, D3D12_SHADER_VISIBILITY_ALL);
    rootParameters[RootSceneConstants].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);
    rootParameters[RootSceneBuffers].InitAsDescriptorTable(1, &sceneBufferRange, D3D12_SHADER_VISIBILITY_ALL);
    rootParameters[RootTextureTable].InitAsDescriptorTable(1, &textureRange, D3D12_SHADER_VISIBILITY_ALL);

    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_ANISOTROPIC;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.MaxAnisotropy = 8;
    sampler.ShaderRegister = 0;
    sampler.RegisterSpace = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    sampler.MinLOD = 0.0f;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;

    CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc;
    rootSignatureDesc.Init(_countof(rootParameters), rootParameters, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_NONE);

    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> error;
    HRESULT hr = D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error);
    if (FAILED(hr) && error)
    {
        OutputDebugStringA(static_cast<const char*>(error->GetBufferPointer()));
    }
    ThrowIfFailed(hr);
    ThrowIfFailed(m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_globalRootSignature)));
}

void BistroExteriorPathtracingD3D12::CreateSceneBuffers()
{
    m_vertexBuffer = CreateDefaultBuffer(m_scene.vertices.data(), m_scene.vertices.size() * sizeof(Bistro::Vertex), D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, L"RtVertices");
    m_indexBuffer = CreateDefaultBuffer(m_scene.indices.data(), m_scene.indices.size() * sizeof(uint32_t), D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, L"RtIndices");
    m_geometryBuffer = CreateDefaultBuffer(m_geometryRecords.data(), m_geometryRecords.size() * sizeof(Bistro::RtGeometryRecord), D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, L"RtGeometryRecords");
    m_lightBuffer = CreateDefaultBuffer(m_lights.data(), m_lights.size() * sizeof(Bistro::RtLight), D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, L"RtLights");

    D3D12_SHADER_RESOURCE_VIEW_DESC vertexSrv = {};
    vertexSrv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    vertexSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    vertexSrv.Buffer.NumElements = static_cast<UINT>(m_scene.vertices.size());
    vertexSrv.Buffer.StructureByteStride = sizeof(Bistro::Vertex);
    m_device->CreateShaderResourceView(m_vertexBuffer.Get(), &vertexSrv, CpuDescriptor(DescriptorVertexBuffer));

    D3D12_SHADER_RESOURCE_VIEW_DESC indexSrv = {};
    indexSrv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    indexSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    indexSrv.Buffer.NumElements = static_cast<UINT>(m_scene.indices.size());
    indexSrv.Buffer.StructureByteStride = sizeof(uint32_t);
    m_device->CreateShaderResourceView(m_indexBuffer.Get(), &indexSrv, CpuDescriptor(DescriptorIndexBuffer));

    D3D12_SHADER_RESOURCE_VIEW_DESC geometrySrv = {};
    geometrySrv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    geometrySrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    geometrySrv.Buffer.NumElements = static_cast<UINT>(m_geometryRecords.size());
    geometrySrv.Buffer.StructureByteStride = sizeof(Bistro::RtGeometryRecord);
    m_device->CreateShaderResourceView(m_geometryBuffer.Get(), &geometrySrv, CpuDescriptor(DescriptorGeometryBuffer));

    D3D12_SHADER_RESOURCE_VIEW_DESC lightSrv = {};
    lightSrv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    lightSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    lightSrv.Buffer.NumElements = static_cast<UINT>(m_lights.size());
    lightSrv.Buffer.StructureByteStride = sizeof(Bistro::RtLight);
    m_device->CreateShaderResourceView(m_lightBuffer.Get(), &lightSrv, CpuDescriptor(DescriptorLightBuffer));

    const UINT constantSize = CalculateConstantBufferByteSize(sizeof(SceneConstantBuffer));
    ThrowIfFailed(m_device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD), D3D12_HEAP_FLAG_NONE, &CD3DX12_RESOURCE_DESC::Buffer(constantSize), D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_sceneConstantBuffer)));
    ThrowIfFailed(m_sceneConstantBuffer->Map(0, nullptr, reinterpret_cast<void**>(&m_mappedSceneConstants)));
}

UINT BistroExteriorPathtracingD3D12::CreateTextureResource(const std::wstring& path, bool srgb, const uint8_t fallback[4], std::map<std::wstring, UINT>& cache)
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
    texture.format = image.format;
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
    m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(texture.resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));

    const UINT index = static_cast<UINT>(m_textures.size());
    m_textures.push_back(texture);
    cache[key] = index;
    return index;
}

void BistroExteriorPathtracingD3D12::CreateTextures()
{
    const uint8_t white[] = { 255, 255, 255, 255 };
    const uint8_t normal[] = { 128, 128, 255, 255 };
    const uint8_t black[] = { 0, 0, 0, 255 };
    const uint8_t environmentFallback[] = { 35, 68, 110, 255 };
    std::map<std::wstring, UINT> cache;
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

        for (UINT slot = 0; slot < TextureSlotCount; ++slot)
        {
            const GpuTexture& texture = m_textures[indices[slot]];
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Format = texture.format;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MipLevels = texture.mipLevels;
            srvDesc.Texture2D.MostDetailedMip = 0;
            m_device->CreateShaderResourceView(texture.resource.Get(), &srvDesc, CpuDescriptor(DescriptorTextureBase + static_cast<UINT>(materialIndex) * TextureSlotCount + slot));
        }

        Bistro::RtMaterial rtMaterial{};
        rtMaterial.baseColorFactor = material.baseColorFactor;
        rtMaterial.emissiveFactor = material.emissiveFactor;
        rtMaterial.textureBaseIndex = static_cast<uint32_t>(materialIndex * TextureSlotCount);
        rtMaterial.alphaMasked = material.alphaMasked ? 1u : 0u;
        rtMaterial.alphaCutoff = material.alphaCutoff;
        m_rtMaterials[materialIndex] = rtMaterial;
    }

    const UINT environmentTexture = CreateTextureResource(m_environmentTexturePath, false, environmentFallback, cache);
    m_environmentDescriptorIndex = static_cast<UINT>(m_scene.materials.size()) * TextureSlotCount;
    const GpuTexture& environment = m_textures[environmentTexture];
    D3D12_SHADER_RESOURCE_VIEW_DESC environmentSrv = {};
    environmentSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    environmentSrv.Format = environment.format;
    environmentSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    environmentSrv.Texture2D.MipLevels = environment.mipLevels;
    environmentSrv.Texture2D.MostDetailedMip = 0;
    m_device->CreateShaderResourceView(environment.resource.Get(), &environmentSrv, CpuDescriptor(DescriptorTextureBase + m_environmentDescriptorIndex));

    m_materialBuffer = CreateDefaultBuffer(m_rtMaterials.data(), m_rtMaterials.size() * sizeof(Bistro::RtMaterial), D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, L"RtMaterials");
    D3D12_SHADER_RESOURCE_VIEW_DESC materialSrv = {};
    materialSrv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    materialSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    materialSrv.Buffer.NumElements = static_cast<UINT>(m_rtMaterials.size());
    materialSrv.Buffer.StructureByteStride = sizeof(Bistro::RtMaterial);
    m_device->CreateShaderResourceView(m_materialBuffer.Get(), &materialSrv, CpuDescriptor(DescriptorMaterialBuffer));
}

void BistroExteriorPathtracingD3D12::CreatePathtracingStateObject()
{
    const std::wstring exeDir = GetAssetFullPath(L"");
    byte* shaderData = nullptr;
    UINT shaderSize = 0;
    ThrowIfFailed(ReadDataFromFile((exeDir + ShaderFileName()).c_str(), &shaderData, &shaderSize));
    std::vector<UINT8> shader(shaderData, shaderData + shaderSize);
    free(shaderData);

    CD3DX12_STATE_OBJECT_DESC pipeline(D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE);
    auto library = pipeline.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
    D3D12_SHADER_BYTECODE shaderBytecode = CD3DX12_SHADER_BYTECODE(shader.data(), shader.size());
    library->SetDXILLibrary(&shaderBytecode);
    library->DefineExport(RayGenShaderName);
    library->DefineExport(MissShaderName);
    library->DefineExport(ShadowMissShaderName);
    library->DefineExport(ClosestHitShaderName);
    library->DefineExport(AnyHitShaderName);
    library->DefineExport(ShadowAnyHitShaderName);

    auto hitGroup = pipeline.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
    hitGroup->SetHitGroupExport(HitGroupName);
    hitGroup->SetClosestHitShaderImport(ClosestHitShaderName);
    hitGroup->SetAnyHitShaderImport(AnyHitShaderName);
    hitGroup->SetHitGroupType(D3D12_HIT_GROUP_TYPE_TRIANGLES);

    auto shadowHitGroup = pipeline.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
    shadowHitGroup->SetHitGroupExport(ShadowHitGroupName);
    shadowHitGroup->SetAnyHitShaderImport(ShadowAnyHitShaderName);
    shadowHitGroup->SetHitGroupType(D3D12_HIT_GROUP_TYPE_TRIANGLES);

    auto shaderConfig = pipeline.CreateSubobject<CD3DX12_RAYTRACING_SHADER_CONFIG_SUBOBJECT>();
    shaderConfig->Config(192, sizeof(XMFLOAT2));

    auto globalRootSignature = pipeline.CreateSubobject<CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT>();
    globalRootSignature->SetRootSignature(m_globalRootSignature.Get());

    auto pipelineConfig = pipeline.CreateSubobject<CD3DX12_RAYTRACING_PIPELINE_CONFIG_SUBOBJECT>();
    pipelineConfig->Config(MaxTraceRecursionDepth());

    ThrowIfFailed(m_device->CreateStateObject(pipeline, IID_PPV_ARGS(&m_stateObject)));
    ThrowIfFailed(m_stateObject.As(&m_stateObjectProperties));
}

void BistroExteriorPathtracingD3D12::CreateRestirReusePipeline()
{
    if (!UsesRestirReuse(m_mode))
    {
        return;
    }

    const std::wstring exeDir = GetAssetFullPath(L"");
    byte* shaderData = nullptr;
    UINT shaderSize = 0;
    ThrowIfFailed(ReadDataFromFile((exeDir + RestirReuseShaderFileName()).c_str(), &shaderData, &shaderSize));
    std::vector<UINT8> shader(shaderData, shaderData + shaderSize);
    free(shaderData);

    D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
    desc.pRootSignature = m_globalRootSignature.Get();
    desc.CS = CD3DX12_SHADER_BYTECODE(shader.data(), shader.size());
    ThrowIfFailed(m_device->CreateComputePipelineState(&desc, IID_PPV_ARGS(&m_restirReusePipeline)));
}

void BistroExteriorPathtracingD3D12::CreateDenoisePipeline()
{
    const std::wstring exeDir = GetAssetFullPath(L"");
    byte* shaderData = nullptr;
    UINT shaderSize = 0;
    ThrowIfFailed(ReadDataFromFile((exeDir + DenoiseShaderFileName()).c_str(), &shaderData, &shaderSize));
    std::vector<UINT8> shader(shaderData, shaderData + shaderSize);
    free(shaderData);

    D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
    desc.pRootSignature = m_globalRootSignature.Get();
    desc.CS = CD3DX12_SHADER_BYTECODE(shader.data(), shader.size());
    ThrowIfFailed(m_device->CreateComputePipelineState(&desc, IID_PPV_ARGS(&m_denoisePipeline)));
}

void BistroExteriorPathtracingD3D12::BuildAccelerationStructures()
{
    std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geometryDescs;
    geometryDescs.reserve(m_scene.draws.size());
    for (const Bistro::DrawItem& draw : m_scene.draws)
    {
        D3D12_RAYTRACING_GEOMETRY_DESC desc = {};
        desc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
        desc.Triangles.VertexBuffer.StartAddress = m_vertexBuffer->GetGPUVirtualAddress();
        desc.Triangles.VertexBuffer.StrideInBytes = sizeof(Bistro::Vertex);
        desc.Triangles.VertexCount = static_cast<UINT>(m_scene.vertices.size());
        desc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
        desc.Triangles.IndexBuffer = m_indexBuffer->GetGPUVirtualAddress() + draw.startIndex * sizeof(uint32_t);
        desc.Triangles.IndexCount = draw.indexCount;
        desc.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
        desc.Flags = m_scene.materials[draw.materialIndex].alphaMasked ? D3D12_RAYTRACING_GEOMETRY_FLAG_NONE : D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
        geometryDescs.push_back(desc);
    }

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS bottomInputs = {};
    bottomInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    bottomInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    bottomInputs.NumDescs = static_cast<UINT>(geometryDescs.size());
    bottomInputs.pGeometryDescs = geometryDescs.data();
    bottomInputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO bottomInfo = {};
    m_device->GetRaytracingAccelerationStructurePrebuildInfo(&bottomInputs, &bottomInfo);
    if (bottomInfo.ResultDataMaxSizeInBytes == 0)
    {
        throw std::runtime_error("Failed to query BLAS size.");
    }

    m_bottomLevelAs.scratch = CreateUavBuffer(bottomInfo.ScratchDataSizeInBytes, D3D12_RESOURCE_STATE_COMMON, L"BLAS Scratch");
    m_bottomLevelAs.result = CreateUavBuffer(bottomInfo.ResultDataMaxSizeInBytes, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, L"BLAS");

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC bottomBuild = {};
    bottomBuild.Inputs = bottomInputs;
    bottomBuild.ScratchAccelerationStructureData = m_bottomLevelAs.scratch->GetGPUVirtualAddress();
    bottomBuild.DestAccelerationStructureData = m_bottomLevelAs.result->GetGPUVirtualAddress();
    m_commandList->BuildRaytracingAccelerationStructure(&bottomBuild, 0, nullptr);
    m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::UAV(m_bottomLevelAs.result.Get()));

    D3D12_RAYTRACING_INSTANCE_DESC instanceDesc = {};
    instanceDesc.Transform[0][0] = 1.0f;
    instanceDesc.Transform[1][1] = 1.0f;
    instanceDesc.Transform[2][2] = 1.0f;
    instanceDesc.InstanceMask = 0xff;
    instanceDesc.AccelerationStructure = m_bottomLevelAs.result->GetGPUVirtualAddress();
    m_topLevelAs.instanceDesc = CreateUploadBuffer(&instanceDesc, sizeof(instanceDesc), L"TLAS Instance");

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS topInputs = {};
    topInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    topInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    topInputs.NumDescs = 1;
    topInputs.InstanceDescs = m_topLevelAs.instanceDesc->GetGPUVirtualAddress();
    topInputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO topInfo = {};
    m_device->GetRaytracingAccelerationStructurePrebuildInfo(&topInputs, &topInfo);
    if (topInfo.ResultDataMaxSizeInBytes == 0)
    {
        throw std::runtime_error("Failed to query TLAS size.");
    }
    m_topLevelAs.scratch = CreateUavBuffer(topInfo.ScratchDataSizeInBytes, D3D12_RESOURCE_STATE_COMMON, L"TLAS Scratch");
    m_topLevelAs.result = CreateUavBuffer(topInfo.ResultDataMaxSizeInBytes, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, L"TLAS");

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC topBuild = {};
    topBuild.Inputs = topInputs;
    topBuild.ScratchAccelerationStructureData = m_topLevelAs.scratch->GetGPUVirtualAddress();
    topBuild.DestAccelerationStructureData = m_topLevelAs.result->GetGPUVirtualAddress();
    m_commandList->BuildRaytracingAccelerationStructure(&topBuild, 0, nullptr);
    m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::UAV(m_topLevelAs.result.Get()));
}

void BistroExteriorPathtracingD3D12::CreateShaderTables()
{
    const UINT shaderIdentifierSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    const UINT recordSize = Align(shaderIdentifierSize, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);

    auto createTable = [&](const wchar_t* name, std::initializer_list<const wchar_t*> exports, ShaderTableInfo& table)
    {
        table.recordSize = recordSize;
        table.recordCount = static_cast<UINT>(exports.size());
        const UINT64 bufferSize = static_cast<UINT64>(recordSize) * table.recordCount;
        table.resource = CreateUploadBuffer(nullptr, bufferSize, name);
        UINT8* mapped = nullptr;
        ThrowIfFailed(table.resource->Map(0, nullptr, reinterpret_cast<void**>(&mapped)));
        UINT index = 0;
        for (const wchar_t* exportName : exports)
        {
            void* identifier = m_stateObjectProperties->GetShaderIdentifier(exportName);
            if (!identifier)
            {
                throw std::runtime_error("Failed to resolve a Path Tracing shader identifier.");
            }
            memcpy(mapped + index * recordSize, identifier, shaderIdentifierSize);
            ++index;
        }
        table.resource->Unmap(0, nullptr);
    };

    createTable(L"RayGen Shader Table", { RayGenShaderName }, m_rayGenTable);
    createTable(L"Miss Shader Table", { MissShaderName, ShadowMissShaderName }, m_missTable);
    createTable(L"HitGroup Shader Table", { HitGroupName, ShadowHitGroupName }, m_hitGroupTable);
}

void BistroExteriorPathtracingD3D12::OnUpdate()
{
    const auto now = std::chrono::steady_clock::now();
    const float deltaSeconds = std::chrono::duration<float>(now - m_lastUpdate).count();
    m_lastUpdate = now;
    m_camera.Update(deltaSeconds);
    UpdateConstantBuffer(deltaSeconds);
}

void BistroExteriorPathtracingD3D12::UpdateConstantBuffer(float)
{
    if (HasAccumulationStateChanged())
    {
        ResetAccumulation();
    }

    const float aspectRatio = static_cast<float>(m_width) / static_cast<float>(m_height);
    XMMATRIX view = m_camera.GetViewMatrix();
    XMMATRIX projection = XMMatrixPerspectiveFovLH(XMConvertToRadians(60.0f), aspectRatio, 0.1f, 10000.0f);
    XMMATRIX inverseViewProjection = XMMatrixInverse(nullptr, view * projection);

    SceneConstantBuffer constants{};
    XMStoreFloat4x4(&constants.inverseViewProjection, inverseViewProjection);
    XMFLOAT3 cameraPosition = m_camera.GetPosition();
    constants.cameraPosition = XMFLOAT4(cameraPosition.x, cameraPosition.y, cameraPosition.z, 1.0f);
    XMFLOAT3 lightDirection = NormalizeFloat3(m_lightDirection);
    constants.lightDirection = XMFLOAT4(lightDirection.x, lightDirection.y, lightDirection.z, 0.0f);
    constants.lightColor = XMFLOAT4(m_lightColor[0], m_lightColor[1], m_lightColor[2], m_lightIntensity);
    constants.debugOptions = XMFLOAT4(static_cast<float>(m_debugViewMode), m_debugNormalMapYFlip ? 1.0f : 0.0f, m_shadowEnabled ? 1.0f : 0.0f, m_skyNeeEnabled ? 1.0f : 0.0f);
    constants.skyColor = XMFLOAT4(m_skyColor[0], m_skyColor[1], m_skyColor[2], m_skyIntensity);
    constants.skyHorizonColor = XMFLOAT4(m_skyHorizonColor[0], m_skyHorizonColor[1], m_skyHorizonColor[2], 0.0f);
    constants.skyZenithColor = XMFLOAT4(m_skyZenithColor[0], m_skyZenithColor[1], m_skyZenithColor[2], 0.0f);
    constants.skyGroundColor = XMFLOAT4(m_skyGroundColor[0], m_skyGroundColor[1], m_skyGroundColor[2], 0.0f);
    constants.skyOptions = XMFLOAT4(m_sunIntensity, m_sunAngularRadius, m_skyGroundBlend, m_skyEnabled ? 1.0f : 0.0f);
    constants.rayOptions = XMFLOAT4(m_rayTMin, m_rayTMax, static_cast<float>(m_width), static_cast<float>(m_height));
    constants.frameOptions = XMFLOAT4(static_cast<float>(m_accumulatedFrames), static_cast<float>(m_maxAccumulatedFrames), m_freezeAccumulation ? 1.0f : 0.0f, static_cast<float>(m_frameCounter));
    constants.giOptions = XMFLOAT4(static_cast<float>(m_giSamplesPerFrame), m_giRadianceClamp, m_giTemporalClampScale, m_giTemporalClampMin);
    constants.pathOptions = XMFLOAT4(static_cast<float>(m_maxPathBounces), static_cast<float>(m_minPathBounces), static_cast<float>(m_restirCandidateSamples), UsesRestirReuse(m_mode) ? 1.0f : 0.0f);
    constants.restirOptions = XMFLOAT4(m_restirTemporalReuse ? 1.0f : 0.0f, static_cast<float>(m_restirSpatialReusePasses), static_cast<float>(m_restirSpatialRadius), m_restirMClamp);
    constants.lightOptions = XMFLOAT4(static_cast<float>(m_activeLightCount), m_emissiveLightsEnabled ? m_emissiveLightIntensity : 0.0f, m_proceduralLightsEnabled ? m_proceduralLightIntensity : 0.0f, static_cast<float>(m_environmentDescriptorIndex));
    constants.environmentOptions = XMFLOAT4(m_environmentMapEnabled ? 1.0f : 0.0f, m_environmentIntensity, m_environmentRotation, 0.0f);
    constants.denoiseOptions = XMFLOAT4(m_denoiserEnabled ? 1.0f : 0.0f, static_cast<float>(m_denoiserSpatialIterations), m_denoiserNormalSigma, m_denoiserDepthSigma);
    constants.denoiseOptions2 = XMFLOAT4(m_denoiserLuminanceSigma, m_denoiserAlbedoSigma, m_denoiserStrength, 0.0f);
    memcpy(m_mappedSceneConstants, &constants, sizeof(constants));

    if (!m_freezeAccumulation)
    {
        m_accumulatedFrames = (std::min)(m_accumulatedFrames + 1u, static_cast<uint32_t>((std::max)(m_maxAccumulatedFrames, 1)));
        ++m_frameCounter;
    }
}

void BistroExteriorPathtracingD3D12::OnRender()
{
    BuildUI();
    PopulateCommandList();
    ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

    const UINT syncInterval = m_vsyncEnabled ? 1 : 0;
    const UINT presentFlags = (!m_vsyncEnabled && m_tearingSupported) ? DXGI_PRESENT_ALLOW_TEARING : 0;
    ThrowIfFailed(m_swapChain->Present(syncInterval, presentFlags));
    WaitForPreviousFrame();
}

void BistroExteriorPathtracingD3D12::PopulateCommandList()
{
    ThrowIfFailed(m_commandAllocator->Reset());
    ThrowIfFailed(m_commandList->Reset(m_commandAllocator.Get(), nullptr));

    ID3D12DescriptorHeap* descriptorHeaps[] = { m_descriptorHeap.Get() };
    m_commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
    DispatchRays();
    RunRestirReusePass();
    RunDenoisePass();
    CopyOutputToBackBuffer();

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, m_rtvDescriptorSize);
    m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_RENDER_TARGET));
    m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

    ID3D12DescriptorHeap* imguiHeaps[] = { m_imguiDescriptorHeap.Get() };
    m_commandList->SetDescriptorHeaps(_countof(imguiHeaps), imguiHeaps);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), m_commandList.Get());

    m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));
    ThrowIfFailed(m_commandList->Close());
}

void BistroExteriorPathtracingD3D12::DispatchRays()
{
    m_commandList->SetComputeRootSignature(m_globalRootSignature.Get());
    m_commandList->SetComputeRootDescriptorTable(RootOutputTable, GpuDescriptor(DescriptorOutputUav));
    m_commandList->SetComputeRootShaderResourceView(RootAccelerationStructure, m_topLevelAs.result->GetGPUVirtualAddress());
    m_commandList->SetComputeRootConstantBufferView(RootSceneConstants, m_sceneConstantBuffer->GetGPUVirtualAddress());
    m_commandList->SetComputeRootDescriptorTable(RootSceneBuffers, GpuDescriptor(DescriptorVertexBuffer));
    m_commandList->SetComputeRootDescriptorTable(RootTextureTable, GpuDescriptor(DescriptorTextureBase));
    m_commandList->SetPipelineState1(m_stateObject.Get());

    D3D12_DISPATCH_RAYS_DESC dispatchDesc = {};
    dispatchDesc.RayGenerationShaderRecord.StartAddress = m_rayGenTable.resource->GetGPUVirtualAddress();
    dispatchDesc.RayGenerationShaderRecord.SizeInBytes = m_rayGenTable.recordSize;
    dispatchDesc.MissShaderTable.StartAddress = m_missTable.resource->GetGPUVirtualAddress();
    dispatchDesc.MissShaderTable.SizeInBytes = m_missTable.recordSize * m_missTable.recordCount;
    dispatchDesc.MissShaderTable.StrideInBytes = m_missTable.recordSize;
    dispatchDesc.HitGroupTable.StartAddress = m_hitGroupTable.resource->GetGPUVirtualAddress();
    dispatchDesc.HitGroupTable.SizeInBytes = m_hitGroupTable.recordSize * m_hitGroupTable.recordCount;
    dispatchDesc.HitGroupTable.StrideInBytes = m_hitGroupTable.recordSize;
    dispatchDesc.Width = m_width;
    dispatchDesc.Height = m_height;
    dispatchDesc.Depth = 1;
    m_commandList->DispatchRays(&dispatchDesc);
    D3D12_RESOURCE_BARRIER uavBarriers[] =
    {
        CD3DX12_RESOURCE_BARRIER::UAV(m_PathtracingOutput.Get()),
        CD3DX12_RESOURCE_BARRIER::UAV(m_accumulationOutput.Get()),
        CD3DX12_RESOURCE_BARRIER::UAV(m_denoiseAov0.Get()),
        CD3DX12_RESOURCE_BARRIER::UAV(m_denoiseAov1.Get()),
        CD3DX12_RESOURCE_BARRIER::UAV(m_restirReservoirCurrent.Get())
    };
    m_commandList->ResourceBarrier(_countof(uavBarriers), uavBarriers);
}

void BistroExteriorPathtracingD3D12::RunRestirReusePass()
{
    if (!UsesRestirReuse(m_mode) || !m_restirReusePipeline)
    {
        return;
    }

    m_commandList->SetComputeRootSignature(m_globalRootSignature.Get());
    m_commandList->SetComputeRootDescriptorTable(RootOutputTable, GpuDescriptor(DescriptorOutputUav));
    m_commandList->SetComputeRootShaderResourceView(RootAccelerationStructure, m_topLevelAs.result->GetGPUVirtualAddress());
    m_commandList->SetComputeRootConstantBufferView(RootSceneConstants, m_sceneConstantBuffer->GetGPUVirtualAddress());
    m_commandList->SetComputeRootDescriptorTable(RootSceneBuffers, GpuDescriptor(DescriptorVertexBuffer));
    m_commandList->SetComputeRootDescriptorTable(RootTextureTable, GpuDescriptor(DescriptorTextureBase));
    m_commandList->SetPipelineState(m_restirReusePipeline.Get());
    m_commandList->Dispatch((m_width + 7) / 8, (m_height + 7) / 8, 1);

    m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::UAV(m_restirReservoirSpatial.Get()));
    D3D12_RESOURCE_BARRIER toCopy[] =
    {
        CD3DX12_RESOURCE_BARRIER::Transition(m_restirReservoirSpatial.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE),
        CD3DX12_RESOURCE_BARRIER::Transition(m_restirReservoirHistory.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST)
    };
    m_commandList->ResourceBarrier(_countof(toCopy), toCopy);
    m_commandList->CopyBufferRegion(m_restirReservoirHistory.Get(), 0, m_restirReservoirSpatial.Get(), 0, m_restirReservoirBufferSize);

    D3D12_RESOURCE_BARRIER afterCopy[] =
    {
        CD3DX12_RESOURCE_BARRIER::Transition(m_restirReservoirSpatial.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
        CD3DX12_RESOURCE_BARRIER::Transition(m_restirReservoirHistory.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
    };
    m_commandList->ResourceBarrier(_countof(afterCopy), afterCopy);
}

void BistroExteriorPathtracingD3D12::RunDenoisePass()
{
    if (!m_denoiserEnabled || m_debugViewMode != 0 || !m_denoisePipeline)
    {
        return;
    }

    m_commandList->SetComputeRootSignature(m_globalRootSignature.Get());
    m_commandList->SetComputeRootDescriptorTable(RootOutputTable, GpuDescriptor(DescriptorOutputUav));
    m_commandList->SetComputeRootShaderResourceView(RootAccelerationStructure, m_topLevelAs.result->GetGPUVirtualAddress());
    m_commandList->SetComputeRootConstantBufferView(RootSceneConstants, m_sceneConstantBuffer->GetGPUVirtualAddress());
    m_commandList->SetComputeRootDescriptorTable(RootSceneBuffers, GpuDescriptor(DescriptorVertexBuffer));
    m_commandList->SetComputeRootDescriptorTable(RootTextureTable, GpuDescriptor(DescriptorTextureBase));
    m_commandList->SetPipelineState(m_denoisePipeline.Get());
    m_commandList->Dispatch((m_width + 7) / 8, (m_height + 7) / 8, 1);
    m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::UAV(m_PathtracingOutput.Get()));
}

void BistroExteriorPathtracingD3D12::CopyOutputToBackBuffer()
{
    D3D12_RESOURCE_BARRIER barriers[] =
    {
        CD3DX12_RESOURCE_BARRIER::Transition(m_PathtracingOutput.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE),
        CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST)
    };
    m_commandList->ResourceBarrier(_countof(barriers), barriers);
    m_commandList->CopyResource(m_renderTargets[m_frameIndex].Get(), m_PathtracingOutput.Get());
    m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_PathtracingOutput.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
}

void BistroExteriorPathtracingD3D12::BuildUI()
{
    ImGui_ImplDX12_NewFrame();
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
        "Reservoir Weight", "Temporal Reuse", "Spatial Reuse"
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
        ImGui::TextUnformatted(m_mode == BistroPathtracingMode::ReSTIRDI ? "ReSTIR DI" : "ReSTIR GI");
        if (ImGui::Checkbox("Temporal Reuse", &m_restirTemporalReuse)) ResetAccumulation();
        if (ImGui::SliderInt("Spatial Reuse Passes", &m_restirSpatialReusePasses, 0, 4)) ResetAccumulation();
        if (ImGui::SliderInt("Spatial Radius", &m_restirSpatialRadius, 1, 64)) ResetAccumulation();
        if (ImGui::SliderInt("Candidate Samples / Pixel", &m_restirCandidateSamples, 1, 4)) ResetAccumulation();
        if (ImGui::SliderFloat("Reservoir M Clamp", &m_restirMClamp, 1.0f, 64.0f, "%.1f")) ResetAccumulation();
        if (ImGui::Button("Reset Reservoirs")) ResetAccumulation();
    }
    ImGui::PopItemWidth();
    ImGui::End();

    BuildRendererStatsUI();
    ImGui::Render();
}

void BistroExteriorPathtracingD3D12::BuildRendererStatsUI()
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
    ImGui::SetNextWindowSize(ImVec2(340.0f, 330.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Renderer Stats");
    ImGui::Text("API: Direct3D 12 DXR");
    ImGui::Text("DXR Tier: %ls", PathtracingTierName(m_raytracingTier).c_str());
    ImGui::Text("FPS: %.1f", fps);
    ImGui::Text("Frame Time: %.3f ms", frameTimeMs);
    ImGui::Checkbox("VSync", &m_vsyncEnabled);
    ImGui::Text("Tearing: %s", m_tearingSupported ? "Supported" : "Unsupported");
    ImGui::Separator();
    ImGui::Text("Materials: %zu", m_scene.materials.size());
    ImGui::Text("Textures: %zu", m_textures.size());
    ImGui::Text("Vertices: %zu", m_scene.vertices.size());
    ImGui::Text("Indices: %zu", m_scene.indices.size());
    ImGui::Text("Submitted Indices: %llu", static_cast<unsigned long long>(submittedIndexCount));
    ImGui::Text("Primitives: %llu", static_cast<unsigned long long>(primitiveCount));
    ImGui::Text("BLAS Geometries: %zu", m_geometryRecords.size());
    ImGui::Text("TLAS Instances: 1");
    ImGui::Text("SBT Records: %u", m_rayGenTable.recordCount + m_missTable.recordCount + m_hitGroupTable.recordCount);
    ImGui::Text("Light List: %u", m_activeLightCount);
    ImGui::Text("Emissive Tri Lights: %u", m_emissiveTriangleLightCount);
    ImGui::Text("Procedural Area Lights: %u", m_proceduralAreaLightCount);
    ImGui::Text("Environment: %s", m_environmentTexturePath.empty() ? "Procedural Sky" : "Texture");
    ImGui::Text("Output: %ux%u", m_width, m_height);
    ImGui::Text("Accumulated Samples: %u", m_accumulatedFrames);
    ImGui::Text("Mode: %s", PathtracingModeName(m_mode));
    ImGui::Text("Denoiser: %s (%d pass%s)", m_denoiserEnabled ? "On" : "Off", m_denoiserSpatialIterations, m_denoiserSpatialIterations == 1 ? "" : "es");
    ImGui::End();
}

void BistroExteriorPathtracingD3D12::OnKeyDown(UINT8 key)
{
    if (key == VK_SPACE)
    {
        ResetCameraView();
        ResetAccumulation();
    }
}

void BistroExteriorPathtracingD3D12::OnWindowMessage(UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_RBUTTONDOWN || message == WM_RBUTTONUP)
    {
        m_camera.OnMouseButton(message, wParam);
    }
    if (message == WM_MOUSEMOVE)
    {
        m_camera.OnMouseMove(lParam);
    }
}

void BistroExteriorPathtracingD3D12::OnDestroy()
{
    ShutdownImGui();
    WaitForPreviousFrame();
    if (m_sceneConstantBuffer && m_mappedSceneConstants)
    {
        m_sceneConstantBuffer->Unmap(0, nullptr);
        m_mappedSceneConstants = nullptr;
    }
    CloseHandle(m_fenceEvent);
}

void BistroExteriorPathtracingD3D12::WaitForPreviousFrame()
{
    const UINT64 fence = m_fenceValue;
    ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), fence));
    ++m_fenceValue;
    if (m_fence->GetCompletedValue() < fence)
    {
        ThrowIfFailed(m_fence->SetEventOnCompletion(fence, m_fenceEvent));
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
}

void BistroExteriorPathtracingD3D12::InitializeImGui()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(Win32Application::GetHwnd());

    ImGui_ImplDX12_InitInfo initInfo = {};
    initInfo.Device = m_device.Get();
    initInfo.CommandQueue = m_commandQueue.Get();
    initInfo.NumFramesInFlight = FrameCount;
    initInfo.RTVFormat = BackBufferFormat;
    initInfo.DSVFormat = DXGI_FORMAT_UNKNOWN;
    initInfo.SrvDescriptorHeap = m_imguiDescriptorHeap.Get();
    initInfo.SrvDescriptorAllocFn = AllocateImGuiDescriptor;
    initInfo.SrvDescriptorFreeFn = FreeImGuiDescriptor;
    initInfo.UserData = this;
    ImGui_ImplDX12_Init(&initInfo);
}

void BistroExteriorPathtracingD3D12::ShutdownImGui()
{
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}

void BistroExteriorPathtracingD3D12::ResetLight()
{
    m_lightDirection[0] = -0.35f;
    m_lightDirection[1] = -0.8f;
    m_lightDirection[2] = 0.45f;
    m_lightColor[0] = 1.0f;
    m_lightColor[1] = 0.96f;
    m_lightColor[2] = 0.88f;
    m_lightIntensity = 4.0f;
}

void BistroExteriorPathtracingD3D12::ResetCameraView()
{
    m_camera.Reset(m_defaultCameraPosition, m_defaultCameraYaw, m_defaultCameraPitch);
}

void BistroExteriorPathtracingD3D12::ResetCameraSpeeds()
{
    m_baseMoveSpeed = 17.0f;
    m_fastMoveSpeed = 58.2f;
    m_camera.SetMoveSpeeds(m_baseMoveSpeed, m_fastMoveSpeed);
}

void BistroExteriorPathtracingD3D12::ResetAccumulation()
{
    m_accumulatedFrames = 0;
    m_resetAccumulationRequested = true;
}

bool BistroExteriorPathtracingD3D12::HasAccumulationStateChanged()
{
    XMFLOAT3 cameraPosition = m_camera.GetPosition();
    XMFLOAT4 cameraAndYaw(cameraPosition.x, cameraPosition.y, cameraPosition.z, m_camera.GetYawRadians() + m_camera.GetPitchRadians());
    XMFLOAT4 lighting(m_lightDirection[0], m_lightDirection[1], m_lightDirection[2], m_lightIntensity + static_cast<float>(m_debugViewMode) + (m_shadowEnabled ? 1.0f : 0.0f) + (m_skyNeeEnabled ? 2.0f : 0.0f));
    XMFLOAT4 giOptions(static_cast<float>(m_giSamplesPerFrame), m_giRadianceClamp, m_giTemporalClampScale, m_giTemporalClampMin);
    XMFLOAT4 pathOptions(static_cast<float>(m_maxPathBounces), static_cast<float>(m_minPathBounces), static_cast<float>(m_restirCandidateSamples), UsesRestirReuse(m_mode) ? 1.0f : 0.0f);
    XMFLOAT4 restirOptions(m_restirTemporalReuse ? 1.0f : 0.0f, static_cast<float>(m_restirSpatialReusePasses), static_cast<float>(m_restirSpatialRadius), m_restirMClamp);
    XMFLOAT4 lightSystemOptions(
        (m_emissiveLightsEnabled ? m_emissiveLightIntensity : 0.0f) + (m_proceduralLightsEnabled ? m_proceduralLightIntensity : 0.0f),
        m_environmentMapEnabled ? m_environmentIntensity : 0.0f,
        m_environmentRotation,
        static_cast<float>(m_activeLightCount));
    const bool changed =
        m_resetAccumulationRequested ||
        memcmp(&cameraAndYaw, &m_lastCameraAndYaw, sizeof(XMFLOAT4)) != 0 ||
        memcmp(&lighting, &m_lastLighting, sizeof(XMFLOAT4)) != 0 ||
        memcmp(&giOptions, &m_lastGiOptions, sizeof(XMFLOAT4)) != 0 ||
        memcmp(&pathOptions, &m_lastPathOptions, sizeof(XMFLOAT4)) != 0 ||
        memcmp(&restirOptions, &m_lastRestirOptions, sizeof(XMFLOAT4)) != 0 ||
        memcmp(&lightSystemOptions, &m_lastLightSystemOptions, sizeof(XMFLOAT4)) != 0;
    m_lastCameraAndYaw = cameraAndYaw;
    m_lastLighting = lighting;
    m_lastGiOptions = giOptions;
    m_lastPathOptions = pathOptions;
    m_lastRestirOptions = restirOptions;
    m_lastLightSystemOptions = lightSystemOptions;
    m_resetAccumulationRequested = false;
    return changed;
}

ComPtr<ID3D12Resource> BistroExteriorPathtracingD3D12::CreateDefaultBuffer(const void* data, UINT64 size, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES finalState, const wchar_t* name)
{
    ComPtr<ID3D12Resource> resource;
    ThrowIfFailed(m_device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE, &CD3DX12_RESOURCE_DESC::Buffer(size, flags), D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&resource)));
    resource->SetName(name);
    if (data && size > 0)
    {
        ComPtr<ID3D12Resource> upload = CreateUploadBuffer(data, size, L"UploadBuffer");
        m_uploadBuffers.push_back(upload);
        m_commandList->CopyBufferRegion(resource.Get(), 0, upload.Get(), 0, size);
        m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, finalState));
    }
    return resource;
}

ComPtr<ID3D12Resource> BistroExteriorPathtracingD3D12::CreateUploadBuffer(const void* data, UINT64 size, const wchar_t* name)
{
    ComPtr<ID3D12Resource> resource;
    ThrowIfFailed(m_device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD), D3D12_HEAP_FLAG_NONE, &CD3DX12_RESOURCE_DESC::Buffer(size), D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&resource)));
    resource->SetName(name);
    if (data && size > 0)
    {
        void* mapped = nullptr;
        ThrowIfFailed(resource->Map(0, nullptr, &mapped));
        memcpy(mapped, data, size);
        resource->Unmap(0, nullptr);
    }
    return resource;
}

ComPtr<ID3D12Resource> BistroExteriorPathtracingD3D12::CreateUavBuffer(UINT64 size, D3D12_RESOURCE_STATES initialState, const wchar_t* name)
{
    ComPtr<ID3D12Resource> resource;
    ThrowIfFailed(m_device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE, &CD3DX12_RESOURCE_DESC::Buffer(size, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS), initialState, nullptr, IID_PPV_ARGS(&resource)));
    resource->SetName(name);
    return resource;
}

D3D12_CPU_DESCRIPTOR_HANDLE BistroExteriorPathtracingD3D12::CpuDescriptor(UINT index) const
{
    return CD3DX12_CPU_DESCRIPTOR_HANDLE(m_descriptorHeap->GetCPUDescriptorHandleForHeapStart(), index, m_descriptorSize);
}

D3D12_GPU_DESCRIPTOR_HANDLE BistroExteriorPathtracingD3D12::GpuDescriptor(UINT index) const
{
    return CD3DX12_GPU_DESCRIPTOR_HANDLE(m_descriptorHeap->GetGPUDescriptorHandleForHeapStart(), index, m_descriptorSize);
}

std::wstring BistroExteriorPathtracingD3D12::ShaderFileName() const
{
    if (m_mode == BistroPathtracingMode::ReSTIR)
    {
        return L"BistroExteriorPathtracingReSTIR.lib.cso";
    }
    if (m_mode == BistroPathtracingMode::ReSTIRDI)
    {
        return L"BistroExteriorPathtracingReSTIRDI.lib.cso";
    }
    return L"BistroExteriorPathtracing.lib.cso";
}

std::wstring BistroExteriorPathtracingD3D12::RestirReuseShaderFileName() const
{
    return L"BistroExteriorPathtracingReSTIRResolve.cso";
}

std::wstring BistroExteriorPathtracingD3D12::DenoiseShaderFileName() const
{
    return L"BistroExteriorPathtracingDenoise.cso";
}

UINT BistroExteriorPathtracingD3D12::MaxTraceRecursionDepth() const
{
    return 1u;
}

UINT BistroExteriorPathtracingD3D12::Align(UINT value, UINT alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

void BistroExteriorPathtracingD3D12::AllocateImGuiDescriptor(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* outCpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE* outGpuHandle)
{
    auto* sample = static_cast<BistroExteriorPathtracingD3D12*>(info->UserData);
    const UINT descriptorIndex = sample->m_imguiDescriptorCursor++;
    if (descriptorIndex >= ImGuiDescriptorCount)
    {
        ThrowIfFailed(E_OUTOFMEMORY);
    }

    *outCpuHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(sample->m_imguiDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), descriptorIndex, sample->m_imguiDescriptorSize);
    *outGpuHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(sample->m_imguiDescriptorHeap->GetGPUDescriptorHandleForHeapStart(), descriptorIndex, sample->m_imguiDescriptorSize);
}

void BistroExteriorPathtracingD3D12::FreeImGuiDescriptor(ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE)
{
}
