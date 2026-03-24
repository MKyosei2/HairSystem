#include "hair/HairSimulation.h"

#include <algorithm>
#include <cmath>

namespace hair
{
    namespace
    {
        constexpr float kEpsilon = 1.0e-5f;
        constexpr float kPi = 3.14159265358979323846f;

        float Clamp(float value, float minValue, float maxValue)
        {
            return std::max(minValue, std::min(value, maxValue));
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

    HairSimulation::HairSimulation()
    {
        Reset();
    }

    HairSimulation::HairSimulation(const SimulationConfig& config)
        : m_config(config)
    {
        Reset();
    }

    void HairSimulation::Reset()
    {
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
        m_config.damping = config.damping;
        m_config.solverIterations = config.solverIterations;
        m_config.bendStiffness = config.bendStiffness;
    }

    const SimulationConfig& HairSimulation::GetConfig() const
    {
        return m_config;
    }

    const std::vector<Strand>& HairSimulation::GetStrands() const
    {
        return m_strands;
    }

    const std::vector<SphereCollider>& HairSimulation::GetColliders() const
    {
        return m_colliders;
    }

    void HairSimulation::Step(float deltaTime)
    {
        if (deltaTime <= 0.0f)
        {
            return;
        }

        Integrate(deltaTime);

        const int iterations = std::max(1, m_config.solverIterations);
        for (int i = 0; i < iterations; ++i)
        {
            SolveConstraints();
        }

        PinRoots();
    }

    void HairSimulation::BuildDefaultScene()
    {
        const int strandCount = std::max(1, m_config.strandCount);
        const int segmentCount = std::max(2, m_config.segmentCount);

        m_strands.clear();
        m_rootPositions.clear();
        m_strands.reserve(static_cast<std::size_t>(strandCount));
        m_rootPositions.reserve(static_cast<std::size_t>(strandCount));

        const float startAngle = -1.05f;
        const float endAngle = 1.05f;

        for (int strandIndex = 0; strandIndex < strandCount; ++strandIndex)
        {
            const float t = (strandCount == 1)
                ? 0.5f
                : static_cast<float>(strandIndex) / static_cast<float>(strandCount - 1);

            const float angle = startAngle + (endAngle - startAngle) * t;
            const Vec3 rootPosition(
                m_config.scalpCenter.x + std::sin(angle) * m_config.scalpRadius,
                m_config.scalpCenter.y - std::cos(angle) * (m_config.scalpRadius * 0.82f),
                0.0f);

            Strand strand;
            strand.particles.reserve(static_cast<std::size_t>(segmentCount));
            strand.restLengths.reserve(static_cast<std::size_t>(segmentCount - 1));

            for (int segmentIndex = 0; segmentIndex < segmentCount; ++segmentIndex)
            {
                const Vec3 position(rootPosition.x, rootPosition.y + segmentIndex * m_config.segmentLength, 0.0f);
                Particle particle;
                particle.position = position;
                particle.previousPosition = position;
                particle.pinned = (segmentIndex == 0);
                strand.particles.push_back(particle);

                if (segmentIndex > 0)
                {
                    strand.restLengths.push_back(m_config.segmentLength);
                }
            }

            m_strands.push_back(strand);
            m_rootPositions.push_back(rootPosition);
        }

        RebuildColliders();
    }

    void HairSimulation::Integrate(float deltaTime)
    {
        const float dtSquared = deltaTime * deltaTime;
        const float velocityScale = Clamp(1.0f - m_config.damping, 0.0f, 1.0f);
        const Vec3 gravityForce(0.0f, m_config.gravity, 0.0f);

        for (auto& strand : m_strands)
        {
            for (auto& particle : strand.particles)
            {
                if (particle.pinned)
                {
                    continue;
                }

                const Vec3 currentPosition = particle.position;
                const Vec3 velocity = (particle.position - particle.previousPosition) * velocityScale;
                particle.position += velocity + gravityForce * dtSquared;
                particle.previousPosition = currentPosition;
            }
        }
    }

    void HairSimulation::SolveConstraints()
    {
        PinRoots();
        SolveDistanceConstraints();
        SolveBendConstraints();
        SolveCollisions();
        PinRoots();
    }

    void HairSimulation::SolveDistanceConstraints()
    {
        for (auto& strand : m_strands)
        {
            for (std::size_t i = 0; i + 1 < strand.particles.size(); ++i)
            {
                Particle& a = strand.particles[i];
                Particle& b = strand.particles[i + 1];
                const float restLength = strand.restLengths[i];

                Vec3 delta = b.position - a.position;
                const float currentLength = Length(delta);
                if (currentLength < kEpsilon)
                {
                    continue;
                }

                const float difference = (currentLength - restLength) / currentLength;
                const Vec3 correction = delta * (0.5f * difference);

                if (!a.pinned)
                {
                    a.position += correction;
                }

                if (!b.pinned)
                {
                    b.position -= correction;
                }
            }
        }
    }

    void HairSimulation::SolveBendConstraints()
    {
        const float bendStrength = Clamp(m_config.bendStiffness, 0.0f, 1.0f);
        if (bendStrength <= 0.0f)
        {
            return;
        }

        for (auto& strand : m_strands)
        {
            if (strand.particles.size() < 3)
            {
                continue;
            }

            for (std::size_t i = 1; i + 1 < strand.particles.size(); ++i)
            {
                Particle& current = strand.particles[i];
                Particle& next = strand.particles[i + 1];
                Particle& previous = strand.particles[i - 1];

                if (current.pinned)
                {
                    continue;
                }

                const Vec3 midpoint = (previous.position + next.position) * 0.5f;
                current.position = Lerp(current.position, midpoint, bendStrength * 0.5f);
            }
        }
    }

    void HairSimulation::SolveCollisions()
    {
        for (auto& strand : m_strands)
        {
            for (auto& particle : strand.particles)
            {
                if (particle.pinned)
                {
                    continue;
                }

                if (particle.position.y > m_config.floorY)
                {
                    particle.position.y = m_config.floorY;
                }

                for (const SphereCollider& collider : m_colliders)
                {
                    Vec3 offset = particle.position - collider.center;
                    float distance = Length(offset);
                    if (distance < kEpsilon)
                    {
                        offset = Vec3(0.0f, -1.0f, 0.0f);
                        distance = 1.0f;
                    }

                    if (distance < collider.radius)
                    {
                        const Vec3 normal = offset / distance;
                        particle.position = collider.center + normal * collider.radius;
                    }
                }
            }
        }
    }

    void HairSimulation::PinRoots()
    {
        const std::size_t count = std::min(m_strands.size(), m_rootPositions.size());
        for (std::size_t i = 0; i < count; ++i)
        {
            if (m_strands[i].particles.empty())
            {
                continue;
            }

            Particle& root = m_strands[i].particles.front();
            root.position = m_rootPositions[i];
            root.previousPosition = m_rootPositions[i];
            root.pinned = true;
        }
    }

    void HairSimulation::RebuildColliders()
    {
        m_colliders.clear();
        m_colliders.push_back({ m_config.scalpCenter, m_config.scalpRadius * 0.92f });
        m_colliders.push_back({ Vec3(m_config.scalpCenter.x - 44.0f, m_config.scalpCenter.y + 150.0f, 0.0f), 52.0f });
        m_colliders.push_back({ Vec3(m_config.scalpCenter.x + 44.0f, m_config.scalpCenter.y + 150.0f, 0.0f), 52.0f });
    }
} // namespace hair
