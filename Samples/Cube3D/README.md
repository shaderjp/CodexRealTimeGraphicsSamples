# Cube3D

Japanese documentation is available in [README.ja.md](README.ja.md).

`Cube3D` expands the minimal triangle sample into a simple 3D object. It renders a rotating cube with vertex and index buffers, a transform constant buffer, and a depth buffer.

## Features

- Direct3D12 and Vulkan projects in one Visual Studio 2022 solution
- Indexed cube geometry
- Per-frame transform update through a constant/uniform buffer
- Depth buffer setup and depth testing
- HLSL 6.6 shaders compiled at build time with DXC

## Projects

- `Cube3D.sln`
- `D3D12/Source/Cube3DD3D12.vcxproj`
- `Vulkan/Source/Cube3DVulkan.vcxproj`

## Screenshots

| Direct3D12 | Vulkan |
| --- | --- |
| ![Cube3D D3D12](../../docs/images/Cube3D%20D3D12%202026_05_05%2012_20_40.png) | ![Cube3D Vulkan](../../docs/images/Cube3D%20Vulkan%202026_05_05%2012_19_58.png) |

## Build

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" Samples\Cube3D\Cube3D.sln /restore /p:Platform=x64 /p:Configuration=Debug
```

Vulkan builds require the Vulkan SDK and `VULKAN_SDK`.
