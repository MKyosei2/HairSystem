from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TOUCHED: list[str] = []


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def write(path: str, text: str) -> None:
    (ROOT / path).write_text(text, encoding="utf-8", newline="\n")
    TOUCHED.append(path)


def replace_once(path: str, old: str, new: str) -> None:
    text = read(path)
    if old not in text:
        raise RuntimeError(f"anchor not found in {path}: {old[:160]!r}")
    write(path, text.replace(old, new, 1))


# -----------------------------------------------------------------------------
# 1) Public validation contract for the deterministic benchmark wig.
# -----------------------------------------------------------------------------
path = "include/hairgpu/Validation.h"
text = read(path)
report_block = r'''
    struct BenchmarkWigValidationReport
    {
        bool valid = true;
        uint32_t errorCount = 0u;
        uint32_t warningCount = 0u;
        uint32_t evaluatedGuideCount = 0u;
        uint32_t frontGuideCount = 0u;
        uint32_t backGuideCount = 0u;
        float minimumGuideLength = 0.0f;
        float averageGuideLength = 0.0f;
        float maximumGuideLength = 0.0f;
        float frontAverageGuideLength = 0.0f;
        float backAverageGuideLength = 0.0f;
        float tipHeightStandardDeviation = 0.0f;
        float maximumWithinStrandSegmentRatio = 1.0f;
        float boundsWidth = 0.0f;
        float boundsHeight = 0.0f;
        float boundsDepth = 0.0f;
        std::vector<ValidationIssue> issues;
    };

'''
if "struct BenchmarkWigValidationReport" not in text:
    anchor = "    // Deterministic benchmark construction. This remains outside novelty 01.\n"
    if anchor not in text:
        raise RuntimeError("Validation.h report anchor missing")
    text = text.replace(anchor, report_block + anchor, 1)

declaration_block = r'''
    BenchmarkWigValidationReport ValidateBenchmarkWigShape(const HairAsset& asset);
    std::string BuildBenchmarkWigValidationSummary(
        const BenchmarkWigValidationReport& report,
        bool includeIssueList = false,
        size_t maxIssues = 12u);

'''
if "ValidateBenchmarkWigShape" not in text:
    anchor = "    bool RebuildSegmentGroupTopology(\n"
    if anchor not in text:
        raise RuntimeError("Validation.h declaration anchor missing")
    text = text.replace(anchor, declaration_block + anchor, 1)
write(path, text)


