#include "BistroExteriorMeshShaderVulkan.h"

#include <stdexcept>

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int showCommand)
{
    try
    {
        BistroExteriorMeshShaderVulkan app(1280, 720, L"BistroExteriorMeshShader Vulkan");
        return app.Run(instance, showCommand);
    }
    catch (const std::exception& error)
    {
        MessageBoxA(nullptr, error.what(), "BistroExteriorMeshShader Vulkan", MB_OK | MB_ICONERROR);
        return -1;
    }
}
