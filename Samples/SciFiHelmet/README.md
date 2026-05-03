# SciFiHelmet

Japanese documentation is available in [README.ja.md](README.ja.md).

`SciFiHelmet` loads the glTF 2.0 SciFiHelmet asset and renders it with a small metallic-roughness PBR shader. The sample keeps Direct3D12 and Vulkan implementations side by side so resource binding, texture upload, and shader compilation can be compared.

## Features

- Direct3D12 and Vulkan projects in one Visual Studio 2022 solution
- glTF 2.0 SciFiHelmet mesh data loaded from the sample asset folder
- Base color, metallic-roughness, normal, and ambient occlusion textures
- HLSL 6.6 PBR shader compiled with DXC for both D3D12 and Vulkan
- One directional light
- Depth buffer setup and depth testing

## Projects

- `SciFiHelmet.sln`
- `D3D12/Source/SciFiHelmetD3D12.vcxproj`
- `Vulkan/Source/SciFiHelmetVulkan.vcxproj`

## Build

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" Samples\SciFiHelmet\SciFiHelmet.sln /restore /p:Platform=x64 /p:Configuration=Debug
```

Vulkan builds require the Vulkan SDK and `VULKAN_SDK`.

## Asset

The SciFiHelmet asset is copied from the Khronos glTF Sample Models repository into `Assets/SciFiHelmet`. See `Assets/SciFiHelmet/README.md` for the original attribution.
