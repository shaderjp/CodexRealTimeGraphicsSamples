#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "imgui.h"

#include <cstdint>

namespace Bistro
{
    struct ResolutionPreset
    {
        const char* label;
        uint32_t width;
        uint32_t height;
    };

    inline constexpr ResolutionPreset ResolutionPresets[] =
    {
        { "720p (1280 x 720)", 1280, 720 },
        { "1080p (1920 x 1080)", 1920, 1080 },
        { "4K (3840 x 2160)", 3840, 2160 }
    };

    inline int FindResolutionPresetIndex(uint32_t width, uint32_t height)
    {
        for (int i = 0; i < static_cast<int>(sizeof(ResolutionPresets) / sizeof(ResolutionPresets[0])); ++i)
        {
            if (ResolutionPresets[i].width == width && ResolutionPresets[i].height == height)
            {
                return i;
            }
        }
        return 1;
    }

    inline bool DrawResolutionCombo(uint32_t currentWidth, uint32_t currentHeight, uint32_t& selectedWidth, uint32_t& selectedHeight)
    {
        const char* labels[] =
        {
            ResolutionPresets[0].label,
            ResolutionPresets[1].label,
            ResolutionPresets[2].label
        };

        int selectedIndex = FindResolutionPresetIndex(currentWidth, currentHeight);
        if (!ImGui::Combo("Output Resolution", &selectedIndex, labels, static_cast<int>(sizeof(labels) / sizeof(labels[0]))))
        {
            return false;
        }

        selectedWidth = ResolutionPresets[selectedIndex].width;
        selectedHeight = ResolutionPresets[selectedIndex].height;
        return selectedWidth != currentWidth || selectedHeight != currentHeight;
    }

    inline void ResizeClientArea(HWND hwnd, uint32_t width, uint32_t height)
    {
        if (!hwnd)
        {
            return;
        }

        RECT rect = { 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
        const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_STYLE));
        const DWORD exStyle = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_EXSTYLE));
        AdjustWindowRectEx(&rect, style, FALSE, exStyle);
        SetWindowPos(
            hwnd,
            nullptr,
            0,
            0,
            rect.right - rect.left,
            rect.bottom - rect.top,
            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
}
