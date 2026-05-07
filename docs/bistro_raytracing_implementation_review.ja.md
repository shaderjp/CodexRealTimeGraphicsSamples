# Bistro Raytracing 実装の振り返り

このドキュメントは、`Samples/BistroExteriorRaytracing` の実装を振り返りながら、DXR と Vulkan Ray Tracing を入門者向けに読むためのメモです。

前提となる記事は次の 3 つです。

- [Bistro 実装前までの振り返り](pre_bistro_implementation_review.ja.md)
- [Bistro 取り組みの振り返り](bistro_implementation_review.ja.md)
- [Bistro Mesh Shader 実装の振り返り](bistro_mesh_shader_implementation_review.ja.md)

この次の発展形として、progressive path tracing と ReSTIR 系の比較を扱う [Bistro Pathtracing 実装の振り返り](bistro_pathtracing_implementation_review.ja.md) も追加しています。

Bistro の通常ラスタ版では、vertex shader / pixel shader と draw call でシーンを描きました。Mesh Shader 版では、geometry submission を meshlet dispatch に置き換えました。Raytracing 版では、さらに一歩進み、シーンの可視性そのものを primary ray で解決します。

このドキュメントでは、「通常の raster pass がない renderer をどう読むか」「DXR と Vulkan Ray Tracing の対応をどう見るか」「direct light、shadow、GI の段階をどう分けて理解するか」を整理します。

## 対象範囲

`Samples/BistroExteriorRaytracing` は 1 つの solution に 6 つのプロジェクトを持ちます。

| 段階 | D3D12 | Vulkan | 役割 |
| --- | --- | --- | --- |
| 1 | `BistroExteriorRaytracingD3D12` | `BistroExteriorRaytracingVulkan` | primary ray + 直接光 |
| 2 | `BistroExteriorRaytracingShadowD3D12` | `BistroExteriorRaytracingShadowVulkan` | primary ray + 直接光 + shadow ray |
| 3 | `BistroExteriorRaytracingGID3D12` | `BistroExteriorRaytracingGIVulkan` | primary ray + shadow ray + 1-bounce diffuse GI + temporal accumulation |

最初に読むなら、まず direct light 版だけで十分です。shadow ray や GI をいったん忘れて、「カメラから primary ray を飛ばし、closest hit で material shading し、UAV に色を書く」流れだけを追います。

現在の Raytracing 版には、miss shader ベースの procedural sky、GI variant の skylight contribution、low-discrepancy sampling、per-frame sample count、radiance clamp、temporal clamp も入っています。ただし、ここでの GI はあくまで 1-bounce diffuse の教材用です。複数 bounce、GGX reflection、emissive / area light sampling、ReSTIR、denoiser まで含むものは `Samples/BistroExteriorPathtracing` 側に分けています。

## Raytracing 版で増えたもの

通常の `Samples/BistroExterior` と比べると、Raytracing 版では次の要素が増えます。

- ray tracing device / feature check
- BLAS
- TLAS
- shader table / shader binding table
- ray generation shader
- miss shader
- closest-hit shader
- any-hit shader
- shadow ray
- bindless texture access
- ray tracing output UAV / storage image
- compute ではない ray tracing dispatch
- output copy to swapchain
- procedural sky / sun disk in miss shader
- diffuse 1-bounce GI
- low-discrepancy GI sampling
- GI samples per frame / radiance clamp / temporal clamp
- temporal accumulation
- accumulation reset
- ray tracing stats

一方で、意図的に入れていないものもあります。

- scene raster pass
- shadow map
- mesh shader pass
- vertex shader fallback
- OMM / SER などの DXR 1.2 専用機能
- spatial denoiser
- acceleration structure compaction / cache

このサンプルの目的は、Raytracing pipeline の基本を Bistro という実シーンで読むことです。そのため、高度な最適化よりも、BLAS/TLAS、SBT、shader、descriptor の対応が見える構成を優先しています。

## まず押さえる言葉

### Primary Ray

Primary ray は、カメラから画面の各 pixel へ飛ばす ray です。

通常ラスタでは、triangle を画面に投影して pixel を作ります。Raytracing 版では逆に、pixel から scene へ ray を飛ばし、最初に当たった triangle を探します。

```text
pixel
  -> camera ray
    -> TLAS
      -> BLAS
        -> triangle hit
          -> material shading
```

