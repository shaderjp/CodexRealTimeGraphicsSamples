#include "Cube3DVulkan.h"

#include <stdexcept>

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int showCommand)
{
    try
    {
        Cube3DVulkan app(1280, 720, L"Cube3D Vulkan");
        return app.Run(instance, showCommand);
    }
    catch (const std::exception& error)
    {
        MessageBoxA(nullptr, error.what(), "Cube3D Vulkan", MB_OK | MB_ICONERROR);
        return -1;
    }
}
