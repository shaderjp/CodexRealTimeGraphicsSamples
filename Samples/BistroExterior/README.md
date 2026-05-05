# BistroExterior

Japanese documentation is available in [README.ja.md](README.ja.md).

`BistroExterior` loads Amazon Lumberyard Bistro Exterior at runtime with Assimp and renders it with Direct3D12 or Vulkan. The sample focuses on a large scene path: FBX import, DDS/TGA texture loading through DirectXTex, material descriptors, depth testing, and an FPS-style camera for walking through the street.

## Features

- Direct3D12 and Vulkan projects in one Visual Studio 2022 solution
- Runtime `BistroExterior.fbx` import through `ThirdParty/assimp`
- DDS/TGA texture loading through `ThirdParty/DirectXTex`
- Shared HLSL 6.6 PBR-style shader compiled with DXC for both APIs
- Directional light plus fixed ambient lighting
- Packed Bistro specular map handling: `R=AO`, `G=Roughness`, `B=Metalness`
- Alpha mask support for cutout materials
- WASD camera movement, `Q/E` down/up, hold `Shift` for fast movement, hold right mouse button for mouse look

## Projects

- `BistroExterior.sln`
- `D3D12/Source/BistroExteriorD3D12.vcxproj`
- `Vulkan/Source/BistroExteriorVulkan.vcxproj`

## Screenshots

Final render:

| Direct3D12 | Vulkan |
| --- | --- |
| ![BistroExterior D3D12 Final](../../docs/images/BistroExterior%20D3D12%202026_05_05%2018_46_41.png) | ![BistroExterior Vulkan Final](../../docs/images/BistroExterior%20Vulkan%202026_05_05%2018_45_55.png) |

The `Bistro Controls` ImGui window exposes a `Debug View` combo box. It can switch the renderer from the final shaded image to intermediate views used for API comparison and shader debugging. The `Renderer Stats` window shows FPS, frame time, draw calls, vertex/index counts, primitive counts, texture counts, and normal-map diagnostics.

| Debug View | Direct3D12 | Vulkan |
| --- | --- | --- |
| Base Color | ![BistroExterior D3D12 Base Color](../../docs/images/BistroExterior%20D3D12%202026_05_05%2018_42_36.png) | ![BistroExterior Vulkan Base Color](../../docs/images/BistroExterior%20Vulkan%202026_05_05%2018_46_02.png) |
| World Normal | ![BistroExterior D3D12 World Normal](../../docs/images/BistroExterior%20D3D12%202026_05_05%2018_47_06.png) | ![BistroExterior Vulkan World Normal](../../docs/images/BistroExterior%20Vulkan%202026_05_05%2018_46_07.png) |
| Normal Texture Decoded | ![BistroExterior D3D12 Normal Texture Decoded](../../docs/images/BistroExterior%20D3D12%202026_05_05%2018_46_58.png) | ![BistroExterior Vulkan Normal Texture Decoded](../../docs/images/BistroExterior%20Vulkan%202026_05_05%2018_46_17.png) |

## Assets

The Bistro dataset is not stored in this repository. Download Amazon Lumberyard Bistro from NVIDIA ORCA and place the extracted `Bistro_v5_2` folder at the repository root:

```text
CodexRealTimeGraphicsSamples/
  Bistro_v5_2/
    BistroExterior.fbx
    Textures/
```

Alternatively, set `BISTRO_ASSET_ROOT` to the folder that directly contains `BistroExterior.fbx`.

Attribution: Amazon Lumberyard Bistro is distributed by NVIDIA ORCA and is licensed under Creative Commons Attribution 4.0 International (CC BY 4.0). `san_giuseppe_bridge_4k.hdr` and `BistroExterior.pyscene` may be present in the dataset, but this first version does not use them.

## Build

Initialize submodules first:

```powershell
git submodule update --init --recursive
```

Then build with Visual Studio 2022 or MSBuild:

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" Samples\BistroExterior\BistroExterior.sln /restore /p:Platform=x64 /p:Configuration=Debug
```

The MSBuild projects configure and build Assimp as a static library before compiling the sample. Vulkan builds require the Vulkan SDK and `VULKAN_SDK`.
