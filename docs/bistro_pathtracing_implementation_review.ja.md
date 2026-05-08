# Bistro Pathtracing 実装の振り返り

このドキュメントは、`Samples/BistroExteriorPathtracing` の実装を振り返りながら、Bistro Exterior を progressive path tracer として読むための入門メモです。

前提となる記事は次の 5 つです。

- [Bistro 実装前までの振り返り](pre_bistro_implementation_review.ja.md)
- [Bistro 取り組みの振り返り](bistro_implementation_review.ja.md)
- [Bistro Clustered Forward 実装の振り返り](bistro_clustered_forward_implementation_review.ja.md)
- [Bistro Mesh Shader 実装の振り返り](bistro_mesh_shader_implementation_review.ja.md)
- [Bistro Raytracing 実装の振り返り](bistro_raytracing_implementation_review.ja.md)

Raytracing 版では、primary ray、shadow ray、1-bounce diffuse GI を段階的に入れました。Pathtracing 版では、その延長として raygen 内に path loop を持ち、diffuse / specular bounce、sun / sky / area light の next-event estimation、emissive light list、ReSTIR GI / DI、軽量 denoiser までを比較用プロジェクトとして広げています。

ただし、このサンプルも production path tracer そのものではありません。目的は、Bistro という実シーンで「path tracing の基本構造」と「D3D12 / Vulkan で同じ概念をどう並べるか」を読むことです。

## 対象範囲

`Samples/BistroExteriorPathtracing` は 1 つの solution に 6 つのプロジェクトを持ちます。

| 段階 | D3D12 | Vulkan | 役割 |
| --- | --- | --- | --- |
| 1 | `BistroExteriorPathtracingD3D12` | `BistroExteriorPathtracingVulkan` | baseline progressive path tracing |
| 2 | `BistroExteriorPathtracingReSTIRD3D12` | `BistroExteriorPathtracingReSTIRVulkan` | ReSTIR GI 比較 |
| 3 | `BistroExteriorPathtracingReSTIRDID3D12` | `BistroExteriorPathtracingReSTIRDIVulkan` | ReSTIR DI 比較 |

最初に読むなら、ReSTIR 版は後回しで構いません。まず通常の `BistroExteriorPathtracingD3D12` または `BistroExteriorPathtracingVulkan` で、「primary ray で最初の hit を取り、raygen の loop で次の ray を選び、accumulation に混ぜる」流れだけを追います。

## Raytracing GI 版との違い

Raytracing GI 版と Pathtracing 版は、どちらも DXR / Vulkan Ray Tracing を使います。違いは、ray の扱い方です。

| 観点 | Raytracing GI 版 | Pathtracing 版 |
| --- | --- | --- |
| 主目的 | ray tracing pipeline の基本を読む | progressive path tracing と reuse を読む |
| indirect | diffuse 1-bounce | diffuse / specular の path loop |
| reflection | なし | stochastic GGX specular bounce |
| light sampling | directional light + sky miss | sun、sky、emissive triangle、procedural area light |
| ReSTIR | なし | GI 版と DI 版を別 project で比較 |
| denoiser | なし | normal / depth / albedo AOV を使う軽量 compute denoiser |
| shader 構成 | stage ごとに direct/shadow/GI shader | shared hlsli + variant define |

Raytracing GI 版は、「secondary ray が 1 回だけ増える」ことを確認する教材です。Pathtracing 版は、「ray が戻ってきたら次の ray を選び、throughput を更新し、複数 frame で収束させる」教材です。

```text
Raytracing GI
  primary ray
    -> secondary diffuse ray
      -> indirect

Pathtracing
  primary ray
    -> bounce 0 shading
    -> next ray
      -> bounce 1 shading
      -> next ray
        -> ...
```

この違いを最初に押さえると、同じ BLAS / TLAS / SBT を使っていても、shader の読み方が変わる理由が見えてきます。

## Pathtracing 版で増えたもの

Raytracing GI 版から Pathtracing 版へ進むと、主に次が増えます。

