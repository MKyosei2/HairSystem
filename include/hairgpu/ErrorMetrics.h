#pragma once

#include <cmath>
#include <cstdint>

#include "hairgpu/Math.h"

namespace hairgpu
{
    struct RmsAccumulator
    {
        double sumSq = 0.0;
        uint64_t count = 0u;

        void AddSquared(double squaredError)
        {
            sumSq += squaredError;
            ++count;
        }

        void AddScalarError(float error)
        {
            const double e = static_cast<double>(error);
            AddSquared(e * e);
        }

        void AddVectorError(const Vec3& error)
        {
            AddSquared(static_cast<double>(Dot(error, error)));
        }

        float Rms() const
        {
            return count > 0u ? static_cast<float>(std::sqrt(sumSq / static_cast<double>(count))) : 0.0f;
        }
    };
}
