#include "BistroExteriorPathtracingVulkan.h"

#include <stdexcept>

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int showCommand)
{
    try
    {
        BistroExteriorPathtracingVulkan app(1280, 720, L"BistroExteriorPathtracing Vulkan", BistroPathtracingMode::Pathtracing);
        return app.Run(instance, showCommand);
    }
    catch (const std::exception& error)
    {
        MessageBoxA(nullptr, error.what(), "BistroExteriorPathtracing Vulkan", MB_OK | MB_ICONERROR);
        return -1;
    }
}
