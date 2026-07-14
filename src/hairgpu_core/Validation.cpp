#include "hairgpu/Validation.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>

namespace hairgpu
{
    namespace
    {
        constexpr float kZeroLengthEpsilon = 1.0e-7f;
        constexpr uint64_t kFnvOffset = 1469598103934665603ull;
        constexpr uint64_t kFnvPrime = 1099511628211ull;

        bool IsFinite(float v)
        {
            return std::isfinite(v) != 0;
        }

        bool IsFinite(const Vec3& v)
        {
            return IsFinite(v.x) && IsFinite(v.y) && IsFinite(v.z);
        }

        float Distance(const Vec3& a, const Vec3& b)
        {
            const float x = b.x - a.x;
            const float y = b.y - a.y;
            const float z = b.z - a.z;
            return std::sqrt(x * x + y * y + z * z);
        }

        void HashU32(uint64_t& hash, uint32_t value)
        {
            for (uint32_t shift = 0u; shift < 32u; shift += 8u)
            {
                hash ^= static_cast<uint8_t>((value >> shift) & 0xffu);
                hash *= kFnvPrime;
            }
        }

        uint64_t ComputeTopologyHash(const HairAsset& asset, uint32_t groupsPerBlock)
        {
            uint64_t hash = kFnvOffset;
            HashU32(hash, static_cast<uint32_t>(asset.strands.size()));
            HashU32(hash, static_cast<uint32_t>(asset.restPoints.size()));
            HashU32(hash, groupsPerBlock);

            for (const StrandRange& strand : asset.strands)
            {
                HashU32(hash, strand.pointOffset);
                HashU32(hash, strand.pointCount);
                HashU32(hash, strand.groupId);
                HashU32(hash, strand.flags);
            }
            return hash;
        }

        void PushIssue(
            HairAssetValidationReport& report,
            ValidationSeverity severity,
            const std::string& message)
        {
            report.issues.push_back({ severity, message });
            switch (severity)
            {
            case ValidationSeverity::Info:
                ++report.infoCount;
                break;
            case ValidationSeverity::Warning:
                ++report.warningCount;
                break;
            case ValidationSeverity::Error:
                ++report.errorCount;
                report.valid = false;
                break;
            }
        }

        void PushIssue(
            SegmentGroupValidationReport& report,
            ValidationSeverity severity,
            const std::string& message)
        {
            report.issues.push_back({ severity, message });
            switch (severity)
            {
            case ValidationSeverity::Info:
                ++report.infoCount;
                break;
            case ValidationSeverity::Warning:
                ++report.warningCount;
                break;
            case ValidationSeverity::Error:
                ++report.errorCount;
                report.valid = false;
                break;
            }
        }

        const char* SeverityName(ValidationSeverity severity)
        {
            switch (severity)
            {
            case ValidationSeverity::Info: return "INFO";
            case ValidationSeverity::Warning: return "WARNING";
            case ValidationSeverity::Error: return "ERROR";
            default: return "UNKNOWN";
            }
        }
    }

