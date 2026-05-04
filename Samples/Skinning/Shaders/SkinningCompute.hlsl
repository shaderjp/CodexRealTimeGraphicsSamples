#if defined(VULKAN)
#define VK_LOCATION(n) [[vk::location(n)]]
#define VK_BINDING(n) [[vk::binding(n, 0)]]
#else
#define VK_LOCATION(n)
#define VK_BINDING(n)
#endif

static const uint VertexCount = 3273;

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

struct SourceVertex
{
    float4 positionTexcoordX;
    float4 normalTexcoordY;
    uint4 joints;
    float4 weights;
};

struct SkinnedVertex
{
    float4 positionTexcoordX;
    float4 normalTexcoordY;
};

VK_BINDING(0) ConstantBuffer<SceneConstants> g_scene : register(b0);
VK_BINDING(1) ConstantBuffer<JointConstants> g_skin : register(b1);
VK_BINDING(2) Texture2D g_baseColorTexture : register(t0);
VK_BINDING(3) SamplerState g_linearSampler : register(s0);
VK_BINDING(4) StructuredBuffer<SourceVertex> g_sourceVertices : register(t1);
VK_BINDING(5) RWStructuredBuffer<SkinnedVertex> g_skinnedVertices : register(u0);

struct VSInput
{
    VK_LOCATION(0) float3 position : POSITION;
    VK_LOCATION(1) float texcoordX : TEXCOORD0;
    VK_LOCATION(2) float3 normal : NORMAL;
    VK_LOCATION(3) float texcoordY : TEXCOORD1;
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

row_major float4x4 BuildSkinMatrix(uint4 joints, float4 weights)
{
    return
        g_skin.joints[joints.x] * weights.x +
        g_skin.joints[joints.y] * weights.y +
        g_skin.joints[joints.z] * weights.z +
        g_skin.joints[joints.w] * weights.w;
}

[numthreads(64, 1, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint vertexIndex = dispatchThreadId.x;
    if (vertexIndex >= VertexCount)
    {
        return;
    }

    SourceVertex source = g_sourceVertices[vertexIndex];
    row_major float4x4 skinMatrix = BuildSkinMatrix(source.joints, source.weights);

    SkinnedVertex result;
    result.positionTexcoordX = float4(mul(float4(source.positionTexcoordX.xyz, 1.0f), skinMatrix).xyz, source.positionTexcoordX.w);
    result.normalTexcoordY = float4(normalize(mul(float4(source.normalTexcoordY.xyz, 0.0f), skinMatrix).xyz), source.normalTexcoordY.w);
    g_skinnedVertices[vertexIndex] = result;
}

PSInput VSMain(VSInput input)
{
    PSInput result;
    result.position = mul(float4(input.position, 1.0f), g_scene.modelViewProjection);
    result.normal = normalize(mul(float4(input.normal, 0.0f), g_scene.model).xyz);
    result.texcoord = float2(input.texcoordX, input.texcoordY);
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
