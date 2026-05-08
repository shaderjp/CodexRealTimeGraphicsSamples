#if defined(VULKAN)
#define VK_LOCATION(n) [[vk::location(n)]]
#define VK_BINDING(n) [[vk::binding(n, 0)]]
#else
#define VK_LOCATION(n)
#define VK_BINDING(n)
#endif

#ifndef CLUSTERED_FORWARD
#define CLUSTERED_FORWARD 0
#endif

static const float PI = 3.14159265359f;

struct SceneConstants
{
    row_major float4x4 viewProjection;
    row_major float4x4 view;
    row_major float4x4 projection;
    row_major float4x4 lightViewProjection;
    float4 cameraPosition;
    float4 lightDirection;
    float4 lightColor;
    float4 debugOptions;
    float4 localLightOptions;
    float4 clusterOptions;
    float4 clusterOptions2;
    float4 clusterOptions3;
    float4 shadowOptions;
};

struct MaterialConstants
{
    float4 baseColorFactor;
    float4 emissiveFactor;
    float4 options;
};

struct RasterLight
{
    float4 positionRadius;
    float4 radianceSource;
};

struct ClusterRecord
{
    uint offset;
    uint count;
    uint overflow;
    uint padding;
};

VK_BINDING(0) ConstantBuffer<SceneConstants> g_scene : register(b0);
VK_BINDING(1) ConstantBuffer<MaterialConstants> g_material : register(b1);
VK_BINDING(2) Texture2D g_baseColorTexture : register(t0);
VK_BINDING(3) Texture2D g_normalTexture : register(t1);
VK_BINDING(4) Texture2D g_specularTexture : register(t2);
VK_BINDING(5) Texture2D g_emissiveTexture : register(t3);
VK_BINDING(6) SamplerState g_linearSampler : register(s0);
VK_BINDING(7) StructuredBuffer<RasterLight> g_lights : register(t4);
VK_BINDING(8) StructuredBuffer<ClusterRecord> g_clusterRecords : register(t5);
VK_BINDING(9) StructuredBuffer<uint> g_clusterLightIndices : register(t6);
VK_BINDING(10) RWStructuredBuffer<ClusterRecord> g_rwClusterRecords : register(u0);
VK_BINDING(11) RWStructuredBuffer<uint> g_rwClusterLightIndices : register(u1);
VK_BINDING(12) RWStructuredBuffer<uint> g_clusterStats : register(u2);
VK_BINDING(13) Texture2D<float> g_shadowMap : register(t7);
VK_BINDING(14) SamplerComparisonState g_shadowSampler : register(s1);

struct VSInput
{
    VK_LOCATION(0) float3 position : POSITION;
    VK_LOCATION(1) float3 normal : NORMAL;
    VK_LOCATION(2) float4 tangent : TANGENT;
    VK_LOCATION(3) float2 texcoord : TEXCOORD0;
};

struct PSInput
{
    float4 position : SV_POSITION;
    VK_LOCATION(0) float3 worldPosition : POSITION;
    VK_LOCATION(1) float3 normal : NORMAL;
    VK_LOCATION(2) float4 tangent : TANGENT;
    VK_LOCATION(3) float2 texcoord : TEXCOORD0;
};

struct ShadowPSInput
{
    float4 position : SV_POSITION;
    VK_LOCATION(0) float2 texcoord : TEXCOORD0;
};

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

float3 FresnelSchlick(float cosTheta, float3 f0)
{
    return f0 + (1.0f - f0) * pow(1.0f - saturate(cosTheta), 5.0f);
}

float3 DebugColorVector(float3 value)
{
    return normalize(value) * 0.5f + 0.5f;
}

float3 ToneMap(float3 color)
{
    color = color / (color + 1.0f);
    return pow(max(color, 0.0f), 1.0f / 2.2f);
}

float3 SampleDebuggableNormalTexture(float2 texcoord)
{
    float forcedMip = g_scene.debugOptions.z;
    float mipBias = g_scene.debugOptions.w;
    if (forcedMip >= 0.0f)
    {
        return g_normalTexture.SampleLevel(g_linearSampler, texcoord, forcedMip).xyz;
    }
    return g_normalTexture.SampleBias(g_linearSampler, texcoord, mipBias).xyz;
}

uint ActiveLightCount()
{
    return (uint)round(max(g_scene.localLightOptions.w, 0.0f));
}

bool SourceEnabled(float sourceType)
{
    bool emissiveEnabled = g_scene.localLightOptions.y > 0.5f;
    bool proceduralEnabled = g_scene.localLightOptions.z > 0.5f;
    return sourceType < 0.5f ? emissiveEnabled : proceduralEnabled;
}

