# BistroExteriorPathtracing

Japanese documentation is available in [README.ja.md](README.ja.md).

`BistroExteriorPathtracing` renders the Bistro Exterior scene with DXR and Vulkan Ray Tracing as a progressive path tracer. Scene visibility is fully ray traced: there is no scene raster pass, no shadow map, and no mesh shader pass. The ray tracing output is accumulated in a UAV/storage image, copied to the swapchain, and then the ImGui overlay is drawn.

## Projects

- `BistroExteriorPathtracingD3D12`
- `BistroExteriorPathtracingVulkan`
- `BistroExteriorPathtracingReSTIRD3D12`
- `BistroExteriorPathtracingReSTIRVulkan`
- `BistroExteriorPathtracingReSTIRDID3D12`
- `BistroExteriorPathtracingReSTIRDIVulkan`
- `BistroExteriorPathtracingReSTIRPTEnhancedD3D12`
- `BistroExteriorPathtracingReSTIRPTEnhancedVulkan`

## Screenshots

The first two captures use the same D3D12 camera/settings and show the built-in denoiser disabled/enabled. The remaining captures show the Vulkan baseline and ReSTIR DI comparison variants with the denoiser controls available from ImGui.

| D3D12 denoiser off | D3D12 denoiser on |
| --- | --- |
| ![D3D12 denoiser disabled](<../../docs/images/BistroExteriorPathtracing D3D12 2026_05_08 0_36_56.png>) | ![D3D12 denoiser enabled](<../../docs/images/BistroExteriorPathtracing D3D12 2026_05_08 0_37_03.png>) |

| ReSTIR DI D3D12 | ReSTIR DI Vulkan |
| --- | --- |
| ![ReSTIR DI D3D12](<../../docs/images/BistroExteriorPathtracing ReSTIR DI D3D12 2026_05_08 0_37_45.png>) | ![ReSTIR DI Vulkan](<../../docs/images/BistroExteriorPathtracing ReSTIR DI Vulkan 2026_05_08 0_38_38.png>) |

![Vulkan path tracing with denoiser](<../../docs/images/BistroExteriorPathtracing Vulkan 2026_05_08 0_39_16.png>)

## Current Features

- Runtime `BistroExterior.fbx` import through `ThirdParty/assimp`
- DDS/TGA texture loading through `ThirdParty/DirectXTex`
- One static BLAS with one triangle geometry per Bistro draw item, plus one identity TLAS instance
- `GeometryIndex()`-based lookup into shared material and geometry records
- Alpha-masked material support with any-hit alpha testing for primary and visibility rays
- Bindless material texture access for D3D12 descriptor tables and Vulkan descriptor indexing
- Iterative raygen-owned path tracing loop with ray pipeline recursion depth kept at 1
- Diffuse and GGX-style stochastic specular bounces using base color, normal, roughness, metallic, emissive, and alpha data
- Sun and procedural sky next-event estimation with alpha-aware visibility rays
- Procedural gradient sky with a sun disk evaluated in the ray tracing miss shader
- Emissive material support that builds a capped emissive-triangle light list for next-event estimation
- Procedural rectangular area lights for window/sign/interior-style lighting tests without adding scene geometry
- Optional 2D environment map sampling in the miss shader and sky NEE path, with procedural sky as the fallback
- Progressive accumulation with samples per frame, radiance clamp, temporal clamp, max accumulated sample count, and freeze/reset controls
- Built-in lightweight denoiser with normal/depth/albedo/luminance-guided multi-scale cross-bilateral filtering; no extra third-party dependency is required
- ReSTIR GI comparison projects with a separate shader variant, ReSTIR-oriented controls, extra candidate sampling, current/history/spatial reservoir buffers, and a compute temporal/spatial reuse pass
- ReSTIR DI comparison projects that reuse the same reservoir flow for primary-hit direct lighting from the sun and local light list
- ReSTIR PT Enhanced research projects with a selected-path reservoir payload, GBuffer-validated temporal reuse, paired spatial reuse offsets, duplication-map temporal caps, replay task classification, and dedicated debug views
- ImGui controls for light, camera, sky, environment map, emissive lights, procedural area lights, ray bias, path depth, accumulation, denoiser settings, ReSTIR settings, and debug views
- Debug views: Final, Base Color, World Normal, Normal Texture, Roughness, Metallic, Emissive, Hit Distance, Direct NEE, Indirect, Bounce Count, Accumulation Samples, Sky, Reservoir Weight, Temporal Reuse, Spatial Reuse
- Renderer stats for materials, textures, vertices, indices, primitives, BLAS geometries, TLAS instances, SBT records, light list counts, output resolution, accumulation samples, and ray tracing limits

## ReSTIR Status

The ReSTIR projects are added as separate D3D12/Vulkan executables so the path-tracing baseline, ReSTIR GI, and ReSTIR DI can be compared side by side. The ReSTIR GI variant generates a current per-pixel indirect-light reservoir in raygen. The ReSTIR DI variant instead reservoirs primary-hit direct lighting from the sun and the local light list. Both variants combine current, history, and neighborhood reservoirs in a compute temporal/spatial reuse pass, and copy the resolved spatial reservoirs back to history for the next frame.

