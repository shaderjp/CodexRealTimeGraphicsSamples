#include "SkinningVulkan.h"

#include <stdexcept>

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int showCommand)
{
    try
    {
        SkinningVulkan app(1280, 720, L"Skinning Vulkan");
        return app.Run(instance, showCommand);
    }
    catch (const std::exception& error)
    {
        MessageBoxA(nullptr, error.what(), "Skinning Vulkan", MB_OK | MB_ICONERROR);
        return -1;
    }
}