float3 EvaluatePbrLight(float3 baseColor, float roughness, float metallic, float3 normal, float3 viewDirection, float3 lightDirection, float3 radiance)
{
    float3 halfVector = normalize(viewDirection + lightDirection);
    float3 f0 = lerp(float3(0.04f, 0.04f, 0.04f), baseColor, metallic);
    float3 fresnel = FresnelSchlick(saturate(dot(halfVector, viewDirection)), f0);
    float distribution = DistributionGGX(normal, halfVector, roughness);
    float geometry = GeometrySmith(normal, viewDirection, lightDirection, roughness);
    float nDotL = saturate(dot(normal, lightDirection));
    float nDotV = saturate(dot(normal, viewDirection));
    float3 specular = (distribution * geometry * fresnel) / max(4.0f * nDotV * nDotL, 0.0001f);
    float3 diffuse = (1.0f - fresnel) * (1.0f - metallic) * baseColor / PI;
    return (diffuse + specular) * radiance * nDotL;
}

float3 EvaluateLocalLight(uint lightIndex, float3 worldPosition, float3 baseColor, float roughness, float metallic, float3 normal, float3 viewDirection)
{
    RasterLight light = g_lights[lightIndex];
    if (!SourceEnabled(light.radianceSource.w))
    {
        return 0.0f;
    }

    float radius = max(light.positionRadius.w * g_scene.clusterOptions3.x, 0.01f);
    float3 toLight = light.positionRadius.xyz - worldPosition;
    float distanceSquared = dot(toLight, toLight);
    float distanceToLight = sqrt(max(distanceSquared, 0.0001f));
    if (distanceToLight >= radius)
    {
        return 0.0f;
    }

    float3 lightDirection = toLight / distanceToLight;
    float falloff = saturate(1.0f - distanceToLight / radius);
    float attenuation = falloff * falloff / (1.0f + distanceSquared * 0.02f);
    float3 radiance = light.radianceSource.rgb * g_scene.clusterOptions2.w * attenuation;
    return EvaluatePbrLight(baseColor, roughness, metallic, normal, viewDirection, lightDirection, radiance);
}

PSInput VSMain(VSInput input)
{
    PSInput result;
    result.position = mul(float4(input.position, 1.0f), g_scene.viewProjection);
    result.worldPosition = input.position;
    result.normal = normalize(input.normal);
    result.tangent = input.tangent;
    result.texcoord = input.texcoord;
    return result;
}

ShadowPSInput VSShadowMain(VSInput input)
{
    ShadowPSInput result;
    result.position = mul(float4(input.position, 1.0f), g_scene.lightViewProjection);
    result.texcoord = input.texcoord;
    return result;
}

void PSShadowMain(ShadowPSInput input)
{
    float4 baseSample = g_baseColorTexture.Sample(g_linearSampler, input.texcoord);
    if (baseSample.a * g_material.baseColorFactor.a < g_material.options.z)
    {
        discard;
    }
}

float ComputeShadowFactor(float4 shadowPosition)
{
    if (g_scene.shadowOptions.x < 0.5f || shadowPosition.w <= 0.0f)
    {
        return 1.0f;
    }

    float3 projected = shadowPosition.xyz / shadowPosition.w;
#if defined(VULKAN)
    float2 shadowUv = projected.xy * 0.5f + 0.5f;
#else
    float2 shadowUv = projected.xy * float2(0.5f, -0.5f) + 0.5f;
#endif
    if (shadowUv.x < 0.0f || shadowUv.x > 1.0f || shadowUv.y < 0.0f || shadowUv.y > 1.0f || projected.z < 0.0f || projected.z > 1.0f)
    {
        return 1.0f;
    }

    uint width = 0;
    uint height = 0;
    g_shadowMap.GetDimensions(width, height);
    float2 texelSize = 1.0f / float2(max(width, 1u), max(height, 1u));
    int radius = clamp((int)round(g_scene.shadowOptions.w), 0, 3);
    float depthBias = g_scene.shadowOptions.y;
    float visibility = 0.0f;
    float sampleCount = 0.0f;
    [loop]
    for (int y = -radius; y <= radius; ++y)
    {
        [loop]
        for (int x = -radius; x <= radius; ++x)
        {
            visibility += g_shadowMap.SampleCmpLevelZero(g_shadowSampler, shadowUv + float2(x, y) * texelSize, projected.z - depthBias);
            sampleCount += 1.0f;
        }
    }
    return visibility / max(sampleCount, 1.0f);
}

