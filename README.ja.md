# CodexRealTimeGraphicsSamples

Windows / Visual Studio 2022 向けの Direct3D12 と Vulkan のリアルタイムグラフィックスサンプル集です。

英語版は [README.md](README.md) を参照してください。

このリポジトリでは、グラフィックス技術ごとに `Samples` 配下へフォルダを作ります。各サンプルは共有アセットと共有 HLSL を持ち、Direct3D12 / Vulkan の Visual Studio プロジェクトを横並びで配置します。

## 必要環境

- Windows 10/11
- Visual Studio 2022 と Desktop development with C++ ワークロード
- Windows SDK と Direct3D 12 のヘッダー / ライブラリ
- Vulkan SDK と `VULKAN_SDK` 環境変数
- Git submodule の初期化

Direct3D12 サンプルは `Microsoft.Direct3D.D3D12` NuGet パッケージ経由で DirectX 12 Agility SDK を使います。Vulkan サンプルは Vulkan SDK の `dxc` で HLSL から SPIR-V をビルドします。シェーダターゲットは HLSL Shader Model 6.6 です。

## セットアップ

```powershell
git submodule update --init --recursive
```

各サンプルの `.sln` を Visual Studio 2022 で開き、`x64` の Debug または Release をビルドしてください。

コマンドラインでは MSBuild から NuGet restore とビルドを実行できます。

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" Samples\Triangle\Triangle.sln /restore /p:Platform=x64 /p:Configuration=Debug
```

## サンプル

- `Samples/Triangle`
  D3D12 / Vulkan の初期化と三角形描画。
- `Samples/Cube3D`
  頂点バッファ、インデックスバッファ、Constant Buffer、Depth Buffer を使った回転する立方体。
- `Samples/SciFiHelmet`
  glTF 2.0 SciFiHelmet を読み込み、PBR ベースの HLSL シェーダ、法線マップ、AO、1 つのディレクショナルライトで描画。
- `Samples/ImGuiLighting`
  SciFiHelmet の描画に Dear ImGui を統合し、ディレクショナルライトの方向、色、強度を実行時に編集。
- `Samples/BistroExterior`
  Amazon Lumberyard Bistro Exterior を外部の `Bistro_v5_2` フォルダから Assimp で読み込み、DirectXTex で DDS/TGA テクスチャを扱い、広いシーンを FPS 風カメラで移動しながら描画。D3D12 / Vulkan の Shadow Map 版も同じソリューションに含み、ImGui からディレクショナルライト shadow のパラメータとデバッグ表示を操作できます。
- `Samples/BistroExteriorMeshShader`
  同じ Bistro Exterior シーンを Mesh Shader pipeline で描画します。D3D12 / Vulkan の直接光、AS/TS meshlet culling、shadow map、shadow map + culling variant を含み、meshoptimizer による実行時 meshlet 生成、ImGui の `Meshlet Color` デバッグ表示、meshlet dispatch / culling 統計を確認できます。
- `Samples/BistroExteriorRaytracing`
  同じ Bistro Exterior シーンを DXR と Vulkan Ray Tracing で描画します。シーン用 raster pass は使わず、D3D12 / Vulkan の直接光、ray-traced shadow、簡易 1-bounce GI variant、alpha test any-hit、bindless texture shading、ImGui debug view、SBT / acceleration structure 統計を確認できます。
- `Samples/BistroExteriorPathtracing`
  同じ Bistro Exterior シーンを DXR と Vulkan Ray Tracing の progressive path tracer として描画します。D3D12 / Vulkan の通常 path tracing project と reservoir temporal / spatial reuse を持つ ReSTIR GI / ReSTIR DI 比較用 project を含み、procedural sky / sun next-event estimation、diffuse / specular bounce、alpha-tested visibility ray、accumulation controls、built-in denoiser、path tracing debug view を確認できます。
- `Samples/Skinning`
  glTF 2.0 Cesium Man を読み込み、Joint Matrix の Constant Buffer を使った Vertex Shader Skinning / Compute Shader Skinning でアニメーション描画。
  Compute Shader Skinning 版は GPU 上の skinned vertex buffer に書き出してから描画します。

各サンプルには英語版 `README.md` と日本語版 `README.ja.md` を置いています。

## スクリーンショット

各サンプル README に、用意できているものは `docs/images` の Direct3D12 / Vulkan スクリーンショットを反映しています。`Samples/BistroExterior` では Vulkan DDS/BC 対応後の通常描画、ImGui の `Debug View` で切り替える Base Color、World Normal、Normal Texture Decoded、Shadow Map 系デバッグ表示の比較画像と、`Renderer Stats` オーバーレイも掲載しています。`Samples/BistroExteriorMeshShader` では Mesh Shader 版と shadow+culling 版の代表画像、および `Meshlet Color` デバッグ表示を掲載しています。`Samples/BistroExteriorRaytracing` では DXR / Vulkan Ray Tracing の variant と debug view を説明しています。`Samples/BistroExteriorPathtracing` では progressive path tracing、ReSTIR GI、ReSTIR DI 比較用 project、built-in denoiser control を説明しています。

## 読み物 / 実装振り返り

`docs` 配下に、サンプルの読み方や実装時に見えた設計ポイントをまとめた日本語記事を置いています。

- [Bistro 実装前までの振り返り](docs/pre_bistro_implementation_review.ja.md)
  Triangle、Cube3D、SciFiHelmet、Skinning、ImGuiLighting までの基礎サンプルを振り返ります。
- [Bistro 取り組みの振り返り](docs/bistro_implementation_review.ja.md)
  BistroExterior、Shadow Map、Mesh Shader、Raytracing、Pathtracing までの流れをまとめています。
- [Bistro Mesh Shader 実装の振り返り](docs/bistro_mesh_shader_implementation_review.ja.md)
  Meshlet 生成、Mesh Shader、AS/TS culling、Shadow 版の読み方を整理しています。
- [Bistro Raytracing 実装の振り返り](docs/bistro_raytracing_implementation_review.ja.md)
  DXR / Vulkan Ray Tracing、BLAS/TLAS、SBT、shadow ray、procedural sky、1-bounce GI の読み方を整理しています。
- [Bistro Pathtracing 実装の振り返り](docs/bistro_pathtracing_implementation_review.ja.md)
  Progressive path tracing、ReSTIR GI / DI、light list、accumulation、built-in denoiser の読み方を整理しています。

## サンプル追加方針

新しいサンプルは次の形で追加します。

```text
Samples/<TechniqueName>/
  <TechniqueName>.sln
  README.md
  README.ja.md
  Directory.Build.props
  Directory.Build.targets
  Assets/
  Shaders/
  D3D12/
    Source/
      <TechniqueName>D3D12.vcxproj
  Vulkan/
    Source/
      <TechniqueName>Vulkan.vcxproj
  D3D12Compute/
    Source/
      <TechniqueName>ComputeD3D12.vcxproj
  VulkanCompute/
    Source/
      <TechniqueName>ComputeVulkan.vcxproj
```

Compute 版のように別パイプラインとして比較したい実装は、同じソリューション内の兄弟プロジェクトとして追加します。共有 HLSL は原則としてサンプルの `Shaders` フォルダに置きます。API 固有の差分が必要な場合だけ、プリプロセッサ定義や API 別シェーダで分けます。

## Third Party

Third-party 依存は `ThirdParty` 配下の Git submodule として管理します。

- `ThirdParty/imgui`
- `ThirdParty/DirectXTex`
- `ThirdParty/assimp`
- `ThirdParty/meshoptimizer`

詳細は [docs/third_party.md](docs/third_party.md) を参照してください。
