#if defined(VULKAN)
#define VK_LOCATION(n) [[vk::location(n)]]
#define VK_BINDING(n) [[vk::binding(n, 0)]]
#define VK_PUSH_CONSTANT [[vk::push_constant]]
#else
#define VK_LOCATION(n)
#define VK_BINDING(n)
#define VK_PUSH_CONSTANT
#endif

static const float PI = 3.14159265359f;
static const uint MaxMeshletVertices = 64;
static const uint MaxMeshletTriangles = 96;

struct SceneConstants
{
    row_major float4x4 viewProjection;
    float4 cameraPosition;
    float4 lightDirection;
    float4 lightColor;
    float4 debugOptions;
};

struct MaterialConstants
{
    float4 baseColorFactor;
    float4 options;
};

struct MeshletDrawConstants
{
    uint meshletBase;
    uint debugMode;
    uint2 padding;
};

struct MeshVertex
{
    float3 position;
    float3 normal;
    float4 tangent;
    float2 texcoord;
};

struct MeshletRecord
{
    uint vertexOffset;
    uint vertexCount;
    uint triangleOffset;
    uint triangleCount;
    uint boundsIndex;
};

VK_BINDING(0) ConstantBuffer<SceneConstants> g_scene : register(b0);
VK_BINDING(1) ConstantBuffer<MaterialConstants> g_material : register(b1);
VK_BINDING(2) Texture2D g_baseColorTexture : register(t0);
VK_BINDING(3) Texture2D g_normalTexture : register(t1);
VK_BINDING(4) Texture2D g_specularTexture : register(t2);
VK_BINDING(5) Texture2D g_emissiveTexture : register(t3);
VK_BINDING(6) SamplerState g_linearSampler : register(s0);
VK_BINDING(7) StructuredBuffer<MeshVertex> g_vertices : register(t4);
VK_BINDING(8) StructuredBuffer<MeshletRecord> g_meshlets : register(t5);
VK_BINDING(9) StructuredBuffer<uint> g_meshletVertices : register(t6);
VK_BINDING(10) StructuredBuffer<uint> g_meshletTriangles : register(t7);
VK_PUSH_CONSTANT ConstantBuffer<MeshletDrawConstants> g_draw : register(b2);

struct PSInput
{
    float4 position : SV_POSITION;
    VK_LOCATION(0) float3 worldPosition : POSITION;
    VK_LOCATION(1) float3 normal : NORMAL;
    VK_LOCATION(2) float4 tangent : TANGENT;
    VK_LOCATION(3) float2 texcoord : TEXCOORD0;
    VK_LOCATION(4) nointerpolation uint meshletId : COLOR0;
};

uint3 DecodeMeshletTriangle(uint packedTriangle)
{
    return uint3(packedTriangle & 0xffu, (packedTriangle >> 8) & 0xffu, (packedTriangle >> 16) & 0xffu);
}

float3 MeshletDebugColor(uint meshletId)
{
    uint h = meshletId * 1664525u + 1013904223u;
    h ^= h >> 16;
    h *= 2246822519u;
    h ^= h >> 13;
    return 0.25f + 0.75f * float3(
        (float)(h & 0xffu),
        (float)((h >> 8) & 0xffu),
        (float)((h >> 16) & 0xffu)) / 255.0f;
}

[numthreads(128, 1, 1)]
[outputtopology("triangle")]
void MSMain(
    uint3 groupThreadId : SV_GroupThreadID,
    uint3 groupId : SV_GroupID,
    out vertices PSInput outputVertices[MaxMeshletVertices],
    out indices uint3 outputTriangles[MaxMeshletTriangles])
{
    const uint meshletId = g_draw.meshletBase + groupId.x;
    MeshletRecord meshlet = g_meshlets[meshletId];
    SetMeshOutputCounts(meshlet.vertexCount, meshlet.triangleCount);

    const uint threadId = groupThreadId.x;
    if (threadId < meshlet.vertexCount)
    {
        const uint vertexIndex = g_meshletVertices[meshlet.vertexOffset + threadId];
        MeshVertex vertex = g_vertices[vertexIndex];

        PSInput output;
        output.position = mul(float4(vertex.position, 1.0f), g_scene.viewProjection);
        output.worldPosition = vertex.position;
        output.normal = normalize(vertex.normal);
        output.tangent = vertex.tangent;
        output.texcoord = vertex.texcoord;
        output.meshletId = meshletId;
        outputVertices[threadId] = output;
    }

    if (threadId < meshlet.triangleCount)
    {
        outputTriangles[threadId] = DecodeMeshletTriangle(g_meshletTriangles[meshlet.triangleOffset + threadId]);
    }
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
    return f0 + (1.0f - f0) * pow(1.0f - saturate(cosTheta), 5.0f);
}

float3 DebugColorVector(float3 value)
{
    return normalize(value) * 0.5f + 0.5f;
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
    float3 emissive = g_emissiveTexture.Sample(g_linearSampler, input.texcoord).rgb;

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
    float3 halfVector = normalize(viewDirection + lightDirection);
    float3 radiance = g_scene.lightColor.rgb * g_scene.lightColor.a;

    float3 f0 = lerp(float3(0.04f, 0.04f, 0.04f), baseColor, metallic);
    float3 fresnel = FresnelSchlick(saturate(dot(halfVector, viewDirection)), f0);
    float distribution = DistributionGGX(normal, halfVector, roughness);
    float geometry = GeometrySmith(normal, viewDirection, lightDirection, roughness);
    float nDotL = saturate(dot(normal, lightDirection));
    float nDotV = saturate(dot(normal, viewDirection));

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
    if (debugMode == 17)
    {
        return float4(MeshletDebugColor(input.meshletId), 1.0f);
    }

    float3 specular = (distribution * geometry * fresnel) / max(4.0f * nDotV * nDotL, 0.0001f);
    float3 diffuse = (1.0f - fresnel) * (1.0f - metallic) * baseColor / PI;
    float3 ambient = baseColor * ao * 0.055f;
    float3 color = ambient + (diffuse + specular) * radiance * nDotL + emissive;

    color = color / (color + 1.0f);
    color = pow(max(color, 0.0f), 1.0f / 2.2f);
    return float4(color, 1.0f);
}
