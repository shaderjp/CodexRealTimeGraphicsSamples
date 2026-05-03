# CodexdRealTimeGraphicsSamples

Direct3D12 and Vulkan real-time graphics samples for Windows and Visual Studio 2022.

Japanese documentation is available in [README.ja.md](README.ja.md).

The repository is organized by graphics technique under `Samples`. Each technique owns its shared assets and shaders, with API-specific Visual Studio projects placed side by side so the Direct3D12 and Vulkan implementations can be compared directly.

## Requirements

- Windows 10/11
- Visual Studio 2022 with the Desktop development with C++ workload
- Windows SDK with Direct3D 12 headers and libraries
- Vulkan SDK, with `VULKAN_SDK` set
- Git submodules initialized

Direct3D12 samples use the DirectX 12 Agility SDK through the `Microsoft.Direct3D.D3D12` NuGet package. Vulkan samples use the Vulkan SDK `dxc` for HLSL-to-SPIR-V compilation. The initial shader target is HLSL Shader Model 6.6.

## Setup

```powershell
git submodule update --init --recursive
```

Open `Samples/Triangle/Triangle.sln` in Visual Studio 2022 and build the desired sample project for `x64`.

Command-line builds can restore NuGet packages through MSBuild:

```powershell
Push-Location Samples\Triangle
& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" Triangle.sln /t:Restore
Pop-Location
```

The sample folders each contain an API-neutral solution and side-by-side Direct3D12/Vulkan projects. Current samples:

- `Samples/Triangle/Triangle.sln`
- `Samples/Triangle/README.md`
- `Samples/Triangle/D3D12/Source/TriangleD3D12.vcxproj`
- `Samples/Triangle/Vulkan/Source/TriangleVulkan.vcxproj`
- `Samples/Cube3D/Cube3D.sln`
- `Samples/Cube3D/README.md`
- `Samples/Cube3D/D3D12/Source/Cube3DD3D12.vcxproj`
- `Samples/Cube3D/Vulkan/Source/Cube3DVulkan.vcxproj`
- `Samples/SciFiHelmet/SciFiHelmet.sln`
- `Samples/SciFiHelmet/README.md`
- `Samples/SciFiHelmet/D3D12/Source/SciFiHelmetD3D12.vcxproj`
- `Samples/SciFiHelmet/Vulkan/Source/SciFiHelmetVulkan.vcxproj`

`Samples/SciFiHelmet` loads the glTF 2.0 SciFiHelmet asset and renders it with a small metallic-roughness PBR shader, normal mapping, ambient occlusion, and one directional light.

## Adding A Sample

Create a new folder under `Samples/<TechniqueName>` with this shape:

```text
Samples/<TechniqueName>/
  <TechniqueName>.sln
  Directory.Build.props
  Directory.Build.targets
  Assets/
  Shaders/
  D3D12/
    Source/
      <TechniqueName>D3D12.vcxproj
  Vulkan/
    Source/
      <TechniqueName>Vulkan.vcxproj
```

Add API projects to the solution under a solution folder named after the technique. Shared HLSL should live in the technique's `Shaders` folder unless the shader is API-specific.

## Third Party

Third-party dependencies are managed as Git submodules under `ThirdParty`.

- `ThirdParty/imgui`
- `ThirdParty/DirectXTex`

See `docs/third_party.md` for dependency notes.
