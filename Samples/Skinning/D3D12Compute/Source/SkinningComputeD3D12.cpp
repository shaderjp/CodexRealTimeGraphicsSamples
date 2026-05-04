#include "stdafx.h"
#include "SkinningComputeD3D12.h"

extern "C" { __declspec(dllexport) extern const UINT D3D12SDKVersion = 619; }
extern "C" { __declspec(dllexport) extern const char* D3D12SDKPath = u8".\\D3D12\\"; }

namespace
{
    XMFLOAT4X4 LoadGltfMatrixAsRowMajor(const float* value)
    {
        return XMFLOAT4X4(
            value[0], value[1], value[2], value[3],
            value[4], value[5], value[6], value[7],
            value[8], value[9], value[10], value[11],
            value[12], value[13], value[14], value[15]);
    }
}

SkinningComputeD3D12::SkinningComputeD3D12(UINT width, UINT height, std::wstring name) :
    DXSample(width, height, name),
    m_frameIndex(0),
    m_viewport(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)),
    m_scissorRect(0, 0, static_cast<LONG>(width), static_cast<LONG>(height)),
    m_rtvDescriptorSize(0),
    m_cbvSrvDescriptorSize(0),
    m_mappedSceneConstantBuffer(nullptr),
    m_mappedJointConstantBuffer(nullptr),
    m_animationTime(0.0f)
{
}

void SkinningComputeD3D12::OnInit()
{
    ThrowIfFailed(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
    LoadModel();
    LoadPipeline();
    LoadAssets();
}

void SkinningComputeD3D12::LoadPipeline()
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

    if (m_useWarpDevice)
    {
        ComPtr<IDXGIAdapter> warpAdapter;
        ThrowIfFailed(factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)));
        ThrowIfFailed(D3D12CreateDevice(warpAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device)));
    }
    else
    {
        ComPtr<IDXGIAdapter1> hardwareAdapter;
        GetHardwareAdapter(factory.Get(), &hardwareAdapter);
        ThrowIfFailed(D3D12CreateDevice(hardwareAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device)));
    }

    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
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

    D3D12_DESCRIPTOR_HEAP_DESC cbvSrvHeapDesc = {};
    cbvSrvHeapDesc.NumDescriptors = 5;
    cbvSrvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    cbvSrvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(m_device->CreateDescriptorHeap(&cbvSrvHeapDesc, IID_PPV_ARGS(&m_cbvSrvHeap)));
    m_cbvSrvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocator)));
}

