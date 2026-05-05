# ImGuiLighting

英語版は [README.md](README.md) を参照してください。

`ImGuiLighting` は glTF 2.0 の SciFiHelmet アセットを読み込み、`Samples/SciFiHelmet` と同じ小さな metallic-roughness PBR シェーダで描画するサンプルです。Direct3D12 と Vulkan の両方で Dear ImGui を統合し、1 つのディレクショナルライトを実行時に編集できます。

## 内容

- 1 つの Visual Studio 2022 ソリューション内の Direct3D12 / Vulkan プロジェクト
- `ThirdParty/imgui` から Dear ImGui を直接参照
- `Directional Light` という ImGui ウィンドウ
- Direction、Color、Intensity、Reset によるディレクショナルライト編集
- glTF 2.0 SciFiHelmet のメッシュと Base Color、Metallic-Roughness、Normal、Ambient Occlusion テクスチャ
- D3D12 / Vulkan 共通の HLSL 6.6 PBR シェーダ
- Depth Buffer と depth test

## プロジェクト

- `ImGuiLighting.sln`
- `D3D12/Source/ImGuiLightingD3D12.vcxproj`
- `Vulkan/Source/ImGuiLightingVulkan.vcxproj`

## スクリーンショット

| Direct3D12 | Vulkan |
| --- | --- |
| ![ImGuiLighting D3D12](../../docs/images/ImGuiLighting%20D3D12%202026_05_05%2012_24_18.png) | ![ImGuiLighting Vulkan](../../docs/images/ImGuiLighting%20Vulkan%202026_05_05%2012_24_47.png) |

## ビルド

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" Samples\ImGuiLighting\ImGuiLighting.sln /restore /p:Platform=x64 /p:Configuration=Debug
```

Vulkan 版のビルドには Vulkan SDK と `VULKAN_SDK` が必要です。

## アセット

SciFiHelmet アセットは Khronos glTF Sample Models リポジトリから `Assets/SciFiHelmet` へ必要分だけコピーしています。元アセットの表記は `Assets/SciFiHelmet/README.md` を参照してください。
