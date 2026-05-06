#include "BistroExteriorMeshShaderShadowCullingVulkan.h"

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
        BistroExteriorMeshShaderShadowCullingVulkan app(1280, 720, L"BistroExteriorMeshShader Shadow Culling Vulkan");
        return app.Run(instance, showCommand);
    }
    catch (const std::exception& error)
    {
        OutputDebugStringA("BistroExteriorMeshShaderShadowCullingVulkan exception: ");
        OutputDebugStringA(error.what());
        OutputDebugStringA("\n");

        std::string logPath = "BistroExteriorMeshShaderShadowCullingVulkan.error.log";
        char modulePath[MAX_PATH] = {};
        DWORD modulePathLength = GetModuleFileNameA(nullptr, modulePath, MAX_PATH);
        if (modulePathLength > 0 && modulePathLength < MAX_PATH)
        {
            char* fileName = std::strrchr(modulePath, '\\');
            if (fileName != nullptr)
            {
                *(fileName + 1) = '\0';
                logPath = std::string(modulePath) + "BistroExteriorMeshShaderShadowCullingVulkan.error.log";
            }
        }

        std::ofstream log(logPath, std::ios::app);
        if (log)
        {
            log << error.what() << '\n';
        }

        MessageBoxA(nullptr, error.what(), "BistroExteriorMeshShader Shadow Culling Vulkan", MB_OK | MB_ICONERROR);
        return -1;
    }
}
