#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <wrl/client.h>

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

using Microsoft::WRL::ComPtr;

namespace
{
    template <typename T>
    constexpr T MaxValue(T a, T b)
    {
        return (a < b) ? b : a;
    }

    constexpr wchar_t kWindowClassName[] = L"HairSolverLabGpuWindow";
    constexpr UINT_PTR kFrameTimerId = 1;
    constexpr UINT kFrameTimerMs = 16;
    constexpr int kPanelWidth = 320;
    constexpr float kFixedTimeStep = 1.0f / 60.0f;
    constexpr int kSafeStartupStrands = 2000;
    constexpr int kDefaultStrands = 8000;
    constexpr int kDenseStrands = 25000;
    constexpr int kParticlesPerStrand = 9; // 8 segments
    constexpr float kSegmentLength = 14.0f;
    constexpr UINT kGridDimX = 48;
    constexpr UINT kGridDimY = 48;
    constexpr UINT kGridDimZ = 16;
    constexpr UINT kGridCellCount = kGridDimX * kGridDimY * kGridDimZ;
    constexpr UINT kGridBucketCapacity = 32;

    enum ControlId
    {
        IdStartPause = 1001,
        IdReset = 1002,
        IdDense25k = 1003,
        IdMedium8k = 1004,
        IdColorBands = 1005,
        IdPulseWind = 1006,
        IdSliderWind = 1101,
        IdSliderIterations = 1102,
        IdSliderDamping = 1103,
        IdSliderCluster = 1104,
        IdSliderRepulsion = 1105,
        IdSliderFriction = 1106,
    };

    struct Float4
    {
        float x;
        float y;
        float z;
        float w;
    };

    struct LineVertex
    {
        UINT particleIndex;
        UINT groupIndex;
    };

    struct alignas(16) SimConstants
    {
        UINT strandCount = 0;
        UINT particlesPerStrand = 0;
        UINT solverIterations = 0;
        UINT enableWindPulse = 0;
        float deltaTime = 0.0f;
        float gravity = 0.0f;
        float damping = 0.0f;
        float segmentLength = 0.0f;
        float windStrength = 0.0f;
        float windDirectionX = 0.0f;
        float windPulseFrequency = 0.0f;
        float elapsedTime = 0.0f;
        float floorY = 0.0f;
        float scalpCenterX = 0.0f;
        float scalpCenterY = 0.0f;
        float scalpRadius = 0.0f;
        float shoulderOffsetX = 0.0f;
        float shoulderCenterY = 0.0f;
        float shoulderRadius = 0.0f;
        float bendStiffness = 0.0f;
        float scalpRepel = 0.0f;
        float clusterStiffness = 0.0f;
        float neighborRepulsion = 0.0f;
        float neighborRadius = 0.0f;
        float rootNeighborRadius = 0.0f;
        UINT neighborSpan = 0;
        float selfFriction = 0.0f;
        UINT selfCollisionPasses = 1;
        UINT gridBucketCapacity = kGridBucketCapacity;
        float _padding0 = 0.0f;
    };

    struct alignas(16) RenderConstants
    {
        float scaleX = 0.0f;
        float scaleY = 0.0f;
        float biasX = 0.0f;
        float biasY = 0.0f;
        Float4 palette[6] = {};
        UINT usePalette = 0;
        float _padding1[3] = {};
    };

    struct GpuStructuredBuffer
    {
        ComPtr<ID3D11Buffer> buffer;
        ComPtr<ID3D11ShaderResourceView> srv;
        ComPtr<ID3D11UnorderedAccessView> uav;
    };

    struct UiHandles
    {
        HWND startPause = nullptr;
        HWND reset = nullptr;
        HWND dense25k = nullptr;
        HWND medium8k = nullptr;
        HWND colorBands = nullptr;
        HWND pulseWind = nullptr;
        HWND windLabel = nullptr;
        HWND iterationsLabel = nullptr;
        HWND dampingLabel = nullptr;
        HWND clusterLabel = nullptr;
        HWND repulsionLabel = nullptr;
        HWND frictionLabel = nullptr;
        HWND windSlider = nullptr;
        HWND iterationsSlider = nullptr;
        HWND dampingSlider = nullptr;
        HWND clusterSlider = nullptr;
        HWND repulsionSlider = nullptr;
        HWND frictionSlider = nullptr;
        HWND infoLabel = nullptr;
    };

    struct AppState
    {
        HWND window = nullptr;
        UiHandles ui;

        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        ComPtr<IDXGISwapChain> swapChain;
        ComPtr<ID3D11RenderTargetView> renderTargetView;
        ComPtr<ID3D11VertexShader> vertexShader;
        ComPtr<ID3D11PixelShader> pixelShader;
        ComPtr<ID3D11ComputeShader> integrateShader;
        ComPtr<ID3D11ComputeShader> distanceShader;
        ComPtr<ID3D11ComputeShader> bendShader;
        ComPtr<ID3D11ComputeShader> collisionShader;
        ComPtr<ID3D11ComputeShader> clusterShader;
        ComPtr<ID3D11ComputeShader> clearGridShader;
        ComPtr<ID3D11ComputeShader> buildGridShader;
        ComPtr<ID3D11ComputeShader> selfCollisionShader;
        ComPtr<ID3D11InputLayout> inputLayout;
        ComPtr<ID3D11Buffer> lineVertexBuffer;
        ComPtr<ID3D11Buffer> simConstantBuffer;
        ComPtr<ID3D11Buffer> renderConstantBuffer;
        GpuStructuredBuffer positions[2];
        GpuStructuredBuffer previousPositions[2];
        GpuStructuredBuffer rootPositions;
        GpuStructuredBuffer gridCellCounts;
        GpuStructuredBuffer gridCellParticles;
        GpuStructuredBuffer gridOverflowCounts;
        ComPtr<ID3D11Buffer> gridOverflowReadback;

        UINT currentBufferIndex = 0;
        UINT strandCount = kSafeStartupStrands;
        UINT particlesPerStrand = kParticlesPerStrand;
        UINT lineVertexCount = 0;
        UINT solverIterations = 2;
        float damping = 0.030f;
        float gravity = 1500.0f;
        float windStrength = 180.0f;
        float windDirectionX = 1.0f;
        float windPulseFrequency = 1.45f;
        float bendStiffness = 0.22f;
        float scalpRepel = 1.0f;
        float clusterStiffness = 0.10f;
        float neighborRepulsion = 0.12f;
        float neighborRadius = 9.0f;
        float rootNeighborRadius = 22.0f;
        UINT neighborSpan = 4;
        float selfFriction = 0.12f;
        UINT selfCollisionPasses = 0;
        UINT overflowCellCount = 0;
        UINT overflowParticleCount = 0;
        UINT frameCounter = 0;
        float elapsedTime = 0.0f;
        float cameraZoom = 1.0f;
        float cameraPanX = 20.0f;
        float cameraPanY = 20.0f;
        bool running = false;
        bool runtimeFailureShown = false;
        bool useWarpDevice = false;
        bool readyToRender = false;
        bool useColorBands = false;
        bool useWindPulse = true;
        bool draggingPan = false;
        POINT lastMouse = {};
        LARGE_INTEGER qpcFrequency = {};
        double lastSimMs = 0.0;
        double lastDrawMs = 0.0;
        int renderWidth = 1;
        int renderHeight = 1;
    };

    bool ValidateReadyForGpuWork(const AppState& app);
    void Render(AppState& app);
    bool RunFrameTickSafe(HWND hwnd, AppState& app);

    float Fract(float value)
    {
        return value - std::floor(value);
    }

    float Hash01(int value)
    {
        const float v = std::sin(static_cast<float>(value) * 12.9898f + 78.233f) * 43758.5453f;
        return Fract(v);
    }

    std::wstring ToWide(const std::string& text)
    {
        if (text.empty())
        {
            return std::wstring();
        }

        const int length = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
        if (length <= 0)
        {
            return std::wstring();
        }

        std::wstring output(static_cast<size_t>(length), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, output.data(), length);
        if (!output.empty() && output.back() == L'\0')
        {
            output.pop_back();
        }
        return output;
    }

    void ShowError(HWND window, const wchar_t* title, const std::wstring& message)
    {
        MessageBoxW(window, message.c_str(), title, MB_ICONERROR | MB_OK);
    }

    std::wstring GetRuntimeLogPath()
    {
        wchar_t tempPath[MAX_PATH] = {};
        DWORD length = GetTempPathW(static_cast<DWORD>(std::size(tempPath)), tempPath);
        if (length == 0 || length >= std::size(tempPath))
        {
            return L"HairSystem_runtime_log.txt";
        }
        std::wstring path(tempPath);
        if (!path.empty() && path.back() != L'\\')
        {
            path.push_back(L'\\');
        }
        path += L"HairSystem_runtime_log.txt";
        return path;
    }

    void AppendRuntimeLog(const std::wstring& message)
    {
        const std::wstring path = GetRuntimeLogPath();
        std::wofstream file(path.c_str(), std::ios::app);
        if (!file)
        {
            return;
        }
        SYSTEMTIME st = {};
        GetLocalTime(&st);
        file << L'['
             << st.wYear << L'-'
             << st.wMonth << L'-'
             << st.wDay << L' '
             << st.wHour << L':'
             << st.wMinute << L':'
             << st.wSecond << L"] "
             << message << L"\n";
    }

    std::wstring HrToString(HRESULT hr)
    {
        wchar_t buffer[64] = {};
        swprintf_s(buffer, L"0x%08X", static_cast<unsigned int>(hr));
        return std::wstring(buffer);
    }

    void HandleRuntimeFailure(HWND window, const wchar_t* stage, const std::wstring& details)
    {
        std::wstring message = std::wstring(L"Stage: ") + stage + L"\r\n" + details +
            L"\r\n\r\nA runtime log was written to %TEMP%\\HairSystem_runtime_log.txt";
        AppendRuntimeLog(std::wstring(L"FAILURE @ ") + stage + L": " + details);
        ShowError(window, L"HairSystem Runtime Error", message);
    }

    bool CheckHr(HWND window, HRESULT hr, const wchar_t* title)
    {
        if (SUCCEEDED(hr))
        {
            return true;
        }

        wchar_t buffer[256] = {};
        swprintf_s(buffer, L"HRESULT 0x%08X", static_cast<unsigned int>(hr));
        ShowError(window, title, buffer);
        return false;
    }

    const char* kVertexShaderSource = R"HLSL(
cbuffer RenderCB : register(b0)
{
    float scaleX;
    float scaleY;
    float biasX;
    float biasY;
    float4 palette[6];
    uint usePalette;
    float3 pad0;
};

StructuredBuffer<float4> positions : register(t0);

struct VSInput
{
    uint particleIndex : INDEX;
    uint groupIndex : GROUP;
};

struct VSOutput
{
    float4 position : SV_Position;
    float4 color : COLOR0;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    float3 p = positions[input.particleIndex].xyz;
    output.position = float4(p.x * scaleX + biasX, p.y * scaleY + biasY, 0.0f, 1.0f);
    output.color = (usePalette != 0) ? palette[input.groupIndex % 6] : float4(0.94f, 0.86f, 0.78f, 0.92f);
    return output;
}
)HLSL";

    const char* kPixelShaderSource = R"HLSL(
