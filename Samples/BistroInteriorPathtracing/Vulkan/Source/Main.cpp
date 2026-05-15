#include "BistroInteriorPathtracingVulkan.h"

#include <stdexcept>

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int showCommand)
{
    try
    {
        BistroInteriorPathtracingVulkan app(1280, 720, L"BistroInteriorPathtracing Vulkan", BistroPathtracingMode::Pathtracing);
        return app.Run(instance, showCommand);
    }
    catch (const std::exception& error)
    {
        MessageBoxA(nullptr, error.what(), "BistroInteriorPathtracing Vulkan", MB_OK | MB_ICONERROR);
        return -1;
    }
}
