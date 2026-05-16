#if defined(VULKAN)
#define VK_BINDING(slot, descriptorSet) [[vk::binding(slot, descriptorSet)]]
#else
#define VK_BINDING(binding, set)
#endif

#ifndef PT_RESTIR
#define PT_RESTIR 0
#endif
#ifndef PT_RESTIR_DI
#define PT_RESTIR_DI 0
#endif
#ifndef PT_RESTIR_PT_ENHANCED
#define PT_RESTIR_PT_ENHANCED 0
#endif

static const float PI = 3.14159265359f;
static const uint TextureSlotBaseColor = 0;
static const uint TextureSlotNormal = 1;
static const uint TextureSlotSpecular = 2;
static const uint TextureSlotEmissive = 3;

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

struct MeshVertex
{
    float3 position;
    float3 normal;
    float4 tangent;
    float2 texcoord;
};

struct RtMaterial
{
    float4 baseColorFactor;
    float4 emissiveFactor;
    uint textureBaseIndex;
    uint alphaMasked;
    float alphaCutoff;
    uint padding;
};

struct RtGeometryRecord
{
    uint indexOffset;
    uint indexCount;
    int baseVertex;
    uint materialIndex;
};

struct RayPayload
{
    float3 position;
    float hitT;
    float3 normal;
    uint hit;
    float3 baseColor;
    float roughness;
    float3 normalTexture;
    float metallic;
    float3 emissive;
    float ao;
    uint materialIndex;
};

struct ShadowPayload
{
    uint occluded;
};

struct SurfaceData
{
    float3 position;
    float3 normal;
    float4 tangent;
    float2 texcoord;
    float3 baseColor;
    float3 normalTexture;
    float ao;
    float roughness;
    float metallic;
    float3 emissive;
    uint materialIndex;
    RtMaterial material;
};

struct RestirReservoir
{
    float4 radianceWeight;
    float4 meta;
#if PT_RESTIR_PT_ENHANCED
    uint4 pathSeedsAndFlags;
    uint4 reconnectData;
#endif
};

#if PT_RESTIR_PT_ENHANCED
struct EnhancedGBuffer
{
    float4 positionDepth;
    float4 normalRoughness;
    uint4 materialAndFlags;
};

struct EnhancedReplayResult
{
    float4 radianceTarget;
    float4 forcedNee;
    float4 metrics;
    uint4 flags;
    uint4 debugFlags;
};
#endif

struct RtLight
{
    float4 positionArea;
    float4 edge0Type;
    float4 edge1;
    float4 radianceCdf;
};

VK_BINDING(0, 0) RaytracingAccelerationStructure g_sceneAs : register(t0, space0);
VK_BINDING(1, 0) RWTexture2D<float4> g_output : register(u0, space0);
VK_BINDING(2, 0) RWTexture2D<float4> g_accumulation : register(u1, space0);
VK_BINDING(3, 0) ConstantBuffer<SceneConstants> g_scene : register(b0, space0);
VK_BINDING(4, 0) StructuredBuffer<MeshVertex> g_vertices : register(t1, space0);
VK_BINDING(5, 0) StructuredBuffer<uint> g_indices : register(t2, space0);
VK_BINDING(6, 0) StructuredBuffer<RtGeometryRecord> g_geometries : register(t3, space0);
VK_BINDING(7, 0) StructuredBuffer<RtMaterial> g_materials : register(t4, space0);
VK_BINDING(8, 0) SamplerState g_linearSampler : register(s0, space0);
VK_BINDING(9, 0) RWStructuredBuffer<RestirReservoir> g_restirCurrent : register(u2, space0);
VK_BINDING(10, 0) RWStructuredBuffer<RestirReservoir> g_restirHistory : register(u3, space0);
VK_BINDING(11, 0) RWStructuredBuffer<RestirReservoir> g_restirSpatial : register(u4, space0);
VK_BINDING(12, 0) StructuredBuffer<RtLight> g_lights : register(t5, space0);
VK_BINDING(13, 0) RWTexture2D<float4> g_denoiseAov0 : register(u5, space0);
VK_BINDING(14, 0) RWTexture2D<float4> g_denoiseAov1 : register(u6, space0);
#if PT_RESTIR_PT_ENHANCED
VK_BINDING(15, 0) RWStructuredBuffer<EnhancedGBuffer> g_enhancedGBufferCurrent : register(u7, space0);
VK_BINDING(16, 0) RWStructuredBuffer<EnhancedGBuffer> g_enhancedGBufferHistory : register(u8, space0);
VK_BINDING(17, 0) RWStructuredBuffer<uint> g_enhancedDuplicationCurrent : register(u9, space0);
VK_BINDING(18, 0) RWStructuredBuffer<uint> g_enhancedDuplicationHistory : register(u10, space0);
VK_BINDING(19, 0) RWStructuredBuffer<uint> g_enhancedReuseTextures : register(u11, space0);
VK_BINDING(20, 0) RWStructuredBuffer<uint4> g_enhancedReplayTasks : register(u12, space0);
#endif
VK_BINDING(0, 1) Texture2D g_textures[] : register(t0, space1);

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

float2 Random2(inout uint state)
{
    return float2(Random01(state), Random01(state));
}

float3 Tonemap(float3 value)
{
    value = max(value, 0.0f.xxx);
    value = value / (1.0f.xxx + value);
    return pow(value, 1.0f / 2.2f);
}