void SkinningComputeD3D12::LoadAssets()
{
    CD3DX12_DESCRIPTOR_RANGE cbvRange0;
    cbvRange0.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);
    CD3DX12_DESCRIPTOR_RANGE cbvRange1;
    cbvRange1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 1);
    CD3DX12_DESCRIPTOR_RANGE textureSrvRange;
    textureSrvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
    CD3DX12_DESCRIPTOR_RANGE sourceVertexSrvRange;
    sourceVertexSrvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);
    CD3DX12_DESCRIPTOR_RANGE skinnedVertexUavRange;
    skinnedVertexUavRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);

    CD3DX12_ROOT_PARAMETER rootParameters[5];
    rootParameters[0].InitAsDescriptorTable(1, &cbvRange0, D3D12_SHADER_VISIBILITY_ALL);
    rootParameters[1].InitAsDescriptorTable(1, &cbvRange1, D3D12_SHADER_VISIBILITY_ALL);
    rootParameters[2].InitAsDescriptorTable(1, &textureSrvRange, D3D12_SHADER_VISIBILITY_PIXEL);
    rootParameters[3].InitAsDescriptorTable(1, &sourceVertexSrvRange, D3D12_SHADER_VISIBILITY_ALL);
    rootParameters[4].InitAsDescriptorTable(1, &skinnedVertexUavRange, D3D12_SHADER_VISIBILITY_ALL);

    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.ShaderRegister = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc;
    rootSignatureDesc.Init(_countof(rootParameters), rootParameters, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> error;
    ThrowIfFailed(D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error));
    ThrowIfFailed(m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)));

    UINT8* vertexShaderData = nullptr;
    UINT8* pixelShaderData = nullptr;
    UINT8* computeShaderData = nullptr;
    UINT vertexShaderDataLength = 0;
    UINT pixelShaderDataLength = 0;
    UINT computeShaderDataLength = 0;
    ThrowIfFailed(ReadDataFromFile(GetAssetFullPath(L"SkinningCompute.vs.cso").c_str(), &vertexShaderData, &vertexShaderDataLength));
    ThrowIfFailed(ReadDataFromFile(GetAssetFullPath(L"SkinningCompute.ps.cso").c_str(), &pixelShaderData, &pixelShaderDataLength));
    ThrowIfFailed(ReadDataFromFile(GetAssetFullPath(L"SkinningCompute.cs.cso").c_str(), &computeShaderData, &computeShaderDataLength));

    D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(SkinnedVertex, positionTexcoordX), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32_FLOAT, 0, offsetof(SkinnedVertex, positionTexcoordX) + sizeof(float) * 3, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(SkinnedVertex, normalTexcoordY), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 1, DXGI_FORMAT_R32_FLOAT, 0, offsetof(SkinnedVertex, normalTexcoordY) + sizeof(float) * 3, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
    psoDesc.pRootSignature = m_rootSignature.Get();
    psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShaderData, vertexShaderDataLength);
    psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShaderData, pixelShaderDataLength);
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    psoDesc.SampleDesc.Count = 1;
    ThrowIfFailed(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineState)));

    D3D12_COMPUTE_PIPELINE_STATE_DESC computePsoDesc = {};
    computePsoDesc.pRootSignature = m_rootSignature.Get();
    computePsoDesc.CS = CD3DX12_SHADER_BYTECODE(computeShaderData, computeShaderDataLength);
    ThrowIfFailed(m_device->CreateComputePipelineState(&computePsoDesc, IID_PPV_ARGS(&m_computePipelineState)));
    delete[] vertexShaderData;
    delete[] pixelShaderData;
    delete[] computeShaderData;

    ThrowIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocator.Get(), m_pipelineState.Get(), IID_PPV_ARGS(&m_commandList)));

    CD3DX12_RANGE readRange(0, 0);
    const UINT vertexBufferSize = static_cast<UINT>(m_vertices.size() * sizeof(Vertex));
    ThrowIfFailed(m_device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD), D3D12_HEAP_FLAG_NONE, &CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize), D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_sourceVertexBuffer)));
    UINT8* vertexDataBegin = nullptr;
    ThrowIfFailed(m_sourceVertexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&vertexDataBegin)));
    memcpy(vertexDataBegin, m_vertices.data(), vertexBufferSize);
    m_sourceVertexBuffer->Unmap(0, nullptr);

    D3D12_SHADER_RESOURCE_VIEW_DESC sourceSrvDesc = {};
    sourceSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sourceSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    sourceSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
    sourceSrvDesc.Buffer.FirstElement = 0;
    sourceSrvDesc.Buffer.NumElements = static_cast<UINT>(m_vertices.size());
    sourceSrvDesc.Buffer.StructureByteStride = sizeof(Vertex);
    CD3DX12_CPU_DESCRIPTOR_HANDLE sourceSrvHandle(m_cbvSrvHeap->GetCPUDescriptorHandleForHeapStart(), 3, m_cbvSrvDescriptorSize);
    m_device->CreateShaderResourceView(m_sourceVertexBuffer.Get(), &sourceSrvDesc, sourceSrvHandle);

    const UINT skinnedVertexBufferSize = static_cast<UINT>(m_vertices.size() * sizeof(SkinnedVertex));
    ThrowIfFailed(m_device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE, &CD3DX12_RESOURCE_DESC::Buffer(skinnedVertexBufferSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&m_skinnedVertexBuffer)));

    D3D12_UNORDERED_ACCESS_VIEW_DESC skinnedUavDesc = {};
    skinnedUavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    skinnedUavDesc.Format = DXGI_FORMAT_UNKNOWN;
    skinnedUavDesc.Buffer.FirstElement = 0;
    skinnedUavDesc.Buffer.NumElements = static_cast<UINT>(m_vertices.size());
    skinnedUavDesc.Buffer.StructureByteStride = sizeof(SkinnedVertex);
    CD3DX12_CPU_DESCRIPTOR_HANDLE skinnedUavHandle(m_cbvSrvHeap->GetCPUDescriptorHandleForHeapStart(), 4, m_cbvSrvDescriptorSize);
    m_device->CreateUnorderedAccessView(m_skinnedVertexBuffer.Get(), nullptr, &skinnedUavDesc, skinnedUavHandle);

    m_vertexBufferView.BufferLocation = m_skinnedVertexBuffer->GetGPUVirtualAddress();
    m_vertexBufferView.StrideInBytes = sizeof(SkinnedVertex);
    m_vertexBufferView.SizeInBytes = skinnedVertexBufferSize;

    const UINT indexBufferSize = static_cast<UINT>(m_indices.size() * sizeof(uint16_t));
    ThrowIfFailed(m_device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD), D3D12_HEAP_FLAG_NONE, &CD3DX12_RESOURCE_DESC::Buffer(indexBufferSize), D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_indexBuffer)));
    UINT8* indexDataBegin = nullptr;
    ThrowIfFailed(m_indexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&indexDataBegin)));
    memcpy(indexDataBegin, m_indices.data(), indexBufferSize);
    m_indexBuffer->Unmap(0, nullptr);
    m_indexBufferView.BufferLocation = m_indexBuffer->GetGPUVirtualAddress();
    m_indexBufferView.Format = DXGI_FORMAT_R16_UINT;
    m_indexBufferView.SizeInBytes = indexBufferSize;

    D3D12_CLEAR_VALUE depthOptimizedClearValue = {};
    depthOptimizedClearValue.Format = DXGI_FORMAT_D32_FLOAT;
    depthOptimizedClearValue.DepthStencil.Depth = 1.0f;
    ThrowIfFailed(m_device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE, &CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D32_FLOAT, m_width, m_height, 1, 0, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL), D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthOptimizedClearValue, IID_PPV_ARGS(&m_depthStencil)));
    m_device->CreateDepthStencilView(m_depthStencil.Get(), nullptr, m_dsvHeap->GetCPUDescriptorHandleForHeapStart());

    const UINT sceneConstantBufferSize = CalculateConstantBufferByteSize(sizeof(SceneConstantBuffer));
    ThrowIfFailed(m_device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD), D3D12_HEAP_FLAG_NONE, &CD3DX12_RESOURCE_DESC::Buffer(sceneConstantBufferSize), D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_sceneConstantBuffer)));
    D3D12_CONSTANT_BUFFER_VIEW_DESC sceneCbvDesc = {};
    sceneCbvDesc.BufferLocation = m_sceneConstantBuffer->GetGPUVirtualAddress();
    sceneCbvDesc.SizeInBytes = sceneConstantBufferSize;
    m_device->CreateConstantBufferView(&sceneCbvDesc, m_cbvSrvHeap->GetCPUDescriptorHandleForHeapStart());
    ThrowIfFailed(m_sceneConstantBuffer->Map(0, &readRange, reinterpret_cast<void**>(&m_mappedSceneConstantBuffer)));

    const UINT jointConstantBufferSize = CalculateConstantBufferByteSize(sizeof(JointConstantBuffer));
    ThrowIfFailed(m_device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD), D3D12_HEAP_FLAG_NONE, &CD3DX12_RESOURCE_DESC::Buffer(jointConstantBufferSize), D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_jointConstantBuffer)));
    D3D12_CONSTANT_BUFFER_VIEW_DESC jointCbvDesc = {};
    jointCbvDesc.BufferLocation = m_jointConstantBuffer->GetGPUVirtualAddress();
    jointCbvDesc.SizeInBytes = jointConstantBufferSize;
    CD3DX12_CPU_DESCRIPTOR_HANDLE jointCbvHandle(m_cbvSrvHeap->GetCPUDescriptorHandleForHeapStart(), 1, m_cbvSrvDescriptorSize);
    m_device->CreateConstantBufferView(&jointCbvDesc, jointCbvHandle);
    ThrowIfFailed(m_jointConstantBuffer->Map(0, &readRange, reinterpret_cast<void**>(&m_mappedJointConstantBuffer)));

    CreateTexture(LoadTexture(GetAssetFullPath(L"..\\..\\..\\..\\Assets\\CesiumMan\\CesiumMan_img0.jpg")));
    UpdateAnimation();

    ThrowIfFailed(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
    m_fenceValue = 1;
    m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (m_fenceEvent == nullptr)
    {
        ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
    }

    ThrowIfFailed(m_commandList->Close());
    ID3D12CommandList* commandLists[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(_countof(commandLists), commandLists);
    WaitForPreviousFrame();
    m_textureUploadHeap.Reset();
}

void SkinningComputeD3D12::OnUpdate()
{
    UpdateAnimation();
}

void SkinningComputeD3D12::OnRender()
{
    PopulateCommandList();
    ID3D12CommandList* commandLists[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(_countof(commandLists), commandLists);
    ThrowIfFailed(m_swapChain->Present(1, 0));
    WaitForPreviousFrame();
}

void SkinningComputeD3D12::OnDestroy()
{
    WaitForPreviousFrame();
    if (m_sceneConstantBuffer) m_sceneConstantBuffer->Unmap(0, nullptr);
    if (m_jointConstantBuffer) m_jointConstantBuffer->Unmap(0, nullptr);
    CloseHandle(m_fenceEvent);
    CoUninitialize();
}

void SkinningComputeD3D12::LoadModel()
{
    std::wstring path = GetAssetFullPath(L"..\\..\\..\\..\\Assets\\CesiumMan\\CesiumMan_data.bin");
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
    {
        ThrowIfFailed(HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND));
    }

    const size_t fileSize = static_cast<size_t>(file.tellg());
    std::vector<uint8_t> data(fileSize);
    file.seekg(0);
    file.read(reinterpret_cast<char*>(data.data()), fileSize);

    const auto* indices = reinterpret_cast<const uint16_t*>(data.data() + SkinningData::IndexOffset);
    const auto* positions = reinterpret_cast<const XMFLOAT3*>(data.data() + SkinningData::PositionOffset);
    const auto* normals = reinterpret_cast<const XMFLOAT3*>(data.data() + SkinningData::NormalOffset);
    const auto* texcoords = reinterpret_cast<const XMFLOAT2*>(data.data() + SkinningData::TexcoordOffset);
    const auto* weights = reinterpret_cast<const XMFLOAT4*>(data.data() + SkinningData::WeightOffset);
    const auto* inverseBinds = reinterpret_cast<const float*>(data.data() + SkinningData::InverseBindOffset);

    m_indices.assign(indices, indices + SkinningData::IndexCount);
    m_vertices.resize(SkinningData::VertexCount);
    for (uint32_t i = 0; i < SkinningData::VertexCount; ++i)
    {
        const uint16_t* joint = reinterpret_cast<const uint16_t*>(data.data() + SkinningData::JointOffset + i * 8);
        m_vertices[i] =
        {
            XMFLOAT4(positions[i].x, positions[i].y, positions[i].z, texcoords[i].x),
            XMFLOAT4(normals[i].x, normals[i].y, normals[i].z, texcoords[i].y),
            XMUINT4(joint[0], joint[1], joint[2], joint[3]),
            weights[i]
        };
    }

    for (uint32_t i = 0; i < SkinningData::JointCount; ++i)
    {
        m_inverseBindMatrices[i] = LoadGltfMatrixAsRowMajor(inverseBinds + i * 16);
    }

    for (uint32_t i = 0; i < SkinningData::NodeCount; ++i)
    {
        m_nodePoses[i] = { SkinningData::Nodes[i].translation, SkinningData::Nodes[i].rotation, SkinningData::Nodes[i].scale };
    }
}

SkinningComputeD3D12::ImageData SkinningComputeD3D12::LoadTexture(const std::wstring& path) const
{
    ComPtr<IWICImagingFactory2> factory;
    ThrowIfFailed(CoCreateInstance(CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)));

    ComPtr<IWICBitmapDecoder> decoder;
    ThrowIfFailed(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder));
    ComPtr<IWICBitmapFrameDecode> frame;
    ThrowIfFailed(decoder->GetFrame(0, &frame));

    ImageData image;
    ThrowIfFailed(frame->GetSize(&image.width, &image.height));
    image.pixels.resize(static_cast<size_t>(image.width) * image.height * 4);

    ComPtr<IWICFormatConverter> converter;
    ThrowIfFailed(factory->CreateFormatConverter(&converter));
    ThrowIfFailed(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom));
    ThrowIfFailed(converter->CopyPixels(nullptr, image.width * 4, static_cast<UINT>(image.pixels.size()), image.pixels.data()));
    return image;
}