- iterative path loop in raygen
- path throughput
- max / min bounce control
- stochastic diffuse bounce
- stochastic GGX specular bounce
- Russian roulette
- sun next-event estimation
- sky next-event estimation
- emissive triangle light list
- procedural rectangular area light
- optional environment map
- ReSTIR reservoir buffer
- temporal reuse compute pass
- spatial reuse compute pass
- ReSTIR GI / DI debug views
- normal / depth / albedo AOV
- lightweight cross-bilateral denoiser

増えたものは多いですが、読む順番はかなり素直です。

```text
scene data
  -> acceleration structure
    -> path tracing shader
      -> accumulation
        -> ReSTIR reuse
          -> denoiser
```

最初から ReSTIR と denoiser まで同時に読むと混乱しやすいので、baseline path tracing、ReSTIR、denoiser の 3 つに分けて見るのがおすすめです。

## まず押さえる言葉

### Path

Path は、camera から始まる ray の連なりです。

```text
camera
  -> surface A
    -> surface B
      -> sky
```

各 surface で material を評価し、次の方向を確率的に選びます。このサンプルでは、diffuse 方向と GGX specular 方向を選びます。

### Throughput

Throughput は、path が進むにつれて残っている光の重みです。

```text
radiance += throughput * emitted_or_direct_light
throughput *= bsdfWeight
```

入門者向けには、「ここまでの bounce でどれだけ光が残っているか」と考えると分かりやすいです。throughput が大きくなりすぎると firefly の原因になるため、このサンプルでは clamp も入れています。

### Next-Event Estimation

Next-event estimation は、surface hit した時点で light 方向を直接 sample する考え方です。

```text
surface hit
  -> sun direction へ visibility ray
  -> sky 方向へ visibility ray
  -> area light へ visibility ray
  -> 直接寄与として足す
```

path が偶然 light に当たるのを待つだけだと、収束が遅くなります。そこで surface ごとに light を明示的に sample します。このサンプルでは sun、sky、emissive / procedural area light を対象にしています。

### Reservoir

Reservoir は、複数 candidate から代表 sample と重みを保持する小さな record です。

```text
candidate samples
  -> reservoir
    -> temporal reuse
      -> spatial reuse
        -> resolved contribution
```

ReSTIR 版では、per-pixel の current / history / spatial reservoir buffer を使います。D3D12 では UAV buffer、Vulkan では storage buffer として同じ概念を並べています。

## 共通シーンデータ

見るべきファイル:

- `../Samples/BistroExteriorPathtracing/Common/BistroScene.h`
- `../Samples/BistroExteriorPathtracing/Common/BistroScene.cpp`
- `../Samples/BistroExteriorPathtracing/Common/BistroTexture.h`
- `../Samples/BistroExteriorPathtracing/Common/BistroTexture.cpp`
- `../Samples/BistroExteriorPathtracing/Common/BistroCamera.h`

Pathtracing 版も、Bistro の Assimp import、material / texture loading、BLAS / TLAS の基本は Raytracing 版を引き継いでいます。大きく増えたのは light list と environment map の扱いです。

代表的な構造体は次です。

| 構造体 | 役割 |
| --- | --- |
| `RtMaterial` | base color、normal、roughness、metallic、emissive、alpha 情報 |
| `RtGeometryRecord` | `GeometryIndex()` から index range と material を引く |
| `RtLight` | emissive triangle / procedural rect light を shader へ渡す |
| `LightBuildResult` | light list、emissive triangle 数、procedural area light 数 |

`BuildLightList` は、emissive material を持つ triangle を light candidate として集めます。数が多すぎると重くなるため、上限で間引きます。そのうえで、Bistro の bounds を基準に procedural rectangular area light も追加します。

```text
materials / draws
  -> emissive triangle candidates
  -> capped emissive lights
  -> procedural rect lights
  -> RtLight buffer
```

この light list は、通常 Pathtracing 版、ReSTIR GI 版、ReSTIR DI 版のすべてで使います。

## Baseline Pathtracing の読み方

見るべきファイル:

