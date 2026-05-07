#include "..\..\Vulkan\Source\BistroExteriorPathtracingVulkan.h"

#include <stdexcept>

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int showCommand)
{
    try
    {
        BistroExteriorPathtracingVulkan app(1280, 720, L"BistroExteriorPathtracing ReSTIR GI Vulkan", BistroPathtracingMode::ReSTIR);
        return app.Run(instance, showCommand);
    }
    catch (const std::exception& error)
    {
        MessageBoxA(nullptr, error.what(), "BistroExteriorPathtracingReSTIR Vulkan", MB_OK | MB_ICONERROR);
        return -1;
    }
}
