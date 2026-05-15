# BistroInteriorPathtracing

Japanese documentation is available in [README.ja.md](README.ja.md).

`BistroInteriorPathtracing` renders the Bistro Interior scene with DXR and Vulkan Ray Tracing as a progressive path tracer. It is based on the Exterior path tracing sample, but loads `BistroInterior.fbx` and uses interior-oriented camera, lighting, and movement defaults.

## Projects

- `BistroInteriorPathtracingD3D12`
- `BistroInteriorPathtracingVulkan`
- `BistroInteriorPathtracingReSTIRD3D12`
- `BistroInteriorPathtracingReSTIRVulkan`
- `BistroInteriorPathtracingReSTIRDID3D12`
- `BistroInteriorPathtracingReSTIRDIVulkan`

## Current Features

- Runtime `BistroInterior.fbx` import through `ThirdParty/assimp`
- DDS/TGA texture loading through `ThirdParty/DirectXTex`, including Interior material texture suffix fallback
- One static BLAS with one triangle geometry per Bistro draw item, plus one identity TLAS instance
- Alpha-masked material support with any-hit alpha testing for primary and visibility rays
- Bindless material texture access for D3D12 descriptor tables and Vulkan descriptor indexing
- Iterative raygen-owned path tracing loop with stochastic diffuse and GGX-style specular bounces
- Sun, procedural sky, emissive triangle, and procedural area-light next-event estimation
- Interior-focused local light list: capped emissive triangle lights plus warm bounds-based ceiling/counter area proxies
- Optional equirectangular environment map sampling, using `san_giuseppe_bridge_4k.hdr` from the Bistro root when present
- Progressive accumulation, debug views, ReSTIR GI / DI comparison variants, and the built-in lightweight denoiser from the Exterior path tracing sample

## Assets

The Bistro dataset is not stored in this repository. Download Amazon Lumberyard Bistro from NVIDIA ORCA and place the extracted `Bistro_v5_2` folder at the repository root:

```text
CodexdRealTimeGraphicsSamples/
  Bistro_v5_2/
    BistroInterior.fbx
    san_giuseppe_bridge_4k.hdr
    Textures/
```

Alternatively, set `BISTRO_ASSET_ROOT` to the folder that directly contains `BistroInterior.fbx`.

## Lighting Notes

The sample keeps the Exterior path tracing shader model and deliberately does not add transmission, volume absorption, or nested dielectric handling for the Wine scene. `BistroInterior.fbx` materials are treated with the same base color, normal, specular/roughness, metallic, alpha, and emissive channels as the other Bistro samples.

At load time, emissive materials are converted into a capped triangle-light list. Three warm procedural rectangular area lights are also generated from the scene bounds to provide stable local-light candidates for the baseline path tracer and ReSTIR DI.

## Build

Initialize submodules first:

```powershell
git submodule update --init --recursive
```

Build the solution or individual projects with Visual Studio 2022 / MSBuild:

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" Samples\BistroInteriorPathtracing\BistroInteriorPathtracing.sln /p:Platform=x64 /p:Configuration=Debug
```

Individual project paths:

```powershell
Samples\BistroInteriorPathtracing\D3D12\Source\BistroInteriorPathtracingD3D12.vcxproj
Samples\BistroInteriorPathtracing\Vulkan\Source\BistroInteriorPathtracingVulkan.vcxproj
Samples\BistroInteriorPathtracing\D3D12ReSTIR\Source\BistroInteriorPathtracingReSTIRD3D12.vcxproj
Samples\BistroInteriorPathtracing\VulkanReSTIR\Source\BistroInteriorPathtracingReSTIRVulkan.vcxproj
Samples\BistroInteriorPathtracing\D3D12ReSTIRDI\Source\BistroInteriorPathtracingReSTIRDID3D12.vcxproj
Samples\BistroInteriorPathtracing\VulkanReSTIRDI\Source\BistroInteriorPathtracingReSTIRDIVulkan.vcxproj
```

## Device Requirements

- D3D12 requires DXR ray tracing support. The sample reports the detected ray tracing tier and does not require Tier 1.2-specific features.
- Vulkan requires Vulkan 1.2+, `VK_KHR_acceleration_structure`, `VK_KHR_ray_tracing_pipeline`, `VK_KHR_deferred_host_operations`, `VK_KHR_buffer_device_address`, `VK_KHR_spirv_1_4`, `VK_KHR_shader_float_controls`, and descriptor indexing support.

## Third Party

- `ThirdParty/assimp`: FBX scene import
- `ThirdParty/DirectXTex`: DDS/TGA texture loading
- `ThirdParty/imgui`: debug controls and renderer stats
