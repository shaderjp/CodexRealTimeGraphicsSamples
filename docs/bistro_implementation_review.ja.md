# Bistro 取り組みの振り返り

このドキュメントは、Amazon Lumberyard Bistro Exterior を使った一連のサンプル実装を振り返るためのメモです。

前段の [Bistro 実装前までの振り返り](pre_bistro_implementation_review.ja.md) では、Triangle、Cube3D、SciFiHelmet、Skinning、ImGuiLighting までを扱いました。Bistro では、そこまでに作った要素を大きな実シーンへ広げ、さらにデバッグ UI、Shadow Map、Mesh Shader、Raytracing、Pathtracing へ展開しています。

入門者向けには、Bistro は「急に巨大になったサンプル」ではなく、次の積み重ねとして読むのがおすすめです。

```text
Triangle
  -> Cube3D
    -> SciFiHelmet
      -> ImGuiLighting
        -> BistroExterior
          -> BistroExterior Shadow
          -> BistroExterior Mesh Shader
          -> BistroExterior Raytracing
          -> BistroExterior Pathtracing
```

## 対象範囲

このドキュメントで扱う Bistro 系サンプルは次の通りです。

| サンプル | 役割 |
| --- | --- |
| `Samples/BistroExterior` | Assimp で FBX を読み込む基本ラスタライズ版 |
| `Samples/BistroExterior` Shadow 版 | 同じソリューション内の directional shadow map 版 |
| `Samples/BistroExteriorMeshShader` | Bistro を Mesh Shader / meshlet culling で描く版 |
| `Samples/BistroExteriorRaytracing` | Bistro を DXR / Vulkan Ray Tracing で描く版 |
| `Samples/BistroExteriorPathtracing` | Bistro を progressive path tracer / ReSTIR 比較で描く版 |

最初に読むなら、まず `Samples/BistroExterior` の通常版だけで十分です。Shadow、Mesh Shader、Raytracing、Pathtracing は、そのあとに「同じシーンを別のテクニックで描く」派生として読むと理解しやすくなります。

## Bistro で増えたもの

SciFiHelmet までは、サンプル内に必要な glTF アセットを置き、比較的小さいモデルを表示していました。Bistro では、次の要素が一気に増えます。

- 巨大な FBX シーン
- 多数の mesh / material / texture
- DDS / TGA テクスチャ
- alpha mask を含む材質
- FPS 風カメラ
- ImGui によるデバッグ UI
- renderer stats
- D3D12 / Vulkan の見た目差分調査
- Shadow Map
- Mesh Shader / meshlet
- Raytracing acceleration structure
- Pathtracing accumulation / ReSTIR / denoiser

この増え方に圧倒されやすいですが、コードを読むときは「シーンデータ」「テクスチャ」「描画」「デバッグ」の 4 つに分けると整理できます。

## 外部依存

Bistro 系サンプルでは、Bistro 前までより外部依存が増えます。

| 依存 | パス | 役割 |
| --- | --- | --- |
| Assimp | `ThirdParty/assimp` | `BistroExterior.fbx` の実行時インポート |
| DirectXTex | `ThirdParty/DirectXTex` | DDS / TGA / WIC テクスチャ読み込み |
| Dear ImGui | `ThirdParty/imgui` | ライト、カメラ、デバッグ表示、統計 UI |
| meshoptimizer | `ThirdParty/meshoptimizer` | Mesh Shader サンプルでの meshlet 生成 |
| Vulkan SDK | local SDK | Vulkan headers / libraries / SPIR-V 用 DXC |
| DirectX 12 Agility SDK | NuGet | D3D12 runtime components |

Bistro データセットそのものは Git に含めません。`Bistro_v5_2` をリポジトリ直下に置くか、`BISTRO_ASSET_ROOT` で `BistroExterior.fbx` を含むフォルダを指定します。

```text
CodexRealTimeGraphicsSamples/
  Bistro_v5_2/
    BistroExterior.fbx
    Textures/
```

## まず読むべき共通コード

Bistro 系は API ごとの renderer が大きくなります。その前に、共通コードを読むと全体が見えやすくなります。

### `BistroScene`

`BistroScene` は、Bistro の CPU 側シーンデータを作る中心です。

見るべきこと:

- `BISTRO_ASSET_ROOT` とリポジトリ直下 `Bistro_v5_2` の探索
- Assimp による `BistroExterior.fbx` の読み込み
- mesh の vertex / index 抽出
- material の抽出
- texture path の解決
- Bistro の命名規則に合わせた fallback 探索