struct PSInput
{
    float4 position : SV_Position;
    float4 color : COLOR0;
};

float4 main(PSInput input) : SV_Target
{
    return input.color;
}
)HLSL";

    const char* kIntegrateShaderSource = R"HLSL(
cbuffer SimCB : register(b0)
{
    uint strandCount;
    uint particlesPerStrand;
    uint solverIterations;
    uint enableWindPulse;
    float deltaTime;
    float gravity;
    float damping;
    float segmentLength;
    float windStrength;
    float windDirectionX;
    float windPulseFrequency;
    float elapsedTime;
    float floorY;
    float scalpCenterX;
    float scalpCenterY;
    float scalpRadius;
    float shoulderOffsetX;
    float shoulderCenterY;
    float shoulderRadius;
    float bendStiffness;
    float scalpRepel;
    float clusterStiffness;
    float neighborRepulsion;
    float neighborRadius;
    float rootNeighborRadius;
    uint neighborSpan;
    float2 pad0;
};

StructuredBuffer<float4> inPositions : register(t0);
StructuredBuffer<float4> inPreviousPositions : register(t1);
StructuredBuffer<float4> roots : register(t2);
RWStructuredBuffer<float4> outPositions : register(u0);
RWStructuredBuffer<float4> outPreviousPositions : register(u1);

[numthreads(64, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint index = dispatchThreadId.x;
    uint totalParticles = strandCount * particlesPerStrand;
    if (index >= totalParticles)
    {
        return;
    }

    uint strandIndex = index / particlesPerStrand;
    uint particleInStrand = index % particlesPerStrand;

    float3 current = inPositions[index].xyz;
    if (particleInStrand == 0)
    {
        float3 root = roots[strandIndex].xyz;
        outPositions[index] = float4(root, 1.0f);
        outPreviousPositions[index] = float4(root, 1.0f);
        return;
    }

    float3 previous = inPreviousPositions[index].xyz;
    float3 velocity = (current - previous) * (1.0f - damping);
    float windEnvelope = 1.0f;
    if (enableWindPulse != 0)
    {
        windEnvelope = 0.35f + 0.65f * (0.5f + 0.5f * sin(elapsedTime * windPulseFrequency + roots[strandIndex].x * 0.018f + particleInStrand * 0.31f));
    }

    float depthPhase = sin(roots[strandIndex].z * 0.23f + elapsedTime * 0.8f) * 18.0f;
    float3 acceleration = float3(windDirectionX * windStrength * windEnvelope, gravity, depthPhase);
    float3 next = current + velocity + acceleration * (deltaTime * deltaTime);

    outPreviousPositions[index] = float4(current, 1.0f);
    outPositions[index] = float4(next, 1.0f);
}
)HLSL";

    const char* kDistanceShaderSource = R"HLSL(
cbuffer SimCB : register(b0)
{
    uint strandCount;
    uint particlesPerStrand;
    uint solverIterations;
    uint enableWindPulse;
    float deltaTime;
    float gravity;
    float damping;
    float segmentLength;
    float windStrength;
    float windDirectionX;
    float windPulseFrequency;
    float elapsedTime;
    float floorY;
    float scalpCenterX;
    float scalpCenterY;
    float scalpRadius;
    float shoulderOffsetX;
    float shoulderCenterY;
    float shoulderRadius;
    float bendStiffness;
    float scalpRepel;
    float clusterStiffness;
    float neighborRepulsion;
    float neighborRadius;
    float rootNeighborRadius;
    uint neighborSpan;
    float2 pad0;
};

StructuredBuffer<float4> roots : register(t0);
RWStructuredBuffer<float4> positions : register(u0);

[numthreads(64, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint strandIndex = dispatchThreadId.x;
    if (strandIndex >= strandCount)
    {
        return;
    }

    uint baseIndex = strandIndex * particlesPerStrand;
    positions[baseIndex] = float4(roots[strandIndex].xyz, 1.0f);

    [loop]
    for (uint particleIndex = 1; particleIndex < particlesPerStrand; ++particleIndex)
    {
        uint currentIndex = baseIndex + particleIndex;
        uint previousIndex = currentIndex - 1;
        float3 anchor = positions[previousIndex].xyz;
        float3 particlePos = positions[currentIndex].xyz;
        float3 delta = particlePos - anchor;
        float len = length(delta);
        float3 direction = (len > 1.0e-5f) ? delta / len : float3(0.0f, 1.0f, 0.0f);
        positions[currentIndex] = float4(anchor + direction * segmentLength, 1.0f);
    }
}
)HLSL";

    const char* kBendShaderSource = R"HLSL(
cbuffer SimCB : register(b0)
{
    uint strandCount;
    uint particlesPerStrand;
    uint solverIterations;
    uint enableWindPulse;
    float deltaTime;
    float gravity;
    float damping;
    float segmentLength;
    float windStrength;
    float windDirectionX;
    float windPulseFrequency;
    float elapsedTime;
    float floorY;
    float scalpCenterX;
    float scalpCenterY;
    float scalpRadius;
    float shoulderOffsetX;
    float shoulderCenterY;
    float shoulderRadius;
    float bendStiffness;
    float scalpRepel;
    float clusterStiffness;
    float neighborRepulsion;
    float neighborRadius;
    float rootNeighborRadius;
    uint neighborSpan;
    float2 pad0;
};

RWStructuredBuffer<float4> positions : register(u0);

[numthreads(64, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint strandIndex = dispatchThreadId.x;
    if (strandIndex >= strandCount)
    {
        return;
    }

    uint baseIndex = strandIndex * particlesPerStrand;

    [loop]
    for (uint particleIndex = 1; particleIndex + 1 < particlesPerStrand; ++particleIndex)
    {
        uint currentIndex = baseIndex + particleIndex;
        float3 previous = positions[currentIndex - 1].xyz;
        float3 current = positions[currentIndex].xyz;
        float3 next = positions[currentIndex + 1].xyz;
        float3 target = (previous + next) * 0.5f;
        positions[currentIndex] = float4(lerp(current, target, bendStiffness), 1.0f);
    }
}
)HLSL";

    

    const char* kClusterShaderSource = R"HLSL(
cbuffer SimCB : register(b0)
{
    uint strandCount;
    uint particlesPerStrand;
    uint solverIterations;
    uint enableWindPulse;
    float deltaTime;
    float gravity;
    float damping;
    float segmentLength;
    float windStrength;
    float windDirectionX;
    float windPulseFrequency;
    float elapsedTime;
    float floorY;
    float scalpCenterX;
    float scalpCenterY;
    float scalpRadius;
    float shoulderOffsetX;
    float shoulderCenterY;
    float shoulderRadius;
    float bendStiffness;
    float scalpRepel;
    float clusterStiffness;
    float neighborRepulsion;
    float neighborRadius;
    float rootNeighborRadius;
    uint neighborSpan;
    float2 pad0;
};

StructuredBuffer<float4> inPositions : register(t0);
StructuredBuffer<float4> roots : register(t1);
RWStructuredBuffer<float4> outPositions : register(u0);

[numthreads(64, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint strandIndex = dispatchThreadId.x;
    if (strandIndex >= strandCount)
    {
        return;
    }

    uint baseIndex = strandIndex * particlesPerStrand;
    float3 root0 = roots[strandIndex].xyz;
    outPositions[baseIndex] = float4(root0, 1.0f);

    [loop]
    for (uint particleIndex = 1; particleIndex < particlesPerStrand; ++particleIndex)
    {
        uint index = baseIndex + particleIndex;
        float3 current = inPositions[index].xyz;
        float3 clusterSum = current;
        float weightSum = 1.0f;
        float3 repulse = float3(0.0f, 0.0f, 0.0f);

        [loop]
        for (int offset = -int(neighborSpan); offset <= int(neighborSpan); ++offset)
        {
            if (offset == 0)
            {
                continue;
            }

            int neighborStrand = int(strandIndex) + offset;
            if (neighborStrand < 0 || neighborStrand >= int(strandCount))
            {
                continue;
            }

            float3 neighborRoot = roots[neighborStrand].xyz;
            float2 rootDeltaXY = neighborRoot.xy - root0.xy;
            if (dot(rootDeltaXY, rootDeltaXY) > rootNeighborRadius * rootNeighborRadius)
            {
                continue;
            }

            uint neighborIndex = uint(neighborStrand) * particlesPerStrand + particleIndex;
            float3 neighborPoint = inPositions[neighborIndex].xyz;
            float w = 1.0f / (1.0f + abs(float(offset)));
            clusterSum += neighborPoint * w;
            weightSum += w;

            float3 delta = current - neighborPoint;
            float dist = length(delta);
            if (dist > 1.0e-4f && dist < neighborRadius)
            {
                float push = (neighborRadius - dist) / neighborRadius;
                repulse += (delta / dist) * (push * w);
            }
        }

        float t = float(particleIndex) / max(1.0f, float(particlesPerStrand - 1));
        float3 clusterTarget = clusterSum / weightSum;
        float3 clustered = lerp(current, clusterTarget, saturate(clusterStiffness * (0.3f + 0.7f * t)));
        clustered += repulse * (neighborRepulsion * (0.35f + 0.65f * t));
        outPositions[index] = float4(clustered, 1.0f);
    }
}
)HLSL";


    const char* kClearGridShaderSource = R"HLSL(
RWStructuredBuffer<uint> cellCounts : register(u0);
RWStructuredBuffer<uint> overflowCounts : register(u1);

[numthreads(64, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    static const uint GRID_TOTAL = 48u * 48u * 16u;
    uint index = dispatchThreadId.x;
    if (index < GRID_TOTAL)
    {
        cellCounts[index] = 0u;
        overflowCounts[index] = 0u;
    }
}
)HLSL";

    const char* kBuildGridShaderSource = R"HLSL(
cbuffer SimCB : register(b0)
{
    uint strandCount;
    uint particlesPerStrand;
    uint solverIterations;
    uint enableWindPulse;
    float deltaTime;
    float gravity;
    float damping;
    float segmentLength;
    float windStrength;
    float windDirectionX;
    float windPulseFrequency;
    float elapsedTime;
    float floorY;
    float scalpCenterX;
    float scalpCenterY;
    float scalpRadius;
    float shoulderOffsetX;
    float shoulderCenterY;
    float shoulderRadius;
    float bendStiffness;
    float scalpRepel;
    float clusterStiffness;
    float neighborRepulsion;
    float neighborRadius;
    float rootNeighborRadius;
    uint neighborSpan;
    float selfFriction;
    uint selfCollisionPasses;
    uint gridBucketCapacity;
    float pad0;
};