この考え方に切り替えると、raster pass がない理由が分かりやすくなります。

### BLAS

BLAS は Bottom Level Acceleration Structure です。実際の triangle geometry を持ちます。

このサンプルでは、Bistro の draw item ごとに 1 つの triangle geometry を作り、それらを 1 つの static BLAS に入れています。

```text
Bistro draw item 0 -> geometry 0
Bistro draw item 1 -> geometry 1
Bistro draw item 2 -> geometry 2
...
```

この構成により、shader 側の `GeometryIndex()` から material / geometry record を直接引けます。

### TLAS

TLAS は Top Level Acceleration Structure です。BLAS の instance を持ちます。

このサンプルでは、Bistro scene は static なので、identity transform の TLAS instance を 1 つだけ作ります。

```text
TLAS
  -> instance 0
    -> BLAS
      -> many geometries
```

動的な object や instance transform は扱っていません。まずは「1 つの scene を ray tracing pipeline へ渡す」ことに集中しています。

### SBT / Shader Table

D3D12 では Shader Table、Vulkan でも一般に Shader Binding Table と呼びます。

Raytracing pipeline では、ray generation、miss、hit group などの shader record を table に並べます。

このサンプルの基本構成は次です。

| Record | 役割 |
| --- | --- |
| RayGen | camera ray を生成し、output に書く |
| Miss | ray が何にも当たらないとき procedural sky を返す |
| ShadowMiss | shadow ray が遮蔽されなかったことを返す |
| HitGroup | closest-hit + any-hit |
| ShadowHitGroup | shadow any-hit |

SBT は入門者が混乱しやすい部分です。まずは「ray が hit / miss したとき、どの shader を呼ぶかを GPU に教える table」と考えるとよいです。

### Any-Hit

Any-hit shader は、ray が候補 triangle に当たったとき、closest hit として採用する前に呼ばれます。

Bistro では alpha masked material があるため、base color texture の alpha を見て、cutoff より小さい場合は `IgnoreHit()` します。

```text
ray hits triangle
  -> any-hit
    -> base color alpha を sample
    -> alpha < cutoff なら IgnoreHit
    -> そうでなければ hit 候補として残す
```

これにより、葉や抜き形状のような alpha mask material が primary ray と shadow ray の両方で正しく扱えます。

## 通常ラスタ版との対応

Raytracing 版を読むときは、通常の BistroExterior と対応させるのが近道です。

| 通常ラスタ版 | Raytracing 版 |
| --- | --- |
| draw item loop | BLAS geometry |
| vertex buffer / index buffer | BLAS build input + shader-readable buffers |
| vertex shader | なし |
| pixel shader | closest-hit shader |
| discard alpha | any-hit `IgnoreHit()` |
| render target | UAV output |
| shadow map | shadow ray |
| draw call stats | BLAS / TLAS / SBT stats |
| material texture descriptors | bindless texture array |

一番大きい違いは、visibility を rasterizer が解かないことです。

通常版:

```text
triangle を画面へ投影する
  -> rasterizer が covered pixel を作る
  -> pixel shader が shading する
```

Raytracing 版:

```text
pixel から ray を飛ばす
  -> acceleration structure が triangle hit を探す
  -> closest-hit shader が shading する
```

この違いを押さえると、コード構造の違いも理解しやすくなります。

## 共通シーンデータ

Raytracing 版では、通常の Bistro scene data に加えて ray tracing 用の record を作ります。

見るべきファイル:

- `../Samples/BistroExteriorRaytracing/Common/BistroScene.h`
- `../Samples/BistroExteriorRaytracing/Common/BistroScene.cpp`
- `../Samples/BistroExteriorRaytracing/Common/BistroTexture.h`
- `../Samples/BistroExteriorRaytracing/Common/BistroTexture.cpp`

代表的な構造体は次です。

| 構造体 | 役割 |
| --- | --- |
| `Vertex` | position / normal / tangent / texcoord |
| `Material` | texture path、base color factor、alpha cutoff |
| `DrawItem` | index offset/count と material index |
| `RtMaterial` | shader で読む material 情報 |
| `RtGeometryRecord` | shader で `GeometryIndex()` から引く geometry 情報 |

