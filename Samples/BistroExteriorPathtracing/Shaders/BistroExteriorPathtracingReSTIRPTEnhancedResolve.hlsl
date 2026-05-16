#if defined(VULKAN)
#define VK_BINDING(slot, descriptorSet) [[vk::binding(slot, descriptorSet)]]
#else
#define VK_BINDING(binding, set)
#endif

struct SceneConstants
{
    row_major float4x4 inverseViewProjection;
    float4 cameraPosition;
    float4 lightDirection;
    float4 lightColor;
    float4 debugOptions;
    float4 skyColor;
    float4 skyHorizonColor;
    float4 skyZenithColor;
    float4 skyGroundColor;
    float4 skyOptions;
    float4 rayOptions;
    float4 frameOptions;
    float4 giOptions;
    float4 pathOptions;
    float4 restirOptions;
    float4 lightOptions;
    float4 environmentOptions;
    float4 denoiseOptions;
    float4 denoiseOptions2;
    float4 restirEnhancedOptions0;
    float4 restirEnhancedOptions1;
    float4 restirEnhancedOptions2;
    row_major float4x4 previousViewProjection;
};

struct RestirReservoir
{
    float4 radianceWeight;
    float4 meta;
    uint4 pathSeedsAndFlags;
    uint4 reconnectData;
};

struct EnhancedGBuffer
{
    float4 positionDepth;
    float4 normalRoughness;
    uint4 materialAndFlags;
};

VK_BINDING(3, 0) ConstantBuffer<SceneConstants> g_scene : register(b0, space0);
VK_BINDING(9, 0) RWStructuredBuffer<RestirReservoir> g_restirCurrent : register(u2, space0);
VK_BINDING(10, 0) RWStructuredBuffer<RestirReservoir> g_restirHistory : register(u3, space0);
VK_BINDING(11, 0) RWStructuredBuffer<RestirReservoir> g_restirSpatial : register(u4, space0);
VK_BINDING(15, 0) RWStructuredBuffer<EnhancedGBuffer> g_enhancedGBufferCurrent : register(u7, space0);
VK_BINDING(16, 0) RWStructuredBuffer<EnhancedGBuffer> g_enhancedGBufferHistory : register(u8, space0);
VK_BINDING(17, 0) RWStructuredBuffer<uint> g_enhancedDuplicationCurrent : register(u9, space0);
VK_BINDING(18, 0) RWStructuredBuffer<uint> g_enhancedDuplicationHistory : register(u10, space0);
VK_BINDING(19, 0) RWStructuredBuffer<uint> g_enhancedReuseTextures : register(u11, space0);
VK_BINDING(20, 0) RWStructuredBuffer<uint4> g_enhancedReplayTasks : register(u12, space0);

static const float PI = 3.14159265359f;

uint Hash(uint value)
{
    value ^= value >> 16;
    value *= 2246822519u;
    value ^= value >> 13;
    value *= 3266489917u;
    value ^= value >> 16;
    return value;
}

float Random01(inout uint state)
{
    state = Hash(state);
    return (float)(state & 0x00ffffffu) / 16777216.0f;
}

