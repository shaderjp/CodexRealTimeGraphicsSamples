# ImGuiLighting

Japanese documentation is available in [README.ja.md](README.ja.md).

`ImGuiLighting` loads the glTF 2.0 SciFiHelmet asset and renders it with the same small metallic-roughness PBR shader used by `Samples/SciFiHelmet`. The Direct3D12 and Vulkan projects add Dear ImGui controls for editing one directional light at runtime.

## Features

- Direct3D12 and Vulkan projects in one Visual Studio 2022 solution
- Dear ImGui integrated from `ThirdParty/imgui`
- Runtime UI window named `Directional Light`
- Editable directional light direction, color, intensity, and reset button
- glTF 2.0 SciFiHelmet mesh and base color, metallic-roughness, normal, and ambient occlusion textures
- HLSL 6.6 PBR shader compiled with DXC for both D3D12 and Vulkan
- Depth buffer setup and depth testing

## Projects

- `ImGuiLighting.sln`
- `D3D12/Source/ImGuiLightingD3D12.vcxproj`
- `Vulkan/Source/ImGuiLightingVulkan.vcxproj`

## Screenshots

| Direct3D12 | Vulkan |
| --- | --- |
| ![ImGuiLighting D3D12](../../docs/images/ImGuiLighting%20D3D12%202026_05_05%2012_24_18.png) | ![ImGuiLighting Vulkan](../../docs/images/ImGuiLighting%20Vulkan%202026_05_05%2012_24_47.png) |

## Build

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" Samples\ImGuiLighting\ImGuiLighting.sln /restore /p:Platform=x64 /p:Configuration=Debug
```

Vulkan builds require the Vulkan SDK and `VULKAN_SDK`.

## Asset

The SciFiHelmet asset is copied from the Khronos glTF Sample Models repository into `Assets/SciFiHelmet`. See `Assets/SciFiHelmet/README.md` for the original attribution.
