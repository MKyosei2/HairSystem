#include "GpuHairViewer.h"

#include <windows.h>
#include <filesystem>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
{
    wchar_t modulePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    const std::filesystem::path exeDir = std::filesystem::path(modulePath).parent_path();
    SetCurrentDirectoryW(exeDir.c_str());

    hairgpu::viewer::GpuHairViewer app;
    if (!app.Initialize(instance, showCommand))
    {
        MessageBoxW(nullptr,
            L"GpuHairViewer::Initialize returned false.\r\n\r\nStartup failed before the normal viewer loop began.\r\nA startup diagnostic dialog should already have been shown.",
            L"Hair GPU Viewer Startup Failure",
            MB_OK | MB_ICONERROR);
        return 1;
    }

    return app.Run();
}