float Luminance(float3 color)
{
    return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

float MaxComponent(float3 value)
{
    return max(value.x, max(value.y, value.z));
}

uint PixelIndex(uint2 pixel, uint2 dimensions)
{
    return pixel.y * dimensions.x + pixel.x;
}

RestirReservoir EmptyRestirReservoir()
{
    RestirReservoir reservoir;
    reservoir.radianceWeight = 0.0f.xxxx;
    reservoir.meta = 0.0f.xxxx;
#if PT_RESTIR_PT_ENHANCED
    reservoir.pathSeedsAndFlags = 0u.xxxx;
    reservoir.reconnectData = 0u.xxxx;
#endif
    return reservoir;
}

float RestirTarget(float3 radiance)
{
    return min(max(Luminance(max(radiance, 0.0f.xxx)), 0.0f), max(g_scene.restirOptions.w, 0.001f));
}

RestirReservoir MakeRestirReservoir(float3 radiance, uint sampleCount)
{
    RestirReservoir reservoir = EmptyRestirReservoir();
    float target = RestirTarget(radiance);
    reservoir.radianceWeight = float4(radiance, target);
    reservoir.meta = float4((float)sampleCount, 0.0f, 0.0f, target > 0.0001f ? 1.0f : 0.0f);
#if PT_RESTIR_PT_ENHANCED
    reservoir.pathSeedsAndFlags = 0u.xxxx;
    reservoir.reconnectData = 0u.xxxx;
#endif
    return reservoir;
}

void CombineRestirReservoir(inout RestirReservoir reservoir, RestirReservoir candidate)
{
    if (candidate.meta.w <= 0.5f || candidate.radianceWeight.w <= 0.0f)
    {
        return;
    }

    if (reservoir.meta.w <= 0.5f || reservoir.radianceWeight.w <= 0.0f)
    {
        reservoir = candidate;
        return;
    }

    float reservoirWeightBefore = reservoir.radianceWeight.w;
    float totalWeight = reservoirWeightBefore + candidate.radianceWeight.w;
    reservoir.radianceWeight.rgb = (reservoir.radianceWeight.rgb * reservoir.radianceWeight.w + candidate.radianceWeight.rgb * candidate.radianceWeight.w) / max(totalWeight, 0.0001f);
    reservoir.radianceWeight.w = min(totalWeight, max(g_scene.restirOptions.w, 0.001f));
    reservoir.meta.x = min(reservoir.meta.x + candidate.meta.x, max(g_scene.restirOptions.w, 1.0f));
    reservoir.meta.w = 1.0f;
#if PT_RESTIR_PT_ENHANCED
    if (candidate.radianceWeight.w >= reservoirWeightBefore)
    {
        reservoir.pathSeedsAndFlags = candidate.pathSeedsAndFlags;
        reservoir.reconnectData = candidate.reconnectData;
    }
#endif
}

RestirReservoir LoadRestirHistory(int2 pixel, uint2 dimensions)
{
    if (pixel.x < 0 || pixel.y < 0 || pixel.x >= (int)dimensions.x || pixel.y >= (int)dimensions.y)
    {
        return EmptyRestirReservoir();
    }
    return g_restirHistory[PixelIndex((uint2)pixel, dimensions)];
}

#if PT_RESTIR_PT_ENHANCED
EnhancedGBuffer EmptyEnhancedGBuffer()
{
    EnhancedGBuffer gbuffer;
    gbuffer.positionDepth = float4(0.0f, 0.0f, 0.0f, -1.0f);
    gbuffer.normalRoughness = float4(0.0f, 0.0f, 0.0f, 1.0f);
    gbuffer.materialAndFlags = 0u.xxxx;
    return gbuffer;
}

EnhancedGBuffer MakeEnhancedGBuffer(RayPayload payload)
{
    if (payload.hit == 0u)
    {
        return EmptyEnhancedGBuffer();
    }

    EnhancedGBuffer gbuffer;
    gbuffer.positionDepth = float4(payload.position, payload.hitT);
    gbuffer.normalRoughness = float4(normalize(payload.normal), payload.roughness);
    gbuffer.materialAndFlags = uint4(payload.materialIndex, payload.hit, 0u, 0u);
    return gbuffer;
}

EnhancedGBuffer LoadEnhancedGBufferHistory(int2 pixel, uint2 dimensions)
{
    if (pixel.x < 0 || pixel.y < 0 || pixel.x >= (int)dimensions.x || pixel.y >= (int)dimensions.y)
    {
        return EmptyEnhancedGBuffer();
    }
    return g_enhancedGBufferHistory[PixelIndex((uint2)pixel, dimensions)];
}

bool ValidateEnhancedHistory(EnhancedGBuffer current, EnhancedGBuffer history, float footprintC, float alphaMin)
{
    if (current.materialAndFlags.y == 0u || history.materialAndFlags.y == 0u)
    {
        return false;
    }
    if (current.materialAndFlags.x != history.materialAndFlags.x)
    {
        return false;
    }

    float currentDepth = max(current.positionDepth.w, 0.001f);
    float historyDepth = max(history.positionDepth.w, 0.001f);
    float depthTolerance = max(footprintC * max(currentDepth, historyDepth), 0.02f);
    if (abs(currentDepth - historyDepth) > depthTolerance)
    {
        return false;
    }

    float normalAgreement = dot(normalize(current.normalRoughness.xyz), normalize(history.normalRoughness.xyz));
    float roughness = min(current.normalRoughness.w, history.normalRoughness.w);
    float normalThreshold = roughness < alphaMin ? 0.96f : 0.82f;
    if (normalAgreement < normalThreshold)
    {
        return false;
    }

    float positionTolerance = max(depthTolerance * 2.0f, 0.05f);
    return length(current.positionDepth.xyz - history.positionDepth.xyz) <= positionTolerance;
}

bool ReprojectToHistoryPixel(float3 worldPosition, uint2 dimensions, out int2 historyPixel)
{
    float4 previousClip = mul(float4(worldPosition, 1.0f), g_scene.previousViewProjection);
    if (previousClip.w <= 0.0001f)
    {
        historyPixel = 0;
        return false;
    }

    float2 previousNdc = previousClip.xy / previousClip.w;
    if (abs(previousNdc.x) > 1.05f || abs(previousNdc.y) > 1.05f)
    {
        historyPixel = 0;
        return false;
    }

    float2 previousUv = float2(previousNdc.x * 0.5f + 0.5f, 0.5f - previousNdc.y * 0.5f);
    historyPixel = int2((int)floor(previousUv.x * (float)dimensions.x), (int)floor(previousUv.y * (float)dimensions.y));
    return historyPixel.x >= 0 && historyPixel.y >= 0 && historyPixel.x < (int)dimensions.x && historyPixel.y < (int)dimensions.y;
}

bool FindFallbackHistoryPixel(int2 pixel, uint2 dimensions, EnhancedGBuffer currentGBuffer, float footprintC, float alphaMin, out int2 historyPixel)
{
    historyPixel = pixel;
    float bestWeight = 0.0f;
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            int2 candidatePixel = pixel + int2(x, y);
            RestirReservoir candidate = LoadRestirHistory(candidatePixel, dimensions);
            EnhancedGBuffer candidateGBuffer = LoadEnhancedGBufferHistory(candidatePixel, dimensions);
            if (candidate.meta.w > 0.5f && candidate.radianceWeight.w > bestWeight && ValidateEnhancedHistory(currentGBuffer, candidateGBuffer, footprintC, alphaMin))
            {
                bestWeight = candidate.radianceWeight.w;
                historyPixel = candidatePixel;
            }
        }
    }
    return bestWeight > 0.0f;
}

int UnpackSigned16(uint value)
{
    int signedValue = (int)(value & 0xffffu);
    return signedValue >= 32768 ? signedValue - 65536 : signedValue;
}

int2 DecodeReuseOffset(uint packed)
{
    return int2(UnpackSigned16(packed), UnpackSigned16(packed >> 16u));
}

int2 PairedReuseOffset(uint2 pixel, uint neighborIndex, uint frameCounter)
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
    int2 offset = DecodeReuseOffset(packed);
    if ((frameCounter & 1u) != 0u) offset.x = -offset.x;
    if ((frameCounter & 2u) != 0u) offset.y = -offset.y;
    if ((frameCounter & 4u) != 0u) offset = offset.yx;
    return offset;
}

float PairwiseMisWeight(float candidateWeight, float otherWeight)
{
    return candidateWeight / max(candidateWeight + otherWeight, 0.0001f);
}
#endif

float2 EnvironmentUv(float3 rayDirection)
{
    float phi = atan2(rayDirection.x, rayDirection.z) + g_scene.environmentOptions.z;
    float u = frac(phi / (2.0f * PI) + 0.5f);
    float v = acos(clamp(rayDirection.y, -1.0f, 1.0f)) / PI;
    return float2(u, v);
}

float3 EvaluateEnvironmentMap(float3 rayDirection)
{
    uint environmentTextureIndex = (uint)round(g_scene.lightOptions.w);
    return g_textures[NonUniformResourceIndex(environmentTextureIndex)].SampleLevel(g_linearSampler, EnvironmentUv(rayDirection), 0.0f).rgb * g_scene.environmentOptions.y;
}

float3 EvaluateSky(float3 rayDirection)
{
    if (g_scene.environmentOptions.x > 0.5f)
    {
        return EvaluateEnvironmentMap(rayDirection);
    }

    float3 fallback = g_scene.skyColor.rgb * g_scene.skyColor.a;
    if (g_scene.skyOptions.w < 0.5f)
    {
        return fallback;
    }

    float y = rayDirection.y;
    float horizonBlend = saturate(y * 0.5f + 0.5f);
    float3 sky = lerp(g_scene.skyGroundColor.rgb, g_scene.skyHorizonColor.rgb, smoothstep(-g_scene.skyOptions.z, 0.15f, y));
    sky = lerp(sky, g_scene.skyZenithColor.rgb, horizonBlend * horizonBlend);

    float3 sunDirection = normalize(-g_scene.lightDirection.xyz);
    float sunDot = saturate(dot(rayDirection, sunDirection));
    float sunSize = max(g_scene.skyOptions.y, 0.001f);
    float sunDisk = smoothstep(cos(sunSize * 2.0f), cos(sunSize), sunDot);
    float3 sun = g_scene.lightColor.rgb * g_scene.skyOptions.x * sunDisk;
    return (sky * g_scene.skyColor.a) + sun + fallback * 0.05f;
}

float3 ClampRadiance(float3 radiance)
{
    float limit = max(g_scene.giOptions.y, 0.0f);
    if (limit <= 0.0f)
    {
        return radiance;
    }
    float lum = Luminance(radiance);
    if (lum > limit)
    {
        radiance *= limit / max(lum, 0.0001f);
    }
    return radiance;
}

float3 ApplyTemporalClamp(float3 current, float3 history, uint accumulatedFrames)
{
    if (accumulatedFrames == 0u)
    {
        return current;
    }
    float clampScale = max(g_scene.giOptions.z, 0.0f);
    float clampMin = max(g_scene.giOptions.w, 0.0f);
    float3 delta = max(abs(history) * clampScale, clampMin.xxx);
    return clamp(current, history - delta, history + delta);
}

