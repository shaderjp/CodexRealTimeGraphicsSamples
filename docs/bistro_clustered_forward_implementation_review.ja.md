# Bistro Clustered Forward 実装の振り返り

このドキュメントは、`Samples/BistroExteriorClusteredForward` の実装を振り返りながら、Bistro Exterior に多光源 Forward と Clustered Forward を追加した流れを入門者向けに読むためのメモです。

このシリーズは次の順番で読む想定です。

1. [Bistro 実装前までの振り返り](pre_bistro_implementation_review.ja.md)
2. [Bistro 取り組みの振り返り](bistro_implementation_review.ja.md)
3. このドキュメント
4. [Bistro Mesh Shader 実装の振り返り](bistro_mesh_shader_implementation_review.ja.md)
5. [Bistro Raytracing 実装の振り返り](bistro_raytracing_implementation_review.ja.md)
6. [Bistro Pathtracing 実装の振り返り](bistro_pathtracing_implementation_review.ja.md)

Bistro の通常ラスタ版では、1 つの directional light と material texture を使って巨大シーンを描きました。Clustered Forward 版では、そのラスタ構成を保ったまま、Bistro の emissive material / emissive texture と procedural light proxy から多くの local light を作り、通常の forward と clustered forward を比較できる形にしています。

## 対象範囲

`Samples/BistroExteriorClusteredForward` は 1 つの solution に 4 つのプロジェクトを持ちます。

| 種類 | D3D12 | Vulkan | 役割 |
| --- | --- | --- | --- |
| Many Lights | `BistroExteriorManyLightsD3D12` | `BistroExteriorManyLightsVulkan` | active local lights を pixel shader で直接ループ |
| Clustered Forward | `BistroExteriorClusteredForwardD3D12` | `BistroExteriorClusteredForwardVulkan` | compute shader で cluster light list を作ってから forward shading |

どちらも同じ Bistro scene、同じ material、同じ generated light list、同じ directional shadow map を使います。違うのは、local light を pixel shader で全件見るか、cluster に入った light だけを見るかです。

## このサンプルで増えたもの

通常の `Samples/BistroExterior` と比べると、Clustered Forward 版では次が増えます。

- emissive material / emissive texture 由来の local light proxy
- procedural area light proxy
- raster 向けの sphere light proxy
- active light count / intensity / radius scale の UI
- ManyLights 版の全 light loop
- ClusteredForward 版の compute pass
- screen tile と depth slice による 3D cluster
- cluster record buffer
- cluster light index buffer
- cluster stats buffer
- `Cluster Light Count` / `Cluster Slice` / `Cluster Overflow` debug view
- directional shadow map
- `Shadow Factor` / `Shadow Map Depth` / `Light Space Depth` debug view

目的は、最新の複雑な clustered renderer を全部作ることではありません。Bistro という実シーンで「多光源をそのまま forward で回すとどうなるか」「cluster list を作ると shader が何を参照するようになるか」を見える形にすることです。

## Many Lights と Clustered Forward

ManyLights 版は比較用の素直な実装です。

```text
pixel
  -> active local lights を 0..N でループ
  -> 各 light の距離減衰と BRDF を足す
```

実装は読みやすいですが、active light 数が増えるほど pixel shader の仕事が増えます。画面上のほとんどの pixel に関係ない light も毎回チェックするためです。

ClusteredForward 版では、描画前に compute shader で cluster ごとの light list を作ります。

```text
camera / viewport
  -> screen を 16x16 tile に分ける
  -> depth を logarithmic slice に分ける
  -> 各 cluster と sphere light を交差判定
  -> cluster record と light index list を書く
  -> pixel shader は自分の cluster の light だけを見る
```

このため、pixel shader では local light 全体ではなく、現在の screen tile と view-space depth から求めた cluster の light list だけを評価します。

## Light list の作り方

このサンプルの local light は、Pathtracing 版で作った light list の考え方を raster 用に移植しています。

見るべきファイル:

- `../Samples/BistroExteriorClusteredForward/Common/BistroScene.h`
- `../Samples/BistroExteriorClusteredForward/Common/BistroScene.cpp`

代表的な構造体は次です。

| 構造体 | 役割 |
| --- | --- |
| `RasterLight` | GPU に渡す local light。`positionRadius` と `radianceSource` を持つ |
| `LightBuildResult` | 生成された light 配列と emissive / procedural の内訳 |
| `ClusterRecord` | cluster ごとの light index offset / count / overflow |
| `ClusterStats` | active cluster 数、割り当て light 参照数、最大 light 数、overflow 数 |

Emissive triangle や area light を正確な面光源として raster shader で評価するのは重くなります。そこで v1 では、それらを近似的な sphere light proxy に変換しています。

```text
emissive material / texture
  -> triangle centroid
  -> sphere light proxy

procedural rectangle light
  -> representative position
  -> sphere light proxy
```

これにより、forward shader 側は「点 / 球に近い local light」として統一的に扱えます。

## Cluster の作り方

ClusteredForward 版では、screen を 16x16 pixel の tile に分け、depth 方向を logarithmic slice に分けます。

```text
cluster id =
  tileX
  + tileY * tileCountX
  + zSlice * tileCountX * tileCountY
```

Z slice は linear ではなく logarithmic です。カメラ近くは depth の変化が見た目に大きく影響するため、近くに slice を多めに割り当てた方が扱いやすくなります。

見るべき shader entry:

- `CSResetStats`
- `CSBuildClusters`
- `PSMain`

見るべきファイル:

- `../Samples/BistroExteriorClusteredForward/Shaders/BistroExteriorClusteredForward.hlsl`

