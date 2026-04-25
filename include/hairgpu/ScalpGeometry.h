#pragma once

#include <algorithm>

#include "hairgpu/Geometry.h"

namespace hairgpu
{
    inline Vec3 ComputeScalpCenter(const ScalpMesh& scalpMesh,
                                   const Vec3& fallback = Vec3(0.0f, 1.6f, 0.0f))
    {
        if (scalpMesh.positions.empty())
        {
            return fallback;
        }

        Vec3 center = {};
        for (const Vec3& p : scalpMesh.positions)
        {
            center += p;
        }
        return center / static_cast<float>(scalpMesh.positions.size());
    }

    inline Vec3 ComputeTriangleNormal(const ScalpMesh& scalpMesh,
                                      const TriangleIndices& tri,
                                      const Vec3& fallback = Vec3(0.0f, 1.0f, 0.0f))
    {
        if (tri.a >= scalpMesh.positions.size() ||
            tri.b >= scalpMesh.positions.size() ||
            tri.c >= scalpMesh.positions.size())
        {
            return fallback;
        }

        return NormalizeOrFallback(Cross(scalpMesh.positions[tri.b] - scalpMesh.positions[tri.a],
                                         scalpMesh.positions[tri.c] - scalpMesh.positions[tri.a]),
                                   fallback);
    }

    inline Vec3 ComputeOutwardTriangleNormal(const ScalpMesh& scalpMesh,
                                             const TriangleIndices& tri,
                                             const Vec3& scalpCenter,
                                             const Vec3& fallback = Vec3(0.0f, 1.0f, 0.0f))
    {
        Vec3 normal = ComputeTriangleNormal(scalpMesh, tri, fallback);
        if (tri.a >= scalpMesh.positions.size() ||
            tri.b >= scalpMesh.positions.size() ||
            tri.c >= scalpMesh.positions.size())
        {
            return normal;
        }

        const Vec3 centroid = (scalpMesh.positions[tri.a] + scalpMesh.positions[tri.b] + scalpMesh.positions[tri.c]) / 3.0f;
        if (Dot(normal, centroid - scalpCenter) < 0.0f)
        {
            normal = normal * -1.0f;
        }
        return normal;
    }

    inline Vec3 EvaluateSurfaceNormal(const ScalpMesh& scalpMesh,
                                      const TriangleIndices& tri,
                                      const Vec3& barycentric,
                                      const Vec3& scalpCenter,
                                      const Vec3& fallback = Vec3(0.0f, 1.0f, 0.0f))
    {
        const Vec3 outward = ComputeOutwardTriangleNormal(scalpMesh, tri, scalpCenter, fallback);
        if (tri.a < scalpMesh.normals.size() &&
            tri.b < scalpMesh.normals.size() &&
            tri.c < scalpMesh.normals.size())
        {
            Vec3 normal = EvaluateBarycentricNormal(scalpMesh.normals[tri.a],
                                                    scalpMesh.normals[tri.b],
                                                    scalpMesh.normals[tri.c],
                                                    barycentric,
                                                    outward);
            if (Dot(normal, outward) < 0.0f)
            {
                normal = normal * -1.0f;
            }
            return normal;
        }
        return outward;
    }

    inline Vec3 EvaluateScalpNormal(const ScalpMesh& scalpMesh,
                                    const RootBinding& binding,
                                    const Vec3& fallback = Vec3(0.0f, 1.0f, 0.0f))
    {
        if (binding.triangleIndex >= scalpMesh.triangles.size())
        {
            return fallback;
        }
        const Vec3 center = ComputeScalpCenter(scalpMesh, fallback);
        return EvaluateSurfaceNormal(scalpMesh,
                                     scalpMesh.triangles[binding.triangleIndex],
                                     binding.barycentric,
                                     center,
                                     fallback);
    }

    inline Vec3 EvaluateScalpPosition(const ScalpMesh& scalpMesh,
                                      const RootBinding& binding,
                                      const Vec3& fallback = Vec3())
    {
        if (binding.triangleIndex >= scalpMesh.triangles.size())
        {
            return fallback;
        }
        const TriangleIndices& tri = scalpMesh.triangles[binding.triangleIndex];
        if (tri.a >= scalpMesh.positions.size() ||
            tri.b >= scalpMesh.positions.size() ||
            tri.c >= scalpMesh.positions.size())
        {
            return fallback;
        }
        return EvaluateBarycentric(scalpMesh.positions[tri.a],
                                   scalpMesh.positions[tri.b],
                                   scalpMesh.positions[tri.c],
                                   binding.barycentric);
    }

    inline Vec2 EvaluateScalpUv(const ScalpMesh& scalpMesh,
                                const RootBinding& binding,
                                const Vec2& fallback = Vec2())
    {
        if (binding.triangleIndex >= scalpMesh.triangles.size())
        {
            return fallback;
        }
        const TriangleIndices& tri = scalpMesh.triangles[binding.triangleIndex];
        if (tri.a >= scalpMesh.uvs.size() ||
            tri.b >= scalpMesh.uvs.size() ||
            tri.c >= scalpMesh.uvs.size())
        {
            return fallback;
        }
        return EvaluateBarycentric(scalpMesh.uvs[tri.a],
                                   scalpMesh.uvs[tri.b],
                                   scalpMesh.uvs[tri.c],
                                   binding.barycentric);
    }

    struct ScalpFrame
    {
        Vec3 normal = Vec3(0.0f, 1.0f, 0.0f);
        Vec3 tangent = Vec3(1.0f, 0.0f, 0.0f);
        Vec3 bitangent = Vec3(0.0f, 0.0f, 1.0f);
    };

    inline ScalpFrame EvaluateScalpFrame(const ScalpMesh& scalpMesh,
                                         const RootBinding& binding,
                                         const Vec3& preferredTangent = Vec3(1.0f, 0.0f, 0.0f))
    {
        ScalpFrame frame = {};
        frame.normal = EvaluateScalpNormal(scalpMesh, binding);
        frame.tangent = NormalizeOrFallback(preferredTangent - frame.normal * Dot(preferredTangent, frame.normal),
                                            std::fabs(frame.normal.y) < 0.9f ? Vec3(0.0f, 1.0f, 0.0f) : Vec3(1.0f, 0.0f, 0.0f));
        frame.bitangent = NormalizeOrFallback(Cross(frame.normal, frame.tangent), Vec3(0.0f, 0.0f, 1.0f));
        frame.tangent = NormalizeOrFallback(Cross(frame.bitangent, frame.normal), frame.tangent);
        return frame;
    }
}
