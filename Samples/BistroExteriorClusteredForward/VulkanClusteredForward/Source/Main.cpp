#include "BistroExteriorClusteredForwardVulkan.h"

#include <stdexcept>

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int showCommand)
{
    try
    {
        BistroExteriorClusteredForwardVulkan app(1280, 720, L"BistroExteriorClusteredForward Vulkan");
        return app.Run(instance, showCommand);
    }
    catch (const std::exception& error)
    {
        MessageBoxA(nullptr, error.what(), "BistroExteriorClusteredForward Vulkan", MB_OK | MB_ICONERROR);
        return -1;
    }
}
