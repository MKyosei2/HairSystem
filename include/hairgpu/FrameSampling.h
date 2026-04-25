#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace hairgpu
{
    struct FrameSample
    {
        uint32_t frame0 = 0u;
        uint32_t frame1 = 0u;
        float t = 0.0f;
    };

    inline bool ComputeFrameSample(uint32_t frameCount,
                                   float frameRate,
                                   float timeSeconds,
                                   bool loop,
                                   FrameSample& outSample)
    {
        if (frameCount == 0u || frameRate <= 0.0f)
        {
            outSample = {};
            return false;
        }

        const float frameTime = 1.0f / frameRate;
        const float totalDuration = frameTime * static_cast<float>(frameCount);
        float sampleTime = std::max(timeSeconds, 0.0f);

        if (loop && totalDuration > 1.0e-6f)
        {
            sampleTime = std::fmod(sampleTime, totalDuration);
            if (sampleTime < 0.0f)
            {
                sampleTime += totalDuration;
            }
        }
        else
        {
            sampleTime = std::min(sampleTime, std::max(totalDuration - frameTime, 0.0f));
        }

        const float frameIndexF = sampleTime / frameTime;
        const float frameIndexFloor = std::floor(frameIndexF);
        outSample.frame0 = std::min<uint32_t>(static_cast<uint32_t>(frameIndexFloor), frameCount - 1u);
        outSample.frame1 = loop
            ? ((outSample.frame0 + 1u) % frameCount)
            : std::min<uint32_t>(outSample.frame0 + 1u, frameCount - 1u);
        outSample.t = std::clamp(frameIndexF - frameIndexFloor, 0.0f, 1.0f);
        return true;
    }
}
