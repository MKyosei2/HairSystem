#include "GpuHairViewer.h"

#include <windowsx.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <sstream>

#include "hairgpu/Math.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

using Microsoft::WRL::ComPtr;

namespace hairgpu::viewer
{
    namespace
    {
        constexpr wchar_t kWindowClassName[] = L"HairGpuViewerWindow";

        float Clamp(float value, float minValue, float maxValue)
        {
            return std::max(minValue, std::min(value, maxValue));
        }

        UINT DispatchGroups(UINT itemCount, UINT groupSize)
        {
            return (itemCount + groupSize - 1u) / groupSize;
        }

        std::wstring HrHex(HRESULT hr)
        {
            std::wstringstream ss;
            ss << L"0x" << std::uppercase << std::hex << static_cast<unsigned long>(hr);
            return ss.str();
        }

        std::wstring Win32Hex(DWORD value)
        {
            std::wstringstream ss;
            ss << L"0x" << std::uppercase << std::hex << static_cast<unsigned long>(value);
            return ss.str();
        }

        void ShowStartupDialog(HWND owner, const wchar_t* title, const std::wstring& message)
        {
            MessageBoxW(owner, message.c_str(), title, MB_OK | MB_ICONERROR);
        }
    }

    bool GpuHairViewer::Initialize(HINSTANCE instance, int showCommand)
    {
        m_asset = BuildSampleHybridHairAsset(320, 10, 16, 0.040f);
        m_simConfig.solverIterations = 6;
        m_simConfig.fixedTimeStep = 1.0f / 120.0f;
        m_simConfig.gravity = 19.0f;
        m_simConfig.globalDamping = 0.035f;
        m_simConfig.segmentLength = 0.040f;
        m_simConfig.bendStiffness = 0.14f;
        m_simConfig.followStiffness = 0.94f;
        m_simConfig.windStrength = 0.42f;
        m_simConfig.windDirection = { 0.90f, 0.0f, 0.25f };
        m_simConfig.floorY = -1.05f;
        m_simConfig.selfCollisionRadius = 0.012f;
        m_simConfig.selfCollisionStiffness = 0.35f;
        m_simConfig.stretchCompliance = 2.0e-7f;
        m_simConfig.bendCompliance = 1.5e-5f;
        m_simConfig.rootStiffness = 0.18f;
        m_simConfig.velocityShockPropagation = 0.72f;
        m_simConfig.selfCollisionGridDimX = 18;
        m_simConfig.selfCollisionGridDimY = 18;
        m_simConfig.selfCollisionGridDimZ = 18;
        m_simConfig.selfCollisionCellCapacity = 24;
        m_simConfig.selfCollisionGridOrigin = { -0.72f, 0.48f, -0.72f };
        m_simConfig.selfCollisionCellSize = 0.085f;
        m_selfCollisionRadius = m_simConfig.selfCollisionRadius;
        m_selfCollisionStiffness = m_simConfig.selfCollisionStiffness;

        m_colliders.spheres.push_back({ { 0.0f, 1.66f, 0.0f }, 0.215f });
        m_colliders.capsules.push_back({ { -0.19f, 1.36f, 0.02f }, 0.11f, { -0.42f, 1.15f, 0.04f }, 0.08f });
        m_colliders.capsules.push_back({ {  0.19f, 1.36f, 0.02f }, 0.11f, {  0.42f, 1.15f, 0.04f }, 0.08f });
        m_colliders.capsules.push_back({ {  0.00f, 1.44f, -0.08f }, 0.14f, {  0.00f, 0.92f, -0.10f }, 0.16f });

        if (!CreateWindowAndDevice(instance, showCommand))
        {
            return false;
        }

        if (!CreateSimulationResources())
        {
            ShowStartupDialog(m_hwnd, L"Hair GPU Viewer Startup Failure",
                L"CreateSimulationResources() failed.\r\nThe viewer window was created, but GPU simulation buffers were not initialized.");
            Shutdown();
            return false;
        }

        if (!CreateRenderResources())
        {
            ShowStartupDialog(m_hwnd, L"Hair GPU Viewer Startup Failure",
                L"CreateRenderResources() failed.\r\nThe viewer window was created, but render resources were not initialized.");
            Shutdown();
            return false;
        }

        if (!LoadShaders())
        {
            if (m_hwnd)
            {
                ShowStartupDialog(m_hwnd, L"Hair GPU Viewer Startup Failure",
                    L"LoadShaders() failed.\r\nCheck the shader file paths and HLSL compile error dialog.");
            }
            Shutdown();
            return false;
        }

        ResetSimulation();
        QueryPerformanceFrequency(&m_qpcFrequency);
        QueryPerformanceCounter(&m_lastTick);
        UpdateWindowTitle();
        return true;
    }

    int GpuHairViewer::Run()
    {
        MSG msg = {};
        while (m_running)
        {
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                if (msg.message == WM_QUIT)
                {
                    m_running = false;
                    break;
                }

                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }

            LARGE_INTEGER now = {};
            QueryPerformanceCounter(&now);
            const float dt = static_cast<float>(now.QuadPart - m_lastTick.QuadPart) / static_cast<float>(m_qpcFrequency.QuadPart);
            m_lastTick = now;

            if (!m_paused)
            {
                m_accumulator += std::min(dt, 0.1f);
                while (m_accumulator >= m_simConfig.fixedTimeStep)
                {
                    StepSimulation(m_simConfig.fixedTimeStep);
                    m_accumulator -= m_simConfig.fixedTimeStep;
                    m_elapsedSeconds += m_simConfig.fixedTimeStep;
                }
            }

            UpdateCamera();
            Render();
        }

