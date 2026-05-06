#include "..\..\Vulkan\Source\BistroExteriorRaytracingVulkan.h"

#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>

extern "C"
{
    __declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
    __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int showCommand)
{
    try
    {
        BistroExteriorRaytracingVulkan app(1280, 720, L"BistroExteriorRaytracing Shadow Vulkan", BistroRaytracingMode::Shadow);
        return app.Run(instance, showCommand);
    }
    catch (const std::exception& error)
    {
        OutputDebugStringA("BistroExteriorRaytracingShadowVulkan exception: ");
        OutputDebugStringA(error.what());
        OutputDebugStringA("\n");

        std::string logPath = "BistroExteriorRaytracingShadowVulkan.error.log";
        char modulePath[MAX_PATH] = {};
        DWORD modulePathLength = GetModuleFileNameA(nullptr, modulePath, MAX_PATH);
        if (modulePathLength > 0 && modulePathLength < MAX_PATH)
        {
            char* fileName = std::strrchr(modulePath, '\\');
            if (fileName != nullptr)
            {
                *(fileName + 1) = '\0';
                logPath = std::string(modulePath) + "BistroExteriorRaytracingShadowVulkan.error.log";
            }
        }

        std::ofstream log(logPath, std::ios::app);
        if (log)
        {
            log << error.what() << '\n';
        }

        MessageBoxA(nullptr, error.what(), "BistroExteriorRaytracing Shadow Vulkan", MB_OK | MB_ICONERROR);
        return -1;
    }
}

