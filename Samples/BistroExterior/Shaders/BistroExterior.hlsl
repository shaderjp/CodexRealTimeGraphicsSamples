#if defined(VULKAN)
#define VK_LOCATION(n) [[vk::location(n)]]
#define VK_BINDING(n) [[vk::binding(n, 0)]]
#else
#define VK_LOCATION(n)
#define VK_BINDING(n)
#endif

static const float PI = 3.14159265359f;

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

VK_BINDING(0) ConstantBuffer<SceneConstants> g_scene : register(b0);
VK_BINDING(1) ConstantBuffer<MaterialConstants> g_material : register(b1);
VK_BINDING(2) Texture2D g_baseColorTexture : register(t0);
VK_BINDING(3) Texture2D g_normalTexture : register(t1);
VK_BINDING(4) Texture2D g_specularTexture : register(t2);
VK_BINDING(5) Texture2D g_emissiveTexture : register(t3);
VK_BINDING(6) SamplerState g_linearSampler : register(s0);

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

    float3 normalTexture = g_normalTexture.Sample(g_linearSampler, input.texcoord).xyz;
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

    float3 specular = (distribution * geometry * fresnel) / max(4.0f * nDotV * nDotL, 0.0001f);
    float3 diffuse = (1.0f - fresnel) * (1.0f - metallic) * baseColor / PI;
    float3 ambient = baseColor * ao * 0.055f;
    float3 color = ambient + (diffuse + specular) * radiance * nDotL + emissive;

    color = color / (color + 1.0f);
    color = pow(max(color, 0.0f), 1.0f / 2.2f);
    return float4(color, 1.0f);
}