# -----------------------------------------------------------------------------
# 2) Shape validator: structural validity, front/back length separation,
#    non-flat tips, sane bounds, and uniform per-strand resampling.
# -----------------------------------------------------------------------------
path = "src/hairgpu_core/Validation.cpp"
text = read(path)
validator = r'''
    BenchmarkWigValidationReport ValidateBenchmarkWigShape(const HairAsset& asset)
    {
        BenchmarkWigValidationReport report = {};
        auto pushIssue = [&](ValidationSeverity severity, const std::string& message)
        {
            report.issues.push_back({ severity, message });
            if (severity == ValidationSeverity::Error)
            {
                ++report.errorCount;
                report.valid = false;
            }
            else if (severity == ValidationSeverity::Warning)
            {
                ++report.warningCount;
            }
        };

        const HairAssetValidationReport structural = ValidateHairAsset(asset);
        if (!structural.valid)
        {
            pushIssue(ValidationSeverity::Error,
                "The benchmark wig failed the structural HairAsset validation.");
        }
        if (asset.guideStrandCount == 0u || asset.strands.size() < asset.guideStrandCount)
        {
            pushIssue(ValidationSeverity::Error,
                "The benchmark wig does not contain the declared guide strands.");
            return report;
        }
        if (asset.rootBindings.size() < asset.guideStrandCount)
        {
            pushIssue(ValidationSeverity::Error,
                "The benchmark wig is missing guide root bindings.");
            return report;
        }

        constexpr float kTau = 6.28318530717958647692f;
        report.minimumGuideLength = std::numeric_limits<float>::max();
        Vec3 boundsMin(
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max());
        Vec3 boundsMax(
            -std::numeric_limits<float>::max(),
            -std::numeric_limits<float>::max(),
            -std::numeric_limits<float>::max());

        double guideLengthSum = 0.0;
        double frontLengthSum = 0.0;
        double backLengthSum = 0.0;
        std::vector<float> tipHeights;
        tipHeights.reserve(asset.guideStrandCount);

        for (uint32_t guideIndex = 0u; guideIndex < asset.guideStrandCount; ++guideIndex)
        {
            const StrandRange& strand = asset.strands[guideIndex];
            const uint64_t strandEnd = static_cast<uint64_t>(strand.pointOffset) + strand.pointCount;
            if (strand.pointCount < 2u || strandEnd > asset.restPoints.size())
            {
                pushIssue(ValidationSeverity::Error,
                    "A guide strand has an invalid point range.");
                continue;
            }

            float guideLength = 0.0f;
            float minimumSegment = std::numeric_limits<float>::max();
            float maximumSegment = 0.0f;
            for (uint32_t localIndex = 0u; localIndex < strand.pointCount; ++localIndex)
            {
                const Vec3& point = asset.restPoints[strand.pointOffset + localIndex];
                boundsMin.x = std::min(boundsMin.x, point.x);
                boundsMin.y = std::min(boundsMin.y, point.y);
                boundsMin.z = std::min(boundsMin.z, point.z);
                boundsMax.x = std::max(boundsMax.x, point.x);
                boundsMax.y = std::max(boundsMax.y, point.y);
                boundsMax.z = std::max(boundsMax.z, point.z);

                if (localIndex > 0u)
                {
                    const Vec3& previous = asset.restPoints[strand.pointOffset + localIndex - 1u];
                    const float segment = Distance(previous, point);
                    guideLength += segment;
                    minimumSegment = std::min(minimumSegment, segment);
                    maximumSegment = std::max(maximumSegment, segment);
                }
            }

            if (!std::isfinite(guideLength) || guideLength <= 0.0f)
            {
                pushIssue(ValidationSeverity::Error,
                    "A guide strand has a non-finite or non-positive length.");
                continue;
            }

            report.minimumGuideLength = std::min(report.minimumGuideLength, guideLength);
            report.maximumGuideLength = std::max(report.maximumGuideLength, guideLength);
            guideLengthSum += guideLength;
            ++report.evaluatedGuideCount;
            tipHeights.push_back(asset.restPoints[strand.pointOffset + strand.pointCount - 1u].y);

            if (minimumSegment > 1.0e-7f)
            {
                report.maximumWithinStrandSegmentRatio = std::max(
                    report.maximumWithinStrandSegmentRatio,
                    maximumSegment / minimumSegment);
            }

            const float angle = asset.rootBindings[guideIndex].rootUv.x * kTau;
            const float front = std::sin(angle);
            if (front >= 0.60f)
            {
                frontLengthSum += guideLength;
                ++report.frontGuideCount;
            }
            else if (front <= -0.60f)
            {
                backLengthSum += guideLength;
                ++report.backGuideCount;
            }
        }

        if (report.evaluatedGuideCount == 0u)
        {
            pushIssue(ValidationSeverity::Error,
                "No guide strand could be evaluated.");
            report.minimumGuideLength = 0.0f;
            return report;
        }

        report.averageGuideLength = static_cast<float>(
            guideLengthSum / static_cast<double>(report.evaluatedGuideCount));
        report.frontAverageGuideLength = report.frontGuideCount > 0u
            ? static_cast<float>(frontLengthSum / static_cast<double>(report.frontGuideCount))
            : 0.0f;
        report.backAverageGuideLength = report.backGuideCount > 0u
            ? static_cast<float>(backLengthSum / static_cast<double>(report.backGuideCount))
            : 0.0f;

        const double tipMean = std::accumulate(
            tipHeights.begin(), tipHeights.end(), 0.0) /
            static_cast<double>(std::max<size_t>(tipHeights.size(), 1u));
        double tipVariance = 0.0;
        for (float tipHeight : tipHeights)
        {
            const double delta = static_cast<double>(tipHeight) - tipMean;
            tipVariance += delta * delta;
        }
        tipVariance /= static_cast<double>(std::max<size_t>(tipHeights.size(), 1u));
        report.tipHeightStandardDeviation = static_cast<float>(std::sqrt(tipVariance));

        report.boundsWidth = boundsMax.x - boundsMin.x;
        report.boundsHeight = boundsMax.y - boundsMin.y;
        report.boundsDepth = boundsMax.z - boundsMin.z;

        const uint32_t minimumRegionalCount = std::max<uint32_t>(4u, asset.guideStrandCount / 20u);
        if (report.frontGuideCount < minimumRegionalCount || report.backGuideCount < minimumRegionalCount)
        {
            pushIssue(ValidationSeverity::Error,
                "Front/back guide coverage is insufficient for an orientation-readable benchmark wig.");
        }
        if (report.minimumGuideLength < 0.16f || report.maximumGuideLength > 0.72f ||
            report.averageGuideLength < 0.28f || report.averageGuideLength > 0.58f)
        {
            pushIssue(ValidationSeverity::Error,
                "Guide lengths are outside the stable benchmark range.");
        }
        if (report.frontAverageGuideLength <= 0.0f || report.backAverageGuideLength <= 0.0f ||
            report.frontAverageGuideLength >= report.backAverageGuideLength * 0.90f)
        {
            pushIssue(ValidationSeverity::Error,
                "Front hair is not sufficiently shorter than back hair.");
        }
        if (report.tipHeightStandardDeviation < 0.025f)
        {
            pushIssue(ValidationSeverity::Error,
                "Guide tips are too uniformly aligned and would form a blunt lower ring.");
        }
        if (report.maximumWithinStrandSegmentRatio > 1.10f)
        {
            pushIssue(ValidationSeverity::Error,
                "A guide strand is not uniformly arc-length resampled.");
        }
        if (report.boundsWidth < 0.34f || report.boundsDepth < 0.28f ||
            report.boundsHeight < 0.30f || report.boundsHeight > 0.90f)
        {
            pushIssue(ValidationSeverity::Error,
                "Benchmark wig bounds are too narrow, flat, short, or excessively long.");
        }

        return report;
    }

    std::string BuildBenchmarkWigValidationSummary(
        const BenchmarkWigValidationReport& report,
        bool includeIssueList,
        size_t maxIssues)
    {
        std::ostringstream ss;
        ss << "valid=" << (report.valid ? "true" : "false")
           << ", errors=" << report.errorCount
           << ", warnings=" << report.warningCount
           << ", guides=" << report.evaluatedGuideCount
           << ", minGuideLength=" << report.minimumGuideLength
           << ", averageGuideLength=" << report.averageGuideLength
           << ", maxGuideLength=" << report.maximumGuideLength
           << ", frontAverageLength=" << report.frontAverageGuideLength
           << ", backAverageLength=" << report.backAverageGuideLength
           << ", tipHeightStdDev=" << report.tipHeightStandardDeviation
           << ", maxSegmentRatio=" << report.maximumWithinStrandSegmentRatio
           << ", bounds=" << report.boundsWidth << "x"
           << report.boundsHeight << "x" << report.boundsDepth;

        if (includeIssueList)
        {
            const size_t count = std::min(maxIssues, report.issues.size());
            for (size_t index = 0u; index < count; ++index)
            {
                ss << "\n[" << SeverityName(report.issues[index].severity) << "] "
                   << report.issues[index].message;
            }
        }
        return ss.str();
    }

'''
if "BenchmarkWigValidationReport ValidateBenchmarkWigShape" not in text:
    anchor = "    bool RebuildSegmentGroupTopology(\n"
    if anchor not in text:
        raise RuntimeError("Validation.cpp validator anchor missing")
    text = text.replace(anchor, validator + anchor, 1)
