#pragma once

#include <cmath>
#include <cstdint>

#include "hairgpu/Math.h"

namespace hairgpu
{
    inline float Fract(float value)
    {
        return value - std::floor(value);
    }

    inline float WrapUnit(float value)
    {
        float wrapped = std::fmod(value, 1.0f);
        if (wrapped < 0.0f)
        {
            wrapped += 1.0f;
        }
        return wrapped;
    }

    inline float SmoothStep01(float value)
    {
        const float t = Clamp(value, 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    inline float Hash01Sine(uint32_t value)
    {
        return Fract(std::sin(static_cast<float>(value) * 12.9898f + 78.233f) * 43758.5453f);
    }

    inline float Hash01Murmur(uint32_t value)
    {
        value ^= value >> 16u;
        value *= 0x7feb352du;
        value ^= value >> 15u;
        value *= 0x846ca68bu;
        value ^= value >> 16u;
        return static_cast<float>(value & 0x00ffffffu) / static_cast<float>(0x01000000u);
    }

    inline float Hash01Wang(uint32_t value)
    {
        value ^= value >> 16u;
        value *= 2246822519u;
        value ^= value >> 13u;
        value *= 3266489917u;
        value ^= value >> 16u;
        return static_cast<float>(value & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
    }

    inline Vec3 RotateYaw(const Vec3& p, float radians)
    {
        const float c = std::cos(radians);
        const float s = std::sin(radians);
        return Vec3(p.x * c + p.z * s, p.y, -p.x * s + p.z * c);
    }
}
