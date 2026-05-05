# Triangle

Japanese documentation is available in [README.ja.md](README.ja.md).

`Triangle` is the first minimal rendering sample. It provides side-by-side Direct3D12 and Vulkan projects in one Visual Studio 2022 solution.

## Features

- Win32 window creation
- Direct3D12 device, swap chain, command list, RTV heap, and fence setup
- Vulkan instance, surface, swap chain, render pass, pipeline, and synchronization setup
- HLSL 6.6 shaders compiled at build time with DXC
- RGB triangle rendering

## Projects

- `Triangle.sln`
- `D3D12/Source/TriangleD3D12.vcxproj`
- `Vulkan/Source/TriangleVulkan.vcxproj`

## Screenshots

| Direct3D12 | Vulkan |
| --- | --- |
| ![Triangle D3D12](../../docs/images/Triangle%20D3D12%202026_05_05%2012_25_43.png) | ![Triangle Vulkan](../../docs/images/Triangle%20Vulkan%202026_05_05%2012_25_33.png) |

## Build

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" Samples\Triangle\Triangle.sln /restore /p:Platform=x64 /p:Configuration=Debug
```

Vulkan builds require the Vulkan SDK and `VULKAN_SDK`.
