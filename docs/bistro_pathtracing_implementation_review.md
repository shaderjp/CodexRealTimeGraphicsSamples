# Bistro Pathtracing Implementation Review

This document reviews `Samples/BistroExteriorPathtracing`, which extends the Bistro ray tracing work into progressive path tracing, ReSTIR GI / DI comparisons, and a lightweight denoiser.

Read the series in this order:

1. [Pre-Bistro implementation review](pre_bistro_implementation_review.md)
2. [Bistro implementation review](bistro_implementation_review.md)
3. [Bistro Clustered Forward implementation review](bistro_clustered_forward_implementation_review.md)
4. [Bistro Mesh Shader implementation review](bistro_mesh_shader_implementation_review.md)
5. [Bistro Raytracing implementation review](bistro_raytracing_implementation_review.md)
6. This document

## Scope

`Samples/BistroExteriorPathtracing` contains six projects:

| Stage | D3D12 | Vulkan | Role |
| --- | --- | --- | --- |
| 1 | `BistroExteriorPathtracingD3D12` | `BistroExteriorPathtracingVulkan` | Baseline progressive path tracing |
| 2 | `BistroExteriorPathtracingReSTIRD3D12` | `BistroExteriorPathtracingReSTIRVulkan` | ReSTIR GI comparison |
| 3 | `BistroExteriorPathtracingReSTIRDID3D12` | `BistroExteriorPathtracingReSTIRDIVulkan` | ReSTIR DI comparison |

## Difference From Raytracing GI

The Raytracing GI sample adds one secondary diffuse ray. The Pathtracing sample adds a path loop.

```text
Raytracing GI:
  primary ray
    -> one diffuse secondary ray

Pathtracing:
  primary ray
    -> bounce 0
      -> next ray
        -> bounce 1
          -> ...
```

This means raygen owns throughput, bounce count, sampling decisions, accumulation, and debug AOVs.

## Lighting

The path tracer samples:

- Sun next-event estimation
- Procedural sky next-event estimation
- Emissive triangle lights
- Procedural area lights
- Optional environment-map path

Materials use diffuse and stochastic GGX specular bounces, so metallic and roughness changes affect reflections.

## ReSTIR Projects

The ReSTIR GI and ReSTIR DI projects add reservoir buffers and reuse passes.

Conceptually:

```text
candidate generation
  -> temporal reuse
    -> spatial reuse
      -> final shading / accumulation
```

The ReSTIR projects are separate so the baseline path tracer remains readable.

## Denoiser

The built-in denoiser is intentionally lightweight and vendor independent. It uses generated AOVs such as normal, depth, and albedo to guide filtering. It is not a replacement for a production denoiser, but it provides a practical next step after temporal accumulation.

## Reading Order

1. Read the baseline path tracer first.
2. Follow raygen's path loop and throughput updates.
3. Read direct-light next-event estimation.
4. Add emissive/procedural light list sampling.
5. Read ReSTIR GI, then ReSTIR DI.
6. Read the denoiser after the path and reservoir flow are clear.
