# Project Policy

## Repository Layout

Samples are grouped by graphics technique:

```text
Samples/
  Triangle/
    Triangle.sln
    Directory.Build.props
    Directory.Build.targets
    Assets/
    Shaders/
    D3D12/
      Source/
        TriangleD3D12.vcxproj
    Vulkan/
      Source/
        TriangleVulkan.vcxproj
  Cube3D/
    Cube3D.sln
    Directory.Build.props
    Directory.Build.targets
    Assets/
    Shaders/
    D3D12/
      Source/
        Cube3DD3D12.vcxproj
    Vulkan/
      Source/
        Cube3DVulkan.vcxproj
ThirdParty/
```

Each technique folder owns its assets and shaders. API-specific source and Visual Studio projects live under `D3D12` and `Vulkan`.

## Visual Studio Policy

- Each technique keeps its Visual Studio solution directly under `Samples/<TechniqueName>`.
- Solution files are named after the technique, such as `Samples/Triangle/Triangle.sln` and `Samples/Cube3D/Cube3D.sln`.
- Projects are hand-managed `.vcxproj` files.
- The supported platform is initially `x64`.
- The solution contains a technique solution folder, with API projects inside it.
- Keep project-local output under `Bin` and `Obj` so generated files do not mix with source files.

## Sharing Policy

The repository should favor readable API-specific samples over a large abstraction layer. Share small utilities only when they remove clear duplication, such as file loading, window setup helpers, or shader build settings.
