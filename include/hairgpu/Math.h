#pragma once

#include <algorithm>
#include <cmath>

namespace hairgpu
{
    struct Vec2
    {
        float x = 0.0f;
        float y = 0.0f;
    };

    struct Vec3
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;

        Vec3() = default;
        Vec3(float inX, float inY, float inZ) : x(inX), y(inY), z(inZ) {}

        Vec3 operator+(const Vec3& rhs) const { return Vec3(x + rhs.x, y + rhs.y, z + rhs.z); }
        Vec3 operator-(const Vec3& rhs) const { return Vec3(x - rhs.x, y - rhs.y, z - rhs.z); }
        Vec3 operator*(float s) const { return Vec3(x * s, y * s, z * s); }
        Vec3 operator/(float s) const { return Vec3(x / s, y / s, z / s); }
        Vec3& operator+=(const Vec3& rhs)
        {
            x += rhs.x; y += rhs.y; z += rhs.z; return *this;
        }
    };

    struct Vec4
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float w = 0.0f;
    };

    struct Mat4
    {
        float m[16] = {
            1,0,0,0,
            0,1,0,0,
            0,0,1,0,
            0,0,0,1 };
    };

    template <typename T>
    inline T Clamp(const T& value, const T& minValue, const T& maxValue)
    {
        return (value < minValue) ? minValue : ((value > maxValue) ? maxValue : value);
    }

    inline float Dot(const Vec3& a, const Vec3& b)
    {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    inline Vec3 Cross(const Vec3& a, const Vec3& b)
    {
        return Vec3(
            a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x);
    }

    inline float Length(const Vec3& v)
    {
        return std::sqrt(Dot(v, v));
    }

    inline Vec3 Normalize(const Vec3& v)
    {
        const float length = Length(v);
        return (length > 1.0e-6f) ? (v / length) : Vec3(0.0f, 0.0f, 0.0f);
    }

    inline float Lerp(float a, float b, float t)
    {
        return a * (1.0f - t) + b * t;
    }

    inline Vec3 Lerp(const Vec3& a, const Vec3& b, float t)
    {
        return a * (1.0f - t) + b * t;
    }

    inline float BiLerp(float c00, float c10,
                        float c01, float c11,
                        float tx, float ty)
    {
        return Lerp(Lerp(c00, c10, tx),
                    Lerp(c01, c11, tx),
                    ty);
    }

    inline Vec3 BiLerp(const Vec3& c00, const Vec3& c10,
                       const Vec3& c01, const Vec3& c11,
                       float tx, float ty)
    {
        return Lerp(Lerp(c00, c10, tx),
                    Lerp(c01, c11, tx),
                    ty);
    }

    inline float TriLerp(float c000, float c100,
                         float c010, float c110,
                         float c001, float c101,
                         float c011, float c111,
                         float tx, float ty, float tz)
    {
        const float c00 = Lerp(c000, c100, tx);
        const float c10 = Lerp(c010, c110, tx);
        const float c01 = Lerp(c001, c101, tx);
        const float c11 = Lerp(c011, c111, tx);
        const float c0 = Lerp(c00, c10, ty);
        const float c1 = Lerp(c01, c11, ty);
        return Lerp(c0, c1, tz);
    }

    inline void ExpandBounds(Vec3& boundsMin, Vec3& boundsMax, const Vec3& p)
    {
        boundsMin.x = std::min(boundsMin.x, p.x);
        boundsMin.y = std::min(boundsMin.y, p.y);
        boundsMin.z = std::min(boundsMin.z, p.z);
        boundsMax.x = std::max(boundsMax.x, p.x);
        boundsMax.y = std::max(boundsMax.y, p.y);
        boundsMax.z = std::max(boundsMax.z, p.z);
    }

    inline float DistanceFactor(const Vec3& position,
                                const Vec3& cameraPosition,
                                float nearDistance,
                                float farDistance,
                                float epsilon = 1.0e-4f)
    {
        const float distance = Length(position - cameraPosition);
        return 1.0f - Clamp((distance - nearDistance) /
                            std::max(farDistance - nearDistance, epsilon),
                            0.0f,
                            1.0f);
    }

    inline float FrontFactor(const Vec3& position,
                             const Vec3& cameraPosition,
                             const Vec3& cameraForward)
    {
        const Vec3 direction = Normalize(position - cameraPosition);
        return Clamp((Dot(Normalize(cameraForward), direction) + 1.0f) * 0.5f,
                     0.0f,
                     1.0f);
    }

    inline float SilhouetteFactorX(const Vec3& root,
                                   const Vec3& center,
                                   float halfWidth,
                                   float epsilon = 1.0e-4f)
    {
        return Clamp(std::fabs(root.x - center.x) / std::max(halfWidth, epsilon),
                     0.0f,
                     1.0f);
    }

    Mat4 Identity();
    Mat4 PerspectiveFovRH(float fovYRadians, float aspectRatio, float nearZ, float farZ);
    Mat4 LookAtRH(const Vec3& eye, const Vec3& target, const Vec3& up);
    Mat4 Multiply(const Mat4& a, const Mat4& b);
    Mat4 RotationX(float radians);
    Mat4 RotationY(float radians);
    Mat4 Translation(const Vec3& offset);
    Vec3 TransformPoint(const Mat4& m, const Vec3& p);
}
