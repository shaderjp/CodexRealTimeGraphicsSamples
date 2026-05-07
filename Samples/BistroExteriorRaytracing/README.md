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
- Direct lighting, hard ray-traced shadows, and diffuse 1-bounce GI with temporal accumulation
- Procedural gradient sky with a sun disk evaluated in the ray tracing miss shader
- Skylight contribution in the GI variants when secondary rays miss the scene
- Progressive low-discrepancy GI sampling with per-frame sample count, radiance clamp, and temporal clamp controls
- ImGui controls for light, camera, sky, ray bias, shadows, GI strength, GI noise reduction, accumulation, and debug views
- Debug views: Final, Base Color, World Normal, Normal Texture, Hit Distance, Direct, Shadow, Indirect, Accumulation Samples, Sky
- Renderer stats for materials, textures, vertices, indices, primitives, BLAS geometries, TLAS instances, SBT records, output resolution, and ray tracing limits

## Procedural Sky

The sky is evaluated procedurally in the ray tracing miss shader. No rasterized sky dome, skybox mesh, or extra texture asset is added. Primary camera rays that miss the Bistro geometry return a horizon/zenith/ground gradient plus a sun disk aligned with the existing directional light.

In the direct-light and shadow variants, this procedural sky is only the visible background and fallback miss radiance; surface lighting remains the existing direct-light model. In the GI variants, secondary diffuse rays use the same miss shader, so rays that escape the scene bring the sky radiance back as a simple skylight contribution to the 1-bounce indirect term.

## GI Noise Reduction

The GI variants still use a simple 1-bounce diffuse model, but the secondary ray sampling is progressive and low-discrepancy instead of purely hashed random. `GI Samples / Frame` controls how many secondary rays are averaged per frame, `GI Radiance Clamp` suppresses bright firefly samples before accumulation, and `GI Temporal Clamp` limits how far a new frame can move away from the accumulated history.

## Screenshots

The screenshots below show the three staged renderer variants. The GI sample images focus on the Final and Indirect debug views after procedural sky and lightweight GI noise-reduction controls were added.

### Direct Light

| D3D12 | Vulkan |
| --- | --- |
| ![BistroExteriorRaytracing D3D12 direct lighting](../../docs/images/BistroExteriorRaytracing%20D3D12%202026_05_06%2013_26_50.png) | ![BistroExteriorRaytracing Vulkan direct lighting](../../docs/images/BistroExteriorRaytracing%20Vulkan%202026_05_06%2013_27_58.png) |

### Direct Light + Ray-Traced Shadow

| D3D12 | Vulkan |
| --- | --- |
| ![BistroExteriorRaytracing Shadow D3D12](../../docs/images/BistroExteriorRaytracing%20Shadow%20D3D12%202026_05_06%2013_29_01.png) | ![BistroExteriorRaytracing Shadow Vulkan](../../docs/images/BistroExteriorRaytracing%20Shadow%20Vulkan%202026_05_06%2013_29_43.png) |

### GI Debug Views

| D3D12 Final | D3D12 Indirect |
| --- | --- |
| ![BistroExteriorRaytracing GI D3D12 final](../../docs/images/BistroExteriorRaytracing%20GI%20D3D12%202026_05_07%2022_15_05.png) | ![BistroExteriorRaytracing GI D3D12 indirect debug](../../docs/images/BistroExteriorRaytracing%20GI%20D3D12%202026_05_07%2022_15_21.png) |

| Vulkan Final | Vulkan Indirect |
| --- | --- |
| ![BistroExteriorRaytracing GI Vulkan final](../../docs/images/BistroExteriorRaytracing%20GI%20Vulkan%202026_05_07%2022_16_53.png) | ![BistroExteriorRaytracing GI Vulkan indirect debug](../../docs/images/BistroExteriorRaytracing%20GI%20Vulkan%202026_05_07%2022_17_05.png) |

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