見るべきファイル:

- `../Samples/BistroExterior/Common/BistroScene.h`
- `../Samples/BistroExterior/Common/BistroScene.cpp`

### `BistroTexture`

`BistroTexture` は、D3D12 / Vulkan に渡す前のテクスチャデータを作ります。

見るべきこと:

- DDS / TGA / WIC の読み込み
- sRGB と linear の扱い
- fallback texture
- mip 情報
- BC 圧縮 DDS を維持する経路
- D3D12 と Vulkan で同じ見た目にするための format 情報

見るべきファイル:

- `../Samples/BistroExterior/Common/BistroTexture.h`
- `../Samples/BistroExterior/Common/BistroTexture.cpp`

### `BistroCamera`

Bistro は広いシーンなので、固定カメラでは足りません。`BistroCamera` は FPS 風の移動を担当します。

見るべきこと:

- `WASD` 移動
- `Q/E` 上下移動
- `Shift` 高速移動
- mouse look
- view matrix
- projection matrix
- ImGui 操作中の入力抑制

見るべきファイル:

- `../Samples/BistroExterior/Common/BistroCamera.h`
- `../Samples/BistroExterior/Common/BistroCamera.cpp`

## BistroExterior 通常版

![BistroExterior D3D12](images/BistroExterior%20D3D12%202026_05_05%2018_46_41.png)

`BistroExterior` の通常版は、Bistro 取り組みの土台です。まずは Shadow や Raytracing を見ずに、ここを読めば十分です。

### 学べること

- 巨大 FBX を実行時ロードする。
- 多数の mesh を draw item として管理する。
- material ごとに texture descriptor を割り当てる。
- base color / normal / specular / emissive texture を扱う。
- Bistro の specular map を `R=AO`, `G=Roughness`, `B=Metalness` として解釈する。
- alpha mask を使う。
- 1 つの directional light と ambient で PBR 風に描画する。
- ImGui でライト、カメラ、デバッグ表示を操作する。
- Renderer Stats で draw call、vertex、index、texture 数を見る。

### コードを読む順番

おすすめの読み順は次です。

1. `Common/BistroScene.cpp`
   どのような CPU 側データが作られるかを確認します。

2. `Common/BistroTexture.cpp`
   DDS / TGA がどの形式で読み込まれるかを確認します。

3. `D3D12/Source/BistroExteriorD3D12.cpp`
   texture resource、descriptor heap、vertex / index buffer、draw loop を確認します。

4. `Vulkan/Source/BistroExteriorVulkan.cpp`
   image、image view、sampler、descriptor set、command buffer を確認します。

5. `Shaders/BistroExterior.hlsl`
   material texture の使い方、PBR 計算、debug view を確認します。

見るべきファイル:

- `../Samples/BistroExterior/D3D12/Source/BistroExteriorD3D12.cpp`
- `../Samples/BistroExterior/Vulkan/Source/BistroExteriorVulkan.cpp`
- `../Samples/BistroExterior/Shaders/BistroExterior.hlsl`

## Material と Texture の読み方

Bistro で最初につまずきやすいのは、material と texture の対応です。SciFiHelmet では texture の種類が明確でしたが、Bistro では FBX とファイル名規則を組み合わせて解決しています。

基本の texture slot は次です。

| Slot | 役割 | 色空間 |
| --- | --- | --- |
| Base Color | albedo / alpha mask | sRGB |
| Normal | tangent-space normal | linear |
| Specular | AO / roughness / metalness packed | linear |
| Emissive | emission | sRGB または用途に応じた扱い |

シェーダ側では specular map を一般的な specular color としてではなく、Bistro 用の packed map として読みます。

```text
R = ambient occlusion
G = roughness
B = metalness
```

この解釈が D3D12 と Vulkan でずれると、同じ normal や NdotL でも見た目が大きく変わります。Bistro で追加した debug view は、この差分を切り分けるためのものです。

## D3D12 / Vulkan の見た目合わせ

Bistro では D3D12 と Vulkan の見た目差分を何度も確認しました。特に問題になりやすかったのは次の領域です。

- sRGB / linear の違い
- DDS の BC 圧縮形式の扱い
- normal map の読み込み
- normal map の Y flip
- mip level / sampler LOD
- tangent / bitangent / handedness
- specular packed map の解釈
- descriptor binding のずれ

このため、ImGui の `Debug View` に多くの表示を追加しました。

代表的な debug view:

