#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "hairgpu/RenderData.h"
#include "hairgpu/Types.h"

namespace hairgpu::viewer
{
    class GpuHairViewer
    {
    public:
        bool Initialize(HINSTANCE instance, int showCommand);
        int Run();

    private:
        struct GpuParticle
        {
            float x, y, z, w;
        };

        struct FollowGpuData
        {
            int32_t parentGuide = -1;
            float followWeight = 1.0f;
            float clumpWeight = 0.0f;
            float noiseWeight = 0.0f;
        };

        struct TriangleGpuData
        {
            uint32_t a = 0;
            uint32_t b = 0;
            uint32_t c = 0;
            uint32_t _padding = 0;
        };

        struct RootBindingGpuData
        {
            uint32_t triangleIndex = 0;
            float barycentric[3] = { 1.0f, 0.0f, 0.0f };
            float rootUv[2] = { 0.0f, 0.0f };
            float _padding[2] = { 0.0f, 0.0f };
        };

        struct DebugLineVertex
        {
            float x, y, z;
            float r, g, b, a;
        };

        struct ScalpVertex
        {
            float x, y, z;
            float nx, ny, nz;
        };

        struct HairMeshVertex
        {
            float x, y, z, w;
            float r, g, b, a;
        };

        struct PerformanceSample
        {
            float elapsedSeconds = 0.0f;
            float frameMilliseconds = 0.0f;
            float simulationMilliseconds = 0.0f;
            float fps = 0.0f;
            uint32_t guideCount = 0;
            uint32_t followerCount = 0;
            uint32_t totalStrandCount = 0;
            uint32_t totalParticleCount = 0;
        };

        struct alignas(16) SimConstants
        {
            uint32_t totalStrandCount = 0;
            uint32_t guideStrandCount = 0;
            uint32_t followerStrandCount = 0;
            uint32_t particlesPerStrand = 0;

            uint32_t solverIterations = 0;
            uint32_t sphereColliderCount = 0;
            uint32_t capsuleColliderCount = 0;
            uint32_t enableSelfCollision = 0;

            float deltaTime = 0.0f;
            float gravity = 9.81f;
            float damping = 0.03f;
            float segmentLength = 0.035f;

            float bendStiffness = 0.12f;
            float followStiffness = 0.92f;
            float windStrength = 0.0f;
            float floorY = -1.2f;

            float elapsedTime = 0.0f;
            float selfCollisionRadius = 0.012f;
            float selfCollisionStiffness = 0.35f;
            float scalpMotionBlend = 1.0f;

            float stretchCompliance = 2.0e-7f;
            float bendCompliance = 1.0e-5f;
            float rootStiffness = 0.18f;
            float velocityShockPropagation = 0.72f;

            float windDirection[3] = { 1.0f, 0.0f, 0.0f };
            float gridCellSize = 0.085f;

            uint32_t gridDimX = 18;
            uint32_t gridDimY = 18;
            uint32_t gridDimZ = 18;
            uint32_t gridCellCapacity = 24;

            float gridOrigin[3] = { -0.72f, 0.48f, -0.72f };
            uint32_t enableGridSelfCollision = 1;

            float sphereData[4 * 4] = {};
            float capsuleData[4 * 8] = {};
        };

        enum class RenderMode : uint32_t
        {
            Ribbon = 0,
            TubePreview = 1,
        };

        enum class DebugColorMode : uint32_t
        {
            Natural = 0,
            GuideFollower = 1,
            WidthHeatmap = 2,
        };

        struct alignas(16) RenderConstants
        {
            float viewProjection[16] = {};
            float cameraPosition[4] = {};
            float lightDirection[4] = { -0.4f, 0.9f, 0.2f, 0.0f };
            float widthScale = 1.0f;
            uint32_t debugColorMode = 0;
            uint32_t guideStrandCount = 0;
            uint32_t _padding1 = 0;
        };

        struct StructuredBuffer
        {
            Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
            Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> uav;
        };

        bool CreateWindowAndDevice(HINSTANCE instance, int showCommand);
        bool CreateSimulationResources();
        bool CreateRenderResources();
        bool LoadShaders();
        bool CreateUiControls();
        bool RebuildAsset(uint32_t guideCount, uint32_t followersPerGuide, uint32_t particlesPerStrand);
        void LayoutUiControls();
        void SyncControlsFromState();
        void SyncStateFromControls();
        void RefreshAssetStatsText();
        void UpdateStatsLabel();
        void ApplyPreset(int presetIndex);
        void ResetSimulation();
        void StepSingleFrame();
        void StepSimulation(float dt);
        void Render();
        void Resize(UINT width, UINT height);
        void UpdateCamera();
        void UpdateWindowTitle();
        void Shutdown();
        void UpdateAnimatedScalp(float elapsedSeconds);
        void BuildHairMeshVertices(const std::vector<GpuParticle>& particles, float aspectRatio);
        void BuildDebugLineVertices(float aspectRatio);
        bool EnsureHairMeshVertexCapacity(size_t vertexCount);
        bool EnsureDebugVertexCapacity(size_t vertexCount);
        bool SaveSettingsToFile(const std::wstring& filePath) const;
        bool LoadSettingsFromFile(const std::wstring& filePath);
        bool ExportCurrentFrameCsv(const std::wstring& filePath);
        bool ExportPerformanceCsv(const std::wstring& filePath) const;
        bool ReadBackCurrentParticlePositions(std::vector<GpuParticle>& outParticles);
        bool LoadHairAssetFromPath(const std::wstring& filePath);
        RECT GetRenderViewportRect() const;
        hairgpu::Vec3 ApplySampleScalpSkinning(const hairgpu::Vec3& restPosition, const hairgpu::SkinWeight4& weights, float elapsedSeconds) const;

