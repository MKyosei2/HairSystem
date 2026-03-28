#pragma once

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

    Mat4 Identity();
    Mat4 PerspectiveFovRH(float fovYRadians, float aspectRatio, float nearZ, float farZ);
    Mat4 LookAtRH(const Vec3& eye, const Vec3& target, const Vec3& up);
    Mat4 Multiply(const Mat4& a, const Mat4& b);
    Mat4 RotationX(float radians);
    Mat4 RotationY(float radians);
    Mat4 Translation(const Vec3& offset);
    Vec3 TransformPoint(const Mat4& m, const Vec3& p);
}