- Final
- Base Color
- World Normal
- Normal Texture Raw
- Normal Texture Decoded
- Normal Texture Status
- UV
- Tangent
- Bitangent
- Tangent Handedness
- NdotL
- Specular Texture Raw
- Roughness
- Metalness
- AO
- Normal Mip

![BistroExterior World Normal D3D12](images/BistroExterior%20D3D12%202026_05_05%2018_47_06.png)

### 入門者向けの見方

見た目が違うときは、Final だけを眺めずに次の順で debug view を切り替えます。

1. Base Color
   まず albedo が同じかを見ます。ここが違えば texture format、sRGB、descriptor を疑います。

2. Normal Texture Raw
   normal map の元データが読めているかを見ます。

3. Normal Texture Decoded
   `0..1` の texture 値を `-1..1` の normal として正しく復元できているかを見ます。

4. World Normal
   tangent space から world space への変換が合っているかを見ます。

5. NdotL
   ライト方向と normal の関係が合っているかを見ます。

6. Specular / Roughness / Metalness / AO
   packed map のチャンネル解釈が合っているかを見ます。

この順番で見ると、「シェーダの計算が違う」のか「入力 texture が違う」のかを分けられます。

## ImGui と Renderer Stats

Bistro では、ImGui が単なる操作 UI ではなく、検証用の計器になりました。

`Bistro Controls` で操作できる主なもの:

- directional light direction
- light color
- light intensity
- camera position
- yaw / pitch
- move speed
- fast speed
- debug view
- normal map Y flip
- force normal mip
- normal mip bias

`Renderer Stats` で確認できる主なもの:

- FPS
- frame time
- API 名
- tearing support
- material count
- draw call count
- vertex count
- index count
- primitive count
- texture count
- normal map diagnostics

入門者にとって重要なのは、こうした UI が「便利機能」ではなく、D3D12 と Vulkan の差分を潰すための観測点になっていることです。

## Shadow Map 版

![BistroExterior Shadow D3D12](images/BistroExterior%20Shadow%20D3D12%202026_05_05%2021_59_38.png)

Shadow Map 版は、通常版と同じ `BistroExterior.sln` に追加された別プロジェクトです。

プロジェクト:

- `D3D12Shadow/Source/BistroExteriorShadowD3D12.vcxproj`
- `VulkanShadow/Source/BistroExteriorShadowVulkan.vcxproj`

見るべきファイル:

- `../Samples/BistroExterior/D3D12Shadow/Source/BistroExteriorShadowD3D12.cpp`
- `../Samples/BistroExterior/VulkanShadow/Source/BistroExteriorShadowVulkan.cpp`
- `../Samples/BistroExterior/Shaders/BistroExteriorShadow.hlsl`

### 学べること

- Shadow 用 depth texture を作る。
- light view projection を作る。
- Shadow pass と main pass を分ける。
- alpha mask 材質を shadow pass でも扱う。
- PCF で shadow を少し柔らかくする。
- ImGui で shadow パラメータを調整する。

### Shadow Map の読み方

処理の流れは次です。

```text
camera / light update
  -> shadow pass
    -> light view projection で depth map を作る
  -> main pass
    -> world position を light space に変換する
    -> shadow map と比較する
    -> direct light だけに shadow factor を掛ける
  -> ImGui
```

ImGui の `Shadow Map` セクションでは、次の値を調整できます。

- Enable Shadows
- Resolution
- Depth Bias
- Normal Bias
- PCF Radius
- Ortho Size
- Focus Distance
- Depth Range

Bistro は広いので、Shadow Map はシーン全体を固定で覆うのではなく、FPS カメラの前方周辺に追従する方式にしています。これは入門者にとって大事な設計判断です。広大なシーン全体を 1 枚の shadow map に入れると解像度が足りず、見たい場所の shadow 品質が落ちるためです。

### D3D12 と Vulkan で見るポイント

D3D12:

- `D32_FLOAT` depth texture
- DSV / SRV
- shadow map の resource barrier
- comparison sampler
- material SRV heap への shadow SRV 追加

Vulkan:

- shadow depth image
- shadow image view
- shadow render pass
- shadow framebuffer
- depth image layout transition
- comparison sampler
- descriptor set 更新

Vulkan では image layout と pipeline barrier が特に重要です。Shadow pass で depth write した image を main pass で shader read するため、状態遷移がずれると shadow が破綻します。

## Mesh Shader 版

![BistroExteriorMeshShader D3D12](images/BistroExteriorMeshShader%20D3D12%202026_05_06%2010_59_04.png)

