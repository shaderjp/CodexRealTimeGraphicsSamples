#include "BistroExteriorShadowVulkan.h"

#include <stdexcept>

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int showCommand)
{
    try
    {
        BistroExteriorShadowVulkan app(1280, 720, L"BistroExterior Shadow Vulkan");
        return app.Run(instance, showCommand);
    }
    catch (const std::exception& error)
    {
        MessageBoxA(nullptr, error.what(), "BistroExterior Shadow Vulkan", MB_OK | MB_ICONERROR);
        return -1;
    }
}
