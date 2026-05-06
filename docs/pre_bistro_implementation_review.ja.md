# Bistro 実装前までの振り返り

このドキュメントは、`Samples/BistroExterior` に入る前までに作成したサンプルを振り返るためのメモです。

目的は、実装の履歴を並べることではなく、これからコードを読む人が「どのサンプルで何を学べるか」「どのファイルから読むと迷いにくいか」をつかめるようにすることです。Direct3D12 と Vulkan の両方を並べて実装しているため、同じ描画内容を 2 つの API でどう表現しているかを見る教材としても使えます。

## 対象範囲

Bistro 実装前までの対象は次のサンプルです。

| 順番 | サンプル | 役割 |
| --- | --- | --- |
| 1 | `Samples/Triangle` | ウィンドウ、デバイス、スワップチェーン、シェーダ、三角形描画 |
| 2 | `Samples/Cube3D` | 頂点/インデックスバッファ、Constant Buffer、Depth Buffer、3D 回転 |
| 3 | `Samples/SciFiHelmet` | glTF 2.0、テクスチャ、簡易 PBR、ディレクショナルライト |
| 4 | `Samples/Skinning` | Cesium Man、ジョイント、アニメーション、Vertex Shader / Compute Shader Skinning |
| 5 | `Samples/ImGuiLighting` | Dear ImGui 統合、実行時パラメータ編集 |

`Samples/BistroExterior` 以降は、Assimp、DirectXTex、巨大 FBX シーン、FPS カメラ、デバッグ UI、Shadow Map などが入る別段階のサンプルとして扱います。

## 全体の設計方針

このリポジトリは、テクニック単位で `Samples/<TechniqueName>` を作り、その下に Visual Studio ソリューションを置く構成です。

```text
Samples/
  Triangle/
    Triangle.sln
    D3D12/
      Source/
    Vulkan/
      Source/
    Shaders/
    Assets/
```

読むときの大事な前提は次の 3 つです。

- 1 つのサンプルに Direct3D12 版と Vulkan 版を横並びで置く。
- シェーダは基本的に HLSL 6.6 を共有し、DXC で D3D12 向け DXIL と Vulkan 向け SPIR-V にビルドする。
- 共通化は急がず、まずは API ごとの実装を読める形で残す。

この方針のおかげで、入門者は「抽象化されたエンジンの中身」を追う前に、API の基本単位を直接見ることができます。

## 最初に読む場所

各サンプルで読む順番は、だいたい同じです。

1. `README.ja.md`
   サンプルの目的、ビルド方法、スクリーンショットを確認します。

2. `Directory.Build.props` / `Directory.Build.targets`
   出力先、NuGet、シェーダコンパイル、Vulkan SDK の扱いを確認します。

3. `D3D12/Source/*.vcxproj` / `Vulkan/Source/*.vcxproj`
   どの `.cpp`、`.h`、`.hlsl`、外部ライブラリを使うかを確認します。

4. `Main.cpp`
   アプリケーションの入口を確認します。

5. `Win32Application.cpp` または各 API のメインループ
   ウィンドウメッセージ、更新、描画の流れを確認します。

6. サンプル本体のクラス
   例: `TriangleD3D12.cpp`、`TriangleVulkan.cpp`。
   `OnInit`、`OnUpdate`、`OnRender`、`OnDestroy` の流れを追います。

7. `Shaders/*.hlsl`
   頂点入力、定数バッファ、テクスチャ、ライティングの扱いを確認します。

最初から全部を理解しようとすると大変なので、まずは「CPU 側でリソースを作る」「GPU にコマンドを積む」「シェーダでピクセルを出す」の 3 段階に分けて読むと楽になります。

## Triangle

![Triangle D3D12](images/Triangle%20D3D12%202026_05_05%2012_25_43.png)

`Triangle` は、もっとも小さい描画サンプルです。ここで見るべきことは、三角形そのものよりも「画面に 1 フレーム描くまでに最低限何が必要か」です。