    HairAssetValidationReport ValidateHairAsset(const HairAsset& asset)
    {
        HairAssetValidationReport report = {};
        report.valid = true;
        report.minSegmentLength = std::numeric_limits<float>::max();

        if (asset.strands.empty())
        {
            PushIssue(report, ValidationSeverity::Error, "Asset has no strands.");
        }
        if (asset.restPoints.empty())
        {
            PushIssue(report, ValidationSeverity::Error, "Asset has no point positions.");
        }
        if (!asset.pointWidths.empty() && asset.pointWidths.size() != asset.restPoints.size())
        {
            PushIssue(report, ValidationSeverity::Error, "pointWidths count does not match restPoints count.");
        }
        if (!asset.materials.empty() && asset.materials.size() != asset.strands.size())
        {
            PushIssue(report, ValidationSeverity::Warning, "materials count does not match strands count.");
        }
        if (!asset.rootBindings.empty() && asset.rootBindings.size() != asset.strands.size())
        {
            PushIssue(report, ValidationSeverity::Warning, "rootBindings count does not match strands count.");
        }
        if (!asset.followData.empty() && asset.followData.size() != asset.strands.size())
        {
            PushIssue(report, ValidationSeverity::Warning, "followData count does not match strands count.");
        }
        if (asset.guideStrandCount + asset.followerStrandCount != asset.strands.size())
        {
            PushIssue(report, ValidationSeverity::Warning, "guideStrandCount + followerStrandCount does not match strands count.");
        }

        for (size_t pointIndex = 0; pointIndex < asset.restPoints.size(); ++pointIndex)
        {
            if (!IsFinite(asset.restPoints[pointIndex]))
            {
                ++report.nonFinitePointCount;
            }
            if (pointIndex < asset.pointWidths.size() &&
                (!IsFinite(asset.pointWidths[pointIndex]) || asset.pointWidths[pointIndex] <= 0.0f))
            {
                ++report.nonPositiveWidthCount;
            }
        }

        uint64_t segmentCount = 0u;
        double totalSegmentLength = 0.0;
        uint32_t previousEnd = 0u;

        for (size_t strandIndex = 0; strandIndex < asset.strands.size(); ++strandIndex)
        {
            const StrandRange& strand = asset.strands[strandIndex];

            if (strand.pointCount == 0u)
            {
                std::ostringstream ss;
                ss << "Strand " << strandIndex << " has zero points.";
                PushIssue(report, ValidationSeverity::Error, ss.str());
                continue;
            }

            const uint64_t first = strand.pointOffset;
            const uint64_t end = first + strand.pointCount;
            if (end > asset.restPoints.size())
            {
                std::ostringstream ss;
                ss << "Strand " << strandIndex << " exceeds restPoints range.";
                PushIssue(report, ValidationSeverity::Error, ss.str());
                continue;
            }

            if (strandIndex > 0u && strand.pointOffset < previousEnd)
            {
                std::ostringstream ss;
                ss << "Strand " << strandIndex << " overlaps a previous point range.";
                PushIssue(report, ValidationSeverity::Error, ss.str());
            }
            previousEnd = std::max(previousEnd, static_cast<uint32_t>(end));

            if (strandIndex < asset.rootBindings.size() &&
                asset.rootBindings[strandIndex].triangleIndex >= asset.scalpMesh.triangles.size() &&
                !asset.scalpMesh.triangles.empty())
            {
                ++report.invalidRootBindingCount;
            }

            if (strandIndex < asset.followData.size())
            {
                const int32_t parent = asset.followData[strandIndex].parentGuide;
                if (parent >= 0 && static_cast<uint32_t>(parent) >= asset.guideStrandCount)
                {
                    ++report.invalidFollowReferenceCount;
                }
            }

            for (uint32_t local = 0u; local + 1u < strand.pointCount; ++local)
            {
                const uint32_t a = strand.pointOffset + local;
                const uint32_t b = a + 1u;
                const float length = Distance(asset.restPoints[a], asset.restPoints[b]);
                if (!IsFinite(length) || length <= kZeroLengthEpsilon)
                {
                    ++report.zeroLengthSegmentCount;
                    continue;
                }
                report.minSegmentLength = std::min(report.minSegmentLength, length);
                report.maxSegmentLength = std::max(report.maxSegmentLength, length);
                totalSegmentLength += length;
                ++segmentCount;
            }
        }

        if (segmentCount > 0u)
        {
            report.averageSegmentLength =
                static_cast<float>(totalSegmentLength / static_cast<double>(segmentCount));
        }
        else
        {
            report.minSegmentLength = 0.0f;
        }

        if (report.nonFinitePointCount > 0u)
        {
            PushIssue(report, ValidationSeverity::Error, "Asset contains non-finite point positions.");
        }
        if (report.nonPositiveWidthCount > 0u)
        {
            PushIssue(report, ValidationSeverity::Warning, "Asset contains non-positive or non-finite point widths.");
        }
        if (report.zeroLengthSegmentCount > 0u)
        {
            PushIssue(report, ValidationSeverity::Warning, "Asset contains zero-length or invalid segments.");
        }
        if (report.invalidRootBindingCount > 0u)
        {
            PushIssue(report, ValidationSeverity::Warning, "Asset contains invalid root bindings.");
        }
        if (report.invalidFollowReferenceCount > 0u)
        {
            PushIssue(report, ValidationSeverity::Error, "Asset contains invalid follower-to-guide references.");
        }

        return report;
    }