uint ComputeClusterIndex(float4 pixelPosition, float3 worldPosition, out uint zSlice)
{
    uint tileSize = max((uint)round(g_scene.clusterOptions.z), 1u);
    uint tileCountX = max((uint)round(g_scene.clusterOptions3.y), 1u);
    uint tileCountY = max((uint)round(g_scene.clusterOptions3.z), 1u);
    uint sliceCount = max((uint)round(g_scene.clusterOptions.w), 1u);

    uint tileX = min((uint)(pixelPosition.x / tileSize), tileCountX - 1u);
    uint tileY = min((uint)(pixelPosition.y / tileSize), tileCountY - 1u);
    float viewZ = max(mul(float4(worldPosition, 1.0f), g_scene.view).z, g_scene.clusterOptions2.x);
    float zScale = log(g_scene.clusterOptions2.y / g_scene.clusterOptions2.x);
    zSlice = min((uint)(log(viewZ / g_scene.clusterOptions2.x) / zScale * sliceCount), sliceCount - 1u);
    return (zSlice * tileCountY + tileY) * tileCountX + tileX;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float4 baseSample = g_baseColorTexture.Sample(g_linearSampler, input.texcoord);
    if (baseSample.a * g_material.baseColorFactor.a < g_material.options.z)
    {
        discard;
    }

    float3 baseColor = baseSample.rgb * g_material.baseColorFactor.rgb;
    float4 specularPacked = g_specularTexture.Sample(g_linearSampler, input.texcoord);
    float ao = specularPacked.r;
    float roughness = clamp(specularPacked.g, 0.04f, 1.0f);
    float metallic = saturate(specularPacked.b);
    float3 emissiveSample = g_emissiveTexture.Sample(g_linearSampler, input.texcoord).rgb;
    float3 emissiveFactor = g_material.emissiveFactor.rgb * max(g_material.emissiveFactor.w, 0.0f);
    float3 emissive = max(emissiveSample * max(g_material.emissiveFactor.rgb, float3(1.0f, 1.0f, 1.0f)), emissiveFactor);

    float3 normalTexture = SampleDebuggableNormalTexture(input.texcoord);
    float2 normalXY = normalTexture.xy * 2.0f - 1.0f;
    normalXY.y *= g_scene.debugOptions.y > 0.5f ? -1.0f : 1.0f;
    float3 normalSample = float3(normalXY, sqrt(saturate(1.0f - dot(normalXY, normalXY))));
    float3 n = normalize(input.normal);
    float3 t = normalize(input.tangent.xyz - n * dot(n, input.tangent.xyz));
    float3 b = normalize(cross(n, t) * input.tangent.w);
    float3 normal = normalize(normalSample.x * t + normalSample.y * b + normalSample.z * n);

    float3 viewDirection = normalize(g_scene.cameraPosition.xyz - input.worldPosition);
    float3 lightDirection = normalize(-g_scene.lightDirection.xyz);
    float3 radiance = g_scene.lightColor.rgb * g_scene.lightColor.a;
    float nDotL = saturate(dot(normal, lightDirection));
    float4 biasedShadowPosition = mul(float4(input.worldPosition + normal * g_scene.shadowOptions.z, 1.0f), g_scene.lightViewProjection);
    float shadowFactor = ComputeShadowFactor(biasedShadowPosition);
    float3 shadowProjected = biasedShadowPosition.xyz / max(biasedShadowPosition.w, 0.0001f);
#if defined(VULKAN)
    float2 shadowUv = shadowProjected.xy * 0.5f + 0.5f;
#else
    float2 shadowUv = shadowProjected.xy * float2(0.5f, -0.5f) + 0.5f;
#endif

    uint debugMode = (uint)round(g_scene.debugOptions.x);
    if (debugMode == 1)
    {
        return float4(baseColor, 1.0f);
    }
    if (debugMode == 2)
    {
        return float4(normal * 0.5f + 0.5f, 1.0f);
    }
    if (debugMode == 3)
    {
        return float4(normalTexture, 1.0f);
    }
    if (debugMode == 4)
    {
        return float4(normalSample * 0.5f + 0.5f, 1.0f);
    }
    if (debugMode == 5)
    {
        return float4(ao, roughness, metallic, 1.0f);
    }
    if (debugMode == 6)
    {
        return float4(nDotL.xxx, 1.0f);
    }
    if (debugMode == 7)
    {
        return float4(specularPacked.rgb, 1.0f);
    }
    if (debugMode == 8)
    {
        return float4(emissive, 1.0f);
    }
    if (debugMode == 9)
    {
        return float4(DebugColorVector(n), 1.0f);
    }
    if (debugMode == 10)
    {
        return float4(DebugColorVector(t), 1.0f);
    }
    if (debugMode == 11)
    {
        return float4(DebugColorVector(b), 1.0f);
    }
    if (debugMode == 12)
    {
        return input.tangent.w >= 0.0f ? float4(0.1f, 0.8f, 0.1f, 1.0f) : float4(0.9f, 0.1f, 0.1f, 1.0f);
    }
    if (debugMode == 13)
    {
        float mipLevel = g_scene.debugOptions.z >= 0.0f ? g_scene.debugOptions.z : g_normalTexture.CalculateLevelOfDetail(g_linearSampler, input.texcoord) + g_scene.debugOptions.w;
        return float4(saturate(mipLevel / 10.0f).xxx, 1.0f);
    }
    if (debugMode == 14)
    {
        return float4(input.texcoord.x, input.texcoord.y, 0.0f, 1.0f);
    }
    if (debugMode == 15)
    {
        return float4((baseSample.a * g_material.baseColorFactor.a).xxx, 1.0f);
    }
    if (debugMode == 16)
    {
        uint width = 0;
        uint height = 0;
        uint mipLevels = 0;
        g_normalTexture.GetDimensions(0, width, height, mipLevels);
        if (width <= 1 || height <= 1)
        {
            return float4(0.95f, 0.1f, 0.1f, 1.0f);
        }
        return float4(0.1f, saturate((float)mipLevels / 10.0f), 0.95f, 1.0f);
    }

    float3 ambient = baseColor * ao * 0.055f;
    float3 directional = EvaluatePbrLight(baseColor, roughness, metallic, normal, viewDirection, lightDirection, radiance);
    float3 local = 0.0f;
    uint localCount = 0;
    uint clusterOverflow = 0;
    uint zSlice = 0;
    if (g_scene.localLightOptions.x > 0.5f)
    {
#if CLUSTERED_FORWARD
        uint clusterIndex = ComputeClusterIndex(input.position, input.worldPosition, zSlice);
        ClusterRecord record = g_clusterRecords[clusterIndex];
        localCount = record.count;
        clusterOverflow = record.overflow;
        for (uint i = 0; i < record.count; ++i)
        {
            local += EvaluateLocalLight(g_clusterLightIndices[record.offset + i], input.worldPosition, baseColor, roughness, metallic, normal, viewDirection);
        }
#else
        localCount = ActiveLightCount();
        for (uint i = 0; i < localCount; ++i)
        {
            local += EvaluateLocalLight(i, input.worldPosition, baseColor, roughness, metallic, normal, viewDirection);
        }
#endif
    }

    if (debugMode == 17)
    {
        return float4(ToneMap(ambient + directional * shadowFactor + emissive), 1.0f);
    }
    if (debugMode == 18)
    {
        return float4(ToneMap(local), 1.0f);
    }
    if (debugMode == 19)
    {
        float maxLights = max(g_scene.clusterOptions2.z, 1.0f);
        return float4(saturate((float)localCount / maxLights).xxx, 1.0f);
    }
    if (debugMode == 20)
    {
        float sliceCount = max(g_scene.clusterOptions.w - 1.0f, 1.0f);
        return float4((float)zSlice / sliceCount, 0.25f, 1.0f - (float)zSlice / sliceCount, 1.0f);
    }
    if (debugMode == 21)
    {
        return clusterOverflow != 0 ? float4(1.0f, 0.0f, 0.0f, 1.0f) : float4(0.0f, 0.05f, 0.0f, 1.0f);
    }
    if (debugMode == 22)
    {
        if (shadowUv.x < 0.0f || shadowUv.x > 1.0f || shadowUv.y < 0.0f || shadowUv.y > 1.0f)
        {
            return float4(0.0f, 0.0f, 0.0f, 1.0f);
        }
        return float4(saturate(g_shadowMap.SampleLevel(g_linearSampler, shadowUv, 0).xxx), 1.0f);
    }
    if (debugMode == 23)
    {
        return float4(shadowFactor.xxx, 1.0f);
    }
    if (debugMode == 24)
    {
        return float4(saturate(shadowProjected.z).xxx, 1.0f);
    }

    float3 color = ambient + directional * shadowFactor + local + emissive;
    return float4(ToneMap(color), 1.0f);
}