        bool CompileShaderFromFile(const wchar_t* filePath, const char* entryPoint, const char* target, ID3DBlob** blobOut, std::wstring& errorText);
        StructuredBuffer CreateStructuredBuffer(UINT elementSize, UINT elementCount, const void* initialData, bool unorderedAccess);

        static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
        LRESULT HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

        HWND m_hwnd = nullptr;
        UINT m_width = 1600;
        UINT m_height = 900;
        bool m_running = true;
        bool m_paused = false;
        bool m_dragging = false;
        bool m_enableSelfCollision = true;
        bool m_enableScalpMotion = true;
        bool m_showColliderDebug = true;
        bool m_showScalpWireframe = false;
        bool m_showScalpMesh = true;
        bool m_showGridBounds = false;
        bool m_showFloorGuide = true;
        bool m_showGuideSkeleton = true;
        bool m_showFollowerSkeleton = false;
        bool m_showRootMarkers = true;
        bool m_showFollowLinks = false;
        bool m_showTestMesh = true;
        POINT m_lastMouse = {};

        float m_cameraYaw = 0.1f;
        float m_cameraPitch = 0.35f;
        float m_cameraDistance = 2.5f;
        hairgpu::Vec3 m_cameraTarget = { 0.0f, 1.2f, 0.0f };
        hairgpu::Vec3 m_cameraPosition = { 0.0f, 0.0f, 0.0f };
        hairgpu::Vec3 m_scalpCenter = { 0.0f, 1.60f, 0.0f };
        hairgpu::Vec3 m_neckPivot = { 0.0f, 1.42f, -0.03f };
        float m_widthScale = 1.8f;
        float m_selfCollisionRadius = 0.012f;
        float m_selfCollisionStiffness = 0.35f;
        bool m_enableGridSelfCollision = true;

        uint32_t m_builderGuideCount = 320;
        uint32_t m_builderFollowersPerGuide = 10;
        uint32_t m_builderParticlesPerStrand = 16;
        std::wstring m_activePresetName = L"Rapunzel Preview";
        std::wstring m_lastAssetSource = L"Sample Hybrid";
        std::wstring m_lastImportStatus = L"Sample asset active";

        float m_smoothedFps = 0.0f;
        float m_lastFrameSeconds = 0.0f;
        float m_lastSimulationStepMilliseconds = 0.0f;
        float m_statsUpdateAccumulator = 0.0f;

        hairgpu::HairAsset m_asset;
        hairgpu::GpuSimulationConfig m_simConfig;
        hairgpu::ColliderSet m_colliders;
        std::vector<GpuParticle> m_initialParticles;
        std::vector<FollowGpuData> m_followGpuData;
        hairgpu::HairSegmentRenderData m_renderData;
        std::vector<GpuParticle> m_restScalpParticles;
        std::vector<GpuParticle> m_animatedScalpParticles;
        std::vector<TriangleGpuData> m_scalpTriangles;
        std::vector<RootBindingGpuData> m_rootBindingGpuData;
        std::vector<float> m_zeroDistanceLambdas;
        std::vector<float> m_zeroBendLambdas;
        std::vector<HairMeshVertex> m_hairMeshVertices;
        std::vector<DebugLineVertex> m_debugLineVertices;
        std::vector<GpuParticle> m_debugReadbackParticles;
        std::vector<PerformanceSample> m_performanceSamples;

        Microsoft::WRL::ComPtr<ID3D11Device> m_device;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_context;
        Microsoft::WRL::ComPtr<IDXGISwapChain> m_swapChain;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_rtv;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> m_depthTexture;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView> m_dsv;

