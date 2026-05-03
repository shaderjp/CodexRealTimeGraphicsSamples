# Third Party Dependencies

Third-party code is stored under `ThirdParty` and managed with Git submodules.

## Dependencies

| Dependency | Path | Purpose | Source |
| --- | --- | --- | --- |
| Dear ImGui | `ThirdParty/imgui` | Future sample UI and debug controls | <https://github.com/ocornut/imgui> |
| DirectXTex | `ThirdParty/DirectXTex` | Future texture loading and processing utilities | <https://github.com/microsoft/DirectXTex> |
| DirectX 12 Agility SDK | NuGet package | Direct3D12 runtime components | `Microsoft.Direct3D.D3D12` |
| Vulkan SDK | Local SDK install | Vulkan headers, libraries, tools, and `dxc` for SPIR-V | <https://vulkan.lunarg.com/sdk/home> |

## Update Policy

- Update submodules intentionally and record notable version changes in the commit message.
- Check each dependency license before introducing code into samples.
- Keep generated build products out of `ThirdParty`.

## Initial Agility SDK Version

Direct3D12 sample projects use `Microsoft.Direct3D.D3D12` version `1.619.1` initially.
