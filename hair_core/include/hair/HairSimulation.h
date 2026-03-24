#pragma once

#include <cstddef>
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
        Vec3 scalpCenter = Vec3(420.0f, 120.0f, 0.0f);
        float scalpRadius = 84.0f;
        TestScenePreset scenePreset = TestScenePreset::Curtain;
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

        const SimulationConfig& GetConfig() const;
        const std::vector<Strand>& GetStrands() const;
        const std::vector<SphereCollider>& GetColliders() const;
        int GetTotalParticleCount() const;
        float GetElapsedTimeSeconds() const;

        int GetStrandCount() const;
        int GetParticlesPerStrand() const;
        const Vec3* GetFlatPositions() const;
        const Vec3* GetFlatPreviousPositions() const;

    private:
        void BuildDefaultScene();
        void RebuildColliders();
        void Integrate(float deltaTime);
        void SolveConstraints();
        void SolveDistanceConstraints();
        void SolveBendConstraints();
        void SolveClusterConstraints();
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
        mutable std::vector<Strand> m_legacyStrands;
        mutable bool m_legacyDirty = true;
        int m_particlesPerStrand = 0;
        float m_elapsedTimeSeconds = 0.0f;
    };
} // namespace hair