Raytracing shader は `GeometryIndex()` で geometry を知り、`RtGeometryRecord` から index offset と material index を取り出します。

```text
GeometryIndex()
  -> RtGeometryRecord
    -> indexOffset
    -> materialIndex
      -> RtMaterial
        -> textureBaseIndex
```

この対応がずれると、形は出ても material が違う、alpha が効かない、shadow が不自然、といった症状になります。

## Direct Light 版の読み方

最初に読むべきプロジェクトは direct light 版です。

- `../Samples/BistroExteriorRaytracing/D3D12/Source/BistroExteriorRaytracingD3D12.cpp`
- `../Samples/BistroExteriorRaytracing/Vulkan/Source/BistroExteriorRaytracingVulkan.cpp`
- `../Samples/BistroExteriorRaytracing/Shaders/BistroExteriorRaytracing.hlsl`
- `../Samples/BistroExteriorRaytracing/Shaders/BistroExteriorRaytracingShared.hlsli`

![BistroExteriorRaytracing D3D12](images/BistroExteriorRaytracing%20D3D12%202026_05_06%2013_26_50.png)

### CPU 側で見ること

D3D12 版:

- DXR support と ray tracing tier の確認
- vertex / index / material / geometry buffer の作成
- texture SRV descriptor heap の作成
- BLAS build
- TLAS build
- global root signature
- DXIL library / state object
- shader table
- `DispatchRays`
- UAV output から swapchain への copy
- ImGui overlay

Vulkan 版:

- Vulkan 1.2 と KHR ray tracing extensions の確認
- acceleration structure / ray tracing pipeline / buffer device address feature の確認
- vertex / index / material / geometry buffer の作成
- descriptor indexing による texture array
- BLAS build
- TLAS build
- ray tracing pipeline
- SBT buffer
- `vkCmdTraceRaysKHR`
- storage image から swapchain image への copy / blit 相当処理
- ImGui overlay

### shader 側で見ること

Direct Light 版では、まず `RayGen` と `ClosestHit` を読むのがよいです。

```text
RayGen
  -> pixel coordinate から camera ray を作る
  -> TraceRay
  -> payload.color を output に書く

ClosestHit
  -> GeometryIndex と PrimitiveIndex を使う
  -> barycentrics で vertex attributes を補間する
  -> texture を sample する
  -> direct lighting を計算する
```

ここで重要なのは、closest-hit shader が pixel shader に近い役割を持つことです。ただし pixel shader と違い、入力は rasterizer が補間した値ではありません。`PrimitiveIndex()` と barycentrics から、自分で position / normal / tangent / texcoord を復元します。

## Camera ray と行列

Raytracing 版では、camera ray の生成が最初の落とし穴になります。

`RayGen` は pixel 位置から NDC を作り、inverse view projection で world 空間の near / far point を復元します。

```text
pixel
  -> uv
    -> ndc
      -> inverseViewProjection
        -> world near / far
          -> ray direction
```

ここで行列の転置や row-major / column-major の扱いがずれると、BLAS/TLAS が正しくても ray が scene に向かわず、空だけが表示されます。

このサンプルでは、既存 Bistro 系と同じく `row_major` の行列をそのまま渡す方針にしています。D3D12 と Vulkan の両方で同じ HLSL を使うため、CPU 側の行列保存と shader 側の `mul` の向きをそろえることが大事です。

## Material shading

Raytracing 版でも、material shading は通常の Bistro に寄せています。

使う texture slot は次です。

| Slot | 内容 |
| --- | --- |
| Base Color | albedo / alpha |
| Normal | tangent-space normal |
| Specular | `R=AO`, `G=Roughness`, `B=Metalness` |
| Emissive | emission |

closest-hit shader では、triangle barycentric から texcoord を補間し、material の texture base index から bindless texture array を参照します。

注意点は AO の扱いです。通常版と同じく、AO は ambient にだけ掛けます。直接光そのものに AO を掛けると、AO が暗い場所で direct light まで消え、シーンが黒いシルエットのようになります。

```text
ambient = baseColor * ao * 0.055
direct  = (diffuse + specular) * radiance * NdotL
color   = ambient + direct + emissive
```

Shadow 版では direct にだけ shadow factor を掛けます。

## Alpha masked material

Bistro には alpha mask を含む material があります。Raytracing 版では any-hit shader で処理します。

