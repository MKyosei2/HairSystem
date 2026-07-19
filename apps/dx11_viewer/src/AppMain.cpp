#include "GpuHairViewer.h"

#include <windows.h>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace
{
    bool ArgumentEquals(int index, const wchar_t* expected)
    {
        return index >= 0 && index < __argc && __wargv[index] != nullptr &&
               std::wstring(__wargv[index]) == expected;
    }
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
{
    wchar_t modulePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    const std::filesystem::path exeDir = std::filesystem::path(modulePath).parent_path();
    SetCurrentDirectoryW(exeDir.c_str());

    if (ArgumentEquals(1, L"--export-frozen-source"))
    {
        if (__argc != 3 || __wargv[2] == nullptr)
        {
            return 64;
        }
        hairgpu::viewer::GpuHairViewer app;
        return app.ExportCanonicalFrozenAssetSource(std::filesystem::path(__wargv[2]));
    }

    if (ArgumentEquals(1, L"--validate-frozen-source"))
    {
        if (__argc != 3 || __wargv[2] == nullptr)
        {
            return 64;
        }
        hairgpu::viewer::GpuHairViewer app;
        return app.ValidateCanonicalFrozenAssetSource(std::filesystem::path(__wargv[2]));
    }

    if (ArgumentEquals(1, L"--native-self-test"))
    {
        hairgpu::viewer::GpuHairViewer app;
        return app.RunNativeSelfTests();
    }

    if (ArgumentEquals(1, L"--shader-self-test"))
    {
        SetEnvironmentVariableW(L"HAIRGPU_SHADER_SELF_TEST", L"1");
        hairgpu::viewer::GpuHairViewer app;
        return app.Initialize(instance, SW_HIDE) ? 0 : 2;
    }

    hairgpu::viewer::GpuHairViewer app;
    if (!app.Initialize(instance, showCommand))
    {
        MessageBoxW(
            nullptr,
            L"GpuHairViewer::Initialize returned false.\r\n\r\n"
            L"Startup failed before the normal viewer loop began.\r\n"
            L"A startup diagnostic dialog should already have been shown.",
            L"Hair GPU Viewer Startup Failure",
            MB_OK | MB_ICONERROR);
        return 1;
    }

    return app.Run();
}