`BistroExteriorMeshShader` は、Bistro のジオメトリ描画を Mesh Shader 化したサンプルです。通常の vertex/index draw ではなく、meshlet を単位にして描画します。

Mesh Shader 版だけをより細かく読みたい場合は、専用の [Bistro Mesh Shader 実装の振り返り](bistro_mesh_shader_implementation_review.ja.md) も参照してください。

見るべきファイル:

- `../Samples/BistroExteriorMeshShader/Common/BistroScene.h`
- `../Samples/BistroExteriorMeshShader/D3D12/Source/BistroExteriorMeshShaderD3D12.cpp`
- `../Samples/BistroExteriorMeshShader/Vulkan/Source/BistroExteriorMeshShaderVulkan.cpp`
- `../Samples/BistroExteriorMeshShader/Shaders/BistroExteriorMeshShader.hlsl`
- `../Samples/BistroExteriorMeshShader/Shaders/BistroExteriorMeshShaderCulling.hlsl`

### 学べること

- `meshoptimizer` で meshlet を生成する。
- meshlet-local triangle を pack する。
- D3D12 Mesh Shader で `DispatchMesh` する。
- Vulkan で `VK_EXT_mesh_shader` を使う。
- meshlet color debug view で分割単位を見る。
- task / amplification shader 相当で culling する。
- camera frustum culling と cone backface culling を行う。
- shadow pass では light frustum で culling する。

### 入門者向けの読み方

Mesh Shader はいきなり読むと難しいため、まず通常版との対応を見るのがよいです。

| 通常ラスタ版 | Mesh Shader 版 |
| --- | --- |
| vertex buffer | meshlet vertex data |
| index buffer | meshlet-local triangle data |
| draw indexed | dispatch mesh tasks |
| vertex shader | mesh shader |
| CPU draw item loop | meshlet dispatch range |

まずは「同じ Bistro scene を別の入力単位で描いている」と考えると理解しやすいです。

![BistroExteriorMeshShader Meshlet Color](images/BistroExteriorMeshShader%20D3D12%202026_05_06%2010_59_22.png)

## Raytracing 版

![BistroExteriorRaytracing D3D12](images/BistroExteriorRaytracing%20D3D12%202026_05_06%2013_26_50.png)

`BistroExteriorRaytracing` は、Bistro を raster pass ではなく ray tracing pipeline で描画するサンプルです。可視性は primary ray で解決し、出力 UAV に書き込んでから swapchain へコピーします。

Raytracing 版だけをより細かく読みたい場合は、専用の [Bistro Raytracing 実装の振り返り](bistro_raytracing_implementation_review.ja.md) も参照してください。

見るべきファイル:

- `../Samples/BistroExteriorRaytracing/Common/BistroScene.h`
- `../Samples/BistroExteriorRaytracing/D3D12/Source/BistroExteriorRaytracingD3D12.cpp`
- `../Samples/BistroExteriorRaytracing/Vulkan/Source/BistroExteriorRaytracingVulkan.cpp`
- `../Samples/BistroExteriorRaytracing/Shaders/BistroExteriorRaytracing.hlsl`
- `../Samples/BistroExteriorRaytracing/Shaders/BistroExteriorRaytracingShadow.hlsl`
- `../Samples/BistroExteriorRaytracing/Shaders/BistroExteriorRaytracingGI.hlsl`
- `../Samples/BistroExteriorRaytracing/Shaders/BistroExteriorRaytracingShared.hlsli`

### 学べること

- BLAS / TLAS を作る。
- geometry index から material / geometry record を引く。
- ray generation / closest hit / any hit / miss shader を使う。
- alpha mask を any-hit で処理する。
- material texture を bindless 的に参照する。
- hard shadow ray を飛ばす。
- 1spp diffuse 1-bounce GI と temporal accumulation を行う。
- ray tracing output を swapchain にコピーする。
- ImGui overlay を最後に描画する。

### 読む順番

Raytracing 版は、通常の raster pipeline とかなり形が違います。次の順で読むと楽です。

1. Scene / material / texture の準備
   通常版と共通する入力データを確認します。

2. Acceleration Structure
   BLAS と TLAS をどう作るかを見ます。

3. Shader Binding Table
   ray generation、miss、hit group の record を確認します。

4. Descriptor / bindless texture
   material から texture をどう引くかを見ます。

5. Raytracing shader
   primary ray、shadow ray、GI ray の役割を確認します。

