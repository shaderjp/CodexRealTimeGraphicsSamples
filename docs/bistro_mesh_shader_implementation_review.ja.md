# Bistro Mesh Shader 実装の振り返り

このドキュメントは、`Samples/BistroExteriorMeshShader` の実装を振り返りながら、Mesh Shader と meshlet culling を入門者向けに読むためのメモです。

前提となる記事は次の 3 つです。

- [Bistro 実装前までの振り返り](pre_bistro_implementation_review.ja.md)
- [Bistro 取り組みの振り返り](bistro_implementation_review.ja.md)
- [Bistro Clustered Forward 実装の振り返り](bistro_clustered_forward_implementation_review.ja.md)

最初の記事では Triangle、Cube3D、SciFiHelmet、Skinning、ImGuiLighting までの基礎を扱い、Bistro 取り組みの振り返りでは BistroExterior、Shadow、Clustered Forward、Mesh Shader、Raytracing、Pathtracing までを大きく俯瞰しました。Clustered Forward の記事では、同じラスタライズの枠内で多光源と GPU compute の light list を扱いました。このドキュメントでは、その次の段階として Mesh Shader 版だけを取り出し、「通常の raster sample から何が変わったのか」「どの順番で読むと理解しやすいか」を整理します。

## 対象範囲

`Samples/BistroExteriorMeshShader` は 1 つの solution に 8 つのプロジェクトを持ちます。

| 段階 | D3D12 | Vulkan | 役割 |
| --- | --- | --- | --- |
| 1 | `BistroExteriorMeshShaderD3D12` | `BistroExteriorMeshShaderVulkan` | 直接光、Mesh Shader のみ、AS/TS なし |
| 2 | `BistroExteriorMeshShaderCullingD3D12` | `BistroExteriorMeshShaderCullingVulkan` | 直接光、AS/TS による meshlet culling |
| 3 | `BistroExteriorMeshShaderShadowD3D12` | `BistroExteriorMeshShaderShadowVulkan` | Shadow Map、Mesh Shader のみ、AS/TS なし |
| 4 | `BistroExteriorMeshShaderShadowCullingD3D12` | `BistroExteriorMeshShaderShadowCullingVulkan` | Shadow Map、AS/TS による meshlet culling |

最初に読むなら、まず `BistroExteriorMeshShaderD3D12` または `BistroExteriorMeshShaderVulkan` の直接光版だけで十分です。そこでは culling や shadow のことを忘れて、「vertex shader / index buffer draw が mesh shader dispatch に置き換わった」点だけを見ます。

## Mesh Shader 版で増えたもの

通常の `Samples/BistroExterior` と比べると、Mesh Shader 版では次の要素が増えます。

- runtime meshlet generation
- meshlet vertex buffer
- meshlet-local triangle buffer
- meshlet bounds
- material ごとの meshlet dispatch range
- D3D12 `DispatchMesh`
- Vulkan `vkCmdDrawMeshTasksEXT`
- D3D12 amplification shader
- Vulkan task shader
- frustum culling
- cone backface culling
- `Meshlet Color` debug view
- meshlet dispatch / visible / culled stats

増えたものは多いですが、目的はかなり単純です。

```text
大きいメッシュを小さい meshlet に分ける
  -> meshlet 単位で描画する
    -> 必要なら meshlet 単位で捨てる
```

この 3 段階で見ると、コードを追いやすくなります。

## まず押さえる言葉

### Meshlet

Meshlet は、小さい mesh のまとまりです。このサンプルでは meshoptimizer を使い、Bistro の draw item ごとに meshlet を生成します。

初期値は次の通りです。

| 値 | 意味 |
| --- | --- |
| `MaxMeshletVertices = 64` | 1 meshlet に入れる最大 vertex 数 |
| `MaxMeshletTriangles = 96` | 1 meshlet に入れる最大 triangle 数 |
| `cone_weight = 0.25f` | meshoptimizer が cone culling 向け境界を作るときの重み |

通常の index buffer は mesh 全体の頂点を参照します。一方 meshlet では、meshlet 内の local vertex index と local triangle を使います。このため、シェーダ側で「meshlet の中の何番目の頂点か」を解いて、最終的な vertex を取り出します。

### Mesh Shader

Mesh Shader は、従来の vertex shader / primitive assembly の役割を置き換える shader stage です。

このサンプルでは、mesh shader が meshlet 1 個ぶんの頂点と三角形を出力します。

