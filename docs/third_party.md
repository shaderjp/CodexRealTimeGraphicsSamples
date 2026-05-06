# Third Party Dependencies

Third-party code is stored under `ThirdParty` and managed with Git submodules.

## Dependencies

| Dependency | Path | Purpose | Source |
| --- | --- | --- | --- |
| Dear ImGui | `ThirdParty/imgui` | Sample UI and debug controls, currently used by `Samples/ImGuiLighting` and `Samples/BistroExterior` | <https://github.com/ocornut/imgui> |
| DirectXTex | `ThirdParty/DirectXTex` | DDS/TGA texture loading and texture processing, currently used by `Samples/BistroExterior` | <https://github.com/microsoft/DirectXTex> |
| Assimp | `ThirdParty/assimp` | Runtime scene import for large source assets, currently used by `Samples/BistroExterior` to load `BistroExterior.fbx` | <https://github.com/assimp/assimp> |
| meshoptimizer | `ThirdParty/meshoptimizer` | Runtime meshlet generation for Mesh Shader samples | <https://github.com/zeux/meshoptimizer> |
| DirectX 12 Agility SDK | NuGet package | Direct3D12 runtime components | `Microsoft.Direct3D.D3D12` |
| Vulkan SDK | Local SDK install | Vulkan headers, libraries, tools, and `dxc` for SPIR-V | <https://vulkan.lunarg.com/sdk/home> |

## Assimp Build Notes

`Samples/BistroExterior` builds Assimp from the `ThirdParty/assimp` submodule with CMake during MSBuild. The sample configures Assimp as a static library with tests, tools, shared library output, and install targets disabled:

- `ASSIMP_BUILD_TESTS=OFF`
- `ASSIMP_BUILD_ASSIMP_TOOLS=OFF`
- `BUILD_SHARED_LIBS=OFF`
- `ASSIMP_INSTALL=OFF`
- `ASSIMP_WARNINGS_AS_ERRORS=OFF`

Generated Assimp build products stay under the sample build output and must not be committed.

## Update Policy

- Update submodules intentionally and record notable version changes in the commit message.
- Check each dependency license before introducing code into samples.
- Keep generated build products out of `ThirdParty`.

## Initial Agility SDK Version

Direct3D12 sample projects use `Microsoft.Direct3D.D3D12` version `1.619.1` initially.
