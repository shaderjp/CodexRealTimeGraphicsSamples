# BistroExteriorMeshShader

Japanese documentation is available in [README.ja.md](README.ja.md).

`BistroExteriorMeshShader` is the staged Mesh Shader version of the Bistro exterior sample. It currently includes the direct-light mesh-shader paths and the AS/TS meshlet-culling paths:

- `BistroExteriorMeshShaderD3D12`
- `BistroExteriorMeshShaderVulkan`
- `BistroExteriorMeshShaderCullingD3D12`
- `BistroExteriorMeshShaderCullingVulkan`
- `BistroExteriorMeshShaderShadowD3D12`
- `BistroExteriorMeshShaderShadowVulkan`
- `BistroExteriorMeshShaderShadowCullingD3D12`
- `BistroExteriorMeshShaderShadowCullingVulkan`

## Current Features

- Runtime `BistroExterior.fbx` import through `ThirdParty/assimp`
- Runtime meshlet generation through `ThirdParty/meshoptimizer`
- Meshlet defaults: 64 vertices, 96 triangles, cone weight 0.25
- Packed meshlet-local triangles as one `uint`
- Direct3D12 Mesh Shader pipeline with `ID3D12GraphicsCommandList6::DispatchMesh`
- Vulkan `VK_EXT_mesh_shader` pipeline with `vkCmdDrawMeshTasksEXT`
- AS/TS meshlet culling variants for direct lighting and shadow maps
- Mesh-shader shadow map variants without AS/TS
- Camera frustum + cone backface culling for main passes
- Light frustum culling for shadow passes
- Shared HLSL 6.6 mesh shader and pixel shader
- No vertex-shader fallback path in the mesh-shader projects
- ImGui `Debug View` mode: `Meshlet Color`
- Renderer stats for meshlet count, meshlet dispatch ranges, and culling counts in culling variants

## Screenshots

The screenshots below show representative variants. The direct-light sample without culling represents the MS-only projects, and the shadow+culling sample represents the AS/TS meshlet-culling projects. The other variants share the same visual output apart from backend, shadows, and debug mode.

### Mesh Shader, No Culling

| D3D12 Final | D3D12 Meshlet Color |
| --- | --- |
| ![BistroExteriorMeshShader D3D12 final](../../docs/images/BistroExteriorMeshShader%20D3D12%202026_05_06%2010_59_04.png) | ![BistroExteriorMeshShader D3D12 meshlet color](../../docs/images/BistroExteriorMeshShader%20D3D12%202026_05_06%2010_59_22.png) |

| Vulkan Final | Vulkan Meshlet Color |
| --- | --- |
| ![BistroExteriorMeshShader Vulkan final](../../docs/images/BistroExteriorMeshShader%20Vulkan%202026_05_06%2011_07_37.png) | ![BistroExteriorMeshShader Vulkan meshlet color](../../docs/images/BistroExteriorMeshShader%20Vulkan%202026_05_06%2011_07_50.png) |

### Shadow + Meshlet Culling

| D3D12 Final | D3D12 Meshlet Color |
| --- | --- |
| ![BistroExteriorMeshShader Shadow Culling D3D12 final](../../docs/images/BistroExteriorMeshShader%20Shadow%20Culling%20D3D12%202026_05_06%2011_08_55.png) | ![BistroExteriorMeshShader Shadow Culling D3D12 meshlet color](../../docs/images/BistroExteriorMeshShader%20Shadow%20Culling%20D3D12%202026_05_06%2011_09_14.png) |

| Vulkan Final | Vulkan Meshlet Color |
| --- | --- |
| ![BistroExteriorMeshShader Shadow Culling Vulkan final](../../docs/images/BistroExteriorMeshShader%20Shadow%20Culling%20Vulkan%202026_05_06%2011_10_19.png) | ![BistroExteriorMeshShader Shadow Culling Vulkan meshlet color](../../docs/images/BistroExteriorMeshShader%20Shadow%20Culling%20Vulkan%202026_05_06%2011_10_34.png) |

## Projects

- `BistroExteriorMeshShader.sln`
- `D3D12/Source/BistroExteriorMeshShaderD3D12.vcxproj`
- `Vulkan/Source/BistroExteriorMeshShaderVulkan.vcxproj`
- `D3D12Culling/Source/BistroExteriorMeshShaderCullingD3D12.vcxproj`
- `VulkanCulling/Source/BistroExteriorMeshShaderCullingVulkan.vcxproj`
- `D3D12Shadow/Source/BistroExteriorMeshShaderShadowD3D12.vcxproj`
- `VulkanShadow/Source/BistroExteriorMeshShaderShadowVulkan.vcxproj`
- `D3D12ShadowCulling/Source/BistroExteriorMeshShaderShadowCullingD3D12.vcxproj`
- `VulkanShadowCulling/Source/BistroExteriorMeshShaderShadowCullingVulkan.vcxproj`

## Assets

The Bistro dataset is not stored in this repository. Download Amazon Lumberyard Bistro from NVIDIA ORCA and place the extracted `Bistro_v5_2` folder at the repository root:

```text
CodexdRealTimeGraphicsSamples/
  Bistro_v5_2/
    BistroExterior.fbx
    Textures/
```

Alternatively, set `BISTRO_ASSET_ROOT` to the folder that directly contains `BistroExterior.fbx`.

## Build

Initialize submodules first:

```powershell
git submodule update --init --recursive
```

Build the Mesh Shader projects with Visual Studio 2022 or MSBuild:

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" Samples\BistroExteriorMeshShader\D3D12\Source\BistroExteriorMeshShaderD3D12.vcxproj /p:Platform=x64 /p:Configuration=Debug
& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" Samples\BistroExteriorMeshShader\Vulkan\Source\BistroExteriorMeshShaderVulkan.vcxproj /p:Platform=x64 /p:Configuration=Debug
& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" Samples\BistroExteriorMeshShader\D3D12Culling\Source\BistroExteriorMeshShaderCullingD3D12.vcxproj /p:Platform=x64 /p:Configuration=Debug
& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" Samples\BistroExteriorMeshShader\VulkanCulling\Source\BistroExteriorMeshShaderCullingVulkan.vcxproj /p:Platform=x64 /p:Configuration=Debug
& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" Samples\BistroExteriorMeshShader\D3D12Shadow\Source\BistroExteriorMeshShaderShadowD3D12.vcxproj /p:Platform=x64 /p:Configuration=Debug
& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" Samples\BistroExteriorMeshShader\VulkanShadow\Source\BistroExteriorMeshShaderShadowVulkan.vcxproj /p:Platform=x64 /p:Configuration=Debug
& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" Samples\BistroExteriorMeshShader\D3D12ShadowCulling\Source\BistroExteriorMeshShaderShadowCullingD3D12.vcxproj /p:Platform=x64 /p:Configuration=Debug
& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" Samples\BistroExteriorMeshShader\VulkanShadowCulling\Source\BistroExteriorMeshShaderShadowCullingVulkan.vcxproj /p:Platform=x64 /p:Configuration=Debug
```

Vulkan builds require the Vulkan SDK and `VULKAN_SDK`. The D3D12 projects fail at startup with a clear error if Mesh Shader Tier support is missing. The Vulkan projects require Vulkan 1.2+, `VK_EXT_mesh_shader`, and `meshShader`; culling variants also require `taskShader`.

## Third Party

- `ThirdParty/assimp`: FBX scene import
- `ThirdParty/DirectXTex`: DDS/TGA texture loading
- `ThirdParty/imgui`: debug controls and renderer stats
- `ThirdParty/meshoptimizer`: runtime meshlet generation
