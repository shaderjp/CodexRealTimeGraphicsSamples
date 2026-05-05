# BistroExterior

英語版は [README.md](README.md) を参照してください。

`BistroExterior` は Amazon Lumberyard Bistro Exterior を Assimp で実行時に読み込み、Direct3D12 または Vulkan で描画するサンプルです。広いシーンを扱うため、FBX 読み込み、DirectXTex による DDS/TGA 読み込み、材質 descriptor、depth test、FPS 風カメラ移動をまとめています。

## 内容

- 1 つの Visual Studio 2022 ソリューション内の Direct3D12 / Vulkan プロジェクト
- `ThirdParty/assimp` による `BistroExterior.fbx` の実行時読み込み
- `ThirdParty/DirectXTex` による DDS/TGA テクスチャ読み込み
- D3D12 / Vulkan 共通の HLSL 6.6 PBR 風シェーダ
- ディレクショナルライト 1 つ + 固定 ambient
- Bistro の Specular map を `R=AO`, `G=Roughness`, `B=Metalness` として扱う処理
- cutout 材質向けの alpha mask
- `WASD` 移動、`Q/E` 下降/上昇、`Shift` 高速移動、右ドラッグ mouse look

## プロジェクト

- `BistroExterior.sln`
- `D3D12/Source/BistroExteriorD3D12.vcxproj`
- `Vulkan/Source/BistroExteriorVulkan.vcxproj`

## スクリーンショット

通常描画:

| Direct3D12 | Vulkan |
| --- | --- |
| ![BistroExterior D3D12 Final](../../docs/images/BistroExterior%20D3D12%202026_05_05%2012_29_22.png) | ![BistroExterior Vulkan Final](../../docs/images/BistroExterior%20Vulkan%202026_05_05%2012_27_21.png) |

`Bistro Controls` の ImGui ウィンドウにある `Debug View` コンボボックスで、最終描画と各種デバッグ表示を切り替えられます。D3D12 / Vulkan の見え方比較やシェーダ確認に使います。

| Debug View | Direct3D12 | Vulkan |
| --- | --- | --- |
| Base Color | ![BistroExterior D3D12 Base Color](../../docs/images/BistroExterior%20D3D12%202026_05_05%2012_29_28.png) | ![BistroExterior Vulkan Base Color](../../docs/images/BistroExterior%20Vulkan%202026_05_05%2012_27_28.png) |
| World Normal | ![BistroExterior D3D12 World Normal](../../docs/images/BistroExterior%20D3D12%202026_05_05%2012_29_35.png) | ![BistroExterior Vulkan World Normal](../../docs/images/BistroExterior%20Vulkan%202026_05_05%2012_27_37.png) |
| Normal Texture Decoded | ![BistroExterior D3D12 Normal Texture Decoded](../../docs/images/BistroExterior%20D3D12%202026_05_05%2012_29_55.png) | ![BistroExterior Vulkan Normal Texture Decoded](../../docs/images/BistroExterior%20Vulkan%202026_05_05%2012_27_46.png) |
| NdotL | ![BistroExterior D3D12 NdotL](../../docs/images/BistroExterior%20D3D12%202026_05_05%2012_30_04.png) | ![BistroExterior Vulkan NdotL](../../docs/images/BistroExterior%20Vulkan%202026_05_05%2012_28_03.png) |

## アセット

Bistro データセットはこのリポジトリには含めません。NVIDIA ORCA から Amazon Lumberyard Bistro を取得し、展開した `Bistro_v5_2` フォルダをリポジトリ直下に置いてください。

```text
CodexdRealTimeGraphicsSamples/
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
