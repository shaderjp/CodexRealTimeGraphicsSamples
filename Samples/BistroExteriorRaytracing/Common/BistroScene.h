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

    struct RtMaterial
    {
        DirectX::XMFLOAT4 baseColorFactor = DirectX::XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
        uint32_t textureBaseIndex = 0;
        uint32_t alphaMasked = 0;
        float alphaCutoff = 0.33f;
        uint32_t padding = 0;
    };

    struct RtGeometryRecord
    {
        uint32_t indexOffset = 0;
        uint32_t indexCount = 0;
        int32_t baseVertex = 0;
        uint32_t materialIndex = 0;
    };

    struct Scene
    {
        std::wstring assetRoot;
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        std::vector<Material> materials;
        std::vector<DrawItem> draws;
        DirectX::XMFLOAT3 boundsMin = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
        DirectX::XMFLOAT3 boundsMax = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
    };

    std::wstring FindAssetRoot();
    Scene LoadScene(const std::wstring& assetRoot);
    std::wstring GetRepoRootFromExecutable(uint32_t levelsFromExeToRepoRoot);
}