### 学べること

- Win32 ウィンドウを作る。
- D3D12 / Vulkan のデバイスを初期化する。
- スワップチェーンを作る。
- ビルド時に HLSL をコンパイルする。
- 頂点バッファを作って三角形を描く。
- GPU の完了を待つ。

### コードを読むポイント

Direct3D12 版では、まず次の関係を見ると理解しやすいです。

- `ID3D12Device`
- `IDXGISwapChain3`
- `ID3D12CommandQueue`
- `ID3D12CommandAllocator`
- `ID3D12GraphicsCommandList`
- RTV descriptor heap
- fence

Vulkan 版では、同じ役割が次のような名前に分かれます。

- `VkInstance`
- `VkSurfaceKHR`
- `VkPhysicalDevice`
- `VkDevice`
- `VkSwapchainKHR`
- `VkRenderPass`
- `VkFramebuffer`
- `VkCommandBuffer`
- `VkSemaphore` / `VkFence`

入門者向けには、まず D3D12 の `LoadPipeline` と Vulkan の初期化処理を並べて、「画面に描くための入れ物」を作っている箇所を探すのがおすすめです。

### 見るべきファイル

- `../Samples/Triangle/D3D12/Source/TriangleD3D12.cpp`
- `../Samples/Triangle/Vulkan/Source/TriangleVulkan.cpp`
- `../Samples/Triangle/Shaders/Triangle.hlsl`

## Cube3D

![Cube3D D3D12](images/Cube3D%20D3D12%202026_05_05%2012_20_40.png)

`Cube3D` は、2D 的な三角形から 3D 描画へ進むサンプルです。ここで「ジオメトリ」「カメラ」「射影」「深度」という 3D 描画の基本部品が入ります。

### 学べること

- 立方体を頂点バッファとインデックスバッファで表す。
- Constant Buffer / Uniform Buffer で変換行列をシェーダに渡す。
- Depth Buffer を作って depth test を行う。
- 毎フレーム回転角を更新する。

### コードを読むポイント

最初に見るとよいのは、頂点定義とインデックス定義です。Triangle では 3 頂点だけでしたが、Cube3D では「同じ頂点を複数の三角形から参照する」ためにインデックスバッファを使います。

次に Constant Buffer を見ます。ここには主に world、view、projection の変換が入ります。毎フレーム回転を変え、シェーダ側で `mul` して頂点位置をクリップ空間へ変換します。

Direct3D12 と Vulkan を比較するときは、次の差分に注意します。

- クリップ空間の Y 向き
- 深度範囲
- Uniform / Constant Buffer のアライメント
- フレームごとのリソース更新と同期

### 見るべきファイル

- `../Samples/Cube3D/D3D12/Source/Cube3DD3D12.cpp`
- `../Samples/Cube3D/Vulkan/Source/Cube3DVulkan.cpp`
- `../Samples/Cube3D/Shaders/Cube3D.hlsl`

## SciFiHelmet

![SciFiHelmet D3D12](images/SciFiHelmet%20D3D12%202026_05_05%2012_21_45.png)

`SciFiHelmet` では、手書きの頂点配列から一歩進み、glTF 2.0 の実アセットを読み込みます。ここから「モデルを表示する」サンプルになります。

### 学べること

- glTF 2.0 のメッシュデータを読む。
- Base Color、Metallic-Roughness、Normal、Ambient Occlusion テクスチャを扱う。
- 簡易 PBR シェーダで描画する。
- ディレクショナルライトを 1 つ置く。
- D3D12 / Vulkan でテクスチャ、サンプラー、descriptor binding をそろえる。

### コードを読むポイント

Triangle と Cube3D では、描く対象の頂点データがコード内にありました。SciFiHelmet では、アセットから頂点、インデックス、マテリアル、テクスチャを読み取ります。

