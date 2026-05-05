#pragma once

#include <DirectXTex.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Bistro
{
    struct TextureMip
    {
        uint32_t width = 1;
        uint32_t height = 1;
        size_t offset = 0;
        size_t rowPitch = 4;
        size_t slicePitch = 4;
    };

    struct TextureData
    {
        uint32_t width = 1;
        uint32_t height = 1;
        uint32_t mipLevels = 1;
        DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
        bool fallback = false;
        std::vector<TextureMip> mips;
        std::vector<uint8_t> pixels;
    };

    TextureData LoadTextureRgba8(const std::wstring& path, bool srgb, const uint8_t fallback[4]);
    TextureData LoadTextureD3D12(const std::wstring& path, bool srgb, const uint8_t fallback[4]);
    TextureData LoadTextureVulkan(const std::wstring& path, bool srgb, const uint8_t fallback[4], bool preserveBcCompressed);
}