- `../Samples/BistroExteriorPathtracing/D3D12/Source/BistroExteriorPathtracingD3D12.cpp`
- `../Samples/BistroExteriorPathtracing/Vulkan/Source/BistroExteriorPathtracingVulkan.cpp`
- `../Samples/BistroExteriorPathtracing/Shaders/BistroExteriorPathtracing.hlsl`
- `../Samples/BistroExteriorPathtracing/Shaders/BistroExteriorPathtracingShared.hlsli`

通常版の entry point は小さく、`BistroExteriorPathtracing.hlsl` は variant define を置いて shared hlsli を include するだけです。実質的な path tracing の中心は `BistroExteriorPathtracingShared.hlsli` にあります。

![BistroExteriorPathtracing D3D12 denoiser on](<images/BistroExteriorPathtracing D3D12 2026_05_08 0_37_03.png>)

### RayGen の流れ

RayGen は pixel ごとに path sample を作り、accumulation texture に混ぜます。

```text
RayGen
  -> sample index を作る
  -> TracePathSample を N 回呼ぶ
  -> frame color を平均
  -> temporal accumulation
  -> debug view を選ぶ
  -> output / AOV に書く
```

ここで重要なのは、ray pipeline recursion depth を深くしないことです。closest-hit shader の中から次の ray を再帰的に呼ぶのではなく、raygen 側の loop で `TraceRay` を繰り返します。

```text
for bounce in maxBounces:
  TraceRay
  if miss:
    add sky
    break
  evaluate material
  add NEE
  choose next direction
  update throughput
```

この形にすると、D3D12 / Vulkan の ray pipeline recursion を抑えたまま path depth を UI で変えられます。

### Surface reconstruction

Closest-hit では、Raytracing 版と同じように barycentric から position、normal、tangent、UV を復元します。shader は `GeometryIndex()` から `RtGeometryRecord` を引き、material index と index offset を得ます。

```text
GeometryIndex()
  -> RtGeometryRecord
    -> indices
    -> vertices
    -> RtMaterial
      -> textureBaseIndex
```

この部分は Raytracing 版と共通の考え方です。Pathtracing 版で表示がおかしいときも、まず Base Color、World Normal、Hit Distance を見るのが近道です。

### Diffuse と Specular

material は、base color、normal、roughness、metallic、emissive、alpha を使います。roughness は低すぎる値を避けるため clamp し、specular bounce は GGX 方向を確率的に選びます。

```text
surface
  -> diffuse bounce candidate
  -> GGX specular bounce candidate
  -> material に応じてどちらかを選ぶ
  -> throughput 更新
```

このため、Pathtracing 版では金属や低 roughness の部分に reflection 的な寄与が出ます。専用の screen-space reflection pass や reflection map を足しているわけではなく、同じ TLAS と procedural sky / environment map へ次の ray を飛ばしています。

## Light Sampling

Pathtracing 版では、path が偶然 light に当たるのを待つだけではなく、surface hit ごとに direct contribution を取りに行きます。

見るべき shader helper:

- `EvaluateSunNEE`
- `EvaluateSkyNEE`
- `EvaluateAreaLightNEE`
- `TraceVisibilityRay`

### Sun

Sun は既存の directional light と同じ方向を使います。surface から light direction へ visibility ray を飛ばし、alpha masked material を any-hit で処理します。

```text
surface
  -> light direction
    -> visibility ray
      -> blocked なら 0
      -> miss なら direct sun
```

### Sky

Sky は procedural gradient sky と optional environment map の両方を扱います。environment map が見つからない場合は procedural sky fallback のまま動作します。

`BISTRO_ENVIRONMENT_MAP` を指定するか、asset root / `Textures` / `Environment` / `Environments` に environment 系のファイルを置くと、2D environment map を読み込みます。

```text
ray miss
  -> environment map enabled?
    -> equirectangular sample
    -> otherwise procedural sky
```

### Emissive / Area Light

Bistro の emissive material は triangle light list に変換します。さらに、窓・看板・室内灯のような確認用として procedural rectangular area light を追加しています。

