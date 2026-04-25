#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include "hairgpu/Types.h"

namespace hairgpu
{
    inline bool IsValidSdfGridValues(uint32_t resolutionX,
                                     uint32_t resolutionY,
                                     uint32_t resolutionZ,
                                     size_t valueCount)
    {
        return resolutionX >= 2u &&
               resolutionY >= 2u &&
               resolutionZ >= 2u &&
               valueCount == static_cast<size_t>(resolutionX) * resolutionY * resolutionZ;
    }

    inline bool IsValidSdfGrid(const SdfGridCollider& grid)
    {
        return IsValidSdfGridValues(grid.resolutionX,
                                    grid.resolutionY,
                                    grid.resolutionZ,
                                    grid.values.size());
    }

    inline size_t SdfGridIndex(uint32_t resolutionX,
                               uint32_t resolutionY,
                               uint32_t x,
                               uint32_t y,
                               uint32_t z)
    {
        return (static_cast<size_t>(z) * resolutionY + y) * resolutionX + x;
    }

    inline size_t SdfGridIndex(const SdfGridCollider& grid,
                               uint32_t x,
                               uint32_t y,
                               uint32_t z)
    {
        return SdfGridIndex(grid.resolutionX, grid.resolutionY, x, y, z);
    }

    inline Vec3 ComputeSdfCellSize(const Vec3& boundsMin,
                                   const Vec3& boundsMax,
                                   uint32_t resolutionX,
                                   uint32_t resolutionY,
                                   uint32_t resolutionZ)
    {
        return Vec3(
            (boundsMax.x - boundsMin.x) / static_cast<float>(std::max(1u, resolutionX - 1u)),
            (boundsMax.y - boundsMin.y) / static_cast<float>(std::max(1u, resolutionY - 1u)),
            (boundsMax.z - boundsMin.z) / static_cast<float>(std::max(1u, resolutionZ - 1u)));
    }

    inline Vec3 SdfGridCellSize(const SdfGridCollider& grid)
    {
        return ComputeSdfCellSize(grid.boundsMin,
                                  grid.boundsMax,
                                  grid.resolutionX,
                                  grid.resolutionY,
                                  grid.resolutionZ);
    }

    inline float SampleSdfValuesNearest(const std::vector<float>& values,
                                        uint32_t resolutionX,
                                        uint32_t resolutionY,
                                        uint32_t resolutionZ,
                                        int x,
                                        int y,
                                        int z,
                                        float invalidValue = std::numeric_limits<float>::max())
    {
        if (!IsValidSdfGridValues(resolutionX, resolutionY, resolutionZ, values.size()))
        {
            return invalidValue;
        }

        const int cx = std::clamp(x, 0, static_cast<int>(resolutionX - 1u));
        const int cy = std::clamp(y, 0, static_cast<int>(resolutionY - 1u));
        const int cz = std::clamp(z, 0, static_cast<int>(resolutionZ - 1u));
        return values[SdfGridIndex(resolutionX,
                                   resolutionY,
                                   static_cast<uint32_t>(cx),
                                   static_cast<uint32_t>(cy),
                                   static_cast<uint32_t>(cz))];
    }

    inline float SampleSdfValuesTrilinear(const std::vector<float>& values,
                                          const Vec3& boundsMin,
                                          const Vec3& boundsMax,
                                          uint32_t resolutionX,
                                          uint32_t resolutionY,
                                          uint32_t resolutionZ,
                                          const Vec3& point,
                                          float invalidValue = std::numeric_limits<float>::max())
    {
        if (!IsValidSdfGridValues(resolutionX, resolutionY, resolutionZ, values.size()))
        {
            return invalidValue;
        }

        const Vec3 cellSize = ComputeSdfCellSize(boundsMin, boundsMax, resolutionX, resolutionY, resolutionZ);
        const float gx = Clamp((point.x - boundsMin.x) / std::max(cellSize.x, 1.0e-6f), 0.0f, static_cast<float>(resolutionX - 1u));
        const float gy = Clamp((point.y - boundsMin.y) / std::max(cellSize.y, 1.0e-6f), 0.0f, static_cast<float>(resolutionY - 1u));
        const float gz = Clamp((point.z - boundsMin.z) / std::max(cellSize.z, 1.0e-6f), 0.0f, static_cast<float>(resolutionZ - 1u));

        const int x0 = static_cast<int>(std::floor(gx));
        const int y0 = static_cast<int>(std::floor(gy));
        const int z0 = static_cast<int>(std::floor(gz));
        const int x1 = x0 + 1;
        const int y1 = y0 + 1;
        const int z1 = z0 + 1;
        const float tx = gx - static_cast<float>(x0);
        const float ty = gy - static_cast<float>(y0);
        const float tz = gz - static_cast<float>(z0);

        const float c000 = SampleSdfValuesNearest(values, resolutionX, resolutionY, resolutionZ, x0, y0, z0, invalidValue);
        const float c100 = SampleSdfValuesNearest(values, resolutionX, resolutionY, resolutionZ, x1, y0, z0, invalidValue);
        const float c010 = SampleSdfValuesNearest(values, resolutionX, resolutionY, resolutionZ, x0, y1, z0, invalidValue);
        const float c110 = SampleSdfValuesNearest(values, resolutionX, resolutionY, resolutionZ, x1, y1, z0, invalidValue);
        const float c001 = SampleSdfValuesNearest(values, resolutionX, resolutionY, resolutionZ, x0, y0, z1, invalidValue);
        const float c101 = SampleSdfValuesNearest(values, resolutionX, resolutionY, resolutionZ, x1, y0, z1, invalidValue);
        const float c011 = SampleSdfValuesNearest(values, resolutionX, resolutionY, resolutionZ, x0, y1, z1, invalidValue);
        const float c111 = SampleSdfValuesNearest(values, resolutionX, resolutionY, resolutionZ, x1, y1, z1, invalidValue);

        return TriLerp(c000, c100,
                       c010, c110,
                       c001, c101,
                       c011, c111,
                       tx, ty, tz);
    }