```text
meshlet id
  -> meshlet record を読む
  -> meshlet-local vertices を読む
  -> global vertex buffer から属性を読む
  -> mesh shader output vertices / triangles を書く
  -> pixel shader で material shading
```

入門者は、まず「mesh shader は小さい mesh を組み立てて出力する」と考えるとよいです。

### Amplification Shader / Task Shader

D3D12 では amplification shader、Vulkan では task shader と呼ばれます。このサンプルでは、どちらも meshlet culling のために使います。

役割は次です。

```text
dispatch range
  -> 複数 meshlet を調べる
  -> 見える meshlet だけを payload に詰める
  -> mesh shader を起動する
```

名前は API ごとに違いますが、サンプル内での役割はほぼ同じです。

## 通常ラスタ版との対応

Mesh Shader 版を読むときは、まず通常の BistroExterior と対応させるのが近道です。

| 通常ラスタ版 | Mesh Shader 版 |
| --- | --- |
| vertex buffer | global vertex buffer + meshlet vertex buffer |
| index buffer | meshlet-local triangle buffer |
| `DrawIndexedInstanced` | `DispatchMesh` / `vkCmdDrawMeshTasksEXT` |
| vertex shader | mesh shader |
| pixel shader | pixel shader |
| draw item | dispatch range |
| material ごとに descriptor を bind | material ごとに descriptor を bind |
| debug view | debug view + `Meshlet Color` |

ここで重要なのは、material shading は大きく変えていないことです。Base Color、Normal、Specular、Emissive を読み、PBR 風に直接光を計算する流れは通常版に寄せています。

つまり Mesh Shader 版の中心は、material を新しくすることではなく、geometry submission を meshlet 駆動に変えることです。

## 共通シーンデータ

Mesh Shader 版では `Common/BistroScene` に meshlet 用のデータが追加されています。

見るべきファイル:

- `../Samples/BistroExteriorMeshShader/Common/BistroScene.h`
- `../Samples/BistroExteriorMeshShader/Common/BistroScene.cpp`

代表的な構造体は次です。

| 構造体 | 役割 |
| --- | --- |
| `MeshletRecord` | 1 meshlet の vertex / triangle 範囲と bounds index |
| `MeshletBounds` | sphere と cone 情報。culling に使う |
| `MeshletDispatchRange` | material index と meshlet の連続範囲 |
| `Scene` | 通常の vertex / index / material / texture に meshlet buffers を足したもの |

読むときは、まず CPU 側で次の配列が作られることを確認します。

- `vertices`
- `indices`
- `materials`
- `meshlets`
- `meshletVertices`
- `meshletTriangles`
- `meshletBounds`
- `meshletDispatchRanges`

通常版では `draws` を回して `DrawIndexed` していました。Mesh Shader 版では `meshletDispatchRanges` を回して mesh dispatch します。

## meshoptimizer の使いどころ

meshoptimizer は、meshlet 生成と bounds 計算に使っています。

主な流れは次です。

```text
Assimp で BistroExterior.fbx を読む
  -> draw item ごとの index 範囲を取り出す
  -> meshopt_buildMeshlets で meshlet を作る
  -> meshopt_optimizeMeshlet で meshlet 内を最適化する
  -> meshopt_computeMeshletBounds で bounds を作る
  -> material ごとに dispatch range をまとめる
```

このサンプルでは offline asset pipeline は作らず、起動時に meshlet を生成します。学習用としては、FBX import から meshlet buffers ができるまでを 1 つのコードで追えるのが利点です。

本格的なアプリケーションでは、meshlet は offline で作ってキャッシュすることも多いです。ただしこのサンプルでは「何が作られているか」を見やすくするため、runtime generation を選んでいます。

## meshlet-local triangle の pack

このサンプルでは、meshlet-local triangle を 1 つの `uint` に pack しています。

```text
bits  0..7  = local vertex index 0
bits  8..15 = local vertex index 1
bits 16..23 = local vertex index 2
```

Meshlet の最大 vertex 数は 64 なので、local vertex index は 8 bit に収まります。これにより、D3D12 と Vulkan の HLSL から同じ形で triangle を読みやすくなります。

入門者向けには、ここは「GPU に渡すデータをシンプルにするための packing」と見るとよいです。圧縮率だけが目的ではなく、API 間で同じ shader layout にする目的もあります。

## 直接光 MS-only 版の読み方

最初に読むべきプロジェクトは次です。