void SkinningComputeD3D12::CreateTexture(const ImageData& image)
{
    D3D12_RESOURCE_DESC textureDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, image.width, image.height);
    ThrowIfFailed(m_device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE, &textureDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_texture)));

    const UINT64 uploadBufferSize = GetRequiredIntermediateSize(m_texture.Get(), 0, 1);
    ThrowIfFailed(m_device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD), D3D12_HEAP_FLAG_NONE, &CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize), D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_textureUploadHeap)));

    D3D12_SUBRESOURCE_DATA textureData = {};
    textureData.pData = image.pixels.data();
    textureData.RowPitch = static_cast<LONG_PTR>(image.width) * 4;
    textureData.SlicePitch = textureData.RowPitch * image.height;
    UpdateSubresources(m_commandList.Get(), m_texture.Get(), m_textureUploadHeap.Get(), 0, 0, 1, &textureData);
    m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(m_cbvSrvHeap->GetCPUDescriptorHandleForHeapStart(), 2, m_cbvSrvDescriptorSize);
    m_device->CreateShaderResourceView(m_texture.Get(), &srvDesc, srvHandle);
}

void SkinningComputeD3D12::PopulateCommandList()
{
    ThrowIfFailed(m_commandAllocator->Reset());
    ThrowIfFailed(m_commandList->Reset(m_commandAllocator.Get(), m_pipelineState.Get()));

    ID3D12DescriptorHeap* descriptorHeaps[] = { m_cbvSrvHeap.Get() };
    m_commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
    m_commandList->SetComputeRootSignature(m_rootSignature.Get());
    m_commandList->SetPipelineState(m_computePipelineState.Get());
    m_commandList->SetComputeRootDescriptorTable(1, CD3DX12_GPU_DESCRIPTOR_HANDLE(m_cbvSrvHeap->GetGPUDescriptorHandleForHeapStart(), 1, m_cbvSrvDescriptorSize));
    m_commandList->SetComputeRootDescriptorTable(3, CD3DX12_GPU_DESCRIPTOR_HANDLE(m_cbvSrvHeap->GetGPUDescriptorHandleForHeapStart(), 3, m_cbvSrvDescriptorSize));
    m_commandList->SetComputeRootDescriptorTable(4, CD3DX12_GPU_DESCRIPTOR_HANDLE(m_cbvSrvHeap->GetGPUDescriptorHandleForHeapStart(), 4, m_cbvSrvDescriptorSize));
    m_commandList->Dispatch((SkinningData::VertexCount + 63) / 64, 1, 1);

    D3D12_RESOURCE_BARRIER computeBarriers[] =
    {
        CD3DX12_RESOURCE_BARRIER::UAV(m_skinnedVertexBuffer.Get()),
        CD3DX12_RESOURCE_BARRIER::Transition(m_skinnedVertexBuffer.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER),
    };
    m_commandList->ResourceBarrier(_countof(computeBarriers), computeBarriers);

    m_commandList->SetPipelineState(m_pipelineState.Get());
    m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
    m_commandList->SetGraphicsRootDescriptorTable(0, m_cbvSrvHeap->GetGPUDescriptorHandleForHeapStart());
    m_commandList->SetGraphicsRootDescriptorTable(2, CD3DX12_GPU_DESCRIPTOR_HANDLE(m_cbvSrvHeap->GetGPUDescriptorHandleForHeapStart(), 2, m_cbvSrvDescriptorSize));
    m_commandList->RSSetViewports(1, &m_viewport);
    m_commandList->RSSetScissorRects(1, &m_scissorRect);
    m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, m_rtvDescriptorSize);
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
    m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

    const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
    m_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
    m_commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
    m_commandList->IASetIndexBuffer(&m_indexBufferView);
    m_commandList->DrawIndexedInstanced(static_cast<UINT>(m_indices.size()), 1, 0, 0, 0);
    m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_skinnedVertexBuffer.Get(), D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
    m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));
    ThrowIfFailed(m_commandList->Close());
}

