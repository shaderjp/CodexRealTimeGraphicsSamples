# Bistro Exterior Clustered Forward

Amazon Lumberyard Bistro Exterior に多光源 Forward 描画を追加し、通常の Many Lights Forward と GPU Clustered Forward を比較するサンプルです。どちらの経路にも directional light 用の Shadow Map を入れ、local light の比較コストは保ったまま太陽光の影を確認できます。

## プロジェクト

- `BistroExteriorManyLightsD3D12`
- `BistroExteriorManyLightsVulkan`
- `BistroExteriorClusteredForwardD3D12`
- `BistroExteriorClusteredForwardVulkan`

ManyLights 版は pixel shader で active local lights を直接ループします。ClusteredForward 版は compute shader で毎フレーム 3D cluster light list を作り、pixel shader では screen tile と logarithmic depth slice に割り当てられた light だけを評価します。

## ライティング

Bistro Exterior の scene loader を継承し、Bistro の emissive material / emissive texture から raster 向けの sphere light proxy を生成します。Pathtracing サンプルの light list 生成と同じ考え方で、シーン内に deterministic な procedural area light proxy も追加します。

既存の directional light は cluster 対象外として残し、通常の depth shadow map で影を描画します。local light shadow はこの版では入れず、ManyLights / ClusteredForward の比較は light list 構築と shading cost に寄せています。

## スクリーンショット

ManyLights 版は、生成された同じ light list を pixel shader で直接ループして描画します。

| Direct3D 12 | Vulkan |
|---|---|
| ![BistroExteriorManyLights D3D12](<../../docs/images/BistroExteriorManyLights D3D12 2026_05_08 23_45_53.png>) | ![BistroExteriorManyLights Vulkan](<../../docs/images/BistroExteriorManyLights Vulkan 2026_05_08 23_46_32.png>) |

ClusteredForward 版は、forward shading の前に screen tile と depth slice ごとの visible light list を作ります。

| Direct3D 12 | Vulkan |
|---|---|
| ![BistroExteriorClusteredForward D3D12](<../../docs/images/BistroExteriorClusteredForward D3D12 2026_05_08 23_47_09.png>) | ![BistroExteriorClusteredForward Vulkan](<../../docs/images/BistroExteriorClusteredForward Vulkan 2026_05_08 23_47_41.png>) |

Debug View では、cluster ごとの light 数や directional shadow map の結果を確認できます。

| Cluster Light Count | Shadow Factor | Shadow Map Depth |
|---|---|---|
| ![Cluster Light Count debug view](<../../docs/images/BistroExteriorClusteredForward Vulkan 2026_05_08 23_48_33.png>) | ![Shadow Factor debug view](<../../docs/images/BistroExteriorClusteredForward Vulkan 2026_05_08 23_48_54.png>) | ![Shadow Map Depth debug view](<../../docs/images/BistroExteriorClusteredForward Vulkan 2026_05_08 23_49_11.png>) |

## 操作

ImGui から次の項目を操作できます。

- Directional light の方向、色、強度
- FPS camera
- Local light、emissive proxy、procedural proxy の有効化
- Active light count、local light intensity scale、radius scale
- Directional shadow の有効化、解像度、depth / normal bias、PCF radius、light frustum 調整
- Clustered 版のみ、Z slice count と cluster-grid stats
- Final、Directional Only、Local Light Contribution、Cluster Light Count、Cluster Slice、Cluster Overflow、Shadow Map Depth、Shadow Factor、Light Space Depth などの debug view

## ビルド

Visual Studio 2022 で `BistroExteriorClusteredForward.sln` を開き、`x64` の Debug または Release をビルドしてください。

アセットは既存の Bistro asset search path を使って `BistroExterior.fbx` を読み込みます。`Samples/BistroExterior` と同じく、`BISTRO_ASSET_ROOT` を設定するか、リポジトリルートに `Bistro_v5_2` を置いてください。