bool SphereIntersectsCluster(RasterLight light, uint tileX, uint tileY, uint zSlice)
{
    if (!SourceEnabled(light.radianceSource.w))
    {
        return false;
    }

    uint tileSize = max((uint)round(g_scene.clusterOptions.z), 1u);
    uint sliceCount = max((uint)round(g_scene.clusterOptions.w), 1u);
    float nearZ = g_scene.clusterOptions2.x;
    float farZ = g_scene.clusterOptions2.y;
    float radius = max(light.positionRadius.w * g_scene.clusterOptions3.x, 0.01f);
    float3 lightView = mul(float4(light.positionRadius.xyz, 1.0f), g_scene.view).xyz;

    float z0 = nearZ * pow(farZ / nearZ, (float)zSlice / (float)sliceCount);
    float z1 = nearZ * pow(farZ / nearZ, (float)(zSlice + 1u) / (float)sliceCount);
    if (lightView.z + radius < z0 || lightView.z - radius > z1)
    {
        return false;
    }

    if (lightView.z <= 0.001f)
    {
        return true;
    }

    float4 clip = mul(float4(light.positionRadius.xyz, 1.0f), g_scene.viewProjection);
    if (clip.w <= 0.001f)
    {
        return true;
    }

    float2 ndc = clip.xy / clip.w;
    float2 screen = float2((ndc.x * 0.5f + 0.5f) * g_scene.clusterOptions.x, 0.0f);
#if defined(VULKAN)
    screen.y = (ndc.y * 0.5f + 0.5f) * g_scene.clusterOptions.y;
#else
    screen.y = (1.0f - (ndc.y * 0.5f + 0.5f)) * g_scene.clusterOptions.y;
#endif
    float radiusPixelsX = radius * abs(g_scene.projection._11) * g_scene.clusterOptions.x * 0.5f / lightView.z;
    float radiusPixelsY = radius * abs(g_scene.projection._22) * g_scene.clusterOptions.y * 0.5f / lightView.z;
    float tileMinX = (float)(tileX * tileSize);
    float tileMinY = (float)(tileY * tileSize);
    float tileMaxX = min(tileMinX + (float)tileSize, g_scene.clusterOptions.x);
    float tileMaxY = min(tileMinY + (float)tileSize, g_scene.clusterOptions.y);

    return screen.x + radiusPixelsX >= tileMinX &&
        screen.x - radiusPixelsX <= tileMaxX &&
        screen.y + radiusPixelsY >= tileMinY &&
        screen.y - radiusPixelsY <= tileMaxY;
}

