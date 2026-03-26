#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace hair
{
    struct Vec3
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;

        Vec3() = default;
        Vec3(float xValue, float yValue, float zValue = 0.0f) : x(xValue), y(yValue), z(zValue) {}

        Vec3 operator+(const Vec3& rhs) const;
        Vec3 operator-(const Vec3& rhs) const;
        Vec3 operator*(float scalar) const;
        Vec3 operator/(float scalar) const;
        Vec3& operator+=(const Vec3& rhs);
        Vec3& operator-=(const Vec3& rhs);
        Vec3& operator*=(float scalar);
    };

    float Dot(const Vec3& a, const Vec3& b);
    float Length(const Vec3& value);
    Vec3 Normalize(const Vec3& value);
    Vec3 Lerp(const Vec3& a, const Vec3& b, float t);
    Vec3 Cross(const Vec3& a, const Vec3& b);

    enum class TestScenePreset
    {
        Curtain = 0,
        SweepLeft = 1,
        CenterPart = 2
    };

    struct SphereCollider
    {
        Vec3 center;
        float radius = 0.0f;
    };

    struct CapsuleCollider
    {
        Vec3 pointA;
        Vec3 pointB;
        float radius = 0.0f;
    };

    struct PlaneCollider
    {
        Vec3 normal = Vec3(0.0f, -1.0f, 0.0f);
        float offset = 0.0f;
    };

    struct Particle
    {
        Vec3 position;
        Vec3 previousPosition;
        bool pinned = false;
    };

    struct Strand
    {
        std::vector<Particle> particles;
        std::vector<float> restLengths;
    };

    struct SimulationConfig
    {
        int strandCount = 4000;
        int segmentCount = 10;
        float segmentLength = 11.0f;
        float rootSpacing = 2.2f;
        float gravity = 1200.0f;
        float damping = 0.025f;
        int solverIterations = 4;
        float bendStiffness = 0.18f;
        float clusterStiffness = 0.10f;
        float windStrength = 0.0f;
        float windDirectionX = 1.0f;
        float windPulseFrequency = 1.0f;
        float floorY = 650.0f;
        float floorFriction = 0.08f;
        float colliderFriction = 0.04f;
        float selfCollisionRadius = 5.0f;
        float selfCollisionStiffness = 0.18f;
        int selfCollisionNeighborSpan = 3;
        float renderRootWidth = 0.90f;
        float renderTipWidth = 0.10f;
        Vec3 scalpCenter = Vec3(420.0f, 120.0f, 0.0f);
        float scalpRadius = 84.0f;
        TestScenePreset scenePreset = TestScenePreset::Curtain;
    };

    struct RenderInterpolationSettings
    {
        int childrenPerGuide = 3;
        float spread = 1.6f;
        float clumpBlend = 0.55f;
        float frizz = 0.10f;
        float rootSeparationBias = 0.75f;
        int randomSeed = 1;
        bool includeGuideStrands = true;
    };

    struct TubeMeshExportSettings
    {
        int childrenPerGuide = 3;
        int radialSegments = 6;
        float rootRadius = 0.70f;
        float tipRadius = 0.08f;
        float spread = 1.4f;
        float frizz = 0.06f;
        int randomSeed = 1;
        bool includeGuideStrands = true;
    };

    struct SimulationStats
    {
        int strandCount = 0;
        int particlesPerStrand = 0;
        int particleCount = 0;
        float averageSegmentError = 0.0f;
        float maxSegmentError = 0.0f;
        float averageVelocity = 0.0f;
        float maxVelocity = 0.0f;
        float averageRootTipDistance = 0.0f;
    };

    class HairSimulation
    {
    public:
        HairSimulation();
        explicit HairSimulation(const SimulationConfig& config);

        void Reset();
        void Reset(const SimulationConfig& config);
        void Step(float deltaTime);
        void UpdateConfig(const SimulationConfig& config);

        bool SaveSnapshot(const char* filePath) const;
        bool LoadSnapshot(const char* filePath);
        bool SaveCurveCache(const char* filePath, const RenderInterpolationSettings& settings) const;
        bool ExportGuideLinesObj(const char* filePath) const;
        bool ExportRenderLinesObj(const char* filePath, const RenderInterpolationSettings& settings) const;
        bool ExportTubeMeshObj(const char* filePath, const TubeMeshExportSettings& settings) const;

        bool SetFlatState(const SimulationConfig& config,
            const Vec3* positions,
            const Vec3* previousPositions,
            const Vec3* rootPositions,
            int positionsCount,
            int particlesPerStrand);

        void SetSphereColliders(const SphereCollider* colliders, int count);
        void SetCapsuleColliders(const CapsuleCollider* colliders, int count);
        void SetPlaneColliders(const PlaneCollider* colliders, int count);
        void RebuildDefaultCollisionRig();

        std::vector<Strand> GenerateRenderStrands(const RenderInterpolationSettings& settings) const;
        SimulationStats ComputeStats(float deltaTimeHint = 1.0f / 60.0f) const;

        const SimulationConfig& GetConfig() const;
        const std::vector<Strand>& GetStrands() const;
        const std::vector<SphereCollider>& GetColliders() const;
        const std::vector<CapsuleCollider>& GetCapsuleColliders() const;
        const std::vector<PlaneCollider>& GetPlaneColliders() const;
        int GetTotalParticleCount() const;
        float GetElapsedTimeSeconds() const;

        int GetStrandCount() const;
        int GetParticlesPerStrand() const;
        const Vec3* GetFlatPositions() const;
        const Vec3* GetFlatPreviousPositions() const;
        const Vec3* GetFlatRoots() const;

    private:
        void BuildDefaultScene();
        void RebuildColliders();
        void Integrate(float deltaTime);
        void SolveConstraints();
        void SolveDistanceConstraints();
        void SolveBendConstraints();
        void SolveClusterConstraints();
        void SolveSelfCollisionConstraints();
        void SolveCollisions();
        void PinRoots();
        void SyncLegacyStrands() const;

        std::size_t ParticleIndex(int strandIndex, int particleIndex) const;

        SimulationConfig m_config;
        std::vector<Vec3> m_positions;
        std::vector<Vec3> m_previousPositions;
        std::vector<Vec3> m_rootPositions;
        std::vector<Vec3> m_clusterScratch;
        std::vector<SphereCollider> m_colliders;
        std::vector<CapsuleCollider> m_capsuleColliders;
        std::vector<PlaneCollider> m_planeColliders;
        mutable std::vector<Strand> m_legacyStrands;
        mutable bool m_legacyDirty = true;
        int m_particlesPerStrand = 0;
        float m_elapsedTimeSeconds = 0.0f;
    };
} // namespace hair