float3 Barycentric3(float2 barycentrics)
{
    return float3(1.0f - barycentrics.x - barycentrics.y, barycentrics.x, barycentrics.y);
}

float3 Interpolate3(float3 a, float3 b, float3 c, float3 bary)
{
    return a * bary.x + b * bary.y + c * bary.z;
}

float2 Interpolate2(float2 a, float2 b, float2 c, float3 bary)
{
    return a * bary.x + b * bary.y + c * bary.z;
}

float4 Interpolate4(float4 a, float4 b, float4 c, float3 bary)
{
    return a * bary.x + b * bary.y + c * bary.z;
}

uint3 LoadTriangleIndices(RtGeometryRecord geometry, uint primitiveIndex)
{
    uint index = geometry.indexOffset + primitiveIndex * 3u;
    return uint3(g_indices[index + 0], g_indices[index + 1], g_indices[index + 2]);
}

SurfaceData LoadSurface(uint geometryIndex, uint primitiveIndex, float2 barycentrics)
{
    RtGeometryRecord geometry = g_geometries[geometryIndex];
    uint3 tri = LoadTriangleIndices(geometry, primitiveIndex);
    MeshVertex v0 = g_vertices[tri.x];
    MeshVertex v1 = g_vertices[tri.y];
    MeshVertex v2 = g_vertices[tri.z];
    float3 bary = Barycentric3(barycentrics);

    SurfaceData surface;
    surface.materialIndex = geometry.materialIndex;
    surface.position = Interpolate3(v0.position, v1.position, v2.position, bary);
    surface.normal = normalize(Interpolate3(v0.normal, v1.normal, v2.normal, bary));
    surface.tangent = Interpolate4(v0.tangent, v1.tangent, v2.tangent, bary);
    surface.texcoord = Interpolate2(v0.texcoord, v1.texcoord, v2.texcoord, bary);
    surface.material = g_materials[geometry.materialIndex];

    float4 baseSample = g_textures[NonUniformResourceIndex(surface.material.textureBaseIndex + TextureSlotBaseColor)].SampleLevel(g_linearSampler, surface.texcoord, 0.0f);
    float4 specularPacked = g_textures[NonUniformResourceIndex(surface.material.textureBaseIndex + TextureSlotSpecular)].SampleLevel(g_linearSampler, surface.texcoord, 0.0f);
    float3 normalTexture = g_textures[NonUniformResourceIndex(surface.material.textureBaseIndex + TextureSlotNormal)].SampleLevel(g_linearSampler, surface.texcoord, 0.0f).xyz;
    surface.baseColor = baseSample.rgb * surface.material.baseColorFactor.rgb;
    surface.ao = specularPacked.r;
    surface.roughness = clamp(specularPacked.g, 0.04f, 1.0f);
    surface.metallic = saturate(specularPacked.b);
    surface.emissive = g_textures[NonUniformResourceIndex(surface.material.textureBaseIndex + TextureSlotEmissive)].SampleLevel(g_linearSampler, surface.texcoord, 0.0f).rgb * surface.material.emissiveFactor.rgb * surface.material.emissiveFactor.a * g_scene.lightOptions.y;
    surface.normalTexture = normalTexture;

    float2 normalXY = normalTexture.xy * 2.0f - 1.0f;
    normalXY.y *= g_scene.debugOptions.y > 0.5f ? -1.0f : 1.0f;
    float3 normalSample = float3(normalXY, sqrt(saturate(1.0f - dot(normalXY, normalXY))));
    float3 n = surface.normal;
    float3 t = normalize(surface.tangent.xyz - n * dot(n, surface.tangent.xyz));
    float3 b = normalize(cross(n, t) * surface.tangent.w);
    surface.normal = normalize(normalSample.x * t + normalSample.y * b + normalSample.z * n);
    return surface;
}

bool IsAlphaTransparent(uint geometryIndex, uint primitiveIndex, float2 barycentrics)
{
    RtGeometryRecord geometry = g_geometries[geometryIndex];
    RtMaterial material = g_materials[geometry.materialIndex];
    if (material.alphaMasked == 0)
    {
        return false;
    }

    uint3 tri = LoadTriangleIndices(geometry, primitiveIndex);
    MeshVertex v0 = g_vertices[tri.x];
    MeshVertex v1 = g_vertices[tri.y];
    MeshVertex v2 = g_vertices[tri.z];
    float3 bary = Barycentric3(barycentrics);
    float2 texcoord = Interpolate2(v0.texcoord, v1.texcoord, v2.texcoord, bary);
    float alpha = g_textures[NonUniformResourceIndex(material.textureBaseIndex + TextureSlotBaseColor)].SampleLevel(g_linearSampler, texcoord, 0.0f).a * material.baseColorFactor.a;
    return alpha < material.alphaCutoff;
}

void BuildBasis(float3 normal, out float3 tangent, out float3 bitangent)
{
    float3 up = abs(normal.z) < 0.999f ? float3(0.0f, 0.0f, 1.0f) : float3(1.0f, 0.0f, 0.0f);
    tangent = normalize(cross(up, normal));
    bitangent = cross(normal, tangent);
}

float3 TangentToWorld(float3 localDirection, float3 normal)
{
    float3 tangent;
    float3 bitangent;
    BuildBasis(normal, tangent, bitangent);
    return normalize(localDirection.x * tangent + localDirection.y * bitangent + localDirection.z * normal);
}

float3 SampleCosineHemisphere(float2 sample, float3 normal)
{
    float phi = 2.0f * PI * sample.x;
    float r = sqrt(sample.y);
    float x = r * cos(phi);
    float y = r * sin(phi);
    float z = sqrt(max(0.0f, 1.0f - sample.y));
    return TangentToWorld(float3(x, y, z), normal);
}

float3 SampleGGXDirection(float2 sample, float roughness, float3 normal, float3 incomingDirection)
{
    float a = max(roughness * roughness, 0.001f);
    float phi = 2.0f * PI * sample.x;
    float cosTheta = sqrt((1.0f - sample.y) / max(1.0f + (a * a - 1.0f) * sample.y, 0.0001f));
    float sinTheta = sqrt(max(0.0f, 1.0f - cosTheta * cosTheta));
    float3 halfVector = TangentToWorld(float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta), normal);
    float3 direction = reflect(incomingDirection, halfVector);
    return dot(direction, normal) > 0.001f ? normalize(direction) : SampleCosineHemisphere(sample.yx, normal);
}

float3 FresnelSchlick(float cosTheta, float3 f0)
{
    return f0 + (1.0f - saturate(f0)) * pow(1.0f - saturate(cosTheta), 5.0f);
}

float DistributionGGX(float3 normal, float3 halfVector, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float nDotH = saturate(dot(normal, halfVector));
    float nDotH2 = nDotH * nDotH;
    float denominator = nDotH2 * (a2 - 1.0f) + 1.0f;
    return a2 / max(PI * denominator * denominator, 0.0001f);
}

float GeometrySchlickGGX(float nDotV, float roughness)
{
    float r = roughness + 1.0f;
    float k = (r * r) / 8.0f;
    return nDotV / max(nDotV * (1.0f - k) + k, 0.0001f);
}

float GeometrySmith(float3 normal, float3 viewDirection, float3 lightDirection, float roughness)
{
    float nDotV = saturate(dot(normal, viewDirection));
    float nDotL = saturate(dot(normal, lightDirection));
    return GeometrySchlickGGX(nDotV, roughness) * GeometrySchlickGGX(nDotL, roughness);
}

float3 EvaluateBsdf(SurfaceData surface, float3 viewDirection, float3 lightDirection)
{
    float nDotL = saturate(dot(surface.normal, lightDirection));
    if (nDotL <= 0.0f)
    {
        return 0.0f.xxx;
    }

    float3 halfVector = normalize(viewDirection + lightDirection);
    float3 f0 = lerp(0.04f.xxx, surface.baseColor, surface.metallic);
    float3 fresnel = FresnelSchlick(saturate(dot(halfVector, viewDirection)), f0);
    float distribution = DistributionGGX(surface.normal, halfVector, surface.roughness);
    float geometry = GeometrySmith(surface.normal, viewDirection, lightDirection, surface.roughness);
    float nDotV = saturate(dot(surface.normal, viewDirection));
    float3 specular = distribution * geometry * fresnel / max(4.0f * nDotV * nDotL, 0.0001f);
    float3 diffuse = (1.0f.xxx - fresnel) * (1.0f - surface.metallic) * surface.baseColor / PI;
    return (diffuse + specular) * nDotL;
}

