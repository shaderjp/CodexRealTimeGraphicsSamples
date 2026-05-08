# Bistro Raytracing Implementation Review

This document reviews `Samples/BistroExteriorRaytracing`, which renders Bistro Exterior with DXR and Vulkan Ray Tracing rather than a scene raster pass.

Read the series in this order:

1. [Pre-Bistro implementation review](pre_bistro_implementation_review.md)
2. [Bistro implementation review](bistro_implementation_review.md)
3. [Bistro Clustered Forward implementation review](bistro_clustered_forward_implementation_review.md)
4. [Bistro Mesh Shader implementation review](bistro_mesh_shader_implementation_review.md)
5. This document
6. [Bistro Pathtracing implementation review](bistro_pathtracing_implementation_review.md)

## Scope

`Samples/BistroExteriorRaytracing` contains six projects:

| Stage | D3D12 | Vulkan | Role |
| --- | --- | --- | --- |
| 1 | `BistroExteriorRaytracingD3D12` | `BistroExteriorRaytracingVulkan` | Primary rays and direct lighting |
| 2 | `BistroExteriorRaytracingShadowD3D12` | `BistroExteriorRaytracingShadowVulkan` | Adds ray-traced hard shadows |
| 3 | `BistroExteriorRaytracingGID3D12` | `BistroExteriorRaytracingGIVulkan` | Adds 1-bounce diffuse GI and accumulation |

## What Changes From Raster

Rasterization projects submit triangles to the screen. Raytracing projects trace rays from pixels into acceleration structures.

```text
pixel
  -> primary ray
    -> TLAS
      -> BLAS
        -> triangle hit
          -> material shading
```

There is no scene raster pass, no shadow map, and no mesh shader pass. The output is written to a ray tracing image and then copied or toned to the swapchain before ImGui is drawn.

## Acceleration Structures

The scene uses one static BLAS with one triangle geometry per Bistro draw item and one identity TLAS instance.

This keeps shader mapping simple:

```text
GeometryIndex()
  -> RtGeometryRecord
  -> material index
  -> bindless textures
```

Alpha-tested materials use any-hit shaders so cutout materials can ignore transparent hits.

## Shader Stages

Core ray tracing shaders:

- Ray generation shader
- Miss shader
- Closest-hit shader
- Any-hit shader

The shadow stage adds a second ray type. The GI stage adds a secondary diffuse ray and temporal accumulation.

## Procedural Sky And GI Noise Controls

The miss shader evaluates a procedural gradient sky with a sun disk. In the GI variant, secondary rays that miss the scene return the same sky radiance, making the sky act as a simple skylight.

The GI variant also includes low-discrepancy sampling, samples per frame, radiance clamp, and temporal clamp. These reduce noise without adding a full spatial denoiser.

## Reading Order

1. Start with the direct-light project.
2. Follow raygen, miss, closest-hit, and any-hit.
3. Read BLAS / TLAS creation.
4. Add the shadow ray path.
5. Add GI accumulation and reset logic.

This prepares the reader for the Pathtracing sample, where raygen owns a full path loop and multiple light sampling strategies.