    std::string BuildHairAssetValidationSummary(
        const HairAssetValidationReport& report,
        bool includeIssueList,
        size_t maxIssues)
    {
        std::ostringstream ss;
        ss << "valid=" << (report.valid ? "true" : "false")
           << ", info=" << report.infoCount
           << ", warnings=" << report.warningCount
           << ", errors=" << report.errorCount
           << ", minSegmentLength=" << report.minSegmentLength
           << ", averageSegmentLength=" << report.averageSegmentLength
           << ", maxSegmentLength=" << report.maxSegmentLength
           << ", zeroLengthSegments=" << report.zeroLengthSegmentCount
           << ", nonFinitePoints=" << report.nonFinitePointCount
           << ", invalidRootBindings=" << report.invalidRootBindingCount
           << ", invalidFollowReferences=" << report.invalidFollowReferenceCount;

        if (includeIssueList)
        {
            const size_t count = std::min(maxIssues, report.issues.size());
            for (size_t i = 0; i < count; ++i)
            {
                ss << "\n[" << SeverityName(report.issues[i].severity) << "] "
                   << report.issues[i].message;
            }
        }
        return ss.str();
    }

    bool RebuildSegmentGroupTopology(
        HairAsset& asset,
        uint32_t groupsPerBlock,
        std::string* outError)
    {
        if (outError)
        {
            outError->clear();
        }
        groupsPerBlock = std::max(groupsPerBlock, 1u);

        SegmentGroupTopologyInfo topology = {};
        topology.groupsPerBlock = groupsPerBlock;
        topology.guideStrandCount = asset.guideStrandCount;
        topology.followerStrandCount = asset.followerStrandCount;
        topology.particlesPerStrand = asset.particlesPerStrand;
        topology.topologyVersion = std::max(asset.segmentGroupTopology.topologyVersion + 1u, 1u);
        topology.strandGroupRanges.reserve(asset.strands.size());

        uint64_t totalGroupCount64 = 0u;
        double totalLength = 0.0;
        uint64_t validLengthCount = 0u;
        float minLength = std::numeric_limits<float>::max();
        float maxLength = 0.0f;

        for (uint32_t strandIndex = 0u;
             strandIndex < static_cast<uint32_t>(asset.strands.size());
             ++strandIndex)
        {
            const StrandRange& strand = asset.strands[strandIndex];
            const uint64_t end = static_cast<uint64_t>(strand.pointOffset) + strand.pointCount;
            if (end > asset.restPoints.size())
            {
                if (outError)
                {
                    std::ostringstream ss;
                    ss << "Strand " << strandIndex << " exceeds restPoints.";
                    *outError = ss.str();
                }
                return false;
            }

            const uint32_t groupCount = strand.pointCount > 1u ? strand.pointCount - 1u : 0u;
            if (totalGroupCount64 + groupCount > std::numeric_limits<uint32_t>::max())
            {
                if (outError)
                {
                    *outError = "Segment-group count exceeds uint32_t capacity.";
                }
                return false;
            }

            SegmentGroupRange range = {};
            range.firstGroup = static_cast<uint32_t>(totalGroupCount64);
            range.groupCount = groupCount;
            topology.strandGroupRanges.push_back(range);

            uint32_t localFirst = 0u;
            uint32_t blockIndex = 0u;
            while (localFirst < groupCount)
            {
                SegmentGroupBlockRange block = {};
                block.firstGroup = range.firstGroup + localFirst;
                block.groupCount = std::min(groupsPerBlock, groupCount - localFirst);
                block.strandIndex = strandIndex;
                block.blockIndex = blockIndex++;
                topology.blocks.push_back(block);
                localFirst += block.groupCount;
            }

            for (uint32_t local = 0u; local < groupCount; ++local)
            {
                const uint32_t a = strand.pointOffset + local;
                const uint32_t b = a + 1u;
                const float length = Distance(asset.restPoints[a], asset.restPoints[b]);
                if (IsFinite(length) && length > kZeroLengthEpsilon)
                {
                    totalLength += length;
                    ++validLengthCount;
                    minLength = std::min(minLength, length);
                    maxLength = std::max(maxLength, length);
                }
            }
            totalGroupCount64 += groupCount;
        }

        topology.totalGroupCount = static_cast<uint32_t>(totalGroupCount64);
        topology.averageSegmentLength = validLengthCount > 0u
            ? static_cast<float>(totalLength / static_cast<double>(validLengthCount))
            : 0.0f;

        if (validLengthCount > 0u)
        {
            const float average = topology.averageSegmentLength;
            topology.maximumSegmentLengthDeviation =
                std::max(std::fabs(maxLength - average), std::fabs(average - minLength));
        }

        topology.topologyHash = ComputeTopologyHash(asset, groupsPerBlock);
        asset.segmentGroupTopology = std::move(topology);
        return true;
    }