- `../Samples/BistroExteriorMeshShader/D3D12/Source/BistroExteriorMeshShaderD3D12.cpp`
- `../Samples/BistroExteriorMeshShader/Vulkan/Source/BistroExteriorMeshShaderVulkan.cpp`
- `../Samples/BistroExteriorMeshShader/Shaders/BistroExteriorMeshShader.hlsl`

### CPU 側で見ること

D3D12 版:

- Mesh Shader Tier の確認
- mesh shader / pixel shader の compile
- mesh PSO stream の作成
- meshlet buffers の SRV 作成
- material texture descriptors
- `ID3D12GraphicsCommandList6::DispatchMesh`

Vulkan 版:

- Vulkan 1.2 と `VK_EXT_mesh_shader` の確認
- `meshShader` feature の確認
- HLSL から SPIR-V への compile
- mesh shader pipeline の作成
- descriptor set / storage buffer / sampled image
- `vkCmdDrawMeshTasksEXT`

### シェーダ側で見ること

`BistroExteriorMeshShader.hlsl` では、mesh shader が meshlet 1 個を展開します。

読む順番は次がおすすめです。

1. `MeshVertex`、`MeshletRecord`、`MeshletDispatchRange` などの構造体を見る。
2. meshlet id から meshlet record を取得する。
3. meshlet vertex を global vertex index に変換する。
4. global vertex buffer から position / normal / tangent / texcoord を読む。
5. `SetMeshOutputCounts` で出力数を決める。
6. vertices と triangles を出力する。
7. pixel shader で通常版と近い material shading を行う。

ここで一番大事なのは、mesh shader が「頂点を変換するだけ」ではなく、「primitive も出力する」ことです。

## Meshlet Color debug view

![BistroExteriorMeshShader Meshlet Color](images/BistroExteriorMeshShader%20D3D12%202026_05_06%2010_59_22.png)

`Meshlet Color` は、meshlet 単位で deterministic な色を出す debug view です。

目的は次です。

- meshlet が正しく生成されているかを見る。
- mesh shader が meshlet 単位で描けているかを見る。
- culling 版で meshlet が消えすぎていないかを見る。
- D3D12 と Vulkan で同じ meshlet id を見ているか確認する。

Final の絵だけでは、meshlet の境界は見えません。Mesh Shader サンプルでは、meshlet が本当に描画単位になっていることを確認するために、この debug view が重要になります。

## AS/TS culling 版の読み方

次に読むプロジェクトは culling 版です。

- `../Samples/BistroExteriorMeshShader/D3D12Culling/Source/BistroExteriorMeshShaderCullingD3D12.vcxproj`
- `../Samples/BistroExteriorMeshShader/VulkanCulling/Source/BistroExteriorMeshShaderCullingVulkan.vcxproj`
- `../Samples/BistroExteriorMeshShader/Shaders/BistroExteriorMeshShaderCulling.hlsl`

### culling の目的

MS-only 版では、すべての meshlet を mesh shader に渡します。culling 版では、task / amplification shader で meshlet を調べ、見えるものだけを mesh shader に渡します。

このサンプルで行う culling は 2 種類です。

| culling | 内容 |
| --- | --- |
| frustum culling | meshlet bounding sphere が camera frustum の外なら捨てる |
| cone backface culling | meshlet の triangle 群がまとめて裏向きなら捨てる |

occlusion culling は入れていません。まずは frustum と cone backface だけに絞ることで、Mesh Shader 入門として読みやすくしています。

### 処理の流れ

```text
dispatch range
  -> task / amplification shader
    -> meshlet bounds を読む
    -> camera frustum と比較する
    -> cone backface を判定する
    -> visible meshlet id を payload に詰める
  -> mesh shader
    -> visible meshlet だけを展開する
  -> pixel shader
    -> material shading
```

D3D12 と Vulkan で stage 名は違います。

| D3D12 | Vulkan |
| --- | --- |
| amplification shader | task shader |
| mesh shader | mesh shader |
| `DispatchMesh` | `vkCmdDrawMeshTasksEXT` |

ただしサンプルでやっていることは同じです。

## Shadow 版の読み方

Shadow 版は、通常の `BistroExteriorShadow` と Mesh Shader 版を合わせたものです。

見るべきファイル:

- `../Samples/BistroExteriorMeshShader/D3D12Shadow/Source/BistroExteriorMeshShaderShadowD3D12.cpp`
- `../Samples/BistroExteriorMeshShader/VulkanShadow/Source/BistroExteriorMeshShaderShadowVulkan.cpp`
- `../Samples/BistroExteriorMeshShader/Shaders/BistroExteriorMeshShaderShadow.hlsl`