Primary ray:

```text
AnyHit
  -> base color alpha を sample
  -> alpha < cutoff なら IgnoreHit
```

Shadow ray:

```text
ShadowAnyHit
  -> base color alpha を sample
  -> alpha < cutoff なら IgnoreHit
```

shadow ray でも alpha test を行うことが重要です。primary ray だけ alpha test しても、shadow ray 側で葉や抜き形状が不透明扱いになれば、影が不自然になります。

この点は raster の shadow map 版でも同じです。main pass と shadow pass の両方で alpha mask を扱う必要があります。

## Shadow 版の読み方

次に読むのは shadow 版です。

- `../Samples/BistroExteriorRaytracing/D3D12Shadow/Source/BistroExteriorRaytracingShadowD3D12.vcxproj`
- `../Samples/BistroExteriorRaytracing/VulkanShadow/Source/BistroExteriorRaytracingShadowVulkan.vcxproj`
- `../Samples/BistroExteriorRaytracing/Shaders/BistroExteriorRaytracingShadow.hlsl`

![BistroExteriorRaytracing Shadow D3D12](images/BistroExteriorRaytracing%20Shadow%20D3D12%202026_05_06%2013_29_01.png)

Shadow 版では shadow map を作りません。primary hit した位置から light 方向へ shadow ray を飛ばします。

```text
primary ray
  -> closest hit
    -> surface position / normal
    -> shadow ray を light 方向へ TraceRay
      -> 何かに当たったら shadow
      -> miss したら lit
    -> direct lighting に shadow factor を掛ける
```

ここで見るべきポイントは、ray type が増えることです。

| Ray | Payload | 役割 |
| --- | --- | --- |
| primary ray | `RayPayload` | 色、距離、debug 情報を返す |
| shadow ray | `ShadowPayload` | 遮蔽されたかだけ返す |

shadow ray は色を計算しません。遮蔽判定だけが目的なので、closest-hit は skip し、any-hit と miss を使って軽くしています。

## Procedural Sky の読み方

Raytracing 版の空は、raster の sky dome や skybox mesh ではありません。primary ray が scene を miss したとき、miss shader で `EvaluateSky(rayDirection)` を呼び、horizon / zenith / ground の gradient と sun disk を procedural に評価します。

```text
primary ray
  -> scene に hit しない
    -> miss shader
      -> ray direction から sky radiance を計算
      -> output に背景として書く
```

sun disk の方向は、既存の directional light とそろえています。そのため ImGui で light direction を変えると、直接光の向きだけでなく空の太陽位置も同じ考え方で動きます。

Direct / Shadow variant では、procedural sky は背景と miss radiance です。surface の直接光モデルには、新しい ambient skylight 項を足していません。GI variant では secondary diffuse ray も同じ miss shader を通るため、scene の外へ抜けた ray が空の radiance を indirect として持ち帰ります。

```text
GI secondary ray
  -> scene を miss
    -> procedural sky
      -> simple skylight contribution
```

ここで大事なのは、空を「描画 pass」として足していないことです。ray が当たらなかったときの戻り値を豊かにしただけなので、Raytracing 版の「scene raster pass なし」という設計は変わりません。

## GI 版の読み方

最後に読むのが GI 版です。

- `../Samples/BistroExteriorRaytracing/D3D12GI/Source/BistroExteriorRaytracingGID3D12.vcxproj`
- `../Samples/BistroExteriorRaytracing/VulkanGI/Source/BistroExteriorRaytracingGIVulkan.vcxproj`
- `../Samples/BistroExteriorRaytracing/Shaders/BistroExteriorRaytracingGI.hlsl`
- `../Samples/BistroExteriorRaytracing/Shaders/BistroExteriorRaytracingShared.hlsli`

GI 版では、primary hit から diffuse 方向へ secondary ray を飛ばします。初期実装は 1 frame に 1 本でしたが、現在は `GI Samples / Frame` で 1..4 本を平均できます。

```text
primary ray
  -> closest hit
    -> direct lighting + shadow
    -> low-discrepancy sequence で diffuse 方向を選ぶ
    -> secondary ray を N 本 trace
      -> hit した先の lighting、または miss した sky を読む
    -> N 本の平均を indirect として足す
```