[numthreads(1, 1, 1)]
void CSResetStats(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    g_clusterStats[0] = 0;
    g_clusterStats[1] = 0;
    g_clusterStats[2] = 0;
    g_clusterStats[3] = 0;
}

[numthreads(4, 4, 4)]
void CSBuildClusters(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint tileCountX = max((uint)round(g_scene.clusterOptions3.y), 1u);
    uint tileCountY = max((uint)round(g_scene.clusterOptions3.z), 1u);
    uint sliceCount = max((uint)round(g_scene.clusterOptions.w), 1u);
    if (dispatchThreadId.x >= tileCountX || dispatchThreadId.y >= tileCountY || dispatchThreadId.z >= sliceCount)
    {
        return;
    }

    uint maxLightsPerCluster = max((uint)round(g_scene.clusterOptions2.z), 1u);
    uint clusterIndex = (dispatchThreadId.z * tileCountY + dispatchThreadId.y) * tileCountX + dispatchThreadId.x;
    uint offset = clusterIndex * maxLightsPerCluster;
    uint count = 0;
    uint overflow = 0;
    uint activeLights = ActiveLightCount();
    for (uint lightIndex = 0; lightIndex < activeLights; ++lightIndex)
    {
        if (!SphereIntersectsCluster(g_lights[lightIndex], dispatchThreadId.x, dispatchThreadId.y, dispatchThreadId.z))
        {
            continue;
        }

        if (count < maxLightsPerCluster)
        {
            g_rwClusterLightIndices[offset + count] = lightIndex;
            ++count;
        }
        else
        {
            overflow = 1;
        }
    }

    ClusterRecord record;
    record.offset = offset;
    record.count = count;
    record.overflow = overflow;
    record.padding = 0;
    g_rwClusterRecords[clusterIndex] = record;

    if (count > 0)
    {
        InterlockedAdd(g_clusterStats[0], 1);
        InterlockedAdd(g_clusterStats[1], count);
        InterlockedMax(g_clusterStats[2], count);
    }
    if (overflow != 0)
    {
        InterlockedAdd(g_clusterStats[3], 1);
    }
}
