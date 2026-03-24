#include <windows.h>
#include <commctrl.h>

#include <algorithm>
#include <chrono>
#include <cwchar>
#include <string>
#include <vector>

#include "hair/HairSimulation.h"

#pragma comment(lib, "Comctl32.lib")

namespace
{
    using hair::HairSimulation;
    using hair::SimulationConfig;
    using hair::SphereCollider;
    using hair::Strand;
    using hair::Vec3;

    constexpr wchar_t kWindowClassName[] = L"HairSolverLabWindow";
    constexpr int kPanelWidth = 320;
    constexpr UINT_PTR kFrameTimerId = 1;
    constexpr UINT kFrameTimerMs = 16;
    constexpr float kFixedTimeStep = 1.0f / 120.0f;

    enum ControlId
    {
        IdStartPause = 1001,
        IdReset = 1002,
        IdStep = 1003,

        IdSliderGravity = 1101,
        IdSliderDamping = 1102,
        IdSliderIterations = 1103,
        IdSliderBend = 1104,
        IdSliderStrands = 1105,
        IdSliderSegments = 1106,

        IdLabelGravity = 1201,
        IdLabelDamping = 1202,
        IdLabelIterations = 1203,
        IdLabelBend = 1204,
        IdLabelStrands = 1205,
        IdLabelSegments = 1206,
        IdStatus = 1207
    };

    struct UiControls
    {
        HWND startPauseButton = nullptr;
        HWND resetButton = nullptr;
        HWND stepButton = nullptr;

        HWND gravitySlider = nullptr;
        HWND dampingSlider = nullptr;
        HWND iterationsSlider = nullptr;
        HWND bendSlider = nullptr;
        HWND strandsSlider = nullptr;
        HWND segmentsSlider = nullptr;

        HWND gravityLabel = nullptr;
        HWND dampingLabel = nullptr;
        HWND iterationsLabel = nullptr;
        HWND bendLabel = nullptr;
        HWND strandsLabel = nullptr;
        HWND segmentsLabel = nullptr;
        HWND statusLabel = nullptr;
    };

    struct AppState
    {
        HairSimulation simulation;
        SimulationConfig config;
        UiControls controls;
        bool running = true;
        float accumulator = 0.0f;
        float fps = 0.0f;
        std::chrono::steady_clock::time_point previousTick = std::chrono::steady_clock::now();
        std::chrono::steady_clock::time_point previousFpsSample = std::chrono::steady_clock::now();
        int frameCounter = 0;
    };

    std::wstring Format(const wchar_t* format, double value)
    {
        wchar_t buffer[128] = {};
        std::swprintf(buffer, sizeof(buffer) / sizeof(buffer[0]), format, value);
        return std::wstring(buffer);
    }

    std::wstring FormatInt(const wchar_t* format, int value)
    {
        wchar_t buffer[128] = {};
        std::swprintf(buffer, sizeof(buffer) / sizeof(buffer[0]), format, value);
        return std::wstring(buffer);
    }

    void SetWindowTextString(HWND control, const std::wstring& text)
    {
        SetWindowTextW(control, text.c_str());
    }

    RECT GetCanvasRect(HWND window)
    {
        RECT clientRect = {};
        GetClientRect(window, &clientRect);
        clientRect.right = std::max(clientRect.left, clientRect.right - kPanelWidth);
        return clientRect;
    }

    void ApplyConfigToSimulation(AppState& app)
    {
        app.simulation.Reset(app.config);
        app.accumulator = 0.0f;
    }

    void UpdateLabels(AppState& app)
    {
        SetWindowTextString(app.controls.gravityLabel, Format(L"Gravity: %.0f", app.config.gravity));
        SetWindowTextString(app.controls.dampingLabel, Format(L"Damping: %.3f", app.config.damping));
        SetWindowTextString(app.controls.iterationsLabel, FormatInt(L"Iterations: %d", app.config.solverIterations));
        SetWindowTextString(app.controls.bendLabel, Format(L"Bend: %.2f", app.config.bendStiffness));
        SetWindowTextString(app.controls.strandsLabel, FormatInt(L"Strands: %d", app.config.strandCount));
        SetWindowTextString(app.controls.segmentsLabel, FormatInt(L"Segments: %d", app.config.segmentCount));

        wchar_t status[256] = {};
        std::swprintf(
            status,
            sizeof(status) / sizeof(status[0]),
            L"State: %s    FPS: %.1f    Nodes: %d",
            app.running ? L"Running" : L"Paused",
            app.fps,
            app.config.strandCount * app.config.segmentCount);
        SetWindowTextW(app.controls.statusLabel, status);
    }

