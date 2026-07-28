#pragma once

#include <cstdint>

namespace hairgpu
{
    // Safety-first SG-APT v1:
    // - SegmentGroupGpuData keeps its fixed GPU index.
    // - Dynamic payloads live in a separate fixed-capacity pool.
    // - Retired slots are not recycled during a run. This deliberately trades
    //   peak efficiency for freedom from same-frame/use-after-free hazards.
    // - A group that cannot obtain a payload continues through the existing
    //   full-position/previous-position path and is marked as fallback.
    static constexpr uint32_t kSgaptInvalidPayloadIndex = 0x00ffffffu;
    static constexpr uint32_t kSgaptPayloadIndexMask = 0x00ffffffu;
    static constexpr uint32_t kSgaptFlagPayloadValid = 0x80000000u;
    static constexpr uint32_t kSgaptFlagPayloadFallback = 0x40000000u;
    static constexpr uint32_t kSgaptDefaultPayloadCapacityPercent = 50u;

    enum SgaptPayloadPoolCounter : uint32_t
    {
        SgaptPayloadPoolCounter_NextUninitialized = 0u,
        SgaptPayloadPoolCounter_Active = 1u,
        SgaptPayloadPoolCounter_Overflow = 2u,
        SgaptPayloadPoolCounter_Retired = 3u,
        SgaptPayloadPoolCounter_Allocation = 4u,
        SgaptPayloadPoolCounter_Fallback = 5u,
        SgaptPayloadPoolCounter_Read = 6u,
        SgaptPayloadPoolCounter_Write = 7u,
        SgaptPayloadPoolCounter_Count = 8u
    };

    // 32-byte optional sidecar. It intentionally contains only values that are
    // required by the dynamic execution path in the first safe implementation.
    // Distance/bend lambdas remain in the existing baseline buffers until their
    // lifetime can be separated without changing solver equivalence.
    struct alignas(16) SegmentDynamicPayloadGpuData
    {
        float previousEndPosition[3] = { 0.0f, 0.0f, 0.0f };
        uint32_t allocationEpoch = 0u;

        float velocity[3] = { 0.0f, 0.0f, 0.0f };
        uint32_t payloadVersion = 0u;
    };

    struct alignas(16) SgaptPayloadConstants
    {
        uint32_t groupCount = 0u;
        uint32_t payloadCapacity = 0u;
        uint32_t frameIndex = 0u;
        uint32_t dynamicState = 2u;

        float deltaTime = 1.0f / 60.0f;
        uint32_t invalidPayloadIndex = kSgaptInvalidPayloadIndex;
        uint32_t payloadIndexMask = kSgaptPayloadIndexMask;
        uint32_t flags = 0u;
    };

    static_assert(
        sizeof(SegmentDynamicPayloadGpuData) == 32u,
        "SegmentDynamicPayloadGpuData must match HairSegmentGroupAdaptivePayloadRuntime.hlsl");

    static_assert(
        sizeof(SgaptPayloadConstants) == 32u,
        "SgaptPayloadConstants must match HairSegmentGroupAdaptivePayloadRuntime.hlsl");
}