    inline float SampleSdfGridTrilinear(const SdfGridCollider& grid,
                                        const Vec3& point,
                                        float invalidValue = std::numeric_limits<float>::max())
    {
        return SampleSdfValuesTrilinear(grid.values,
                                        grid.boundsMin,
                                        grid.boundsMax,
                                        grid.resolutionX,
                                        grid.resolutionY,
                                        grid.resolutionZ,
                                        point,
                                        invalidValue);
    }

    inline Vec3 EstimateSdfValuesGradient(const std::vector<float>& values,
                                          const Vec3& boundsMin,
                                          const Vec3& boundsMax,
                                          uint32_t resolutionX,
                                          uint32_t resolutionY,
                                          uint32_t resolutionZ,
                                          const Vec3& point,
                                          const Vec3& fallback = Vec3(0.0f, 1.0f, 0.0f))
    {
        if (!IsValidSdfGridValues(resolutionX, resolutionY, resolutionZ, values.size()))
        {
            return fallback;
        }

        const Vec3 cellSize = ComputeSdfCellSize(boundsMin, boundsMax, resolutionX, resolutionY, resolutionZ);
        const float epsX = std::max(cellSize.x, 1.0e-4f);
        const float epsY = std::max(cellSize.y, 1.0e-4f);
        const float epsZ = std::max(cellSize.z, 1.0e-4f);

        const float dx = SampleSdfValuesTrilinear(values, boundsMin, boundsMax, resolutionX, resolutionY, resolutionZ, point + Vec3(epsX, 0.0f, 0.0f))
                       - SampleSdfValuesTrilinear(values, boundsMin, boundsMax, resolutionX, resolutionY, resolutionZ, point - Vec3(epsX, 0.0f, 0.0f));
        const float dy = SampleSdfValuesTrilinear(values, boundsMin, boundsMax, resolutionX, resolutionY, resolutionZ, point + Vec3(0.0f, epsY, 0.0f))
                       - SampleSdfValuesTrilinear(values, boundsMin, boundsMax, resolutionX, resolutionY, resolutionZ, point - Vec3(0.0f, epsY, 0.0f));
        const float dz = SampleSdfValuesTrilinear(values, boundsMin, boundsMax, resolutionX, resolutionY, resolutionZ, point + Vec3(0.0f, 0.0f, epsZ))
                       - SampleSdfValuesTrilinear(values, boundsMin, boundsMax, resolutionX, resolutionY, resolutionZ, point - Vec3(0.0f, 0.0f, epsZ));

        const Vec3 gradient = Normalize(Vec3(dx, dy, dz));
        return (Length(gradient) > 1.0e-6f) ? gradient : fallback;
    }

    inline Vec3 EstimateSdfGridGradient(const SdfGridCollider& grid,
                                        const Vec3& point,
                                        const Vec3& fallback = Vec3(0.0f, 1.0f, 0.0f))
    {
        return EstimateSdfValuesGradient(grid.values,
                                         grid.boundsMin,
                                         grid.boundsMax,
                                         grid.resolutionX,
                                         grid.resolutionY,
                                         grid.resolutionZ,
                                         point,
                                         fallback);
    }
}
