# Bistro Exterior Clustered Forward

This sample adds a many-light forward renderer for Amazon Lumberyard Bistro Exterior and compares it with a GPU clustered forward path. Both paths also include a directional shadow map so the original sun light can be shadowed while the local-light cost stays easy to compare.

## Projects

- `BistroExteriorManyLightsD3D12`
- `BistroExteriorManyLightsVulkan`
- `BistroExteriorClusteredForwardD3D12`
- `BistroExteriorClusteredForwardVulkan`

The ManyLights projects loop over the active local lights directly in the pixel shader. The ClusteredForward projects build a per-frame 3D cluster light list with a compute shader, then shade each pixel from the lights assigned to its screen tile and logarithmic depth slice.

## Lighting

The sample reuses the Bistro Exterior scene loader and creates raster-friendly sphere light proxies from Bistro emissive materials and emissive textures. It also adds a few deterministic procedural area-light proxies around the scene, matching the light-list approach used by the path tracing sample but converting the result to local sphere lights for forward shading.

The original directional light remains separate from the clustered local-light list and uses a conventional depth shadow map. Local lights remain unshadowed in this version, keeping the ManyLights and ClusteredForward comparison focused on light-list construction and shading cost.

## Screenshots

ManyLights renders the same generated light list directly in the pixel shader.

| Direct3D 12 | Vulkan |
|---|---|
| ![BistroExteriorManyLights D3D12](<../../docs/images/BistroExteriorManyLights D3D12 2026_05_08 23_45_53.png>) | ![BistroExteriorManyLights Vulkan](<../../docs/images/BistroExteriorManyLights Vulkan 2026_05_08 23_46_32.png>) |

ClusteredForward builds the visible-light list per screen tile and depth slice before the forward shading pass.

| Direct3D 12 | Vulkan |
|---|---|
| ![BistroExteriorClusteredForward D3D12](<../../docs/images/BistroExteriorClusteredForward D3D12 2026_05_08 23_47_09.png>) | ![BistroExteriorClusteredForward Vulkan](<../../docs/images/BistroExteriorClusteredForward Vulkan 2026_05_08 23_47_41.png>) |

Debug views expose the clustered light distribution and the directional shadow-map result.

| Cluster Light Count | Shadow Factor | Shadow Map Depth |
|---|---|---|
| ![Cluster light count debug view](<../../docs/images/BistroExteriorClusteredForward Vulkan 2026_05_08 23_48_33.png>) | ![Shadow factor debug view](<../../docs/images/BistroExteriorClusteredForward Vulkan 2026_05_08 23_48_54.png>) | ![Shadow map depth debug view](<../../docs/images/BistroExteriorClusteredForward Vulkan 2026_05_08 23_49_11.png>) |

## Controls

The ImGui controls include:

- Directional light direction, color, and intensity
- FPS camera controls
- Local light enable, emissive proxy enable, procedural proxy enable
- Active light count, local light intensity scale, and radius scale
- Directional shadow enable, resolution, depth/normal bias, PCF radius, and light-frustum controls
- Clustered projects only: Z slice count and cluster-grid stats
- Debug views for final color, directional-only lighting, local-light contribution, cluster light count, cluster slice, cluster overflow, shadow-map depth, shadow factor, and light-space depth

## Build

Open `BistroExteriorClusteredForward.sln` in Visual Studio 2022 and build Debug or Release for `x64`.

The sample loads `BistroExterior.fbx` from the existing Bistro asset search path. Set `BISTRO_ASSET_ROOT` or place `Bistro_v5_2` at the repository root, as with `Samples/BistroExterior`.
