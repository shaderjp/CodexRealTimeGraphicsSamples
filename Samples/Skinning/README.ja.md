# Skinning

英語版は [README.md](README.md) を参照してください。

`Skinning` は glTF 2.0 の Cesium Man アセットを読み込み、Vertex Shader Skinning または Compute Shader Skinning で描画するサンプルです。Direct3D12 と Vulkan のプロジェクトで HLSL 6.6 シェーダとアセットデータを共有し、スキニングパイプラインの違いを比較しやすくしています。

## 内容

- 1 つの Visual Studio 2022 ソリューション内の Direct3D12 / Vulkan プロジェクト
- glTF 2.0 Cesium Man のメッシュ、スケルトン、inverse bind matrix、アニメーションデータ
- Position、Normal、Texcoord、Joint Index、Joint Weight を持つ頂点バッファ
- Joint Matrix の Constant Buffer を使った Vertex Shader Skinning
- Compute Shader で skinned vertex buffer へ書き出し、そのバッファを描画する Compute Shader Skinning
- Direct3D12 / Vulkan の storage buffer で一致する 16-byte 単位の compute vertex buffer レイアウト
- Base Color テクスチャと 1 つのディレクショナルライト
- Depth Buffer と depth test

## プロジェクト

- `Skinning.sln`
- `D3D12/Source/SkinningD3D12.vcxproj`
- `Vulkan/Source/SkinningVulkan.vcxproj`
- `D3D12Compute/Source/SkinningComputeD3D12.vcxproj`
- `VulkanCompute/Source/SkinningComputeVulkan.vcxproj`

## ビルド

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" Samples\Skinning\Skinning.sln /restore /p:Platform=x64 /p:Configuration=Debug
```

Vulkan 版のビルドには Vulkan SDK と `VULKAN_SDK` が必要です。

Compute Shader 版は同じ `SkinningCompute.hlsl` を共有します。入力頂点と出力頂点のレコードは `float4` 単位で詰め、Direct3D12 の structured buffer と Vulkan の storage buffer で同じレイアウトになるようにしています。

## アセット

Cesium Man アセットは Khronos glTF Sample Models リポジトリから `Assets/CesiumMan` へ必要分だけコピーしています。元アセットの表記は `Assets/CesiumMan/README.md` を参照してください。
