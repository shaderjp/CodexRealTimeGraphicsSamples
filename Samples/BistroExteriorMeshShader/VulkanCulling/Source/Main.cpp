#include "BistroExteriorMeshShaderCullingVulkan.h"

#include <stdexcept>

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int showCommand)
{
    try
    {
        BistroExteriorMeshShaderCullingVulkan app(1280, 720, L"BistroExteriorMeshShaderCulling Vulkan");
        return app.Run(instance, showCommand);
    }
    catch (const std::exception& error)
    {
        MessageBoxA(nullptr, error.what(), "BistroExteriorMeshShaderCulling Vulkan", MB_OK | MB_ICONERROR);
        return -1;
    }
}
