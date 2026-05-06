#include "BistroTexture.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <stdexcept>

namespace
{
    constexpr size_t MaxTextureDimension = 512;

    Bistro::TextureData MakeFallbackTexture(bool srgb, const uint8_t fallback[4])
    {
        Bistro::TextureData texture;
        texture.width = 1;
        texture.height = 1;
        texture.mipLevels = 1;
        texture.format = srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
        texture.fallback = true;
        texture.mips.push_back({ 1, 1, 0, 4, 4 });
        texture.pixels.assign(fallback, fallback + 4);
        return texture;
    }

    Bistro::TextureData MakeTextureFromImages(const DirectX::Image* images, size_t imageCount, DXGI_FORMAT format)
    {
        if (images == nullptr || imageCount == 0 || images[0].width == 0 || images[0].height == 0 || images[0].pixels == nullptr)
        {
            throw std::runtime_error("Invalid texture image data.");
        }

        Bistro::TextureData texture;
        texture.width = static_cast<uint32_t>(images[0].width);
        texture.height = static_cast<uint32_t>(images[0].height);
        texture.mipLevels = static_cast<uint32_t>(imageCount);
        texture.format = format;
        texture.mips.reserve(imageCount);

        for (size_t mipIndex = 0; mipIndex < imageCount; ++mipIndex)
        {
            const DirectX::Image& image = images[mipIndex];
            const size_t rowSize = image.width * 4;
            const size_t slicePitch = rowSize * image.height;
            const size_t offset = texture.pixels.size();
            texture.pixels.resize(offset + slicePitch);

            for (size_t y = 0; y < image.height; ++y)
            {
                memcpy(texture.pixels.data() + offset + y * rowSize, image.pixels + y * image.rowPitch, rowSize);
            }

            texture.mips.push_back({
                static_cast<uint32_t>(image.width),
                static_cast<uint32_t>(image.height),
                offset,
                rowSize,
                slicePitch
            });
        }

        return texture;
    }

    Bistro::TextureData MakeTextureFromImageMemory(const DirectX::Image* images, size_t imageCount, DXGI_FORMAT format)
    {
        if (images == nullptr || imageCount == 0 || images[0].width == 0 || images[0].height == 0 || images[0].pixels == nullptr)
        {
            throw std::runtime_error("Invalid texture image data.");
        }

        Bistro::TextureData texture;
        texture.width = static_cast<uint32_t>(images[0].width);
        texture.height = static_cast<uint32_t>(images[0].height);
        texture.mipLevels = static_cast<uint32_t>(imageCount);
        texture.format = format;
        texture.mips.reserve(imageCount);

        for (size_t mipIndex = 0; mipIndex < imageCount; ++mipIndex)
        {
            const DirectX::Image& image = images[mipIndex];
            const size_t offset = texture.pixels.size();
            texture.pixels.resize(offset + image.slicePitch);
            memcpy(texture.pixels.data() + offset, image.pixels, image.slicePitch);
            texture.mips.push_back({
                static_cast<uint32_t>(image.width),
                static_cast<uint32_t>(image.height),
                offset,
                image.rowPitch,
                image.slicePitch
            });
        }

        return texture;
    }

    DXGI_FORMAT ToSrgbFormat(DXGI_FORMAT format)
    {
        switch (format)
        {
        case DXGI_FORMAT_R8G8B8A8_UNORM:
            return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        case DXGI_FORMAT_BC1_UNORM:
            return DXGI_FORMAT_BC1_UNORM_SRGB;
        case DXGI_FORMAT_BC2_UNORM:
            return DXGI_FORMAT_BC2_UNORM_SRGB;
        case DXGI_FORMAT_BC3_UNORM:
            return DXGI_FORMAT_BC3_UNORM_SRGB;
        case DXGI_FORMAT_BC7_UNORM:
            return DXGI_FORMAT_BC7_UNORM_SRGB;
        default:
            return format;
        }
    }

