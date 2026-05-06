# BistroExteriorRaytracing

英語版は [README.md](README.md) を参照してください。

`BistroExteriorRaytracing` は Bistro Exterior シーンを DirectX Raytracing と Vulkan Ray Tracing で描画するサンプルです。シーンの可視性はレイトレーシングで解決し、シーン用の raster pass、shadow map、mesh shader pass は使いません。レイトレーシング出力を UAV に書き込み、swapchain へコピーしたあと ImGui overlay を描画します。

## プロジェクト

- `BistroExteriorRaytracingD3D12`
- `BistroExteriorRaytracingVulkan`
- `BistroExteriorRaytracingShadowD3D12`
- `BistroExteriorRaytracingShadowVulkan`
- `BistroExteriorRaytracingGID3D12`
- `BistroExteriorRaytracingGIVulkan`

## 現在の内容

- `ThirdParty/assimp` による `BistroExterior.fbx` の実行時読み込み
- `ThirdParty/DirectXTex` による DDS/TGA テクスチャ読み込み
- Bistro の draw item ごとに 1 つの triangle geometry を持つ static BLAS と、identity TLAS instance
- `GeometryIndex()` から共有 material / geometry record を参照
- primary ray / shadow ray の any-hit alpha test による alpha masked material 対応
- D3D12 descriptor table と Vulkan descriptor indexing による material texture の bindless 参照
- 直接光、hard ray-traced shadow、1spp diffuse 1-bounce GI と temporal accumulation
- ImGui から light、camera、sky、ray bias、shadow、GI strength、accumulation、debug view を調整
- Debug View: Final、Base Color、World Normal、Normal Texture、Hit Distance、Direct、Shadow、Indirect、Accumulation Samples
- Renderer Stats: material、texture、vertex、index、primitive、BLAS geometry、TLAS instance、SBT record、output resolution、ray tracing limit

## スクリーンショット

下の画像は 3 段階の renderer variant を示しています。GI サンプルは ray tracing output と accumulation input を確認するための主要な debug view を載せています。

### 直接光

| D3D12 | Vulkan |
| --- | --- |
| ![BistroExteriorRaytracing D3D12 direct lighting](../../docs/images/BistroExteriorRaytracing%20D3D12%202026_05_06%2013_26_50.png) | ![BistroExteriorRaytracing Vulkan direct lighting](../../docs/images/BistroExteriorRaytracing%20Vulkan%202026_05_06%2013_27_58.png) |

### 直接光 + Ray-Traced Shadow

| D3D12 | Vulkan |
| --- | --- |
| ![BistroExteriorRaytracing Shadow D3D12](../../docs/images/BistroExteriorRaytracing%20Shadow%20D3D12%202026_05_06%2013_29_01.png) | ![BistroExteriorRaytracing Shadow Vulkan](../../docs/images/BistroExteriorRaytracing%20Shadow%20Vulkan%202026_05_06%2013_29_43.png) |

### GI Debug Views

| D3D12 Final | D3D12 Hit Distance |
| --- | --- |
| ![BistroExteriorRaytracing GI D3D12 final](../../docs/images/BistroExteriorRaytracing%20GI%20D3D12%202026_05_06%2013_30_29.png) | ![BistroExteriorRaytracing GI D3D12 hit distance](../../docs/images/BistroExteriorRaytracing%20GI%20D3D12%202026_05_06%2013_30_39.png) |

| D3D12 Shadow | D3D12 Indirect |
| --- | --- |
| ![BistroExteriorRaytracing GI D3D12 shadow debug](../../docs/images/BistroExteriorRaytracing%20GI%20D3D12%202026_05_06%2013_30_50.png) | ![BistroExteriorRaytracing GI D3D12 indirect debug](../../docs/images/BistroExteriorRaytracing%20GI%20D3D12%202026_05_06%2013_30_55.png) |

| Vulkan Final | Vulkan Hit Distance |
| --- | --- |
| ![BistroExteriorRaytracing GI Vulkan final](../../docs/images/BistroExteriorRaytracing%20GI%20Vulkan%202026_05_06%2013_31_36.png) | ![BistroExteriorRaytracing GI Vulkan hit distance](../../docs/images/BistroExteriorRaytracing%20GI%20Vulkan%202026_05_06%2013_31_43.png) |

| Vulkan Shadow | Vulkan Indirect |
| --- | --- |
| ![BistroExteriorRaytracing GI Vulkan shadow debug](../../docs/images/BistroExteriorRaytracing%20GI%20Vulkan%202026_05_06%2013_31_54.png) | ![BistroExteriorRaytracing GI Vulkan indirect debug](../../docs/images/BistroExteriorRaytracing%20GI%20Vulkan%202026_05_06%2013_32_01.png) |

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

Visual Studio 2022 または MSBuild でソリューション全体、または個別プロジェクトをビルドできます。

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" Samples\BistroExteriorRaytracing\BistroExteriorRaytracing.sln /p:Platform=x64 /p:Configuration=Debug
```

個別プロジェクト:

```powershell
Samples\BistroExteriorRaytracing\D3D12\Source\BistroExteriorRaytracingD3D12.vcxproj
Samples\BistroExteriorRaytracing\Vulkan\Source\BistroExteriorRaytracingVulkan.vcxproj
Samples\BistroExteriorRaytracing\D3D12Shadow\Source\BistroExteriorRaytracingShadowD3D12.vcxproj
Samples\BistroExteriorRaytracing\VulkanShadow\Source\BistroExteriorRaytracingShadowVulkan.vcxproj
Samples\BistroExteriorRaytracing\D3D12GI\Source\BistroExteriorRaytracingGID3D12.vcxproj
Samples\BistroExteriorRaytracing\VulkanGI\Source\BistroExteriorRaytracingGIVulkan.vcxproj
```

## デバイス要件

- D3D12 は DXR ray tracing support が必要です。この版では検出した ray tracing tier を表示しますが、Tier 1.2 専用機能は必須にしていません。
- Vulkan は Vulkan 1.2+、`VK_KHR_acceleration_structure`、`VK_KHR_ray_tracing_pipeline`、`VK_KHR_deferred_host_operations`、`VK_KHR_buffer_device_address`、`VK_KHR_spirv_1_4`、`VK_KHR_shader_float_controls`、descriptor indexing support が必要です。

## Third Party

- `ThirdParty/assimp`: FBX scene import
- `ThirdParty/DirectXTex`: DDS/TGA texture loading
- `ThirdParty/imgui`: debug controls and renderer stats