    SegmentGroupValidationReport ValidateSegmentGroupTopology(
        const HairAsset& asset,
        uint32_t expectedParticlesPerStrand,
        uint32_t expectedTotalStrandCount)
    {
        SegmentGroupValidationReport report = {};
        report.valid = true;
        const SegmentGroupTopologyInfo& topology = asset.segmentGroupTopology;

        if (expectedTotalStrandCount > 0u &&
            asset.strands.size() != expectedTotalStrandCount)
        {
            std::ostringstream ss;
            ss << "Expected " << expectedTotalStrandCount
               << " strands, got " << asset.strands.size() << ".";
            PushIssue(report, ValidationSeverity::Error, ss.str());
        }

        uint64_t expectedGroups64 = 0u;
        uint64_t expectedBlocks64 = 0u;
        const uint32_t groupsPerBlock = std::max(topology.groupsPerBlock, 1u);

        for (uint32_t strandIndex = 0u;
             strandIndex < static_cast<uint32_t>(asset.strands.size());
             ++strandIndex)
        {
            const StrandRange& strand = asset.strands[strandIndex];

            if (expectedParticlesPerStrand > 0u &&
                strand.pointCount != expectedParticlesPerStrand)
            {
                ++report.nonUniformPointCountStrandCount;
            }

            const uint32_t groupCount = strand.pointCount > 1u ? strand.pointCount - 1u : 0u;
            expectedGroups64 += groupCount;
            expectedBlocks64 += (groupCount + groupsPerBlock - 1u) / groupsPerBlock;

            const uint64_t pointEnd =
                static_cast<uint64_t>(strand.pointOffset) + strand.pointCount;
            if (pointEnd > asset.restPoints.size())
            {
                ++report.invalidStrandRangeCount;
            }
            else
            {
                for (uint32_t local = 0u; local < groupCount; ++local)
                {
                    const Vec3& a = asset.restPoints[strand.pointOffset + local];
                    const Vec3& b = asset.restPoints[strand.pointOffset + local + 1u];
                    if (!IsFinite(a) || !IsFinite(b))
                    {
                        ++report.nonFinitePointCount;
                    }
                    if (Distance(a, b) <= kZeroLengthEpsilon)
                    {
                        ++report.zeroLengthSegmentCount;
                    }
                }
            }
        }

        if (expectedGroups64 > std::numeric_limits<uint32_t>::max() ||
            expectedBlocks64 > std::numeric_limits<uint32_t>::max())
        {
            PushIssue(report, ValidationSeverity::Error, "Expected topology exceeds uint32_t capacity.");
            return report;
        }

        report.expectedGroupCount = static_cast<uint32_t>(expectedGroups64);
        report.actualGroupCount = topology.totalGroupCount;
        report.expectedBlockCount = static_cast<uint32_t>(expectedBlocks64);
        report.actualBlockCount = static_cast<uint32_t>(topology.blocks.size());

        if (topology.strandGroupRanges.size() != asset.strands.size())
        {
            PushIssue(report, ValidationSeverity::Error, "strandGroupRanges count does not match strands count.");
        }

        uint32_t expectedFirstGroup = 0u;
        const size_t rangeCount = std::min(
            topology.strandGroupRanges.size(),
            asset.strands.size());

        for (size_t strandIndex = 0u; strandIndex < rangeCount; ++strandIndex)
        {
            const SegmentGroupRange& range = topology.strandGroupRanges[strandIndex];
            const uint32_t expectedCount =
                asset.strands[strandIndex].pointCount > 1u
                    ? asset.strands[strandIndex].pointCount - 1u
                    : 0u;

            if (range.firstGroup != expectedFirstGroup ||
                range.groupCount != expectedCount ||
                static_cast<uint64_t>(range.firstGroup) + range.groupCount > topology.totalGroupCount)
            {
                ++report.invalidStrandRangeCount;
            }
            expectedFirstGroup += expectedCount;
        }

        std::vector<uint8_t> coverage(topology.totalGroupCount, 0u);
        for (const SegmentGroupBlockRange& block : topology.blocks)
        {
            const uint64_t blockEnd =
                static_cast<uint64_t>(block.firstGroup) + block.groupCount;
            if (block.groupCount == 0u ||
                blockEnd > topology.totalGroupCount ||
                block.strandIndex >= asset.strands.size())
            {
                ++report.invalidBlockRangeCount;
                continue;
            }

            for (uint32_t group = block.firstGroup;
                 group < block.firstGroup + block.groupCount;
                 ++group)
            {
                if (coverage[group] != 0u)
                {
                    ++report.overlappingBlockCount;
                }
                coverage[group] = 1u;
            }
        }

        report.uncoveredGroupCount = static_cast<uint32_t>(
            std::count(coverage.begin(), coverage.end(), static_cast<uint8_t>(0u)));

        report.expectedTopologyHash =
            ComputeTopologyHash(asset, topology.groupsPerBlock);
        report.actualTopologyHash = topology.topologyHash;
        if (report.actualTopologyHash != report.expectedTopologyHash)
        {
            report.topologyHashMismatchCount = 1u;
        }

        if (report.actualGroupCount != report.expectedGroupCount)
        {
            PushIssue(report, ValidationSeverity::Error, "totalGroupCount does not match the strand-derived group count.");
        }
        if (report.actualBlockCount != report.expectedBlockCount)
        {
            PushIssue(report, ValidationSeverity::Error, "Block count does not match the expected count.");
        }
        if (report.invalidStrandRangeCount > 0u)
        {
            PushIssue(report, ValidationSeverity::Error, "Invalid strand-to-group ranges were found.");
        }
        if (report.invalidBlockRangeCount > 0u)
        {
            PushIssue(report, ValidationSeverity::Error, "Invalid segment-group block ranges were found.");
        }
        if (report.overlappingBlockCount > 0u)
        {
            PushIssue(report, ValidationSeverity::Error, "Overlapping segment-group blocks were found.");
        }
        if (report.uncoveredGroupCount > 0u)
        {
            PushIssue(report, ValidationSeverity::Error, "Some segment groups are not covered by a block.");
        }
        if (report.nonUniformPointCountStrandCount > 0u)
        {
            PushIssue(report, ValidationSeverity::Error, "The benchmark requires a uniform point count per strand.");
        }
        if (report.topologyHashMismatchCount > 0u)
        {
            PushIssue(report, ValidationSeverity::Error, "Topology hash is stale or invalid.");
        }
        if (report.nonFinitePointCount > 0u)
        {
            PushIssue(report, ValidationSeverity::Error, "Non-finite points were found while validating topology.");
        }
        if (report.zeroLengthSegmentCount > 0u)
        {
            PushIssue(report, ValidationSeverity::Warning, "Zero-length segments were found while validating topology.");
        }

        return report;
    }

