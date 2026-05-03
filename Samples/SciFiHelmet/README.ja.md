# SciFiHelmet

英語版は [README.md](README.md) を参照してください。

`SciFiHelmet` は glTF 2.0 の SciFiHelmet アセットを読み込み、metallic-roughness ベースの小さな PBR シェーダで描画するサンプルです。Direct3D12 と Vulkan の実装を横並びにし、リソースバインディング、テクスチャアップロード、シェーダコンパイルの違いを比較しやすくしています。

## 内容

- 1 つの Visual Studio 2022 ソリューション内の Direct3D12 / Vulkan プロジェクト
- サンプル配下の glTF 2.0 SciFiHelmet メッシュデータ読み込み
- Base Color、Metallic-Roughness、Normal、Ambient Occlusion テクスチャ
- D3D12 / Vulkan 共通の HLSL 6.6 PBR シェーダ
- 1 つのディレクショナルライト
- Depth Buffer と depth test

## プロジェクト

- `SciFiHelmet.sln`
- `D3D12/Source/SciFiHelmetD3D12.vcxproj`
- `Vulkan/Source/SciFiHelmetVulkan.vcxproj`

## ビルド

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" Samples\SciFiHelmet\SciFiHelmet.sln /restore /p:Platform=x64 /p:Configuration=Debug
```

Vulkan 版のビルドには Vulkan SDK と `VULKAN_SDK` が必要です。

## アセット

SciFiHelmet アセットは Khronos glTF Sample Models リポジトリから `Assets/SciFiHelmet` へ必要分だけコピーしています。元アセットの表記は `Assets/SciFiHelmet/README.md` を参照してください。
