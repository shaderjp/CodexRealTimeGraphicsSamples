#pragma once

#include <DirectXMath.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Bistro
{
    static constexpr uint32_t TextureSlotCount = 4;
    static constexpr uint32_t TextureSlotBaseColor = 0;
    static constexpr uint32_t TextureSlotNormal = 1;
    static constexpr uint32_t TextureSlotSpecular = 2;
    static constexpr uint32_t TextureSlotEmissive = 3;
    static constexpr uint32_t MaxMeshletVertices = 64;
    static constexpr uint32_t MaxMeshletTriangles = 96;

    struct Vertex
    {
        DirectX::XMFLOAT3 position;
        DirectX::XMFLOAT3 normal;
        DirectX::XMFLOAT4 tangent;
        DirectX::XMFLOAT2 texcoord;
    };

    struct Material
    {
        std::wstring name;
        std::array<std::wstring, TextureSlotCount> textures;
        DirectX::XMFLOAT4 baseColorFactor = DirectX::XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
        float alphaCutoff = 0.33f;
        bool alphaMasked = false;
    };

    struct DrawItem
    {
        uint32_t indexCount = 0;
        uint32_t startIndex = 0;
        int32_t baseVertex = 0;
        uint32_t materialIndex = 0;
    };

    struct MeshletRecord
    {
        uint32_t vertexOffset = 0;
        uint32_t vertexCount = 0;
        uint32_t triangleOffset = 0;
        uint32_t triangleCount = 0;
        uint32_t boundsIndex = 0;
    };

    struct MeshletBounds
    {
        DirectX::XMFLOAT4 sphere = DirectX::XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);
        DirectX::XMFLOAT4 coneApex = DirectX::XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);
        DirectX::XMFLOAT4 coneAxis = DirectX::XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);
    };

    struct MeshletDispatchRange
    {
        uint32_t materialIndex = 0;
        uint32_t firstMeshlet = 0;
        uint32_t meshletCount = 0;
        uint32_t padding = 0;
    };

    struct Scene
    {
        std::wstring assetRoot;
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        std::vector<Material> materials;
        std::vector<DrawItem> draws;
        std::vector<MeshletRecord> meshlets;
        std::vector<uint32_t> meshletVertices;
        std::vector<uint32_t> meshletTriangles;
        std::vector<MeshletBounds> meshletBounds;
        std::vector<MeshletDispatchRange> meshletDispatchRanges;
        DirectX::XMFLOAT3 boundsMin = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
        DirectX::XMFLOAT3 boundsMax = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
    };

    std::wstring FindAssetRoot();
    Scene LoadScene(const std::wstring& assetRoot);
    std::wstring GetRepoRootFromExecutable(uint32_t levelsFromExeToRepoRoot);
}