`CSBuildClusters` は、cluster と light の交差判定を行い、見える可能性がある light index を `RWStructuredBuffer` に書きます。`PSMain` は screen position と depth から cluster id を復元し、その cluster の light list をループします。

## Debug View の読み方

Clustered Forward は、最終画像だけを見ても内部が分かりにくいです。そのため、このサンプルでは debug view が重要です。

| Debug View | 見ること |
| --- | --- |
| `Local Light Contribution` | local light だけの寄与 |
| `Cluster Light Count` | cluster に入った light 数 |
| `Cluster Slice` | depth slice の分布 |
| `Cluster Overflow` | `MaxLightsPerCluster` を超えた cluster |
| `Shadow Factor` | directional shadow の係数 |
| `Shadow Map Depth` | shadow map の深度 |
| `Light Space Depth` | light view projection 後の深度 |

![Cluster Light Count](<images/BistroExteriorClusteredForward Vulkan 2026_05_08 23_48_33.png>)

`Cluster Light Count` は、カメラを動かしたときに分布が変わることを見るための表示です。白くなるほど、その cluster に多くの light が入っています。

![Shadow Factor](<images/BistroExteriorClusteredForward Vulkan 2026_05_08 23_48_54.png>)

`Shadow Factor` は directional light の shadow map が最終 shading にどう効いているかを見る表示です。local light shadow は v1 では入れていないため、この表示は directional light の影だけを確認するものです。

## D3D12 側で見ること

見るべきファイル:

- `../Samples/BistroExteriorClusteredForward/D3D12ManyLights/Source/BistroExteriorManyLightsD3D12.cpp`
- `../Samples/BistroExteriorClusteredForward/D3D12ClusteredForward/Source/BistroExteriorClusteredForwardD3D12.cpp`

D3D12 版でまず見るべきなのは、descriptor と root signature の対応です。

```text
b0  Scene constants
b1  Material constants
t0..t3 material textures
s0  material sampler
t4  light buffer
t5  cluster records
t6  cluster light indices
u0..u2 cluster build buffers
t7  shadow map
s1  comparison sampler
```

ManyLights 版では compute pass は実質使いませんが、shader と root signature をそろえるために cluster buffer の枠は持っています。ClusteredForward 版では、描画前に compute pipeline を実行し、UAV barrier を挟んで pixel shader から cluster buffers を読みます。

Shadow map は depth-only pass として先に描き、そのあと main pass で comparison sampler から参照します。D3D12 の Debug Layer では resource state と root binding の不整合がすぐ出るので、shadow map の DSV/SRV 作成、barrier、root descriptor table の順番を見ると学びが多いです。

## Vulkan 側で見ること

見るべきファイル:

- `../Samples/BistroExteriorClusteredForward/VulkanManyLights/Source/BistroExteriorManyLightsVulkan.cpp`
- `../Samples/BistroExteriorClusteredForward/VulkanClusteredForward/Source/BistroExteriorClusteredForwardVulkan.cpp`

Vulkan 版では、D3D12 の root signature に相当するものを descriptor set layout として明示します。

見るポイントは次です。

- sampled image と sampler を別 binding にしていること
- storage buffer を fragment / compute の両方で使うこと
- compute shader write から fragment shader read へ barrier を張ること
- shadow map 用に depth-only render pass / framebuffer / sampler を持つこと
- swapchain resize / minimize の安定化方針を既存 Bistro 系から継承していること

D3D12 と Vulkan で同じ HLSL を共有するため、`[[vk::binding]]` と register の対応が重要です。binding が 1 つずれると、最終画像より debug view の方が先に破綻することがあります。

## Shadow Map の位置づけ

このサンプルの shadow map は、Clustered Forward の主題ではありません。ただし、多光源サンプルとして見たときに、directional light が常に影なしだと Bistro の見た目が平坦になります。そこで、既存 BistroExterior Shadow の考え方を移植し、directional light のみ shadow map を使う形にしています。

```text
camera position
  -> focus point
  -> light direction から light camera を作る
  -> orthographic shadow map
  -> main pass で shadow factor をかける
```

local light shadow は入れていません。入れる場合は、shadow atlas、cubemap shadow、clustered shadow assignment など別の主題が増えるため、v1 では切り分けています。

## 読む順番

おすすめの読み順は次です。

1. `README.ja.md`
   プロジェクト構成とスクリーンショットを確認します。

2. `Common/BistroScene.cpp`
   Bistro の scene loader と light proxy 生成を確認します。

3. `Shaders/BistroExteriorClusteredForward.hlsl`
   `RasterLight`、local light shading、cluster debug view を確認します。

4. `D3D12ManyLights` または `VulkanManyLights`
   active light を直接ループする素直な forward を確認します。

5. `D3D12ClusteredForward` または `VulkanClusteredForward`
   compute pass、cluster buffers、barrier、pixel shader 参照を確認します。

6. shadow map の pass
   shadow map が final shading に入るまでの resource / descriptor / sampler を追います。

## 次の Mesh Shader 版へのつながり

Clustered Forward は、「fragment shading の仕事を減らす」サンプルです。Mesh Shader 版は、次に「geometry submission の単位を変える」サンプルになります。

```text
Clustered Forward
  local light を cluster 単位で絞る

Mesh Shader
  geometry を meshlet 単位で送る
  meshlet 単位で culling する
```

どちらも考え方は似ています。大きな仕事を小さい単位に分け、その単位ごとに必要なものだけを処理します。Clustered Forward を読んでおくと、Mesh Shader 版の meshlet culling も「単位が light cluster から meshlet に変わった」と捉えやすくなります。
