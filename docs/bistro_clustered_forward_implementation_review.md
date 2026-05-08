# Bistro Clustered Forward Implementation Review

This document reviews `Samples/BistroExteriorClusteredForward`, the sample that adds many local lights to Bistro Exterior and compares direct many-light forward shading with clustered forward shading.

Read the series in this order:

1. [Pre-Bistro implementation review](pre_bistro_implementation_review.md)
2. [Bistro implementation review](bistro_implementation_review.md)
3. This document
4. [Bistro Mesh Shader implementation review](bistro_mesh_shader_implementation_review.md)
5. [Bistro Raytracing implementation review](bistro_raytracing_implementation_review.md)
6. [Bistro Pathtracing implementation review](bistro_pathtracing_implementation_review.md)

## Scope

`Samples/BistroExteriorClusteredForward` contains four projects:

| Type | D3D12 | Vulkan | Role |
| --- | --- | --- | --- |
| Many Lights | `BistroExteriorManyLightsD3D12` | `BistroExteriorManyLightsVulkan` | Loops over active local lights directly in the pixel shader |
| Clustered Forward | `BistroExteriorClusteredForwardD3D12` | `BistroExteriorClusteredForwardVulkan` | Builds a per-cluster light list with compute, then shades from that list |

All four projects use the same Bistro scene, materials, generated light list, and directional shadow map. The comparison focuses on how local lights are selected for shading.

## What This Sample Adds

Compared with the base Bistro raster renderer, this sample adds:

- Emissive material / texture light proxies
- Procedural area-light proxies
- Raster-friendly sphere light proxies
- ManyLights direct local-light loop
- ClusteredForward compute pass
- Screen tiles and logarithmic depth slices
- Cluster record, light-index, and stats buffers
- Cluster debug views
- Directional shadow-map support

The goal is not to build a production clustered renderer. The goal is to make the data flow visible on a real scene.

## ManyLights Versus ClusteredForward

ManyLights is the baseline:

```text
pixel
  -> loop all active local lights
  -> evaluate distance falloff and BRDF
```

It is easy to read, but every shaded pixel checks many lights even if most of them are irrelevant.

ClusteredForward adds a compute pass:

```text
viewport
  -> 16x16 screen tiles
  -> logarithmic depth slices
  -> sphere-light / cluster tests
  -> cluster records and light indices
  -> pixel shader reads only its cluster lights
```

The pixel shader computes its tile and depth slice, finds a cluster id, then loops only the lights assigned to that cluster.

## Light Generation

Files:

- `../Samples/BistroExteriorClusteredForward/Common/BistroScene.h`
- `../Samples/BistroExteriorClusteredForward/Common/BistroScene.cpp`

The raster light list follows the same idea as the path tracing light list, but converts emissive and procedural lights into sphere proxies.

Important structures:

| Structure | Role |
| --- | --- |
| `RasterLight` | GPU local light with position, radius, radiance, and source type |
| `LightBuildResult` | Generated light array and source counts |
| `ClusterRecord` | Per-cluster offset, count, and overflow flag |
| `ClusterStats` | Runtime statistics for the cluster build |

This keeps the shader simple: every local light is evaluated through the same point/sphere proxy function.

## Cluster Build

File:

- `../Samples/BistroExteriorClusteredForward/Shaders/BistroExteriorClusteredForward.hlsl`

Important entries:

- `CSResetStats`
- `CSBuildClusters`
- `PSMain`

The compute pass divides the view into screen tiles and depth slices. For each cluster, it tests light spheres and appends matching light indices to a global light-index buffer. Each `ClusterRecord` stores where that cluster's list begins and how many lights it contains.

The debug view `Cluster Light Count` is the fastest way to confirm that this path is working.

![Cluster Light Count](<images/BistroExteriorClusteredForward Vulkan 2026_05_08 23_48_33.png>)

## Shadow Map

The directional light remains outside the local-light cluster list. It uses a conventional depth shadow map:

```text
light view-projection
  -> depth-only pass
  -> comparison sampling in the main pass
```

Local lights are intentionally unshadowed in this version. Adding local-light shadows would introduce shadow atlases, cubemap shadows, or clustered shadow assignment, which would distract from the clustered forward comparison.

![Shadow Factor](<images/BistroExteriorClusteredForward Vulkan 2026_05_08 23_48_54.png>)

## D3D12 Reading Points

Files:

- `../Samples/BistroExteriorClusteredForward/D3D12ManyLights/Source/BistroExteriorManyLightsD3D12.cpp`
- `../Samples/BistroExteriorClusteredForward/D3D12ClusteredForward/Source/BistroExteriorClusteredForwardD3D12.cpp`

Focus on:

- Root signature slots for material textures, light buffers, cluster buffers, and shadow map
- UAV barriers between compute writes and pixel-shader reads
- Shadow depth resource state transitions
- The difference between ManyLights and ClusteredForward command recording

The Direct3D 12 debug layer is especially useful here because root binding and resource-state mistakes are reported directly.

## Vulkan Reading Points

Files:

- `../Samples/BistroExteriorClusteredForward/VulkanManyLights/Source/BistroExteriorManyLightsVulkan.cpp`
- `../Samples/BistroExteriorClusteredForward/VulkanClusteredForward/Source/BistroExteriorClusteredForwardVulkan.cpp`

Focus on:

- Descriptor set layout matching the shared HLSL bindings
- Storage buffer use from compute and fragment stages
- Pipeline barriers from compute writes to fragment reads
- The depth-only shadow render pass and comparison sampler
- Swapchain resize and minimize stability inherited from the Bistro Vulkan samples

## Why This Comes Before Mesh Shader

Clustered Forward reduces shading work by grouping lights. Mesh Shader reduces geometry submission work by grouping triangles into meshlets. Both samples use the same broad idea:

```text
large work
  -> split into small units
  -> keep only what is relevant
```

After reading Clustered Forward, Mesh Shader culling is easier to approach because the mental model is already familiar.
