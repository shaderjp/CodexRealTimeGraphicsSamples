#if defined(VULKAN)
#define VK_BINDING(slot, descriptorSet) [[vk::binding(slot, descriptorSet)]]
#else
#define VK_BINDING(binding, set)
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
    float3 color;
    float hitT;
    float3 directColor;
    uint depth;
    float3 indirectColor;
    uint hit;
    float3 baseColor;
    uint shadowed;
    float3 normal;
    uint padding0;
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
    RtMaterial material;
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

float HashToUnitFloat(uint value)
{
    return (float)(Hash(value) & 0x00ffffffu) / 16777216.0f;
}

float3 Tonemap(float3 value)
{
    value = max(value, 0.0f.xxx);
    value = value / (1.0f.xxx + value);
    return pow(value, 1.0f / 2.2f);
}

float3 DebugColorVector(float3 value)
{
    return normalize(value) * 0.5f + 0.5f;
}

float Luminance(float3 color)
{
    return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

float3 EvaluateSky(float3 rayDirection)
{
    float3 fallback = g_scene.skyColor.rgb * g_scene.skyColor.a;
    if (g_scene.skyOptions.w < 0.5f)
    {
        return fallback;
    }

    float3 direction = normalize(rayDirection);
    float upperAmount = saturate(direction.y);
    float groundBlend = max(g_scene.skyOptions.z, 0.001f);
    float lowerAmount = saturate(-direction.y / groundBlend);
    float3 upperSky = lerp(g_scene.skyHorizonColor.rgb, g_scene.skyZenithColor.rgb, pow(upperAmount, 0.65f));
    float3 lowerSky = lerp(g_scene.skyHorizonColor.rgb, g_scene.skyGroundColor.rgb, lowerAmount);
    float3 sky = direction.y >= 0.0f ? upperSky : lowerSky;

    float3 sunDirection = normalize(-g_scene.lightDirection.xyz);
    float sunCos = dot(direction, sunDirection);
    float sunRadius = max(g_scene.skyOptions.y, 0.001f);
    float sunDisk = smoothstep(cos(sunRadius * 2.0f), cos(sunRadius), sunCos);
    float sunGlow = pow(saturate(sunCos), 64.0f) * 0.12f;
    sky += g_scene.lightColor.rgb * g_scene.skyOptions.x * (sunDisk + sunGlow);
    return sky * g_scene.skyColor.a;
}

float2 ProgressiveGiSample(uint2 pixel, uint sampleIndex)
{
    const float2 r2 = float2(0.754877666f, 0.569840296f);
    uint seed = pixel.x * 1973u + pixel.y * 9277u;
    float2 rotation = float2(HashToUnitFloat(seed), HashToUnitFloat(seed ^ 0x9e3779b9u));
    return frac(rotation + (float)(sampleIndex + 1u) * r2);
}

float3 ClampGiRadiance(float3 radiance)
{
    float limit = max(g_scene.giOptions.y, 0.0f);
    if (limit <= 0.0f)
    {
        return radiance;
    }

    float value = max(Luminance(max(radiance, 0.0f.xxx)), 0.0001f);
    return value > limit ? radiance * (limit / value) : radiance;
}

float3 ApplyTemporalClamp(float3 current, float3 history, uint accumulatedFrames)
{
    if (accumulatedFrames == 0u)
    {
        return current;
    }

    float clampScale = max(g_scene.giOptions.z, 0.0f);
    float clampMin = max(g_scene.giOptions.w, 0.0f);
    float3 extent = max(abs(history) * clampScale, clampMin.xxx);
    return clamp(current, history - extent, history + extent);
}

uint3 LoadTriangleIndices(RtGeometryRecord geometry, uint primitiveIndex)
{
    uint first = geometry.indexOffset + primitiveIndex * 3;
    return uint3(g_indices[first + 0], g_indices[first + 1], g_indices[first + 2]);
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

SurfaceData LoadSurface(uint geometryIndex, uint primitiveIndex, float2 barycentrics)
{
    RtGeometryRecord geometry = g_geometries[geometryIndex];
    uint3 tri = LoadTriangleIndices(geometry, primitiveIndex);
    MeshVertex v0 = g_vertices[tri.x];
    MeshVertex v1 = g_vertices[tri.y];
    MeshVertex v2 = g_vertices[tri.z];
    float3 bary = Barycentric3(barycentrics);

    SurfaceData surface;
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
    surface.emissive = g_textures[NonUniformResourceIndex(surface.material.textureBaseIndex + TextureSlotEmissive)].SampleLevel(g_linearSampler, surface.texcoord, 0.0f).rgb;
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
    return f0 + (1.0f - saturate(f0)) * pow(1.0f - saturate(cosTheta), 5.0f);
}

float3 EvaluateDirectLighting(SurfaceData surface, float3 viewDirection, float shadowFactor)
{
    float3 lightDirection = normalize(-g_scene.lightDirection.xyz);
    float3 halfVector = normalize(viewDirection + lightDirection);
    float3 radiance = g_scene.lightColor.rgb * g_scene.lightColor.a;
    float3 f0 = lerp(0.04f.xxx, surface.baseColor, surface.metallic);
    float3 fresnel = FresnelSchlick(saturate(dot(halfVector, viewDirection)), f0);
    float distribution = DistributionGGX(surface.normal, halfVector, surface.roughness);
    float geometry = GeometrySmith(surface.normal, viewDirection, lightDirection, surface.roughness);
    float nDotL = saturate(dot(surface.normal, lightDirection));
    float nDotV = saturate(dot(surface.normal, viewDirection));
    float3 specular = distribution * geometry * fresnel / max(4.0f * nDotV * nDotL, 0.0001f);
    float3 diffuse = (1.0f.xxx - fresnel) * (1.0f - surface.metallic) * surface.baseColor / PI;
    float3 ambient = surface.baseColor * surface.ao * 0.055f;
    return ambient + (diffuse + specular) * radiance * nDotL * shadowFactor + surface.emissive;
}

float TraceShadowRay(float3 origin, float3 normal)
{
#if RT_ENABLE_SHADOW
    if (g_scene.debugOptions.z < 0.5f)
    {
        return 1.0f;
    }

    RayDesc ray;
    ray.Origin = origin + normal * g_scene.rayOptions.x;
    ray.Direction = normalize(-g_scene.lightDirection.xyz);
    ray.TMin = g_scene.rayOptions.x;
    ray.TMax = g_scene.rayOptions.y;

    ShadowPayload payload;
    payload.occluded = 1;
    TraceRay(g_sceneAs, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER, 0xff, 1, 0, 1, ray, payload);
    return payload.occluded == 0 ? 1.0f : 0.0f;
#else
    return 1.0f;
#endif
}

void MakeBasis(float3 n, out float3 t, out float3 b)
{
    float3 up = abs(n.y) < 0.999f ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
    t = normalize(cross(up, n));
    b = cross(n, t);
}

float3 SampleCosineHemisphere(float2 xi, float3 n)
{
    float r = sqrt(xi.x);
    float phi = 2.0f * PI * xi.y;
    float3 local = float3(r * cos(phi), r * sin(phi), sqrt(saturate(1.0f - xi.x)));
    float3 t;
    float3 b;
    MakeBasis(n, t, b);
    return normalize(local.x * t + local.y * b + local.z * n);
}

[shader("raygeneration")]
void RayGen()
{
    uint2 pixel = DispatchRaysIndex().xy;
    uint2 dimensions = DispatchRaysDimensions().xy;
    float2 uv = ((float2)pixel + 0.5f) / (float2)dimensions;
    float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);

    float4 nearPoint = mul(float4(ndc, 0.0f, 1.0f), g_scene.inverseViewProjection);
    float4 farPoint = mul(float4(ndc, 1.0f, 1.0f), g_scene.inverseViewProjection);
    nearPoint.xyz /= nearPoint.w;
    farPoint.xyz /= farPoint.w;

    RayDesc ray;
    ray.Origin = g_scene.cameraPosition.xyz;
    ray.Direction = normalize(farPoint.xyz - nearPoint.xyz);
    ray.TMin = g_scene.rayOptions.x;
    ray.TMax = g_scene.rayOptions.y;

    float3 skyRadiance = EvaluateSky(ray.Direction);

    RayPayload payload;
    payload.color = skyRadiance;
    payload.hitT = 0.0f;
    payload.directColor = 0.0f.xxx;
    payload.depth = 0;
    payload.indirectColor = 0.0f.xxx;
    payload.hit = 0;
    payload.baseColor = 0.0f.xxx;
    payload.shadowed = 0;
    payload.normal = 0.0f.xxx;

    TraceRay(g_sceneAs, RAY_FLAG_NONE, 0xff, 0, 0, 0, ray, payload);

    uint debugMode = (uint)round(g_scene.debugOptions.x);
    float3 color = payload.color;
    if (debugMode == 1) color = payload.baseColor;
    if (debugMode == 2) color = payload.normal * 0.5f + 0.5f;
    if (debugMode == 4) color = payload.hit != 0 ? saturate(payload.hitT / 250.0f).xxx : 0.0f.xxx;
    if (debugMode == 5) color = payload.directColor;
    if (debugMode == 6) color = payload.shadowed != 0 ? 0.0f.xxx : 1.0f.xxx;
    if (debugMode == 7) color = payload.indirectColor;

#if RT_ENABLE_GI
    uint accumulatedFrames = (uint)round(g_scene.frameOptions.x);
    uint maxAccumulatedFrames = max((uint)round(g_scene.frameOptions.y), 1u);
    if (g_scene.frameOptions.z > 0.5f)
    {
        color = g_accumulation[pixel].rgb;
    }
    else
    {
        float3 history = g_accumulation[pixel].rgb;
        color = ApplyTemporalClamp(color, history, accumulatedFrames);
        float weight = 1.0f / (float)(min(accumulatedFrames, maxAccumulatedFrames - 1u) + 1u);
        color = lerp(history, color, weight);
        g_accumulation[pixel] = float4(color, 1.0f);
    }
    if (debugMode == 8)
    {
        color = ((float)accumulatedFrames / (float)maxAccumulatedFrames).xxx;
    }
#endif
    if (debugMode == 9) color = skyRadiance;

    g_output[pixel] = float4(Tonemap(color), 1.0f);
}

[shader("miss")]
void Miss(inout RayPayload payload)
{
    payload.color = EvaluateSky(WorldRayDirection());
    payload.hit = 0;
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
    float3 viewDirection = normalize(g_scene.cameraPosition.xyz - surface.position);
    float shadowFactor = TraceShadowRay(surface.position, surface.normal);
    float3 direct = EvaluateDirectLighting(surface, viewDirection, shadowFactor);
    float3 indirect = 0.0f.xxx;

#if RT_ENABLE_GI
    if (payload.depth == 0)
    {
        uint2 pixel = DispatchRaysIndex().xy;
        uint samplesPerFrame = clamp((uint)round(g_scene.giOptions.x), 1u, 4u);
        uint firstSampleIndex = (uint)round(g_scene.frameOptions.w) * samplesPerFrame;

        for (uint localSampleIndex = 0; localSampleIndex < samplesPerFrame; ++localSampleIndex)
        {
            float2 sample = ProgressiveGiSample(pixel, firstSampleIndex + localSampleIndex);
            float3 giDirection = SampleCosineHemisphere(sample, surface.normal);

            RayDesc giRay;
            giRay.Origin = surface.position + surface.normal * g_scene.rayOptions.x;
            giRay.Direction = giDirection;
            giRay.TMin = g_scene.rayOptions.x;
            giRay.TMax = g_scene.rayOptions.y;

            RayPayload giPayload;
            giPayload.color = EvaluateSky(giRay.Direction);
            giPayload.hitT = 0.0f;
            giPayload.directColor = 0.0f.xxx;
            giPayload.depth = 1;
            giPayload.indirectColor = 0.0f.xxx;
            giPayload.hit = 0;
            giPayload.baseColor = 0.0f.xxx;
            giPayload.shadowed = 0;
            giPayload.normal = 0.0f.xxx;
            TraceRay(g_sceneAs, RAY_FLAG_NONE, 0xff, 0, 0, 0, giRay, giPayload);
            indirect += ClampGiRadiance(giPayload.color);
        }

        indirect = indirect * (surface.baseColor * g_scene.rayOptions.z / (float)samplesPerFrame);
    }
#endif

    uint debugMode = (uint)round(g_scene.debugOptions.x);
    float3 shaded = direct + indirect;
    if (debugMode == 3)
    {
        shaded = surface.normalTexture;
    }

    payload.color = shaded;
    payload.hitT = RayTCurrent();
    payload.directColor = direct;
    payload.indirectColor = indirect;
    payload.hit = 1;
    payload.baseColor = surface.baseColor;
    payload.shadowed = shadowFactor < 0.5f ? 1u : 0u;
    payload.normal = surface.normal;
}
