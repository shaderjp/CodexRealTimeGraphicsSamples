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
    float4 denoiseOptions;
    float4 denoiseOptions2;
};

VK_BINDING(1, 0) RWTexture2D<float4> g_output : register(u0, space0);
VK_BINDING(2, 0) RWTexture2D<float4> g_accumulation : register(u1, space0);
VK_BINDING(3, 0) ConstantBuffer<SceneConstants> g_scene : register(b0, space0);
VK_BINDING(13, 0) RWTexture2D<float4> g_denoiseAov0 : register(u5, space0);
VK_BINDING(14, 0) RWTexture2D<float4> g_denoiseAov1 : register(u6, space0);

float Luminance(float3 color)
{
    return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

float3 Tonemap(float3 color)
{
    color = max(color, 0.0f.xxx);
    color = color / (1.0f.xxx + color);
    return pow(color, 1.0f / 2.2f);
}

float3 DecodeNormal(float4 aov)
{
    return normalize(aov.xyz * 2.0f - 1.0f);
}

float KernelWeight(int offset)
{
    int a = abs(offset);
    return a == 0 ? 6.0f : (a == 1 ? 4.0f : 1.0f);
}

float3 LoadAccumulatedColor(int2 pixel)
{
    return max(g_accumulation[uint2(pixel)].rgb, 0.0f.xxx);
}

[numthreads(8, 8, 1)]
void DenoiseCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 dimensions = (uint2)round(g_scene.rayOptions.zw);
    uint2 pixel = dispatchThreadId.xy;
    if (pixel.x >= dimensions.x || pixel.y >= dimensions.y)
    {
        return;
    }

    uint debugMode = (uint)round(g_scene.debugOptions.x);
    if (debugMode != 0u || g_scene.denoiseOptions.x < 0.5f)
    {
        return;
    }

    int2 centerPixel = int2(pixel);
    float3 centerColor = LoadAccumulatedColor(centerPixel);
    float4 centerAov0 = g_denoiseAov0[pixel];
    float4 centerAov1 = g_denoiseAov1[pixel];
    float centerDepth = centerAov0.w;
    if (centerDepth <= 0.0f)
    {
        g_output[pixel] = float4(Tonemap(centerColor), 1.0f);
        return;
    }

    float3 centerNormal = DecodeNormal(centerAov0);
    float3 centerAlbedo = max(centerAov1.rgb, 0.0f.xxx);
    float centerLum = Luminance(centerColor);

    uint iterations = clamp((uint)round(g_scene.denoiseOptions.y), 0u, 4u);
    if (iterations == 0u)
    {
        g_output[pixel] = float4(Tonemap(centerColor), 1.0f);
        return;
    }

    float normalSigma = max(g_scene.denoiseOptions.z, 0.001f);
    float depthSigma = max(g_scene.denoiseOptions.w, 0.0005f);
    float luminanceSigma = max(g_scene.denoiseOptions2.x, 0.001f);
    float albedoSigma = max(g_scene.denoiseOptions2.y, 0.001f);
    float strength = saturate(g_scene.denoiseOptions2.z);

    float3 sumColor = centerColor;
    float sumWeight = 1.0f;

    [loop]
    for (uint iteration = 0u; iteration < 4u; ++iteration)
    {
        if (iteration >= iterations)
        {
            break;
        }

        int stepWidth = (int)(1u << iteration);
        [unroll]
        for (int y = -2; y <= 2; ++y)
        {
            [unroll]
            for (int x = -2; x <= 2; ++x)
            {
                if (x == 0 && y == 0)
                {
                    continue;
                }

                int2 samplePixel = clamp(centerPixel + int2(x, y) * stepWidth, int2(0, 0), int2((int)dimensions.x, (int)dimensions.y) - int2(1, 1));
                float4 sampleAov0 = g_denoiseAov0[uint2(samplePixel)];
                float sampleDepth = sampleAov0.w;
                if (sampleDepth <= 0.0f)
                {
                    continue;
                }

                float3 sampleNormal = DecodeNormal(sampleAov0);
                float3 sampleColor = LoadAccumulatedColor(samplePixel);
                float3 sampleAlbedo = max(g_denoiseAov1[uint2(samplePixel)].rgb, 0.0f.xxx);

                float normalWeight = exp((dot(centerNormal, sampleNormal) - 1.0f) / normalSigma);
                float depthWeight = exp(-abs(sampleDepth - centerDepth) / max(centerDepth * depthSigma, 0.02f));
                float luminanceWeight = exp(-abs(Luminance(sampleColor) - centerLum) / luminanceSigma);
                float albedoWeight = exp(-length(sampleAlbedo - centerAlbedo) / albedoSigma);
                float spatialWeight = KernelWeight(x) * KernelWeight(y) / 256.0f;
                float weight = spatialWeight * normalWeight * depthWeight * luminanceWeight * albedoWeight;

                sumColor += sampleColor * weight;
                sumWeight += weight;
            }
        }
    }

    float3 filtered = sumColor / max(sumWeight, 0.0001f);
    float3 resolved = lerp(centerColor, filtered, strength);
    g_output[pixel] = float4(Tonemap(resolved), 1.0f);
}
