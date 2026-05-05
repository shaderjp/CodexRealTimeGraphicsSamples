#include "BistroExteriorVulkan.h"

#include <stdexcept>

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int showCommand)
{
    try
    {
        BistroExteriorVulkan app(1280, 720, L"BistroExterior Vulkan");
        return app.Run(instance, showCommand);
    }
    catch (const std::exception& error)
    {
        MessageBoxA(nullptr, error.what(), "BistroExterior Vulkan", MB_OK | MB_ICONERROR);
        return -1;
    }
}
