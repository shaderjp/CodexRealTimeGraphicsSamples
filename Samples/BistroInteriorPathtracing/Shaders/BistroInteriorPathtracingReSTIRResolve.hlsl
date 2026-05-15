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
};

struct RestirReservoir
{
    float4 radianceWeight;
    float4 meta;
};

VK_BINDING(3, 0) ConstantBuffer<SceneConstants> g_scene : register(b0, space0);
VK_BINDING(9, 0) RWStructuredBuffer<RestirReservoir> g_restirCurrent : register(u2, space0);
VK_BINDING(10, 0) RWStructuredBuffer<RestirReservoir> g_restirHistory : register(u3, space0);
VK_BINDING(11, 0) RWStructuredBuffer<RestirReservoir> g_restirSpatial : register(u4, space0);

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
    return reservoir;
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

    float totalWeight = reservoir.radianceWeight.w + candidate.radianceWeight.w;
    reservoir.radianceWeight.rgb = (reservoir.radianceWeight.rgb * reservoir.radianceWeight.w + candidate.radianceWeight.rgb * candidate.radianceWeight.w) / max(totalWeight, 0.0001f);
    reservoir.radianceWeight.w = min(totalWeight, max(g_scene.restirOptions.w, 0.001f));
    reservoir.meta.x = min(reservoir.meta.x + candidate.meta.x, max(g_scene.restirOptions.w, 1.0f));
    reservoir.meta.y = max(reservoir.meta.y, temporalFlag);
    reservoir.meta.z += spatialFlag;
    reservoir.meta.w = 1.0f;
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
    uint spatialPasses = min((uint)round(g_scene.restirOptions.y), 4u);
    RestirReservoir reservoir = g_restirCurrent[pixelIndex];

    if (accumulatedFrames > 0u && g_scene.restirOptions.x > 0.5f)
    {
        CombineReservoir(reservoir, g_restirHistory[pixelIndex], 0.75f, 1.0f, 0.0f);
    }

    if (accumulatedFrames > 0u && spatialPasses > 0u)
    {
        int radius = max((int)round(g_scene.restirOptions.z), 1);
        uint sampleCount = min(spatialPasses * 4u, 16u);
        uint rngState = Hash(pixel.x * 1664525u ^ pixel.y * 1013904223u ^ (uint)round(g_scene.frameOptions.w) * 747796405u);

        for (uint i = 0u; i < 16u; ++i)
        {
            if (i >= sampleCount)
            {
                break;
            }

            int2 offset = int2((int)round(Random01(rngState) * 2.0f * radius) - radius, (int)round(Random01(rngState) * 2.0f * radius) - radius);
            RestirReservoir currentNeighbor = LoadCurrentReservoir(int2(pixel) + offset, dimensions);
            CombineReservoir(reservoir, currentNeighbor, 0.5f, 0.0f, 1.0f);

            RestirReservoir historyNeighbor = LoadHistoryReservoir(int2(pixel) + offset, dimensions);
            CombineReservoir(reservoir, historyNeighbor, 0.35f, 1.0f, 1.0f);
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
}
