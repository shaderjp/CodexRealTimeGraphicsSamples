# BistroExteriorPathtracing

英語版は [README.md](README.md) を参照してください。

`BistroExteriorPathtracing` は Bistro Exterior シーンを DXR と Vulkan Ray Tracing の progressive path tracer として描画するサンプルです。シーンの可視性はすべて ray tracing で解決し、シーン用 raster pass、shadow map、mesh shader pass は使いません。ray tracing 出力を UAV / storage image に蓄積し、swapchain へコピーしたあと ImGui overlay を描画します。

## プロジェクト

- `BistroExteriorPathtracingD3D12`
- `BistroExteriorPathtracingVulkan`
- `BistroExteriorPathtracingReSTIRD3D12`
- `BistroExteriorPathtracingReSTIRVulkan`
- `BistroExteriorPathtracingReSTIRDID3D12`
- `BistroExteriorPathtracingReSTIRDIVulkan`

## スクリーンショット

最初の2枚は同じ D3D12 カメラ / 設定で、built-in denoiser の off / on を比較しています。残りは Vulkan 通常版と ReSTIR DI 比較版で、ImGui から denoiser control を調整できる状態を示しています。

| D3D12 denoiser off | D3D12 denoiser on |
| --- | --- |
| ![D3D12 denoiser disabled](<../../docs/images/BistroExteriorPathtracing D3D12 2026_05_08 0_36_56.png>) | ![D3D12 denoiser enabled](<../../docs/images/BistroExteriorPathtracing D3D12 2026_05_08 0_37_03.png>) |

| ReSTIR DI D3D12 | ReSTIR DI Vulkan |
| --- | --- |
| ![ReSTIR DI D3D12](<../../docs/images/BistroExteriorPathtracing ReSTIR DI D3D12 2026_05_08 0_37_45.png>) | ![ReSTIR DI Vulkan](<../../docs/images/BistroExteriorPathtracing ReSTIR DI Vulkan 2026_05_08 0_38_38.png>) |

![Vulkan path tracing with denoiser](<../../docs/images/BistroExteriorPathtracing Vulkan 2026_05_08 0_39_16.png>)

## 現在の内容

- `ThirdParty/assimp` による `BistroExterior.fbx` の実行時読み込み
- `ThirdParty/DirectXTex` による DDS/TGA テクスチャ読み込み
- Bistro の draw item ごとに 1 つの triangle geometry を持つ static BLAS と、identity TLAS instance
- `GeometryIndex()` から共有 material / geometry record を参照
- primary ray / visibility ray の any-hit alpha test による alpha masked material 対応
- D3D12 descriptor table と Vulkan descriptor indexing による material texture の bindless 参照
- raygen が iterative path tracing loop を持ち、ray pipeline recursion depth は 1 に維持
- base color、normal、roughness、metallic、emissive、alpha を使った diffuse / GGX 風 stochastic specular bounce
- alpha-aware visibility ray による sun / procedural sky next-event estimation
- ray tracing の miss shader で評価する procedural gradient sky と sun disk
- emissive material から capped emissive triangle light list を生成し、next-event estimation に利用
- 追加 geometry なしで window / sign / interior light 風の確認ができる procedural rectangular area light
- procedural sky を fallback にした、miss shader / sky NEE での optional 2D environment map sampling
- samples per frame、radiance clamp、temporal clamp、max accumulated sample count、freeze/reset control を持つ progressive accumulation
- normal / depth / albedo / luminance を使った multi-scale cross-bilateral filter の軽量 denoiser。追加 ThirdParty なしでサンプル内の compute pass として実装
- ReSTIR GI 比較用プロジェクトとして、別 shader variant、ReSTIR 向け UI、追加 candidate sampling、current/history/spatial reservoir buffer、temporal / spatial reuse compute pass を追加
- Sun と local light list による primary-hit direct lighting に同じ reservoir flow を適用する ReSTIR DI 比較用プロジェクト
- ImGui から light、camera、sky、environment map、emissive light、procedural area light、ray bias、path depth、accumulation、denoiser 設定、ReSTIR 設定、debug view を調整
- Debug View: Final、Base Color、World Normal、Normal Texture、Roughness、Metallic、Emissive、Hit Distance、Direct NEE、Indirect、Bounce Count、Accumulation Samples、Sky、Reservoir Weight、Temporal Reuse、Spatial Reuse
- Renderer Stats: material、texture、vertex、index、primitive、BLAS geometry、TLAS instance、SBT record、light list count、output resolution、accumulation sample、ray tracing limit

## ReSTIR の状態

