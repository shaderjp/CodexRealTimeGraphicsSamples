# Triangle

英語版は [README.md](README.md) を参照してください。

`Triangle` は最小構成の描画サンプルです。1 つの Visual Studio 2022 ソリューション内に Direct3D12 と Vulkan のプロジェクトを横並びで配置しています。

## 内容

- Win32 ウィンドウ作成
- Direct3D12 のデバイス、スワップチェーン、コマンドリスト、RTV ヒープ、フェンス初期化
- Vulkan のインスタンス、サーフェス、スワップチェーン、レンダーパス、パイプライン、同期初期化
- DXC による HLSL 6.6 シェーダのビルド時コンパイル
- RGB 三角形の描画

## プロジェクト

- `Triangle.sln`
- `D3D12/Source/TriangleD3D12.vcxproj`
- `Vulkan/Source/TriangleVulkan.vcxproj`

## スクリーンショット

| Direct3D12 | Vulkan |
| --- | --- |
| ![Triangle D3D12](../../docs/images/Triangle%20D3D12%202026_05_05%2012_25_43.png) | ![Triangle Vulkan](../../docs/images/Triangle%20Vulkan%202026_05_05%2012_25_33.png) |

## ビルド

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" Samples\Triangle\Triangle.sln /restore /p:Platform=x64 /p:Configuration=Debug
```

Vulkan 版のビルドには Vulkan SDK と `VULKAN_SDK` が必要です。