float TraceVisibilityRay(float3 origin, float3 normal, float3 direction, float tMax)
{
    ShadowPayload payload;
    payload.occluded = 1;

    RayDesc ray;
    ray.Origin = origin + normal * g_scene.rayOptions.x;
    ray.Direction = direction;
    ray.TMin = g_scene.rayOptions.x;
    ray.TMax = tMax;
    TraceRay(g_sceneAs, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER, 0xff, 1, 0, 1, ray, payload);
    return payload.occluded == 0 ? 1.0f : 0.0f;
}

float3 EvaluateSunNEE(SurfaceData surface, float3 viewDirection)
{
    if (g_scene.debugOptions.z < 0.5f)
    {
        return 0.0f.xxx;
    }

    float3 lightDirection = normalize(-g_scene.lightDirection.xyz);
    float visibility = TraceVisibilityRay(surface.position, surface.normal, lightDirection, g_scene.rayOptions.y);
    float nDotL = saturate(dot(surface.normal, lightDirection));
    if (visibility <= 0.0f || nDotL <= 0.0f)
    {
        return 0.0f.xxx;
    }

    float3 radiance = g_scene.lightColor.rgb * g_scene.lightColor.a;
    return EvaluateBsdf(surface, viewDirection, lightDirection) * radiance * visibility;
}

float3 EvaluateSkyNEE(SurfaceData surface, inout uint rngState)
{
    if (g_scene.debugOptions.w < 0.5f)
    {
        return 0.0f.xxx;
    }

    float3 direction = SampleCosineHemisphere(Random2(rngState), surface.normal);
    float visibility = TraceVisibilityRay(surface.position, surface.normal, direction, g_scene.rayOptions.y);
    float3 sky = EvaluateSky(direction);
    return sky * surface.baseColor * surface.ao * visibility;
}

float PreviousLightCdf(uint lightIndex)
{
    return lightIndex == 0u ? 0.0f : g_lights[lightIndex - 1u].radianceCdf.w;
}

uint SelectLightIndex(float sample, uint lightCount)
{
    uint low = 0u;
    uint high = lightCount;
    for (uint i = 0u; i < 16u; ++i)
    {
        if (low >= high)
        {
            break;
        }

        uint mid = (low + high) >> 1u;
        if (sample <= g_lights[mid].radianceCdf.w)
        {
            high = mid;
        }
        else
        {
            low = mid + 1u;
        }
    }
    return min(low, lightCount - 1u);
}

float3 EvaluateAreaLightSampleNEE(SurfaceData surface, float3 viewDirection, float lightSample, float2 uv, out uint sampledLightIndex)
{
    sampledLightIndex = 0u;
    uint lightCount = (uint)round(g_scene.lightOptions.x);
    if (lightCount == 0u)
    {
        return 0.0f.xxx;
    }

    uint lightIndex = SelectLightIndex(lightSample, lightCount);
    sampledLightIndex = lightIndex;
    RtLight light = g_lights[lightIndex];
    float3 edge0 = light.edge0Type.xyz;
    float3 edge1 = light.edge1.xyz;
    float3 lightPosition = light.positionArea.xyz + edge0 * uv.x + edge1 * uv.y;
    float3 toLight = lightPosition - surface.position;
    float distanceSquared = max(dot(toLight, toLight), 0.0001f);
    float distanceToLight = sqrt(distanceSquared);
    float3 lightDirection = toLight / distanceToLight;
    float nDotL = saturate(dot(surface.normal, lightDirection));
    if (nDotL <= 0.0f)
    {
        return 0.0f.xxx;
    }

    float3 lightNormal = normalize(cross(edge0, edge1));
    float lightCos = abs(dot(lightNormal, -lightDirection));
    if (lightCos <= 0.0001f)
    {
        return 0.0f.xxx;
    }

    float selectionPdf = max(light.radianceCdf.w - PreviousLightCdf(lightIndex), 0.0001f);
    float areaPdf = selectionPdf / max(light.positionArea.w, 0.0001f);
    float solidAnglePdf = max(areaPdf * distanceSquared / lightCos, 0.0001f);
    float visibility = TraceVisibilityRay(surface.position, surface.normal, lightDirection, distanceToLight - g_scene.rayOptions.x * 2.0f);
    if (visibility <= 0.0f)
    {
        return 0.0f.xxx;
    }

    float type = light.edge0Type.w;
    float intensity = type < 0.5f ? g_scene.lightOptions.y : g_scene.lightOptions.z;
    float3 radiance = light.radianceCdf.rgb * intensity;
    return EvaluateBsdf(surface, viewDirection, lightDirection) * radiance * visibility / solidAnglePdf;
}

float3 EvaluateAreaLightNEE(SurfaceData surface, float3 viewDirection, inout uint rngState)
{
    uint sampledLightIndex;
    return EvaluateAreaLightSampleNEE(surface, viewDirection, Random01(rngState), Random2(rngState), sampledLightIndex);
}

#if PT_RESTIR_PT_ENHANCED
float3 EvaluateForcedNeeReconnection(SurfaceData surface, float3 viewDirection, inout uint rngState, out uint forcedFlags)
{
    forcedFlags = 1u;
    bool sunEnabled = g_scene.debugOptions.z > 0.5f;
    uint lightCount = (uint)round(g_scene.lightOptions.x);
    float choose = Random01(rngState);

    if (sunEnabled && (choose < 0.5f || lightCount == 0u))
    {
        forcedFlags |= 2u;
        float3 contribution = EvaluateSunNEE(surface, viewDirection);
        forcedFlags |= RestirTarget(contribution) > 0.0001f ? 8u : 16u;
        return contribution;
    }

    if (lightCount > 0u)
    {
        forcedFlags |= 4u;
        uint sampledLightIndex;
        float3 contribution = EvaluateAreaLightSampleNEE(surface, viewDirection, Random01(rngState), Random2(rngState), sampledLightIndex);
        forcedFlags |= RestirTarget(contribution) > 0.0001f ? 8u : 16u;
        return contribution;
    }

    forcedFlags |= 32u;
    return 0.0f.xxx;
}
#endif

RayPayload EmptyPayload()
{
    RayPayload payload;
    payload.position = 0.0f.xxx;
    payload.hitT = 0.0f;
    payload.normal = 0.0f.xxx;
    payload.hit = 0u;
    payload.baseColor = 0.0f.xxx;
    payload.roughness = 1.0f;
    payload.normalTexture = 0.5f.xxx;
    payload.metallic = 0.0f;
    payload.emissive = 0.0f.xxx;
    payload.ao = 1.0f;
    payload.materialIndex = 0u;
    return payload;
}

SurfaceData SurfaceFromPayload(RayPayload payload)
{
    SurfaceData surface;
    surface.position = payload.position;
    surface.normal = payload.normal;
    surface.tangent = float4(1.0f, 0.0f, 0.0f, 1.0f);
    surface.texcoord = 0.0f.xx;
    surface.baseColor = payload.baseColor;
    surface.normalTexture = payload.normalTexture;
    surface.ao = payload.ao;
    surface.roughness = payload.roughness;
    surface.metallic = payload.metallic;
    surface.emissive = payload.emissive;
    surface.materialIndex = payload.materialIndex;
    return surface;
}

float3 GenerateCameraDirection(uint2 pixel)
{
    uint2 dimensions = DispatchRaysDimensions().xy;
    float2 uv = (float2(pixel) + 0.5f.xx) / float2(dimensions);
    float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
    float4 nearPoint = mul(float4(ndc, 0.0f, 1.0f), g_scene.inverseViewProjection);
    float4 farPoint = mul(float4(ndc, 1.0f, 1.0f), g_scene.inverseViewProjection);
    nearPoint.xyz /= nearPoint.w;
    farPoint.xyz /= farPoint.w;
    return normalize(farPoint.xyz - nearPoint.xyz);
}

