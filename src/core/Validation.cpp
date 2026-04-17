#include "hairgpu/Validation.h"

#include <cmath>
#include <limits>
#include <sstream>

namespace hairgpu
{
    namespace
    {
        bool IsFinite(float v)
        {
            return std::isfinite(v) != 0;
        }

        bool IsFinite(const Vec3& v)
        {
            return IsFinite(v.x) && IsFinite(v.y) && IsFinite(v.z);
        }

        void PushIssue(HairAssetValidationReport& report, ValidationSeverity severity, const std::string& message)
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
        if (asset.pointWidths.size() != asset.restPoints.size())
        {
            std::ostringstream ss;
            ss << "pointWidths count (" << asset.pointWidths.size() << ") does not match restPoints count (" << asset.restPoints.size() << ").";
            PushIssue(report, ValidationSeverity::Error, ss.str());
        }
        if (!asset.materials.empty() && asset.materials.size() != asset.strands.size())
        {
            std::ostringstream ss;
            ss << "materials count (" << asset.materials.size() << ") does not match strands count (" << asset.strands.size() << ").";
            PushIssue(report, ValidationSeverity::Warning, ss.str());
        }
        if (!asset.rootBindings.empty() && asset.rootBindings.size() != asset.strands.size())
        {
            std::ostringstream ss;
            ss << "rootBindings count (" << asset.rootBindings.size() << ") does not match strands count (" << asset.strands.size() << ").";
            PushIssue(report, ValidationSeverity::Warning, ss.str());
        }
        if (!asset.followData.empty() && asset.followData.size() != asset.strands.size())
        {
            std::ostringstream ss;
            ss << "followData count (" << asset.followData.size() << ") does not match strands count (" << asset.strands.size() << ").";
            PushIssue(report, ValidationSeverity::Warning, ss.str());
        }
        if (asset.guideStrandCount + asset.followerStrandCount != asset.strands.size())
        {
            std::ostringstream ss;
            ss << "guideStrandCount + followerStrandCount != total strands ("
               << asset.guideStrandCount << " + " << asset.followerStrandCount << " vs " << asset.strands.size() << ").";
            PushIssue(report, ValidationSeverity::Warning, ss.str());
        }
        if (asset.particlesPerStrand == 0)
        {
            PushIssue(report, ValidationSeverity::Warning, "particlesPerStrand is zero.");
        }

        uint32_t segmentCount = 0;
        float totalSegmentLength = 0.0f;
        uint32_t previousMaxPointEnd = 0;
        for (size_t strandIndex = 0; strandIndex < asset.strands.size(); ++strandIndex)
        {
            const StrandRange& strand = asset.strands[strandIndex];
            if (strand.pointCount == 0)
            {
                std::ostringstream ss;
                ss << "Strand " << strandIndex << " has zero points.";
                PushIssue(report, ValidationSeverity::Error, ss.str());
                continue;
            }
            if (strand.pointOffset >= asset.restPoints.size() || strand.pointOffset + strand.pointCount > asset.restPoints.size())
            {
                std::ostringstream ss;
                ss << "Strand " << strandIndex << " point range is out of bounds.";
                PushIssue(report, ValidationSeverity::Error, ss.str());
                continue;
            }
            if (strandIndex > 0 && strand.pointOffset < previousMaxPointEnd)
            {
                std::ostringstream ss;
                ss << "Strand " << strandIndex << " overlaps a previous strand point range.";
                PushIssue(report, ValidationSeverity::Warning, ss.str());
            }
            previousMaxPointEnd = std::max<uint32_t>(previousMaxPointEnd, strand.pointOffset + strand.pointCount);
            if (asset.particlesPerStrand != 0 && strand.pointCount != asset.particlesPerStrand)
            {
                std::ostringstream ss;
                ss << "Strand " << strandIndex << " pointCount (" << strand.pointCount
                   << ") differs from particlesPerStrand (" << asset.particlesPerStrand << ").";
                PushIssue(report, ValidationSeverity::Info, ss.str());
            }

            for (uint32_t localPointIndex = 0; localPointIndex < strand.pointCount; ++localPointIndex)
            {
                const uint32_t pointIndex = strand.pointOffset + localPointIndex;
                const Vec3& p = asset.restPoints[pointIndex];
                if (!IsFinite(p))
                {
                    ++report.nonFinitePointCount;
                }
                if (pointIndex < asset.pointWidths.size() && (!IsFinite(asset.pointWidths[pointIndex]) || asset.pointWidths[pointIndex] <= 0.0f))
                {
                    ++report.nonPositiveWidthCount;
                }
                if (localPointIndex > 0)
                {
                    const Vec3& prev = asset.restPoints[pointIndex - 1u];
                    const float segLength = Length(p - prev);
                    report.minSegmentLength = std::min(report.minSegmentLength, segLength);
                    report.maxSegmentLength = std::max(report.maxSegmentLength, segLength);
                    totalSegmentLength += segLength;
                    ++segmentCount;
                    if (segLength <= 1.0e-6f)
                    {
                        ++report.zeroLengthSegmentCount;
                    }
                }
            }

            if (strandIndex < asset.followData.size() && strandIndex >= asset.guideStrandCount)
            {
                const FollowData& follow = asset.followData[strandIndex];
                if (follow.parentGuide < 0 || static_cast<uint32_t>(follow.parentGuide) >= asset.guideStrandCount)
                {
                    ++report.invalidFollowReferenceCount;
                }
            }

            if (strandIndex < asset.rootBindings.size())
            {
                const RootBinding& binding = asset.rootBindings[strandIndex];
                if (!asset.scalpMesh.triangles.empty())
                {
                    if (binding.triangleIndex >= asset.scalpMesh.triangles.size())
                    {
                        ++report.invalidRootBindingCount;
                    }
                }
                if (!IsFinite(binding.barycentric.x) || !IsFinite(binding.barycentric.y) || !IsFinite(binding.barycentric.z))
                {
                    ++report.invalidRootBindingCount;
                }
            }
        }