        Shutdown();
        return 0;
    }

    bool GpuHairViewer::CreateWindowAndDevice(HINSTANCE instance, int showCommand)
    {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = instance;
        wc.lpszClassName = kWindowClassName;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        {
            const DWORD err = GetLastError();
            ShowStartupDialog(nullptr, L"Hair GPU Viewer Startup Failure",
                std::wstring(L"RegisterClassExW failed. Win32 error=") + Win32Hex(err));
            return false;
        }

        RECT rect = { 0, 0, static_cast<LONG>(m_width), static_cast<LONG>(m_height) };
        AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);

        m_hwnd = CreateWindowExW(
            0,
            kWindowClassName,
            L"Hair GPU Viewer",
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT,
            rect.right - rect.left,
            rect.bottom - rect.top,
            nullptr,
            nullptr,
            instance,
            this);

        if (!m_hwnd)
        {
            const DWORD err = GetLastError();
            ShowStartupDialog(nullptr, L"Hair GPU Viewer Startup Failure",
                std::wstring(L"CreateWindowExW failed. Win32 error=") + Win32Hex(err));
            return false;
        }

        ShowWindow(m_hwnd, showCommand);
        UpdateWindow(m_hwnd);

        DXGI_SWAP_CHAIN_DESC swapDesc = {};
        swapDesc.BufferCount = 2;
        swapDesc.BufferDesc.Width = m_width;
        swapDesc.BufferDesc.Height = m_height;
        swapDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapDesc.OutputWindow = m_hwnd;
        swapDesc.SampleDesc.Count = 1;
        swapDesc.Windowed = TRUE;
        swapDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        UINT debugFlags = 0;
    #if defined(_DEBUG)
        debugFlags |= D3D11_CREATE_DEVICE_DEBUG;
    #endif

        auto tryCreate = [&](D3D_DRIVER_TYPE driverType, UINT flags, HRESULT& outHr) -> bool
        {
            D3D_FEATURE_LEVEL requested = D3D_FEATURE_LEVEL_11_0;
            outHr = D3D11CreateDeviceAndSwapChain(
                nullptr,
                driverType,
                nullptr,
                flags,
                &requested,
                1,
                D3D11_SDK_VERSION,
                &swapDesc,
                &m_swapChain,
                &m_device,
                nullptr,
                &m_context);
            return SUCCEEDED(outHr);
        };

        HRESULT hrHardwareDebug = E_FAIL;
        HRESULT hrHardware = E_FAIL;
        HRESULT hrWarp = E_FAIL;

        if (!(tryCreate(D3D_DRIVER_TYPE_HARDWARE, debugFlags, hrHardwareDebug) ||
              tryCreate(D3D_DRIVER_TYPE_HARDWARE, 0u, hrHardware) ||
              tryCreate(D3D_DRIVER_TYPE_WARP, 0u, hrWarp)))
        {
            std::wstring message =
                std::wstring(L"D3D11CreateDeviceAndSwapChain failed.\r\n\r\n") +
                L"Hardware + debug: " + HrHex(hrHardwareDebug) + L"\r\n" +
                L"Hardware: " + HrHex(hrHardware) + L"\r\n" +
                L"WARP fallback: " + HrHex(hrWarp) + L"\r\n\r\n" +
                L"Try one of these:\r\n" +
                L"- build/run Release\r\n" +
                L"- install Windows Optional Feature: Graphics Tools\r\n" +
                L"- use x64 configuration if available";
            ShowStartupDialog(m_hwnd, L"Hair GPU Viewer Startup Failure", message);
            return false;
        }

        Resize(m_width, m_height);
        return true;
    }

    bool GpuHairViewer::CreateSimulationResources()
    {
        m_initialParticles.clear();
        m_followGpuData.clear();
        m_restScalpParticles.clear();
        m_animatedScalpParticles.clear();
        m_scalpTriangles.clear();
        m_rootBindingGpuData.clear();
        m_zeroDistanceLambdas.clear();
        m_zeroBendLambdas.clear();

        m_initialParticles.reserve(m_asset.restPoints.size());
        m_followGpuData.reserve(m_asset.followData.size());
        m_restScalpParticles.reserve(m_asset.scalpMesh.positions.size());
        m_animatedScalpParticles.reserve(m_asset.scalpMesh.positions.size());
        m_scalpTriangles.reserve(m_asset.scalpMesh.triangles.size());
        m_rootBindingGpuData.reserve(m_asset.rootBindings.size());

        for (const Vec3& p : m_asset.restPoints)
        {
            m_initialParticles.push_back({ p.x, p.y, p.z, 1.0f });
        }

        for (const FollowData& f : m_asset.followData)
        {
            m_followGpuData.push_back({ f.parentGuide, f.followWeight, f.clumpWeight, f.noiseWeight });
        }

        m_renderData = BuildSegmentRenderData(m_asset);

        for (const Vec3& p : m_asset.scalpMesh.positions)
        {
            m_restScalpParticles.push_back({ p.x, p.y, p.z, 1.0f });
            m_animatedScalpParticles.push_back({ p.x, p.y, p.z, 1.0f });
        }

        for (const TriangleIndices& tri : m_asset.scalpMesh.triangles)
        {
            m_scalpTriangles.push_back({ tri.a, tri.b, tri.c, 0u });
        }

        for (const RootBinding& binding : m_asset.rootBindings)
        {
            RootBindingGpuData gpu = {};
            gpu.triangleIndex = binding.triangleIndex;
            gpu.barycentric[0] = binding.barycentric.x;
            gpu.barycentric[1] = binding.barycentric.y;
            gpu.barycentric[2] = binding.barycentric.z;
            gpu.rootUv[0] = binding.rootUv.x;
            gpu.rootUv[1] = binding.rootUv.y;
            m_rootBindingGpuData.push_back(gpu);
        }

        const UINT totalParticles = static_cast<UINT>(m_initialParticles.size());
        const UINT guideSegments = m_asset.guideStrandCount * (m_asset.particlesPerStrand > 0 ? (m_asset.particlesPerStrand - 1u) : 0u);
        const UINT guideBends = m_asset.guideStrandCount * (m_asset.particlesPerStrand > 2 ? (m_asset.particlesPerStrand - 2u) : 0u);
        m_zeroDistanceLambdas.resize(std::max<UINT>(guideSegments, 1u), 0.0f);
        m_zeroBendLambdas.resize(std::max<UINT>(guideBends, 1u), 0.0f);

        m_positions[0] = CreateStructuredBuffer(sizeof(GpuParticle), totalParticles, m_initialParticles.data(), true);
        m_positions[1] = CreateStructuredBuffer(sizeof(GpuParticle), totalParticles, m_initialParticles.data(), true);
        m_previousPositions[0] = CreateStructuredBuffer(sizeof(GpuParticle), totalParticles, m_initialParticles.data(), true);
        m_previousPositions[1] = CreateStructuredBuffer(sizeof(GpuParticle), totalParticles, m_initialParticles.data(), true);
        m_restPositions = CreateStructuredBuffer(sizeof(GpuParticle), totalParticles, m_initialParticles.data(), false);
        m_pointWidths = CreateStructuredBuffer(sizeof(float), static_cast<UINT>(m_asset.pointWidths.size()), m_asset.pointWidths.data(), false);
        m_followBuffer = CreateStructuredBuffer(sizeof(FollowGpuData), static_cast<UINT>(m_followGpuData.size()), m_followGpuData.data(), false);

        m_scalpPositions = CreateStructuredBuffer(sizeof(GpuParticle), static_cast<UINT>(m_animatedScalpParticles.size()), m_animatedScalpParticles.data(), false);
        m_scalpTrianglesBuffer = CreateStructuredBuffer(sizeof(TriangleGpuData), static_cast<UINT>(m_scalpTriangles.size()), m_scalpTriangles.data(), false);
        m_rootBindingsBuffer = CreateStructuredBuffer(sizeof(RootBindingGpuData), static_cast<UINT>(m_rootBindingGpuData.size()), m_rootBindingGpuData.data(), false);
        m_distanceLambdaBuffer = CreateStructuredBuffer(sizeof(float), static_cast<UINT>(m_zeroDistanceLambdas.size()), m_zeroDistanceLambdas.data(), true);
        m_bendLambdaBuffer = CreateStructuredBuffer(sizeof(float), static_cast<UINT>(m_zeroBendLambdas.size()), m_zeroBendLambdas.data(), true);

        const UINT gridCellCount = std::max<UINT>(m_simConfig.selfCollisionGridDimX * m_simConfig.selfCollisionGridDimY * m_simConfig.selfCollisionGridDimZ, 1u);
        std::vector<uint32_t> zeroCellCounts(gridCellCount, 0u);
        std::vector<uint32_t> zeroCellIndices(std::max<UINT>(gridCellCount * m_simConfig.selfCollisionCellCapacity, 1u), 0xFFFFFFFFu);
        m_gridCellCountsBuffer = CreateStructuredBuffer(sizeof(uint32_t), gridCellCount, zeroCellCounts.data(), true);
        m_gridCellIndicesBuffer = CreateStructuredBuffer(sizeof(uint32_t), static_cast<UINT>(zeroCellIndices.size()), zeroCellIndices.data(), true);

        D3D11_BUFFER_DESC vbDesc = {};
        vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        vbDesc.ByteWidth = static_cast<UINT>(m_renderData.segmentVertices.size() * sizeof(SegmentRenderVertex));
        vbDesc.Usage = D3D11_USAGE_IMMUTABLE;
        D3D11_SUBRESOURCE_DATA vbData = {};
        vbData.pSysMem = m_renderData.segmentVertices.data();
        if (FAILED(m_device->CreateBuffer(&vbDesc, &vbData, &m_segmentVertexBuffer)))
        {
            return false;
        }

        D3D11_BUFFER_DESC cbDesc = {};
        cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cbDesc.ByteWidth = sizeof(SimConstants);
        cbDesc.Usage = D3D11_USAGE_DYNAMIC;
        cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(m_device->CreateBuffer(&cbDesc, nullptr, &m_simConstants)))
        {
            return false;
        }

        cbDesc.ByteWidth = sizeof(RenderConstants);
        if (FAILED(m_device->CreateBuffer(&cbDesc, nullptr, &m_renderConstants)))
        {
            return false;
        }

        return true;
    }

    bool GpuHairViewer::CreateRenderResources()
    {
        D3D11_RASTERIZER_DESC raster = {};
        raster.FillMode = D3D11_FILL_SOLID;
        raster.CullMode = D3D11_CULL_NONE;
        raster.DepthClipEnable = TRUE;
        if (FAILED(m_device->CreateRasterizerState(&raster, &m_rasterizerState)))
        {
            return false;
        }

        D3D11_BLEND_DESC blend = {};
        blend.RenderTarget[0].BlendEnable = TRUE;
        blend.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        blend.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        blend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        if (FAILED(m_device->CreateBlendState(&blend, &m_blendState)))
        {
            return false;
        }

        D3D11_DEPTH_STENCIL_DESC depthDesc = {};
        depthDesc.DepthEnable = TRUE;
        depthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        depthDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
        if (FAILED(m_device->CreateDepthStencilState(&depthDesc, &m_depthState)))
        {
            return false;
        }
        return true;
    }

    bool GpuHairViewer::LoadShaders()
    {
        std::wstring errorText;
        ComPtr<ID3DBlob> vsBlob;
        ComPtr<ID3DBlob> gsBlob;
        ComPtr<ID3DBlob> psBlob;
        ComPtr<ID3DBlob> csBlob;

        if (!CompileShaderFromFile(L"HairRibbon.hlsl", "VSMain", "vs_5_0", &vsBlob, errorText) ||
            !CompileShaderFromFile(L"HairRibbon.hlsl", "GSMain", "gs_5_0", &gsBlob, errorText) ||
            !CompileShaderFromFile(L"HairRibbon.hlsl", "PSMain", "ps_5_0", &psBlob, errorText))
        {
            MessageBoxW(m_hwnd, errorText.c_str(), L"Shader compile failed", MB_ICONERROR);
            return false;
        }

        if (FAILED(m_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &m_ribbonVS)) ||
            FAILED(m_device->CreateGeometryShader(gsBlob->GetBufferPointer(), gsBlob->GetBufferSize(), nullptr, &m_ribbonGS)) ||
            FAILED(m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &m_ribbonPS)))
        {
            return false;
        }

        if (!CompileShaderFromFile(L"HairTube.hlsl", "VSMain", "vs_5_0", &vsBlob, errorText) ||
            !CompileShaderFromFile(L"HairTube.hlsl", "GSMain", "gs_5_0", &gsBlob, errorText) ||
            !CompileShaderFromFile(L"HairTube.hlsl", "PSMain", "ps_5_0", &psBlob, errorText))
        {
            MessageBoxW(m_hwnd, errorText.c_str(), L"Shader compile failed", MB_ICONERROR);
            return false;
        }

        if (FAILED(m_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &m_tubeVS)) ||
            FAILED(m_device->CreateGeometryShader(gsBlob->GetBufferPointer(), gsBlob->GetBufferSize(), nullptr, &m_tubeGS)) ||
            FAILED(m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &m_tubePS)))
        {
            return false;
        }

        if (!CompileShaderFromFile(L"HairSim.hlsl", "RootBindCS", "cs_5_0", &csBlob, errorText) ||
            FAILED(m_device->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(), nullptr, &m_rootBindCS)) ||
            !CompileShaderFromFile(L"HairSim.hlsl", "IntegrateCS", "cs_5_0", &csBlob, errorText) ||
            FAILED(m_device->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(), nullptr, &m_integrateCS)) ||
            !CompileShaderFromFile(L"HairSim.hlsl", "DistanceCS", "cs_5_0", &csBlob, errorText) ||
            FAILED(m_device->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(), nullptr, &m_distanceCS)) ||
            !CompileShaderFromFile(L"HairSim.hlsl", "BendCS", "cs_5_0", &csBlob, errorText) ||
            FAILED(m_device->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(), nullptr, &m_bendCS)) ||
            !CompileShaderFromFile(L"HairSim.hlsl", "CollisionCS", "cs_5_0", &csBlob, errorText) ||
            FAILED(m_device->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(), nullptr, &m_collisionCS)) ||
            !CompileShaderFromFile(L"HairSim.hlsl", "ClearGridCS", "cs_5_0", &csBlob, errorText) ||
            FAILED(m_device->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(), nullptr, &m_clearGridCS)) ||
            !CompileShaderFromFile(L"HairSim.hlsl", "BuildGridCS", "cs_5_0", &csBlob, errorText) ||
            FAILED(m_device->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(), nullptr, &m_buildGridCS)) ||
            !CompileShaderFromFile(L"HairSim.hlsl", "SelfCollisionCS", "cs_5_0", &csBlob, errorText) ||
            FAILED(m_device->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(), nullptr, &m_selfCollisionCS)) ||
            !CompileShaderFromFile(L"HairSim.hlsl", "FollowCS", "cs_5_0", &csBlob, errorText) ||
            FAILED(m_device->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(), nullptr, &m_followCS)))
        {
            MessageBoxW(m_hwnd, errorText.c_str(), L"Shader compile failed", MB_ICONERROR);
            return false;
        }

        const D3D11_INPUT_ELEMENT_DESC layout[] = {
            { "PARTICLE_INDEX", 0, DXGI_FORMAT_R32_UINT, 0, offsetof(SegmentRenderVertex, particleIndex), D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "STRAND_INDEX", 0, DXGI_FORMAT_R32_UINT, 0, offsetof(SegmentRenderVertex, strandIndex), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
        if (FAILED(m_device->CreateInputLayout(layout, 2, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &m_inputLayout)))
        {
            return false;
        }

        return true;
    }

    void GpuHairViewer::ResetSimulation()
    {
        m_currentBufferIndex = 0;
        m_context->UpdateSubresource(m_positions[0].buffer.Get(), 0, nullptr, m_initialParticles.data(), 0, 0);
        m_context->UpdateSubresource(m_positions[1].buffer.Get(), 0, nullptr, m_initialParticles.data(), 0, 0);
        m_context->UpdateSubresource(m_previousPositions[0].buffer.Get(), 0, nullptr, m_initialParticles.data(), 0, 0);
        m_context->UpdateSubresource(m_previousPositions[1].buffer.Get(), 0, nullptr, m_initialParticles.data(), 0, 0);
        if (!m_zeroDistanceLambdas.empty())
        {
            m_context->UpdateSubresource(m_distanceLambdaBuffer.buffer.Get(), 0, nullptr, m_zeroDistanceLambdas.data(), 0, 0);
        }
        if (!m_zeroBendLambdas.empty())
        {
            m_context->UpdateSubresource(m_bendLambdaBuffer.buffer.Get(), 0, nullptr, m_zeroBendLambdas.data(), 0, 0);
        }
        const UINT gridCellCount = std::max<UINT>(m_simConfig.selfCollisionGridDimX * m_simConfig.selfCollisionGridDimY * m_simConfig.selfCollisionGridDimZ, 1u);
        std::vector<uint32_t> zeroCellCounts(gridCellCount, 0u);
        std::vector<uint32_t> zeroCellIndices(std::max<UINT>(gridCellCount * m_simConfig.selfCollisionCellCapacity, 1u), 0xFFFFFFFFu);
        m_context->UpdateSubresource(m_gridCellCountsBuffer.buffer.Get(), 0, nullptr, zeroCellCounts.data(), 0, 0);
        m_context->UpdateSubresource(m_gridCellIndicesBuffer.buffer.Get(), 0, nullptr, zeroCellIndices.data(), 0, 0);
        UpdateAnimatedScalp(0.0f);
        m_elapsedSeconds = 0.0f;
    }

    Vec3 GpuHairViewer::ApplySampleScalpSkinning(const Vec3& restPosition, const SkinWeight4& weights, float elapsedSeconds) const
    {
        const float motionBlend = m_enableScalpMotion ? 1.0f : 0.0f;
        const float neckYaw = std::sin(elapsedSeconds * 0.83f) * 0.07f * motionBlend;
        const float neckPitch = std::sin(elapsedSeconds * 1.07f) * 0.035f * motionBlend;
        const float headYaw = std::sin(elapsedSeconds * 0.91f) * 0.14f * motionBlend;
        const float headPitch = std::sin(elapsedSeconds * 1.39f) * 0.06f * motionBlend;
        const float bob = std::sin(elapsedSeconds * 1.15f) * 0.012f * motionBlend;

        const Mat4 neck = Multiply(Multiply(Translation(m_neckPivot + Vec3(0.0f, bob * 0.5f, 0.0f)), RotationY(neckYaw)), Multiply(RotationX(neckPitch), Translation(m_neckPivot * -1.0f)));
        const Vec3 headPivotWorld = TransformPoint(neck, m_scalpCenter);
        const Mat4 headLocal = Multiply(Multiply(Translation(headPivotWorld), RotationY(headYaw)), Multiply(RotationX(headPitch), Translation(headPivotWorld * -1.0f)));
        const Mat4 head = Multiply(neck, headLocal);

        Vec3 blended(0.0f, 0.0f, 0.0f);
        for (size_t i = 0; i < 4; ++i)
        {
            const float weight = weights.boneWeights[i];
            if (weight <= 0.0f)
            {
                continue;
            }
            const uint32_t boneIndex = weights.boneIndices[i];
            const Mat4& bone = (boneIndex == 0u) ? neck : head;
            blended += TransformPoint(bone, restPosition) * weight;
        }
        return blended;
    }

    void GpuHairViewer::UpdateAnimatedScalp(float elapsedSeconds)
    {
        if (m_restScalpParticles.empty())
        {
            return;
        }

        for (size_t i = 0; i < m_restScalpParticles.size(); ++i)
        {
            const GpuParticle& source = m_restScalpParticles[i];
            const Vec3 rest(source.x, source.y, source.z);
            SkinWeight4 weights = {};
            if (!m_asset.scalpMesh.skinWeights.empty())
            {
                weights = m_asset.scalpMesh.skinWeights[i];
            }
            const Vec3 skinned = ApplySampleScalpSkinning(rest, weights, elapsedSeconds);
            m_animatedScalpParticles[i] = { skinned.x, skinned.y, skinned.z, 1.0f };
        }

        m_context->UpdateSubresource(m_scalpPositions.buffer.Get(), 0, nullptr, m_animatedScalpParticles.data(), 0, 0);
    }

    void GpuHairViewer::StepSimulation(float dt)
    {
        UpdateAnimatedScalp(m_elapsedSeconds);

        if (!m_zeroDistanceLambdas.empty())
        {
            m_context->UpdateSubresource(m_distanceLambdaBuffer.buffer.Get(), 0, nullptr, m_zeroDistanceLambdas.data(), 0, 0);
        }
        if (!m_zeroBendLambdas.empty())
        {
            m_context->UpdateSubresource(m_bendLambdaBuffer.buffer.Get(), 0, nullptr, m_zeroBendLambdas.data(), 0, 0);
        }

        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (SUCCEEDED(m_context->Map(m_simConstants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            SimConstants* constants = reinterpret_cast<SimConstants*>(mapped.pData);
            *constants = SimConstants{};
            constants->totalStrandCount = static_cast<uint32_t>(m_asset.strands.size());
            constants->guideStrandCount = m_asset.guideStrandCount;
            constants->followerStrandCount = m_asset.followerStrandCount;
            constants->particlesPerStrand = m_asset.particlesPerStrand;
            constants->solverIterations = m_simConfig.solverIterations;
            constants->sphereColliderCount = static_cast<uint32_t>(std::min<size_t>(m_colliders.spheres.size(), 4));
            constants->capsuleColliderCount = static_cast<uint32_t>(std::min<size_t>(m_colliders.capsules.size(), 4));
            constants->enableSelfCollision = m_enableSelfCollision ? 1u : 0u;
            constants->deltaTime = dt;
            constants->gravity = m_simConfig.gravity;
            constants->damping = m_simConfig.globalDamping;
            constants->segmentLength = m_simConfig.segmentLength;
            constants->bendStiffness = m_simConfig.bendStiffness;
            constants->followStiffness = m_simConfig.followStiffness;
            constants->windStrength = m_simConfig.windStrength;
            constants->floorY = m_simConfig.floorY;
            constants->elapsedTime = m_elapsedSeconds;
            constants->selfCollisionRadius = m_selfCollisionRadius;
            constants->selfCollisionStiffness = m_selfCollisionStiffness;
            constants->scalpMotionBlend = m_enableScalpMotion ? 1.0f : 0.0f;
            constants->stretchCompliance = m_simConfig.stretchCompliance;
            constants->bendCompliance = m_simConfig.bendCompliance;
            constants->rootStiffness = m_simConfig.rootStiffness;
            constants->velocityShockPropagation = m_simConfig.velocityShockPropagation;
            constants->gridCellSize = m_simConfig.selfCollisionCellSize;
            constants->gridDimX = m_simConfig.selfCollisionGridDimX;
            constants->gridDimY = m_simConfig.selfCollisionGridDimY;
            constants->gridDimZ = m_simConfig.selfCollisionGridDimZ;
            constants->gridCellCapacity = m_simConfig.selfCollisionCellCapacity;
            constants->gridOrigin[0] = m_simConfig.selfCollisionGridOrigin.x;
            constants->gridOrigin[1] = m_simConfig.selfCollisionGridOrigin.y;
            constants->gridOrigin[2] = m_simConfig.selfCollisionGridOrigin.z;
            constants->enableGridSelfCollision = m_enableGridSelfCollision ? 1u : 0u;
            constants->windDirection[0] = m_simConfig.windDirection.x;
            constants->windDirection[1] = m_simConfig.windDirection.y;
            constants->windDirection[2] = m_simConfig.windDirection.z;
            for (size_t i = 0; i < static_cast<size_t>(constants->sphereColliderCount); ++i)
            {
                const SphereCollider& s = m_colliders.spheres[i];
                constants->sphereData[i * 4 + 0] = s.center.x;
                constants->sphereData[i * 4 + 1] = s.center.y;
                constants->sphereData[i * 4 + 2] = s.center.z;
                constants->sphereData[i * 4 + 3] = s.radius;
            }
            for (size_t i = 0; i < static_cast<size_t>(constants->capsuleColliderCount); ++i)
            {
                const CapsuleCollider& c = m_colliders.capsules[i];
                constants->capsuleData[i * 8 + 0] = c.pointA.x;
                constants->capsuleData[i * 8 + 1] = c.pointA.y;
                constants->capsuleData[i * 8 + 2] = c.pointA.z;
                constants->capsuleData[i * 8 + 3] = c.radiusA;
                constants->capsuleData[i * 8 + 4] = c.pointB.x;
                constants->capsuleData[i * 8 + 5] = c.pointB.y;
                constants->capsuleData[i * 8 + 6] = c.pointB.z;
                constants->capsuleData[i * 8 + 7] = c.radiusB;
            }
            m_context->Unmap(m_simConstants.Get(), 0);
        }

        ID3D11Buffer* simCb = m_simConstants.Get();
        const UINT totalParticleCount = static_cast<UINT>(m_asset.restPoints.size());
        const UINT totalStrandCount = static_cast<UINT>(m_asset.strands.size());
        const UINT guideStrandCount = m_asset.guideStrandCount;
        const UINT followerStrandCount = m_asset.followerStrandCount;
        const UINT guideParticleGroups = DispatchGroups(guideStrandCount * m_asset.particlesPerStrand, 64u);
        const UINT guideStrandGroups = DispatchGroups(guideStrandCount, 64u);
        const UINT followerGroups = DispatchGroups(followerStrandCount, 64u);
        const UINT gridCellCount = std::max<UINT>(m_simConfig.selfCollisionGridDimX * m_simConfig.selfCollisionGridDimY * m_simConfig.selfCollisionGridDimZ, 1u);
        const UINT gridCellGroups = DispatchGroups(gridCellCount, 64u);

        ID3D11ShaderResourceView* bindSrvs[] = {
            m_scalpPositions.srv.Get(),
            m_scalpTrianglesBuffer.srv.Get(),
            m_rootBindingsBuffer.srv.Get()
        };
        ID3D11UnorderedAccessView* bindUavs[] = {
            m_positions[m_currentBufferIndex].uav.Get(),
            m_previousPositions[m_currentBufferIndex].uav.Get()
        };
        m_context->CSSetConstantBuffers(0, 1, &simCb);
        m_context->CSSetShaderResources(4, 3, bindSrvs);
        m_context->CSSetUnorderedAccessViews(0, 2, bindUavs, nullptr);
        m_context->CSSetShader(m_rootBindCS.Get(), nullptr, 0);
        m_context->Dispatch(DispatchGroups(totalStrandCount, 64u), 1, 1);

        ID3D11ShaderResourceView* nullBindSrvs[3] = {};
        ID3D11UnorderedAccessView* nullBindUavs[2] = {};
        m_context->CSSetShader(nullptr, nullptr, 0);
        m_context->CSSetShaderResources(4, 3, nullBindSrvs);
        m_context->CSSetUnorderedAccessViews(0, 2, nullBindUavs, nullptr);

        const uint32_t nextBufferIndex = 1u - m_currentBufferIndex;
        ID3D11ShaderResourceView* integrateSrvs[] = {
            m_positions[m_currentBufferIndex].srv.Get(),
            m_previousPositions[m_currentBufferIndex].srv.Get(),
            m_restPositions.srv.Get(),
            m_followBuffer.srv.Get(),
            m_scalpPositions.srv.Get(),
            m_scalpTrianglesBuffer.srv.Get(),
            m_rootBindingsBuffer.srv.Get()
        };
        ID3D11UnorderedAccessView* simUavs[] = {
            m_positions[nextBufferIndex].uav.Get(),
            m_previousPositions[nextBufferIndex].uav.Get(),
            m_distanceLambdaBuffer.uav.Get(),
            m_bendLambdaBuffer.uav.Get(),
            m_gridCellCountsBuffer.uav.Get(),
            m_gridCellIndicesBuffer.uav.Get()
        };

        m_context->CSSetConstantBuffers(0, 1, &simCb);
        m_context->CSSetShaderResources(0, 7, integrateSrvs);
        m_context->CSSetUnorderedAccessViews(0, 6, simUavs, nullptr);

        m_context->CSSetShader(m_integrateCS.Get(), nullptr, 0);
        m_context->Dispatch(DispatchGroups(totalParticleCount, 64u), 1, 1);

        if (guideStrandCount > 0u)
        {
            for (uint32_t iteration = 0; iteration < m_simConfig.solverIterations; ++iteration)
            {
                m_context->CSSetShader(m_distanceCS.Get(), nullptr, 0);
                m_context->Dispatch(guideStrandGroups, 1, 1);

                m_context->CSSetShader(m_bendCS.Get(), nullptr, 0);
                m_context->Dispatch(guideStrandGroups, 1, 1);

                m_context->CSSetShader(m_collisionCS.Get(), nullptr, 0);
                m_context->Dispatch(guideParticleGroups, 1, 1);

                if (m_enableSelfCollision)
                {
                    if (m_enableGridSelfCollision)
                    {
                        m_context->CSSetShader(m_clearGridCS.Get(), nullptr, 0);
                        m_context->Dispatch(gridCellGroups, 1, 1);

                        m_context->CSSetShader(m_buildGridCS.Get(), nullptr, 0);
                        m_context->Dispatch(guideParticleGroups, 1, 1);
                    }

                    m_context->CSSetShader(m_selfCollisionCS.Get(), nullptr, 0);
                    m_context->Dispatch(guideParticleGroups, 1, 1);
                }
            }
        }

        if (followerStrandCount > 0u)
        {
            m_context->CSSetShader(m_followCS.Get(), nullptr, 0);
            m_context->Dispatch(followerGroups, 1, 1);
        }

        ID3D11ShaderResourceView* nullSrvs[7] = {};
        ID3D11UnorderedAccessView* nullUavs[6] = {};
        m_context->CSSetShader(nullptr, nullptr, 0);
        m_context->CSSetShaderResources(0, 7, nullSrvs);
        m_context->CSSetUnorderedAccessViews(0, 6, nullUavs, nullptr);

        m_currentBufferIndex = nextBufferIndex;
    }

    void GpuHairViewer::UpdateCamera()
    {
        const float cosPitch = std::cos(m_cameraPitch);
        const float sinPitch = std::sin(m_cameraPitch);
        const float cosYaw = std::cos(m_cameraYaw);
        const float sinYaw = std::sin(m_cameraYaw);

        const Vec3 offset(
            cosPitch * sinYaw * m_cameraDistance,
            sinPitch * m_cameraDistance,
            cosPitch * cosYaw * m_cameraDistance);

        m_cameraPosition = m_cameraTarget + offset;
    }

    void GpuHairViewer::Render()
    {
        if (!m_swapChain || !m_rtv || !m_dsv)
        {
            return;
        }

        RenderConstants rc = {};
        const Mat4 view = LookAtRH(m_cameraPosition, m_cameraTarget, Vec3(0.0f, 1.0f, 0.0f));
        const Mat4 projection = PerspectiveFovRH(60.0f * 3.1415926535f / 180.0f, static_cast<float>(m_width) / static_cast<float>(m_height), 0.01f, 20.0f);
        const Mat4 viewProjection = Multiply(view, projection);
        for (int i = 0; i < 16; ++i)
        {
            rc.viewProjection[i] = viewProjection.m[i];
        }
        rc.cameraPosition[0] = m_cameraPosition.x;
        rc.cameraPosition[1] = m_cameraPosition.y;
        rc.cameraPosition[2] = m_cameraPosition.z;
        rc.widthScale = m_widthScale;
        m_context->UpdateSubresource(m_renderConstants.Get(), 0, nullptr, &rc, 0, 0);

        const float clearColor[4] = { 0.14f, 0.18f, 0.24f, 1.0f };
        m_context->ClearRenderTargetView(m_rtv.Get(), clearColor);
        m_context->ClearDepthStencilView(m_dsv.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);

        const UINT stride = sizeof(SegmentRenderVertex);
        const UINT offset = 0;
        ID3D11Buffer* vb = m_segmentVertexBuffer.Get();
        ID3D11Buffer* renderCb = m_renderConstants.Get();
        ID3D11ShaderResourceView* renderSrvs[] = {
            m_positions[m_currentBufferIndex].srv.Get(),
            m_pointWidths.srv.Get()
        };

        D3D11_VIEWPORT vp = {};
        vp.Width = static_cast<float>(m_width);
        vp.Height = static_cast<float>(m_height);
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        m_context->RSSetViewports(1, &vp);
        m_context->OMSetRenderTargets(1, m_rtv.GetAddressOf(), m_dsv.Get());
        m_context->RSSetState(m_rasterizerState.Get());
        m_context->OMSetDepthStencilState(m_depthState.Get(), 0);
        const float blendFactor[4] = { 0, 0, 0, 0 };
        m_context->OMSetBlendState(m_blendState.Get(), blendFactor, 0xFFFFFFFF);
        m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
        m_context->IASetInputLayout(m_inputLayout.Get());
        m_context->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
        if (m_renderMode == RenderMode::TubePreview)
        {
            m_context->VSSetShader(m_tubeVS.Get(), nullptr, 0);
            m_context->GSSetShader(m_tubeGS.Get(), nullptr, 0);
            m_context->PSSetShader(m_tubePS.Get(), nullptr, 0);
        }
        else
        {
            m_context->VSSetShader(m_ribbonVS.Get(), nullptr, 0);
            m_context->GSSetShader(m_ribbonGS.Get(), nullptr, 0);
            m_context->PSSetShader(m_ribbonPS.Get(), nullptr, 0);
        }
        m_context->VSSetConstantBuffers(0, 1, &renderCb);
        m_context->GSSetConstantBuffers(0, 1, &renderCb);
        m_context->PSSetConstantBuffers(0, 1, &renderCb);
        m_context->VSSetShaderResources(0, 2, renderSrvs);
        m_context->GSSetShaderResources(0, 2, renderSrvs);
        m_context->Draw(static_cast<UINT>(m_renderData.segmentVertices.size()), 0);

        ID3D11ShaderResourceView* nullSrvs[2] = {};
        m_context->VSSetShaderResources(0, 2, nullSrvs);
        m_context->GSSetShaderResources(0, 2, nullSrvs);
        m_context->GSSetShader(nullptr, nullptr, 0);

        m_swapChain->Present(1, 0);
        UpdateWindowTitle();
    }

    void GpuHairViewer::Resize(UINT width, UINT height)
    {
        m_width = std::max<UINT>(width, 1u);
        m_height = std::max<UINT>(height, 1u);
        if (!m_swapChain)
        {
            return;
        }

        m_rtv.Reset();
        m_dsv.Reset();
        m_depthTexture.Reset();
        m_context->OMSetRenderTargets(0, nullptr, nullptr);

        m_swapChain->ResizeBuffers(0, m_width, m_height, DXGI_FORMAT_UNKNOWN, 0);

        ComPtr<ID3D11Texture2D> backBuffer;
        m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
        m_device->CreateRenderTargetView(backBuffer.Get(), nullptr, &m_rtv);

        D3D11_TEXTURE2D_DESC depthDesc = {};
        depthDesc.Width = m_width;
        depthDesc.Height = m_height;
        depthDesc.ArraySize = 1;
        depthDesc.MipLevels = 1;
        depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        depthDesc.SampleDesc.Count = 1;
        m_device->CreateTexture2D(&depthDesc, nullptr, &m_depthTexture);
        m_device->CreateDepthStencilView(m_depthTexture.Get(), nullptr, &m_dsv);
    }

    void GpuHairViewer::UpdateWindowTitle()
    {
        std::wstringstream ss;
        ss << L"Hair GPU Base Viewer  |  guides=" << m_asset.guideStrandCount
           << L"  followers=" << m_asset.followerStrandCount
           << L"  totalStrands=" << m_asset.strands.size()
           << L"  particles=" << m_asset.restPoints.size()
           << L"  render=" << (m_renderMode == RenderMode::TubePreview ? L"tube" : L"ribbon")
           << L"  widthScale=" << m_widthScale
           << L"  stretchCompliance=" << m_simConfig.stretchCompliance
           << L"  selfCollision=" << (m_enableSelfCollision ? (m_enableGridSelfCollision ? L"grid" : L"legacy") : L"off")
           << L"  scalpSkinning=" << (m_enableScalpMotion ? L"on" : L"off")
           << (m_paused ? L"  [PAUSED]" : L"");
        SetWindowTextW(m_hwnd, ss.str().c_str());
    }

    void GpuHairViewer::Shutdown()
    {
        if (m_context)
        {
            m_context->ClearState();
        }
    }

    bool GpuHairViewer::CompileShaderFromFile(const wchar_t* filePath, const char* entryPoint, const char* target, ID3DBlob** blobOut, std::wstring& errorText)
    {
        UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
    #if defined(_DEBUG)
        flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
    #endif
        ComPtr<ID3DBlob> shaderBlob;
        ComPtr<ID3DBlob> errorBlob;

        wchar_t modulePath[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
        const std::filesystem::path exeDir = std::filesystem::path(modulePath).parent_path();
        std::array<std::filesystem::path, 6> candidates = {
            std::filesystem::path(filePath),
            std::filesystem::path(HAIRGPU_VIEWER_SHADER_DIR) / filePath,
            exeDir / filePath,
            exeDir / L"shaders" / filePath,
            std::filesystem::current_path() / filePath,
            std::filesystem::current_path() / L"shaders" / filePath
        };

        HRESULT hr = E_FAIL;
        std::wstringstream tried;
        for (const auto& candidate : candidates)
        {
            tried << candidate.c_str() << L"\r\n";
            hr = D3DCompileFromFile(candidate.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, entryPoint, target, flags, 0, &shaderBlob, &errorBlob);
            if (SUCCEEDED(hr))
            {
                break;
            }
        }
        if (FAILED(hr))
        {
            if (errorBlob)
            {
                const std::string message(static_cast<const char*>(errorBlob->GetBufferPointer()), errorBlob->GetBufferSize());
                errorText.assign(message.begin(), message.end());
            }
            else
            {
                errorText = L"Unknown shader compilation error.";
            }
            errorText += L"\r\n\r\nTried paths:\r\n";
            errorText += tried.str();
            return false;
        }
        *blobOut = shaderBlob.Detach();
        return true;
    }

    GpuHairViewer::StructuredBuffer GpuHairViewer::CreateStructuredBuffer(UINT elementSize, UINT elementCount, const void* initialData, bool unorderedAccess)
    {
        StructuredBuffer result;

        D3D11_BUFFER_DESC desc = {};
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | (unorderedAccess ? D3D11_BIND_UNORDERED_ACCESS : 0u);
        desc.ByteWidth = std::max<UINT>(elementSize * elementCount, elementSize);
        desc.StructureByteStride = elementSize;
        desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        desc.Usage = D3D11_USAGE_DEFAULT;

        D3D11_SUBRESOURCE_DATA data = {};
        data.pSysMem = initialData;
        m_device->CreateBuffer(&desc, initialData ? &data : nullptr, &result.buffer);

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements = elementCount;
        m_device->CreateShaderResourceView(result.buffer.Get(), &srvDesc, &result.srv);

        if (unorderedAccess)
        {
            D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
            uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
            uavDesc.Format = DXGI_FORMAT_UNKNOWN;
            uavDesc.Buffer.FirstElement = 0;
            uavDesc.Buffer.NumElements = elementCount;
            m_device->CreateUnorderedAccessView(result.buffer.Get(), &uavDesc, &result.uav);
        }

        return result;
    }

    LRESULT CALLBACK GpuHairViewer::WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        GpuHairViewer* app = reinterpret_cast<GpuHairViewer*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE)
        {
            const CREATESTRUCTW* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            app = reinterpret_cast<GpuHairViewer*>(create->lpCreateParams);
            if (app)
            {
                app->m_hwnd = hwnd;
            }
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        }

        return app ? app->HandleMessage(hwnd, message, wParam, lParam) : DefWindowProcW(hwnd, message, wParam, lParam);
    }

    LRESULT GpuHairViewer::HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message)
        {
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        case WM_SIZE:
            Resize(LOWORD(lParam), HIWORD(lParam));
            return 0;
        case WM_LBUTTONDOWN:
            m_dragging = true;
            m_lastMouse.x = GET_X_LPARAM(lParam);
            m_lastMouse.y = GET_Y_LPARAM(lParam);
            SetCapture(m_hwnd);
            return 0;
        case WM_LBUTTONUP:
            m_dragging = false;
            ReleaseCapture();
            return 0;
        case WM_MOUSEMOVE:
            if (m_dragging)
            {
                POINT p = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                const float dx = static_cast<float>(p.x - m_lastMouse.x);
                const float dy = static_cast<float>(p.y - m_lastMouse.y);
                m_lastMouse = p;
                m_cameraYaw += dx * 0.01f;
                m_cameraPitch = Clamp(m_cameraPitch + dy * 0.01f, -1.2f, 1.2f);
            }
            return 0;
        case WM_MOUSEWHEEL:
        {
            const float wheel = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / static_cast<float>(WHEEL_DELTA);
            m_cameraDistance = Clamp(m_cameraDistance - wheel * 0.15f, 0.6f, 8.0f);
            return 0;
        }
        case WM_KEYDOWN:
            switch (wParam)
            {
            case VK_SPACE:
                m_paused = !m_paused;
                return 0;
            case 'R':
                ResetSimulation();
                return 0;
            case '1':
                m_widthScale = Clamp(m_widthScale * 0.85f, 0.2f, 3.0f);
                return 0;
            case '2':
                m_widthScale = Clamp(m_widthScale * 1.15f, 0.2f, 3.0f);
                return 0;
            case '3':
                m_enableSelfCollision = !m_enableSelfCollision;
                return 0;
            case '4':
                m_enableScalpMotion = !m_enableScalpMotion;
                return 0;
            case '5':
                m_selfCollisionRadius = Clamp(m_selfCollisionRadius * 0.90f, 0.004f, 0.040f);
                return 0;
            case '6':
                m_selfCollisionRadius = Clamp(m_selfCollisionRadius * 1.10f, 0.004f, 0.040f);
                return 0;
            case '7':
                m_simConfig.stretchCompliance *= 0.5f;
                return 0;
            case '8':
                m_simConfig.stretchCompliance *= 2.0f;
                return 0;
            case '9':
                m_simConfig.bendCompliance *= 0.5f;
                return 0;
            case '0':
                m_simConfig.bendCompliance *= 2.0f;
                return 0;
            case 'G':
                m_enableGridSelfCollision = !m_enableGridSelfCollision;
                return 0;
            case 'T':
                m_renderMode = (m_renderMode == RenderMode::Ribbon) ? RenderMode::TubePreview : RenderMode::Ribbon;
                return 0;
            case VK_OEM_MINUS:
                m_simConfig.selfCollisionCellSize = Clamp(m_simConfig.selfCollisionCellSize * 0.92f, 0.03f, 0.20f);
                return 0;
            case VK_OEM_PLUS:
                m_simConfig.selfCollisionCellSize = Clamp(m_simConfig.selfCollisionCellSize * 1.08f, 0.03f, 0.20f);
                return 0;
            default:
                break;
            }
            break;
        default:
            break;
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}