6. Output copy
   ray tracing の結果を swapchain にどう出すかを見ます。

## Pathtracing 版

![BistroExteriorPathtracing D3D12](<images/BistroExteriorPathtracing D3D12 2026_05_08 0_37_03.png>)

`BistroExteriorPathtracing` は、Raytracing 版の GI をさらに進め、Bistro を progressive path tracer として描画するサンプルです。シーン用 raster pass は使わず、raygen 内の path loop で diffuse / specular bounce を進め、sun / sky / local light の next-event estimation、accumulation、ReSTIR 比較、軽量 denoiser を扱います。

Pathtracing 版だけをより細かく読みたい場合は、専用の [Bistro Pathtracing 実装の振り返り](bistro_pathtracing_implementation_review.ja.md) も参照してください。

見るべきファイル:

- `../Samples/BistroExteriorPathtracing/Common/BistroScene.h`
- `../Samples/BistroExteriorPathtracing/D3D12/Source/BistroExteriorPathtracingD3D12.cpp`
- `../Samples/BistroExteriorPathtracing/Vulkan/Source/BistroExteriorPathtracingVulkan.cpp`
- `../Samples/BistroExteriorPathtracing/Shaders/BistroExteriorPathtracingShared.hlsli`
- `../Samples/BistroExteriorPathtracing/Shaders/BistroExteriorPathtracingReSTIR.hlsl`
- `../Samples/BistroExteriorPathtracing/Shaders/BistroExteriorPathtracingReSTIRDI.hlsl`
- `../Samples/BistroExteriorPathtracing/Shaders/BistroExteriorPathtracingDenoise.hlsl`

### 学べること

- raygen 内で path loop を回す。
- throughput を更新しながら diffuse / GGX specular bounce を選ぶ。
- sun / sky / emissive / procedural area light を sample する。
- optional environment map を procedural sky fallback として扱う。
- ReSTIR GI / DI を別 project として比較する。
- current / history / spatial reservoir buffer を compute pass で reuse する。
- normal / depth / albedo AOV を使った軽量 denoiser を入れる。

### 読む順番

Pathtracing 版は、Raytracing 版よりさらに機能が多いです。次の順で読むと楽です。

1. Baseline path tracing
   ReSTIR と denoiser を忘れて、`TracePathSample` の path loop を読む。

2. Light list
   emissive triangle と procedural area light が `RtLight` buffer に入る流れを見る。

3. Accumulation
   sample count、radiance clamp、temporal clamp、reset 条件を見る。

4. ReSTIR GI / DI
   reservoir に何を入れているか、reuse pass がどの buffer を読むかを見る。

5. Denoiser
   RayGen が書く AOV と compute pass の入力を確認する。

## D3D12 と Vulkan を比較する軸

Bistro 系では、同じテクニックを D3D12 と Vulkan の両方で実装しているため、比較ポイントが増えます。

### Raster / Shadow

| 観点 | D3D12 | Vulkan |
| --- | --- | --- |
| texture | `ID3D12Resource` + SRV | `VkImage` + `VkImageView` + sampler |
| descriptor | CBV/SRV/UAV heap | descriptor set / descriptor pool |
| render target | RTV / DSV | render pass / framebuffer |
| shadow depth | DSV + SRV | depth image + sampled image layout |
| 状態遷移 | resource barrier | pipeline barrier / image layout |

### Mesh Shader

| 観点 | D3D12 | Vulkan |
| --- | --- | --- |
| feature | Mesh Shader Tier | `VK_EXT_mesh_shader` |
| dispatch | `DispatchMesh` | `vkCmdDrawMeshTasksEXT` |
| task shader 相当 | amplification shader | task shader |
| shader model | HLSL 6.6+ | HLSL to SPIR-V |

### Raytracing

| 観点 | D3D12 | Vulkan |
| --- | --- | --- |
| acceleration structure | DXR AS | `VK_KHR_acceleration_structure` |
| pipeline | state object | ray tracing pipeline |
| shader table | SBT | SBT |
| resource address | GPU virtual address | buffer device address |
| required features | DXR tier | KHR ray tracing extensions |

### Pathtracing / ReSTIR / Denoiser

| 観点 | D3D12 | Vulkan |
| --- | --- | --- |
| path tracing output | UAV texture | storage image |
| accumulation | UAV texture | storage image |
| reservoir | UAV structured buffer | storage buffer |
| reuse pass | compute PSO | compute pipeline |
| denoiser AOV | UAV texture | storage image |
| 同期 | resource / UAV barrier | image / buffer memory barrier |

