# Bistro Implementation Review

This document introduces the Bistro Exterior sample family. It follows the pre-Bistro samples and prepares the reader for the specialized Clustered Forward, Mesh Shader, Raytracing, and Pathtracing reviews.

Read the series in this order:

1. [Pre-Bistro implementation review](pre_bistro_implementation_review.md)
2. This document
3. [Bistro Clustered Forward implementation review](bistro_clustered_forward_implementation_review.md)
4. [Bistro Mesh Shader implementation review](bistro_mesh_shader_implementation_review.md)
5. [Bistro Raytracing implementation review](bistro_raytracing_implementation_review.md)
6. [Bistro Pathtracing implementation review](bistro_pathtracing_implementation_review.md)

## Scope

This overview covers:

| Sample | Role |
| --- | --- |
| `Samples/BistroExterior` | Base rasterized Bistro Exterior renderer |
| `Samples/BistroExterior` shadow variants | Directional shadow-map variants |
| `Samples/BistroExteriorClusteredForward` | Many-light and clustered forward comparison |
| `Samples/BistroExteriorMeshShader` | Mesh shader and meshlet culling variants |
| `Samples/BistroExteriorRaytracing` | DXR / Vulkan Ray Tracing direct, shadow, and GI variants |
| `Samples/BistroExteriorPathtracing` | Progressive path tracing, ReSTIR GI / DI, and denoising |

The first file to read is still the base `BistroExterior` renderer. The later samples reuse the same scene and material concepts while changing one major rendering technique at a time.

## What Bistro Adds

Compared with the earlier samples, Bistro introduces:

- A large FBX scene
- Many meshes, materials, and textures
- DDS / TGA loading through DirectXTex
- Assimp scene import
- Alpha-masked materials
- An FPS camera
- Dear ImGui debug controls
- Renderer statistics
- D3D12 / Vulkan visual parity work
- Shadow maps
- Mesh shaders and meshlets
- Ray tracing acceleration structures
- Path tracing accumulation and reuse

The best way to read the code is to split it into four areas:

```text
scene data
  -> texture loading
    -> draw or dispatch submission
      -> debug UI and stats
```

## Shared Code

The shared Bistro code is the map for the whole family.

### `BistroScene`

Files:

- `../Samples/BistroExterior/Common/BistroScene.h`
- `../Samples/BistroExterior/Common/BistroScene.cpp`

This code finds the Bistro asset root, imports `BistroExterior.fbx` with Assimp, extracts vertices, indices, draw items, materials, and texture paths, and applies Bistro-specific fallback rules.

### `BistroTexture`

Files:

- `../Samples/BistroExterior/Common/BistroTexture.h`
- `../Samples/BistroExterior/Common/BistroTexture.cpp`

This code loads DDS, TGA, and WIC images, preserves BC compressed DDS paths, records mip information, and tags sRGB versus linear textures.

### `BistroCamera`

Files:

- `../Samples/BistroExterior/Common/BistroCamera.h`
- `../Samples/BistroExterior/Common/BistroCamera.cpp`

Bistro is large enough that a fixed camera is not useful. The camera provides WASD movement, vertical movement, fast movement, mouse look, and ImGui-safe input handling.

## Base Raster Renderer

The base raster renderer is the foundation. It loads Bistro, creates material texture descriptors, loops through draw items, and shades with a directional light plus ambient contribution.

Files:

- `../Samples/BistroExterior/D3D12/Source/BistroExteriorD3D12.cpp`
- `../Samples/BistroExterior/Vulkan/Source/BistroExteriorVulkan.cpp`
- `../Samples/BistroExterior/Shaders/BistroExterior.hlsl`

Key ideas:

- Each draw item has an index range and a material index.
- Material descriptors point to base color, normal, specular-packed, and emissive textures.
- Bistro's specular texture is treated as a packed map: `R=AO`, `G=roughness`, `B=metalness`.
- Debug views make D3D12/Vulkan differences easier to isolate.

## Shadow Map Variant

The shadow variants add a depth-only pass from the directional light and sample it in the main pass.

Files:

- `../Samples/BistroExterior/D3D12Shadow/Source/BistroExteriorShadowD3D12.cpp`
- `../Samples/BistroExterior/VulkanShadow/Source/BistroExteriorShadowVulkan.cpp`

The important reading path is:

```text
camera focus point
  -> light view-projection
  -> depth-only shadow pass
  -> main pass comparison sampling
```

This same directional shadow-map idea later reappears in Mesh Shader and Clustered Forward samples.

## Debug Views

Bistro relies heavily on debug views because visual mismatches can come from many places.

Useful views include:

- Base Color
- World Normal
- Normal Texture Raw / Decoded
- AO / Roughness / Metallic
- NdotL
- Emissive Texture
- Shadow Factor
- Shadow Map Depth

When D3D12 and Vulkan differ, these views help isolate whether the issue is texture format, descriptor binding, normal map orientation, sampler LOD, or material interpretation.

## How The Later Samples Branch

Each later Bistro sample changes one major axis:

- Clustered Forward changes local-light selection.
- Mesh Shader changes geometry submission.
- Raytracing changes visibility from rasterization to rays.
- Pathtracing changes shading from direct evaluation to progressive light transport.

Keeping this separation makes the code easier to read. You can always return to base Bistro and ask, "Which part did this variant replace?"