float3 TracePathSample(uint2 pixel, uint sampleIndex, bool useRussianRoulette, out RayPayload firstHit, out float3 directNee, out float3 indirect, out uint bounceCount)
{
    uint rngState = Hash(pixel.x * 1973u ^ pixel.y * 9277u ^ sampleIndex * 26699u ^ 0x9e3779b9u);
    float3 rayOrigin = g_scene.cameraPosition.xyz;
    float3 rayDirection = GenerateCameraDirection(pixel);
    float3 throughput = 1.0f.xxx;
    float3 radiance = 0.0f.xxx;
    directNee = 0.0f.xxx;
    indirect = 0.0f.xxx;
    bounceCount = 0u;
    firstHit = EmptyPayload();

    uint maxBounces = clamp((uint)round(g_scene.pathOptions.x), 1u, 8u);
    uint minBounces = min((uint)round(g_scene.pathOptions.y), maxBounces);

    for (uint bounce = 0u; bounce < 8u; ++bounce)
    {
        if (bounce >= maxBounces)
        {
            break;
        }

        RayPayload payload = EmptyPayload();
        RayDesc ray;
        ray.Origin = rayOrigin;
        ray.Direction = rayDirection;
        ray.TMin = g_scene.rayOptions.x;
        ray.TMax = g_scene.rayOptions.y;
        TraceRay(g_sceneAs, RAY_FLAG_NONE, 0xff, 0, 0, 0, ray, payload);

        if (payload.hit == 0u)
        {
            float3 sky = EvaluateSky(rayDirection);
            radiance += throughput * sky;
            if (bounce > 0u)
            {
                indirect += throughput * sky;
            }
            break;
        }

        SurfaceData surface = SurfaceFromPayload(payload);
        surface.normal = dot(surface.normal, -rayDirection) > 0.0f ? surface.normal : -surface.normal;
        if (bounce == 0u)
        {
            firstHit = payload;
            firstHit.normal = surface.normal;
        }

        radiance += throughput * surface.emissive;
        float3 viewDirection = normalize(-rayDirection);
        float3 sun = EvaluateSunNEE(surface, viewDirection);
        float3 skyNee = EvaluateSkyNEE(surface, rngState);
        float3 areaNee = EvaluateAreaLightNEE(surface, viewDirection, rngState);
        float3 nee = sun + skyNee + areaNee;
        radiance += throughput * nee;
        if (bounce == 0u)
        {
            directNee += sun + areaNee;
            indirect += skyNee;
        }
        else
        {
            indirect += throughput * nee;
        }

        float3 f0 = lerp(0.04f.xxx, surface.baseColor, surface.metallic);
        float fresnelWeight = saturate(MaxComponent(FresnelSchlick(saturate(dot(surface.normal, viewDirection)), f0)));
        float specularProbability = saturate(0.2f + 0.55f * (1.0f - surface.roughness) + 0.25f * surface.metallic + 0.25f * fresnelWeight);
        float chooseSpecular = Random01(rngState);
        float3 nextDirection;
        float3 bsdfWeight;

        if (chooseSpecular < specularProbability)
        {
            nextDirection = SampleGGXDirection(Random2(rngState), surface.roughness, surface.normal, rayDirection);
            float3 fresnel = FresnelSchlick(saturate(dot(surface.normal, nextDirection)), f0);
            bsdfWeight = fresnel / max(specularProbability, 0.05f);
        }
        else
        {
            nextDirection = SampleCosineHemisphere(Random2(rngState), surface.normal);
            bsdfWeight = surface.baseColor * (1.0f - surface.metallic) / max(1.0f - specularProbability, 0.05f);
        }

        throughput *= bsdfWeight;
        throughput = min(throughput, 16.0f.xxx);
        rayOrigin = surface.position + surface.normal * g_scene.rayOptions.x;
        rayDirection = normalize(nextDirection);
        bounceCount = bounce + 1u;

        if (useRussianRoulette && bounce + 1u >= minBounces)
        {
            float continueProbability = clamp(MaxComponent(throughput), 0.05f, 0.95f);
            if (Random01(rngState) > continueProbability)
            {
                break;
            }
            throughput /= continueProbability;
        }
    }

    return ClampRadiance(radiance);
}

#if PT_RESTIR_PT_ENHANCED
EnhancedReplayResult EmptyEnhancedReplayResult()
{
    EnhancedReplayResult result;
    result.radianceTarget = 0.0f.xxxx;
    result.forcedNee = 0.0f.xxxx;
    result.metrics = 0.0f.xxxx;
    result.flags = 0u.xxxx;
    result.debugFlags = 0u.xxxx;
    return result;
}

EnhancedReplayResult ReplayPathSample(uint2 pixel, RestirReservoir reservoir)
{
    EnhancedReplayResult result = EmptyEnhancedReplayResult();
    if (reservoir.meta.w <= 0.5f)
    {
        return result;
    }

    RayPayload replayFirstHit;
    float3 replayDirectNee;
    float3 replayIndirect;
    uint replayBounceCount;
    float3 replayRadiance = TracePathSample(pixel, reservoir.pathSeedsAndFlags.x, false, replayFirstHit, replayDirectNee, replayIndirect, replayBounceCount);
    float replayTarget = RestirTarget(replayRadiance);
    float sourceTarget = max(asfloat(reservoir.reconnectData.z), 0.0001f);

    bool storedHit = (reservoir.pathSeedsAndFlags.z & 0xffu) != 0u;
    bool replayHit = replayFirstHit.hit != 0u;
    bool hitValid = storedHit == replayHit;

    float storedHitT = asfloat(reservoir.reconnectData.x);
    float hitError = storedHit ? abs(replayFirstHit.hitT - storedHitT) : 0.0f;
    float hitTolerance = max(max(g_scene.restirEnhancedOptions1.y, 0.001f) * max(max(storedHitT, replayFirstHit.hitT), 0.001f), 0.02f);
    bool depthValid = !storedHit || hitError <= hitTolerance;

    float storedRoughness = asfloat(reservoir.reconnectData.y);
    float roughnessError = storedHit ? abs(replayFirstHit.roughness - storedRoughness) : 0.0f;
    bool roughnessValid = !storedHit || roughnessError <= 0.08f;
    bool radianceValid = replayTarget > 0.0001f;
    bool valid = hitValid && depthValid && roughnessValid && radianceValid;

    uint forcedFlags = 0u;
    float3 forcedNee = 0.0f.xxx;
    if (g_scene.restirEnhancedOptions2.x > 0.5f)
    {
        forcedFlags = 1u;
        if (valid && replayHit)
        {
            SurfaceData replaySurface = SurfaceFromPayload(replayFirstHit);
            replaySurface.normal = replayFirstHit.normal;
            float3 viewDirection = normalize(g_scene.cameraPosition.xyz - replaySurface.position);
            uint forcedState = Hash(reservoir.pathSeedsAndFlags.y ^ pixel.x * 2246822519u ^ pixel.y * 3266489917u ^ 0x4f1bbcddu);
            forcedNee = EvaluateForcedNeeReconnection(replaySurface, viewDirection, forcedState, forcedFlags);
        }
        else
        {
            forcedFlags |= 32u;
        }
    }

    float3 shiftedRadiance = replayRadiance + forcedNee;
    float shiftedTarget = RestirTarget(shiftedRadiance);
    result.radianceTarget = float4(shiftedRadiance, shiftedTarget);
    result.forcedNee = float4(forcedNee, RestirTarget(forcedNee));
    result.metrics = float4(sourceTarget, shiftedTarget / sourceTarget, hitError / hitTolerance, roughnessError);
    result.flags = uint4(valid ? 1u : 0u, hitValid ? 1u : 0u, depthValid ? 1u : 0u, roughnessValid ? 1u : 0u);
    result.debugFlags = uint4(forcedFlags, 0u, 0u, 0u);
    return result;
}