重要なのは、main pass だけでなく shadow pass も Mesh Shader 化していることです。

```text
shadow pass
  -> light view projection
  -> mesh shader で shadow caster を描く
  -> alpha mask を考慮して depth map を作る

main pass
  -> mesh shader で scene を描く
  -> shadow map を sample する
  -> direct light に shadow factor を掛ける
```

Shadow あり MS-only 版では、AS/TS culling は使いません。まずは「shadow pass でも mesh shader を使う」ことを確認する段階です。

## Shadow + Culling 版の読み方

最後に読むのが Shadow + Culling 版です。

- `../Samples/BistroExteriorMeshShader/D3D12ShadowCulling/Source/BistroExteriorMeshShaderShadowCullingD3D12.vcxproj`
- `../Samples/BistroExteriorMeshShader/VulkanShadowCulling/Source/BistroExteriorMeshShaderShadowCullingVulkan.vcxproj`
- `../Samples/BistroExteriorMeshShader/Shaders/BistroExteriorMeshShaderShadowCulling.hlsl`

ここでは culling が 2 つの pass で使われます。

| pass | culling 対象 |
| --- | --- |
| main pass | camera frustum + cone backface |
| shadow pass | light frustum |

shadow pass では camera から見えるかではなく、light から見て shadow map に影響するかが重要です。このため、shadow pass 用の culling は light frustum を使います。

入門者が混乱しやすいのは、「main pass で見えない meshlet でも shadow pass では必要なことがある」点です。影は camera に直接見えていない物体からも落ちるため、main pass と shadow pass の culling 条件を同じにしてはいけません。

## ImGui と Renderer Stats

Mesh Shader 版でも ImGui は重要な観測点です。

主な操作:

- directional light direction
- light color
- light intensity
- camera position
- yaw / pitch
- move speed
- debug view
- normal map Y flip
- shadow parameters
- `Meshlet Color`

主な stats:

- material count
- draw call / dispatch range count
- meshlet dispatch count
- meshlet count
- visible meshlet count
- culled meshlet count
- vertex / index / primitive count
- texture count

culling 版では、カメラを動かしたときに visible / culled meshlet の数が変わることを確認します。ここが変わらない場合、culling が効いていないか、stats の更新が間違っている可能性があります。

## D3D12 と Vulkan の比較

Mesh Shader は D3D12 と Vulkan で名前や feature check が少し違います。

| 観点 | D3D12 | Vulkan |
| --- | --- | --- |
| feature check | `D3D12_FEATURE_D3D12_OPTIONS7.MeshShaderTier` | `VK_EXT_mesh_shader` と feature chain |
| mesh shader | `ms_6_6` | HLSL `ms_6_6` から SPIR-V |
| culling stage | amplification shader `as_6_6` | task shader |
| dispatch | `ID3D12GraphicsCommandList6::DispatchMesh` | `vkCmdDrawMeshTasksEXT` |
| pipeline | mesh PSO stream | graphics pipeline with mesh shader stages |
| input assembler | 使わない | 使わない |

このサンプルでは、D3D12 / Vulkan とも vertex buffer / index buffer を input assembler に bind しません。Mesh Shader が structured buffer から vertex と meshlet data を直接読みます。

ここは通常版との大きな違いです。

## 実装中につまずきやすいところ

### 1. meshlet id と material の対応

Meshlet は material ごとの draw item から作ります。描画時には material を bind し、その material に属する meshlet range を dispatch します。

ここがずれると、形は出るのに texture が違う、alpha が違う、shadow caster が違う、といった症状になります。

### 2. local index と global index

meshlet triangle は local vertex index を持ちます。そこから `meshletVertices` を引き、さらに global vertex index を得ます。

```text
packed local triangle
  -> local vertex index
    -> meshlet vertex buffer
      -> global vertex index
        -> vertex buffer
```

この変換を 1 段飛ばすと、形が壊れます。

### 3. bounds の空間

culling に使う sphere / cone は、meshlet の vertex と同じ world 空間にそろえる必要があります。Bistro は Assimp import 時に transform を適用して vertex を作っているため、bounds もその前提で扱います。

空間がずれると、全部消える、または全部残る、という分かりやすい失敗になります。

### 4. shadow pass の alpha test