The implementation remains intentionally compact for this sample: it focuses on reservoir reuse, debug visibility, and comparable D3D12/Vulkan resource flow rather than a full production ReSTIR pipeline with visibility validation or disocclusion tests.

The ReSTIR PT Enhanced projects are separate D3D12/Vulkan executables that keep the baseline and existing ReSTIR GI/DI variants untouched. The current implementation stores a compressed selected-path reservoir, writes Enhanced-only current/history GBuffer resources, validates temporal and spatial history against primary-hit position/normal/material data, uses three CPU-generated paired spatial reuse offset tables, tracks a duplication map for temporal cap reduction, and exposes reservoir, path-depth, reconnection, temporal, paired-spatial, duplication, replay-task, and replay-validation debug views.

Remaining research items are intentionally documented as work-in-progress: full path replay with shader-side random replay and forced NEE reconnection is still approximated by the current selected-path payload, pairwise MIS/dual-footprint validation is not yet a complete paper-accurate implementation, and replay compaction currently classifies tasks and reports them rather than replacing the fallback dispatch path with a full indirect replay pipeline.

## Denoiser

All path tracing variants include a small in-sample denoiser implemented as a compute pass, without adding another ThirdParty dependency. Raygen writes auxiliary normal/depth and albedo/roughness AOVs, and the compute shader filters the accumulated linear color with a multi-scale cross-bilateral filter before the swapchain copy. It is applied only to the `Final` debug view; diagnostic debug views remain unfiltered.

The ImGui `Denoiser` section exposes enable/disable, spatial iterations, normal sigma, depth sigma, luminance sigma, albedo sigma, and blend strength. The defaults are intentionally conservative so edges are preserved while early progressive samples look less noisy.

## Assets

The Bistro dataset is not stored in this repository. Download Amazon Lumberyard Bistro from NVIDIA ORCA and place the extracted `Bistro_v5_2` folder at the repository root:

```text
CodexdRealTimeGraphicsSamples/
  Bistro_v5_2/
    BistroExterior.fbx
    Textures/
```

Alternatively, set `BISTRO_ASSET_ROOT` to the folder that directly contains `BistroExterior.fbx`.

## Lighting Extensions

The path tracing projects build a compact `RtLight` list at load time. Emissive materials contribute triangle lights, capped to keep the sample lightweight, and three procedural rectangular area lights are added around the Bistro bounds so the baseline and ReSTIR projects have non-sun local lighting to sample. These procedural lights are analytical lights only; they illuminate the scene but do not add visible mesh geometry.

An optional equirectangular 2D environment map can be enabled through the `Environment Map` control. Set `BISTRO_ENVIRONMENT_MAP` to an `.hdr`, `.dds`, `.tga`, `.png`, `.jpg`, or `.jpeg` file, or place a sky/environment-named file under the Bistro asset root or its `Textures`, `Environment`, or `Environments` subfolder. If no texture is found, the sample keeps using the procedural sky fallback.

## Build

Initialize submodules first:

```powershell
git submodule update --init --recursive
```

Build the solution or individual projects with Visual Studio 2022 / MSBuild:

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" Samples\BistroExteriorPathtracing\BistroExteriorPathtracing.sln /p:Platform=x64 /p:Configuration=Debug
```

Individual project paths:

```powershell
Samples\BistroExteriorPathtracing\D3D12\Source\BistroExteriorPathtracingD3D12.vcxproj
Samples\BistroExteriorPathtracing\Vulkan\Source\BistroExteriorPathtracingVulkan.vcxproj
Samples\BistroExteriorPathtracing\D3D12ReSTIR\Source\BistroExteriorPathtracingReSTIRD3D12.vcxproj
Samples\BistroExteriorPathtracing\VulkanReSTIR\Source\BistroExteriorPathtracingReSTIRVulkan.vcxproj
Samples\BistroExteriorPathtracing\D3D12ReSTIRDI\Source\BistroExteriorPathtracingReSTIRDID3D12.vcxproj
Samples\BistroExteriorPathtracing\VulkanReSTIRDI\Source\BistroExteriorPathtracingReSTIRDIVulkan.vcxproj
Samples\BistroExteriorPathtracing\D3D12ReSTIRPTEnhanced\Source\BistroExteriorPathtracingReSTIRPTEnhancedD3D12.vcxproj
Samples\BistroExteriorPathtracing\VulkanReSTIRPTEnhanced\Source\BistroExteriorPathtracingReSTIRPTEnhancedVulkan.vcxproj
```

## Device Requirements

- D3D12 requires DXR ray tracing support. The sample reports the detected ray tracing tier and does not require Tier 1.2-specific features in this version.
- Vulkan requires Vulkan 1.2+, `VK_KHR_acceleration_structure`, `VK_KHR_ray_tracing_pipeline`, `VK_KHR_deferred_host_operations`, `VK_KHR_buffer_device_address`, `VK_KHR_spirv_1_4`, `VK_KHR_shader_float_controls`, and descriptor indexing support.

## Third Party

- `ThirdParty/assimp`: FBX scene import
- `ThirdParty/DirectXTex`: DDS/TGA texture loading
- `ThirdParty/imgui`: debug controls and renderer stats