bool ReplayShiftCandidate(uint2 pixel, inout RestirReservoir candidate, out EnhancedReplayResult replay, out uint rejectReason)
{
    replay = EmptyEnhancedReplayResult();
    rejectReason = 0u;
    if (candidate.meta.w <= 0.5f || candidate.radianceWeight.w <= 0.0f)
    {
        rejectReason = 1u;
        candidate.radianceWeight.w = 0.0f;
        return false;
    }

    replay = ReplayPathSample(pixel, candidate);
    if (replay.flags.x == 0u)
    {
        rejectReason = replay.flags.y == 0u ? 2u : (replay.flags.z == 0u ? 3u : (replay.flags.w == 0u ? 4u : 5u));
        candidate.radianceWeight.w = 0.0f;
        return false;
    }

    float targetRatio = clamp(replay.metrics.y, 0.05f, 8.0f);
    candidate.radianceWeight.rgb = replay.radianceTarget.rgb;
    candidate.radianceWeight.w = min(candidate.radianceWeight.w * targetRatio, max(g_scene.restirOptions.w, 0.001f));
    candidate.reconnectData.z = asuint(replay.radianceTarget.w);
    candidate.reconnectData.w = (candidate.reconnectData.w & 0xffu) | ((replay.debugFlags.x & 0x00ffffffu) << 8u);
    return candidate.radianceWeight.w > 0.0f;
}
#endif

