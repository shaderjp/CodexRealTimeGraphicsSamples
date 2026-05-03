#include "SciFiHelmetVulkan.h"

#include <stdexcept>

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int showCommand)
{
    try
    {
        SciFiHelmetVulkan app(1280, 720, L"SciFiHelmet Vulkan");
        return app.Run(instance, showCommand);
    }
    catch (const std::exception& error)
    {
        MessageBoxA(nullptr, error.what(), "SciFiHelmet Vulkan", MB_OK | MB_ICONERROR);
        return -1;
    }
}
