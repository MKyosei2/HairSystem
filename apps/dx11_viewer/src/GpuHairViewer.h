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
#include <filesystem>
#include <string>
#include <vector>

#include "hairgpu/ActiveSet.h"
#include "hairgpu/Authoring.h"
#include "hairgpu/BoneAnimation.h"
#include "hairgpu/HybridRuntime.h"
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

        struct DebugLineTemplateVertex
        {
            float endpointId = 0.0f;
        };

        struct GpuDebugLineEntry
        {
            uint32_t type = 0u;
            uint32_t indexA = 0u;
            uint32_t indexB = 0u;
            uint32_t axisOrFlags = 0u;
            float data0[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
            float data1[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
            float color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        };

        struct DebugLineStats
        {
            uint32_t worldLineCount = 0u;
            uint32_t hairLineCount = 0u;
            uint32_t scalpLineCount = 0u;
            uint32_t rootMarkerCount = 0u;
            uint32_t totalLineCount = 0u;
        };

        struct ScalpVertex
        {
            uint32_t scalpIndex = 0u;
            float nx = 0.0f, ny = 1.0f, nz = 0.0f;
            float r = 0.76f, g = 0.62f, b = 0.52f;
        };

        struct HairMeshVertex
        {
            float x, y, z, w;
            float r, g, b, a;
        };

        struct BrickVertex
        {
            float px, py, pz;
            float nx, ny, nz;
        };

        struct BrickInstance
        {
            uint32_t particleIndex = 0;
            uint32_t strandIndex = 0;
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
            uint32_t activeSetClusterCount = 0;
            uint32_t activeSetSelectedClusterCount = 0;
            uint32_t activeSetSelectedGuideCount = 0;
            uint32_t activeSetSelectedStrandCount = 0;
            float activeSetHighestScore = 0.0f;
            float activeSetAverageSelectedScore = 0.0f;
            float activeSetEvaluationMilliseconds = 0.0f;
            float activeSetBudgetScale = 1.0f;
            uint32_t activeSetRecommendedSolverIterations = 0u;
            uint32_t fullTierCount = 0u;
            uint32_t reducedTierCount = 0u;
            uint32_t frozenTierCount = 0u;
            uint32_t cachedTierCount = 0u;
            uint32_t correctiveStrandCount = 0u;
            uint32_t correctivePointCount = 0u;
            float correctiveAveragePenetration = 0.0f;
            float correctiveMaxPenetration = 0.0f;
            float correctiveAverageLengthErrorBefore = 0.0f;
            float correctiveAverageLengthErrorAfter = 0.0f;
        };

        struct Float4
        {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
            float w = 0.0f;
        };

        struct alignas(16) SimConstantsHair
        {
            uint32_t totalStrandCount = 0;
            uint32_t guideStrandCount = 0;
            uint32_t followerStrandCount = 0;
            uint32_t particlesPerStrand = 0;

            uint32_t solverIterations = 0;
            uint32_t sphereColliderCount = 0;
            uint32_t capsuleColliderCount = 0;
            uint32_t enableSelfCollision = 0;
            uint32_t planeColliderCount = 0;
            uint32_t aabbColliderCount = 0;
            uint32_t useSdfCollider = 0;
            uint32_t sdfValueCount = 0;

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

            float looseConstraintStiffness = 0.12f;
            float looseConstraintMaxDistance = 0.085f;
            float supportCollisionRadius = 0.030f;
            float supportCollisionStiffness = 0.26f;

            float tangentialFriction = 0.06f;
            float lateralFriction = 0.78f;
            float freezeBackThreshold = -0.045f;
            float freezeBlend = 0.82f;

            uint32_t broadphaseMode = 0u;
            uint32_t broadphaseHashBucketCount = 32768u;
            uint32_t broadphasePadding0 = 0u;
            uint32_t broadphasePadding1 = 0u;

            uint32_t sortedPaddedEntryCount = 1u;
            uint32_t enableRuntimeAngularBend = 1u;
            uint32_t runtimePadding0 = 0u;
            uint32_t runtimePadding1 = 0u;

            float angularBendRadians = 0.18f;
            float angularBendCompliance = 5.0e-6f;
            float angularBendStiffness = 0.35f;
            float angularBendDamping = 0.08f;

            Float4 sphereData[4] = {};
            Float4 capsuleData[8] = {};
            Float4 planeData[4] = {};
            Float4 aabbData[8] = {};
            uint32_t sdfResolution[4] = {};
            Float4 sdfBoundsMin = {};
            Float4 sdfBoundsMax = {};

            uint32_t useCompactedGuideDispatch = 0u;
            uint32_t compactedGuideCount = 0u;
            uint32_t followerUpdateStride = 1u;
            uint32_t followerUpdatePhase = 0u;
            uint32_t useCompactedFollowerDispatch = 0u;
            uint32_t compactedFollowerCount = 0u;
            uint32_t useCompactedGuidePointDispatch = 0u;
            uint32_t compactedGuidePointCount = 0u;
        };

        struct alignas(16) SimConstantsActiveSet
        {
            uint32_t totalStrandCount = 0;
            uint32_t guideStrandCount = 0;
            uint32_t followerStrandCount = 0;
            uint32_t particlesPerStrand = 0;

            uint32_t solverIterations = 0;
            uint32_t sphereColliderCount = 0;
            uint32_t capsuleColliderCount = 0;
            uint32_t enableSelfCollision = 0;
            uint32_t planeColliderCount = 0;
            uint32_t aabbColliderCount = 0;
            uint32_t useSdfCollider = 0;
            uint32_t sdfValueCount = 0;

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

            float looseConstraintStiffness = 0.12f;
            float looseConstraintMaxDistance = 0.085f;
            float supportCollisionRadius = 0.030f;
            float supportCollisionStiffness = 0.26f;

            float tangentialFriction = 0.06f;
            float lateralFriction = 0.78f;
            float freezeBackThreshold = -0.045f;
            float freezeBlend = 0.82f;

            uint32_t broadphaseMode = 0u;
            uint32_t broadphaseHashBucketCount = 32768u;
            uint32_t broadphasePadding0 = 0u;
            uint32_t broadphasePadding1 = 0u;

            uint32_t sortedPaddedEntryCount = 1u;
            uint32_t enableRuntimeAngularBend = 1u;
            uint32_t runtimePadding0 = 0u;
            uint32_t runtimePadding1 = 0u;

            float angularBendRadians = 0.18f;
            float angularBendCompliance = 5.0e-6f;
            float angularBendStiffness = 0.35f;
            float angularBendDamping = 0.08f;

            Float4 sphereData[4] = {};
            Float4 capsuleData[8] = {};
            Float4 planeData[4] = {};
            Float4 aabbData[8] = {};
            uint32_t sdfResolution[4] = {};
            Float4 sdfBoundsMin = {};
            Float4 sdfBoundsMax = {};

            uint32_t followerUpdateStride = 1u;
            uint32_t followerUpdatePhase = 0u;
            uint32_t useCompactedFollowerDispatch = 0u;
            uint32_t compactedFollowerCount = 0u;
        };

        struct alignas(16) SimConstantsScalp
        {
            uint32_t totalStrandCount = 0;
            uint32_t guideStrandCount = 0;
            uint32_t followerStrandCount = 0;
            uint32_t particlesPerStrand = 0;

            uint32_t solverIterations = 0;
            uint32_t sphereColliderCount = 0;
            uint32_t capsuleColliderCount = 0;
            uint32_t enableSelfCollision = 0;
            uint32_t planeColliderCount = 0;
            uint32_t aabbColliderCount = 0;
            uint32_t useSdfCollider = 0;
            uint32_t sdfValueCount = 0;

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

            float scalpCenter[4] = {};
            float neckPivot[4] = {};

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

            float looseConstraintStiffness = 0.12f;
            float looseConstraintMaxDistance = 0.085f;
            float supportCollisionRadius = 0.030f;
            float supportCollisionStiffness = 0.26f;

            float tangentialFriction = 0.06f;
            float lateralFriction = 0.78f;
            float freezeBackThreshold = -0.045f;
            float freezeBlend = 0.82f;

            uint32_t broadphaseMode = 0u;
            uint32_t broadphaseHashBucketCount = 32768u;
            uint32_t broadphasePadding0 = 0u;
            uint32_t broadphasePadding1 = 0u;

            uint32_t sortedPaddedEntryCount = 1u;
            uint32_t enableRuntimeAngularBend = 1u;
            uint32_t runtimePadding0 = 0u;
            uint32_t runtimePadding1 = 0u;

            float angularBendRadians = 0.18f;
            float angularBendCompliance = 5.0e-6f;
            float angularBendStiffness = 0.35f;
            float angularBendDamping = 0.08f;

            Float4 sphereData[4] = {};
            Float4 capsuleData[8] = {};
            Float4 planeData[4] = {};
            Float4 aabbData[8] = {};
            uint32_t sdfResolution[4] = {};
            Float4 sdfBoundsMin = {};
            Float4 sdfBoundsMax = {};
        };

        struct alignas(16) BitonicConstants
        {
            uint32_t j = 0u;
            uint32_t k = 0u;
            uint32_t entryCount = 0u;
            uint32_t _padding = 0u;
        };

        enum class RenderMode : uint32_t
        {
            ParticlePointSprite = 0,
            ParticleBillboard = 1,
            StrandRibbon = 2,
        };

        enum class DebugColorMode : uint32_t
        {
            Natural = 0,
            GuideFollower = 1,
            WidthHeatmap = 2,
        };

        enum class MaskOverlayMode : uint32_t
        {
            None = 0,
            Parting = 1,
            Region = 2,
            EditInfluence = 3,
            EditInfluenceBeforeAfter = 4,
            ActiveSetRiskSelection = 5,
            ActiveSetMotion = 6,
            ActiveSetContact = 7,
            ActiveSetUncertainty = 8,
            ActiveSetEdit = 9,
        };

        enum class CpuReadbackMode : uint32_t
        {
            Off = 0,
            OnDemandVisibility = 1,
            FullParticlesDebug = 2,
        };

        enum class StrandGizmoMode : uint32_t
        {
            TipTranslate = 0,
            WholeTranslate = 1,
            RootTranslate = 2,
        };

        enum class GizmoAxis : uint32_t
        {
            None = 0,
            X = 1,
            Y = 2,
            Z = 3,
        };

        struct alignas(16) RenderConstants
        {
            float viewProjection[16] = {};
            float cameraPosition[4] = {};
            float lightDirection[4] = { -0.4f, 0.9f, 0.2f, 0.0f };
            float widthScale = 1.0f;
            uint32_t debugColorMode = 0;
            uint32_t guideStrandCount = 0;
            uint32_t particlesPerStrand = 0;
            uint32_t totalStrandCount = 0;
            uint32_t tubeRingSides = 0;
            uint32_t renderFollowerStride = 1u;
            uint32_t renderGuideOnlyAboveTier = 4u;
            float renderReducedWidthBoost = 0.82f;
            float renderHiddenWidthThreshold = 0.0f;
            float renderStrandAlpha = 1.0f;
            float renderPadding0 = 0.0f;
        };

        static_assert(sizeof(SimConstantsHair) % 16u == 0u, "SimConstantsHair must stay 16-byte aligned");
        static_assert(sizeof(SimConstantsActiveSet) % 16u == 0u, "SimConstantsActiveSet must stay 16-byte aligned");
        static_assert(sizeof(SimConstantsScalp) % 16u == 0u, "SimConstantsScalp must stay 16-byte aligned");
        static_assert(sizeof(SimConstantsHair) == 704u, "SimConstantsHair must stay in sync with HairSim/HairIndirectArgs HLSL");
        static_assert(sizeof(SimConstantsActiveSet) == 688u, "SimConstantsActiveSet must stay in sync with HairActiveSet HLSL");
        static_assert(sizeof(SimConstantsScalp) == 704u, "SimConstantsScalp must stay in sync with ScalpSim HLSL");
        static_assert(sizeof(RenderConstants) == 144u, "RenderConstants must stay in sync with HLSL render constant buffers");

        struct StructuredBuffer
        {
            Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
            Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> uav;
        };

        struct GpuTimingFrame
        {
            Microsoft::WRL::ComPtr<ID3D11Query> disjoint;
            Microsoft::WRL::ComPtr<ID3D11Query> frameStart;
            Microsoft::WRL::ComPtr<ID3D11Query> simStart;
            Microsoft::WRL::ComPtr<ID3D11Query> simEnd;
            Microsoft::WRL::ComPtr<ID3D11Query> hairDrawStart;
            Microsoft::WRL::ComPtr<ID3D11Query> hairDrawEnd;
            Microsoft::WRL::ComPtr<ID3D11Query> debugDrawStart;
            Microsoft::WRL::ComPtr<ID3D11Query> debugDrawEnd;
            Microsoft::WRL::ComPtr<ID3D11Query> frameEnd;
            Microsoft::WRL::ComPtr<ID3D11Query> hairPipelineStats;
            Microsoft::WRL::ComPtr<ID3D11Query> debugPipelineStats;
            bool issued = false;
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
        void RebuildActiveSetFoundation();
        void SyncActiveSetGpuBuffers();
        void FillHairSimConstants(SimConstantsHair& constants, float dt) const;
        void FillActiveSetSimConstants(SimConstantsActiveSet& constants, float dt) const;
        void FillScalpSimConstants(SimConstantsScalp& constants, float dt) const;
        void FillRenderConstants(RenderConstants& constants, const Mat4& viewProjection) const;
        void UpdateActiveSetRuntimeFromParticles(const std::vector<GpuParticle>& particles);
        std::vector<hairgpu::Vec3> BuildGuideRootPositionsFromParticles(const std::vector<GpuParticle>& particles) const;
        std::vector<float> BuildGuideContactScoresFromParticles(const std::vector<GpuParticle>& particles) const;
        void BuildHairMeshVertices(const std::vector<GpuParticle>& particles, float aspectRatio);
        void RecomputeAssetBounds();
        void AutoFrameCameraToAsset(bool resetAngles);
        void QueueVisibilityRecovery();
        bool ShouldRunVisibilityRecoveryThisFrame() const;
        bool RunVisibilityRecoveryPass(float aspectRatio, bool allowCameraRecovery, std::vector<GpuParticle>* outParticles);
        bool RestoreGpuParticleStateFromInitialPose(bool resetCamera);
        uint32_t EstimateVisibleParticleCount(const std::vector<GpuParticle>& particles, float aspectRatio) const;
        void RebuildGpuDebugLineEntries();
        bool EnsureHairMeshVertexCapacity(size_t vertexCount);
        bool EnsureGpuDebugLineCapacity(size_t lineCount);
        void UpdateRenderStatistics();
        bool CreateGpuTimingQueries();
        void ResolveGpuTimingFrame(GpuTimingFrame& frame);
        bool SaveSettingsToFile(const std::wstring& filePath) const;
        bool LoadSettingsFromFile(const std::wstring& filePath);
        bool ExportCurrentFrameCsv(const std::wstring& filePath);
        bool ExportPerformanceCsv(const std::wstring& filePath) const;
        bool ExportActiveSetPlanJson(const std::wstring& filePath) const;
        bool ExportRuntimeReport(const std::wstring& filePath) const;
        bool ReadBackCurrentParticlePositions(std::vector<GpuParticle>& outParticles);
        bool PickStrandAtPoint(POINT clientPoint);
        bool ApplySelectedStrandOffset(const hairgpu::Vec3& rootOffset, const hairgpu::Vec3& tipOffset);
        bool BeginStrandGizmoDrag(POINT clientPoint);
        void UpdateStrandGizmoDrag(POINT clientPoint);
        void EndStrandGizmoDrag(bool cancelDrag);
        hairgpu::Vec3 GetSelectedStrandAnchorPoint(bool root) const;
        bool ProjectWorldToScreen(const hairgpu::Vec3& world, float& outX, float& outY) const;
        void CommitEditedRestState();
        void PushStrandEditUndoSnapshot();
        bool UndoStrandEdit();
        bool RedoStrandEdit();
        bool SaveStrandEditSessionToPath(const std::filesystem::path& path) const;
        bool LoadStrandEditSessionFromPath(const std::filesystem::path& path);
        bool LoadHairAssetFromPath(const std::wstring& filePath);
        bool LoadScalpAnimationFromPath(const std::wstring& filePath, bool showMessageOnFailure);
        bool BrowseAndSetEditStackPath();
        bool ClearEditStackAndRestoreAsset();
        bool ApplyEditStackToCurrentAsset(bool showMessageOnFailure);
        bool ApplyEditStackToAsset(hairgpu::HairAsset& asset, std::wstring* outStatusText = nullptr);
        bool ApplyInlineEditStackFromUi(bool showMessageOnFailure);
        bool SaveInlineEditStackToPath(const std::wstring& filePath, bool applyAfterSave);
        void ResetInlineEditStackText();
        std::wstring GetInlineEditStackText() const;
        std::filesystem::path GetInlineEditStackTempPath() const;
        bool BrowseAndSetMaskPath(bool partingMask);
        void RefreshActiveSetTuningLabels();
        void RefreshQuickEditFormLabels();
        void SyncQuickEditFormControlsFromState();
        void RefreshInlineLayerListFromText(int preferredSelection = -1);
        bool TryBuildSelectedInlineLayerPreview(std::vector<float>& outGuideInfluence, uint32_t& outActiveGuideCount, float& outMaxInfluence) const;
        bool IsEditInfluenceOverlayMode() const;
        bool IsBoundaryBeforeAfterOverlayMode() const;
        bool IsActiveSetRiskOverlayMode() const;
        const wchar_t* GetMaskOverlayModeDisplayName() const;
        float GetActiveSetOverlayScoreValue(const hairgpu::ActiveSetClusterScore& score) const;
        int GetSelectedInlineLayerIndex() const;
        void SetSelectedInlineLayerIndex(int layerIndex);
        void LoadQuickEditFormFromInlineText(int preferredSelection = -1);
        bool PushQuickEditFormToInlineText(bool replaceSelectedLayer, bool applyAfterPush);
        bool MoveSelectedInlineLayer(int direction, bool applyAfterMove);
        bool ToggleSelectedInlineLayerEnabled(bool applyAfterToggle);
        bool DuplicateSelectedInlineLayer(bool applyAfterDuplicate);
        bool DeleteSelectedInlineLayer(bool applyAfterDelete);
        hairgpu::EditLayer BuildQuickEditLayerFromUi() const;

        void ApplySampleMaskOverrides();
        void RefreshMaskPreviewControls();
        void ReleaseMaskPreviewBitmaps();
        void ShowMaskPreviewPopup(bool partingMask);
        void UpdateScalpOverlayVertexBuffer();
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
        POINT m_mouseDownPoint = {};
        bool m_syncingControls = false;
        bool m_enableSelfCollision = true;
        bool m_enableScalpMotion = false;
        bool m_showColliderDebug = false;
        bool m_showScalpWireframe = false;
        bool m_showScalpMesh = true;
        bool m_showGridBounds = false;
        bool m_showFloorGuide = false;
        bool m_showGuideSkeleton = false;
        bool m_showFollowerSkeleton = false;
        bool m_showRootMarkers = false;
        bool m_showFollowLinks = false;
        bool m_showTestMesh = false; // legacy CPU test mesh path disabled in runtime UI
        POINT m_lastMouse = {};
        struct StrandEditSnapshot
        {
            std::vector<hairgpu::Vec3> restPoints;
            std::vector<GpuParticle> initialParticles;
            int32_t selectedStrandIndex = -1;
        };

        int32_t m_selectedStrandIndex = -1;
        float m_lastSelectedStrandScreenDistance = 0.0f;
        StrandGizmoMode m_strandGizmoMode = StrandGizmoMode::TipTranslate;
        GizmoAxis m_activeGizmoAxis = GizmoAxis::None;
        bool m_gizmoDragging = false;
        POINT m_gizmoDragStartPoint = {};
        std::vector<hairgpu::Vec3> m_gizmoDragBaseRestPoints;
        std::vector<StrandEditSnapshot> m_strandUndoStack;
        std::vector<StrandEditSnapshot> m_strandRedoStack;
        hairgpu::BoneAnimationClip m_scalpAnimationClip;
        std::vector<hairgpu::Mat4> m_currentScalpBoneMatrices;
        std::wstring m_lastScalpAnimationSource;

        float m_cameraYaw = 0.1f;
        float m_cameraPitch = 0.35f;
        float m_cameraDistance = 2.5f;
        hairgpu::Vec3 m_cameraTarget = { 0.0f, 1.2f, 0.0f };
        hairgpu::Vec3 m_cameraPosition = { 0.0f, 0.0f, 0.0f };
        hairgpu::Vec3 m_scalpCenter = { 0.0f, 1.60f, 0.0f };
        hairgpu::Vec3 m_neckPivot = { 0.0f, 1.42f, -0.03f };
        hairgpu::Vec3 m_assetBoundsMin = { -0.5f, 1.0f, -0.5f };
        hairgpu::Vec3 m_assetBoundsMax = { 0.5f, 2.0f, 0.5f };
        hairgpu::Vec3 m_assetBoundsCenter = { 0.0f, 1.5f, 0.0f };
        float m_assetBoundsRadius = 0.75f;
        float m_widthScale = 0.56f;
        float m_selfCollisionRadius = 0.012f;
        float m_selfCollisionStiffness = 0.35f;
        bool m_enableGridSelfCollision = true;
        uint32_t m_selfCollisionBroadphaseMode = 0u;
        bool m_enableRuntimeAngularBend = true;
        float m_runtimeAngularBendRadians = 0.18f;
        float m_runtimeAngularBendCompliance = 5.0e-6f;
        float m_runtimeAngularBendStiffness = 0.35f;
        float m_runtimeAngularBendDamping = 0.08f;
        uint32_t m_sortedBroadphasePaddedCount = 1u;

        uint32_t m_builderGuideCount = 2500;
        uint32_t m_builderFollowersPerGuide = 9;
        uint32_t m_builderParticlesPerStrand = 15;
        std::wstring m_activePresetName = L"Rapunzel Preview";
        std::wstring m_lastAssetSource = L"Sample Hybrid";
        std::wstring m_lastImportStatus = L"Sample asset active";
        enum class MaskSourceMode : uint32_t
        {
            Auto = 0,
            ExternalFiles = 1,
            Procedural = 2,
        };

        std::wstring m_partingMaskPath;
        std::wstring m_regionMaskPath;
        std::wstring m_editStackPath;
        std::wstring m_lastEditStackStatus = L"No edit stack";
        hairgpu::RuntimeEditState m_runtimeEditState;
        hairgpu::RegionSelectMode m_quickEditMode = hairgpu::RegionSelectMode::Ellipse;
        bool m_quickEditEnabled = true;
        std::wstring m_quickEditLayerName = L"quick_form";
        float m_quickEditFeather = 0.20f;
        float m_quickEditLengthScale = 1.00f;
        float m_quickEditLayerWeight = 1.00f;
        float m_quickEditCenterU = 0.50f;
        float m_quickEditCenterV = 0.50f;
        float m_quickEditRadiusU = 0.18f;
        float m_quickEditRadiusV = 0.18f;
        float m_quickEditInnerRadius = 0.00f;
        float m_quickEditAngleCenterRadians = 0.00f;
        float m_quickEditAngleHalfExtentRadians = 3.1415926f;
        float m_quickEditRootOffsetX = 0.00f;
        float m_quickEditRootOffsetY = 0.00f;
        float m_quickEditRootOffsetZ = 0.00f;
        float m_quickEditTipOffsetX = 0.02f;
        float m_quickEditTipOffsetY = 0.00f;
        float m_quickEditTipOffsetZ = -0.08f;
        float m_quickEditSideOffset = 0.00f;
        float m_quickEditYawRadians = 0.00f;
        float m_quickEditWidthScale = 1.00f;
        float m_quickEditFollowerInfluence = 1.00f;
        float m_quickEditBoundarySmoothing = 0.42f;
        float m_quickEditBoundaryRadiusUv = 0.10f;
        uint32_t m_quickEditGroupId = UINT32_MAX;
        bool m_quickEditGuidesOnly = true;
        int m_selectedInlineLayerIndex = 0;

        hairgpu::HairAsset m_baseAsset;
        MaskSourceMode m_maskSourceMode = MaskSourceMode::Auto;
        MaskOverlayMode m_maskOverlayMode = MaskOverlayMode::None;

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
        std::vector<GpuDebugLineEntry> m_gpuDebugLineEntries;
        std::vector<GpuParticle> m_debugReadbackParticles;
        bool m_enableActiveSetRuntime = false;
        hairgpu::ActiveSetClusterSettings m_activeSetClusterSettings;
        hairgpu::ActiveSetScoringSettings m_activeSetScoringSettings;
        hairgpu::ActiveSetBudget m_activeSetBudget;
        std::vector<hairgpu::ActiveSetCluster> m_activeSetClusters;
        hairgpu::ActiveSetPlan m_activeSetPlan;
        std::vector<hairgpu::Vec3> m_activeSetPreviousGuideRoots;
        std::vector<float> m_activeSetGuideContactScores;
        std::vector<float> m_activeSetGuideUncertainty;
        std::wstring m_activeSetLastStatus = L"Not evaluated";
        float m_activeSetEvaluationMilliseconds = 0.0f;
        hairgpu::SparseCorrectiveSettings m_sparseCorrectiveSettings;
        hairgpu::SparseCorrectiveReport m_sparseCorrectiveReport;
        std::vector<PerformanceSample> m_performanceSamples;

        Microsoft::WRL::ComPtr<ID3D11Device> m_device;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_context;
        Microsoft::WRL::ComPtr<IDXGISwapChain> m_swapChain;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_rtv;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> m_depthTexture;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView> m_dsv;

        Microsoft::WRL::ComPtr<ID3D11VertexShader> m_particleVS;
        Microsoft::WRL::ComPtr<ID3D11GeometryShader> m_particlePointGS;
        Microsoft::WRL::ComPtr<ID3D11GeometryShader> m_particleBillboardGS;
        Microsoft::WRL::ComPtr<ID3D11PixelShader> m_particlePS;
        Microsoft::WRL::ComPtr<ID3D11VertexShader> m_ribbonVS;
        Microsoft::WRL::ComPtr<ID3D11GeometryShader> m_ribbonGS;
        Microsoft::WRL::ComPtr<ID3D11PixelShader> m_ribbonPS;
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
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_clearHashGridCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_buildHashGridCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_clearSortedRangesCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_buildSortedEntriesCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_bitonicSortCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_buildSortedRangesCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_selfCollisionCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_followCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_compactFollowerDispatchCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_buildFollowDispatchArgsCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_compactGuideDispatchCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_buildGuideDispatchArgsCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_buildGuidePointDispatchArgsCS;
        Microsoft::WRL::ComPtr<ID3D11InputLayout> m_segmentInputLayout;
        Microsoft::WRL::ComPtr<ID3D11InputLayout> m_debugInputLayout;
        Microsoft::WRL::ComPtr<ID3D11InputLayout> m_scalpInputLayout;
        Microsoft::WRL::ComPtr<ID3D11InputLayout> m_hairMeshInputLayout;

        StructuredBuffer m_positions[2];
        StructuredBuffer m_previousPositions[2];
        StructuredBuffer m_restPositions;
        StructuredBuffer m_pointWidths;
        StructuredBuffer m_followBuffer;
        StructuredBuffer m_renderStrandTiersBuffer;
        StructuredBuffer m_guideTierAssignmentsBuffer;
        StructuredBuffer m_followerTierAssignmentsBuffer;
        StructuredBuffer m_compactedFollowerIndicesBuffer;
        StructuredBuffer m_compactedFollowerCounterBuffer;
        StructuredBuffer m_compactedGuideIndicesBuffer;
        StructuredBuffer m_compactedGuideCounterBuffer;
        StructuredBuffer m_scalpPositions;
        StructuredBuffer m_scalpTrianglesBuffer;
        StructuredBuffer m_rootBindingsBuffer;
        StructuredBuffer m_sdfValuesBuffer;
        StructuredBuffer m_distanceLambdaBuffer;
        StructuredBuffer m_bendLambdaBuffer;
        StructuredBuffer m_gridCellCountsBuffer;
        StructuredBuffer m_gridCellIndicesBuffer;
        StructuredBuffer m_gridHashHeadsBuffer;
        StructuredBuffer m_gridParticleNextBuffer;
        StructuredBuffer m_sortedKeysBuffer;
        StructuredBuffer m_sortedParticleIndicesBuffer;
        StructuredBuffer m_sortedCellStartsBuffer;
        StructuredBuffer m_sortedCellEndsBuffer;
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_segmentVertexBuffer;
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_strandTubeVertexBuffer;
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_strandTubeIndexBuffer;
        uint32_t m_strandTubeIndexCount = 0;
        uint32_t m_strandTubeRingSides = 0;
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_hairMeshVertexBuffer;
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_debugLineTemplateVertexBuffer;
        StructuredBuffer m_debugLineEntriesBuffer;
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_followDispatchArgsBuffer;
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_followDispatchArgsUav;
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_guideDispatchArgsBuffer;
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_guideDispatchArgsUav;
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_guidePointDispatchArgsBuffer;
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_guidePointDispatchArgsUav;
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_scalpVertexBuffer;
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_scalpIndexBuffer;
        uint32_t m_scalpIndexCount = 0u;
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_simConstantsHair;
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_simConstantsActiveSet;
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_simConstantsScalp;
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_bitonicConstants;
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_renderConstants;
        Microsoft::WRL::ComPtr<ID3D11RasterizerState> m_rasterizerState;
        Microsoft::WRL::ComPtr<ID3D11BlendState> m_blendState;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilState> m_depthState;
        size_t m_hairMeshVertexCapacity = 0;
        size_t m_debugLineEntryCapacity = 0;
        bool m_gpuDebugLinesDirty = true;
        DebugLineStats m_debugLineStats = {};
        uint32_t m_lastCurrentHairDrawCalls = 0u;
        uint64_t m_lastCurrentHairVertexCount = 0u;
        uint64_t m_lastCurrentHairPrimitiveCount = 0u;
        uint32_t m_lastCurrentHairInstanceCount = 0u;
        uint32_t m_lastTotalDrawCalls = 0u;
        float m_lastGpuFrameMilliseconds = 0.0f;
        float m_lastGpuSimulationMilliseconds = 0.0f;
        float m_lastGpuHairDrawMilliseconds = 0.0f;
        float m_lastGpuDebugDrawMilliseconds = 0.0f;
        uint32_t m_lastEstimatedVisibleParticles = 0u;
        uint32_t m_lastReadbackParticleCount = 0u;
        bool m_lastVisibilitySuspect = false;
        bool m_lastUsedCpuVisibilityFallback = false;
        bool m_lastVisibilityReadbackValid = true;
        bool m_lastVisibilityUsedInitialPoseFallback = false;
        uint32_t m_visibilityReadbackFailureCount = 0u;
        uint32_t m_visibilityRecoveryCount = 0u;
        CpuReadbackMode m_cpuReadbackMode = CpuReadbackMode::OnDemandVisibility;
        bool m_visibilityRecoveryPending = true;
        uint32_t m_consecutiveInvisibleFrames = 0u;
        uint32_t m_framesSinceVisibilityCheck = 0u;
        uint32_t m_runtimeFollowerUpdatePhase = 0u;
        D3D11_QUERY_DATA_PIPELINE_STATISTICS m_lastHairPipelineStats = {};
        D3D11_QUERY_DATA_PIPELINE_STATISTICS m_lastDebugPipelineStats = {};
        std::array<GpuTimingFrame, 3> m_gpuTimingFrames = {};
        uint32_t m_gpuTimingWriteIndex = 0u;
        int32_t m_activeGpuTimingFrameIndex = -1;

        RenderMode m_renderMode = RenderMode::StrandRibbon;
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
        HWND m_btnBrowseEditStack = nullptr;
        HWND m_btnClearEditStack = nullptr;
        HWND m_btnApplyInlineEditStack = nullptr;
        HWND m_btnSaveInlineEditStack = nullptr;
        HWND m_btnResetInlineEditStack = nullptr;
        HWND m_labelQuickEditForm = nullptr;
        HWND m_labelLayerList = nullptr;
        HWND m_listInlineLayers = nullptr;
        HWND m_labelQuickEditName = nullptr;
        HWND m_editQuickEditName = nullptr;
        HWND m_btnQuickEditMoveUp = nullptr;
        HWND m_btnQuickEditMoveDown = nullptr;
        HWND m_checkQuickEditEnabled = nullptr;
        HWND m_btnQuickEditDelete = nullptr;
        HWND m_btnQuickEditDuplicate = nullptr;
        HWND m_comboQuickEditMode = nullptr;
        HWND m_trackQuickEditFeather = nullptr;
        HWND m_trackQuickEditLengthScale = nullptr;
        HWND m_trackQuickEditLayerWeight = nullptr;
        HWND m_trackQuickEditCenterU = nullptr;
        HWND m_trackQuickEditCenterV = nullptr;
        HWND m_trackQuickEditRadiusU = nullptr;
        HWND m_trackQuickEditRadiusV = nullptr;
        HWND m_trackQuickEditInnerRadius = nullptr;
        HWND m_trackQuickEditAngleCenter = nullptr;
        HWND m_trackQuickEditAngleHalfExtent = nullptr;
        HWND m_trackQuickEditRootOffsetX = nullptr;
        HWND m_trackQuickEditRootOffsetY = nullptr;
        HWND m_trackQuickEditRootOffsetZ = nullptr;
        HWND m_trackQuickEditTipOffsetX = nullptr;
        HWND m_trackQuickEditTipOffsetY = nullptr;
        HWND m_trackQuickEditTipOffsetZ = nullptr;
        HWND m_trackQuickEditSideOffset = nullptr;
        HWND m_trackQuickEditYaw = nullptr;
        HWND m_trackQuickEditWidthScale = nullptr;
        HWND m_trackQuickEditFollowerInfluence = nullptr;
        HWND m_trackQuickEditBoundarySmoothing = nullptr;
        HWND m_trackQuickEditBoundaryRadiusUv = nullptr;
        HWND m_editQuickEditGroupId = nullptr;
        HWND m_checkQuickEditGuidesOnly = nullptr;
        HWND m_btnQuickEditReplace = nullptr;
        HWND m_btnQuickEditAppend = nullptr;

        HWND m_btnBrowsePartingMask = nullptr;
        HWND m_btnBrowseRegionMask = nullptr;
        HWND m_comboMaskSource = nullptr;
        HWND m_comboMaskOverlay = nullptr;
        HWND m_previewPartingMask = nullptr;
        HWND m_previewRegionMask = nullptr;
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
        HWND m_labelEditStack = nullptr;
        HWND m_labelInlineEditStack = nullptr;
        HWND m_editInlineEditStack = nullptr;
        HWND m_labelQuickEditFeather = nullptr;
        HWND m_labelQuickEditLengthScale = nullptr;
        HWND m_labelQuickEditLayerWeight = nullptr;
        HWND m_labelQuickEditCenterU = nullptr;
        HWND m_labelQuickEditCenterV = nullptr;
        HWND m_labelQuickEditRadiusU = nullptr;
        HWND m_labelQuickEditRadiusV = nullptr;
        HWND m_labelQuickEditInnerRadius = nullptr;
        HWND m_labelQuickEditAngleCenter = nullptr;
        HWND m_labelQuickEditAngleHalfExtent = nullptr;
        HWND m_labelQuickEditRootOffsetX = nullptr;
        HWND m_labelQuickEditRootOffsetY = nullptr;
        HWND m_labelQuickEditRootOffsetZ = nullptr;
        HWND m_labelQuickEditTipOffsetX = nullptr;
        HWND m_labelQuickEditTipOffsetY = nullptr;
        HWND m_labelQuickEditTipOffsetZ = nullptr;
        HWND m_labelQuickEditSideOffset = nullptr;
        HWND m_labelQuickEditYaw = nullptr;
        HWND m_labelQuickEditWidthScale = nullptr;
        HWND m_labelQuickEditFollowerInfluence = nullptr;
        HWND m_labelQuickEditBoundarySmoothing = nullptr;
        HWND m_labelQuickEditBoundaryRadiusUv = nullptr;
        HWND m_labelQuickEditGroupId = nullptr;
        HWND m_labelQuickEditVisualization = nullptr;

        HWND m_labelPartingMask = nullptr;
        HWND m_labelRegionMask = nullptr;
        HWND m_labelMaskSource = nullptr;
        HWND m_labelMaskOverlay = nullptr;
        HBITMAP m_partingPreviewBitmap = nullptr;
        HBITMAP m_regionPreviewBitmap = nullptr;
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
        HWND m_labelActiveSetTuning = nullptr;
        HWND m_checkActiveSetAdaptiveBudget = nullptr;
        HWND m_checkActiveSetHardClamp = nullptr;
        HWND m_labelActiveSetMaxClusters = nullptr;
        HWND m_trackActiveSetMaxClusters = nullptr;
        HWND m_labelActiveSetMaxGuides = nullptr;
        HWND m_trackActiveSetMaxGuides = nullptr;
        HWND m_labelActiveSetThreshold = nullptr;
        HWND m_trackActiveSetThreshold = nullptr;
        HWND m_labelActiveSetTargetFrame = nullptr;
        HWND m_trackActiveSetTargetFrame = nullptr;
        HWND m_labelActiveSetFixedCost = nullptr;
        HWND m_trackActiveSetFixedCost = nullptr;
        HWND m_labelActiveSetClusterCost = nullptr;
        HWND m_trackActiveSetClusterCost = nullptr;
        HWND m_labelActiveSetScoreWeights = nullptr;
        HWND m_labelActiveSetMotionWeight = nullptr;
        HWND m_trackActiveSetMotionWeight = nullptr;
        HWND m_labelActiveSetContactWeight = nullptr;
        HWND m_trackActiveSetContactWeight = nullptr;
        HWND m_labelActiveSetUncertaintyWeight = nullptr;
        HWND m_trackActiveSetUncertaintyWeight = nullptr;
        HWND m_labelActiveSetEditWeight = nullptr;
        HWND m_trackActiveSetEditWeight = nullptr;
        HWND m_btnExportActiveSetJson = nullptr;
        HWND m_btnExportRuntimeReport = nullptr;
    };
}