[shader("raygeneration")]
void RayGen()
{
    uint2 pixel = DispatchRaysIndex().xy;
    uint accumulatedFrames = (uint)round(g_scene.frameOptions.x);
    uint maxAccumulatedFrames = max((uint)round(g_scene.frameOptions.y), 1u);
    uint frameCounter = (uint)round(g_scene.frameOptions.w);
    uint samplesPerFrame = clamp((uint)round(g_scene.giOptions.x), 1u, 8u);
    uint candidateMultiplier = (PT_RESTIR || PT_RESTIR_DI) ? clamp((uint)round(g_scene.pathOptions.z), 1u, 4u) : 1u;
#if PT_RESTIR_PT_ENHANCED
    candidateMultiplier = clamp((uint)round(g_scene.restirEnhancedOptions1.w), 1u, 32u);
#endif
    uint totalSamples = min(samplesPerFrame * candidateMultiplier, 32u);
    uint firstSampleIndex = frameCounter * totalSamples;

    RayPayload firstHit = EmptyPayload();
    float3 color = 0.0f.xxx;
    float3 directNee = 0.0f.xxx;
    float3 indirect = 0.0f.xxx;
    uint bounceCount = 0u;
#if PT_RESTIR_PT_ENHANCED
    uint selectionState = Hash(pixel.x * 915488749u ^ pixel.y * 652447297u ^ frameCounter * 1103515245u);
    float selectedWeightSum = 0.0f;
    float3 selectedColor = 0.0f.xxx;
    float3 selectedDirectNee = 0.0f.xxx;
    float3 selectedIndirect = 0.0f.xxx;
    RayPayload selectedFirstHit = EmptyPayload();
    uint selectedBounceCount = 0u;
    uint selectedSampleIndex = firstSampleIndex;
    bool useInitialRussianRoulette = g_scene.restirEnhancedOptions0.w > 0.5f;
#endif

    for (uint i = 0u; i < 32u; ++i)
    {
        if (i >= totalSamples)
        {
            break;
        }
        RayPayload sampleFirstHit;
        float3 sampleDirect;
        float3 sampleIndirect;
        uint sampleBounces;
        float3 sampleColor = TracePathSample(pixel, firstSampleIndex + i,
#if PT_RESTIR_PT_ENHANCED
            useInitialRussianRoulette,
#else
            true,
#endif
            sampleFirstHit, sampleDirect, sampleIndirect, sampleBounces);
        color += sampleColor;
        if (i == 0u)
        {
            firstHit = sampleFirstHit;
        }
#if PT_RESTIR_PT_ENHANCED
        float sampleWeight = max(RestirTarget(sampleColor), 0.0001f);
        selectedWeightSum += sampleWeight;
        if (Random01(selectionState) < sampleWeight / max(selectedWeightSum, 0.0001f))
        {
            selectedColor = sampleColor;
            selectedDirectNee = sampleDirect;
            selectedIndirect = sampleIndirect;
            selectedFirstHit = sampleFirstHit;
            selectedBounceCount = sampleBounces;
            selectedSampleIndex = firstSampleIndex + i;
        }
#endif
        directNee += sampleDirect;
        indirect += sampleIndirect;
        bounceCount += sampleBounces;
    }

    color /= (float)totalSamples;
    directNee /= (float)totalSamples;
    indirect /= (float)totalSamples;
    float averageBounces = (float)bounceCount / (float)max(totalSamples, 1u);
    float3 skyRadiance = EvaluateSky(GenerateCameraDirection(pixel));

    uint debugMode = (uint)round(g_scene.debugOptions.x);
    float3 debugColor = color;
    if (debugMode == 1u) debugColor = firstHit.baseColor;
    if (debugMode == 2u) debugColor = firstHit.hit != 0u ? firstHit.normal * 0.5f + 0.5f : 0.0f.xxx;
    if (debugMode == 3u) debugColor = firstHit.normalTexture;
    if (debugMode == 4u) debugColor = firstHit.roughness.xxx;
    if (debugMode == 5u) debugColor = firstHit.metallic.xxx;
    if (debugMode == 6u) debugColor = firstHit.emissive;
    if (debugMode == 7u) debugColor = firstHit.hit != 0u ? saturate(firstHit.hitT / 250.0f).xxx : 0.0f.xxx;
    if (debugMode == 8u) debugColor = directNee;
    if (debugMode == 9u) debugColor = indirect;
    if (debugMode == 10u) debugColor = (averageBounces / max(g_scene.pathOptions.x, 1.0f)).xxx;
    if (debugMode == 12u) debugColor = skyRadiance;
#if PT_RESTIR
    uint2 dimensions = DispatchRaysDimensions().xy;
    uint pixelIndex = PixelIndex(pixel, dimensions);
    uint restirSpatialPasses = min((uint)round(g_scene.restirOptions.y), 4u);
    RestirReservoir reservoir = MakeRestirReservoir(indirect, totalSamples);
    uint restirState = Hash(pixel.x * 4099u ^ pixel.y * 131u ^ frameCounter * 65537u ^ 0x51ed270bu);

    if (accumulatedFrames > 0u && g_scene.restirOptions.x > 0.5f)
    {
        RestirReservoir history = g_restirHistory[pixelIndex];
        float historyWeightScale = saturate((float)accumulatedFrames / 8.0f);
        history.radianceWeight.w *= historyWeightScale;
        CombineRestirReservoir(reservoir, history);
        reservoir.meta.y = max(reservoir.meta.y, history.meta.w);
    }

    if (accumulatedFrames > 0u && restirSpatialPasses > 0u)
    {
        int radius = max((int)round(g_scene.restirOptions.z), 1);
        uint spatialSamples = min(restirSpatialPasses * 2u, 8u);
        for (uint i = 0u; i < 8u; ++i)
        {
            if (i >= spatialSamples)
            {
                break;
            }
            int2 offset = int2((int)round(Random01(restirState) * 2.0f * radius) - radius, (int)round(Random01(restirState) * 2.0f * radius) - radius);
            RestirReservoir neighbor = LoadRestirHistory(int2(pixel) + offset, dimensions);
            neighbor.radianceWeight.w *= 0.5f;
            CombineRestirReservoir(reservoir, neighbor);
            reservoir.meta.z += neighbor.meta.w;
        }
    }

    if (reservoir.meta.w > 0.5f)
    {
        float3 restirIndirect = ClampRadiance(reservoir.radianceWeight.rgb);
        color += restirIndirect - indirect;
        indirect = restirIndirect;
    }
    g_restirCurrent[pixelIndex] = reservoir;

    if (debugMode == 0u) debugColor = color;
    if (debugMode == 9u) debugColor = indirect;
    if (debugMode == 13u) debugColor = saturate(reservoir.radianceWeight.w / max(g_scene.restirOptions.w, 0.001f)).xxx;
    if (debugMode == 14u) debugColor = reservoir.meta.y.xxx;
    if (debugMode == 15u) debugColor = saturate(reservoir.meta.z / max(restirSpatialPasses * 2.0f, 1.0f)).xxx;
#endif
#if PT_RESTIR_DI
    uint2 dimensions = DispatchRaysDimensions().xy;
    uint pixelIndex = PixelIndex(pixel, dimensions);
    uint restirSpatialPasses = min((uint)round(g_scene.restirOptions.y), 4u);
    RestirReservoir reservoir = MakeRestirReservoir(directNee, totalSamples);
    uint restirState = Hash(pixel.x * 15485863u ^ pixel.y * 32452843u ^ frameCounter * 49979687u ^ 0xa511e9b3u);

    if (accumulatedFrames > 0u && g_scene.restirOptions.x > 0.5f)
    {
        RestirReservoir history = g_restirHistory[pixelIndex];
        float historyWeightScale = saturate((float)accumulatedFrames / 6.0f);
        history.radianceWeight.w *= historyWeightScale;
        CombineRestirReservoir(reservoir, history);
        reservoir.meta.y = max(reservoir.meta.y, history.meta.w);
    }

    if (accumulatedFrames > 0u && restirSpatialPasses > 0u)
    {
        int radius = max((int)round(g_scene.restirOptions.z), 1);
        uint spatialSamples = min(restirSpatialPasses * 4u, 16u);
        for (uint i = 0u; i < 16u; ++i)
        {
            if (i >= spatialSamples)
            {
                break;
            }
            int2 offset = int2((int)round(Random01(restirState) * 2.0f * radius) - radius, (int)round(Random01(restirState) * 2.0f * radius) - radius);
            RestirReservoir neighbor = LoadRestirHistory(int2(pixel) + offset, dimensions);
            neighbor.radianceWeight.w *= 0.5f;
            CombineRestirReservoir(reservoir, neighbor);
            reservoir.meta.z += neighbor.meta.w;
        }
    }

    if (reservoir.meta.w > 0.5f)
    {
        float3 restirDirect = ClampRadiance(reservoir.radianceWeight.rgb);
        color += restirDirect - directNee;
        directNee = restirDirect;
    }
    g_restirCurrent[pixelIndex] = reservoir;

    if (debugMode == 0u) debugColor = color;
    if (debugMode == 8u) debugColor = directNee;
    if (debugMode == 13u) debugColor = saturate(reservoir.radianceWeight.w / max(g_scene.restirOptions.w, 0.001f)).xxx;
    if (debugMode == 14u) debugColor = reservoir.meta.y.xxx;
    if (debugMode == 15u) debugColor = saturate(reservoir.meta.z / max(restirSpatialPasses * 4.0f, 1.0f)).xxx;
#endif
#if PT_RESTIR_PT_ENHANCED
    uint2 enhancedDimensions = DispatchRaysDimensions().xy;
    uint enhancedPixelIndex = PixelIndex(pixel, enhancedDimensions);
    uint enhancedSpatialPasses = min((uint)round(g_scene.restirOptions.y), 3u);
    bool enhancedPairedSpatial = g_scene.restirEnhancedOptions0.x > 0.5f;
    bool enhancedDuplication = g_scene.restirEnhancedOptions0.y > 0.5f;
    bool enhancedColorReuse = g_scene.restirEnhancedOptions0.z > 0.5f;
    bool enhancedReplayCompaction = g_scene.restirEnhancedOptions1.x > 0.5f;
    float enhancedFootprintC = max(g_scene.restirEnhancedOptions1.y, 0.001f);
    float enhancedAlphaMin = max(g_scene.restirEnhancedOptions1.z, 0.001f);
    float enhancedDefaultCap = max(g_scene.restirEnhancedOptions2.y, 1.0f);
    float enhancedMinCap = max(g_scene.restirEnhancedOptions2.z, 1.0f);
    float enhancedDuplicationAlpha = max(g_scene.restirEnhancedOptions2.w, 0.001f);
    bool enhancedTemporalRejected = false;
    bool enhancedSpatialRejected = false;
    bool enhancedReplayRejected = false;
    bool enhancedReplayAccepted = false;
    bool enhancedForcedNeeRejected = false;
    bool enhancedForcedNeeAccepted = false;
    uint enhancedSeed = selectedSampleIndex;
    uint enhancedReplaySeed = Hash(pixel.x * 9781u ^ pixel.y * 6271u ^ frameCounter * 104729u ^ 0x9e21f4u);
    RestirReservoir reservoir = MakeRestirReservoir(selectedColor, totalSamples);
    reservoir.radianceWeight.w = max(selectedWeightSum / (float)max(totalSamples, 1u), reservoir.radianceWeight.w);
    reservoir.pathSeedsAndFlags = uint4(enhancedSeed, enhancedReplaySeed, selectedFirstHit.hit | (selectedBounceCount << 8u), selectedBounceCount);
    reservoir.reconnectData = uint4(asuint(selectedFirstHit.hitT), asuint(selectedFirstHit.roughness), asuint(RestirTarget(selectedColor)), 0u);
    EnhancedGBuffer currentGBuffer = MakeEnhancedGBuffer(selectedFirstHit);
    g_enhancedGBufferCurrent[enhancedPixelIndex] = currentGBuffer;
    int2 enhancedHistoryPixel = int2(pixel);
    bool enhancedHistoryValid = selectedFirstHit.hit != 0u && ReprojectToHistoryPixel(selectedFirstHit.position, enhancedDimensions, enhancedHistoryPixel);
    if (enhancedHistoryValid)
    {
        EnhancedGBuffer historyGBuffer = LoadEnhancedGBufferHistory(enhancedHistoryPixel, enhancedDimensions);
        enhancedHistoryValid = ValidateEnhancedHistory(currentGBuffer, historyGBuffer, enhancedFootprintC, enhancedAlphaMin);
    }
    if (!enhancedHistoryValid && accumulatedFrames > 0u)
    {
        enhancedHistoryValid = FindFallbackHistoryPixel(int2(pixel), enhancedDimensions, currentGBuffer, enhancedFootprintC, enhancedAlphaMin, enhancedHistoryPixel);
    }

    float temporalCap = enhancedDefaultCap;
    if (accumulatedFrames > 0u && enhancedDuplication && enhancedHistoryValid)
    {
        RestirReservoir seedHistory = LoadRestirHistory(enhancedHistoryPixel, enhancedDimensions);
        float priorDuplication = (float)(g_enhancedDuplicationHistory[PixelIndex((uint2)enhancedHistoryPixel, enhancedDimensions)] & 0xffu) / 255.0f;
        temporalCap = lerp(enhancedDefaultCap, enhancedMinCap, pow(saturate(priorDuplication), enhancedDuplicationAlpha));
    }

    if (accumulatedFrames > 0u && g_scene.restirOptions.x > 0.5f)
    {
        if (enhancedHistoryValid)
        {
            RestirReservoir history = LoadRestirHistory(enhancedHistoryPixel, enhancedDimensions);
            EnhancedReplayResult historyReplay;
            uint historyRejectReason;
            bool historyReplayValid = ReplayShiftCandidate(pixel, history, historyReplay, historyRejectReason);
            if (historyReplayValid)
            {
                uint forcedFlags = historyReplay.debugFlags.x;
                enhancedForcedNeeAccepted = enhancedForcedNeeAccepted || ((forcedFlags & 8u) != 0u);
                enhancedForcedNeeRejected = enhancedForcedNeeRejected || ((forcedFlags & 1u) != 0u && (forcedFlags & 8u) == 0u);
                float historyM = max(history.meta.x, 1.0f);
                float mis = PairwiseMisWeight(history.radianceWeight.w, reservoir.radianceWeight.w);
                history.radianceWeight.w *= (min(historyM, temporalCap) / historyM) * mis;
                CombineRestirReservoir(reservoir, history);
                reservoir.meta.y = max(reservoir.meta.y, history.meta.w);
                enhancedReplayAccepted = true;
            }
            else
            {
                enhancedTemporalRejected = true;
                enhancedReplayRejected = historyRejectReason > 1u;
            }
        }
        else
        {
            enhancedTemporalRejected = true;
        }
    }

    if (accumulatedFrames > 0u && enhancedSpatialPasses > 0u)
    {
        int radius = max((int)round(g_scene.restirOptions.z), 1);
        uint spatialIterations = min(enhancedSpatialPasses, 3u);
        uint pairState = Hash(pixel.x * 1664525u ^ pixel.y * 1013904223u ^ frameCounter * 747796405u);
        for (uint i = 0u; i < 3u; ++i)
        {
            if (i >= spatialIterations)
            {
                break;
            }

            int2 offset;
            if (enhancedPairedSpatial)
            {
                offset = PairedReuseOffset(pixel, i, frameCounter);
                offset = clamp(offset, int2(-radius, -radius), int2(radius, radius));
            }
            else
            {
                offset = int2((int)round(Random01(pairState) * 2.0f * radius) - radius, (int)round(Random01(pairState) * 2.0f * radius) - radius);
            }

            RestirReservoir neighbor = LoadRestirHistory(int2(pixel) + offset, enhancedDimensions);
            EnhancedGBuffer neighborGBuffer = LoadEnhancedGBufferHistory(int2(pixel) + offset, enhancedDimensions);
            bool acceptNeighbor = ValidateEnhancedHistory(currentGBuffer, neighborGBuffer, enhancedFootprintC, enhancedAlphaMin);
            if (acceptNeighbor)
            {
                EnhancedReplayResult neighborReplay;
                uint neighborRejectReason;
                acceptNeighbor = ReplayShiftCandidate(pixel, neighbor, neighborReplay, neighborRejectReason);
                uint forcedFlags = neighborReplay.debugFlags.x;
                enhancedForcedNeeAccepted = enhancedForcedNeeAccepted || ((forcedFlags & 8u) != 0u);
                enhancedForcedNeeRejected = enhancedForcedNeeRejected || ((forcedFlags & 1u) != 0u && (forcedFlags & 8u) == 0u);
                enhancedReplayRejected = enhancedReplayRejected || (!acceptNeighbor && neighborRejectReason > 1u);
                enhancedReplayAccepted = enhancedReplayAccepted || acceptNeighbor;
            }
            else
            {
                enhancedSpatialRejected = true;
            }

            if (acceptNeighbor)
            {
                neighbor.radianceWeight.w *= enhancedColorReuse ? 0.65f : 0.5f;
                CombineRestirReservoir(reservoir, neighbor);
                reservoir.meta.z += neighbor.meta.w;
            }
            else
            {
                enhancedSpatialRejected = true;
            }
        }
    }

    if (reservoir.meta.w > 0.5f)
    {
        float3 enhancedRadiance = ClampRadiance(reservoir.radianceWeight.rgb);
        color = enhancedRadiance;
        directNee = enhancedRadiance;
        indirect = enhancedRadiance;
    }

    float duplicationScore = 0.0f;
    if (enhancedDuplication && accumulatedFrames > 0u && reservoir.meta.w > 0.5f)
    {
        uint duplicateCount = 0u;
        uint baseSeed = reservoir.pathSeedsAndFlags.x;
        for (int y = -8; y <= 8; ++y)
        {
            for (int x = -8; x <= 8; ++x)
            {
                RestirReservoir neighbor = LoadRestirHistory(int2(pixel) + int2(x, y), enhancedDimensions);
                duplicateCount += (neighbor.meta.w > 0.5f && neighbor.pathSeedsAndFlags.x == baseSeed) ? 1u : 0u;
            }
        }
        duplicationScore = saturate((float)duplicateCount / 288.0f);
        reservoir.reconnectData.w = (reservoir.reconnectData.w & 0xffffff00u) | ((uint)round(duplicationScore * 255.0f) & 0xffu);
    }
    g_enhancedDuplicationCurrent[enhancedPixelIndex] = (uint)round(duplicationScore * 255.0f);

    g_restirCurrent[enhancedPixelIndex] = reservoir;
    uint replayNeeded = enhancedReplayCompaction && reservoir.meta.w > 0.5f && (reservoir.meta.y > 0.5f || reservoir.meta.z > 0.5f) ? 1u : 0u;
    g_enhancedReplayTasks[enhancedPixelIndex] = uint4(replayNeeded, pixel.x, pixel.y, reservoir.pathSeedsAndFlags.z);
    bool replayDebugView = debugMode >= 22u && debugMode <= 25u;
    EnhancedReplayResult replayDebug = EmptyEnhancedReplayResult();
    if (replayDebugView)
    {
        replayDebug = ReplayPathSample(pixel, reservoir);
    }

    if (debugMode == 0u) debugColor = color;
    if (debugMode == 13u || debugMode == 16u) debugColor = saturate(reservoir.radianceWeight.w / max(g_scene.restirOptions.w, 0.001f)).xxx;
    if (debugMode == 17u) debugColor = saturate((float)reservoir.pathSeedsAndFlags.w / max(g_scene.pathOptions.x, 1.0f)).xxx;
    if (debugMode == 18u) debugColor = float3(enhancedTemporalRejected ? 1.0f : (reservoir.meta.y > 0.5f ? 0.25f : 0.0f), enhancedSpatialRejected ? 1.0f : (reservoir.meta.z > 0.5f ? 0.25f : 0.0f), enhancedForcedNeeRejected ? 1.0f : (enhancedForcedNeeAccepted ? 0.25f : (enhancedReplayRejected ? 0.5f : 0.0f)));
    if (debugMode == 19u) debugColor = saturate(reservoir.meta.z / 3.0f).xxx;
    if (debugMode == 20u) debugColor = duplicationScore.xxx;
    if (debugMode == 21u) debugColor = replayNeeded.xxx;
    if (debugMode == 22u) debugColor = replayDebug.flags.x > 0u ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, replayDebug.flags.y > 0u ? 0.5f : 0.0f, replayDebug.flags.z > 0u ? 0.5f : 0.0f);
    if (debugMode == 23u) debugColor = replayDebug.radianceTarget.rgb;
    if (debugMode == 24u) debugColor = float3(saturate(replayDebug.metrics.y), replayDebug.flags.x > 0u ? 1.0f : 0.0f, saturate(abs(log2(max(replayDebug.metrics.y, 0.0001f))) / 4.0f));
    if (debugMode == 25u) debugColor = replayDebug.forcedNee.rgb;