読むときは、まず CPU 側で次のデータがどう作られるかを見ると理解しやすいです。

- position
- normal
- tangent
- texcoord
- index
- material
- texture

その後、シェーダで次の値がどう使われるかを見ます。

- base color
- roughness
- metallic
- normal map
- ambient occlusion
- light direction

ここから「シェーダの見た目の差分」が API 差分として現れやすくなります。D3D12 と Vulkan で見た目をそろえるには、画像フォーマット、sRGB、UV、normal map、サンプラー、座標系を順に疑う癖が大事です。

### 見るべきファイル

- `../Samples/SciFiHelmet/D3D12/Source/SciFiHelmetD3D12.cpp`
- `../Samples/SciFiHelmet/Vulkan/Source/SciFiHelmetVulkan.cpp`
- `../Samples/SciFiHelmet/Shaders/SciFiHelmet.hlsl`

## Skinning

![Skinning D3D12](images/Skinning%20D3D12%202026_05_05%2012_22_39.png)

`Skinning` は、glTF 2.0 の Cesium Man を使ったアニメーションサンプルです。ここでは「メッシュが骨に従って変形する」流れを扱います。

### 学べること

- glTF の skeleton、joint、inverse bind matrix を読む。
- アニメーションを時間でサンプリングする。
- joint matrix を作る。
- 頂点ごとの joint index と joint weight を使う。
- Vertex Shader Skinning と Compute Shader Skinning を比較する。

### Vertex Shader Skinning の読み方

Vertex Shader Skinning では、元の頂点バッファはそのままです。頂点シェーダ内で joint matrix を参照し、各頂点をその場で変形してから描画します。

読む順番は次がおすすめです。

1. CPU 側でアニメーション時刻を進める。
2. joint ごとの行列を更新する。
3. Constant Buffer に joint matrix を書く。
4. 頂点シェーダで joint index / joint weight を使ってスキニングする。

### Compute Shader Skinning の読み方

Compute Shader Skinning では、先に compute shader で変形済み頂点バッファを作ります。そのあと graphics pipeline で、そのバッファを普通のメッシュとして描きます。

この構成では、次の 2 段階に分かれます。

1. Compute pass: 入力頂点を読み、skinned vertex buffer へ書き込む。
2. Graphics pass: skinned vertex buffer を読んで描画する。

D3D12 の structured buffer と Vulkan の storage buffer で同じレイアウトにするため、16-byte 単位のデータ配置が重要になります。ここは入門者がつまずきやすいので、`SkinningData.h` と `SkinningCompute.hlsl` を並べて見るのがよいです。

### 見るべきファイル

- `../Samples/Skinning/Shared/SkinningData.h`
- `../Samples/Skinning/D3D12/Source/SkinningD3D12.cpp`
- `../Samples/Skinning/Vulkan/Source/SkinningVulkan.cpp`
- `../Samples/Skinning/D3D12Compute/Source/SkinningComputeD3D12.cpp`
- `../Samples/Skinning/VulkanCompute/Source/SkinningComputeVulkan.cpp`
- `../Samples/Skinning/Shaders/Skinning.hlsl`
- `../Samples/Skinning/Shaders/SkinningCompute.hlsl`

## ImGuiLighting

![ImGuiLighting D3D12](images/ImGuiLighting%20D3D12%202026_05_05%2012_24_18.png)

`ImGuiLighting` は、SciFiHelmet をベースに Dear ImGui を統合したサンプルです。ここで初めて、描画結果を見ながら実行時にパラメータを変更する UI が入ります。

### 学べること

- `ThirdParty/imgui` をプロジェクトから直接参照する。
- Win32 メッセージを ImGui に渡す。
- D3D12 / Vulkan のメイン描画後に ImGui を描画する。
- Direction、Color、Intensity を UI から編集し、Constant Buffer へ反映する。

### コードを読むポイント