```text
RtLight
  type 0 -> emissive triangle light
  type 1 -> procedural rectangular area light
```

procedural light は analytical light なので、表示用 mesh geometry は増やしていません。見た目に点や矩形が直接描かれるわけではなく、lighting sample としてだけ存在します。

## ReSTIR GI の読み方

見るべきファイル:

- `../Samples/BistroExteriorPathtracing/D3D12ReSTIR/Source/BistroExteriorPathtracingReSTIRD3D12.vcxproj`
- `../Samples/BistroExteriorPathtracing/VulkanReSTIR/Source/BistroExteriorPathtracingReSTIRVulkan.vcxproj`
- `../Samples/BistroExteriorPathtracing/Shaders/BistroExteriorPathtracingReSTIR.hlsl`
- `../Samples/BistroExteriorPathtracing/Shaders/BistroExteriorPathtracingReSTIRResolve.hlsl`

ReSTIR GI 版は、indirect contribution を reservoir に入れ、temporal / spatial reuse で近傍や前フレームの candidate を混ぜます。

```text
raygen
  -> current reservoir を作る
  -> output は通常通り accumulation

compute
  -> history reservoir を混ぜる
  -> neighbor reservoir を混ぜる
  -> spatial reservoir を history へコピー
```

このサンプルの ReSTIR GI は、production 向けの完全な validation、disocclusion test、visibility recheck をすべて持つものではありません。目的は、reservoir buffer、temporal reuse、spatial reuse、debug view、D3D12 / Vulkan の barrier / descriptor 対応を見える形にすることです。

### Reservoir debug

ReSTIR GI / DI 版では、通常の debug view に加えて次が使えます。

| Debug View | 何を見るか |
| --- | --- |
| Reservoir Weight | reservoir の重み |
| Temporal Reuse | history が混ざっているか |
| Spatial Reuse | neighbor が混ざっているか |

Final だけを見ると、ReSTIR が効いているのか、ただ暗くなったのかが分かりにくいです。Reservoir Weight と Temporal / Spatial Reuse を見ながら確認するのが大事です。

## ReSTIR DI の読み方

見るべきファイル:

- `../Samples/BistroExteriorPathtracing/D3D12ReSTIRDI/Source/BistroExteriorPathtracingReSTIRDID3D12.vcxproj`
- `../Samples/BistroExteriorPathtracing/VulkanReSTIRDI/Source/BistroExteriorPathtracingReSTIRDIVulkan.vcxproj`
- `../Samples/BistroExteriorPathtracing/Shaders/BistroExteriorPathtracingReSTIRDI.hlsl`
- `../Samples/BistroExteriorPathtracing/Shaders/BistroExteriorPathtracingReSTIRResolve.hlsl`

ReSTIR DI 版は、primary-hit direct lighting を reservoir 化します。対象は sun と local light list です。

![BistroExteriorPathtracing ReSTIR DI D3D12](<images/BistroExteriorPathtracing ReSTIR DI D3D12 2026_05_08 0_37_45.png>)

ReSTIR DI は、light candidate が少ないと効果が見えにくいです。そのため、このサンプルでは emissive triangle light list と procedural area light を先に入れています。sun だけなら、普通に 1 本 visibility ray を飛ばしても十分なので、ReSTIR DI の教材としては light list が重要です。

```text
primary surface
  -> sun candidate
  -> emissive / area light candidate
  -> direct reservoir
  -> temporal / spatial reuse
```

ReSTIR GI と ReSTIR DI は、同じ reservoir buffer と resolve compute shader の考え方を共有しています。違うのは、reservoir に入れる candidate が indirect か direct かです。

## Denoiser の読み方

見るべきファイル:

- `../Samples/BistroExteriorPathtracing/Shaders/BistroExteriorPathtracingDenoise.hlsl`
- `../Samples/BistroExteriorPathtracing/D3D12/Source/BistroExteriorPathtracingD3D12.cpp`
- `../Samples/BistroExteriorPathtracing/Vulkan/Source/BistroExteriorPathtracingVulkan.cpp`