    std::string BuildSegmentGroupValidationSummary(
        const SegmentGroupValidationReport& report,
        bool includeIssueList,
        size_t maxIssues)
    {
        std::ostringstream ss;
        ss << "valid=" << (report.valid ? "true" : "false")
           << ", groups=" << report.actualGroupCount
           << "/" << report.expectedGroupCount
           << ", blocks=" << report.actualBlockCount
           << "/" << report.expectedBlockCount
           << ", invalidStrandRanges=" << report.invalidStrandRangeCount
           << ", invalidBlockRanges=" << report.invalidBlockRangeCount
           << ", overlappingBlocks=" << report.overlappingBlockCount
           << ", uncoveredGroups=" << report.uncoveredGroupCount
           << ", nonUniformStrands=" << report.nonUniformPointCountStrandCount
           << ", topologyHashMismatch=" << report.topologyHashMismatchCount
           << ", zeroLengthSegments=" << report.zeroLengthSegmentCount
           << ", nonFinitePoints=" << report.nonFinitePointCount
           << ", topologyHash=0x" << std::hex << report.actualTopologyHash;

        if (includeIssueList)
        {
            const size_t count = std::min(maxIssues, report.issues.size());
            for (size_t i = 0; i < count; ++i)
            {
                ss << "\n[" << SeverityName(report.issues[i].severity) << "] "
                   << report.issues[i].message;
            }
        }
        return ss.str();
    }

