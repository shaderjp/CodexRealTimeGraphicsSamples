# Shader Policy

## Language And Target

- HLSL is the shader language for both Direct3D12 and Vulkan samples.
- The initial shader target is Shader Model 6.6.
- Use `vs_6_6` and `ps_6_6` for the initial graphics pipeline samples.

## Direct3D12

Direct3D12 projects compile HLSL to DXIL-compatible object files at build time with `dxc`.

Expected outputs:

- Vertex shaders: `.vs.cso`
- Pixel shaders: `.ps.cso`

## Vulkan

Vulkan projects compile HLSL to SPIR-V at build time with the Vulkan SDK `dxc`.

Expected outputs:

- Vertex shaders: `.vs.spv`
- Pixel shaders: `.ps.spv`

The Vulkan project expects the `VULKAN_SDK` environment variable to point at an installed Vulkan SDK. If it is missing, the build fails early with a clear MSBuild error.

## Future Shader Model Updates

Before raising the default above Shader Model 6.6, document the required Agility SDK version, Vulkan SDK version, GPU support, and driver requirements.
