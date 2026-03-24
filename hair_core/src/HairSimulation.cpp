#include "hair/HairSimulation.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
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

    std::size_t HairSimulation::ParticleIndex(int strandIndex, int particleIndex) const
    {
        return static_cast<std::size_t>(strandIndex) * static_cast<std::size_t>(m_particlesPerStrand)
            + static_cast<std::size_t>(particleIndex);
    }

    void HairSimulation::Reset()
    {
        m_config.strandCount = ClampInt(m_config.strandCount, 1, 25000);
        m_config.segmentCount = ClampInt(m_config.segmentCount, 2, 24);
        m_config.solverIterations = ClampInt(m_config.solverIterations, 1, 10);
        m_config.damping = Clamp(m_config.damping, 0.0f, 0.20f);
        m_config.bendStiffness = Clamp(m_config.bendStiffness, 0.0f, 1.0f);
        m_config.clusterStiffness = Clamp(m_config.clusterStiffness, 0.0f, 1.0f);
        m_config.windDirectionX = Clamp(m_config.windDirectionX, -1.0f, 1.0f);
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
        m_config.solverIterations = ClampInt(config.solverIterations, 1, 10);
        m_config.bendStiffness = Clamp(config.bendStiffness, 0.0f, 1.0f);
        m_config.clusterStiffness = Clamp(config.clusterStiffness, 0.0f, 1.0f);
        m_config.windStrength = config.windStrength;
        m_config.windDirectionX = Clamp(config.windDirectionX, -1.0f, 1.0f);
        m_config.windPulseFrequency = std::max(0.0f, config.windPulseFrequency);
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

    void HairSimulation::RebuildColliders()
    {
        m_colliders.clear();
        m_colliders.push_back({ m_config.scalpCenter, m_config.scalpRadius });
        m_colliders.push_back({ Vec3(m_config.scalpCenter.x - m_config.scalpRadius * 0.65f, m_config.scalpCenter.y + m_config.scalpRadius * 1.55f, 0.0f), m_config.scalpRadius * 0.50f });
        m_colliders.push_back({ Vec3(m_config.scalpCenter.x + m_config.scalpRadius * 0.65f, m_config.scalpCenter.y + m_config.scalpRadius * 1.55f, 0.0f), m_config.scalpRadius * 0.50f });
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
                Vec3 velocity = (current - previous) * dampingScale;

                const float normalizedDepth = static_cast<float>(particleIndex) / static_cast<float>(std::max(1, m_particlesPerStrand - 1));
                const float localFlutter = std::sin((m_elapsedTimeSeconds * 3.6f) + strandNoise * 10.0f + normalizedDepth * 4.0f);
                Vec3 windAcceleration(
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
                Vec3 delta = b - a;
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

    void HairSimulation::SolveCollisions()
    {
        for (Vec3& position : m_positions)
        {
            if (position.y > m_config.floorY)
            {
                position.y = m_config.floorY;
            }
        }

        for (const SphereCollider& collider : m_colliders)
        {
            for (int strandIndex = 0; strandIndex < m_config.strandCount; ++strandIndex)
            {
                for (int particleIndex = 1; particleIndex < m_particlesPerStrand; ++particleIndex)
                {
                    const std::size_t flatIndex = ParticleIndex(strandIndex, particleIndex);
                    Vec3 delta = m_positions[flatIndex] - collider.center;
                    const float distance = Length(delta);
                    if (distance < collider.radius)
                    {
                        const Vec3 normal = (distance > kEpsilon) ? (delta / distance) : Vec3(0.0f, -1.0f, 0.0f);
                        m_positions[flatIndex] = collider.center + normal * collider.radius;
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

        output << "HAIRSNAPSHOT_V3\n";
        output << m_config.strandCount << ' ' << m_config.segmentCount << '\n';
        output << m_config.segmentLength << ' ' << m_config.rootSpacing << ' ' << m_config.gravity << ' '
               << m_config.damping << ' ' << m_config.solverIterations << ' ' << m_config.bendStiffness << ' '
               << m_config.clusterStiffness << ' ' << m_config.floorY << '\n';
        output << m_config.windStrength << ' ' << m_config.windDirectionX << ' ' << m_config.windPulseFrequency << ' '
               << static_cast<int>(m_config.scenePreset) << ' ' << m_elapsedTimeSeconds << '\n';
        WriteVec3(output, m_config.scalpCenter);
        output << ' ' << m_config.scalpRadius << '\n';
        output << m_particlesPerStrand << '\n';
        output << m_positions.size() << '\n';

        for (std::size_t i = 0; i < m_positions.size(); ++i)
        {
            WriteVec3(output, m_positions[i]);
            output << ' ';
            WriteVec3(output, m_previousPositions[i]);
            output << '\n';
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
        if (header != "HAIRSNAPSHOT_V1" && header != "HAIRSNAPSHOT_V2" && header != "HAIRSNAPSHOT_V3")
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

        loadedConfig.strandCount = ClampInt(loadedConfig.strandCount, 1, 25000);
        loadedConfig.segmentCount = ClampInt(loadedConfig.segmentCount, 2, 24);
        m_config = loadedConfig;
        m_particlesPerStrand = m_config.segmentCount + 1;
        RebuildColliders();
        m_rootPositions.assign(static_cast<std::size_t>(m_config.strandCount), Vec3());

        if (header == "HAIRSNAPSHOT_V3")
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
            for (int strandIndex = 0; strandIndex < m_config.strandCount; ++strandIndex)
            {
                m_rootPositions[static_cast<std::size_t>(strandIndex)] = m_positions[ParticleIndex(strandIndex, 0)];
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
} // namespace hair
