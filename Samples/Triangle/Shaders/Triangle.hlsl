//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

#if defined(VULKAN)
#define VK_LOCATION(n) [[vk::location(n)]]
#else
#define VK_LOCATION(n)
#endif

struct VSInput
{
    VK_LOCATION(0) float4 position : POSITION;
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

    result.position = input.position;
    result.color = input.color;

    return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    return input.color;
}
