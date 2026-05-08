# Bistro Mesh Shader Implementation Review

This document reviews `Samples/BistroExteriorMeshShader`, which renders Bistro Exterior with mesh shaders and optional meshlet culling.

Read the series in this order:

1. [Pre-Bistro implementation review](pre_bistro_implementation_review.md)
2. [Bistro implementation review](bistro_implementation_review.md)
3. [Bistro Clustered Forward implementation review](bistro_clustered_forward_implementation_review.md)
4. This document
5. [Bistro Raytracing implementation review](bistro_raytracing_implementation_review.md)
6. [Bistro Pathtracing implementation review](bistro_pathtracing_implementation_review.md)

## Scope

`Samples/BistroExteriorMeshShader` contains eight projects:

| Stage | D3D12 | Vulkan | Role |
| --- | --- | --- | --- |
| 1 | `BistroExteriorMeshShaderD3D12` | `BistroExteriorMeshShaderVulkan` | Direct lighting, mesh shader only |
| 2 | `BistroExteriorMeshShaderCullingD3D12` | `BistroExteriorMeshShaderCullingVulkan` | Direct lighting with AS/TS meshlet culling |
| 3 | `BistroExteriorMeshShaderShadowD3D12` | `BistroExteriorMeshShaderShadowVulkan` | Shadow-map variant without AS/TS culling |
| 4 | `BistroExteriorMeshShaderShadowCullingD3D12` | `BistroExteriorMeshShaderShadowCullingVulkan` | Shadow-map variant with meshlet culling |

## What Changes From Base Bistro

The material shading remains close to the raster Bistro sample. The major change is geometry submission:

| Base raster | Mesh Shader sample |
| --- | --- |
| Vertex shader + index buffer | Mesh shader emits vertices and primitives |
| `DrawIndexedInstanced` | `DispatchMesh` / `vkCmdDrawMeshTasksEXT` |
| Draw item | Meshlet dispatch range |
| Whole draw submitted | Meshlet can be culled before mesh shader work |

## Meshlets

Meshlets are generated at load time with meshoptimizer. Each draw item is split into small groups, using defaults such as 64 vertices and 96 triangles per meshlet.

Important data:

- `MeshletRecord`
- `MeshletBounds`
- `MeshletDispatchRange`
- Meshlet vertex and triangle buffers

The mesh shader reads one meshlet, fetches its local vertices and packed local triangles, and emits the final mesh output.

## Culling Variants

The culling projects add an amplification shader in D3D12 and a task shader in Vulkan. Their job is to inspect meshlet bounds and launch mesh shader work only for visible meshlets.

The first-pass culling is:

- Frustum culling
- Cone backface culling

It does not include occlusion culling.

## Debug Views

The key debug view is `Meshlet Color`, which colors output deterministically by meshlet id. It confirms that rendering is truly meshlet driven and makes meshlet boundaries visible.

Renderer stats also show meshlet counts, dispatch counts, visible meshlets, and culled meshlets for culling variants.

## Reading Order

1. Read the shared Bistro scene loader changes for meshlet generation.
2. Read the non-culling D3D12 or Vulkan project.
3. Follow the mesh shader from meshlet id to emitted triangles.
4. Add the AS/TS culling path.
5. Add shadow-map variants only after the meshlet path is clear.