Pathtracing 版には、追加 ThirdParty なしの軽量 denoiser を入れています。RayGen が color だけでなく、denoiser 用 AOV も出力します。

| AOV | 中身 |
| --- | --- |
| `g_denoiseAov0` | world normal + hit distance |
| `g_denoiseAov1` | base color + roughness |

compute denoiser は、accumulation 済みの color を読み、normal、depth、luminance、albedo の差を見ながら multi-scale cross-bilateral filter をかけます。

```text
path tracing output
  -> accumulation output
  -> denoise AOV
  -> denoise compute
  -> swapchain copy
```

![BistroExteriorPathtracing D3D12 denoiser off](<images/BistroExteriorPathtracing D3D12 2026_05_08 0_36_56.png>)

適用対象は `Final` debug view だけです。Base Color、World Normal、Reservoir Weight などの debug view に filter をかけると原因調査がしにくくなるため、debug 表示は未フィルタのままにしています。

ImGui では次を調整できます。

- Denoiser Enabled
- Spatial Iterations
- Normal Sigma
- Depth Sigma
- Luminance Sigma
- Albedo Sigma
- Denoiser Strength

この denoiser は、OIDN や NRD のような本格 denoiser ではありません。サンプル内で完結する、初期ノイズを少し落ち着かせるための教材用 compute pass です。

## ImGui と Renderer Stats

Pathtracing 版の ImGui は、Raytracing 版より操作項目が増えています。

主な操作:

- directional light direction / color / intensity
- editable camera position / yaw / pitch
- sky horizon / zenith / ground color
- environment map enable / intensity / rotation
- sun NEE
- ray bias / TMin
- samples per frame
- radiance clamp
- temporal clamp
- max / min path bounces
- emissive light enable / intensity
- procedural light enable / intensity
- denoiser controls
- ReSTIR temporal / spatial reuse controls
- reset accumulation / reset reservoirs

主な stats:

- API
- DXR tier / Vulkan ray tracing limits
- materials / textures
- vertices / indices / primitives
- BLAS geometries / TLAS instances
- SBT records
- light list count
- emissive triangle lights
- procedural area lights
- output resolution
- accumulated samples
- denoiser state

Pathtracing 版では、FPS だけを見ると判断を誤ります。samples per frame、max bounces、ReSTIR reuse、denoiser の有無でコストが変わるため、Renderer Stats と debug view を合わせて見るのが大事です。

## D3D12 と Vulkan の比較

Pathtracing 版でも、概念の対応は Raytracing 版と同じです。

| 役割 | D3D12 | Vulkan |
| --- | --- | --- |
| ray tracing pipeline | DXR state object | `VkPipeline` |
| shader table | shader table buffer | SBT buffer |
| trace command | `DispatchRays` | `vkCmdTraceRaysKHR` |
| output | UAV texture | storage image |
| reservoir | UAV structured buffer | storage buffer |
| denoise AOV | UAV texture | storage image |
| reuse / denoise pass | compute PSO | compute pipeline |
| resource sync | resource barrier / UAV barrier | image layout / buffer memory barrier |

入門者向けには、API 名よりも「誰が書き、誰が読むか」を追う方が楽です。

```text
RayGen writes:
  output
  accumulation
  current reservoir
  denoise AOV

Compute writes:
  spatial reservoir
  history reservoir
  denoised output

Copy / present reads:
  output or denoised output
```

D3D12 と Vulkan の片方だけ壊れるときは、descriptor index、barrier、SBT stride、image layout、storage image format を疑います。両方で同じ壊れ方なら、shared HLSL、scene data、random sampling、material decode を疑います。

## 入門者向けの確認順

Pathtracing 版で表示がおかしいときは、次の順で確認します。

1. Renderer Stats を見る
   vertices、indices、BLAS geometries、TLAS instances、SBT records、light list が 0 でないか確認します。

2. Hit Distance を見る
   primary ray が scene に当たっているか確認します。

3. Base Color を見る
   material と texture binding を確認します。

4. World Normal を見る
   normal map decode と tangent basis を確認します。