このサンプルは path tracer 全体を作るものではありません。目的は、diffuse 1-bounce を temporal accumulation で少しずつ収束させることです。specular bounce、emissive light sampling、ReSTIR、denoiser まで含めて読む場合は、Pathtracing 版の担当範囲です。

![BistroExteriorRaytracing GI D3D12 Final](images/BistroExteriorRaytracing%20GI%20D3D12%202026_05_06%2013_30_29.png)

### Temporal accumulation

GI は 1 frame の sample 数が少ないとノイズが多いです。そのため、前フレームまでの結果を accumulation texture に保持し、新しい sample と混ぜます。

```text
frame 0 -> sample 0
frame 1 -> sample 0 と sample 1 を平均
frame 2 -> sample 0..2 を平均
...
```

ただし camera や light が動いたら、過去の sample は使えません。そのため、次の変更で accumulation を reset します。

- camera position
- yaw / pitch
- light direction
- light intensity
- debug view
- sky settings
- GI strength
- GI samples per frame
- GI radiance clamp
- GI temporal clamp
- max accumulated samples
- resolution
- explicit reset button

入門者向けには、「動いたら過去の絵は別条件の結果なので捨てる」と考えると分かりやすいです。

### GI noise reduction

GI variant は、追加 compute denoiser なしで効く軽量なノイズ対策を入れています。

| 項目 | 役割 |
| --- | --- |
| Low-discrepancy sampling | frame が進むごとに diffuse hemisphere を偏りにくく覆う |
| Cranley-Patterson rotation | pixel ごとの規則的な縞を避ける |
| `GI Samples / Frame` | 1 frame 内で複数 secondary ray を平均する |
| `GI Radiance Clamp` | 明るすぎる外れ値を accumulation 前に抑える |
| `GI Temporal Clamp` | 新しい frame が history から離れすぎるのを抑える |

これは本格的な spatial denoiser ではありません。Raytracing 版では「1-bounce GI を教材として扱える程度に安定させる」ことを狙い、filter pass や追加 G-buffer は入れていません。Pathtracing 版では、この次の段階として normal / depth / albedo AOV を使う軽量 denoiser を別途入れています。

## Debug View の読み方

Raytracing 版では、ImGui の `Debug View` が原因調査の中心になります。

| Debug View | 何を見るか |
| --- | --- |
| Final | 最終結果 |
| Base Color | material / texture binding |
| World Normal | barycentric 補間と normal map decode |
| Normal Texture | normal texture raw |
| Hit Distance | primary ray が当たった距離 |
| Direct | direct lighting 成分 |
| Shadow | shadow ray の遮蔽結果 |
| Indirect | GI の indirect 成分 |
| Accumulation Samples | accumulation の進み具合 |
| Sky | primary ray direction に対する procedural sky |

Hit Distance は、ray が正しく scene に当たっているかを見るのに便利です。

![BistroExteriorRaytracing GI D3D12 Hit Distance](images/BistroExteriorRaytracing%20GI%20D3D12%202026_05_06%2013_30_39.png)

Shadow debug は、shadow ray の結果だけを確認するために使います。

![BistroExteriorRaytracing GI D3D12 Shadow](images/BistroExteriorRaytracing%20GI%20D3D12%202026_05_06%2013_30_50.png)

Indirect debug は、GI の secondary ray contribution が入っているかを見るために使います。

![BistroExteriorRaytracing GI D3D12 Indirect](images/BistroExteriorRaytracing%20GI%20D3D12%202026_05_06%2013_30_55.png)

Final だけを見ても、primary ray、material、shadow、GI のどこがおかしいかは分かりません。Raytracing 版では、debug view を段階的に切り替えることが特に重要です。

## ImGui と Renderer Stats

Raytracing 版でも ImGui は操作 UI であり、同時に検証用の計器です。

主な操作:

- directional light direction
- light color
- light intensity
- camera position
- yaw / pitch
- move speed
- debug view
- normal map Y flip
- ray bias / TMin
- sky color
- sky horizon / zenith / ground color
- sky intensity
- sun intensity / sun size
- ray traced shadow toggle
- GI strength
- GI samples per frame
- GI radiance clamp
- GI temporal clamp
- max accumulated samples
- freeze accumulation
- reset accumulation

主な stats:

- API
- FPS / frame time
- DXR tier
- Vulkan ray tracing limits
- material count
- texture count
- vertex / index / primitive count
- BLAS geometry count
- TLAS instance count
- SBT record count
- output resolution
- accumulated samples

Raytracing では、draw call 数よりも BLAS / TLAS / SBT / output resolution の方が重要な観測点になります。

## D3D12 と Vulkan の比較

Raytracing は D3D12 と Vulkan で API 名が大きく違いますが、役割で見ると対応が見えます。

| 役割 | D3D12 | Vulkan |
| --- | --- | --- |
| feature check | `D3D12_FEATURE_D3D12_OPTIONS5.RaytracingTier` | KHR extensions + feature chain |
| BLAS / TLAS | DXR acceleration structure | `VK_KHR_acceleration_structure` |
| GPU address | GPU virtual address | buffer device address |
| ray tracing pipeline | state object | `VkPipeline` |
| shader library | DXIL library | SPIR-V shader modules |
| shader table | shader table buffer | SBT buffer |
| dispatch | `DispatchRays` | `vkCmdTraceRaysKHR` |
| output | UAV texture | storage image |
| bindless texture | descriptor table | descriptor indexing |

入門者は、API 名を丸暗記するより、次の 5 つを対応させると読みやすくなります。

1. acceleration structure を作る。
2. shader を pipeline に入れる。
3. shader table / SBT を作る。
4. descriptor で scene buffer と texture を渡す。
5. trace command を積む。

この 5 点は D3D12 と Vulkan の両方にあります。

## 実装中につまずきやすいところ

### 1. Ray が全部 miss する

BLAS/TLAS と stats が正しく見えるのに空だけが出る場合、camera ray を疑います。

特に見るところ:

- inverse view projection
- row-major / column-major
- `mul` の向き
- near / far point
- camera position
- ray TMin / TMax

行列が 1 つ転置されているだけで、ray は scene ではなく別方向へ飛びます。

### 2. Geometry は出るが shading が黒い

形が出ているなら primary ray と AS は概ね動いています。次に疑うのは material shading です。

見るところ:

- base color texture
- specular packed texture
- AO の掛け方
- normal map
- light direction
- NdotL
- tonemap

AO を direct light に掛けると、Bistro では黒く沈みすぎることがあります。通常版と同じ式にそろえるのが大事です。

### 3. Alpha material が板になる

葉や抜き形状が不透明な板のように見える場合、any-hit の alpha test を疑います。

見るところ:

- `alphaMasked`
- `alphaCutoff`
- base color alpha
- `IgnoreHit()`
- primary ray と shadow ray の両方で any-hit が有効か

### 4. Shadow が濃すぎる、または全部影になる

shadow ray の payload 初期値と miss shader を確認します。

見るところ:

- shadow ray direction
- ray bias / TMin
- TMax
- shadow miss shader
- shadow any-hit alpha test
- self intersection

ray bias が小さすぎると self shadow が出ます。大きすぎると接地影が浮きます。

### 5. GI が溜まらない

accumulation が増えない場合は、reset 条件を確認します。

見るところ:

- camera/light/debug state の比較
- `m_resetAccumulationRequested`
- freeze accumulation
- frame index / sample index
- output resolution change

毎フレーム何かが変化している扱いになると、accumulation は常に 0 に戻ります。

### 6. Vulkan だけ落ちる

Vulkan Ray Tracing では、extension / feature / function pointer / buffer device address / SBT alignment が重要です。

見るところ:

- required extension list
- feature chain
- `vkGetDeviceProcAddr`
- acceleration structure build scratch buffer
- SBT stride alignment
- descriptor indexing
- swapchain minimize / out-of-date handling

Raytracing shader が正しくても、SBT alignment がずれると実行時に壊れます。

## 入門者向けの確認順

Raytracing 版で表示がおかしいときは、次の順で確認します。

1. Renderer Stats を見る
   vertex / index / primitive / BLAS geometry / TLAS instance / SBT record が 0 でないか確認します。

2. Hit Distance debug view を見る
   primary ray が scene に当たっているか確認します。

3. Base Color を見る
   material と texture binding が合っているか確認します。

4. World Normal を見る
   barycentric 補間、normal、tangent、normal map decode を確認します。

5. Direct を見る
   lighting 計算だけを確認します。

6. Shadow を見る
   shadow ray の遮蔽結果を確認します。