    DXGI_FORMAT ToLinearFormat(DXGI_FORMAT format)
    {
        switch (format)
        {
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
            return DXGI_FORMAT_R8G8B8A8_UNORM;
        case DXGI_FORMAT_BC1_UNORM_SRGB:
            return DXGI_FORMAT_BC1_UNORM;
        case DXGI_FORMAT_BC2_UNORM_SRGB:
            return DXGI_FORMAT_BC2_UNORM;
        case DXGI_FORMAT_BC3_UNORM_SRGB:
            return DXGI_FORMAT_BC3_UNORM;
        case DXGI_FORMAT_BC7_UNORM_SRGB:
            return DXGI_FORMAT_BC7_UNORM;
        default:
            return format;
        }
    }

    bool IsVulkanPreservableFormat(DXGI_FORMAT format)
    {
        switch (format)
        {
        case DXGI_FORMAT_R8G8B8A8_UNORM:
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        case DXGI_FORMAT_BC1_UNORM:
        case DXGI_FORMAT_BC1_UNORM_SRGB:
        case DXGI_FORMAT_BC2_UNORM:
        case DXGI_FORMAT_BC2_UNORM_SRGB:
        case DXGI_FORMAT_BC3_UNORM:
        case DXGI_FORMAT_BC3_UNORM_SRGB:
        case DXGI_FORMAT_BC4_UNORM:
        case DXGI_FORMAT_BC4_SNORM:
        case DXGI_FORMAT_BC5_UNORM:
        case DXGI_FORMAT_BC5_SNORM:
        case DXGI_FORMAT_BC6H_UF16:
        case DXGI_FORMAT_BC6H_SF16:
        case DXGI_FORMAT_BC7_UNORM:
        case DXGI_FORMAT_BC7_UNORM_SRGB:
            return true;
        default:
            return false;
        }
    }
}

namespace Bistro
{
    TextureData LoadTextureRgba8(const std::wstring& path, bool srgb, const uint8_t fallback[4])
    {
        if (path.empty() || !std::filesystem::exists(path))
        {
            return MakeFallbackTexture(srgb, fallback);
        }

        DirectX::TexMetadata metadata{};
        DirectX::ScratchImage image;
        HRESULT hr = E_FAIL;
        std::filesystem::path fsPath(path);
        if (_wcsicmp(fsPath.extension().c_str(), L".dds") == 0)
        {
            hr = DirectX::LoadFromDDSFile(path.c_str(), DirectX::DDS_FLAGS_NONE, &metadata, image);
        }
        else if (_wcsicmp(fsPath.extension().c_str(), L".tga") == 0)
        {
            hr = DirectX::LoadFromTGAFile(path.c_str(), &metadata, image);
        }
        else
        {
            hr = DirectX::LoadFromWICFile(path.c_str(), DirectX::WIC_FLAGS_NONE, &metadata, image);
        }

        if (FAILED(hr))
        {
            return MakeFallbackTexture(srgb, fallback);
        }

        DirectX::ScratchImage decompressed;
        const DirectX::Image* source = image.GetImage(0, 0, 0);
        if (DirectX::IsCompressed(metadata.format))
        {
            hr = DirectX::Decompress(*source, DXGI_FORMAT_UNKNOWN, decompressed);
            if (FAILED(hr))
            {
                return MakeFallbackTexture(srgb, fallback);
            }
            source = decompressed.GetImage(0, 0, 0);
        }

        DirectX::ScratchImage converted;
        const DXGI_FORMAT outputFormat = srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
        hr = DirectX::Convert(*source, outputFormat, DirectX::TEX_FILTER_DEFAULT, 0.0f, converted);
        if (FAILED(hr))
        {
            return MakeFallbackTexture(srgb, fallback);
        }

        source = converted.GetImage(0, 0, 0);
        DirectX::ScratchImage resized;
        if (source->width > MaxTextureDimension || source->height > MaxTextureDimension)
        {
            const float scale = static_cast<float>(MaxTextureDimension) / static_cast<float>((std::max)(source->width, source->height));
            const size_t width = (std::max<size_t>)(1, static_cast<size_t>(source->width * scale));
            const size_t height = (std::max<size_t>)(1, static_cast<size_t>(source->height * scale));
            hr = DirectX::Resize(*source, width, height, DirectX::TEX_FILTER_DEFAULT, resized);
            if (FAILED(hr))
            {
                return MakeFallbackTexture(srgb, fallback);
            }
            source = resized.GetImage(0, 0, 0);
        }

        if (source->width == 0 || source->height == 0 || source->pixels == nullptr)
        {
            return MakeFallbackTexture(srgb, fallback);
        }

        DirectX::ScratchImage mipChain;
        hr = DirectX::GenerateMipMaps(*source, DirectX::TEX_FILTER_DEFAULT, 0, mipChain);
        try
        {
            if (SUCCEEDED(hr) && mipChain.GetImageCount() > 0)
            {
                return MakeTextureFromImages(mipChain.GetImages(), mipChain.GetImageCount(), outputFormat);
            }

            return MakeTextureFromImages(source, 1, outputFormat);
        }
        catch (const std::runtime_error&)
        {
            return MakeFallbackTexture(srgb, fallback);
        }
    }

