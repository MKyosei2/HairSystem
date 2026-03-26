#include "hair/HairSimulation.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>

namespace hair
{
    namespace
    {
        constexpr float kEpsilon = 1.0e-5f;
        constexpr float kTau = 6.28318530717958647692f;

        float Clamp(float value, float minValue, float maxValue)
        {
            return std::max(minValue, std::min(value, maxValue));
        }

        int ClampInt(int value, int minValue, int maxValue)
        {
            return std::max(minValue, std::min(value, maxValue));
        }

        float Fract(float value)
        {
            return value - std::floor(value);
        }

        float Hash01(int value)
        {
            const float v = std::sin(static_cast<float>(value) * 12.9898f + 78.233f) * 43758.5453f;
            return Fract(v);
        }

        Vec3 SafeNormal(const Vec3& value, const Vec3& fallback = Vec3(0.0f, -1.0f, 0.0f))
        {
            const float length = Length(value);
            if (length < kEpsilon)
            {
                return fallback;
            }
            return value / length;
        }

        TestScenePreset SanitizeScenePreset(TestScenePreset preset)
        {
            switch (preset)
            {
            case TestScenePreset::Curtain:
            case TestScenePreset::SweepLeft:
            case TestScenePreset::CenterPart:
                return preset;
            default:
                return TestScenePreset::Curtain;
            }
        }

        bool ReadVec3(std::istream& input, Vec3& value)
        {
            return static_cast<bool>(input >> value.x >> value.y >> value.z);
        }

        void WriteVec3(std::ostream& output, const Vec3& value)
        {
            output << value.x << ' ' << value.y << ' ' << value.z;
        }

        Vec3 ClosestPointOnSegment(const Vec3& point, const Vec3& a, const Vec3& b)
        {
            const Vec3 ab = b - a;
            const float denom = Dot(ab, ab);
            if (denom < kEpsilon)
            {
                return a;
            }
            const float t = Clamp(Dot(point - a, ab) / denom, 0.0f, 1.0f);
            return a + ab * t;
        }

        void ApplySurfaceFriction(Vec3& previousPosition, const Vec3& currentPosition, const Vec3& correctedPosition, const Vec3& normal, float friction)
        {
            const Vec3 originalVelocity = currentPosition - previousPosition;
            const Vec3 normalComponent = normal * Dot(originalVelocity, normal);
            const Vec3 tangentialComponent = originalVelocity - normalComponent;
            previousPosition = correctedPosition - tangentialComponent * (1.0f - Clamp(friction, 0.0f, 1.0f));
        }

        Vec3 OrthonormalPerpendicular(const Vec3& tangent)
        {
            const Vec3 up = (std::abs(tangent.y) < 0.92f) ? Vec3(0.0f, 1.0f, 0.0f) : Vec3(1.0f, 0.0f, 0.0f);
            return SafeNormal(Cross(up, tangent), Vec3(1.0f, 0.0f, 0.0f));
        }

        void AppendLineObj(const std::vector<Strand>& strands, std::ostream& output)
        {
            std::size_t vertexOffset = 1;
            for (const Strand& strand : strands)
            {
                for (const Particle& particle : strand.particles)
                {
                    output << "v " << particle.position.x << ' ' << particle.position.y << ' ' << particle.position.z << '\n';
                }
                for (std::size_t index = 0; index + 1 < strand.particles.size(); ++index)
                {
                    output << "l " << (vertexOffset + index) << ' ' << (vertexOffset + index + 1) << '\n';
                }
                vertexOffset += strand.particles.size();
            }
        }
    }

    Vec3 Vec3::operator+(const Vec3& rhs) const
    {
        return Vec3(x + rhs.x, y + rhs.y, z + rhs.z);
    }

    Vec3 Vec3::operator-(const Vec3& rhs) const
    {
        return Vec3(x - rhs.x, y - rhs.y, z - rhs.z);
    }

    Vec3 Vec3::operator*(float scalar) const
    {
        return Vec3(x * scalar, y * scalar, z * scalar);
    }

    Vec3 Vec3::operator/(float scalar) const
    {
        return Vec3(x / scalar, y / scalar, z / scalar);
    }

    Vec3& Vec3::operator+=(const Vec3& rhs)
    {
        x += rhs.x;
        y += rhs.y;
        z += rhs.z;
        return *this;
    }

    Vec3& Vec3::operator-=(const Vec3& rhs)
    {
        x -= rhs.x;
        y -= rhs.y;
        z -= rhs.z;
        return *this;
    }

    Vec3& Vec3::operator*=(float scalar)
    {
        x *= scalar;
        y *= scalar;
        z *= scalar;
        return *this;
    }