StructuredBuffer<float4> positions : register(t0);
RWStructuredBuffer<uint> cellCounts : register(u0);
RWStructuredBuffer<uint> cellParticles : register(u1);
RWStructuredBuffer<uint> overflowCounts : register(u2);

static const int GRID_X = 48;
static const int GRID_Y = 48;
static const int GRID_Z = 16;
static const int CELL_CAPACITY = 32;
static const float3 GRID_MIN = float3(160.0f, 40.0f, -140.0f);
static const float CELL_SIZE = 18.0f;

bool InBounds(float3 p)
{
    return p.x >= GRID_MIN.x && p.y >= GRID_MIN.y && p.z >= GRID_MIN.z &&
           p.x < GRID_MIN.x + GRID_X * CELL_SIZE &&
           p.y < GRID_MIN.y + GRID_Y * CELL_SIZE &&
           p.z < GRID_MIN.z + GRID_Z * CELL_SIZE;
}

int3 CellCoords(float3 p)
{
    float3 local = (p - GRID_MIN) / CELL_SIZE;
    return clamp(int3(floor(local)), int3(0, 0, 0), int3(GRID_X - 1, GRID_Y - 1, GRID_Z - 1));
}

uint FlattenCell(int3 c)
{
    return uint(c.x + c.y * GRID_X + c.z * GRID_X * GRID_Y);
}

