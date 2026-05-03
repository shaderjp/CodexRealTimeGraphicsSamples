#include "TriangleVulkan.h"

#include <stdexcept>

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int showCommand)
{
    try
    {
        TriangleVulkan app(1280, 720, L"Triangle Vulkan");
        return app.Run(instance, showCommand);
    }
    catch (const std::exception& error)
    {
        MessageBoxA(nullptr, error.what(), "Triangle Vulkan", MB_OK | MB_ICONERROR);
        return -1;
    }
}
