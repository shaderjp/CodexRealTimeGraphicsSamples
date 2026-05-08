#include "BistroExteriorManyLightsVulkan.h"

#include <stdexcept>

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int showCommand)
{
    try
    {
        BistroExteriorManyLightsVulkan app(1280, 720, L"BistroExteriorManyLights Vulkan");
        return app.Run(instance, showCommand);
    }
    catch (const std::exception& error)
    {
        MessageBoxA(nullptr, error.what(), "BistroExteriorManyLights Vulkan", MB_OK | MB_ICONERROR);
        return -1;
    }
}
