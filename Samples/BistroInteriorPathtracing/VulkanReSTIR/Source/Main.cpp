#include "..\..\Vulkan\Source\BistroInteriorPathtracingVulkan.h"

#include <stdexcept>

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int showCommand)
{
    try
    {
        BistroInteriorPathtracingVulkan app(1280, 720, L"BistroInteriorPathtracing ReSTIR GI Vulkan", BistroPathtracingMode::ReSTIR);
        return app.Run(instance, showCommand);
    }
    catch (const std::exception& error)
    {
        MessageBoxA(nullptr, error.what(), "BistroInteriorPathtracingReSTIR Vulkan", MB_OK | MB_ICONERROR);
        return -1;
    }
}