#endif

    if (debugMode == 0u)
    {
        if (g_scene.frameOptions.z > 0.5f)
        {
            debugColor = g_accumulation[pixel].rgb;
        }
        else
        {
            float3 history = g_accumulation[pixel].rgb;
            debugColor = ApplyTemporalClamp(debugColor, history, accumulatedFrames);
            float weight = 1.0f / (float)(min(accumulatedFrames, maxAccumulatedFrames - 1u) + 1u);
            debugColor = lerp(history, debugColor, weight);
            g_accumulation[pixel] = float4(debugColor, 1.0f);
        }
    }

    if (debugMode == 11u)
    {
        debugColor = ((float)accumulatedFrames / (float)maxAccumulatedFrames).xxx;
    }

    float firstHitDepth = firstHit.hit != 0u ? firstHit.hitT : -1.0f;
    float3 firstHitNormal = firstHit.hit != 0u ? firstHit.normal * 0.5f + 0.5f : 0.0f.xxx;
    g_denoiseAov0[pixel] = float4(firstHitNormal, firstHitDepth);
    g_denoiseAov1[pixel] = float4(firstHit.baseColor, firstHit.roughness);

    g_output[pixel] = float4(Tonemap(debugColor), 1.0f);
}

[shader("miss")]
void Miss(inout RayPayload payload)
{
    payload = EmptyPayload();
}

[shader("miss")]
void ShadowMiss(inout ShadowPayload payload)
{
    payload.occluded = 0;
}

[shader("anyhit")]
void AnyHit(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attributes)
{
    if (IsAlphaTransparent(GeometryIndex(), PrimitiveIndex(), attributes.barycentrics))
    {
        IgnoreHit();
    }
}

[shader("anyhit")]
void ShadowAnyHit(inout ShadowPayload payload, in BuiltInTriangleIntersectionAttributes attributes)
{
    if (IsAlphaTransparent(GeometryIndex(), PrimitiveIndex(), attributes.barycentrics))
    {
        IgnoreHit();
    }
}

[shader("closesthit")]
void ClosestHit(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attributes)
{
    SurfaceData surface = LoadSurface(GeometryIndex(), PrimitiveIndex(), attributes.barycentrics);
    payload.position = surface.position;
    payload.hitT = RayTCurrent();
    payload.normal = surface.normal;
    payload.hit = 1u;
    payload.baseColor = surface.baseColor;
    payload.roughness = surface.roughness;
    payload.normalTexture = surface.normalTexture;
    payload.metallic = surface.metallic;
    payload.emissive = surface.emissive;
    payload.ao = surface.ao;
    payload.materialIndex = surface.materialIndex;
}