write(path, text)


# -----------------------------------------------------------------------------
# 3) Deterministic benchmark-wig authoring.
#    - Front is visibly shorter than back.
#    - Guide tips do not share one height.
#    - Mid-length silhouette has controlled volume.
#    - Random short support followers and the second strong cap styling pass are removed.
# -----------------------------------------------------------------------------
path = "src/hairgpu_core/SampleAsset.cpp"
text = read(path)
text = text.replace(
    "        constexpr float kTargetAverageStrandLengthMeters = 0.50f;\n",
    "        constexpr float kTargetAverageStrandLengthMeters = 0.44f;\n",
    1,
)
old = r'''        float ComputeGuideLength(const Vec3& root, const Vec3& scalpCenter, const Vec2& uv, float baseLength, const WigParameters& parameters)
        {
            const float lowerRing = Clamp((1.0f - Clamp((root.y - (scalpCenter.y - 0.02f)) / 0.26f, 0.0f, 1.0f)), 0.0f, 1.0f);
            const float crownReduction = Clamp((uv.y - 0.40f) / 0.60f, 0.0f, 1.0f) * 0.025f;
            const float faceOpening = EvaluateFaceOpeningMask(uv.x, uv.y);
            const float frontTrim = 0.03f * faceOpening;
            return baseLength * parameters.overallLength * (1.0f + parameters.lowerRingLengthBias * lowerRing - crownReduction - frontTrim);
        }
'''
new = r'''        float ComputeGuideLength(const Vec3& root, const Vec3& scalpCenter, const Vec2& uv, float baseLength, const WigParameters& parameters)
        {
            const float angle = WrapUnit(uv.x) * kTau;
            const float front = std::max(std::sin(angle), 0.0f);
            const float back = std::max(-std::sin(angle), 0.0f);
            const float side = std::fabs(std::cos(angle));
            const float lowerRing = Clamp(
                1.0f - Clamp((root.y - (scalpCenter.y - 0.02f)) / 0.26f, 0.0f, 1.0f),
                0.0f,
                1.0f);
            const float faceOpening = EvaluateFaceOpeningMask(uv.x, uv.y);

            // Orientation is encoded by the silhouette itself: fringe/front guides
            // are shorter, side guides are medium, and back/nape guides are longer.
            const float frontTrim = front * Lerp(0.24f, 0.38f, faceOpening);
            const float backExtension = back * 0.18f;
            const float sideExtension = side * 0.035f;
            const float lowerExtension = parameters.lowerRingLengthBias * lowerRing;
            const float regionalScale = Clamp(
                0.94f + backExtension + sideExtension + lowerExtension - frontTrim,
                0.56f,
                1.26f);
            return baseLength * parameters.overallLength * regionalScale;
        }
'''
if old not in text:
    raise RuntimeError("ComputeGuideLength anchor missing")
