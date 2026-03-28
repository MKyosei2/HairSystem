#pragma once

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

        struct alignas(16) RenderConstants
        {
            float viewProjection[16] = {};
            float cameraPosition[4] = {};
            float lightDirection[4] = { -0.4f, 0.9f, 0.2f, 0.0f };
            float widthScale = 1.0f;
            float _padding1[3] = {};
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
        void ResetSimulation();
        void StepSimulation(float dt);
        void Render();
        void Resize(UINT width, UINT height);
        void UpdateCamera();
        void UpdateWindowTitle();
        void Shutdown();
        void UpdateAnimatedScalp(float elapsedSeconds);
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
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_simConstants;
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_renderConstants;
        Microsoft::WRL::ComPtr<ID3D11RasterizerState> m_rasterizerState;
        Microsoft::WRL::ComPtr<ID3D11BlendState> m_blendState;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilState> m_depthState;

        RenderMode m_renderMode = RenderMode::Ribbon;

        LARGE_INTEGER m_qpcFrequency = {};
        LARGE_INTEGER m_lastTick = {};
        float m_accumulator = 0.0f;
        float m_elapsedSeconds = 0.0f;
        uint32_t m_currentBufferIndex = 0;
    };
}