ImGui は通常の 3D 描画とは別の描画経路を持ちます。読むときは、次の 3 箇所を探すとよいです。

- 初期化: ImGui context、Win32 backend、D3D12 / Vulkan backend
- フレーム開始: `NewFrame`
- 描画: 3D シーンのあとに ImGui draw data を送る

D3D12 では ImGui 用に shader-visible descriptor heap を用意します。Vulkan では descriptor pool や render pass との接続を見るのがポイントです。

このサンプル以降、BistroExterior では ImGui をデバッグ UI としてさらに広く使います。したがって ImGuiLighting は「デバッグ可能なサンプル」へ進むための橋渡しです。

### 見るべきファイル

- `../Samples/ImGuiLighting/D3D12/Source/ImGuiLightingD3D12.cpp`
- `../Samples/ImGuiLighting/Vulkan/Source/ImGuiLightingVulkan.cpp`
- `../Samples/ImGuiLighting/Shaders/ImGuiLighting.hlsl`

## D3D12 と Vulkan を比較するときの対応表

同じ概念でも API ごとに名前や責務の分け方が違います。読むときは、完全に 1 対 1 で対応させようとせず、「どの役割を担っているか」で見ると理解しやすくなります。

| 役割 | Direct3D12 | Vulkan |
| --- | --- | --- |
| GPU デバイス | `ID3D12Device` | `VkDevice` |
| 描画先の交換 | `IDXGISwapChain3` | `VkSwapchainKHR` |
| コマンド投入 | `ID3D12CommandQueue` | `VkQueue` |
| コマンド記録 | `ID3D12GraphicsCommandList` | `VkCommandBuffer` |
| コマンド記録用メモリ | `ID3D12CommandAllocator` | `VkCommandPool` |
| Render Target 管理 | RTV descriptor heap | render pass / framebuffer / image view |
| Depth 管理 | DSV descriptor heap | depth image / image view / attachment |
| リソース参照 | CBV/SRV/UAV descriptor heap | descriptor set / descriptor pool / descriptor set layout |
| 同期 | fence | fence / semaphore |
| 状態遷移 | resource barrier | image layout transition / pipeline barrier |
| シェーダ出力 | `.cso` | `.spv` |

## 入門者向けのデバッグ観点

描画がおかしいときは、いきなりシェーダ全部を疑うより、段階を分けると原因を絞りやすいです。

1. 何も描けない
   スワップチェーン、render target、command buffer、fence、present を確認します。

2. 形が崩れる
   頂点レイアウト、stride、index format、座標系、行列の転置を確認します。

3. 奥行きがおかしい
   Depth Buffer、depth compare、projection matrix、near/far、クリップ空間の差を確認します。

4. 色が違う
   sRGB、テクスチャフォーマット、sampler、base color、ガンマ補正を確認します。

5. ライティングが違う
   normal、tangent、bitangent、normal map の Y 反転、light direction、NdotL を確認します。

6. アニメーションが崩れる
   joint index、joint weight、inverse bind matrix、行列の掛け順、buffer layout を確認します。

この流れは、BistroExterior のような大きいサンプルでもそのまま使えます。

## Bistro へ進む前に押さえること

BistroExterior は、これまでの内容を一気に大きくしたサンプルです。入る前に、次の点を押さえておくと読みやすくなります。

- Triangle で、1 フレーム描くための API の基本形を理解する。
- Cube3D で、Constant Buffer と Depth Buffer を理解する。
- SciFiHelmet で、モデル、マテリアル、テクスチャ、PBR の流れを理解する。
- Skinning で、CPU 側データと GPU 側バッファレイアウトの対応を意識する。
- ImGuiLighting で、実行時 UI と描画パラメータ更新の流れを理解する。

この順番で見ておくと、BistroExterior で増える複雑さは「未知の仕組み」ではなく、「既に見た仕組みが大きなシーンに広がったもの」として読めます。