text = text.replace(old, new, 1)

old = r'''            const bool isGuideStrand = (flags & static_cast<uint32_t>(StrandFlags::Guide)) != 0u;
            const float crownSupportZone = crownMask * (1.0f - faceOpening * 0.45f) * (0.78f + 0.22f * (1.0f - sideFactor));
            const float supportSeed = Hash01(seed + 131u);
            const float supportMix = (!isGuideStrand && supportSeed < (0.12f + 0.30f * crownSupportZone))
                ? (0.58f + 0.22f * Hash01(seed + 151u))
                : 0.0f;
'''
new = r'''            const bool isGuideStrand = (flags & static_cast<uint32_t>(StrandFlags::Guide)) != 0u;
            (void)isGuideStrand;
            // The formal benchmark uses one coherent length distribution. Randomly
            // inserting short support followers created the visible scalp halo and
            // made quality comparisons depend on authoring noise.
            const float supportMix = 0.0f;
'''
if old not in text:
    raise RuntimeError("supportMix anchor missing")
text = text.replace(old, new, 1)

text = text.replace(
    "            Vec3 root = rootSurface + normal * 0.0045f + tangentOffset * 0.30f;\n",
    "            Vec3 root = rootSurface + normal * (0.0035f + parameters.rootLift * 0.15f) + tangentOffset * 0.72f;\n",
    1,
)
text = text.replace(
    "            const float lengthJitter = 0.97f + 0.06f * Hash01(seed + 41u);\n",
    "            const float lengthJitter = 0.93f + 0.14f * Hash01(seed + 41u);\n",
    1,
)
text = text.replace(
    "            const float lengthScale = Clamp(0.96f + 0.05f * sideFactor + 0.08f * backFactor - 0.04f * fringeFactor + 0.03f * crownMask, 0.90f, 1.12f);\n",
    "            const float lengthScale = Clamp(0.98f + 0.025f * sideFactor + 0.035f * backFactor - 0.020f * fringeFactor + 0.015f * crownMask, 0.94f, 1.07f);\n",
    1,
)

