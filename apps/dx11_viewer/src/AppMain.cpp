#include "GpuHairViewer.h"

#include <windows.h>
#include <cwchar>
#include <filesystem>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
{
    wchar_t modulePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    const std::filesystem::path exeDir = std::filesystem::path(modulePath).parent_path();
    SetCurrentDirectoryW(exeDir.c_str());

    const wchar_t* commandLine = GetCommandLineW();
    if (std::wcsstr(commandLine, L"--native-self-test") != nullptr)
    {
        hairgpu::viewer::GpuHairViewer app;
        return app.RunNativeSelfTests();
    }
    if (std::wcsstr(commandLine, L"--shader-self-test") != nullptr)
    {
        SetEnvironmentVariableW(L"HAIRGPU_SHADER_SELF_TEST", L"1");
        hairgpu::viewer::GpuHairViewer app;
        return app.Initialize(instance, SW_HIDE) ? 0 : 2;
    }

    hairgpu::viewer::GpuHairViewer app;
    if (!app.Initialize(instance, showCommand))
    {
        MessageBoxW(nullptr,
  L"GpuHairViewer::Initialize returned false.

Startup failed before the normal viewer loop began.
A startup diagnostic dialog should already have been shown.",
  L"Hair GPU Viewer Startup Failure",
  MB_OK | MB_ICONERROR);
        return 1;
    }

    return app.Run();
}