    void SyncControlsFromConfig(AppState& app)
    {
        SendMessageW(app.controls.gravitySlider, TBM_SETPOS, TRUE, static_cast<LPARAM>(app.config.gravity));
        SendMessageW(app.controls.dampingSlider, TBM_SETPOS, TRUE, static_cast<LPARAM>(app.config.damping * 1000.0f));
        SendMessageW(app.controls.iterationsSlider, TBM_SETPOS, TRUE, static_cast<LPARAM>(app.config.solverIterations));
        SendMessageW(app.controls.bendSlider, TBM_SETPOS, TRUE, static_cast<LPARAM>(app.config.bendStiffness * 100.0f));
        SendMessageW(app.controls.strandsSlider, TBM_SETPOS, TRUE, static_cast<LPARAM>(app.config.strandCount));
        SendMessageW(app.controls.segmentsSlider, TBM_SETPOS, TRUE, static_cast<LPARAM>(app.config.segmentCount));
        UpdateLabels(app);
    }

    void PullConfigFromControls(AppState& app)
    {
        app.config.gravity = static_cast<float>(SendMessageW(app.controls.gravitySlider, TBM_GETPOS, 0, 0));
        app.config.damping = static_cast<float>(SendMessageW(app.controls.dampingSlider, TBM_GETPOS, 0, 0)) / 1000.0f;
        app.config.solverIterations = static_cast<int>(SendMessageW(app.controls.iterationsSlider, TBM_GETPOS, 0, 0));
        app.config.bendStiffness = static_cast<float>(SendMessageW(app.controls.bendSlider, TBM_GETPOS, 0, 0)) / 100.0f;
        app.config.strandCount = static_cast<int>(SendMessageW(app.controls.strandsSlider, TBM_GETPOS, 0, 0));
        app.config.segmentCount = static_cast<int>(SendMessageW(app.controls.segmentsSlider, TBM_GETPOS, 0, 0));
    }

    HWND CreateLabel(HWND parent, int x, int y, int width, int height, int id, const wchar_t* text)
    {
        return CreateWindowExW(
            0,
            L"STATIC",
            text,
            WS_CHILD | WS_VISIBLE,
            x,
            y,
            width,
            height,
            parent,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
            GetModuleHandleW(nullptr),
            nullptr);
    }

    HWND CreateTrackbar(HWND parent, int x, int y, int width, int id, int minValue, int maxValue)
    {
        HWND slider = CreateWindowExW(
            0,
            TRACKBAR_CLASSW,
            L"",
            WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS,
            x,
            y,
            width,
            34,
            parent,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
            GetModuleHandleW(nullptr),
            nullptr);

        SendMessageW(slider, TBM_SETRANGE, TRUE, MAKELONG(minValue, maxValue));
        SendMessageW(slider, TBM_SETTICFREQ, (maxValue - minValue) / 5, 0);
        return slider;
    }

    void LayoutControls(HWND window, AppState& app)
    {
        RECT clientRect = {};
        GetClientRect(window, &clientRect);

        const int panelLeft = clientRect.right - kPanelWidth + 16;
        const int panelWidth = kPanelWidth - 32;
        int y = 20;

        MoveWindow(app.controls.startPauseButton, panelLeft, y, 90, 30, TRUE);
        MoveWindow(app.controls.stepButton, panelLeft + 100, y, 80, 30, TRUE);
        MoveWindow(app.controls.resetButton, panelLeft + 190, y, 90, 30, TRUE);
        y += 50;

        const struct SliderLayout
        {
            HWND label;
            HWND slider;
        } sliders[] = {
            { app.controls.gravityLabel, app.controls.gravitySlider },
            { app.controls.dampingLabel, app.controls.dampingSlider },
            { app.controls.iterationsLabel, app.controls.iterationsSlider },
            { app.controls.bendLabel, app.controls.bendSlider },
            { app.controls.strandsLabel, app.controls.strandsSlider },
            { app.controls.segmentsLabel, app.controls.segmentsSlider }
        };

        for (const SliderLayout& sliderLayout : sliders)
        {
            MoveWindow(sliderLayout.label, panelLeft, y, panelWidth, 18, TRUE);
            y += 20;
            MoveWindow(sliderLayout.slider, panelLeft, y, panelWidth, 34, TRUE);
            y += 42;
        }

        MoveWindow(app.controls.statusLabel, panelLeft, clientRect.bottom - 60, panelWidth, 40, TRUE);
    }