old = r'''                p += normal * (0.0050f * (1.0f - rootPhase) + 0.015f * supportMix * (1.0f - fallPhase));

                if (t > 0.35f)
'''
new = r'''                p += normal * (0.0040f * (1.0f - rootPhase));

                const float silhouetteBell = std::sin(kPi * Clamp(t, 0.0f, 1.0f));
                const float radialVolume =
                    parameters.silhouetteWidth * (0.30f + 0.45f * sideFactor) +
                    parameters.sideVolume * sideFactor * 0.55f +
                    parameters.backVolume * backFactor * 0.70f;
                const float faceVolumeAttenuation = 1.0f - 0.72f * frontFactor;
                p += radial * (radialVolume * silhouetteBell * faceVolumeAttenuation);
                p += lateral * (parameters.silhouetteWidth * 0.28f * frontFactor * silhouetteBell);
                p -= radial * (parameters.tipInward * t * t * t);

                if (t > 0.35f)
'''
if old not in text:
    raise RuntimeError("curve-volume anchor missing")
text = text.replace(old, new, 1)

old = r'''        WigParameters parameters;
        parameters.overallLength = 1.00f;
        parameters.partingOffset = 0.0f;
        parameters.partWidth = 0.010f;
        parameters.scalpConform = 1.0f;
        parameters.rootLift = 0.0f;
        parameters.silhouetteWidth = 0.0025f;
        parameters.sideVolume = 0.0008f;
        parameters.backVolume = 0.0010f;
        parameters.lowerRingLengthBias = 0.06f;
        parameters.strandNoise = 0.00025f;
        parameters.followerSpread = 0.00042f;
        parameters.tipInward = 0.0002f;
        return parameters;
'''
new = r'''        WigParameters parameters;
        parameters.overallLength = 0.96f;
        parameters.partingOffset = 0.0f;
        parameters.partWidth = 0.026f;
        parameters.scalpConform = 1.0f;
        parameters.rootLift = 0.006f;
        parameters.silhouetteWidth = 0.050f;
        parameters.sideVolume = 0.032f;
        parameters.backVolume = 0.048f;
        parameters.lowerRingLengthBias = 0.14f;
        parameters.strandNoise = 0.0022f;
        parameters.followerSpread = 0.0060f;
        parameters.tipInward = 0.009f;
        return parameters;
'''
if old not in text:
    raise RuntimeError("DefaultWigParameters anchor missing")
text = text.replace(old, new, 1)

text = text.replace("            material.rootWidth = 0.0032f;\n", "            material.rootWidth = 0.00095f;\n", 1)
text = text.replace("            material.tipWidth = 0.00016f;\n", "            material.tipWidth = 0.000065f;\n", 1)
text = text.replace(
    "                followerFollow.followWeight = 0.948f + Hash01(seed + 7u) * 0.010f - edgeRisk * 0.028f;\n",
    "                followerFollow.followWeight = 0.988f + Hash01(seed + 7u) * 0.006f - edgeRisk * 0.010f;\n",
    1,
)
text = text.replace(
    "                followerFollow.clumpWeight = 0.76f + Hash01(seed + 19u) * 0.050f - edgeRisk * 0.12f;\n",
    "                followerFollow.clumpWeight = 0.925f + Hash01(seed + 19u) * 0.035f - edgeRisk * 0.025f;\n",
    1,
)
text = text.replace(
    "                followerFollow.noiseWeight = 0.00020f + Hash01(seed + 23u) * 0.00020f + edgeRisk * 0.00008f;\n",
    "                followerFollow.noiseWeight = 0.00008f + Hash01(seed + 23u) * 0.00008f;\n",
    1,
)