uint HashUint(uint x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

[numthreads(64, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint index = dispatchThreadId.x;
    uint totalParticles = strandCount * particlesPerStrand;
    if (index >= totalParticles)
    {
        return;
    }

    uint particleInStrand = index % particlesPerStrand;
    if (particleInStrand == 0u)
    {
        return;
    }

    float3 p = positions[index].xyz;
    if (!InBounds(p))
    {
        return;
    }

    uint cellIndex = FlattenCell(CellCoords(p));
    uint slot = 0u;
    InterlockedAdd(cellCounts[cellIndex], 1u, slot);
    if (slot < CELL_CAPACITY)
    {
        cellParticles[cellIndex * CELL_CAPACITY + slot] = index;
    }
    else
    {
        uint discardCount = 0u;
        InterlockedAdd(overflowCounts[cellIndex], 1u, discardCount);
        uint replaceSlot = HashUint(index + discardCount * 17u) & (CELL_CAPACITY - 1u);
        cellParticles[cellIndex * CELL_CAPACITY + replaceSlot] = index;
    }
}
)HLSL";

    const char* kSelfCollisionShaderSource = R"HLSL(
cbuffer SimCB : register(b0)
{
    uint strandCount;
    uint particlesPerStrand;
    uint solverIterations;
    uint enableWindPulse;
    float deltaTime;
    float gravity;
    float damping;
    float segmentLength;
    float windStrength;
    float windDirectionX;
    float windPulseFrequency;
    float elapsedTime;
    float floorY;
    float scalpCenterX;
    float scalpCenterY;
    float scalpRadius;
    float shoulderOffsetX;
    float shoulderCenterY;
    float shoulderRadius;
    float bendStiffness;
    float scalpRepel;
    float clusterStiffness;
    float neighborRepulsion;
    float neighborRadius;
    float rootNeighborRadius;
    uint neighborSpan;
    float selfFriction;
    uint selfCollisionPasses;
    uint gridBucketCapacity;
    float pad0;
};

StructuredBuffer<float4> inPositions : register(t0);
StructuredBuffer<float4> inPreviousPositions : register(t1);
StructuredBuffer<uint> cellCounts : register(t2);
StructuredBuffer<uint> cellParticles : register(t3);
RWStructuredBuffer<float4> outPositions : register(u0);
RWStructuredBuffer<float4> outPreviousPositions : register(u1);

static const int GRID_X = 48;
static const int GRID_Y = 48;
static const int GRID_Z = 16;
static const int CELL_CAPACITY = 32;
static const float3 GRID_MIN = float3(160.0f, 40.0f, -140.0f);
static const float CELL_SIZE = 18.0f;

bool InBounds(float3 p)
{
    return p.x >= GRID_MIN.x && p.y >= GRID_MIN.y && p.z >= GRID_MIN.z &&
           p.x < GRID_MIN.x + GRID_X * CELL_SIZE &&
           p.y < GRID_MIN.y + GRID_Y * CELL_SIZE &&
           p.z < GRID_MIN.z + GRID_Z * CELL_SIZE;
}

int3 CellCoords(float3 p)
{
    float3 local = (p - GRID_MIN) / CELL_SIZE;
    return clamp(int3(floor(local)), int3(0, 0, 0), int3(GRID_X - 1, GRID_Y - 1, GRID_Z - 1));
}

uint FlattenCell(int3 c)
{
    return uint(c.x + c.y * GRID_X + c.z * GRID_X * GRID_Y);
}

[numthreads(64, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint index = dispatchThreadId.x;
    uint totalParticles = strandCount * particlesPerStrand;
    if (index >= totalParticles)
    {
        return;
    }

    uint strandIndex = index / particlesPerStrand;
    uint particleInStrand = index % particlesPerStrand;
    float3 current = inPositions[index].xyz;
    float3 previous = inPreviousPositions[index].xyz;

    if (particleInStrand == 0u)
    {
        outPositions[index] = float4(current, 1.0f);
        outPreviousPositions[index] = float4(previous, 1.0f);
        return;
    }

    if (!InBounds(current))
    {
        outPositions[index] = float4(current, 1.0f);
        outPreviousPositions[index] = float4(previous, 1.0f);
        return;
    }

    float radius = max(neighborRadius, 1.0f);
    float radiusSq = radius * radius;
    float along = float(particleInStrand) / max(1.0f, float(particlesPerStrand - 1u));
    float3 normalCorrection = float3(0.0f, 0.0f, 0.0f);
    float3 tangentCorrection = float3(0.0f, 0.0f, 0.0f);
    float normalWeightSum = 0.0f;
    float tangentWeightSum = 0.0f;
    float3 velocity = current - previous;

    int3 center = CellCoords(current);

    [loop]
    for (int dz = -1; dz <= 1; ++dz)
    {
        [loop]
        for (int dy = -1; dy <= 1; ++dy)
        {
            [loop]
            for (int dx = -1; dx <= 1; ++dx)
            {
                int3 cell = center + int3(dx, dy, dz);
                if (cell.x < 0 || cell.y < 0 || cell.z < 0 || cell.x >= GRID_X || cell.y >= GRID_Y || cell.z >= GRID_Z)
                {
                    continue;
                }

                uint cellIndex = FlattenCell(cell);
                uint count = min(cellCounts[cellIndex], (uint)CELL_CAPACITY);

                [loop]
                for (uint i = 0u; i < count; ++i)
                {
                    uint otherIndex = cellParticles[cellIndex * CELL_CAPACITY + i];
                    if (otherIndex == index || otherIndex >= totalParticles)
                    {
                        continue;
                    }

                    uint otherStrand = otherIndex / particlesPerStrand;
                    uint otherParticle = otherIndex % particlesPerStrand;
                    if (otherStrand == strandIndex && abs(int(otherParticle) - int(particleInStrand)) <= 2)
                    {
                        continue;
                    }

                    float3 other = inPositions[otherIndex].xyz;
                    float3 delta = current - other;
                    float distSq = dot(delta, delta);
                    if (distSq <= 1.0e-6f || distSq >= radiusSq)
                    {
                        continue;
                    }

                    float dist = sqrt(distSq);
                    float3 n = delta / dist;
                    float penetration = radius - dist;
                    float w = (0.25f + 0.75f * along);
                    float stiffness = saturate(0.55f + 0.25f * float(selfCollisionPasses > 1u));
                    normalCorrection += n * (penetration * w * stiffness);
                    normalWeightSum += w;

                    float3 otherPrev = inPreviousPositions[otherIndex].xyz;
                    float3 otherVel = other - otherPrev;
                    float3 relativeVel = velocity - otherVel;
                    float3 tangentVel = relativeVel - n * dot(relativeVel, n);
                    float tangentMag = length(tangentVel);
                    if (tangentMag > 1.0e-5f)
                    {
                        float frictionW = w * saturate(penetration / radius);
                        tangentCorrection += (tangentVel / tangentMag) * min(tangentMag, penetration) * frictionW;
                        tangentWeightSum += frictionW;
                    }
                }
            }
        }
    }

    float3 pushed = current;
    float3 prevOut = previous;

    if (normalWeightSum > 0.0f)
    {
        float3 avgNormal = normalCorrection / normalWeightSum;
        float avgLen = length(avgNormal);
        float maxStep = radius * 0.35f;
        if (avgLen > maxStep)
        {
            avgNormal *= maxStep / avgLen;
        }
        float pushScale = neighborRepulsion * (0.65f + 0.35f * along);
        pushed += avgNormal * pushScale;
        prevOut += avgNormal * pushScale * 0.70f;
    }

    if (tangentWeightSum > 0.0f)
    {
        float3 avgTangent = tangentCorrection / tangentWeightSum;
        float tangentLen = length(avgTangent);
        float maxTangent = radius * 0.22f;
        if (tangentLen > maxTangent)
        {
            avgTangent *= maxTangent / tangentLen;
        }
        float frictionScale = saturate(selfFriction) * (0.25f + 0.75f * along);
        prevOut += avgTangent * frictionScale;
    }

    outPositions[index] = float4(pushed, 1.0f);
    outPreviousPositions[index] = float4(prevOut, 1.0f);
}
)HLSL";

    const char* kCollisionShaderSource = R"HLSL(
cbuffer SimCB : register(b0)
{
    uint strandCount;
    uint particlesPerStrand;
    uint solverIterations;
    uint enableWindPulse;
    float deltaTime;
    float gravity;
    float damping;
    float segmentLength;
    float windStrength;
    float windDirectionX;
    float windPulseFrequency;
    float elapsedTime;
    float floorY;
    float scalpCenterX;
    float scalpCenterY;
    float scalpRadius;
    float shoulderOffsetX;
    float shoulderCenterY;
    float shoulderRadius;
    float bendStiffness;
    float scalpRepel;
    float clusterStiffness;
    float neighborRepulsion;
    float neighborRadius;
    float rootNeighborRadius;
    uint neighborSpan;
    float2 pad0;
};

StructuredBuffer<float4> roots : register(t0);
RWStructuredBuffer<float4> positions : register(u0);

void PushOutSphere(inout float3 p, float3 center, float radius)
{
    float3 delta = p - center;
    float len = length(delta);
    if (len < radius)
    {
        float3 direction = (len > 1.0e-5f) ? delta / len : float3(0.0f, -1.0f, 0.0f);
        p = center + direction * radius;
    }
}

[numthreads(64, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint index = dispatchThreadId.x;
    uint totalParticles = strandCount * particlesPerStrand;
    if (index >= totalParticles)
    {
        return;
    }

    uint strandIndex = index / particlesPerStrand;
    uint particleInStrand = index % particlesPerStrand;
    if (particleInStrand == 0)
    {
        positions[index] = float4(roots[strandIndex].xyz, 1.0f);
        return;
    }

    float3 p = positions[index].xyz;

    if (p.y > floorY)
    {
        p.y = floorY;
    }

    float3 headCenter = float3(scalpCenterX, scalpCenterY + scalpRadius * 0.22f, 0.0f);
    PushOutSphere(p, headCenter, scalpRadius * scalpRepel);
    PushOutSphere(p, float3(scalpCenterX - shoulderOffsetX, shoulderCenterY, 0.0f), shoulderRadius);
    PushOutSphere(p, float3(scalpCenterX + shoulderOffsetX, shoulderCenterY, 0.0f), shoulderRadius);

    positions[index] = float4(p, 1.0f);
}
)HLSL";

    HRESULT CompileShader(const char* source, const char* entryPoint, const char* target, ID3DBlob** blobOut, std::string& errorsOut)
    {
        UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
    #if defined(_DEBUG)
        flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
    #else
        flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
    #endif

        ComPtr<ID3DBlob> shaderBlob;
        ComPtr<ID3DBlob> errorBlob;
        const HRESULT hr = D3DCompile(
            source,
            strlen(source),
            nullptr,
            nullptr,
            nullptr,
            entryPoint,
            target,
            flags,
            0,
            &shaderBlob,
            &errorBlob);

        if (FAILED(hr))
        {
            if (errorBlob)
            {
                errorsOut.assign(static_cast<const char*>(errorBlob->GetBufferPointer()), errorBlob->GetBufferSize());
            }
            return hr;
        }

        *blobOut = shaderBlob.Detach();
        errorsOut.clear();
        return S_OK;
    }

    bool CreateStructuredBuffer(
        ID3D11Device* device,
        UINT elementSize,
        UINT elementCount,
        const void* initialData,
        bool allowUav,
        GpuStructuredBuffer& outBuffer)
    {
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = elementSize * elementCount;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | (allowUav ? D3D11_BIND_UNORDERED_ACCESS : 0);
        desc.CPUAccessFlags = 0;
        desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        desc.StructureByteStride = elementSize;

        D3D11_SUBRESOURCE_DATA initData = {};
        initData.pSysMem = initialData;

        const HRESULT hrBuffer = device->CreateBuffer(&desc, initialData ? &initData : nullptr, &outBuffer.buffer);
        if (FAILED(hrBuffer))
        {
            return false;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
        srvDesc.BufferEx.FirstElement = 0;
        srvDesc.BufferEx.NumElements = elementCount;
        if (FAILED(device->CreateShaderResourceView(outBuffer.buffer.Get(), &srvDesc, &outBuffer.srv)))
        {
            return false;
        }

        if (allowUav)
        {
            D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
            uavDesc.Format = DXGI_FORMAT_UNKNOWN;
            uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
            uavDesc.Buffer.FirstElement = 0;
            uavDesc.Buffer.NumElements = elementCount;
            if (FAILED(device->CreateUnorderedAccessView(outBuffer.buffer.Get(), &uavDesc, &outBuffer.uav)))
            {
                return false;
            }
        }

        return true;
    }
    bool CreateStructuredReadbackBuffer(
        ID3D11Device* device,
        UINT elementSize,
        UINT elementCount,
        ComPtr<ID3D11Buffer>& outBuffer)
    {
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = elementSize * elementCount;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.BindFlags = 0;
        desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        desc.StructureByteStride = elementSize;
        return SUCCEEDED(device->CreateBuffer(&desc, nullptr, &outBuffer));
    }

    void UpdateWindowText(AppState& app)
    {
        std::wostringstream title;
        title.setf(std::ios::fixed);
        title.precision(2);
        title << L"GPU Hair Solver | "
              << (app.running ? L"RUN" : L"PAUSE")
              << L" | " << app.strandCount << L" strands x " << app.particlesPerStrand
              << L" pts | Sim " << app.lastSimMs << L" ms | Draw " << app.lastDrawMs << L" ms"
              << L" | Wind " << app.windStrength
              << L" | Iter " << app.solverIterations
              << L" | Cluster " << app.clusterStiffness
              << L" | SelfColl " << app.neighborRepulsion
              << L" | Friction " << app.selfFriction
              << L" | Wheel=Zoom  MMB=Pan";
        SetWindowTextW(app.window, title.str().c_str());

        std::wostringstream info;
        info.setf(std::ios::fixed);
        info.precision(3);
        info << L"GPU compute + GPU line rendering\r\n"
             << L"Device: " << (app.useWarpDevice ? L"WARP (software fallback)" : L"Hardware GPU") << L"\r\n"
             << L"Current preset: " << (app.strandCount >= kDenseStrands ? L"Dense 25k" : (app.strandCount >= kDefaultStrands ? L"Medium 8k" : L"Safe 2k startup")) << L"\r\n"
             << L"Visible segments: " << (app.strandCount * (app.particlesPerStrand - 1)) << L"\r\n"
             << L"Pulse wind: " << (app.useWindPulse ? L"ON" : L"OFF") << L"\r\n"
             << L"Color bands: " << (app.useColorBands ? L"ON" : L"OFF") << L"\r\n"
             << L"Bundle cluster: " << app.clusterStiffness << L" / Self collision: " << app.neighborRepulsion << L" / Friction: " << app.selfFriction << L"\r\n"
             << L"Root radius: " << app.rootNeighborRadius << L" px / Neighbor span: +/-" << app.neighborSpan << L" / Passes: " << app.selfCollisionPasses << L"\r\n"
             << L"Grid self-collision: 48x48x16 cells, bucket 32, overflow cells " << app.overflowCellCount << L", overflow particles " << app.overflowParticleCount << L"\r\n"
             << L"Recommended build: Release | x64\r\n"
             << L"Safety: starts paused, runtime log in %TEMP%\\HairSystem_runtime_log.txt";
        SetWindowTextW(app.ui.infoLabel, info.str().c_str());
    }

    void UpdateSliderLabels(const AppState& app)
    {
        wchar_t label[128] = {};
        swprintf_s(label, L"Wind Strength: %.0f", app.windStrength);
        SetWindowTextW(app.ui.windLabel, label);

        swprintf_s(label, L"Solver Iterations: %u", app.solverIterations);
        SetWindowTextW(app.ui.iterationsLabel, label);

        swprintf_s(label, L"Damping: %.3f", app.damping);
        SetWindowTextW(app.ui.dampingLabel, label);

        swprintf_s(label, L"Bundle Cluster: %.3f", app.clusterStiffness);
        SetWindowTextW(app.ui.clusterLabel, label);

        swprintf_s(label, L"Self Collision: %.3f", app.neighborRepulsion);
        SetWindowTextW(app.ui.repulsionLabel, label);

        swprintf_s(label, L"Self Friction: %.3f", app.selfFriction);
        SetWindowTextW(app.ui.frictionLabel, label);
    }

    void CreateUi(AppState& app)
    {
        HFONT uiFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        const int x = 16;
        int y = 16;
        const int width = kPanelWidth - 32;

        auto createButton = [&](const wchar_t* text, int id, DWORD style = BS_PUSHBUTTON)
        {
            HWND hwnd = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | style,
                x, y, width, 28, app.window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
            SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont), TRUE);
            y += 36;
            return hwnd;
        };

        auto createLabel = [&](const wchar_t* text, int height = 22)
        {
            HWND hwnd = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE,
                x, y, width, height, app.window, nullptr, GetModuleHandleW(nullptr), nullptr);
            SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont), TRUE);
            y += height + 4;
            return hwnd;
        };

        auto createSlider = [&](int id, int minValue, int maxValue, int value)
        {
            HWND hwnd = CreateWindowExW(0, TRACKBAR_CLASSW, L"", WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS,
                x, y, width, 30, app.window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
            SendMessageW(hwnd, TBM_SETRANGE, TRUE, MAKELPARAM(minValue, maxValue));
            SendMessageW(hwnd, TBM_SETPOS, TRUE, value);
            y += 38;
            return hwnd;
        };

        app.ui.startPause = createButton(L"Start", IdStartPause);
        app.ui.reset = createButton(L"Reset Current", IdReset);
        app.ui.dense25k = createButton(L"Dense 25k", IdDense25k);
        app.ui.medium8k = createButton(L"Medium 8k", IdMedium8k);
        app.ui.colorBands = createButton(L"Color Bands", IdColorBands, BS_AUTOCHECKBOX);
        app.ui.pulseWind = createButton(L"Pulse Wind", IdPulseWind, BS_AUTOCHECKBOX);

        SendMessageW(app.ui.pulseWind, BM_SETCHECK, BST_CHECKED, 0);

        y += 4;
        app.ui.windLabel = createLabel(L"Wind Strength:");
        app.ui.windSlider = createSlider(IdSliderWind, 0, 1000, 180);
        app.ui.iterationsLabel = createLabel(L"Solver Iterations:");
        app.ui.iterationsSlider = createSlider(IdSliderIterations, 1, 8, 2);
        app.ui.dampingLabel = createLabel(L"Damping:");
        app.ui.dampingSlider = createSlider(IdSliderDamping, 0, 80, 25);
        app.ui.clusterLabel = createLabel(L"Bundle Cluster:");
        app.ui.clusterSlider = createSlider(IdSliderCluster, 0, 1000, 100);
        app.ui.repulsionLabel = createLabel(L"Self Collision:");
        app.ui.repulsionSlider = createSlider(IdSliderRepulsion, 0, 1000, 120);
        app.ui.frictionLabel = createLabel(L"Self Friction:");
        app.ui.frictionSlider = createSlider(IdSliderFriction, 0, 1000, 120);

        y += 8;
        app.ui.infoLabel = createLabel(L"", 136);

        UpdateSliderLabels(app);
        UpdateWindowText(app);
    }

    void LayoutUi(AppState& app, int clientWidth, int clientHeight)
    {
        const int panelX = clientWidth - kPanelWidth;
        const int panelWidth = kPanelWidth;
        const int x = panelX + 16;
        int y = 16;
        const int width = panelWidth - 32;

        auto move = [&](HWND hwnd, int height)
        {
            MoveWindow(hwnd, x, y, width, height, TRUE);
            y += height + 8;
        };

        move(app.ui.startPause, 28);
        move(app.ui.reset, 28);
        move(app.ui.dense25k, 28);
        move(app.ui.medium8k, 28);
        move(app.ui.colorBands, 24);
        move(app.ui.pulseWind, 24);
        y += 8;
        move(app.ui.windLabel, 22);
        move(app.ui.windSlider, 30);
        move(app.ui.iterationsLabel, 22);
        move(app.ui.iterationsSlider, 30);
        move(app.ui.dampingLabel, 22);
        move(app.ui.dampingSlider, 30);
        move(app.ui.clusterLabel, 22);
        move(app.ui.clusterSlider, 30);
        move(app.ui.repulsionLabel, 22);
        move(app.ui.repulsionSlider, 30);
        move(app.ui.frictionLabel, 22);
        move(app.ui.frictionSlider, 30);
        y += 8;
        MoveWindow(app.ui.infoLabel, x, y, width, MaxValue(136, clientHeight - y - 16), TRUE);
    }

    bool CreateRenderTarget(AppState& app)
    {
        ComPtr<ID3D11Texture2D> backBuffer;
        if (FAILED(app.swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer))))
        {
            return false;
        }
        if (FAILED(app.device->CreateRenderTargetView(backBuffer.Get(), nullptr, &app.renderTargetView)))
        {
            return false;
        }
        return true;
    }

    bool CreateDeviceResources(AppState& app)
    {
        DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
        swapChainDesc.BufferCount = 2;
        swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapChainDesc.OutputWindow = app.window;
        swapChainDesc.SampleDesc.Count = 1;
        swapChainDesc.Windowed = TRUE;
        swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        UINT createFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    #if defined(_DEBUG)
        createFlags |= D3D11_CREATE_DEVICE_DEBUG;
    #endif

        const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0 };
        D3D_FEATURE_LEVEL createdLevel = D3D_FEATURE_LEVEL_11_0;

        auto tryCreate = [&](D3D_DRIVER_TYPE driverType, UINT flags, const wchar_t* label) -> HRESULT
        {
            app.swapChain.Reset();
            app.device.Reset();
            app.context.Reset();
            const HRESULT hrTry = D3D11CreateDeviceAndSwapChain(
                nullptr,
                driverType,
                nullptr,
                flags,
                levels,
                static_cast<UINT>(std::size(levels)),
                D3D11_SDK_VERSION,
                &swapChainDesc,
                &app.swapChain,
                &app.device,
                &createdLevel,
                &app.context);
            AppendRuntimeLog(std::wstring(L"CreateDevice attempt: ") + label + L" -> " + HrToString(hrTry));
            if (SUCCEEDED(hrTry))
            {
                app.useWarpDevice = (driverType == D3D_DRIVER_TYPE_WARP);
            }
            return hrTry;
        };

        HRESULT hr = tryCreate(D3D_DRIVER_TYPE_HARDWARE, createFlags, L"hardware/debug");
    #if defined(_DEBUG)
        if (FAILED(hr) && (createFlags & D3D11_CREATE_DEVICE_DEBUG) != 0)
        {
            hr = tryCreate(D3D_DRIVER_TYPE_HARDWARE, createFlags & ~D3D11_CREATE_DEVICE_DEBUG, L"hardware/release-flags");
        }
    #endif
        if (FAILED(hr))
        {
            hr = tryCreate(D3D_DRIVER_TYPE_WARP, D3D11_CREATE_DEVICE_BGRA_SUPPORT, L"warp/software-fallback");
        }

        if (FAILED(hr))
        {
            AppendRuntimeLog(std::wstring(L"CreateDeviceResources failed completely: ") + HrToString(hr));
            return false;
        }

        if (!CreateRenderTarget(app))
        {
            AppendRuntimeLog(L"CreateRenderTarget failed after device creation.");
            return false;
        }

        AppendRuntimeLog(std::wstring(L"CreateDeviceResources OK. Device type: ") + (app.useWarpDevice ? L"WARP" : L"Hardware"));
        return true;
    }

    bool CreatePipeline(AppState& app)
    {
        std::string errors;
        ComPtr<ID3DBlob> vsBlob;
        HRESULT hr = CompileShader(kVertexShaderSource, "main", "vs_5_0", &vsBlob, errors);
        if (FAILED(hr))
        {
            ShowError(app.window, L"Vertex Shader Compile Failed", ToWide(errors));
            return false;
        }

        ComPtr<ID3DBlob> psBlob;
        hr = CompileShader(kPixelShaderSource, "main", "ps_5_0", &psBlob, errors);
        if (FAILED(hr))
        {
            ShowError(app.window, L"Pixel Shader Compile Failed", ToWide(errors));
            return false;
        }

        ComPtr<ID3DBlob> integrateBlob;
        hr = CompileShader(kIntegrateShaderSource, "main", "cs_5_0", &integrateBlob, errors);
        if (FAILED(hr))
        {
            ShowError(app.window, L"Integrate Shader Compile Failed", ToWide(errors));
            return false;
        }

        ComPtr<ID3DBlob> distanceBlob;
        hr = CompileShader(kDistanceShaderSource, "main", "cs_5_0", &distanceBlob, errors);
        if (FAILED(hr))
        {
            ShowError(app.window, L"Distance Shader Compile Failed", ToWide(errors));
            return false;
        }

        ComPtr<ID3DBlob> bendBlob;
        hr = CompileShader(kBendShaderSource, "main", "cs_5_0", &bendBlob, errors);
        if (FAILED(hr))
        {
            ShowError(app.window, L"Bend Shader Compile Failed", ToWide(errors));
            return false;
        }

        ComPtr<ID3DBlob> collisionBlob;
        hr = CompileShader(kCollisionShaderSource, "main", "cs_5_0", &collisionBlob, errors);
        if (FAILED(hr))
        {
            ShowError(app.window, L"Collision Shader Compile Failed", ToWide(errors));
            return false;
        }

ComPtr<ID3DBlob> clusterBlob;
hr = CompileShader(kClusterShaderSource, "main", "cs_5_0", &clusterBlob, errors);
if (FAILED(hr))
{
    ShowError(app.window, L"Cluster Shader Compile Failed", ToWide(errors));
    return false;
}

ComPtr<ID3DBlob> clearGridBlob;
hr = CompileShader(kClearGridShaderSource, "main", "cs_5_0", &clearGridBlob, errors);
if (FAILED(hr))
{
    ShowError(app.window, L"Clear Grid Shader Compile Failed", ToWide(errors));
    return false;
}

ComPtr<ID3DBlob> buildGridBlob;
hr = CompileShader(kBuildGridShaderSource, "main", "cs_5_0", &buildGridBlob, errors);
if (FAILED(hr))
{
    ShowError(app.window, L"Build Grid Shader Compile Failed", ToWide(errors));
    return false;
}

ComPtr<ID3DBlob> selfCollisionBlob;
hr = CompileShader(kSelfCollisionShaderSource, "main", "cs_5_0", &selfCollisionBlob, errors);
if (FAILED(hr))
{
    ShowError(app.window, L"Self Collision Shader Compile Failed", ToWide(errors));
    return false;
}

if (FAILED(app.device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &app.vertexShader)))
{
    return false;
}
if (FAILED(app.device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &app.pixelShader)))
{
    return false;
}
if (FAILED(app.device->CreateComputeShader(integrateBlob->GetBufferPointer(), integrateBlob->GetBufferSize(), nullptr, &app.integrateShader)))
{
    return false;
}
if (FAILED(app.device->CreateComputeShader(distanceBlob->GetBufferPointer(), distanceBlob->GetBufferSize(), nullptr, &app.distanceShader)))
{
    return false;
}
if (FAILED(app.device->CreateComputeShader(bendBlob->GetBufferPointer(), bendBlob->GetBufferSize(), nullptr, &app.bendShader)))
{
    return false;
}
if (FAILED(app.device->CreateComputeShader(collisionBlob->GetBufferPointer(), collisionBlob->GetBufferSize(), nullptr, &app.collisionShader)))
{
    return false;
}
if (FAILED(app.device->CreateComputeShader(clusterBlob->GetBufferPointer(), clusterBlob->GetBufferSize(), nullptr, &app.clusterShader)))
{
    return false;
}
if (FAILED(app.device->CreateComputeShader(clearGridBlob->GetBufferPointer(), clearGridBlob->GetBufferSize(), nullptr, &app.clearGridShader)))
{
    return false;
}
if (FAILED(app.device->CreateComputeShader(buildGridBlob->GetBufferPointer(), buildGridBlob->GetBufferSize(), nullptr, &app.buildGridShader)))
{
    return false;
}
if (FAILED(app.device->CreateComputeShader(selfCollisionBlob->GetBufferPointer(), selfCollisionBlob->GetBufferSize(), nullptr, &app.selfCollisionShader)))
{
    return false;
}

        const D3D11_INPUT_ELEMENT_DESC inputElements[] =
        {
            { "INDEX", 0, DXGI_FORMAT_R32_UINT, 0, offsetof(LineVertex, particleIndex), D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "GROUP", 0, DXGI_FORMAT_R32_UINT, 0, offsetof(LineVertex, groupIndex), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };

        if (FAILED(app.device->CreateInputLayout(
            inputElements,
            static_cast<UINT>(std::size(inputElements)),
            vsBlob->GetBufferPointer(),
            vsBlob->GetBufferSize(),
            &app.inputLayout)))
        {
            return false;
        }

        D3D11_BUFFER_DESC simCbDesc = {};
        simCbDesc.ByteWidth = sizeof(SimConstants);
        simCbDesc.Usage = D3D11_USAGE_DEFAULT;
        simCbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(app.device->CreateBuffer(&simCbDesc, nullptr, &app.simConstantBuffer)))
        {
            return false;
        }

        D3D11_BUFFER_DESC renderCbDesc = {};
        renderCbDesc.ByteWidth = sizeof(RenderConstants);
        renderCbDesc.Usage = D3D11_USAGE_DEFAULT;
        renderCbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(app.device->CreateBuffer(&renderCbDesc, nullptr, &app.renderConstantBuffer)))
        {
            return false;
        }

        return true;
    }

    void BuildInitialHairData(AppState& app, std::vector<Float4>& roots, std::vector<Float4>& positions, std::vector<Float4>& previous, std::vector<LineVertex>& lineVertices)
    {
        roots.clear();
        positions.clear();
        previous.clear();
        lineVertices.clear();

        roots.reserve(app.strandCount);
        const UINT totalParticles = app.strandCount * app.particlesPerStrand;
        AppendRuntimeLog(std::wstring(L"BuildSimulationResources begin. strands=") + std::to_wstring(app.strandCount) + L", particles=" + std::to_wstring(totalParticles));
        positions.reserve(totalParticles);
        previous.reserve(totalParticles);
        lineVertices.reserve(app.strandCount * (app.particlesPerStrand - 1) * 2);

        constexpr float goldenAngle = 2.39996323f;
        const float headCenterX = 470.0f;
        const float headCenterY = 192.0f;
        const float scalpRadiusX = 84.0f;
        const float scalpRadiusY = 62.0f;
        const float scalpRadiusZ = 70.0f;
        const float crownLift = 54.0f;
        const float faceOpenZ = 30.0f;

        for (UINT strand = 0; strand < app.strandCount; ++strand)
        {
            const float u = (static_cast<float>(strand) + 0.5f) / static_cast<float>(app.strandCount);
            const float ring = std::sqrt(u);
            const float angle = goldenAngle * static_cast<float>(strand);

            float px = std::cos(angle) * scalpRadiusX * ring;
            float pz = std::sin(angle) * scalpRadiusZ * ring;

            // Put roots on a scalp cap instead of filling the whole face/front oval.
            const float radial01 = std::min(1.0f, std::sqrt((px * px) / (scalpRadiusX * scalpRadiusX) + (pz * pz) / (scalpRadiusZ * scalpRadiusZ)));
            const float crown01 = std::sqrt(std::max(0.0f, 1.0f - radial01 * radial01));
            float py = headCenterY - crownLift * crown01 + scalpRadiusY * std::pow(std::abs(px) / scalpRadiusX, 1.35f) * 0.28f;

            // Keep a face opening by pushing front-center roots toward the top/back.
            const float frontness = std::max(0.0f, pz / scalpRadiusZ);
            const float centerMask = 1.0f - std::min(1.0f, std::abs(px) / (scalpRadiusX * 0.58f));
            if (frontness > 0.15f && centerMask > 0.0f)
            {
                pz -= faceOpenZ * frontness * centerMask;
                py -= 18.0f * frontness * centerMask;
            }

            const float rootJitter = (Hash01(static_cast<int>(strand) * 17) - 0.5f) * 4.5f;
            const float rootX = headCenterX + px + rootJitter * 0.35f;
            const float rootY = py + rootJitter * 0.18f;
            const float rootZ = pz + (Hash01(static_cast<int>(strand) * 31 + 11) - 0.5f) * 6.0f;
            roots.push_back({ rootX, rootY, rootZ, 1.0f });

            const float side01 = std::min(1.0f, std::abs(px) / scalpRadiusX);
            const float forward01 = std::max(0.0f, rootZ / scalpRadiusZ);
            const float outwardX = (scalpRadiusX > 0.0f) ? (px / scalpRadiusX) : 0.0f;
            const float outwardZ = (scalpRadiusZ > 0.0f) ? (rootZ / scalpRadiusZ) : 0.0f;
            const float partSign = (rootX < headCenterX) ? -1.0f : 1.0f;
            const float partBias = 9.0f * (1.0f - side01) * (0.35f + 0.65f * forward01);
            const float strandPhase = (Hash01(static_cast<int>(strand) * 53) - 0.5f) * 10.0f;

            for (UINT particle = 0; particle < app.particlesPerStrand; ++particle)
            {
                const float s = (app.particlesPerStrand > 1) ? (static_cast<float>(particle) / static_cast<float>(app.particlesPerStrand - 1)) : 0.0f;
                const float slide = std::min(1.0f, s * 3.0f);
                const float drop = std::pow(s, 1.18f);
                const float cling = std::exp(-5.0f * s);

                // Start by following the scalp tangent a little, then let the strand fall down.
                const float x = rootX
                    + outwardX * (14.0f + 10.0f * side01) * slide * cling
                    + partSign * partBias * (0.35f + 0.65f * s)
                    + strandPhase * s * 0.22f;

                const float y = rootY
                    + 3.0f
                    + 10.0f * slide * cling
                    + kSegmentLength * static_cast<float>(app.particlesPerStrand - 1) * (0.18f * slide + 0.82f * drop);

                const float z = rootZ
                    + outwardZ * 8.0f * slide * cling
                    + (Hash01(static_cast<int>(strand) * 79 + static_cast<int>(particle) * 7) - 0.5f) * 2.0f * s;

                positions.push_back({ x, y, z, 1.0f });
                previous.push_back({ x, y, z, 1.0f });
            }

            const UINT baseIndex = strand * app.particlesPerStrand;
            const UINT group = (strand / 4) % 6;
            for (UINT particle = 0; particle + 1 < app.particlesPerStrand; ++particle)
            {
                lineVertices.push_back({ baseIndex + particle, group });
                lineVertices.push_back({ baseIndex + particle + 1, group });
            }
        }
    }

    bool BuildSimulationResources(AppState& app)
    {
        std::vector<Float4> roots;
        std::vector<Float4> positions;
        std::vector<Float4> previous;
        std::vector<LineVertex> lineVertices;
        BuildInitialHairData(app, roots, positions, previous, lineVertices);

        app.positions[0] = {};
        app.positions[1] = {};
        app.previousPositions[0] = {};
        app.previousPositions[1] = {};
        app.rootPositions = {};
        app.gridCellCounts = {};
        app.gridCellParticles = {};
        app.gridOverflowCounts = {};
        app.gridOverflowReadback.Reset();
        app.overflowCellCount = 0;
        app.overflowParticleCount = 0;
        app.frameCounter = 0;
        app.lineVertexBuffer.Reset();
        app.lineVertexCount = static_cast<UINT>(lineVertices.size());
        app.currentBufferIndex = 0;
        app.elapsedTime = 0.0f;

        const UINT totalParticles = app.strandCount * app.particlesPerStrand;
        AppendRuntimeLog(std::wstring(L"BuildSimulationResources begin. strands=") + std::to_wstring(app.strandCount) + L", particles=" + std::to_wstring(totalParticles));
        if (!CreateStructuredBuffer(app.device.Get(), sizeof(Float4), totalParticles, positions.data(), true, app.positions[0]))
        {
            return false;
        }
        if (!CreateStructuredBuffer(app.device.Get(), sizeof(Float4), totalParticles, positions.data(), true, app.positions[1]))
        {
            return false;
        }
        if (!CreateStructuredBuffer(app.device.Get(), sizeof(Float4), totalParticles, previous.data(), true, app.previousPositions[0]))
        {
            return false;
        }
        if (!CreateStructuredBuffer(app.device.Get(), sizeof(Float4), totalParticles, previous.data(), true, app.previousPositions[1]))
        {
            return false;
        }
        if (!CreateStructuredBuffer(app.device.Get(), sizeof(Float4), app.strandCount, roots.data(), false, app.rootPositions))
        {
            return false;
        }

        std::vector<UINT> zeroCounts(kGridCellCount, 0u);
        if (!CreateStructuredBuffer(app.device.Get(), sizeof(UINT), kGridCellCount, zeroCounts.data(), true, app.gridCellCounts))
        {
            return false;
        }
        if (!CreateStructuredBuffer(app.device.Get(), sizeof(UINT), kGridCellCount * kGridBucketCapacity, nullptr, true, app.gridCellParticles))
        {
            return false;
        }
        if (!CreateStructuredBuffer(app.device.Get(), sizeof(UINT), kGridCellCount, zeroCounts.data(), true, app.gridOverflowCounts))
        {
            return false;
        }
        if (!CreateStructuredReadbackBuffer(app.device.Get(), sizeof(UINT), kGridCellCount, app.gridOverflowReadback))
        {
            return false;
        }

        D3D11_BUFFER_DESC vbDesc = {};
        vbDesc.ByteWidth = static_cast<UINT>(lineVertices.size() * sizeof(LineVertex));
        vbDesc.Usage = D3D11_USAGE_IMMUTABLE;
        vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

        D3D11_SUBRESOURCE_DATA vbData = {};
        vbData.pSysMem = lineVertices.data();
        if (FAILED(app.device->CreateBuffer(&vbDesc, &vbData, &app.lineVertexBuffer)))
        {
            return false;
        }

        UpdateWindowText(app);
        AppendRuntimeLog(L"BuildSimulationResources OK.");
        return true;
    }

    void ResizeRenderTarget(AppState& app, int width, int height)
    {
        if (!app.swapChain)
        {
            return;
        }

        app.renderTargetView.Reset();
        app.context->OMSetRenderTargets(0, nullptr, nullptr);
        app.swapChain->ResizeBuffers(0, static_cast<UINT>(MaxValue(width, 1)), static_cast<UINT>(MaxValue(height, 1)), DXGI_FORMAT_UNKNOWN, 0);
        CreateRenderTarget(app);
    }

    void UpdateSimulationConstants(AppState& app, float deltaTime)
    {
        SimConstants constants = {};
        constants.strandCount = app.strandCount;
        constants.particlesPerStrand = app.particlesPerStrand;
        constants.solverIterations = app.solverIterations;
        constants.enableWindPulse = app.useWindPulse ? 1u : 0u;
        constants.deltaTime = deltaTime;
        constants.gravity = app.gravity;
        constants.damping = app.damping;
        constants.segmentLength = kSegmentLength;
        constants.windStrength = app.windStrength;
        constants.windDirectionX = app.windDirectionX;
        constants.windPulseFrequency = app.windPulseFrequency;
        constants.elapsedTime = app.elapsedTime;
        constants.floorY = 700.0f;
        constants.scalpCenterX = 470.0f;
        constants.scalpCenterY = 135.0f;
        constants.scalpRadius = 92.0f;
        constants.shoulderOffsetX = 92.0f;
        constants.shoulderCenterY = 310.0f;
        constants.shoulderRadius = 62.0f;
        constants.bendStiffness = app.bendStiffness;
        constants.scalpRepel = app.scalpRepel;
        constants.clusterStiffness = app.clusterStiffness;
        constants.neighborRepulsion = app.neighborRepulsion;
        constants.neighborRadius = app.neighborRadius;
        constants.rootNeighborRadius = app.rootNeighborRadius;
        constants.neighborSpan = app.neighborSpan;
        constants.selfFriction = app.selfFriction;
        constants.selfCollisionPasses = app.selfCollisionPasses;
        constants.gridBucketCapacity = kGridBucketCapacity;
        app.context->UpdateSubresource(app.simConstantBuffer.Get(), 0, nullptr, &constants, 0, 0);
    }

    void UnbindCompute(AppState& app)
    {
        ID3D11ShaderResourceView* nullSrvs[5] = { nullptr, nullptr, nullptr, nullptr, nullptr };
        ID3D11UnorderedAccessView* nullUavs[3] = { nullptr, nullptr, nullptr };
        app.context->CSSetShaderResources(0, 5, nullSrvs);
        app.context->CSSetUnorderedAccessViews(0, 3, nullUavs, nullptr);
        app.context->CSSetShader(nullptr, nullptr, 0);
    }

    void DispatchIntegrate(AppState& app)
    {
        const UINT src = app.currentBufferIndex;
        const UINT dst = 1u - src;
        ID3D11ShaderResourceView* srvs[] =
        {
            app.positions[src].srv.Get(),
            app.previousPositions[src].srv.Get(),
            app.rootPositions.srv.Get(),
        };
        ID3D11UnorderedAccessView* uavs[] =
        {
            app.positions[dst].uav.Get(),
            app.previousPositions[dst].uav.Get(),
        };

        app.context->CSSetShader(app.integrateShader.Get(), nullptr, 0);
        app.context->CSSetConstantBuffers(0, 1, app.simConstantBuffer.GetAddressOf());
        app.context->CSSetShaderResources(0, 3, srvs);
        app.context->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);

        const UINT totalParticles = app.strandCount * app.particlesPerStrand;
        AppendRuntimeLog(std::wstring(L"BuildSimulationResources begin. strands=") + std::to_wstring(app.strandCount) + L", particles=" + std::to_wstring(totalParticles));
        app.context->Dispatch((totalParticles + 63u) / 64u, 1, 1);

        UnbindCompute(app);
        app.currentBufferIndex = dst;
    }

    void DispatchDistance(AppState& app)
    {
        ID3D11ShaderResourceView* srvs[] = { app.rootPositions.srv.Get() };
        ID3D11UnorderedAccessView* uavs[] = { app.positions[app.currentBufferIndex].uav.Get() };
        app.context->CSSetShader(app.distanceShader.Get(), nullptr, 0);
        app.context->CSSetConstantBuffers(0, 1, app.simConstantBuffer.GetAddressOf());
        app.context->CSSetShaderResources(0, 1, srvs);
        app.context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
        app.context->Dispatch((app.strandCount + 63u) / 64u, 1, 1);
        UnbindCompute(app);
    }

    void DispatchBend(AppState& app)
    {
        ID3D11UnorderedAccessView* uavs[] = { app.positions[app.currentBufferIndex].uav.Get() };
        app.context->CSSetShader(app.bendShader.Get(), nullptr, 0);
        app.context->CSSetConstantBuffers(0, 1, app.simConstantBuffer.GetAddressOf());
        app.context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
        app.context->Dispatch((app.strandCount + 63u) / 64u, 1, 1);
        UnbindCompute(app);
    }

    void DispatchCollisions(AppState& app)
    {
        ID3D11ShaderResourceView* srvs[] = { app.rootPositions.srv.Get() };
        ID3D11UnorderedAccessView* uavs[] = { app.positions[app.currentBufferIndex].uav.Get() };
        app.context->CSSetShader(app.collisionShader.Get(), nullptr, 0);
        app.context->CSSetConstantBuffers(0, 1, app.simConstantBuffer.GetAddressOf());
        app.context->CSSetShaderResources(0, 1, srvs);
        app.context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
        const UINT totalParticles = app.strandCount * app.particlesPerStrand;
        AppendRuntimeLog(std::wstring(L"BuildSimulationResources begin. strands=") + std::to_wstring(app.strandCount) + L", particles=" + std::to_wstring(totalParticles));
        app.context->Dispatch((totalParticles + 63u) / 64u, 1, 1);
        UnbindCompute(app);
    }


    void DispatchClusterRepulsion(AppState& app)
    {
        const UINT scratch = 1u - app.currentBufferIndex;
        ID3D11ShaderResourceView* srvs[] =
        {
            app.positions[app.currentBufferIndex].srv.Get(),
            app.rootPositions.srv.Get(),
        };
        ID3D11UnorderedAccessView* uavs[] = { app.positions[scratch].uav.Get() };
        app.context->CSSetShader(app.clusterShader.Get(), nullptr, 0);
        app.context->CSSetConstantBuffers(0, 1, app.simConstantBuffer.GetAddressOf());
        app.context->CSSetShaderResources(0, 2, srvs);
        app.context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
        app.context->Dispatch((app.strandCount + 63u) / 64u, 1, 1);
        UnbindCompute(app);
        app.context->CopyResource(app.positions[app.currentBufferIndex].buffer.Get(), app.positions[scratch].buffer.Get());
    }



