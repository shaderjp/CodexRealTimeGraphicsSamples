# BistroInteriorPathtracing

英語版は [README.md](README.md) を参照してください。

`BistroInteriorPathtracing` は Bistro Interior シーンを DXR と Vulkan Ray Tracing の progressive path tracer として描画するサンプルです。Exterior path tracing sample をベースにしつつ、`BistroInterior.fbx` を読み込み、室内向けの camera、lighting、movement 初期値を使います。

## プロジェクト

- `BistroInteriorPathtracingD3D12`
- `BistroInteriorPathtracingVulkan`
- `BistroInteriorPathtracingReSTIRD3D12`
- `BistroInteriorPathtracingReSTIRVulkan`
- `BistroInteriorPathtracingReSTIRDID3D12`
- `BistroInteriorPathtracingReSTIRDIVulkan`

## 現在の内容

- `ThirdParty/assimp` による `BistroInterior.fbx` の実行時読み込み
- `ThirdParty/DirectXTex` による DDS/TGA テクスチャ読み込みと、Interior material 向け suffix fallback
- Bistro の draw item ごとに 1 つの triangle geometry を持つ static BLAS と、identity TLAS instance
- primary ray / visibility ray の any-hit alpha test による alpha masked material 対応
- D3D12 descriptor table と Vulkan descriptor indexing による material texture の bindless 参照
- raygen 内 path tracing loop による diffuse / GGX 風 stochastic specular bounce
- sun、procedural sky、emissive triangle、procedural area light の next-event estimation
- 室内向け local light list: 上限付き emissive triangle light と、bounds から作る暖色の ceiling / counter area proxy
- Bistro root の `san_giuseppe_bridge_4k.hdr` を優先する optional equirectangular environment map sampling
- Exterior path tracing sample 由来の progressive accumulation、debug view、ReSTIR GI / DI 比較 variant、built-in lightweight denoiser

## アセット

Bistro データセットはこのリポジトリには含めません。NVIDIA ORCA から Amazon Lumberyard Bistro を取得し、展開した `Bistro_v5_2` フォルダをリポジトリ直下に置いてください。

```text
CodexdRealTimeGraphicsSamples/
  Bistro_v5_2/
    BistroInterior.fbx
    san_giuseppe_bridge_4k.hdr
    Textures/
```

または、`BistroInterior.fbx` を直接含むフォルダを `BISTRO_ASSET_ROOT` 環境変数で指定できます。

## ライティングメモ

このサンプルは Exterior path tracing shader model を維持し、Wine scene 向けの transmission、volume absorption、nested dielectric までは追加しません。`BistroInterior.fbx` の material は、他の Bistro サンプルと同じ base color、normal、specular/roughness、metallic、alpha、emissive channel として扱います。

load 時には emissive material を上限付き triangle light list に変換します。さらに scene bounds から 3 つの暖色 rectangular area light を作り、通常 path tracer と ReSTIR DI の local-light candidate を安定して確保します。

## ビルド

先に submodule を初期化してください。

```powershell
git submodule update --init --recursive
```

Visual Studio 2022 または MSBuild でソリューション全体、または個別プロジェクトをビルドできます。

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" Samples\BistroInteriorPathtracing\BistroInteriorPathtracing.sln /p:Platform=x64 /p:Configuration=Debug
```

個別プロジェクト:

```powershell
Samples\BistroInteriorPathtracing\D3D12\Source\BistroInteriorPathtracingD3D12.vcxproj
Samples\BistroInteriorPathtracing\Vulkan\Source\BistroInteriorPathtracingVulkan.vcxproj
Samples\BistroInteriorPathtracing\D3D12ReSTIR\Source\BistroInteriorPathtracingReSTIRD3D12.vcxproj
Samples\BistroInteriorPathtracing\VulkanReSTIR\Source\BistroInteriorPathtracingReSTIRVulkan.vcxproj
Samples\BistroInteriorPathtracing\D3D12ReSTIRDI\Source\BistroInteriorPathtracingReSTIRDID3D12.vcxproj
Samples\BistroInteriorPathtracing\VulkanReSTIRDI\Source\BistroInteriorPathtracingReSTIRDIVulkan.vcxproj
```

## デバイス要件

- D3D12 は DXR ray tracing support が必要です。この版では検出した ray tracing tier を表示しますが、Tier 1.2 専用機能は必須にしていません。
- Vulkan は Vulkan 1.2+、`VK_KHR_acceleration_structure`、`VK_KHR_ray_tracing_pipeline`、`VK_KHR_deferred_host_operations`、`VK_KHR_buffer_device_address`、`VK_KHR_spirv_1_4`、`VK_KHR_shader_float_controls`、descriptor indexing support が必要です。

## Third Party

- `ThirdParty/assimp`: FBX scene import
- `ThirdParty/DirectXTex`: DDS/TGA texture loading
- `ThirdParty/imgui`: debug controls and renderer stats