old = r'''        RebuildRootRestPositionsFromScalp(asset);

        WigStyleSettings wigSettings;
        wigSettings.strength = 0.88f;
        wigSettings.capRegion = 0.16f;
        wigSettings.shellOffset = 0.0045f;
        wigSettings.flowBlend = 0.18f;
        wigSettings.outwardBlend = 0.02f;
        wigSettings.restoreOriginalTowardsTip = 0.88f;
        StyleAssetToScalpWig(asset, wigSettings);
        return asset;
'''
new = r'''        RebuildRootRestPositionsFromScalp(asset);
        // AppendStaticWigStrand already creates the benchmark silhouette. A second
        // strong normal-directed cap styling pass made roots stand away from the
        // scalp and created the crown halo seen in the Viewer.
        return asset;
'''
if old not in text:
    raise RuntimeError("double wig styling anchor missing")
text = text.replace(old, new, 1)
write(path, text)


# -----------------------------------------------------------------------------
# 4) Make the validation API available to the Viewer aggregation unit.
# -----------------------------------------------------------------------------
path = "apps/dx11_viewer/src/GpuHairViewer.cpp"
text = read(path)
if '#include "hairgpu/Validation.h"' not in text:
    anchor = '#include "hairgpu/Wig.h"\n'
    if anchor not in text:
        raise RuntimeError("GpuHairViewer.cpp include anchor missing")
    text = text.replace(anchor, anchor + '#include "hairgpu/Validation.h"\n', 1)
write(path, text)


# -----------------------------------------------------------------------------
# 5) Stable benchmark preset and fail-fast shape validation before GPU upload.
# -----------------------------------------------------------------------------
path = "apps/dx11_viewer/src/GpuHairViewer_parts/08_ui_state_sync_and_controls.inl"
text = read(path)
text = text.replace('            m_activePresetName = L"Rapunzel Preview";\n', '            m_activePresetName = L"Stable Benchmark Wig";\n', 1)
text = text.replace('            m_widthScale = 0.56f;\n', '            m_widthScale = 0.82f;\n', 1)
text = text.replace('            m_simConfig.gravity = 18.5f;\n', '            m_simConfig.gravity = 9.81f;\n', 1)
text = text.replace('            m_simConfig.globalDamping = 0.085f;\n', '            m_simConfig.globalDamping = 0.065f;\n', 1)
text = text.replace('            m_simConfig.windStrength = 0.03f;\n', '            m_simConfig.windStrength = 0.0f;\n', 1)
text = text.replace('            m_simConfig.followStiffness = 0.992f;\n', '            m_simConfig.followStiffness = 0.995f;\n', 1)
text = text.replace('            m_selfCollisionRadius = 0.0055f;\n', '            m_selfCollisionRadius = 0.0045f;\n', 1)
text = text.replace('            m_selfCollisionStiffness = 0.10f;\n', '            m_selfCollisionStiffness = 0.08f;\n', 1)
text = text.replace('            m_simConfig.selfCollisionCellSize = 0.060f;\n', '            m_simConfig.selfCollisionCellSize = 0.040f;\n', 1)
text = text.replace('            m_simConfig.stretchCompliance = 2.0e-7f;\n', '            m_simConfig.stretchCompliance = 5.0e-8f;\n', 1)
text = text.replace('            m_simConfig.bendCompliance = 1.5e-5f;\n', '            m_simConfig.bendCompliance = 7.5e-6f;\n            m_simConfig.looseConstraintStiffness = 0.0f;\n            m_simConfig.supportCollisionRadius = 0.0f;\n            m_simConfig.supportCollisionStiffness = 0.0f;\n            m_simConfig.freezeBlend = 0.0f;\n', 1)

anchor = r'''        m_enableSelfCollision = false;
        m_enableGridSelfCollision = false;
        m_enableScalpMotion = false;
        EnforceFixedWorkload();
'''
replacement = r'''        m_enableSelfCollision = false;
        m_enableGridSelfCollision = false;
        m_enableScalpMotion = false;
        if (presetIndex == 0)
        {
            m_workspaceMode = WorkspaceMode::Sim;
            m_interactionMode = InteractionMode::View;
            m_groomTool = GroomTool::Select;
            m_debugColorMode = DebugColorMode::Natural;
            m_maskOverlayMode = MaskOverlayMode::None;
            m_showInspectorPane = false;
            m_showBottomTray = false;
            m_fastGroomViewport = false;
        }
        EnforceFixedWorkload();
'''
if anchor not in text:
    raise RuntimeError("stable preset mode anchor missing")
