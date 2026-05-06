#include "..\..\Vulkan\Source\BistroExteriorRaytracingVulkan.h"

#include <stdexcept>

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int showCommand)
{
    try
    {
        BistroExteriorRaytracingVulkan app(1280, 720, L"BistroExteriorRaytracing GI Vulkan", BistroRaytracingMode::GI);
        return app.Run(instance, showCommand);
    }
    catch (const std::exception& error)
    {
        MessageBoxA(nullptr, error.what(), "BistroExteriorRaytracingGI Vulkan", MB_OK | MB_ICONERROR);
        return -1;
    }
}