ReSTIR プロジェクトは、通常の path tracing baseline、ReSTIR GI、ReSTIR DI を同じソリューション内で比較できるように、D3D12 / Vulkan の別 executable として追加しています。ReSTIR GI 版では raygen で pixel ごとの indirect-light reservoir を生成します。ReSTIR DI 版では、sun と local light list から得られる primary-hit direct lighting を reservoir 化します。どちらも compute の temporal / spatial reuse pass で current、history、近傍 reservoir を合成して、解決した spatial reservoir を次フレーム用の history にコピーします。

このサンプルでは、production 向けの visibility validation や disocclusion test までは入れず、reservoir reuse、debug visibility、D3D12 / Vulkan で対応する resource flow を小さく確認できる形にしています。

## Denoiser

すべての Pathtracing variant に、追加 ThirdParty なしの小さな compute denoiser を入れています。Raygen が normal/depth と albedo/roughness の補助 AOV を出力し、compute shader が accumulation 済みの linear color を multi-scale cross-bilateral filter で処理してから swapchain へコピーします。適用対象は `Final` debug view のみで、各種 debug view は比較しやすいように未フィルタのままです。

ImGui の `Denoiser` セクションでは enable、spatial iterations、normal sigma、depth sigma、luminance sigma、albedo sigma、blend strength を調整できます。初期値はエッジを保ちながら、progressive sampling の初期ノイズを少し落ち着かせる控えめな設定です。

## アセット

Bistro データセットはこのリポジトリには含めません。NVIDIA ORCA から Amazon Lumberyard Bistro を取得し、展開した `Bistro_v5_2` フォルダをリポジトリ直下に置いてください。

```text
CodexdRealTimeGraphicsSamples/
  Bistro_v5_2/
    BistroExterior.fbx
    Textures/
```

または、`BistroExterior.fbx` を直接含むフォルダを `BISTRO_ASSET_ROOT` 環境変数で指定できます。

## ライティング拡張

Pathtracing の各プロジェクトは、load 時に小さな `RtLight` list を構築します。Emissive material は triangle light として登録されますが、サンプルを軽く保つため上限で間引きます。また、Bistro の bounds を基準に 3 つの procedural rectangular area light を追加し、通常版と ReSTIR 版の両方で sun 以外の local lighting をサンプリングできるようにしています。これらの procedural light は analytical light のみで、表示用 mesh geometry は追加しません。

`Environment Map` control で optional な equirectangular 2D environment map を有効化できます。`BISTRO_ENVIRONMENT_MAP` に `.hdr`、`.dds`、`.tga`、`.png`、`.jpg`、`.jpeg` を指定するか、Bistro asset root または `Textures`、`Environment`、`Environments` サブフォルダに sky / environment 系の名前のファイルを置くと読み込みます。見つからない場合は procedural sky fallback のまま動作します。

## ビルド

先に submodule を初期化してください。

```powershell
git submodule update --init --recursive
```

Visual Studio 2022 または MSBuild でソリューション全体、または個別プロジェクトをビルドできます。

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" Samples\BistroExteriorPathtracing\BistroExteriorPathtracing.sln /p:Platform=x64 /p:Configuration=Debug
```

個別プロジェクト:

```powershell
Samples\BistroExteriorPathtracing\D3D12\Source\BistroExteriorPathtracingD3D12.vcxproj
Samples\BistroExteriorPathtracing\Vulkan\Source\BistroExteriorPathtracingVulkan.vcxproj
Samples\BistroExteriorPathtracing\D3D12ReSTIR\Source\BistroExteriorPathtracingReSTIRD3D12.vcxproj
Samples\BistroExteriorPathtracing\VulkanReSTIR\Source\BistroExteriorPathtracingReSTIRVulkan.vcxproj
Samples\BistroExteriorPathtracing\D3D12ReSTIRDI\Source\BistroExteriorPathtracingReSTIRDID3D12.vcxproj
Samples\BistroExteriorPathtracing\VulkanReSTIRDI\Source\BistroExteriorPathtracingReSTIRDIVulkan.vcxproj
```

## デバイス要件

- D3D12 は DXR ray tracing support が必要です。この版では検出した ray tracing tier を表示しますが、Tier 1.2 専用機能は必須にしていません。
- Vulkan は Vulkan 1.2+、`VK_KHR_acceleration_structure`、`VK_KHR_ray_tracing_pipeline`、`VK_KHR_deferred_host_operations`、`VK_KHR_buffer_device_address`、`VK_KHR_spirv_1_4`、`VK_KHR_shader_float_controls`、descriptor indexing support が必要です。

## Third Party

- `ThirdParty/assimp`: FBX scene import
- `ThirdParty/DirectXTex`: DDS/TGA texture loading
- `ThirdParty/imgui`: debug controls and renderer stats
