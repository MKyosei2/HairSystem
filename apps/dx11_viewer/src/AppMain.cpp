#include "GpuHairViewer.h"

#include <windows.h>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
{
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
