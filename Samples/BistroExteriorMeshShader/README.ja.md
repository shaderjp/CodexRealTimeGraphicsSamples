# BistroExteriorMeshShader

英語版は [README.md](README.md) を参照してください。

`BistroExteriorMeshShader` は Bistro Exterior サンプルを Mesh Shader 化していくための新しいサンプルです。現在は直接光の mesh shader 版と、AS/TS meshlet culling 版を含みます。

- `BistroExteriorMeshShaderD3D12`
- `BistroExteriorMeshShaderVulkan`
- `BistroExteriorMeshShaderCullingD3D12`
- `BistroExteriorMeshShaderCullingVulkan`
- `BistroExteriorMeshShaderShadowD3D12`
- `BistroExteriorMeshShaderShadowVulkan`
- `BistroExteriorMeshShaderShadowCullingD3D12`
- `BistroExteriorMeshShaderShadowCullingVulkan`

## 現在の内容

- `ThirdParty/assimp` による `BistroExterior.fbx` の実行時読み込み
- `ThirdParty/meshoptimizer` による実行時 meshlet 生成
- meshlet 初期値: 64 vertices、96 triangles、cone weight 0.25
- meshlet-local triangle を 1 つの `uint` に pack
- D3D12: Mesh Shader PSO と `ID3D12GraphicsCommandList6::DispatchMesh`
- Vulkan: Vulkan 1.2 + `VK_EXT_mesh_shader` と `vkCmdDrawMeshTasksEXT`
- 直接光と shadow map の AS/TS meshlet culling variant
- AS/TS なしの mesh-shader shadow map variant
- main pass は camera frustum + cone backface culling
- shadow pass は light frustum culling
- D3D12 / Vulkan 共通の HLSL 6.6 mesh shader + pixel shader
- Mesh Shader プロジェクトには vertex-shader fallback なし
- ImGui `Debug View` に `Meshlet Color`
- `Renderer Stats` に meshlet 数、meshlet dispatch range 数、culling variant では culling 数

## スクリーンショット

下の画像は代表 variant のスクリーンショットです。直接光のみで culling なしのサンプルは MS-only プロジェクトの代表、shadow+culling サンプルは AS/TS meshlet culling プロジェクトの代表です。その他の variant は backend、shadow、debug mode 以外の見た目が同じです。

### Mesh Shader、Culling なし

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

## プロジェクト

- `BistroExteriorMeshShader.sln`
- `D3D12/Source/BistroExteriorMeshShaderD3D12.vcxproj`
- `Vulkan/Source/BistroExteriorMeshShaderVulkan.vcxproj`
- `D3D12Culling/Source/BistroExteriorMeshShaderCullingD3D12.vcxproj`
- `VulkanCulling/Source/BistroExteriorMeshShaderCullingVulkan.vcxproj`
- `D3D12Shadow/Source/BistroExteriorMeshShaderShadowD3D12.vcxproj`
- `VulkanShadow/Source/BistroExteriorMeshShaderShadowVulkan.vcxproj`
- `D3D12ShadowCulling/Source/BistroExteriorMeshShaderShadowCullingD3D12.vcxproj`
- `VulkanShadowCulling/Source/BistroExteriorMeshShaderShadowCullingVulkan.vcxproj`

## アセット

Bistro データセットはこのリポジトリには含めません。NVIDIA ORCA から Amazon Lumberyard Bistro を取得し、展開した `Bistro_v5_2` フォルダをリポジトリ直下に置いてください。

```text
CodexdRealTimeGraphicsSamples/
  Bistro_v5_2/
    BistroExterior.fbx
    Textures/
```

または、`BistroExterior.fbx` を直接含むフォルダを `BISTRO_ASSET_ROOT` 環境変数で指定できます。

## ビルド

先に submodule を初期化してください。

```powershell
git submodule update --init --recursive
```

Mesh Shader プロジェクトは Visual Studio 2022 または MSBuild でビルドできます。

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

Vulkan 版のビルドには Vulkan SDK と `VULKAN_SDK` が必要です。D3D12 版は Mesh Shader Tier 非対応デバイスでは起動時に明示的なエラーを出します。Vulkan 版は Vulkan 1.2+、`VK_EXT_mesh_shader`、`meshShader` feature が必要です。culling variant では `taskShader` も必要です。

## Third Party

- `ThirdParty/assimp`: FBX scene import
- `ThirdParty/DirectXTex`: DDS/TGA texture loading
- `ThirdParty/imgui`: debug controls and renderer stats
- `ThirdParty/meshoptimizer`: runtime meshlet generation