7. Indirect を見る
   GI ray の寄与があるか確認します。

8. D3D12 と Vulkan の症状を比べる
   両方同じなら shared HLSL / scene data、片方だけなら API binding / AS / SBT / barrier を疑います。

## おすすめの読み順

このサンプルを読む順番は、実装した段階と同じにするのが一番楽です。

1. `Common/BistroScene`
   raster 版と同じ Bistro scene から ray tracing 用 record がどう作られるかを見る。

2. `Common/BistroTexture`
   texture format、fallback、mip 情報を見る。

3. `BistroExteriorRaytracingShared.hlsli`
   shader の構造体、payload、surface reconstruction、material shading、procedural sky helper を見る。

4. `BistroExteriorRaytracing.hlsl`
   direct light 版の entry point と define の少なさを見る。

5. `D3D12/Source/BistroExteriorRaytracingD3D12.cpp`
   DXR device、AS、state object、shader table、DispatchRays を見る。

6. `Vulkan/Source/BistroExteriorRaytracingVulkan.cpp`
   KHR extensions、AS、ray tracing pipeline、SBT、vkCmdTraceRaysKHR を見る。

7. `BistroExteriorRaytracingShadow.hlsl`
   shadow ray が増えたときの payload と miss / any-hit を見る。

8. `BistroExteriorRaytracingGI.hlsl`
   GI、low-discrepancy sampling、radiance / temporal clamp、temporal accumulation の define を見る。

9. GI プロジェクトの CPU 側
   accumulation texture、reset 条件、ImGui controls を見る。

この順番なら、Raytracing pipeline の新しい概念を一気に全部抱え込まずに済みます。

## 実装してみて分かったこと

Bistro の Raytracing 化で大事だったのは、通常版や Mesh Shader 版のように段階を分けることでした。

```text
1. Primary ray + direct light
2. Shadow ray
3. Procedural sky
4. 1-bounce GI + accumulation
5. GI noise reduction controls
```

この順番にしたことで、問題の切り分けがしやすくなりました。

- direct light 版で出ないなら camera ray / AS / SBT / material を疑う。
- shadow 版だけおかしいなら shadow payload / any-hit / bias を疑う。
- GI 版だけおかしいなら secondary ray / random seed / accumulation reset を疑う。
- D3D12 と Vulkan で同じなら shared HLSL を疑う。
- 片方だけなら API 側の descriptor / pipeline / SBT / barrier を疑う。

Raytracing は raster よりも「何も出ない」失敗が起きやすいです。そのため、Hit Distance、Base Color、World Normal、Direct、Shadow、Indirect の debug view を早めに入れたことが大きな助けになりました。

もう 1 つ大事だったのは、Raytracing 版と Pathtracing 版の境界を分けたことです。Raytracing 版は「DXR / Vulkan Ray Tracing の基本 pipeline を Bistro で読む」教材にし、複数 bounce、reflection、light list、ReSTIR、denoiser は Pathtracing 版へ進めました。この分け方により、入門者が最初から path tracer 全体を抱え込まずに済みます。

## まとめ

`BistroExteriorRaytracing` は、Bistro を別の見た目にするためだけのサンプルではありません。

同じ scene、同じ material、同じ texture を使いながら、visibility を rasterizer ではなく ray tracing pipeline で解くサンプルです。

入門者は、次のように考えると読みやすいです。

```text
BistroExterior
  -> triangle を画面に投影して pixel shader で塗る

BistroExteriorRaytracing
  -> pixel から ray を飛ばして triangle を探す
  -> closest-hit shader で pixel shader 相当の shading をする
  -> shadow や GI は追加の ray として表現する

BistroExteriorPathtracing
  -> 同じ AS と material を使い、raygen 内の path loop で複数 bounce へ広げる
  -> ReSTIR や denoiser は比較用プロジェクトとして足す
```

Raytracing の理解は、DXR や Vulkan の API 名を覚えることよりも、「ray がどの shader を呼び、どの payload を持って戻るか」を追うことが大事です。

このサンプルは、その流れを Bistro という実シーンで確認するための教材になっています。その先の progressive path tracing まで読みたい場合は、[Bistro Pathtracing 実装の振り返り](bistro_pathtracing_implementation_review.ja.md) へ進むと自然です。