    TextureData LoadTextureD3D12(const std::wstring& path, bool srgb, const uint8_t fallback[4])
    {
        if (path.empty() || !std::filesystem::exists(path))
        {
            return MakeFallbackTexture(srgb, fallback);
        }

        std::filesystem::path fsPath(path);
        if (_wcsicmp(fsPath.extension().c_str(), L".dds") != 0)
        {
            return LoadTextureRgba8(path, srgb, fallback);
        }

        DirectX::TexMetadata metadata{};
        DirectX::ScratchImage image;
        HRESULT hr = DirectX::LoadFromDDSFile(path.c_str(), DirectX::DDS_FLAGS_NONE, &metadata, image);
        if (FAILED(hr) || image.GetImageCount() == 0)
        {
            return MakeFallbackTexture(srgb, fallback);
        }

        const DXGI_FORMAT outputFormat = srgb ? ToSrgbFormat(metadata.format) : ToLinearFormat(metadata.format);
        try
        {
            return MakeTextureFromImageMemory(image.GetImages(), image.GetImageCount(), outputFormat);
        }
        catch (const std::runtime_error&)
        {
            return MakeFallbackTexture(srgb, fallback);
        }
    }

    TextureData LoadTextureVulkan(const std::wstring& path, bool srgb, const uint8_t fallback[4], bool preserveBcCompressed)
    {
        if (path.empty() || !std::filesystem::exists(path))
        {
            return MakeFallbackTexture(srgb, fallback);
        }

        std::filesystem::path fsPath(path);
        if (_wcsicmp(fsPath.extension().c_str(), L".dds") != 0)
        {
            return LoadTextureRgba8(path, srgb, fallback);
        }

        DirectX::TexMetadata metadata{};
        DirectX::ScratchImage image;
        HRESULT hr = DirectX::LoadFromDDSFile(path.c_str(), DirectX::DDS_FLAGS_NONE, &metadata, image);
        if (FAILED(hr) || image.GetImageCount() == 0)
        {
            return MakeFallbackTexture(srgb, fallback);
        }

        const DXGI_FORMAT outputFormat = srgb ? ToSrgbFormat(metadata.format) : ToLinearFormat(metadata.format);
        const bool canPreserve = IsVulkanPreservableFormat(outputFormat) && (!DirectX::IsCompressed(outputFormat) || preserveBcCompressed);
        if (!canPreserve)
        {
            return LoadTextureRgba8(path, srgb, fallback);
        }

        try
        {
            return MakeTextureFromImageMemory(image.GetImages(), image.GetImageCount(), outputFormat);
        }
        catch (const std::runtime_error&)
        {
            return MakeFallbackTexture(srgb, fallback);
        }
    }
}