text = text.replace(anchor, replacement, 1)

anchor = r'''        m_asset = BuildSampleHybridHairAsset(guideCount, followersPerGuide, m_builderParticlesPerStrand, m_simConfig.segmentLength);
        // Avoid double wig styling in viewer; sample/import paths already shape the asset.
'''
replacement = r'''        m_asset = BuildSampleHybridHairAsset(guideCount, followersPerGuide, m_builderParticlesPerStrand, m_simConfig.segmentLength);
        const hairgpu::HairAssetValidationReport structuralReport = hairgpu::ValidateHairAsset(m_asset);
        const hairgpu::BenchmarkWigValidationReport shapeReport = hairgpu::ValidateBenchmarkWigShape(m_asset);
        if (!structuralReport.valid || !shapeReport.valid)
        {
            const std::string diagnostic =
                hairgpu::BuildHairAssetValidationSummary(structuralReport, true) + "\n" +
                hairgpu::BuildBenchmarkWigValidationSummary(shapeReport, true);
            m_lastImportStatus = L"Benchmark wig validation failed";
            const std::wstring wideDiagnostic(diagnostic.begin(), diagnostic.end());
            MessageBoxW(
                m_hwnd,
                wideDiagnostic.c_str(),
                L"Invalid Benchmark Wig",
                MB_OK | MB_ICONERROR);
            return false;
        }
        // Avoid double wig styling in viewer; sample/import paths already shape the asset.
'''
if anchor not in text:
    raise RuntimeError("RebuildAsset validation anchor missing")
text = text.replace(anchor, replacement, 1)
write(path, text)


# -----------------------------------------------------------------------------
# 6) Native CI test builds a smaller deterministic instance and rejects the
#    column-shaped / blunt-tip asset before users can start formal experiments.
# -----------------------------------------------------------------------------
path = "apps/dx11_viewer/src/GpuHairViewer_parts/15_shutdown_and_tail.inl"
text = read(path)
old = r'''        if (!std::isfinite(SegmentLengthForParticleCount(kFixedParticlesPerStrand)) ||
            SegmentLengthForParticleCount(kFixedParticlesPerStrand) <= 0.0f) return 15;
        return 0;
'''
new = r'''        if (!std::isfinite(SegmentLengthForParticleCount(kFixedParticlesPerStrand)) ||
            SegmentLengthForParticleCount(kFixedParticlesPerStrand) <= 0.0f) return 15;

        const hairgpu::HairAsset benchmarkAsset = hairgpu::BuildSampleHybridHairAsset(
            256u,
            2u,
            kFixedParticlesPerStrand,
            SegmentLengthForParticleCount(kFixedParticlesPerStrand));
        const hairgpu::HairAssetValidationReport structuralReport =
            hairgpu::ValidateHairAsset(benchmarkAsset);
        if (!structuralReport.valid) return 16;
        const hairgpu::BenchmarkWigValidationReport shapeReport =
            hairgpu::ValidateBenchmarkWigShape(benchmarkAsset);
        if (!shapeReport.valid) return 17;
        return 0;
'''
if old not in text:
    raise RuntimeError("RunNativeSelfTests anchor missing")
text = text.replace(old, new, 1)
write(path, text)


EXPECTED = [
    "include/hairgpu/Validation.h",
    "src/hairgpu_core/Validation.cpp",
    "src/hairgpu_core/SampleAsset.cpp",
    "apps/dx11_viewer/src/GpuHairViewer.cpp",
    "apps/dx11_viewer/src/GpuHairViewer_parts/08_ui_state_sync_and_controls.inl",
    "apps/dx11_viewer/src/GpuHairViewer_parts/15_shutdown_and_tail.inl",
]
if sorted(set(TOUCHED)) != sorted(EXPECTED):
    raise RuntimeError(f"unexpected modified-file set: {sorted(set(TOUCHED))}")
(ROOT / "stable_benchmark_wig_modified_files.txt").write_text(
    "\n".join(EXPECTED) + "\n",
    encoding="utf-8",
)
print("Patched stable benchmark wig files:")
print("\n".join(EXPECTED))