Bistro には alpha mask material があります。main pass だけで alpha を扱っても、shadow pass で扱わなければ葉や抜き形状の影がおかしくなります。

Shadow 版では、shadow caster 側でも base color alpha と alpha cutoff を見る必要があります。

### 5. window minimize と swapchain

Vulkan では window minimize 時に swapchain extent が 0 になることがあります。Mesh Shader そのものとは別の話ですが、実装中に落ちやすい箇所です。

この手の問題は、描画技術のバグではなく「window / swapchain lifecycle」のバグとして切り分けるのが大事です。

## 入門者向けの確認順

Mesh Shader 版で表示がおかしいときは、次の順で確認します。

1. MS-only 版で Final が出るか
   まず culling を疑わない状態で確認します。

2. `Meshlet Color` が出るか
   meshlet id と meshlet boundary が正しく見えるか確認します。

3. Base Color / Normal / Specular debug view が通常版と近いか
   material binding と texture binding を確認します。

4. culling 版で culling を一時的に弱める
   全部消える場合は frustum / cone 判定を疑います。

5. stats の visible / culled count が動くか
   カメラ移動に反応しているか確認します。

6. Shadow なしで合ってから Shadow ありを見る
   shadow map と mesh shader の問題を分けます。

7. D3D12 と Vulkan のどちらでも同じ症状かを見る
   両方同じなら共有 HLSL / 共通データ、片方だけなら API 側の binding / pipeline / barrier を疑います。

## おすすめの読み順

このサンプルを読む順番は、実装した順番と同じにするのが一番楽です。

1. `Common/BistroScene`
   meshlet が CPU 側でどう作られるかを見る。

2. `BistroExteriorMeshShader.hlsl`
   MS-only の mesh shader が meshlet をどう展開するかを見る。

3. `D3D12/Source/BistroExteriorMeshShaderD3D12.cpp`
   D3D12 の feature check、PSO、descriptor、`DispatchMesh` を見る。

4. `Vulkan/Source/BistroExteriorMeshShaderVulkan.cpp`
   Vulkan の extension / feature、pipeline、descriptor、`vkCmdDrawMeshTasksEXT` を見る。

5. `BistroExteriorMeshShaderCulling.hlsl`
   AS/TS で visible meshlet を選ぶ流れを見る。

6. `BistroExteriorMeshShaderShadow.hlsl`
   Shadow Map と Mesh Shader の組み合わせを見る。

7. `BistroExteriorMeshShaderShadowCulling.hlsl`
   main pass と shadow pass で culling 条件が変わる点を見る。

この順で読むと、「Mesh Shader」「culling」「shadow」を一度に理解しようとせず、段階ごとに頭の置き場を変えられます。

## 実装してみて分かったこと

Bistro の Mesh Shader 化で大事だったのは、最初から culling や shadow まで一気に作らないことでした。

実装は次の順番で進めました。

```text
1. 直接光 MS-only
2. 直接光 + AS/TS culling
3. Shadow MS-only
4. Shadow + AS/TS culling
```

この順番にしたことで、問題が起きたときに原因を分けられました。

- MS-only で出ないなら meshlet generation / mesh shader / material binding を疑う。
- culling 版だけ出ないなら AS/TS と culling 判定を疑う。
- shadow 版だけおかしいなら shadow map pass / alpha shadow caster / light matrix を疑う。
- Vulkan だけ落ちるなら swapchain、feature chain、descriptor、pipeline barrier を疑う。

特に `Meshlet Color` と stats は、Mesh Shader サンプルの安心材料になりました。Final の絵が同じでも、meshlet 単位で描けているか、culling が動いているかは別問題だからです。

## まとめ

`BistroExteriorMeshShader` は、通常の Bistro raster sample をいきなり別物にしたサンプルではありません。

基本は同じ Bistro scene、同じ material、同じ texture、同じ ImGui debug UI です。そのうえで、geometry submission の単位を draw indexed から meshlet dispatch に変えています。

入門者は、次のように考えると読みやすいです。

```text
BistroExterior
  -> draw item ごとに index buffer で描く

BistroExteriorMeshShader
  -> draw item を meshlet に分ける
  -> meshlet ごとに mesh shader で描く
  -> 必要なら task / amplification shader で見えない meshlet を捨てる
```

Mesh Shader の理解は、API の新しい stage 名を覚えることよりも、「GPU に渡す geometry の単位をどう再設計するか」を見ることが大事です。

このサンプルは、その再設計を Bistro という実シーンで確認するための教材になっています。
