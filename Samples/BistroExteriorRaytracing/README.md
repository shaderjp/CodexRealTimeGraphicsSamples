# BistroExteriorRaytracing

Japanese documentation is available in [README.ja.md](README.ja.md).

`BistroExteriorRaytracing` renders the Bistro Exterior scene through DirectX Raytracing and Vulkan Ray Tracing. Scene visibility is ray traced: there is no scene raster pass, no shadow map, and no mesh shader pass. The renderer traces into a UAV output, copies it to the swapchain, and then renders the ImGui overlay.

## Projects

- `BistroExteriorRaytracingD3D12`
- `BistroExteriorRaytracingVulkan`
- `BistroExteriorRaytracingShadowD3D12`
- `BistroExteriorRaytracingShadowVulkan`
- `BistroExteriorRaytracingGID3D12`
- `BistroExteriorRaytracingGIVulkan`

## Current Features

- Runtime `BistroExterior.fbx` import through `ThirdParty/assimp`
- DDS/TGA texture loading through `ThirdParty/DirectXTex`
- One static BLAS with one triangle geometry per Bistro draw item, plus one identity TLAS instance
- `GeometryIndex()`-based lookup into shared material and geometry records
- Alpha-masked material support with any-hit alpha testing for primary and shadow rays
- Bindless material texture access for D3D12 descriptor tables and Vulkan descriptor indexing
- Direct lighting, hard ray-traced shadows, and 1-spp diffuse 1-bounce GI with temporal accumulation
- ImGui controls for light, camera, sky, ray bias, shadows, GI strength, accumulation, and debug views
- Debug views: Final, Base Color, World Normal, Normal Texture, Hit Distance, Direct, Shadow, Indirect, Accumulation Samples
- Renderer stats for materials, textures, vertices, indices, primitives, BLAS geometries, TLAS instances, SBT records, output resolution, and ray tracing limits

## Screenshots

The screenshots below show the three staged renderer variants. The GI sample images focus on the main debug views used to inspect ray tracing output and accumulation inputs.

### Direct Light

| D3D12 | Vulkan |
| --- | --- |
| ![BistroExteriorRaytracing D3D12 direct lighting](../../docs/images/BistroExteriorRaytracing%20D3D12%202026_05_06%2013_26_50.png) | ![BistroExteriorRaytracing Vulkan direct lighting](../../docs/images/BistroExteriorRaytracing%20Vulkan%202026_05_06%2013_27_58.png) |

### Direct Light + Ray-Traced Shadow

| D3D12 | Vulkan |
| --- | --- |
| ![BistroExteriorRaytracing Shadow D3D12](../../docs/images/BistroExteriorRaytracing%20Shadow%20D3D12%202026_05_06%2013_29_01.png) | ![BistroExteriorRaytracing Shadow Vulkan](../../docs/images/BistroExteriorRaytracing%20Shadow%20Vulkan%202026_05_06%2013_29_43.png) |

### GI Debug Views

| D3D12 Final | D3D12 Hit Distance |
| --- | --- |
| ![BistroExteriorRaytracing GI D3D12 final](../../docs/images/BistroExteriorRaytracing%20GI%20D3D12%202026_05_06%2013_30_29.png) | ![BistroExteriorRaytracing GI D3D12 hit distance](../../docs/images/BistroExteriorRaytracing%20GI%20D3D12%202026_05_06%2013_30_39.png) |

| D3D12 Shadow | D3D12 Indirect |
| --- | --- |
| ![BistroExteriorRaytracing GI D3D12 shadow debug](../../docs/images/BistroExteriorRaytracing%20GI%20D3D12%202026_05_06%2013_30_50.png) | ![BistroExteriorRaytracing GI D3D12 indirect debug](../../docs/images/BistroExteriorRaytracing%20GI%20D3D12%202026_05_06%2013_30_55.png) |

| Vulkan Final | Vulkan Hit Distance |
| --- | --- |
| ![BistroExteriorRaytracing GI Vulkan final](../../docs/images/BistroExteriorRaytracing%20GI%20Vulkan%202026_05_06%2013_31_36.png) | ![BistroExteriorRaytracing GI Vulkan hit distance](../../docs/images/BistroExteriorRaytracing%20GI%20Vulkan%202026_05_06%2013_31_43.png) |

| Vulkan Shadow | Vulkan Indirect |
| --- | --- |
| ![BistroExteriorRaytracing GI Vulkan shadow debug](../../docs/images/BistroExteriorRaytracing%20GI%20Vulkan%202026_05_06%2013_31_54.png) | ![BistroExteriorRaytracing GI Vulkan indirect debug](../../docs/images/BistroExteriorRaytracing%20GI%20Vulkan%202026_05_06%2013_32_01.png) |

## Assets

The Bistro dataset is not stored in this repository. Download Amazon Lumberyard Bistro from NVIDIA ORCA and place the extracted `Bistro_v5_2` folder at the repository root:

```text
CodexdRealTimeGraphicsSamples/
  Bistro_v5_2/
    BistroExterior.fbx
    Textures/
```

Alternatively, set `BISTRO_ASSET_ROOT` to the folder that directly contains `BistroExterior.fbx`.

## Build

Initialize submodules first:

```powershell
git submodule update --init --recursive
```

Build the solution or individual projects with Visual Studio 2022 / MSBuild:

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" Samples\BistroExteriorRaytracing\BistroExteriorRaytracing.sln /p:Platform=x64 /p:Configuration=Debug
```

Individual project paths:

```powershell
Samples\BistroExteriorRaytracing\D3D12\Source\BistroExteriorRaytracingD3D12.vcxproj
Samples\BistroExteriorRaytracing\Vulkan\Source\BistroExteriorRaytracingVulkan.vcxproj
Samples\BistroExteriorRaytracing\D3D12Shadow\Source\BistroExteriorRaytracingShadowD3D12.vcxproj
Samples\BistroExteriorRaytracing\VulkanShadow\Source\BistroExteriorRaytracingShadowVulkan.vcxproj
Samples\BistroExteriorRaytracing\D3D12GI\Source\BistroExteriorRaytracingGID3D12.vcxproj
Samples\BistroExteriorRaytracing\VulkanGI\Source\BistroExteriorRaytracingGIVulkan.vcxproj
```

## Device Requirements

- D3D12 requires DXR ray tracing support. The sample reports the detected ray tracing tier and does not require Tier 1.2-specific features in this version.
- Vulkan requires Vulkan 1.2+, `VK_KHR_acceleration_structure`, `VK_KHR_ray_tracing_pipeline`, `VK_KHR_deferred_host_operations`, `VK_KHR_buffer_device_address`, `VK_KHR_spirv_1_4`, `VK_KHR_shader_float_controls`, and descriptor indexing support.

## Third Party

- `ThirdParty/assimp`: FBX scene import
- `ThirdParty/DirectXTex`: DDS/TGA texture loading
- `ThirdParty/imgui`: debug controls and renderer stats
