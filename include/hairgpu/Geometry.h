#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>

#include "hairgpu/Types.h"

namespace hairgpu
{
    struct ClosestTrianglePoint
    {
        Vec3 position = {};
        Vec3 barycentric = { 1.0f, 0.0f, 0.0f };
        float distanceSq = std::numeric_limits<float>::max();
        uint32_t triangleIndex = 0u;
    };

    inline Vec3 EvaluateBarycentric(const Vec3& a,
                                    const Vec3& b,
                                    const Vec3& c,
                                    const Vec3& barycentric)
    {
        return a * barycentric.x + b * barycentric.y + c * barycentric.z;
    }

    inline Vec2 EvaluateBarycentric(const Vec2& a,
                                    const Vec2& b,
                                    const Vec2& c,
                                    const Vec3& barycentric)
    {
        return {
            a.x * barycentric.x + b.x * barycentric.y + c.x * barycentric.z,
            a.y * barycentric.x + b.y * barycentric.y + c.y * barycentric.z
        };
    }

    inline Vec3 NormalizeOrFallback(const Vec3& value, const Vec3& fallback = Vec3(0.0f, 1.0f, 0.0f))
    {
        const float length = Length(value);
        return (length > 1.0e-6f) ? (value / length) : fallback;
    }

    inline Vec3 EvaluateBarycentricNormal(const Vec3& a,
                                          const Vec3& b,
                                          const Vec3& c,
                                          const Vec3& barycentric,
                                          const Vec3& fallback = Vec3(0.0f, 1.0f, 0.0f))
    {
        return NormalizeOrFallback(EvaluateBarycentric(a, b, c, barycentric), fallback);
    }

    inline ClosestTrianglePoint ClosestPointOnTriangle(const Vec3& p,
                                                       const Vec3& a,
                                                       const Vec3& b,
                                                       const Vec3& c)
    {
        const Vec3 ab = b - a;
        const Vec3 ac = c - a;
        const Vec3 ap = p - a;
        const float d1 = Dot(ab, ap);
        const float d2 = Dot(ac, ap);
        if (d1 <= 0.0f && d2 <= 0.0f)
        {
            return { a, Vec3(1.0f, 0.0f, 0.0f), Dot(p - a, p - a), 0u };
        }

        const Vec3 bp = p - b;
        const float d3 = Dot(ab, bp);
        const float d4 = Dot(ac, bp);
        if (d3 >= 0.0f && d4 <= d3)
        {
            return { b, Vec3(0.0f, 1.0f, 0.0f), Dot(p - b, p - b), 0u };
        }

        const float vc = d1 * d4 - d3 * d2;
        if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f)
        {
            const float v = d1 / std::max(d1 - d3, 1.0e-8f);
            const Vec3 q = a + ab * v;
            return { q, Vec3(1.0f - v, v, 0.0f), Dot(p - q, p - q), 0u };
        }

        const Vec3 cp = p - c;
        const float d5 = Dot(ab, cp);
        const float d6 = Dot(ac, cp);
        if (d6 >= 0.0f && d5 <= d6)
        {
            return { c, Vec3(0.0f, 0.0f, 1.0f), Dot(p - c, p - c), 0u };
        }

        const float vb = d5 * d2 - d1 * d6;
        if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f)
        {
            const float w = d2 / std::max(d2 - d6, 1.0e-8f);
            const Vec3 q = a + ac * w;
            return { q, Vec3(1.0f - w, 0.0f, w), Dot(p - q, p - q), 0u };
        }

        const float va = d3 * d6 - d5 * d4;
        if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f)
        {
            const Vec3 bc = c - b;
            const float w = (d4 - d3) / std::max((d4 - d3) + (d5 - d6), 1.0e-8f);
            const Vec3 q = b + bc * w;
            return { q, Vec3(0.0f, 1.0f - w, w), Dot(p - q, p - q), 0u };
        }

        const float denom = 1.0f / std::max(va + vb + vc, 1.0e-8f);
        const float v = vb * denom;
        const float w = vc * denom;
        const float u = 1.0f - v - w;
        const Vec3 q = EvaluateBarycentric(a, b, c, Vec3(u, v, w));
        return { q, Vec3(u, v, w), Dot(p - q, p - q), 0u };
    }

    struct CapsuleProjection
    {
        float t = 0.0f;
        Vec3 closest = {};
        float radius = 0.0f;
        Vec3 delta = {};
        float distance = 0.0f;
        Vec3 normal = { 0.0f, 1.0f, 0.0f };
    };

    inline CapsuleProjection ProjectToCapsule(const CapsuleCollider& capsule,
                                              const Vec3& point,
                                              float radiusOffset = 0.0f)
    {
        const Vec3 axis = capsule.pointB - capsule.pointA;
        const float denom = Dot(axis, axis);
        const float t = (denom > 1.0e-6f)
            ? Clamp(Dot(point - capsule.pointA, axis) / denom, 0.0f, 1.0f)
            : 0.0f;

        CapsuleProjection projection = {};
        projection.t = t;
        projection.closest = capsule.pointA + axis * t;
        projection.radius = Lerp(capsule.radiusA, capsule.radiusB, t) + radiusOffset;
        projection.delta = point - projection.closest;
        projection.distance = Length(projection.delta);
        projection.normal = NormalizeOrFallback(projection.delta);
        return projection;
    }
}
