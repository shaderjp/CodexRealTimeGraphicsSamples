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
    row_major float4x4 model;
    row_major float4x4 modelViewProjection;
    float4 cameraPosition;
    float4 lightDirection;
    float4 lightColor;
};

VK_BINDING(0) ConstantBuffer<SceneConstants> g_scene : register(b0);
VK_BINDING(1) Texture2D g_baseColorTexture : register(t0);
VK_BINDING(2) Texture2D g_metallicRoughnessTexture : register(t1);
VK_BINDING(3) Texture2D g_normalTexture : register(t2);
VK_BINDING(4) Texture2D g_occlusionTexture : register(t3);
VK_BINDING(5) SamplerState g_linearSampler : register(s0);

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

float3 SrgbToLinear(float3 value)
{
    return pow(max(value, 0.0f), 2.2f);
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

PSInput VSMain(VSInput input)
{
    PSInput result;
    float4 worldPosition = mul(float4(input.position, 1.0f), g_scene.model);
    result.position = mul(float4(input.position, 1.0f), g_scene.modelViewProjection);
    result.worldPosition = worldPosition.xyz;
    result.normal = normalize(mul(float4(input.normal, 0.0f), g_scene.model).xyz);
    result.tangent = float4(normalize(mul(float4(input.tangent.xyz, 0.0f), g_scene.model).xyz), input.tangent.w);
    result.texcoord = input.texcoord;
    return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float3 baseColor = SrgbToLinear(g_baseColorTexture.Sample(g_linearSampler, input.texcoord).rgb);
    float4 metallicRoughness = g_metallicRoughnessTexture.Sample(g_linearSampler, input.texcoord);
    float metallic = saturate(metallicRoughness.b);
    float roughness = clamp(metallicRoughness.g, 0.04f, 1.0f);
    float ao = g_occlusionTexture.Sample(g_linearSampler, input.texcoord).r;

    float3 normalSample = g_normalTexture.Sample(g_linearSampler, input.texcoord).xyz * 2.0f - 1.0f;
    float3 n = normalize(input.normal);
    float3 t = normalize(input.tangent.xyz - n * dot(n, input.tangent.xyz));
    float3 b = normalize(cross(n, t) * input.tangent.w);
    float3 normal = normalize(mul(normalSample, float3x3(t, b, n)));

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

    float3 specular = (distribution * geometry * fresnel) / max(4.0f * nDotV * nDotL, 0.0001f);
    float3 diffuse = (1.0f - fresnel) * (1.0f - metallic) * baseColor / PI;
    float3 ambient = baseColor * ao * 0.045f;
    float3 color = ambient + (diffuse + specular) * radiance * nDotL;

    color = color / (color + 1.0f);
    color = pow(max(color, 0.0f), 1.0f / 2.2f);
    return float4(color, 1.0f);
}