void SkinningComputeD3D12::WaitForPreviousFrame()
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

XMMATRIX SkinningComputeD3D12::GetLocalMatrix(UINT nodeIndex) const
{
    const auto& node = SkinningData::Nodes[nodeIndex];
    if (node.hasMatrix)
    {
        return XMLoadFloat4x4(&node.matrix);
    }
    const NodePose& pose = m_nodePoses[nodeIndex];
    return XMMatrixScaling(pose.scale.x, pose.scale.y, pose.scale.z) *
        XMMatrixRotationQuaternion(XMLoadFloat4(&pose.rotation)) *
        XMMatrixTranslation(pose.translation.x, pose.translation.y, pose.translation.z);
}

void SkinningComputeD3D12::UpdateAnimation()
{
    m_animationTime += 1.0f / 60.0f;
    float animationTime = fmodf(m_animationTime, 2.0f);

    for (uint32_t nodeIndex = 0; nodeIndex < SkinningData::NodeCount; ++nodeIndex)
    {
        m_nodePoses[nodeIndex] = { SkinningData::Nodes[nodeIndex].translation, SkinningData::Nodes[nodeIndex].rotation, SkinningData::Nodes[nodeIndex].scale };
    }

    const std::wstring path = GetAssetFullPath(L"..\\..\\..\\..\\Assets\\CesiumMan\\CesiumMan_data.bin");
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    const size_t fileSize = static_cast<size_t>(file.tellg());
    std::vector<uint8_t> data(fileSize);
    file.seekg(0);
    file.read(reinterpret_cast<char*>(data.data()), fileSize);

    for (uint32_t group = 0; group < SkinningData::JointCount; ++group)
    {
        const float* times = reinterpret_cast<const float*>(data.data() + SkinningData::TimeOffset + group * 192);
        uint32_t frame0 = 0;
        while (frame0 + 1 < SkinningData::KeyframeCount && times[frame0 + 1] < animationTime)
        {
            frame0++;
        }
        uint32_t frame1 = (frame0 + 1) % SkinningData::KeyframeCount;
        float span = (frame1 > frame0) ? times[frame1] - times[frame0] : 2.0f - times[frame0];
        float alpha = span > 0.0f ? (animationTime - times[frame0]) / span : 0.0f;
        alpha = std::clamp(alpha, 0.0f, 1.0f);

        const auto* translations = reinterpret_cast<const XMFLOAT3*>(data.data() + SkinningData::TranslationOffset + group * 1152);
        const auto* rotations = reinterpret_cast<const XMFLOAT4*>(data.data() + SkinningData::RotationOffset + group * 768);
        const auto* scales = reinterpret_cast<const XMFLOAT3*>(data.data() + SkinningData::TranslationOffset + group * 1152 + 576);

        NodePose& pose = m_nodePoses[SkinningData::AnimatedNodes[group]];
        XMStoreFloat3(&pose.translation, XMVectorLerp(XMLoadFloat3(&translations[frame0]), XMLoadFloat3(&translations[frame1]), alpha));
        XMStoreFloat4(&pose.rotation, XMQuaternionNormalize(XMQuaternionSlerp(XMLoadFloat4(&rotations[frame0]), XMLoadFloat4(&rotations[frame1]), alpha)));
        XMStoreFloat3(&pose.scale, XMVectorLerp(XMLoadFloat3(&scales[frame0]), XMLoadFloat3(&scales[frame1]), alpha));
    }

    for (uint32_t i = 0; i < SkinningData::NodeCount; ++i)
    {
        XMMATRIX local = GetLocalMatrix(i);
        int parent = SkinningData::Nodes[i].parent;
        XMMATRIX global = parent >= 0 ? local * XMLoadFloat4x4(&m_globalNodeMatrices[parent]) : local;
        XMStoreFloat4x4(&m_globalNodeMatrices[i], global);
    }

    for (uint32_t i = 0; i < SkinningData::JointCount; ++i)
    {
        uint32_t nodeIndex = SkinningData::JointNodes[i];
        XMMATRIX joint = XMLoadFloat4x4(&m_inverseBindMatrices[i]) * XMLoadFloat4x4(&m_globalNodeMatrices[nodeIndex]);
        XMStoreFloat4x4(&m_jointConstantBufferData.joints[i], joint);
    }

    UpdateConstantBuffers();
}

