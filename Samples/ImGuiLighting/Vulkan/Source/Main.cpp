#include "ImGuiLightingVulkan.h"

#include <stdexcept>

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int showCommand)
{
    try
    {
        ImGuiLightingVulkan app(1280, 720, L"ImGuiLighting Vulkan");
        return app.Run(instance, showCommand);
    }
    catch (const std::exception& error)
    {
        MessageBoxA(nullptr, error.what(), "ImGuiLighting Vulkan", MB_OK | MB_ICONERROR);
        return -1;
    }
}
