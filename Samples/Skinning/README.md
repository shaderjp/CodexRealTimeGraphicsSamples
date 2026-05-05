# Skinning

Japanese documentation is available in [README.ja.md](README.ja.md).

`Skinning` loads the glTF 2.0 Cesium Man asset and renders it with vertex shader skinning or compute shader skinning. The Direct3D12 and Vulkan projects share HLSL 6.6 shaders and asset data so the skinning pipelines can be compared directly.

## Features

- Direct3D12 and Vulkan projects in one Visual Studio 2022 solution
- glTF 2.0 Cesium Man mesh, skeleton, inverse bind matrices, and animation data
- Vertex buffer attributes for position, normal, texture coordinate, joint indices, and joint weights
- Vertex shader skinning using a joint matrix constant buffer
- Compute shader skinning into a skinned vertex buffer, then graphics rendering from that buffer
- 16-byte compute vertex buffer layout shared by Direct3D12 and Vulkan storage buffers
- Base color texture sampling and one directional light
- Depth buffer setup and depth testing

## Projects

- `Skinning.sln`
- `D3D12/Source/SkinningD3D12.vcxproj`
- `Vulkan/Source/SkinningVulkan.vcxproj`
- `D3D12Compute/Source/SkinningComputeD3D12.vcxproj`
- `VulkanCompute/Source/SkinningComputeVulkan.vcxproj`

## Screenshots

Vertex shader skinning:

| Direct3D12 | Vulkan |
| --- | --- |
| ![Skinning D3D12](../../docs/images/Skinning%20D3D12%202026_05_05%2012_22_39.png) | ![Skinning Vulkan](../../docs/images/Skinning%20Vulkan%202026_05_05%2012_22_48.png) |

Compute shader skinning:

| Direct3D12 | Vulkan |
| --- | --- |
| ![Skinning Compute D3D12](../../docs/images/Skinning%20Compute%20D3D12%202026_05_05%2012_22_57.png) | ![Skinning Compute Vulkan](../../docs/images/Skinning%20Compute%20Vulkan%202026_05_05%2012_23_06.png) |

## Build

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" Samples\Skinning\Skinning.sln /restore /p:Platform=x64 /p:Configuration=Debug
```

Vulkan builds require the Vulkan SDK and `VULKAN_SDK`.

The compute shader projects use the same `SkinningCompute.hlsl` source. The source and output vertex records are packed as `float4` fields so the Direct3D12 structured buffer layout and the Vulkan storage buffer layout remain identical.

## Asset

The Cesium Man asset is copied from the Khronos glTF Sample Models repository into `Assets/CesiumMan`. See `Assets/CesiumMan/README.md` for the original attribution.