void DispatchClearGrid(AppState& app)
{
    ID3D11UnorderedAccessView* uavs[] = { app.gridCellCounts.uav.Get(), app.gridOverflowCounts.uav.Get() };
    app.context->CSSetShader(app.clearGridShader.Get(), nullptr, 0);
    app.context->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);
    app.context->Dispatch((kGridCellCount + 63u) / 64u, 1, 1);
    UnbindCompute(app);
}

void DispatchBuildGrid(AppState& app)
{
    ID3D11ShaderResourceView* srvs[] = { app.positions[app.currentBufferIndex].srv.Get() };
    ID3D11UnorderedAccessView* uavs[] =
    {
        app.gridCellCounts.uav.Get(),
        app.gridCellParticles.uav.Get(),
        app.gridOverflowCounts.uav.Get(),
    };
    app.context->CSSetShader(app.buildGridShader.Get(), nullptr, 0);
    app.context->CSSetConstantBuffers(0, 1, app.simConstantBuffer.GetAddressOf());
    app.context->CSSetShaderResources(0, 1, srvs);
    app.context->CSSetUnorderedAccessViews(0, 3, uavs, nullptr);
    const UINT totalParticles = app.strandCount * app.particlesPerStrand;
    app.context->Dispatch((totalParticles + 63u) / 64u, 1, 1);
    UnbindCompute(app);
}

