# Cube3D

英語版は [README.md](README.md) を参照してください。

`Cube3D` は Triangle の最小構成から一歩進めた 3D オブジェクト描画サンプルです。頂点バッファ、インデックスバッファ、トランスフォーム用 Constant Buffer、Depth Buffer を使って、毎フレーム回転する立方体を描画します。

## 内容

- 1 つの Visual Studio 2022 ソリューション内の Direct3D12 / Vulkan プロジェクト
- インデックス付き立方体ジオメトリ
- Constant Buffer / Uniform Buffer による毎フレームのトランスフォーム更新
- Depth Buffer と depth test
- DXC による HLSL 6.6 シェーダのビルド時コンパイル

## プロジェクト

- `Cube3D.sln`
- `D3D12/Source/Cube3DD3D12.vcxproj`
- `Vulkan/Source/Cube3DVulkan.vcxproj`

## スクリーンショット

| Direct3D12 | Vulkan |
| --- | --- |
| ![Cube3D D3D12](../../docs/images/Cube3D%20D3D12%202026_05_05%2012_20_40.png) | ![Cube3D Vulkan](../../docs/images/Cube3D%20Vulkan%202026_05_05%2012_19_58.png) |

## ビルド

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" Samples\Cube3D\Cube3D.sln /restore /p:Platform=x64 /p:Configuration=Debug
```

Vulkan 版のビルドには Vulkan SDK と `VULKAN_SDK` が必要です。