5. Direct NEE を見る
   sun / area light の visibility ray と light list を確認します。

6. Indirect を見る
   secondary bounce と sky / environment miss を確認します。

7. Bounce Count を見る
   max bounces、Russian roulette、throughput が極端に止まっていないか確認します。

8. Reservoir debug を見る
   ReSTIR 版だけおかしい場合、Reservoir Weight、Temporal Reuse、Spatial Reuse を確認します。

9. Denoiser を off にする
   filter が原因か、path tracing output 自体が原因かを分けます。

この順番なら、Final の見た目だけで勘に頼らず、どの段階でおかしくなっているかを切り分けられます。

## おすすめの読み順

このサンプルを読む順番は、実装した段階に合わせると理解しやすいです。

1. `Common/BistroScene`
   material、geometry record、light list、environment map discovery を見る。

2. `Common/BistroTexture`
   DDS/TGA に加えて environment map texture の読み込みを見る。

3. `BistroExteriorPathtracingShared.hlsli`
   payload、surface reconstruction、EvaluateSky、NEE、TracePathSample を見る。

4. `BistroExteriorPathtracing.hlsl`
   baseline variant が shared hlsli をどう使うかを見る。

5. `D3D12/Source/BistroExteriorPathtracingD3D12.cpp`
   DXR state object、SBT、output / accumulation / AOV / reservoir resource を見る。

6. `Vulkan/Source/BistroExteriorPathtracingVulkan.cpp`
   ray tracing pipeline、SBT、storage image / buffer、barrier を見る。

7. `BistroExteriorPathtracingReSTIR.hlsl`
   ReSTIR GI の candidate と reservoir を見る。

8. `BistroExteriorPathtracingReSTIRDI.hlsl`
   ReSTIR DI の direct-light reservoir を見る。

9. `BistroExteriorPathtracingReSTIRResolve.hlsl`
   temporal / spatial reuse compute pass を見る。

10. `BistroExteriorPathtracingDenoise.hlsl`
    AOV を使う denoiser compute pass を見る。

この順番なら、baseline path tracer が分かってから ReSTIR と denoiser に進めます。

## 実装してみて分かったこと

Pathtracing 版で大事だったのは、機能を 1 つの executable に詰め込まず、比較したい技術を sibling project として分けたことです。

```text
baseline path tracing
  -> ReSTIR GI
  -> ReSTIR DI
```

こうすると、通常版で出る問題か、ReSTIR だけの問題かを切り分けやすくなります。D3D12 / Vulkan のどちらでも同じ project 名と同じ shader variant を持つため、API 差分も追いやすくなります。

また、ReSTIR DI を入れる前に emissive triangle light list と procedural area light を入れたことも重要でした。sun だけでは direct lighting candidate が少なく、ReSTIR DI の効果を教材として観察しにくいためです。

denoiser については、ThirdParty を増やさずに AOV 付き compute pass として入れたことで、resource flow を読みやすく保てました。一方で、これは本格的な temporal/spatial denoiser ではないため、今後さらに品質を求めるなら disocclusion handling、motion vector、variance estimate、外部 denoiser の検討が次の段階になります。

## まとめ

`BistroExteriorPathtracing` は、Raytracing 版の延長にあるサンプルです。

```text
BistroExteriorRaytracing
  -> pixel から ray を飛ばす
  -> hit / miss / shadow / 1-bounce GI を理解する

BistroExteriorPathtracing
  -> raygen で path loop を回す
  -> diffuse / specular bounce を積む
  -> sun / sky / local lights を sample する
  -> ReSTIR と denoiser を比較する
```

入門者にとって大事なのは、path tracing を「魔法の高画質化」として見るのではなく、次の小さい部品に分けて読むことです。

- ray がどこから出るか。
- hit したら何を読むか。
- light をどう sample するか。
- 次の ray をどう選ぶか。
- accumulation と denoiser はどの texture を読むか。
- ReSTIR はどの buffer を reuse するか。

このサンプルは、Bistro という実シーンを使って、その部品を D3D12 と Vulkan で横並びに確認するための教材になっています。