void DispatchSelfCollision(AppState& app)
{
    const UINT scratch = 1u - app.currentBufferIndex;
    ID3D11ShaderResourceView* srvs[] =
    {
        app.positions[app.currentBufferIndex].srv.Get(),
        app.previousPositions[app.currentBufferIndex].srv.Get(),
        app.gridCellCounts.srv.Get(),
        app.gridCellParticles.srv.Get(),
    };
    ID3D11UnorderedAccessView* uavs[] = { app.positions[scratch].uav.Get(), app.previousPositions[scratch].uav.Get() };
    app.context->CSSetShader(app.selfCollisionShader.Get(), nullptr, 0);
    app.context->CSSetConstantBuffers(0, 1, app.simConstantBuffer.GetAddressOf());
    app.context->CSSetShaderResources(0, 4, srvs);
    app.context->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);
    const UINT totalParticles = app.strandCount * app.particlesPerStrand;
    app.context->Dispatch((totalParticles + 63u) / 64u, 1, 1);
    UnbindCompute(app);
    app.currentBufferIndex = scratch;
}

    bool CheckDeviceHealth(AppState& app, const wchar_t* stage)
    {
        if (!app.device)
        {
            return false;
        }

        const HRESULT reason = app.device->GetDeviceRemovedReason();
        if (reason == S_OK)
        {
            return true;
        }

        if (!app.runtimeFailureShown)
        {
            app.runtimeFailureShown = true;
            app.running = false;
            HandleRuntimeFailure(app.window, stage, std::wstring(L"D3D11 device removed/reset. HRESULT ") + HrToString(reason));
        }
        return false;
    }

    void ReadGridOverflowStats(AppState& app)
    {
        if (!app.gridOverflowReadback)
        {
            return;
        }

        app.context->CopyResource(app.gridOverflowReadback.Get(), app.gridOverflowCounts.buffer.Get());
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (SUCCEEDED(app.context->Map(app.gridOverflowReadback.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
        {
            const UINT* data = static_cast<const UINT*>(mapped.pData);
            UINT cells = 0;
            UINT particles = 0;
            for (UINT i = 0; i < kGridCellCount; ++i)
            {
                if (data[i] > 0u)
                {
                    ++cells;
                    particles += data[i];
                }
            }
            app.overflowCellCount = cells;
            app.overflowParticleCount = particles;
            app.context->Unmap(app.gridOverflowReadback.Get(), 0);
        }
    }

    void StepSimulation(AppState& app, float deltaTime)
    {
        if (!app.readyToRender || !ValidateReadyForGpuWork(app))
        {
            return;
        }

        app.elapsedTime += deltaTime;
        UpdateSimulationConstants(app, deltaTime);

        LARGE_INTEGER t0 = {};
        LARGE_INTEGER t1 = {};
        QueryPerformanceCounter(&t0);

        DispatchIntegrate(app);
        for (UINT iteration = 0; iteration < app.solverIterations; ++iteration)
        {
            DispatchDistance(app);
            DispatchBend(app);
            DispatchCollisions(app);
        }
        DispatchClusterRepulsion(app);
        for (UINT pass = 0; pass < app.selfCollisionPasses; ++pass)
        {
            DispatchClearGrid(app);
            DispatchBuildGrid(app);
            DispatchSelfCollision(app);
            DispatchDistance(app);
            DispatchCollisions(app);
        }
        DispatchBend(app);
        ++app.frameCounter;
        if ((app.frameCounter % 15u) == 0u)
        {
            ReadGridOverflowStats(app);
        }

        QueryPerformanceCounter(&t1);
        app.lastSimMs = (static_cast<double>(t1.QuadPart - t0.QuadPart) * 1000.0) / static_cast<double>(app.qpcFrequency.QuadPart);
        CheckDeviceHealth(app, L"StepSimulation");
    }

    void UpdateRenderConstants(AppState& app)
    {
        RenderConstants constants = {};
        constants.scaleX = 2.0f * app.cameraZoom / static_cast<float>(MaxValue(app.renderWidth, 1));
        constants.scaleY = -2.0f * app.cameraZoom / static_cast<float>(MaxValue(app.renderHeight, 1));
        constants.biasX = (2.0f * app.cameraPanX / static_cast<float>(MaxValue(app.renderWidth, 1))) - 1.0f;
        constants.biasY = 1.0f - (2.0f * app.cameraPanY / static_cast<float>(MaxValue(app.renderHeight, 1)));
        constants.palette[0] = { 0.95f, 0.65f, 0.52f, 0.90f };
        constants.palette[1] = { 0.93f, 0.82f, 0.53f, 0.90f };
        constants.palette[2] = { 0.73f, 0.90f, 0.58f, 0.90f };
        constants.palette[3] = { 0.47f, 0.85f, 0.91f, 0.90f };
        constants.palette[4] = { 0.69f, 0.66f, 0.96f, 0.90f };
        constants.palette[5] = { 0.96f, 0.63f, 0.89f, 0.90f };
        constants.usePalette = app.useColorBands ? 1u : 0u;
        app.context->UpdateSubresource(app.renderConstantBuffer.Get(), 0, nullptr, &constants, 0, 0);
    }

    bool ValidateReadyForGpuWork(const AppState& app)
    {
        return app.device && app.context && app.swapChain && app.renderTargetView && app.vertexShader && app.pixelShader &&
            app.integrateShader && app.distanceShader && app.bendShader && app.collisionShader && app.clusterShader &&
            app.clearGridShader && app.buildGridShader && app.selfCollisionShader && app.inputLayout && app.lineVertexBuffer &&
            app.simConstantBuffer && app.renderConstantBuffer && app.positions[0].buffer && app.positions[1].buffer &&
            app.previousPositions[0].buffer && app.previousPositions[1].buffer && app.rootPositions.buffer;
    }

    void Render(AppState& app)
    {
        if (!app.readyToRender || !ValidateReadyForGpuWork(app))
        {
            return;
        }

        LARGE_INTEGER t0 = {};
        LARGE_INTEGER t1 = {};
        QueryPerformanceCounter(&t0);

        const float clearColor[] = { 0.08f, 0.09f, 0.12f, 1.0f };
        app.context->OMSetRenderTargets(1, app.renderTargetView.GetAddressOf(), nullptr);
        app.context->ClearRenderTargetView(app.renderTargetView.Get(), clearColor);

        D3D11_VIEWPORT viewport = {};
        viewport.TopLeftX = 0.0f;
        viewport.TopLeftY = 0.0f;
        viewport.Width = static_cast<float>(MaxValue(app.renderWidth, 1));
        viewport.Height = static_cast<float>(MaxValue(app.renderHeight, 1));
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;
        app.context->RSSetViewports(1, &viewport);

        UpdateRenderConstants(app);

        const UINT stride = sizeof(LineVertex);
        const UINT offset = 0;
        app.context->IASetInputLayout(app.inputLayout.Get());
        app.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
        app.context->IASetVertexBuffers(0, 1, app.lineVertexBuffer.GetAddressOf(), &stride, &offset);
        app.context->VSSetShader(app.vertexShader.Get(), nullptr, 0);
        app.context->PSSetShader(app.pixelShader.Get(), nullptr, 0);
        app.context->VSSetConstantBuffers(0, 1, app.renderConstantBuffer.GetAddressOf());
        app.context->VSSetShaderResources(0, 1, app.positions[app.currentBufferIndex].srv.GetAddressOf());
        app.context->Draw(app.lineVertexCount, 0);

        ID3D11ShaderResourceView* nullSrv = nullptr;
        app.context->VSSetShaderResources(0, 1, &nullSrv);
        const HRESULT presentHr = app.swapChain->Present(1, 0);
        if (FAILED(presentHr) && !app.runtimeFailureShown)
        {
            app.runtimeFailureShown = true;
            app.running = false;
            HandleRuntimeFailure(app.window, L"Present", std::wstring(L"SwapChain::Present failed. HRESULT ") + HrToString(presentHr));
        }
        CheckDeviceHealth(app, L"Render");

        QueryPerformanceCounter(&t1);
        app.lastDrawMs = (static_cast<double>(t1.QuadPart - t0.QuadPart) * 1000.0) / static_cast<double>(app.qpcFrequency.QuadPart);
        UpdateWindowText(app);
    }

    void ResetSimulation(AppState& app, UINT strandCount)
    {
        app.strandCount = strandCount;
        BuildSimulationResources(app);
        UpdateSliderLabels(app);
        UpdateWindowText(app);
    }

    void SyncUiState(AppState& app)
    {
        SetWindowTextW(app.ui.startPause, app.running ? L"Pause" : L"Start");
        SendMessageW(app.ui.colorBands, BM_SETCHECK, app.useColorBands ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(app.ui.pulseWind, BM_SETCHECK, app.useWindPulse ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(app.ui.windSlider, TBM_SETPOS, TRUE, static_cast<LPARAM>(std::clamp(static_cast<int>(app.windStrength), 0, 1000)));
        SendMessageW(app.ui.iterationsSlider, TBM_SETPOS, TRUE, static_cast<LPARAM>(std::clamp(static_cast<int>(app.solverIterations), 1, 8)));
        SendMessageW(app.ui.dampingSlider, TBM_SETPOS, TRUE, static_cast<LPARAM>(std::clamp(static_cast<int>(app.damping * 1000.0f), 0, 80)));
        SendMessageW(app.ui.clusterSlider, TBM_SETPOS, TRUE, static_cast<LPARAM>(std::clamp(static_cast<int>(app.clusterStiffness * 1000.0f), 0, 1000)));
        SendMessageW(app.ui.repulsionSlider, TBM_SETPOS, TRUE, static_cast<LPARAM>(std::clamp(static_cast<int>(app.neighborRepulsion * 1000.0f), 0, 1000)));
        SendMessageW(app.ui.frictionSlider, TBM_SETPOS, TRUE, static_cast<LPARAM>(std::clamp(static_cast<int>(app.selfFriction * 1000.0f), 0, 1000)));
        UpdateSliderLabels(app);
        UpdateWindowText(app);
    }

    bool RunFrameTickSafe(HWND hwnd, AppState& app)
    {
        if (app.running)
        {
            StepSimulation(app, kFixedTimeStep);
        }
        Render(app);
        return true;
    }

    LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        AppState* app = reinterpret_cast<AppState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

        switch (message)
        {
        case WM_CREATE:
        {
            CREATESTRUCTW* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            app = reinterpret_cast<AppState*>(create->lpCreateParams);
            app->window = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));

            AppendRuntimeLog(L"WM_CREATE begin.");
            if (!CreateDeviceResources(*app))
            {
                ShowError(hwnd, L"Direct3D 11 Error", L"Failed to initialize Direct3D 11 / Shader Model 5. Please run on a supported GPU.");
                return -1;
            }
            if (!CreatePipeline(*app))
            {
                AppendRuntimeLog(L"CreatePipeline failed.");
                ShowError(hwnd, L"Shader Error", L"Failed to create GPU shaders.");
                return -1;
            }
            AppendRuntimeLog(L"CreatePipeline OK.");
            QueryPerformanceFrequency(&app->qpcFrequency);

            CreateUi(*app);
            RECT clientRect = {};
            GetClientRect(hwnd, &clientRect);
            app->renderWidth = MaxValue(1, static_cast<int>((clientRect.right - clientRect.left) - kPanelWidth));
            app->renderHeight = MaxValue(1, static_cast<int>(clientRect.bottom - clientRect.top));
            LayoutUi(*app, clientRect.right - clientRect.left, clientRect.bottom - clientRect.top);
            if (!BuildSimulationResources(*app))
            {
                AppendRuntimeLog(L"Initial BuildSimulationResources failed.");
                ShowError(hwnd, L"GPU Resource Error", L"Failed to create GPU hair buffers.");
                return -1;
            }
            app->readyToRender = true;
            AppendRuntimeLog(L"Startup completed. Solver starts paused for safety. Press Start to simulate.");
            SyncUiState(*app);
            SetTimer(hwnd, kFrameTimerId, kFrameTimerMs, nullptr);
            return 0;
        }
        case WM_SIZE:
            if (app && app->swapChain)
            {
                const int clientWidth = LOWORD(lParam);
                const int clientHeight = HIWORD(lParam);
                app->renderWidth = MaxValue(1, static_cast<int>(clientWidth - kPanelWidth));
                app->renderHeight = MaxValue(1, static_cast<int>(clientHeight));
                ResizeRenderTarget(*app, clientWidth, clientHeight);
                LayoutUi(*app, clientWidth, clientHeight);
            }
            return 0;
        case WM_COMMAND:
            if (!app)
            {
                break;
            }

            switch (LOWORD(wParam))
            {
            case IdStartPause:
                app->running = !app->running;
                SyncUiState(*app);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            case IdReset:
                ResetSimulation(*app, app->strandCount);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            case IdDense25k:
                ResetSimulation(*app, kDenseStrands);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            case IdMedium8k:
                ResetSimulation(*app, kDefaultStrands);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            case IdColorBands:
                app->useColorBands = (SendMessageW(app->ui.colorBands, BM_GETCHECK, 0, 0) == BST_CHECKED);
                SyncUiState(*app);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            case IdPulseWind:
                app->useWindPulse = (SendMessageW(app->ui.pulseWind, BM_GETCHECK, 0, 0) == BST_CHECKED);
                SyncUiState(*app);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            default:
                break;
            }
            break;
        case WM_HSCROLL:
            if (!app)
            {
                break;
            }
            if (reinterpret_cast<HWND>(lParam) == app->ui.windSlider)
            {
                app->windStrength = static_cast<float>(SendMessageW(app->ui.windSlider, TBM_GETPOS, 0, 0));
            }
            else if (reinterpret_cast<HWND>(lParam) == app->ui.iterationsSlider)
            {
                app->solverIterations = static_cast<UINT>(SendMessageW(app->ui.iterationsSlider, TBM_GETPOS, 0, 0));
            }
            else if (reinterpret_cast<HWND>(lParam) == app->ui.dampingSlider)
            {
                app->damping = static_cast<float>(SendMessageW(app->ui.dampingSlider, TBM_GETPOS, 0, 0)) / 1000.0f;
            }
            else if (reinterpret_cast<HWND>(lParam) == app->ui.clusterSlider)
            {
                app->clusterStiffness = static_cast<float>(SendMessageW(app->ui.clusterSlider, TBM_GETPOS, 0, 0)) / 1000.0f;
            }
            else if (reinterpret_cast<HWND>(lParam) == app->ui.repulsionSlider)
            {
                app->neighborRepulsion = static_cast<float>(SendMessageW(app->ui.repulsionSlider, TBM_GETPOS, 0, 0)) / 1000.0f;
            }
            else if (reinterpret_cast<HWND>(lParam) == app->ui.frictionSlider)
            {
                app->selfFriction = static_cast<float>(SendMessageW(app->ui.frictionSlider, TBM_GETPOS, 0, 0)) / 1000.0f;
            }
            UpdateSliderLabels(*app);
            UpdateWindowText(*app);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_MOUSEWHEEL:
            if (app)
            {
                const short delta = GET_WHEEL_DELTA_WPARAM(wParam);
                if (delta > 0)
                {
                    app->cameraZoom *= 1.1f;
                }
                else if (delta < 0)
                {
                    app->cameraZoom /= 1.1f;
                }
                app->cameraZoom = std::clamp(app->cameraZoom, 0.2f, 4.5f);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_MBUTTONDOWN:
            if (app)
            {
                app->draggingPan = true;
                app->lastMouse.x = GET_X_LPARAM(lParam);
                app->lastMouse.y = GET_Y_LPARAM(lParam);
                SetCapture(hwnd);
            }
            return 0;
        case WM_MBUTTONUP:
            if (app)
            {
                app->draggingPan = false;
                ReleaseCapture();
            }
            return 0;
        case WM_MOUSEMOVE:
            if (app && app->draggingPan)
            {
                const int x = GET_X_LPARAM(lParam);
                const int y = GET_Y_LPARAM(lParam);
                app->cameraPanX += static_cast<float>(x - app->lastMouse.x);
                app->cameraPanY += static_cast<float>(y - app->lastMouse.y);
                app->lastMouse.x = x;
                app->lastMouse.y = y;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_TIMER:
            if (app && wParam == kFrameTimerId)
            {
                RunFrameTickSafe(hwnd, *app);
            }
            return 0;
        case WM_PAINT:
            if (app)
            {
                PAINTSTRUCT paint = {};
                BeginPaint(hwnd, &paint);
                EndPaint(hwnd, &paint);
                return 0;
            }
            break;
        case WM_DESTROY:
            if (app)
            {
                app->readyToRender = false;
            }
            KillTimer(hwnd, kFrameTimerId);
            PostQuitMessage(0);
            return 0;
        default:
            break;
        }

        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
{
    INITCOMMONCONTROLSEX commonControls = {};
    commonControls.dwSize = sizeof(commonControls);
    commonControls.dwICC = ICC_BAR_CLASSES;
    InitCommonControlsEx(&commonControls);

    AppState app = {};

    WNDCLASSEXW windowClass = {};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszClassName = kWindowClassName;
    RegisterClassExW(&windowClass);

    HWND window = CreateWindowExW(
        0,
        kWindowClassName,
        L"GPU Hair Solver",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        1440,
        920,
        nullptr,
        nullptr,
        instance,
        &app);

    if (!window)
    {
        return -1;
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