void SkinningComputeD3D12::UpdateConstantBuffers()
{
    XMMATRIX model = XMMatrixScaling(1.2f, 1.2f, 1.2f) * XMMatrixRotationY(XMConvertToRadians(180.0f));
    XMMATRIX view = XMMatrixLookAtLH(XMVectorSet(0.0f, 0.85f, -8.0f, 1.0f), XMVectorSet(0.0f, 0.75f, 0.0f, 1.0f), XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
    XMMATRIX projection = XMMatrixPerspectiveFovLH(XMConvertToRadians(45.0f), m_aspectRatio, 0.1f, 100.0f);
    XMStoreFloat4x4(&m_sceneConstantBufferData.model, model);
    XMStoreFloat4x4(&m_sceneConstantBufferData.modelViewProjection, model * view * projection);
    m_sceneConstantBufferData.lightDirection = XMFLOAT4(-0.35f, -0.8f, 0.45f, 0.0f);
    m_sceneConstantBufferData.lightColor = XMFLOAT4(1.0f, 0.96f, 0.88f, 3.0f);
    memcpy(m_mappedSceneConstantBuffer, &m_sceneConstantBufferData, sizeof(m_sceneConstantBufferData));
    memcpy(m_mappedJointConstantBuffer, &m_jointConstantBufferData, sizeof(m_jointConstantBufferData));
}
