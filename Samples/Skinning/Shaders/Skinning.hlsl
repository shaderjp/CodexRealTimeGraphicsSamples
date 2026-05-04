#if defined(VULKAN)
#define VK_LOCATION(n) [[vk::location(n)]]
#define VK_BINDING(n) [[vk::binding(n, 0)]]
#else
#define VK_LOCATION(n)
#define VK_BINDING(n)
#endif

struct SceneConstants
{
    row_major float4x4 model;
    row_major float4x4 modelViewProjection;
    float4 lightDirection;
    float4 lightColor;
};

struct JointConstants
{
    row_major float4x4 joints[64];
};

VK_BINDING(0) ConstantBuffer<SceneConstants> g_scene : register(b0);
VK_BINDING(1) ConstantBuffer<JointConstants> g_skin : register(b1);
VK_BINDING(2) Texture2D g_baseColorTexture : register(t0);
VK_BINDING(3) SamplerState g_linearSampler : register(s0);

struct VSInput
{
    VK_LOCATION(0) float3 position : POSITION;
    VK_LOCATION(1) float3 normal : NORMAL;
    VK_LOCATION(2) float2 texcoord : TEXCOORD0;
    VK_LOCATION(3) uint4 joints : BLENDINDICES;
    VK_LOCATION(4) float4 weights : BLENDWEIGHT;
};

struct PSInput
{
    float4 position : SV_POSITION;
    VK_LOCATION(0) float3 normal : NORMAL;
    VK_LOCATION(1) float2 texcoord : TEXCOORD0;
};

float3 SrgbToLinear(float3 value)
{
    return pow(max(value, 0.0f), 2.2f);
}

PSInput VSMain(VSInput input)
{
    row_major float4x4 skinMatrix =
        g_skin.joints[input.joints.x] * input.weights.x +
        g_skin.joints[input.joints.y] * input.weights.y +
        g_skin.joints[input.joints.z] * input.weights.z +
        g_skin.joints[input.joints.w] * input.weights.w;

    float4 skinnedPosition = mul(float4(input.position, 1.0f), skinMatrix);
    float3 skinnedNormal = normalize(mul(float4(input.normal, 0.0f), skinMatrix).xyz);

    PSInput result;
    result.position = mul(skinnedPosition, g_scene.modelViewProjection);
    result.normal = normalize(mul(float4(skinnedNormal, 0.0f), g_scene.model).xyz);
    result.texcoord = input.texcoord;
    return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float3 baseColor = SrgbToLinear(g_baseColorTexture.Sample(g_linearSampler, input.texcoord).rgb);
    float3 normal = normalize(input.normal);
    float3 lightDirection = normalize(-g_scene.lightDirection.xyz);
    float nDotL = saturate(dot(normal, lightDirection));
    float3 color = baseColor * (0.12f + g_scene.lightColor.rgb * g_scene.lightColor.a * nDotL);
    color = color / (color + 1.0f);
    color = pow(max(color, 0.0f), 1.0f / 2.2f);
    return float4(color, 1.0f);
}
