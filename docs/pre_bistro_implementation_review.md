# Pre-Bistro Implementation Review

This note is the English entry point for the implementation-review series. It covers the samples that come before `Samples/BistroExterior` and explains what each one teaches before the repository moves to a large real-world scene.

Read the series in this order:

1. This document
2. [Bistro implementation review](bistro_implementation_review.md)
3. [Bistro Clustered Forward implementation review](bistro_clustered_forward_implementation_review.md)
4. [Bistro Mesh Shader implementation review](bistro_mesh_shader_implementation_review.md)
5. [Bistro Raytracing implementation review](bistro_raytracing_implementation_review.md)
6. [Bistro Pathtracing implementation review](bistro_pathtracing_implementation_review.md)

## Scope

| Order | Sample | Role |
| --- | --- | --- |
| 1 | `Samples/Triangle` | Window, device, swapchain, shader compilation, triangle draw |
| 2 | `Samples/Cube3D` | Vertex/index buffers, constant buffers, depth, 3D transforms |
| 3 | `Samples/SciFiHelmet` | glTF loading, textures, normal mapping, small PBR shader |
| 4 | `Samples/Skinning` | glTF animation, joints, vertex-shader and compute skinning |
| 5 | `Samples/ImGuiLighting` | Dear ImGui controls for runtime light editing |

These samples intentionally keep the scene small. Their job is to make the Direct3D 12 and Vulkan building blocks visible before the Bistro samples add Assimp, DirectXTex, a large FBX scene, many materials, debug UI, shadows, mesh shaders, and ray tracing.

## Repository Pattern

Each technique lives under `Samples/<TechniqueName>` and keeps Direct3D 12 and Vulkan projects side by side.

```text
Samples/
  Triangle/
    Triangle.sln
    D3D12/
      Source/
    Vulkan/
      Source/
    Shaders/
    Assets/
```

The important pattern is:

- Direct3D 12 and Vulkan implement the same visual target.
- Shared HLSL is compiled to DXIL for D3D12 and SPIR-V for Vulkan.
- The code avoids hiding the graphics APIs behind a large abstraction layer, so readers can compare the API concepts directly.

## How To Read A Sample

A useful reading order is:

1. `README.md` or `README.ja.md`
2. `Directory.Build.props` and `Directory.Build.targets`
3. The `.vcxproj` files
4. `Main.cpp`
5. The Win32 loop or API-specific main loop
6. The renderer class
7. `Shaders/*.hlsl`

For each renderer, split the code into three questions:

- What CPU-side resources are created?
- What commands are recorded?
- What does the shader output?

## Triangle

`Samples/Triangle` is the smallest rendering sample. The triangle is less important than the minimum frame pipeline.

Look for:

- Device and swapchain creation
- Render-target descriptors / image views
- Command allocators and command buffers
- Shader compilation
- Synchronization with fences or semaphores

Files to read:

- `../Samples/Triangle/D3D12/Source/TriangleD3D12.cpp`
- `../Samples/Triangle/Vulkan/Source/TriangleVulkan.cpp`
- `../Samples/Triangle/Shaders/Triangle.hlsl`

## Cube3D

`Samples/Cube3D` adds 3D concepts: indexed geometry, a constant buffer, matrices, and a depth buffer.

Compare how D3D12 and Vulkan handle:

- Constant/uniform buffer alignment
- Depth image creation
- Clip-space differences
- Per-frame transform updates

Files to read:

- `../Samples/Cube3D/D3D12/Source/Cube3DD3D12.cpp`
- `../Samples/Cube3D/Vulkan/Source/Cube3DVulkan.cpp`
- `../Samples/Cube3D/Shaders/Cube3D.hlsl`

## SciFiHelmet

`Samples/SciFiHelmet` moves from hard-coded geometry to a real glTF asset. It introduces material textures and a compact PBR-style shader.

Focus on how the loader creates:

- Positions, normals, tangents, and UVs
- Indices
- Material records
- Base color, normal, metallic-roughness, and AO textures

Then read the shader and follow base color, roughness, metallic, normal map, ambient occlusion, and directional light through the final color.

## Skinning

`Samples/Skinning` introduces animated mesh deformation. The useful comparison is vertex-shader skinning versus compute-shader skinning.

Vertex-shader skinning keeps the original mesh and applies joint matrices during draw. Compute skinning writes a skinned vertex buffer first, then the graphics pass reads that buffer.

Read:

- Animation sampling on the CPU
- Joint matrix creation
- Joint index / weight usage in the shader
- Compute pass barriers before graphics reads

## ImGuiLighting

`Samples/ImGuiLighting` adds the runtime control pattern used heavily by later Bistro samples.

The key lesson is not the light itself, but the loop:

```text
Begin ImGui frame
  -> edit CPU-side parameters
  -> update constant buffer
  -> render scene
  -> render ImGui overlay
```

This pattern later becomes the controls for cameras, debug views, shadow maps, meshlet coloring, ray tracing settings, path tracing accumulation, and denoisers.

## Why This Matters For Bistro

Bistro is much larger, but it does not start from nowhere. It is the same sequence expanded:

```text
Triangle
  -> Cube3D
    -> SciFiHelmet
      -> ImGuiLighting
        -> BistroExterior
```

If Bistro feels overwhelming, return to these earlier samples and identify the same concepts in smaller form.