    HairAsset BuildBenchmarkHairAsset(
        const HairBenchmarkAssetSettings& settings,
        SegmentGroupValidationReport* outTopologyReport,
        std::string* outError)
    {
        if (outError)
        {
            outError->clear();
        }

        if (settings.totalStrandCount == 0u ||
            settings.particlesPerStrand < 2u ||
            settings.guideStrandCount == 0u ||
            settings.segmentLength <= 0.0f)
        {
            if (outError)
            {
                *outError = "Invalid benchmark asset settings.";
            }
            return {};
        }

        const uint64_t calculatedTotal =
            static_cast<uint64_t>(settings.guideStrandCount) *
            static_cast<uint64_t>(settings.followersPerGuide + 1u);

        if (settings.requireExactStrandCount &&
            calculatedTotal != settings.totalStrandCount)
        {
            if (outError)
            {
                std::ostringstream ss;
                ss << "guideStrandCount * (followersPerGuide + 1) must equal totalStrandCount ("
                   << calculatedTotal << " vs " << settings.totalStrandCount << ").";
                *outError = ss.str();
            }
            return {};
        }

        HairAsset asset = BuildSampleHybridHairAsset(
            settings.guideStrandCount,
            settings.followersPerGuide,
            settings.particlesPerStrand,
            settings.segmentLength);

        if (settings.requireExactStrandCount &&
            asset.strands.size() != settings.totalStrandCount)
        {
            if (outError)
            {
                std::ostringstream ss;
                ss << "Benchmark builder returned " << asset.strands.size()
                   << " strands instead of " << settings.totalStrandCount << ".";
                *outError = ss.str();
            }
            return {};
        }

        std::string topologyError;
        if (!RebuildSegmentGroupTopology(asset, settings.groupsPerBlock, &topologyError))
        {
            if (outError)
            {
                *outError = topologyError;
            }
            return {};
        }

        SegmentGroupValidationReport report = ValidateSegmentGroupTopology(
            asset,
            settings.requireUniformPointCount ? settings.particlesPerStrand : 0u,
            settings.requireExactStrandCount ? settings.totalStrandCount : 0u);

        if (outTopologyReport)
        {
            *outTopologyReport = report;
        }
        if (!report.valid && outError)
        {
            *outError = BuildSegmentGroupValidationSummary(report, true);
        }
        return asset;
    }
}
