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
        int strandCount = 18;
        int segmentCount = 16;
        float segmentLength = 18.0f;
        float rootSpacing = 12.0f;
        float gravity = 1400.0f;
        float damping = 0.035f;
        int solverIterations = 8;
        float bendStiffness = 0.24f;
        float floorY = 620.0f;
        Vec3 scalpCenter = Vec3(420.0f, 120.0f, 0.0f);
        float scalpRadius = 80.0f;
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

        const SimulationConfig& GetConfig() const;
        const std::vector<Strand>& GetStrands() const;
        const std::vector<SphereCollider>& GetColliders() const;

    private:
        void BuildDefaultScene();
        void Integrate(float deltaTime);
        void SolveConstraints();
        void SolveDistanceConstraints();
        void SolveBendConstraints();
        void SolveCollisions();
        void PinRoots();
        void RebuildColliders();

        SimulationConfig m_config;
        std::vector<Strand> m_strands;
        std::vector<SphereCollider> m_colliders;
        std::vector<Vec3> m_rootPositions;
    };
} // namespace hair