float Luminance(float3 color)
{
    return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

uint PixelIndex(uint2 pixel, uint2 dimensions)
{
    return pixel.y * dimensions.x + pixel.x;
}

RestirReservoir EmptyReservoir()
{
    RestirReservoir reservoir;
    reservoir.radianceWeight = 0.0f.xxxx;
    reservoir.meta = 0.0f.xxxx;
    reservoir.pathSeedsAndFlags = 0u.xxxx;
    reservoir.reconnectData = 0u.xxxx;
    return reservoir;
}

RestirReservoir LoadCurrentReservoir(int2 pixel, uint2 dimensions)
{
    if (pixel.x < 0 || pixel.y < 0 || pixel.x >= (int)dimensions.x || pixel.y >= (int)dimensions.y)
    {
        return EmptyReservoir();
    }
    return g_restirCurrent[PixelIndex((uint2)pixel, dimensions)];
}

RestirReservoir LoadHistoryReservoir(int2 pixel, uint2 dimensions)
{
    if (pixel.x < 0 || pixel.y < 0 || pixel.x >= (int)dimensions.x || pixel.y >= (int)dimensions.y)
    {
        return EmptyReservoir();
    }
    return g_restirHistory[PixelIndex((uint2)pixel, dimensions)];
}

EnhancedGBuffer EmptyGBuffer()
{
    EnhancedGBuffer gbuffer;
    gbuffer.positionDepth = float4(0.0f, 0.0f, 0.0f, -1.0f);
    gbuffer.normalRoughness = float4(0.0f, 0.0f, 0.0f, 1.0f);
    gbuffer.materialAndFlags = 0u.xxxx;
    return gbuffer;
}

EnhancedGBuffer LoadCurrentGBuffer(int2 pixel, uint2 dimensions)
{
    if (pixel.x < 0 || pixel.y < 0 || pixel.x >= (int)dimensions.x || pixel.y >= (int)dimensions.y)
    {
        return EmptyGBuffer();
    }
    return g_enhancedGBufferCurrent[PixelIndex((uint2)pixel, dimensions)];
}

EnhancedGBuffer LoadHistoryGBuffer(int2 pixel, uint2 dimensions)
{
    if (pixel.x < 0 || pixel.y < 0 || pixel.x >= (int)dimensions.x || pixel.y >= (int)dimensions.y)
    {
        return EmptyGBuffer();
    }
    return g_enhancedGBufferHistory[PixelIndex((uint2)pixel, dimensions)];
}

bool ValidateGBufferReuse(EnhancedGBuffer current, EnhancedGBuffer candidate)
{
    if (current.materialAndFlags.y == 0u || candidate.materialAndFlags.y == 0u || current.materialAndFlags.x != candidate.materialAndFlags.x)
    {
        return false;
    }

    float footprintC = max(g_scene.restirEnhancedOptions1.y, 0.001f);
    float alphaMin = max(g_scene.restirEnhancedOptions1.z, 0.001f);
    float currentDepth = max(current.positionDepth.w, 0.001f);
    float candidateDepth = max(candidate.positionDepth.w, 0.001f);
    float tolerance = max(footprintC * max(currentDepth, candidateDepth), 0.02f);
    if (abs(currentDepth - candidateDepth) > tolerance)
    {
        return false;
    }

    float roughness = min(current.normalRoughness.w, candidate.normalRoughness.w);
    float normalThreshold = roughness < alphaMin ? 0.96f : 0.82f;
    return dot(normalize(current.normalRoughness.xyz), normalize(candidate.normalRoughness.xyz)) >= normalThreshold;
}

void CombineReservoir(inout RestirReservoir reservoir, RestirReservoir candidate, float scale, float temporalFlag, float spatialFlag)
{
    if (candidate.meta.w <= 0.5f || candidate.radianceWeight.w <= 0.0f)
    {
        return;
    }

    candidate.radianceWeight.w *= scale;
    if (candidate.radianceWeight.w <= 0.0f)
    {
        return;
    }

    if (reservoir.meta.w <= 0.5f || reservoir.radianceWeight.w <= 0.0f)
    {
        reservoir = candidate;
        reservoir.meta.y = max(reservoir.meta.y, temporalFlag);
        reservoir.meta.z = max(reservoir.meta.z, spatialFlag);
        return;
    }

    float originalWeight = reservoir.radianceWeight.w;
    float totalWeight = originalWeight + candidate.radianceWeight.w;
    reservoir.radianceWeight.rgb = (reservoir.radianceWeight.rgb * originalWeight + candidate.radianceWeight.rgb * candidate.radianceWeight.w) / max(totalWeight, 0.0001f);
    reservoir.radianceWeight.w = min(totalWeight, max(g_scene.restirOptions.w, 0.001f));
    reservoir.meta.x = min(reservoir.meta.x + candidate.meta.x, max(g_scene.restirOptions.w, 1.0f));
    reservoir.meta.y = max(reservoir.meta.y, temporalFlag);
    reservoir.meta.z += spatialFlag;
    reservoir.meta.w = 1.0f;

    if (candidate.radianceWeight.w >= originalWeight)
    {
        reservoir.pathSeedsAndFlags = candidate.pathSeedsAndFlags;
        reservoir.reconnectData = candidate.reconnectData;
    }
}

int2 PairedSpatialOffset(uint2 pixel, uint neighborIndex, int radius, uint frameCounter)
{
    static const uint TextureSize0 = 254u;
    static const uint TextureSize1 = 230u;
    static const uint TextureSize2 = 210u;
    static const uint TextureBase1 = TextureSize0 * TextureSize0;
    static const uint TextureBase2 = TextureBase1 + TextureSize1 * TextureSize1;
    uint textureIndex = neighborIndex % 3u;
    uint textureSize = textureIndex == 0u ? TextureSize0 : (textureIndex == 1u ? TextureSize1 : TextureSize2);
    uint textureBase = textureIndex == 0u ? 0u : (textureIndex == 1u ? TextureBase1 : TextureBase2);
    uint2 coord = uint2((pixel.x + frameCounter * (17u + neighborIndex * 5u)) % textureSize, (pixel.y + frameCounter * (29u + neighborIndex * 7u)) % textureSize);
    uint packed = g_enhancedReuseTextures[textureBase + coord.y * textureSize + coord.x];
    int x = (int)(packed & 0xffffu);
    int y = (int)((packed >> 16u) & 0xffffu);
    x = x >= 32768 ? x - 65536 : x;
    y = y >= 32768 ? y - 65536 : y;
    int2 offset = int2(x, y);
    if ((frameCounter & 1u) != 0u) offset.x = -offset.x;
    if ((frameCounter & 2u) != 0u) offset.y = -offset.y;
    if ((frameCounter & 4u) != 0u) offset = offset.yx;
    return clamp(offset, int2(-radius, -radius), int2(radius, radius));
}

[numthreads(8, 8, 1)]
void RestirTemporalSpatialCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 dimensions = (uint2)round(g_scene.rayOptions.zw);
    uint2 pixel = dispatchThreadId.xy;
    if (pixel.x >= dimensions.x || pixel.y >= dimensions.y)
    {
        return;
    }

    uint pixelIndex = PixelIndex(pixel, dimensions);
    uint accumulatedFrames = (uint)round(g_scene.frameOptions.x);
    uint frameCounter = (uint)round(g_scene.frameOptions.w);
    uint spatialPasses = min((uint)round(g_scene.restirOptions.y), 3u);
    bool pairedSpatial = g_scene.restirEnhancedOptions0.x > 0.5f;
    bool replayCompaction = g_scene.restirEnhancedOptions1.x > 0.5f;

    RestirReservoir reservoir = g_restirCurrent[pixelIndex];
    EnhancedGBuffer currentGBuffer = g_enhancedGBufferCurrent[pixelIndex];
    if (accumulatedFrames > 0u && spatialPasses > 0u)
    {
        int radius = max((int)round(g_scene.restirOptions.z), 1);
        uint rngState = Hash(pixel.x * 1664525u ^ pixel.y * 1013904223u ^ frameCounter * 747796405u);
        for (uint i = 0u; i < 3u; ++i)
        {
            if (i >= spatialPasses)
            {
                break;
            }

            int2 offset = pairedSpatial
                ? PairedSpatialOffset(pixel, i, radius, frameCounter)
                : int2((int)round(Random01(rngState) * 2.0f * radius) - radius, (int)round(Random01(rngState) * 2.0f * radius) - radius);
            int2 neighborPixel = int2(pixel) + offset;
            bool currentValid = ValidateGBufferReuse(currentGBuffer, LoadCurrentGBuffer(neighborPixel, dimensions));
            bool historyValid = ValidateGBufferReuse(currentGBuffer, LoadHistoryGBuffer(neighborPixel, dimensions));
            CombineReservoir(reservoir, LoadCurrentReservoir(neighborPixel, dimensions), currentValid ? 0.60f : 0.0f, 0.0f, 1.0f);
            CombineReservoir(reservoir, LoadHistoryReservoir(neighborPixel, dimensions), historyValid ? 0.40f : 0.0f, 1.0f, 1.0f);
        }
    }

    if (reservoir.meta.w > 0.5f)
    {
        float limit = max(g_scene.restirOptions.w, 0.001f);
        float lum = Luminance(max(reservoir.radianceWeight.rgb, 0.0f.xxx));
        if (lum > limit)
        {
            reservoir.radianceWeight.rgb *= limit / max(lum, 0.0001f);
        }
    }

    g_restirSpatial[pixelIndex] = reservoir;
    uint replayNeeded = replayCompaction && reservoir.meta.w > 0.5f && (reservoir.meta.y > 0.5f || reservoir.meta.z > 0.5f) ? 1u : 0u;
    g_enhancedReplayTasks[pixelIndex] = uint4(replayNeeded, pixel.x, pixel.y, reservoir.pathSeedsAndFlags.z);
}