        if (segmentCount > 0)
        {
            report.averageSegmentLength = totalSegmentLength / static_cast<float>(segmentCount);
        }
        else
        {
            report.minSegmentLength = 0.0f;
        }

        if (report.nonFinitePointCount > 0)
        {
            std::ostringstream ss;
            ss << report.nonFinitePointCount << " non-finite point positions found.";
            PushIssue(report, ValidationSeverity::Error, ss.str());
        }
        if (report.nonPositiveWidthCount > 0)
        {
            std::ostringstream ss;
            ss << report.nonPositiveWidthCount << " widths are non-finite or <= 0.";
            PushIssue(report, ValidationSeverity::Warning, ss.str());
        }
        if (report.zeroLengthSegmentCount > 0)
        {
            std::ostringstream ss;
            ss << report.zeroLengthSegmentCount << " zero-length segments detected.";
            PushIssue(report, ValidationSeverity::Warning, ss.str());
        }
        if (report.invalidRootBindingCount > 0)
        {
            std::ostringstream ss;
            ss << report.invalidRootBindingCount << " invalid root bindings detected.";
            PushIssue(report, ValidationSeverity::Warning, ss.str());
        }
        if (report.invalidFollowReferenceCount > 0)
        {
            std::ostringstream ss;
            ss << report.invalidFollowReferenceCount << " follower strands reference invalid guides.";
            PushIssue(report, ValidationSeverity::Warning, ss.str());
        }

        if (!asset.scalpMesh.positions.empty())
        {
            if (!asset.scalpMesh.normals.empty() && asset.scalpMesh.normals.size() != asset.scalpMesh.positions.size())
            {
                PushIssue(report, ValidationSeverity::Warning, "Scalp normal count does not match scalp vertex count.");
            }
            if (!asset.scalpMesh.uvs.empty() && asset.scalpMesh.uvs.size() != asset.scalpMesh.positions.size())
            {
                PushIssue(report, ValidationSeverity::Warning, "Scalp UV count does not match scalp vertex count.");
            }
        }

        if (report.valid && report.warningCount == 0 && report.infoCount == 0)
        {
            PushIssue(report, ValidationSeverity::Info, "Asset validation passed without issues.");
        }

        return report;
    }

    std::string BuildHairAssetValidationSummary(const HairAssetValidationReport& report, bool includeIssueList, size_t maxIssues)
    {
        std::ostringstream ss;
        ss << (report.valid ? "valid" : "invalid")
           << " | errors=" << report.errorCount
           << " warnings=" << report.warningCount
           << " info=" << report.infoCount
           << " | avgSeg=" << report.averageSegmentLength
           << " maxSeg=" << report.maxSegmentLength;
        if (includeIssueList && !report.issues.empty())
        {
            const size_t issueCount = std::min(maxIssues, report.issues.size());
            for (size_t i = 0; i < issueCount; ++i)
            {
                const char* severity = (report.issues[i].severity == ValidationSeverity::Error)
                    ? "error"
                    : ((report.issues[i].severity == ValidationSeverity::Warning) ? "warning" : "info");
                ss << "\n- [" << severity << "] " << report.issues[i].message;
            }
            if (report.issues.size() > issueCount)
            {
                ss << "\n- ... " << (report.issues.size() - issueCount) << " more issue(s)";
            }
        }
        return ss.str();
    }
}