        Microsoft::WRL::ComPtr<ID3D11VertexShader> m_ribbonVS;
        Microsoft::WRL::ComPtr<ID3D11GeometryShader> m_ribbonGS;
        Microsoft::WRL::ComPtr<ID3D11PixelShader> m_ribbonPS;
        Microsoft::WRL::ComPtr<ID3D11VertexShader> m_tubeVS;
        Microsoft::WRL::ComPtr<ID3D11GeometryShader> m_tubeGS;
        Microsoft::WRL::ComPtr<ID3D11PixelShader> m_tubePS;
        Microsoft::WRL::ComPtr<ID3D11VertexShader> m_debugLineVS;
        Microsoft::WRL::ComPtr<ID3D11PixelShader> m_debugLinePS;
        Microsoft::WRL::ComPtr<ID3D11VertexShader> m_scalpVS;
        Microsoft::WRL::ComPtr<ID3D11PixelShader> m_scalpPS;
        Microsoft::WRL::ComPtr<ID3D11VertexShader> m_hairMeshVS;
        Microsoft::WRL::ComPtr<ID3D11PixelShader> m_hairMeshPS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_rootBindCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_integrateCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_distanceCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_bendCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_collisionCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_clearGridCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_buildGridCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_selfCollisionCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_followCS;
        Microsoft::WRL::ComPtr<ID3D11InputLayout> m_inputLayout;
        Microsoft::WRL::ComPtr<ID3D11InputLayout> m_debugInputLayout;
        Microsoft::WRL::ComPtr<ID3D11InputLayout> m_scalpInputLayout;
        Microsoft::WRL::ComPtr<ID3D11InputLayout> m_hairMeshInputLayout;

        StructuredBuffer m_positions[2];
        StructuredBuffer m_previousPositions[2];
        StructuredBuffer m_restPositions;
        StructuredBuffer m_pointWidths;
        StructuredBuffer m_followBuffer;
        StructuredBuffer m_scalpPositions;
        StructuredBuffer m_scalpTrianglesBuffer;
        StructuredBuffer m_rootBindingsBuffer;
        StructuredBuffer m_distanceLambdaBuffer;
        StructuredBuffer m_bendLambdaBuffer;
        StructuredBuffer m_gridCellCountsBuffer;
        StructuredBuffer m_gridCellIndicesBuffer;
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_segmentVertexBuffer;
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_hairMeshVertexBuffer;
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_debugLineVertexBuffer;
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_scalpVertexBuffer;
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_scalpIndexBuffer;
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_simConstants;
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_renderConstants;
        Microsoft::WRL::ComPtr<ID3D11RasterizerState> m_rasterizerState;
        Microsoft::WRL::ComPtr<ID3D11BlendState> m_blendState;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilState> m_depthState;
        size_t m_hairMeshVertexCapacity = 0;
        size_t m_debugLineVertexCapacity = 0;

        RenderMode m_renderMode = RenderMode::TubePreview;
        DebugColorMode m_debugColorMode = DebugColorMode::Natural;

        LARGE_INTEGER m_qpcFrequency = {};
        LARGE_INTEGER m_lastTick = {};
        float m_accumulator = 0.0f;
        float m_elapsedSeconds = 0.0f;
        uint32_t m_currentBufferIndex = 0;

        HWND m_uiPanel = nullptr;
        HWND m_btnPlayPause = nullptr;
        HWND m_btnReset = nullptr;
        HWND m_btnStep = nullptr;
        HWND m_btnApplyAsset = nullptr;
        HWND m_btnImportAsset = nullptr;
        HWND m_btnSaveSettings = nullptr;
        HWND m_btnLoadSettings = nullptr;
        HWND m_btnExportCsv = nullptr;
        HWND m_btnExportPerfCsv = nullptr;
        HWND m_comboPreset = nullptr;
        HWND m_comboRenderMode = nullptr;
        HWND m_comboDebugColor = nullptr;
        HWND m_checkSelfCollision = nullptr;
        HWND m_checkGrid = nullptr;
        HWND m_checkScalpMotion = nullptr;
        HWND m_checkShowColliders = nullptr;
        HWND m_checkShowScalp = nullptr;
        HWND m_checkShowScalpMesh = nullptr;
        HWND m_checkShowGridBounds = nullptr;
        HWND m_checkShowFloor = nullptr;
        HWND m_checkShowGuides = nullptr;
        HWND m_checkShowFollowers = nullptr;
        HWND m_checkShowRoots = nullptr;
        HWND m_checkShowLinks = nullptr;
        HWND m_checkShowTestMesh = nullptr;
        HWND m_editGuides = nullptr;
        HWND m_editFollowers = nullptr;
        HWND m_editParticles = nullptr;
        HWND m_trackWidth = nullptr;
        HWND m_trackGravity = nullptr;
        HWND m_trackDamping = nullptr;
        HWND m_trackWind = nullptr;
        HWND m_trackIterations = nullptr;
        HWND m_trackFollow = nullptr;
        HWND m_trackSelfRadius = nullptr;
        HWND m_trackSelfStiffness = nullptr;
        HWND m_trackGridCellSize = nullptr;
        HWND m_trackStretchCompliance = nullptr;
        HWND m_trackBendCompliance = nullptr;
        HWND m_labelStats = nullptr;
        HWND m_labelAssetStats = nullptr;
        HWND m_labelWidth = nullptr;
        HWND m_labelGravity = nullptr;
        HWND m_labelDamping = nullptr;
        HWND m_labelWind = nullptr;
        HWND m_labelIterations = nullptr;
        HWND m_labelFollow = nullptr;
        HWND m_labelSelfRadius = nullptr;
        HWND m_labelSelfStiffness = nullptr;
        HWND m_labelGridCellSize = nullptr;
        HWND m_labelStretchCompliance = nullptr;
        HWND m_labelBendCompliance = nullptr;
    };
}
