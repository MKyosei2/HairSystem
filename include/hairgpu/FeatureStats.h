#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace hairgpu
{
    inline float NormalizeValue(float value, float mean, float stddev, float epsilon = 1.0e-8f)
    {
        return (std::fabs(stddev) > epsilon) ? ((value - mean) / stddev) : (value - mean);
    }

    inline float DenormalizeValue(float value, float mean, float stddev, float epsilon = 1.0e-8f)
    {
        return value * ((std::fabs(stddev) > epsilon) ? stddev : 1.0f) + mean;
    }

    struct RunningScalarStats
    {
        double sum = 0.0;
        double sumSq = 0.0;
        uint64_t count = 0u;

        void Add(float value)
        {
            const double v = static_cast<double>(value);
            sum += v;
            sumSq += v * v;
            ++count;
        }

        float Mean() const
        {
            return count > 0u ? static_cast<float>(sum / static_cast<double>(count)) : 0.0f;
        }

        float StdDev(float epsilon = 1.0e-6f) const
        {
            if (count == 0u)
            {
                return 1.0f;
            }
            const double mean = sum / static_cast<double>(count);
            const double variance = std::max(0.0, sumSq / static_cast<double>(count) - mean * mean);
            return static_cast<float>(std::sqrt(variance + static_cast<double>(epsilon)));
        }
    };

    template <size_t N>
    struct RunningFeatureStats
    {
        std::array<double, N> sum = {};
        std::array<double, N> sumSq = {};
        uint64_t count = 0u;

        void Add(const std::array<float, N>& values)
        {
            for (size_t i = 0u; i < N; ++i)
            {
                const double v = static_cast<double>(values[i]);
                sum[i] += v;
                sumSq[i] += v * v;
            }
            ++count;
        }

        void Finalize(std::array<float, N>& outMean, std::array<float, N>& outStd, float epsilon = 1.0e-6f) const
        {
            for (size_t i = 0u; i < N; ++i)
            {
                if (count == 0u)
                {
                    outMean[i] = 0.0f;
                    outStd[i] = 1.0f;
                    continue;
                }
                const double mean = sum[i] / static_cast<double>(count);
                const double variance = std::max(0.0, sumSq[i] / static_cast<double>(count) - mean * mean);
                outMean[i] = static_cast<float>(mean);
                outStd[i] = static_cast<float>(std::sqrt(variance + static_cast<double>(epsilon)));
            }
        }
    };


    template <size_t N>
    inline void FinalizeArrayStats(const std::array<double, N>& sum,
                                   const std::array<double, N>& sumSq,
                                   double count,
                                   std::vector<float>& outMean,
                                   std::vector<float>& outStd,
                                   float epsilon = 1.0e-4f)
    {
        outMean.resize(N);
        outStd.resize(N);
        for (size_t i = 0u; i < N; ++i)
        {
            const double mean = (count > 0.0) ? (sum[i] / count) : 0.0;
            const double variance = (count > 0.0) ? std::max(0.0, (sumSq[i] / count) - mean * mean) : 1.0;
            outMean[i] = static_cast<float>(mean);
            outStd[i] = static_cast<float>(std::sqrt(variance) + static_cast<double>(epsilon));
        }
    }

    inline void FinalizeFeatureStats(const std::vector<double>& sum,
                                     const std::vector<double>& sumSq,
                                     double count,
                                     std::vector<float>& outMean,
                                     std::vector<float>& outStd,
                                     float epsilon = 1.0e-4f)
    {
        const size_t featureCount = std::min(sum.size(), sumSq.size());
        outMean.resize(featureCount, 0.0f);
        outStd.resize(featureCount, 1.0f);
        for (size_t i = 0u; i < featureCount; ++i)
        {
            const double mean = (count > 0.0) ? (sum[i] / count) : 0.0;
            const double variance = (count > 0.0) ? std::max(0.0, (sumSq[i] / count) - mean * mean) : 1.0;
            outMean[i] = static_cast<float>(mean);
            outStd[i] = static_cast<float>(std::sqrt(variance) + static_cast<double>(epsilon));
        }
    }
}
