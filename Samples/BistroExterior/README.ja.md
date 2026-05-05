# BistroExterior

英語版は [README.md](README.md) を参照してください。

`BistroExterior` は Amazon Lumberyard Bistro Exterior を Assimp で実行時に読み込み、Direct3D12 または Vulkan で描画するサンプルです。広いシーンを扱うため、FBX 読み込み、DirectXTex による DDS/TGA 読み込み、材質 descriptor、depth test、FPS 風カメラ移動をまとめています。Shadow Map 版は別プロジェクトとして追加しており、通常版とディレクショナルライト shadow の実装を同じソリューション内で比較できます。

## 内容

- 1 つの Visual Studio 2022 ソリューション内の Direct3D12 / Vulkan プロジェクト
- `ThirdParty/assimp` による `BistroExterior.fbx` の実行時読み込み
- `ThirdParty/DirectXTex` による DDS/TGA テクスチャ読み込み
- D3D12 と同じ BC 圧縮 DDS 形式を維持して Vulkan にアップロードするテクスチャ経路
- D3D12 / Vulkan 共通の HLSL 6.6 PBR 風シェーダ
- ディレクショナルライト 1 つ + 固定 ambient
- Bistro の Specular map を `R=AO`, `G=Roughness`, `B=Metalness` として扱う処理
- cutout 材質向けの alpha mask
- `WASD` 移動、`Q/E` 下降/上昇、`Shift` 高速移動、右ドラッグ mouse look
- 単一ディレクショナルライト向け Shadow Map 版。ImGui から解像度、bias、PCF radius、カメラ追従 light frustum を調整できます

## プロジェクト

- `BistroExterior.sln`
- `D3D12/Source/BistroExteriorD3D12.vcxproj`
- `Vulkan/Source/BistroExteriorVulkan.vcxproj`
- `D3D12Shadow/Source/BistroExteriorShadowD3D12.vcxproj`
- `VulkanShadow/Source/BistroExteriorShadowVulkan.vcxproj`

## スクリーンショット

通常描画:

| Direct3D12 | Vulkan |
| --- | --- |
| ![BistroExterior D3D12 Final](../../docs/images/BistroExterior%20D3D12%202026_05_05%2018_46_41.png) | ![BistroExterior Vulkan Final](../../docs/images/BistroExterior%20Vulkan%202026_05_05%2021_58_06.png) |

Shadow Map 描画:

| Direct3D12 | Vulkan |
| --- | --- |
| ![BistroExterior Shadow D3D12](../../docs/images/BistroExterior%20Shadow%20D3D12%202026_05_05%2021_59_38.png) | ![BistroExterior Shadow Vulkan](../../docs/images/BistroExterior%20Shadow%20Vulkan%202026_05_05%2022_20_03.png) |

`Bistro Controls` の ImGui ウィンドウにある `Debug View` コンボボックスで、最終描画と各種デバッグ表示を切り替えられます。D3D12 / Vulkan の見え方比較やシェーダ確認に使います。Shadow Map 版では `Shadow Map Depth`、`Shadow Factor`、`Light Space Depth` のデバッグ表示も使えます。

Shadow Map 版の `Shadow Map` セクションでは、有効/無効、1024/2048/4096 の解像度、depth bias、normal bias、PCF radius、orthographic size、focus distance、depth range、reset を変更できます。Bistro は広いシーンなので、light camera はシーン全体固定ではなく FPS カメラの前方周辺を追従します。

`Renderer Stats` ウィンドウでは FPS、フレーム時間、draw call、頂点数、インデックス数、プリミティブ数、テクスチャ数、normal map 診断を確認できます。Shadow Map 版では shadow 解像度、shadow draw call、shadow primitive 数、bias / PCF 設定も表示します。

| Debug View | Direct3D12 | Vulkan |
| --- | --- | --- |
| Base Color | ![BistroExterior D3D12 Base Color](../../docs/images/BistroExterior%20D3D12%202026_05_05%2018_42_36.png) | ![BistroExterior Vulkan Base Color](../../docs/images/BistroExterior%20Vulkan%202026_05_05%2021_58_17.png) |
| World Normal | ![BistroExterior D3D12 World Normal](../../docs/images/BistroExterior%20D3D12%202026_05_05%2018_47_06.png) | ![BistroExterior Vulkan World Normal](../../docs/images/BistroExterior%20Vulkan%202026_05_05%2021_58_32.png) |
| Normal Texture Decoded | ![BistroExterior D3D12 Normal Texture Decoded](../../docs/images/BistroExterior%20D3D12%202026_05_05%2018_46_58.png) | ![BistroExterior Vulkan Normal Texture Decoded](../../docs/images/BistroExterior%20Vulkan%202026_05_05%2021_58_44.png) |

## アセット

Bistro データセットはこのリポジトリには含めません。NVIDIA ORCA から Amazon Lumberyard Bistro を取得し、展開した `Bistro_v5_2` フォルダをリポジトリ直下に置いてください。

```text
CodexRealTimeGraphicsSamples/
  Bistro_v5_2/
    BistroExterior.fbx
    Textures/
```

または、`BistroExterior.fbx` を直接含むフォルダを `BISTRO_ASSET_ROOT` 環境変数で指定できます。

Attribution: Amazon Lumberyard Bistro は NVIDIA ORCA で配布されており、Creative Commons Attribution 4.0 International (CC BY 4.0) ライセンスです。データセット内に `san_giuseppe_bridge_4k.hdr` と `BistroExterior.pyscene` が含まれている場合がありますが、この初期版では使用していません。

## ビルド

先に submodule を初期化してください。

```powershell
git submodule update --init --recursive
```

Visual Studio 2022 または MSBuild でビルドします。

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" Samples\BistroExterior\BistroExterior.sln /restore /p:Platform=x64 /p:Configuration=Debug
```

MSBuild 時に Assimp を static library として configure/build してからサンプル本体をコンパイルします。Vulkan 版のビルドには Vulkan SDK と `VULKAN_SDK` が必要です。