API 名は違いますが、やっていることはかなり近いです。入門者は、まず「どのデータを GPU に置くか」「どの shader が読むか」「どこで同期するか」の 3 点で見ると迷いにくくなります。

## デバッグ機能を増やした理由

Bistro では、見た目の差分が単純なバグに見えても、原因が texture format、sampler、normal map、material channel、座標系、shadow depth などに分かれます。

そのため、Final の見た目だけで判断せず、入力と中間値を可視化する方針を取りました。

追加したデバッグ表示の意義:

- Base Color: albedo と sRGB の確認
- Normal Texture Raw: texture の読み込み確認
- Normal Texture Decoded: normal map decode 確認
- World Normal: tangent basis 変換確認
- UV: UV 座標と texture repeat 確認
- Tangent / Bitangent: TBN の向き確認
- Tangent Handedness: handedness の符号確認
- NdotL: ライト計算の基本確認
- Specular Texture Raw: packed map の確認
- Roughness / Metalness / AO: チャンネル解釈確認
- Shadow Map Depth: shadow map が書けているか確認
- Shadow Factor: shadow compare の結果確認
- Light Space Depth: light projection の範囲確認
- Hit Distance: ray が scene に当たっているか確認
- Direct / Indirect: ray tracing / path tracing の lighting 成分確認
- Reservoir Weight: ReSTIR の candidate reuse 確認
- Accumulation Samples: progressive sampling の進み具合確認

この考え方は、今後さらに大きなサンプルを作るときにも使えます。複雑な描画ほど「完成画だけを見る」のではなく、「入力」「中間値」「最終合成」を分けて見るのが近道です。

## Bistro 系を読むためのおすすめ順

最初から Mesh Shader、Raytracing、Pathtracing に飛ぶより、次の順番が読みやすいです。

1. `Samples/BistroExterior/Common`
   シーン、テクスチャ、カメラの共通データを読む。

2. `Samples/BistroExterior/D3D12` 通常版
   D3D12 の大規模ラスタ描画を読む。

3. `Samples/BistroExterior/Vulkan` 通常版
   同じ描画を Vulkan でどう書くかを見る。

4. `Samples/BistroExterior/Shaders/BistroExterior.hlsl`
   material、normal、PBR、debug view を読む。

5. `D3D12Shadow` / `VulkanShadow`
   pass が 1 つ増えると renderer がどう変わるかを見る。

6. `Samples/BistroExteriorMeshShader`
   draw indexed から meshlet dispatch へ切り替わる点を見る。

7. `Samples/BistroExteriorRaytracing`
   raster pipeline ではなく ray tracing pipeline で同じ scene を扱う点を見る。

8. `Samples/BistroExteriorPathtracing`
   raygen の path loop、accumulation、ReSTIR、denoiser へ広げる点を見る。

## 入門者向けチェックリスト

Bistro のコードを読んでいて迷ったら、次の質問に戻ると整理しやすいです。

- このデータは CPU 側で作られているか、GPU 側で作られているか。
- この buffer / texture は誰が書き、誰が読むか。
- descriptor は material ごとか、frame ごとか、global か。
- texture は sRGB か linear か。
- normal map は raw、decoded、world normal のどこまで正しいか。
- D3D12 と Vulkan で同じ binding 番号を見ているか。
- pass が変わるときに resource state / image layout は正しいか。
- ImGui で切り替えた値は constant buffer / uniform buffer に反映されているか。
- Renderer Stats の draw call / primitive / texture 数は想定通りか。

## まとめ

Bistro の取り組みは、単に大きなシーンを表示しただけではありません。

まず `BistroExterior` で、実データを読み込み、D3D12 / Vulkan の見た目をそろえるための土台を作りました。次に Shadow Map で multi-pass rendering を加えました。その後、同じ Bistro scene を Mesh Shader、Raytracing、Pathtracing の教材として再利用できる形に広げました。

コードを読むときは、Bistro を巨大な 1 個のサンプルとして見るより、次のように段階で分けると見通しがよくなります。

```text
Asset loading
  -> Texture and material setup
    -> Raster rendering
      -> Debug visualization
        -> Shadow pass
          -> Meshlet rendering
            -> Ray tracing
              -> Path tracing / ReSTIR / denoising
```

この段階構造を意識すると、Bistro 系サンプルは「大きくて怖いコード」ではなく、これまで作ってきた描画技術を実シーンで検証するための、かなりよい実験場として読めます。
