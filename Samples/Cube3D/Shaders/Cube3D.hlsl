#if defined(VULKAN)
#define VK_LOCATION(n) [[vk::location(n)]]
#else
#define VK_LOCATION(n)
#endif

struct TransformConstants
{
    row_major float4x4 modelViewProjection;
};

ConstantBuffer<TransformConstants> g_transform : register(b0);

struct VSInput
{
    VK_LOCATION(0) float3 position : POSITION;
    VK_LOCATION(1) float4 color : COLOR;
};

struct PSInput
{
    float4 position : SV_POSITION;
    VK_LOCATION(0) float4 color : COLOR;
};

PSInput VSMain(VSInput input)
{
    PSInput result;
    result.position = mul(float4(input.position, 1.0f), g_transform.modelViewProjection);
    result.color = input.color;
    return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    return input.color;
}