    void CreateUi(HWND window, AppState& app)
    {
        app.controls.startPauseButton = CreateWindowExW(
            0,
            L"BUTTON",
            L"Pause",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0,
            0,
            0,
            0,
            window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IdStartPause)),
            GetModuleHandleW(nullptr),
            nullptr);

        app.controls.stepButton = CreateWindowExW(
            0,
            L"BUTTON",
            L"Step",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0,
            0,
            0,
            0,
            window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IdStep)),
            GetModuleHandleW(nullptr),
            nullptr);

        app.controls.resetButton = CreateWindowExW(
            0,
            L"BUTTON",
            L"Reset",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0,
            0,
            0,
            0,
            window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IdReset)),
            GetModuleHandleW(nullptr),
            nullptr);

        app.controls.gravityLabel = CreateLabel(window, 0, 0, 0, 0, IdLabelGravity, L"Gravity");
        app.controls.dampingLabel = CreateLabel(window, 0, 0, 0, 0, IdLabelDamping, L"Damping");
        app.controls.iterationsLabel = CreateLabel(window, 0, 0, 0, 0, IdLabelIterations, L"Iterations");
        app.controls.bendLabel = CreateLabel(window, 0, 0, 0, 0, IdLabelBend, L"Bend");
        app.controls.strandsLabel = CreateLabel(window, 0, 0, 0, 0, IdLabelStrands, L"Strands");
        app.controls.segmentsLabel = CreateLabel(window, 0, 0, 0, 0, IdLabelSegments, L"Segments");
        app.controls.statusLabel = CreateLabel(window, 0, 0, 0, 0, IdStatus, L"Status");

        app.controls.gravitySlider = CreateTrackbar(window, 0, 0, 0, IdSliderGravity, 200, 2800);
        app.controls.dampingSlider = CreateTrackbar(window, 0, 0, 0, IdSliderDamping, 0, 200);
        app.controls.iterationsSlider = CreateTrackbar(window, 0, 0, 0, IdSliderIterations, 1, 20);
        app.controls.bendSlider = CreateTrackbar(window, 0, 0, 0, IdSliderBend, 0, 100);
        app.controls.strandsSlider = CreateTrackbar(window, 0, 0, 0, IdSliderStrands, 1, 64);
        app.controls.segmentsSlider = CreateTrackbar(window, 0, 0, 0, IdSliderSegments, 4, 36);

        SyncControlsFromConfig(app);
        LayoutControls(window, app);
    }

    void DrawTextBlock(HDC hdc, int x, int y, const wchar_t* text, COLORREF color)
    {
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, color);
        TextOutW(hdc, x, y, text, lstrlenW(text));
    }

    void DrawScene(HDC hdc, const RECT& canvasRect, AppState& app)
    {
        const int width = std::max(1, canvasRect.right - canvasRect.left);
        const int height = std::max(1, canvasRect.bottom - canvasRect.top);

        HDC memoryDc = CreateCompatibleDC(hdc);
        HBITMAP bitmap = CreateCompatibleBitmap(hdc, width, height);
        HGDIOBJ oldBitmap = SelectObject(memoryDc, bitmap);

        HBRUSH backgroundBrush = CreateSolidBrush(RGB(20, 24, 31));
        RECT localRect = { 0, 0, width, height };
        FillRect(memoryDc, &localRect, backgroundBrush);
        DeleteObject(backgroundBrush);

        HPEN gridPen = CreatePen(PS_SOLID, 1, RGB(38, 44, 58));
        HGDIOBJ oldPen = SelectObject(memoryDc, gridPen);
        for (int x = 0; x < width; x += 40)
        {
            MoveToEx(memoryDc, x, 0, nullptr);
            LineTo(memoryDc, x, height);
        }
        for (int y = 0; y < height; y += 40)
        {
            MoveToEx(memoryDc, 0, y, nullptr);
            LineTo(memoryDc, width, y);
        }
        SelectObject(memoryDc, oldPen);
        DeleteObject(gridPen);

        const int floorY = static_cast<int>(app.config.floorY);
        HPEN floorPen = CreatePen(PS_SOLID, 2, RGB(88, 96, 110));
        oldPen = SelectObject(memoryDc, floorPen);
        MoveToEx(memoryDc, 0, floorY, nullptr);
        LineTo(memoryDc, width, floorY);
        SelectObject(memoryDc, oldPen);
        DeleteObject(floorPen);

        HBRUSH colliderBrush = CreateSolidBrush(RGB(55, 70, 96));
        HPEN colliderPen = CreatePen(PS_SOLID, 1, RGB(120, 145, 180));
        HGDIOBJ oldBrush = SelectObject(memoryDc, colliderBrush);
        oldPen = SelectObject(memoryDc, colliderPen);
        for (const SphereCollider& collider : app.simulation.GetColliders())
        {
            const int left = static_cast<int>(collider.center.x - collider.radius);
            const int top = static_cast<int>(collider.center.y - collider.radius);
            const int right = static_cast<int>(collider.center.x + collider.radius);
            const int bottom = static_cast<int>(collider.center.y + collider.radius);
            Ellipse(memoryDc, left, top, right, bottom);
        }
        SelectObject(memoryDc, oldBrush);
        SelectObject(memoryDc, oldPen);
        DeleteObject(colliderBrush);
        DeleteObject(colliderPen);

        HPEN strandPen = CreatePen(PS_SOLID, 2, RGB(245, 198, 92));
        oldPen = SelectObject(memoryDc, strandPen);
        for (const Strand& strand : app.simulation.GetStrands())
        {
            if (strand.particles.empty())
            {
                continue;
            }

            MoveToEx(
                memoryDc,
                static_cast<int>(strand.particles[0].position.x),
                static_cast<int>(strand.particles[0].position.y),
                nullptr);

            for (std::size_t i = 1; i < strand.particles.size(); ++i)
            {
                LineTo(
                    memoryDc,
                    static_cast<int>(strand.particles[i].position.x),
                    static_cast<int>(strand.particles[i].position.y));
            }
        }
        SelectObject(memoryDc, oldPen);
        DeleteObject(strandPen);

        HBRUSH rootBrush = CreateSolidBrush(RGB(255, 240, 140));
        oldBrush = SelectObject(memoryDc, rootBrush);
        for (const Strand& strand : app.simulation.GetStrands())
        {
            if (strand.particles.empty())
            {
                continue;
            }

            const int x = static_cast<int>(strand.particles.front().position.x);
            const int y = static_cast<int>(strand.particles.front().position.y);
            Ellipse(memoryDc, x - 3, y - 3, x + 3, y + 3);
        }
        SelectObject(memoryDc, oldBrush);
        DeleteObject(rootBrush);

        DrawTextBlock(memoryDc, 18, 18, L"Hair Solver Lab - Phase 1", RGB(225, 231, 240));
        DrawTextBlock(memoryDc, 18, 40, L"Verlet + distance constraints + simple bend + circle collisions", RGB(170, 182, 198));

        BitBlt(hdc, canvasRect.left, canvasRect.top, width, height, memoryDc, 0, 0, SRCCOPY);

        SelectObject(memoryDc, oldBitmap);
        DeleteObject(bitmap);
        DeleteDC(memoryDc);
    }

    void UpdateSimulation(HWND window, AppState& app)
    {
        const auto now = std::chrono::steady_clock::now();
        float deltaSeconds = std::chrono::duration<float>(now - app.previousTick).count();
        app.previousTick = now;
        deltaSeconds = std::clamp(deltaSeconds, 0.0f, 0.05f);

        if (app.running)
        {
            app.accumulator += deltaSeconds;
            int substeps = 0;
            while (app.accumulator >= kFixedTimeStep && substeps < 8)
            {
                app.simulation.UpdateConfig(app.config);
                app.simulation.Step(kFixedTimeStep);
                app.accumulator -= kFixedTimeStep;
                ++substeps;
            }
        }

        ++app.frameCounter;
        const float fpsWindow = std::chrono::duration<float>(now - app.previousFpsSample).count();
        if (fpsWindow >= 0.25f)
        {
            app.fps = static_cast<float>(app.frameCounter) / fpsWindow;
            app.frameCounter = 0;
            app.previousFpsSample = now;
            UpdateLabels(app);
        }

        InvalidateRect(window, nullptr, FALSE);
    }

    LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        AppState* app = reinterpret_cast<AppState*>(GetWindowLongPtrW(window, GWLP_USERDATA));

        switch (message)
        {
        case WM_CREATE:
        {
            CREATESTRUCTW* createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
            AppState* initialState = reinterpret_cast<AppState*>(createStruct->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(initialState));
            CreateUi(window, *initialState);
            SetTimer(window, kFrameTimerId, kFrameTimerMs, nullptr);
            return 0;
        }
        case WM_SIZE:
            if (app != nullptr)
            {
                LayoutControls(window, *app);
            }
            return 0;
        case WM_HSCROLL:
            if (app != nullptr)
            {
                PullConfigFromControls(*app);
                ApplyConfigToSimulation(*app);
                UpdateLabels(*app);
                InvalidateRect(window, nullptr, FALSE);
            }
            return 0;
        case WM_COMMAND:
            if (app != nullptr)
            {
                switch (LOWORD(wParam))
                {
                case IdStartPause:
                    app->running = !app->running;
                    SetWindowTextW(app->controls.startPauseButton, app->running ? L"Pause" : L"Start");
                    UpdateLabels(*app);
                    return 0;
                case IdReset:
                    PullConfigFromControls(*app);
                    ApplyConfigToSimulation(*app);
                    UpdateLabels(*app);
                    InvalidateRect(window, nullptr, FALSE);
                    return 0;
                case IdStep:
                    PullConfigFromControls(*app);
                    app->simulation.UpdateConfig(app->config);
                    app->simulation.Step(kFixedTimeStep);
                    UpdateLabels(*app);
                    InvalidateRect(window, nullptr, FALSE);
                    return 0;
                default:
                    break;
                }
            }
            break;
        case WM_TIMER:
            if (app != nullptr && wParam == kFrameTimerId)
            {
                UpdateSimulation(window, *app);
                return 0;
            }
            break;
        case WM_PAINT:
            if (app != nullptr)
            {
                PAINTSTRUCT paintStruct = {};
                HDC hdc = BeginPaint(window, &paintStruct);

                RECT clientRect = {};
                GetClientRect(window, &clientRect);

                HBRUSH panelBrush = CreateSolidBrush(RGB(33, 38, 48));
                RECT panelRect = { clientRect.right - kPanelWidth, clientRect.top, clientRect.right, clientRect.bottom };
                FillRect(hdc, &panelRect, panelBrush);
                DeleteObject(panelBrush);

                DrawScene(hdc, GetCanvasRect(window), *app);
                EndPaint(window, &paintStruct);
                return 0;
            }
            break;
        case WM_DESTROY:
            KillTimer(window, kFrameTimerId);
            PostQuitMessage(0);
            return 0;
        default:
            break;
        }

        return DefWindowProcW(window, message, wParam, lParam);
    }
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
{
    INITCOMMONCONTROLSEX initControls = {};
    initControls.dwSize = sizeof(initControls);
    initControls.dwICC = ICC_WIN95_CLASSES | ICC_BAR_CLASSES;
    InitCommonControlsEx(&initControls);

    AppState app;
    app.config = app.simulation.GetConfig();

    WNDCLASSEXW windowClass = {};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = kWindowClassName;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

    if (!RegisterClassExW(&windowClass))
    {
        return 0;
    }

    HWND window = CreateWindowExW(
        0,
        kWindowClassName,
        L"Hair Solver Lab - Phase 1",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        1280,
        820,
        nullptr,
        nullptr,
        instance,
        &app);

    if (window == nullptr)
    {
        return 0;
    }

    ShowWindow(window, showCommand);
    UpdateWindow(window);

    MSG message = {};
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    return static_cast<int>(message.wParam);
}