    float Dot(const Vec3& a, const Vec3& b)
    {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    float Length(const Vec3& value)
    {
        return std::sqrt(Dot(value, value));
    }

    Vec3 Normalize(const Vec3& value)
    {
        const float length = Length(value);
        if (length < kEpsilon)
        {
            return Vec3();
        }
        return value / length;
    }

    Vec3 Lerp(const Vec3& a, const Vec3& b, float t)
    {
        return a * (1.0f - t) + b * t;
    }

    Vec3 Cross(const Vec3& a, const Vec3& b)
    {
        return Vec3(
            a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x);
    }

    HairSimulation::HairSimulation()
    {
        Reset();
    }

    HairSimulation::HairSimulation(const SimulationConfig& config)
        : m_config(config)
    {
        Reset();
    }

    std::size_t HairSimulation::ParticleIndex(int strandIndex, int particleIndex) const
    {
        return static_cast<std::size_t>(strandIndex) * static_cast<std::size_t>(m_particlesPerStrand)
            + static_cast<std::size_t>(particleIndex);
    }

    void HairSimulation::Reset()
    {
        m_config.strandCount = ClampInt(m_config.strandCount, 1, 25000);
        m_config.segmentCount = ClampInt(m_config.segmentCount, 2, 32);
        m_config.segmentLength = std::max(0.1f, m_config.segmentLength);
        m_config.rootSpacing = std::max(0.0f, m_config.rootSpacing);
        m_config.solverIterations = ClampInt(m_config.solverIterations, 1, 16);
        m_config.damping = Clamp(m_config.damping, 0.0f, 0.20f);
        m_config.bendStiffness = Clamp(m_config.bendStiffness, 0.0f, 1.0f);
        m_config.clusterStiffness = Clamp(m_config.clusterStiffness, 0.0f, 1.0f);
        m_config.windDirectionX = Clamp(m_config.windDirectionX, -1.0f, 1.0f);
        m_config.windPulseFrequency = std::max(0.0f, m_config.windPulseFrequency);
        m_config.floorFriction = Clamp(m_config.floorFriction, 0.0f, 1.0f);
        m_config.colliderFriction = Clamp(m_config.colliderFriction, 0.0f, 1.0f);
        m_config.selfCollisionRadius = std::max(0.0f, m_config.selfCollisionRadius);
        m_config.selfCollisionStiffness = Clamp(m_config.selfCollisionStiffness, 0.0f, 1.0f);
        m_config.selfCollisionNeighborSpan = ClampInt(m_config.selfCollisionNeighborSpan, 1, 24);
        m_config.renderRootWidth = std::max(0.01f, m_config.renderRootWidth);
        m_config.renderTipWidth = Clamp(m_config.renderTipWidth, 0.001f, m_config.renderRootWidth);
        m_config.scenePreset = SanitizeScenePreset(m_config.scenePreset);
        m_elapsedTimeSeconds = 0.0f;
        BuildDefaultScene();
    }

    void HairSimulation::Reset(const SimulationConfig& config)
    {
        m_config = config;
        Reset();
    }

    void HairSimulation::UpdateConfig(const SimulationConfig& config)
    {
        m_config.gravity = config.gravity;
        m_config.damping = Clamp(config.damping, 0.0f, 0.20f);
        m_config.solverIterations = ClampInt(config.solverIterations, 1, 16);
        m_config.bendStiffness = Clamp(config.bendStiffness, 0.0f, 1.0f);
        m_config.clusterStiffness = Clamp(config.clusterStiffness, 0.0f, 1.0f);
        m_config.windStrength = config.windStrength;
        m_config.windDirectionX = Clamp(config.windDirectionX, -1.0f, 1.0f);
        m_config.windPulseFrequency = std::max(0.0f, config.windPulseFrequency);
        m_config.floorFriction = Clamp(config.floorFriction, 0.0f, 1.0f);
        m_config.colliderFriction = Clamp(config.colliderFriction, 0.0f, 1.0f);
        m_config.selfCollisionRadius = std::max(0.0f, config.selfCollisionRadius);
        m_config.selfCollisionStiffness = Clamp(config.selfCollisionStiffness, 0.0f, 1.0f);
        m_config.selfCollisionNeighborSpan = ClampInt(config.selfCollisionNeighborSpan, 1, 24);
        m_config.renderRootWidth = std::max(0.01f, config.renderRootWidth);
        m_config.renderTipWidth = Clamp(config.renderTipWidth, 0.001f, m_config.renderRootWidth);
        m_config.scenePreset = SanitizeScenePreset(config.scenePreset);
    }

    void HairSimulation::BuildDefaultScene()
    {
        m_particlesPerStrand = m_config.segmentCount + 1;
        const std::size_t totalParticles = static_cast<std::size_t>(m_config.strandCount) * static_cast<std::size_t>(m_particlesPerStrand);
        m_positions.assign(totalParticles, Vec3());
        m_previousPositions.assign(totalParticles, Vec3());
        m_clusterScratch.assign(totalParticles, Vec3());
        m_rootPositions.assign(static_cast<std::size_t>(m_config.strandCount), Vec3());

        const int rootGridCols = std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<float>(m_config.strandCount) * 1.15f))));
        const int rootGridRows = std::max(1, (m_config.strandCount + rootGridCols - 1) / rootGridCols);
        const float rootAreaRadiusX = m_config.scalpRadius * 0.92f;
        const float rootAreaRadiusY = m_config.scalpRadius * 0.52f;
        const float rootAreaDepth = m_config.scalpRadius * 0.70f;

        for (int strandIndex = 0; strandIndex < m_config.strandCount; ++strandIndex)
        {
            const int col = strandIndex % rootGridCols;
            const int row = strandIndex / rootGridCols;
            const float u = (static_cast<float>(col) + 0.5f) / static_cast<float>(rootGridCols);
            const float v = (static_cast<float>(row) + 0.5f) / static_cast<float>(rootGridRows);
            float x = (u - 0.5f) * 2.0f;
            float y = (v - 0.5f) * 2.0f;
            const float radial = x * x + y * y;
            if (radial > 1.0f)
            {
                const float inv = 1.0f / std::sqrt(radial);
                x *= inv;
                y *= inv;
            }

            const float jitter0 = Hash01(strandIndex * 17 + 1) - 0.5f;
            const float jitter1 = Hash01(strandIndex * 17 + 2) - 0.5f;
            const float jitter2 = Hash01(strandIndex * 17 + 3) - 0.5f;
            const Vec3 root(
                m_config.scalpCenter.x + x * rootAreaRadiusX + jitter0 * m_config.rootSpacing,
                m_config.scalpCenter.y - m_config.scalpRadius * 0.55f + y * rootAreaRadiusY + jitter1 * (m_config.rootSpacing * 0.55f),
                jitter2 * rootAreaDepth);
            m_rootPositions[static_cast<std::size_t>(strandIndex)] = root;

            Vec3 sideBias;
            switch (m_config.scenePreset)
            {
            case TestScenePreset::Curtain:
                sideBias = Vec3(x * 1.0f, 1.0f, 0.0f);
                break;
            case TestScenePreset::SweepLeft:
                sideBias = Vec3(-0.9f + x * 0.18f, 1.0f, 0.0f);
                break;
            case TestScenePreset::CenterPart:
                sideBias = Vec3((x >= 0.0f ? 0.75f : -0.75f), 1.0f, 0.0f);
                break;
            default:
                sideBias = Vec3(0.0f, 1.0f, 0.0f);
                break;
            }

            const Vec3 initialDirection = Normalize(Vec3(sideBias.x * 0.18f, 1.0f, sideBias.z));
            for (int particleIndex = 0; particleIndex < m_particlesPerStrand; ++particleIndex)
            {
                const float distanceAlong = static_cast<float>(particleIndex) * m_config.segmentLength;
                const float curl = 0.10f * std::sin((static_cast<float>(particleIndex) * 0.45f) + Hash01(strandIndex * 23 + 9) * kTau);
                Vec3 position = root + initialDirection * distanceAlong;
                position.x += curl * distanceAlong * (0.16f + 0.10f * x);
                position.z += curl * distanceAlong * 0.12f;
                const std::size_t flatIndex = ParticleIndex(strandIndex, particleIndex);
                m_positions[flatIndex] = position;
                m_previousPositions[flatIndex] = position;
            }
            m_positions[ParticleIndex(strandIndex, 0)] = root;
            m_previousPositions[ParticleIndex(strandIndex, 0)] = root;
        }

        RebuildColliders();
        m_legacyDirty = true;
    }

    void HairSimulation::RebuildDefaultCollisionRig()
    {
        RebuildColliders();
    }

    void HairSimulation::RebuildColliders()
    {
        m_colliders.clear();
        m_capsuleColliders.clear();
        m_planeColliders.clear();

        m_colliders.push_back({ m_config.scalpCenter, m_config.scalpRadius });
        m_colliders.push_back({ Vec3(m_config.scalpCenter.x - m_config.scalpRadius * 0.65f, m_config.scalpCenter.y + m_config.scalpRadius * 1.55f, 0.0f), m_config.scalpRadius * 0.50f });
        m_colliders.push_back({ Vec3(m_config.scalpCenter.x + m_config.scalpRadius * 0.65f, m_config.scalpCenter.y + m_config.scalpRadius * 1.55f, 0.0f), m_config.scalpRadius * 0.50f });

        const float shoulderY = m_config.scalpCenter.y + m_config.scalpRadius * 1.85f;
        const float shoulderOffset = m_config.scalpRadius * 0.88f;
        const float shoulderRadius = m_config.scalpRadius * 0.46f;
        m_capsuleColliders.push_back({
            Vec3(m_config.scalpCenter.x - shoulderOffset * 1.45f, shoulderY, -m_config.scalpRadius * 0.18f),
            Vec3(m_config.scalpCenter.x + shoulderOffset * 1.45f, shoulderY, m_config.scalpRadius * 0.18f),
            shoulderRadius });
        m_capsuleColliders.push_back({
            Vec3(m_config.scalpCenter.x, shoulderY - m_config.scalpRadius * 0.12f, -m_config.scalpRadius * 0.55f),
            Vec3(m_config.scalpCenter.x, shoulderY + m_config.scalpRadius * 1.25f, -m_config.scalpRadius * 0.60f),
            m_config.scalpRadius * 0.38f });

        PlaneCollider floor;
        floor.normal = Vec3(0.0f, -1.0f, 0.0f);
        floor.offset = m_config.floorY;
        m_planeColliders.push_back(floor);
    }

    void HairSimulation::SetSphereColliders(const SphereCollider* colliders, int count)
    {
        m_colliders.clear();
        if (colliders != nullptr && count > 0)
        {
            m_colliders.assign(colliders, colliders + count);
        }
    }

    void HairSimulation::SetCapsuleColliders(const CapsuleCollider* colliders, int count)
    {
        m_capsuleColliders.clear();
        if (colliders != nullptr && count > 0)
        {
            m_capsuleColliders.assign(colliders, colliders + count);
        }
    }

    void HairSimulation::SetPlaneColliders(const PlaneCollider* colliders, int count)
    {
        m_planeColliders.clear();
        if (colliders != nullptr && count > 0)
        {
            m_planeColliders.assign(colliders, colliders + count);
        }
    }

    void HairSimulation::Integrate(float deltaTime)
    {
        const Vec3 gravityAcceleration(0.0f, m_config.gravity, 0.0f);
        const float pulse = 0.55f + 0.45f * std::sin(m_elapsedTimeSeconds * m_config.windPulseFrequency * kTau);
        const float windScale = m_config.windStrength * pulse;
        const float dampingScale = 1.0f - m_config.damping;
        const float dtSquared = deltaTime * deltaTime;

        for (int strandIndex = 0; strandIndex < m_config.strandCount; ++strandIndex)
        {
            const float strandNoise = Hash01(strandIndex * 97 + 11);
            for (int particleIndex = 1; particleIndex < m_particlesPerStrand; ++particleIndex)
            {
                const std::size_t flatIndex = ParticleIndex(strandIndex, particleIndex);
                Vec3 current = m_positions[flatIndex];
                const Vec3 previous = m_previousPositions[flatIndex];
                const Vec3 velocity = (current - previous) * dampingScale;

                const float normalizedDepth = static_cast<float>(particleIndex) / static_cast<float>(std::max(1, m_particlesPerStrand - 1));
                const float localFlutter = std::sin((m_elapsedTimeSeconds * 3.6f) + strandNoise * 10.0f + normalizedDepth * 4.0f);
                const Vec3 windAcceleration(
                    m_config.windDirectionX * windScale * (0.35f + normalizedDepth * 0.85f),
                    -windScale * 0.035f * normalizedDepth,
                    localFlutter * windScale * 0.12f * normalizedDepth);

                const Vec3 next = current + velocity + (gravityAcceleration + windAcceleration) * dtSquared;
                m_previousPositions[flatIndex] = current;
                m_positions[flatIndex] = next;
            }
        }
    }

    void HairSimulation::SolveDistanceConstraints()
    {
        for (int strandIndex = 0; strandIndex < m_config.strandCount; ++strandIndex)
        {
            const std::size_t base = static_cast<std::size_t>(strandIndex) * static_cast<std::size_t>(m_particlesPerStrand);
            for (int particleIndex = 0; particleIndex < m_particlesPerStrand - 1; ++particleIndex)
            {
                Vec3& a = m_positions[base + static_cast<std::size_t>(particleIndex)];
                Vec3& b = m_positions[base + static_cast<std::size_t>(particleIndex + 1)];
                const Vec3 delta = b - a;
                const float length = Length(delta);
                if (length < kEpsilon)
                {
                    continue;
                }

                const float error = (length - m_config.segmentLength) / length;
                if (particleIndex == 0)
                {
                    b -= delta * error;
                }
                else
                {
                    const Vec3 correction = delta * (error * 0.5f);
                    a += correction;
                    b -= correction;
                }
            }
        }
    }

    void HairSimulation::SolveBendConstraints()
    {
        const float stiffness = m_config.bendStiffness * 0.55f;
        if (stiffness <= 0.0f)
        {
            return;
        }

        for (int strandIndex = 0; strandIndex < m_config.strandCount; ++strandIndex)
        {
            const std::size_t base = static_cast<std::size_t>(strandIndex) * static_cast<std::size_t>(m_particlesPerStrand);
            for (int particleIndex = 1; particleIndex < m_particlesPerStrand - 1; ++particleIndex)
            {
                Vec3& previous = m_positions[base + static_cast<std::size_t>(particleIndex - 1)];
                Vec3& current = m_positions[base + static_cast<std::size_t>(particleIndex)];
                Vec3& next = m_positions[base + static_cast<std::size_t>(particleIndex + 1)];
                const Vec3 target = (previous + next) * 0.5f;
                const Vec3 correction = (target - current) * stiffness;
                current += correction;
                if (particleIndex > 1)
                {
                    previous -= correction * 0.12f;
                }
                next -= correction * 0.12f;
            }
        }
    }

    void HairSimulation::SolveClusterConstraints()
    {
        const float stiffness = m_config.clusterStiffness * 0.35f;
        if (stiffness <= 0.0f || m_config.strandCount < 3)
        {
            return;
        }

        m_clusterScratch = m_positions;
        for (int strandIndex = 1; strandIndex < m_config.strandCount - 1; ++strandIndex)
        {
            for (int particleIndex = 1; particleIndex < m_particlesPerStrand; ++particleIndex)
            {
                const std::size_t centerIndex = ParticleIndex(strandIndex, particleIndex);
                const std::size_t leftIndex = ParticleIndex(strandIndex - 1, particleIndex);
                const std::size_t rightIndex = ParticleIndex(strandIndex + 1, particleIndex);
                const Vec3 average = (m_positions[leftIndex] + m_positions[centerIndex] + m_positions[rightIndex]) / 3.0f;
                m_clusterScratch[centerIndex] = Lerp(m_positions[centerIndex], average, stiffness);
            }
        }
        m_positions.swap(m_clusterScratch);
    }

    void HairSimulation::SolveSelfCollisionConstraints()
    {
        const float radius = m_config.selfCollisionRadius;
        const float stiffness = m_config.selfCollisionStiffness * 0.5f;
        if (radius <= 0.0f || stiffness <= 0.0f || m_config.strandCount < 2)
        {
            return;
        }

        const int neighborSpan = ClampInt(m_config.selfCollisionNeighborSpan, 1, std::max(1, m_config.strandCount - 1));
        const float minDistance = radius;
        const float minDistanceSq = minDistance * minDistance;

        for (int strandIndex = 0; strandIndex < m_config.strandCount; ++strandIndex)
        {
            const int start = std::max(0, strandIndex - neighborSpan);
            const int end = std::min(m_config.strandCount - 1, strandIndex + neighborSpan);
            for (int otherStrand = strandIndex + 1; otherStrand <= end; ++otherStrand)
            {
                if (otherStrand < start)
                {
                    continue;
                }
                for (int particleIndex = 1; particleIndex < m_particlesPerStrand; ++particleIndex)
                {
                    const std::size_t aIndex = ParticleIndex(strandIndex, particleIndex);
                    const std::size_t bIndex = ParticleIndex(otherStrand, particleIndex);
                    Vec3 delta = m_positions[bIndex] - m_positions[aIndex];
                    const float distanceSq = Dot(delta, delta);
                    if (distanceSq >= minDistanceSq)
                    {
                        continue;
                    }
                    const float distance = std::sqrt(std::max(distanceSq, kEpsilon));
                    Vec3 normal = (distance > kEpsilon) ? (delta / distance) : SafeNormal(m_positions[bIndex] - m_rootPositions[static_cast<std::size_t>(otherStrand)]);
                    const float penetration = minDistance - distance;
                    const Vec3 correction = normal * (penetration * stiffness * 0.5f);
                    m_positions[aIndex] -= correction;
                    m_positions[bIndex] += correction;

                    const float friction = 0.20f * stiffness;
                    const Vec3 mid = (m_positions[aIndex] + m_positions[bIndex]) * 0.5f;
                    ApplySurfaceFriction(m_previousPositions[aIndex], m_positions[aIndex], mid - normal * (minDistance * 0.5f), normal, friction);
                    ApplySurfaceFriction(m_previousPositions[bIndex], m_positions[bIndex], mid + normal * (minDistance * 0.5f), normal, friction);
                }
            }
        }
    }

    void HairSimulation::SolveCollisions()
    {
        for (const PlaneCollider& plane : m_planeColliders)
        {
            const Vec3 normal = SafeNormal(plane.normal);
            for (int strandIndex = 0; strandIndex < m_config.strandCount; ++strandIndex)
            {
                for (int particleIndex = 1; particleIndex < m_particlesPerStrand; ++particleIndex)
                {
                    const std::size_t flatIndex = ParticleIndex(strandIndex, particleIndex);
                    const Vec3 position = m_positions[flatIndex];
                    const float signedDistance = Dot(position, normal) - plane.offset;
                    if (signedDistance > 0.0f)
                    {
                        const Vec3 corrected = position - normal * signedDistance;
                        m_positions[flatIndex] = corrected;
                        ApplySurfaceFriction(m_previousPositions[flatIndex], position, corrected, normal, m_config.floorFriction);
                    }
                }
            }
        }

        for (const SphereCollider& collider : m_colliders)
        {
            for (int strandIndex = 0; strandIndex < m_config.strandCount; ++strandIndex)
            {
                for (int particleIndex = 1; particleIndex < m_particlesPerStrand; ++particleIndex)
                {
                    const std::size_t flatIndex = ParticleIndex(strandIndex, particleIndex);
                    const Vec3 position = m_positions[flatIndex];
                    Vec3 delta = position - collider.center;
                    const float distance = Length(delta);
                    if (distance < collider.radius)
                    {
                        const Vec3 normal = (distance > kEpsilon) ? (delta / distance) : Vec3(0.0f, -1.0f, 0.0f);
                        const Vec3 corrected = collider.center + normal * collider.radius;
                        m_positions[flatIndex] = corrected;
                        ApplySurfaceFriction(m_previousPositions[flatIndex], position, corrected, normal, m_config.colliderFriction);
                    }
                }
            }
        }

        for (const CapsuleCollider& collider : m_capsuleColliders)
        {
            for (int strandIndex = 0; strandIndex < m_config.strandCount; ++strandIndex)
            {
                for (int particleIndex = 1; particleIndex < m_particlesPerStrand; ++particleIndex)
                {
                    const std::size_t flatIndex = ParticleIndex(strandIndex, particleIndex);
                    const Vec3 position = m_positions[flatIndex];
                    const Vec3 closest = ClosestPointOnSegment(position, collider.pointA, collider.pointB);
                    Vec3 delta = position - closest;
                    const float distance = Length(delta);
                    if (distance < collider.radius)
                    {
                        const Vec3 normal = (distance > kEpsilon) ? (delta / distance) : Vec3(0.0f, -1.0f, 0.0f);
                        const Vec3 corrected = closest + normal * collider.radius;
                        m_positions[flatIndex] = corrected;
                        ApplySurfaceFriction(m_previousPositions[flatIndex], position, corrected, normal, m_config.colliderFriction);
                    }
                }
            }
        }
    }

    void HairSimulation::PinRoots()
    {
        for (int strandIndex = 0; strandIndex < m_config.strandCount; ++strandIndex)
        {
            const std::size_t flatIndex = ParticleIndex(strandIndex, 0);
            m_positions[flatIndex] = m_rootPositions[static_cast<std::size_t>(strandIndex)];
            m_previousPositions[flatIndex] = m_rootPositions[static_cast<std::size_t>(strandIndex)];
        }
        m_legacyDirty = true;
    }

    void HairSimulation::SolveConstraints()
    {
        for (int iteration = 0; iteration < m_config.solverIterations; ++iteration)
        {
            SolveDistanceConstraints();
            SolveBendConstraints();
            SolveClusterConstraints();
            SolveSelfCollisionConstraints();
            SolveCollisions();
            PinRoots();
        }
    }

    void HairSimulation::Step(float deltaTime)
    {
        const float clampedDeltaTime = Clamp(deltaTime, 1.0f / 240.0f, 1.0f / 24.0f);
        Integrate(clampedDeltaTime);
        SolveConstraints();
        m_elapsedTimeSeconds += clampedDeltaTime;
        m_legacyDirty = true;
    }

    bool HairSimulation::SetFlatState(const SimulationConfig& config,
        const Vec3* positions,
        const Vec3* previousPositions,
        const Vec3* rootPositions,
        int positionsCount,
        int particlesPerStrand)
    {
        if (positions == nullptr || previousPositions == nullptr || rootPositions == nullptr)
        {
            return false;
        }
        if (particlesPerStrand < 2 || positionsCount <= 0)
        {
            return false;
        }
        if ((positionsCount % particlesPerStrand) != 0)
        {
            return false;
        }

        const int strandCount = positionsCount / particlesPerStrand;
        SimulationConfig resolved = config;
        resolved.strandCount = strandCount;
        resolved.segmentCount = particlesPerStrand - 1;
        m_config = resolved;
        Reset();

        m_particlesPerStrand = particlesPerStrand;
        m_positions.assign(positions, positions + positionsCount);
        m_previousPositions.assign(previousPositions, previousPositions + positionsCount);
        m_rootPositions.assign(rootPositions, rootPositions + strandCount);
        m_clusterScratch.assign(static_cast<std::size_t>(positionsCount), Vec3());
        PinRoots();
        m_legacyDirty = true;
        return true;
    }

    bool HairSimulation::SaveSnapshot(const char* filePath) const
    {
        if (filePath == nullptr || *filePath == '\0')
        {
            return false;
        }

        std::ofstream output(std::filesystem::u8path(filePath), std::ios::out | std::ios::trunc);
        if (!output)
        {
            return false;
        }

        output << "HAIRSNAPSHOT_V4\n";
        output << m_config.strandCount << ' ' << m_config.segmentCount << '\n';
        output << m_config.segmentLength << ' ' << m_config.rootSpacing << ' ' << m_config.gravity << ' '
               << m_config.damping << ' ' << m_config.solverIterations << ' ' << m_config.bendStiffness << ' '
               << m_config.clusterStiffness << ' ' << m_config.floorY << '\n';
        output << m_config.windStrength << ' ' << m_config.windDirectionX << ' ' << m_config.windPulseFrequency << ' '
               << static_cast<int>(m_config.scenePreset) << ' ' << m_elapsedTimeSeconds << '\n';
        WriteVec3(output, m_config.scalpCenter);
        output << ' ' << m_config.scalpRadius << '\n';
        output << m_config.floorFriction << ' ' << m_config.colliderFriction << ' '
               << m_config.selfCollisionRadius << ' ' << m_config.selfCollisionStiffness << ' '
               << m_config.selfCollisionNeighborSpan << ' ' << m_config.renderRootWidth << ' ' << m_config.renderTipWidth << '\n';
        output << m_particlesPerStrand << '\n';
        output << m_positions.size() << '\n';
        output << m_rootPositions.size() << '\n';

        for (std::size_t i = 0; i < m_rootPositions.size(); ++i)
        {
            WriteVec3(output, m_rootPositions[i]);
            output << '\n';
        }

        for (std::size_t i = 0; i < m_positions.size(); ++i)
        {
            WriteVec3(output, m_positions[i]);
            output << ' ';
            WriteVec3(output, m_previousPositions[i]);
            output << '\n';
        }

        output << m_colliders.size() << ' ' << m_capsuleColliders.size() << ' ' << m_planeColliders.size() << '\n';
        for (const SphereCollider& collider : m_colliders)
        {
            WriteVec3(output, collider.center);
            output << ' ' << collider.radius << '\n';
        }
        for (const CapsuleCollider& collider : m_capsuleColliders)
        {
            WriteVec3(output, collider.pointA);
            output << ' ';
            WriteVec3(output, collider.pointB);
            output << ' ' << collider.radius << '\n';
        }
        for (const PlaneCollider& collider : m_planeColliders)
        {
            WriteVec3(output, collider.normal);
            output << ' ' << collider.offset << '\n';
        }

        return static_cast<bool>(output);
    }

    bool HairSimulation::LoadSnapshot(const char* filePath)
    {
        if (filePath == nullptr || *filePath == '\0')
        {
            return false;
        }

        std::ifstream input(std::filesystem::u8path(filePath));
        if (!input)
        {
            return false;
        }

        std::string header;
        input >> header;
        if (header != "HAIRSNAPSHOT_V1" && header != "HAIRSNAPSHOT_V2" && header != "HAIRSNAPSHOT_V3" && header != "HAIRSNAPSHOT_V4")
        {
            return false;
        }

        SimulationConfig loadedConfig;
        if (!(input >> loadedConfig.strandCount >> loadedConfig.segmentCount))
        {
            return false;
        }
        if (!(input >> loadedConfig.segmentLength >> loadedConfig.rootSpacing >> loadedConfig.gravity
              >> loadedConfig.damping >> loadedConfig.solverIterations >> loadedConfig.bendStiffness
              >> loadedConfig.clusterStiffness >> loadedConfig.floorY))
        {
            return false;
        }

        int preset = 0;
        if (!(input >> loadedConfig.windStrength >> loadedConfig.windDirectionX >> loadedConfig.windPulseFrequency >> preset >> m_elapsedTimeSeconds))
        {
            loadedConfig.windStrength = 0.0f;
            loadedConfig.windDirectionX = 1.0f;
            loadedConfig.windPulseFrequency = 1.0f;
            preset = 0;
            m_elapsedTimeSeconds = 0.0f;
            input.clear();
        }
        loadedConfig.scenePreset = SanitizeScenePreset(static_cast<TestScenePreset>(preset));

        if (!ReadVec3(input, loadedConfig.scalpCenter) || !(input >> loadedConfig.scalpRadius))
        {
            return false;
        }

        if (header == "HAIRSNAPSHOT_V4")
        {
            if (!(input >> loadedConfig.floorFriction >> loadedConfig.colliderFriction
                  >> loadedConfig.selfCollisionRadius >> loadedConfig.selfCollisionStiffness
                  >> loadedConfig.selfCollisionNeighborSpan >> loadedConfig.renderRootWidth >> loadedConfig.renderTipWidth))
            {
                return false;
            }
        }
        else
        {
            loadedConfig.floorFriction = 0.08f;
            loadedConfig.colliderFriction = 0.04f;
            loadedConfig.selfCollisionRadius = 0.0f;
            loadedConfig.selfCollisionStiffness = 0.0f;
            loadedConfig.selfCollisionNeighborSpan = 3;
            loadedConfig.renderRootWidth = 0.90f;
            loadedConfig.renderTipWidth = 0.10f;
        }

        loadedConfig.strandCount = ClampInt(loadedConfig.strandCount, 1, 25000);
        loadedConfig.segmentCount = ClampInt(loadedConfig.segmentCount, 2, 32);
        m_config = loadedConfig;
        m_particlesPerStrand = m_config.segmentCount + 1;
        RebuildColliders();
        m_rootPositions.assign(static_cast<std::size_t>(m_config.strandCount), Vec3());

        if (header == "HAIRSNAPSHOT_V3" || header == "HAIRSNAPSHOT_V4")
        {
            int particlesPerStrand = 0;
            std::size_t totalParticles = 0;
            if (!(input >> particlesPerStrand >> totalParticles))
            {
                return false;
            }
            if (particlesPerStrand != m_particlesPerStrand)
            {
                return false;
            }
            std::size_t rootCount = static_cast<std::size_t>(m_config.strandCount);
            if (header == "HAIRSNAPSHOT_V4")
            {
                if (!(input >> rootCount))
                {
                    return false;
                }
            }
            m_rootPositions.assign(rootCount, Vec3());
            for (std::size_t i = 0; i < rootCount; ++i)
            {
                if (!ReadVec3(input, m_rootPositions[i]))
                {
                    return false;
                }
            }
            m_positions.assign(totalParticles, Vec3());
            m_previousPositions.assign(totalParticles, Vec3());
            m_clusterScratch.assign(totalParticles, Vec3());
            for (std::size_t i = 0; i < totalParticles; ++i)
            {
                if (!ReadVec3(input, m_positions[i]) || !ReadVec3(input, m_previousPositions[i]))
                {
                    return false;
                }
            }

            if (header == "HAIRSNAPSHOT_V4")
            {
                std::size_t sphereCount = 0;
                std::size_t capsuleCount = 0;
                std::size_t planeCount = 0;
                if (!(input >> sphereCount >> capsuleCount >> planeCount))
                {
                    return false;
                }
                m_colliders.assign(sphereCount, SphereCollider());
                for (std::size_t i = 0; i < sphereCount; ++i)
                {
                    if (!ReadVec3(input, m_colliders[i].center) || !(input >> m_colliders[i].radius))
                    {
                        return false;
                    }
                }
                m_capsuleColliders.assign(capsuleCount, CapsuleCollider());
                for (std::size_t i = 0; i < capsuleCount; ++i)
                {
                    if (!ReadVec3(input, m_capsuleColliders[i].pointA)
                        || !ReadVec3(input, m_capsuleColliders[i].pointB)
                        || !(input >> m_capsuleColliders[i].radius))
                    {
                        return false;
                    }
                }
                m_planeColliders.assign(planeCount, PlaneCollider());
                for (std::size_t i = 0; i < planeCount; ++i)
                {
                    if (!ReadVec3(input, m_planeColliders[i].normal) || !(input >> m_planeColliders[i].offset))
                    {
                        return false;
                    }
                }
            }
            else
            {
                for (int strandIndex = 0; strandIndex < m_config.strandCount; ++strandIndex)
                {
                    m_rootPositions[static_cast<std::size_t>(strandIndex)] = m_positions[ParticleIndex(strandIndex, 0)];
                }
            }
        }
        else
        {
            int strandFileCount = 0;
            if (!(input >> strandFileCount))
            {
                return false;
            }
            m_positions.assign(static_cast<std::size_t>(m_config.strandCount) * static_cast<std::size_t>(m_particlesPerStrand), Vec3());
            m_previousPositions.assign(m_positions.size(), Vec3());
            m_clusterScratch.assign(m_positions.size(), Vec3());
            for (int strandIndex = 0; strandIndex < strandFileCount && strandIndex < m_config.strandCount; ++strandIndex)
            {
                int particleCount = 0;
                if (!(input >> particleCount))
                {
                    return false;
                }

                for (int particleIndex = 0; particleIndex < particleCount; ++particleIndex)
                {
                    Vec3 position;
                    Vec3 previous;
                    int pinned = 0;
                    if (!ReadVec3(input, position) || !ReadVec3(input, previous) || !(input >> pinned))
                    {
                        return false;
                    }
                    if (particleIndex < m_particlesPerStrand)
                    {
                        const std::size_t flatIndex = ParticleIndex(strandIndex, particleIndex);
                        m_positions[flatIndex] = position;
                        m_previousPositions[flatIndex] = previous;
                    }
                }

                int restCount = 0;
                if (!(input >> restCount))
                {
                    return false;
                }
                for (int restIndex = 0; restIndex < restCount; ++restIndex)
                {
                    float ignored = 0.0f;
                    input >> ignored;
                }
                m_rootPositions[static_cast<std::size_t>(strandIndex)] = m_positions[ParticleIndex(strandIndex, 0)];
            }
        }

        PinRoots();
        m_legacyDirty = true;
        return true;
    }

    std::vector<Strand> HairSimulation::GenerateRenderStrands(const RenderInterpolationSettings& settings) const
    {
        RenderInterpolationSettings resolved = settings;
        resolved.childrenPerGuide = ClampInt(resolved.childrenPerGuide, 0, 32);
        resolved.spread = std::max(0.0f, resolved.spread);
        resolved.clumpBlend = Clamp(resolved.clumpBlend, 0.0f, 1.0f);
        resolved.frizz = std::max(0.0f, resolved.frizz);
        resolved.rootSeparationBias = Clamp(resolved.rootSeparationBias, 0.0f, 2.0f);

        std::vector<Strand> result;
        const int estimatedPerGuide = resolved.includeGuideStrands ? (resolved.childrenPerGuide + 1) : resolved.childrenPerGuide;
        result.reserve(static_cast<std::size_t>(m_config.strandCount) * static_cast<std::size_t>(std::max(estimatedPerGuide, 1)));

        auto appendGuide = [&](int strandIndex)
        {
            Strand strand;
            strand.particles.resize(static_cast<std::size_t>(m_particlesPerStrand));
            strand.restLengths.assign(static_cast<std::size_t>(m_particlesPerStrand - 1), m_config.segmentLength);
            for (int particleIndex = 0; particleIndex < m_particlesPerStrand; ++particleIndex)
            {
                const std::size_t flatIndex = ParticleIndex(strandIndex, particleIndex);
                strand.particles[static_cast<std::size_t>(particleIndex)].position = m_positions[flatIndex];
                strand.particles[static_cast<std::size_t>(particleIndex)].previousPosition = m_previousPositions[flatIndex];
                strand.particles[static_cast<std::size_t>(particleIndex)].pinned = (particleIndex == 0);
            }
            result.push_back(std::move(strand));
        };

        for (int strandIndex = 0; strandIndex < m_config.strandCount; ++strandIndex)
        {
            if (resolved.includeGuideStrands)
            {
                appendGuide(strandIndex);
            }

            const int leftGuide = std::max(0, strandIndex - 1);
            const int rightGuide = std::min(m_config.strandCount - 1, strandIndex + 1);
            const Vec3 root = m_rootPositions[static_cast<std::size_t>(strandIndex)];
            const Vec3 lateralAxis = SafeNormal(m_rootPositions[static_cast<std::size_t>(rightGuide)] - m_rootPositions[static_cast<std::size_t>(leftGuide)], Vec3(1.0f, 0.0f, 0.0f));

            for (int child = 0; child < resolved.childrenPerGuide; ++child)
            {
                Strand strand;
                strand.particles.resize(static_cast<std::size_t>(m_particlesPerStrand));
                strand.restLengths.assign(static_cast<std::size_t>(m_particlesPerStrand - 1), m_config.segmentLength);

                const float childSeed = static_cast<float>(resolved.randomSeed + strandIndex * 97 + child * 31);
                const float spreadSign = (Hash01(static_cast<int>(childSeed) * 3 + 5) > 0.5f) ? 1.0f : -1.0f;
                const float spreadMagnitude = (0.35f + 0.65f * Hash01(static_cast<int>(childSeed) * 5 + 7)) * resolved.spread;
                const float baseBlend = Clamp(0.18f + 0.64f * Hash01(static_cast<int>(childSeed) * 7 + 9), 0.0f, 1.0f);
                const float leftWeight = 0.5f * (1.0f - baseBlend);
                const float rightWeight = 0.5f * baseBlend;
                const float centerWeight = 1.0f - leftWeight - rightWeight;

                for (int particleIndex = 0; particleIndex < m_particlesPerStrand; ++particleIndex)
                {
                    const std::size_t centerIndex = ParticleIndex(strandIndex, particleIndex);
                    const std::size_t leftIndex = ParticleIndex(leftGuide, particleIndex);
                    const std::size_t rightIndex = ParticleIndex(rightGuide, particleIndex);
                    Vec3 base = m_positions[centerIndex] * centerWeight + m_positions[leftIndex] * leftWeight + m_positions[rightIndex] * rightWeight;
                    const float depthT = static_cast<float>(particleIndex) / static_cast<float>(std::max(1, m_particlesPerStrand - 1));

                    Vec3 tangent;
                    if (particleIndex + 1 < m_particlesPerStrand)
                    {
                        tangent = m_positions[ParticleIndex(strandIndex, particleIndex + 1)] - m_positions[centerIndex];
                    }
                    else
                    {
                        tangent = m_positions[centerIndex] - m_positions[ParticleIndex(strandIndex, particleIndex - 1)];
                    }
                    tangent = SafeNormal(tangent, Vec3(0.0f, 1.0f, 0.0f));
                    const Vec3 normal = OrthonormalPerpendicular(tangent);
                    const Vec3 binormal = SafeNormal(Cross(tangent, normal), Vec3(0.0f, 0.0f, 1.0f));

                    const float clumpScale = depthT * depthT;
                    const float clumpBlend = resolved.clumpBlend * (0.25f + 0.75f * depthT);
                    const float sideOffset = spreadSign * spreadMagnitude * clumpScale;
                    const float frizzPhase = Hash01(static_cast<int>(childSeed) * 11 + particleIndex * 13) * kTau;
                    const float frizz = resolved.frizz * depthT * (0.5f + 0.5f * std::sin(frizzPhase + depthT * 8.0f));
                    const float rootBias = resolved.rootSeparationBias * (1.0f - depthT) * (Hash01(static_cast<int>(childSeed) * 17 + 23) - 0.5f);
                    const Vec3 offset = normal * (sideOffset + rootBias) + binormal * frizz;
                    base = Lerp(base, m_positions[centerIndex] + offset, clumpBlend);

                    Particle particle;
                    particle.position = base;
                    particle.previousPosition = base;
                    particle.pinned = (particleIndex == 0);
                    strand.particles[static_cast<std::size_t>(particleIndex)] = particle;
                }
                strand.particles.front().position = root;
                strand.particles.front().previousPosition = root;
                result.push_back(std::move(strand));
            }
        }

        return result;
    }

    SimulationStats HairSimulation::ComputeStats(float deltaTimeHint) const
    {
        SimulationStats stats;
        stats.strandCount = m_config.strandCount;
        stats.particlesPerStrand = m_particlesPerStrand;
        stats.particleCount = static_cast<int>(m_positions.size());

        double segmentErrorSum = 0.0;
        double velocitySum = 0.0;
        double rootTipDistanceSum = 0.0;
        int segmentSamples = 0;
        int velocitySamples = 0;
        const float dt = std::max(deltaTimeHint, 1.0e-4f);

        for (int strandIndex = 0; strandIndex < m_config.strandCount; ++strandIndex)
        {
            const Vec3 root = m_positions[ParticleIndex(strandIndex, 0)];
            const Vec3 tip = m_positions[ParticleIndex(strandIndex, m_particlesPerStrand - 1)];
            rootTipDistanceSum += Length(tip - root);

            for (int particleIndex = 0; particleIndex + 1 < m_particlesPerStrand; ++particleIndex)
            {
                const std::size_t aIndex = ParticleIndex(strandIndex, particleIndex);
                const std::size_t bIndex = ParticleIndex(strandIndex, particleIndex + 1);
                const float lengthError = std::abs(Length(m_positions[bIndex] - m_positions[aIndex]) - m_config.segmentLength);
                segmentErrorSum += lengthError;
                stats.maxSegmentError = std::max(stats.maxSegmentError, lengthError);
                ++segmentSamples;
            }

            for (int particleIndex = 1; particleIndex < m_particlesPerStrand; ++particleIndex)
            {
                const std::size_t flatIndex = ParticleIndex(strandIndex, particleIndex);
                const float velocity = Length(m_positions[flatIndex] - m_previousPositions[flatIndex]) / dt;
                velocitySum += velocity;
                stats.maxVelocity = std::max(stats.maxVelocity, velocity);
                ++velocitySamples;
            }
        }

        if (segmentSamples > 0)
        {
            stats.averageSegmentError = static_cast<float>(segmentErrorSum / static_cast<double>(segmentSamples));
        }
        if (velocitySamples > 0)
        {
            stats.averageVelocity = static_cast<float>(velocitySum / static_cast<double>(velocitySamples));
        }
        if (m_config.strandCount > 0)
        {
            stats.averageRootTipDistance = static_cast<float>(rootTipDistanceSum / static_cast<double>(m_config.strandCount));
        }
        return stats;
    }

    bool HairSimulation::SaveCurveCache(const char* filePath, const RenderInterpolationSettings& settings) const
    {
        if (filePath == nullptr || *filePath == '\0')
        {
            return false;
        }

        const std::vector<Strand> renderStrands = GenerateRenderStrands(settings);
        std::ofstream output(std::filesystem::u8path(filePath), std::ios::out | std::ios::trunc);
        if (!output)
        {
            return false;
        }

        output << "HAIRCURVES_V1\n";
        output << renderStrands.size() << ' ' << m_particlesPerStrand << '\n';
        output << settings.childrenPerGuide << ' ' << settings.spread << ' ' << settings.clumpBlend << ' ' << settings.frizz << ' ' << settings.randomSeed << '\n';
        for (const Strand& strand : renderStrands)
        {
            output << strand.particles.size() << '\n';
            for (const Particle& particle : strand.particles)
            {
                WriteVec3(output, particle.position);
                output << '\n';
            }
        }
        return static_cast<bool>(output);
    }

    bool HairSimulation::ExportGuideLinesObj(const char* filePath) const
    {
        if (filePath == nullptr || *filePath == '\0')
        {
            return false;
        }
        std::ofstream output(std::filesystem::u8path(filePath), std::ios::out | std::ios::trunc);
        if (!output)
        {
            return false;
        }
        output << "# hair guide strands\n";
        AppendLineObj(GetStrands(), output);
        return static_cast<bool>(output);
    }

    bool HairSimulation::ExportRenderLinesObj(const char* filePath, const RenderInterpolationSettings& settings) const
    {
        if (filePath == nullptr || *filePath == '\0')
        {
            return false;
        }
        std::ofstream output(std::filesystem::u8path(filePath), std::ios::out | std::ios::trunc);
        if (!output)
        {
            return false;
        }
        output << "# hair render strands\n";
        AppendLineObj(GenerateRenderStrands(settings), output);
        return static_cast<bool>(output);
    }

    bool HairSimulation::ExportTubeMeshObj(const char* filePath, const TubeMeshExportSettings& settings) const
    {
        if (filePath == nullptr || *filePath == '\0')
        {
            return false;
        }

        RenderInterpolationSettings renderSettings;
        renderSettings.childrenPerGuide = settings.childrenPerGuide;
        renderSettings.spread = settings.spread;
        renderSettings.frizz = settings.frizz;
        renderSettings.randomSeed = settings.randomSeed;
        renderSettings.includeGuideStrands = settings.includeGuideStrands;

        const std::vector<Strand> strands = GenerateRenderStrands(renderSettings);
        std::ofstream output(std::filesystem::u8path(filePath), std::ios::out | std::ios::trunc);
        if (!output)
        {
            return false;
        }

        const int radialSegments = ClampInt(settings.radialSegments, 3, 32);
        const float rootRadius = std::max(0.001f, settings.rootRadius);
        const float tipRadius = Clamp(settings.tipRadius, 0.0005f, rootRadius);

        output << "# hair tube mesh\n";
        std::size_t vertexOffset = 1;
        for (const Strand& strand : strands)
        {
            if (strand.particles.size() < 2)
            {
                continue;
            }

            const std::size_t ringCount = strand.particles.size();
            for (std::size_t particleIndex = 0; particleIndex < ringCount; ++particleIndex)
            {
                const Vec3 current = strand.particles[particleIndex].position;
                Vec3 tangent;
                if (particleIndex + 1 < ringCount)
                {
                    tangent = strand.particles[particleIndex + 1].position - current;
                }
                else
                {
                    tangent = current - strand.particles[particleIndex - 1].position;
                }
                tangent = SafeNormal(tangent, Vec3(0.0f, 1.0f, 0.0f));
                const Vec3 normal = OrthonormalPerpendicular(tangent);
                const Vec3 binormal = SafeNormal(Cross(tangent, normal), Vec3(0.0f, 0.0f, 1.0f));

                const float t = static_cast<float>(particleIndex) / static_cast<float>(std::max<std::size_t>(1, ringCount - 1));
                const float radius = Lerp(Vec3(rootRadius, 0.0f, 0.0f), Vec3(tipRadius, 0.0f, 0.0f), t).x;
                for (int radial = 0; radial < radialSegments; ++radial)
                {
                    const float angle = kTau * static_cast<float>(radial) / static_cast<float>(radialSegments);
                    const Vec3 offset = normal * (std::cos(angle) * radius) + binormal * (std::sin(angle) * radius);
                    const Vec3 vertex = current + offset;
                    output << "v " << vertex.x << ' ' << vertex.y << ' ' << vertex.z << '\n';
                }
            }

            for (std::size_t ring = 0; ring + 1 < ringCount; ++ring)
            {
                for (int radial = 0; radial < radialSegments; ++radial)
                {
                    const int nextRadial = (radial + 1) % radialSegments;
                    const std::size_t i0 = vertexOffset + ring * radialSegments + static_cast<std::size_t>(radial);
                    const std::size_t i1 = vertexOffset + ring * radialSegments + static_cast<std::size_t>(nextRadial);
                    const std::size_t i2 = vertexOffset + (ring + 1) * radialSegments + static_cast<std::size_t>(nextRadial);
                    const std::size_t i3 = vertexOffset + (ring + 1) * radialSegments + static_cast<std::size_t>(radial);
                    output << "f " << i0 << ' ' << i1 << ' ' << i2 << ' ' << i3 << '\n';
                }
            }

            vertexOffset += ringCount * static_cast<std::size_t>(radialSegments);
        }

        return static_cast<bool>(output);
    }

    const SimulationConfig& HairSimulation::GetConfig() const
    {
        return m_config;
    }

    void HairSimulation::SyncLegacyStrands() const
    {
        if (!m_legacyDirty)
        {
            return;
        }

        m_legacyStrands.assign(static_cast<std::size_t>(m_config.strandCount), Strand());
        for (int strandIndex = 0; strandIndex < m_config.strandCount; ++strandIndex)
        {
            Strand& strand = m_legacyStrands[static_cast<std::size_t>(strandIndex)];
            strand.particles.resize(static_cast<std::size_t>(m_particlesPerStrand));
            strand.restLengths.assign(static_cast<std::size_t>(std::max(0, m_particlesPerStrand - 1)), m_config.segmentLength);
            for (int particleIndex = 0; particleIndex < m_particlesPerStrand; ++particleIndex)
            {
                const std::size_t flatIndex = ParticleIndex(strandIndex, particleIndex);
                strand.particles[static_cast<std::size_t>(particleIndex)].position = m_positions[flatIndex];
                strand.particles[static_cast<std::size_t>(particleIndex)].previousPosition = m_previousPositions[flatIndex];
                strand.particles[static_cast<std::size_t>(particleIndex)].pinned = (particleIndex == 0);
            }
        }
        m_legacyDirty = false;
    }

    const std::vector<Strand>& HairSimulation::GetStrands() const
    {
        SyncLegacyStrands();
        return m_legacyStrands;
    }

    const std::vector<SphereCollider>& HairSimulation::GetColliders() const
    {
        return m_colliders;
    }

    const std::vector<CapsuleCollider>& HairSimulation::GetCapsuleColliders() const
    {
        return m_capsuleColliders;
    }

    const std::vector<PlaneCollider>& HairSimulation::GetPlaneColliders() const
    {
        return m_planeColliders;
    }

    int HairSimulation::GetTotalParticleCount() const
    {
        return static_cast<int>(m_positions.size());
    }

    float HairSimulation::GetElapsedTimeSeconds() const
    {
        return m_elapsedTimeSeconds;
    }

    int HairSimulation::GetStrandCount() const
    {
        return m_config.strandCount;
    }

    int HairSimulation::GetParticlesPerStrand() const
    {
        return m_particlesPerStrand;
    }

    const Vec3* HairSimulation::GetFlatPositions() const
    {
        return m_positions.empty() ? nullptr : m_positions.data();
    }

    const Vec3* HairSimulation::GetFlatPreviousPositions() const
    {
        return m_previousPositions.empty() ? nullptr : m_previousPositions.data();
    }

    const Vec3* HairSimulation::GetFlatRoots() const
    {
        return m_rootPositions.empty() ? nullptr : m_rootPositions.data();
    }
} // namespace hair
