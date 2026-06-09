// HybridRuntime.cpp ordered part 00: helpers_error_metrics_and_frame_asset
// This file is included by ../HybridRuntime.cpp; do not compile it directly.

namespace hairgpu
{
    namespace
    {
        constexpr float kPi = 3.1415926535f;

        struct PenetrationResult
        {
            Vec3 correctedPoint = { 0.0f, 0.0f, 0.0f };
            float penetration = 0.0f;
            bool corrected = false;
        };

        float ComputeAverageScalar(const std::vector<float>& values);
        float ComputeMaxScalar(const std::vector<float>& values);

        HairAsset BuildFrameAsset(const HairAsset& referenceAsset, const std::vector<Vec3>& points)
        {
            HairAsset frameAsset = referenceAsset;
            frameAsset.restPoints = points;
            if (frameAsset.pointWidths.size() != frameAsset.restPoints.size())
            {
                const float fallbackWidth = frameAsset.pointWidths.empty() ? 0.0025f : frameAsset.pointWidths.back();
                frameAsset.pointWidths.resize(frameAsset.restPoints.size(), fallbackWidth);
            }
            return frameAsset;
        }

        float EstimateRuntimeRamMegabytes(const HairAsset& asset,
                                          const HybridModelBundle& bundle,
                                          uint32_t clusterCount,
                                          uint32_t cachedFramePointCount)
        {
            uint64_t bytes = 0u;
            const uint64_t pointCount = static_cast<uint64_t>(asset.restPoints.size());
            const uint64_t strandCount = static_cast<uint64_t>(asset.strands.size());
            bytes += pointCount * static_cast<uint64_t>(sizeof(Vec3)) * 14u;
            bytes += pointCount * static_cast<uint64_t>(sizeof(float)) * 6u;
            bytes += strandCount * static_cast<uint64_t>(sizeof(StrandRange)) * 2u;
            bytes += static_cast<uint64_t>(asset.rootBindings.size()) * static_cast<uint64_t>(sizeof(RootBinding));
            bytes += static_cast<uint64_t>(clusterCount) * (16u * static_cast<uint64_t>(sizeof(uint32_t)) + 16u * static_cast<uint64_t>(sizeof(float)));
            bytes += static_cast<uint64_t>(bundle.priorModel.selectedGuideStrandIndices.size()) * static_cast<uint64_t>(sizeof(uint32_t));
            bytes += static_cast<uint64_t>(bundle.residualModel.selectedGuideStrandIndices.size()) * static_cast<uint64_t>(sizeof(uint32_t));
            bytes += static_cast<uint64_t>(bundle.priorModel.strandRecords.size()) * static_cast<uint64_t>(sizeof(ReducedModelStrandRecord));
            bytes += static_cast<uint64_t>(bundle.residualModel.strandRecords.size()) * static_cast<uint64_t>(sizeof(ReducedModelStrandRecord));
            bytes += static_cast<uint64_t>(bundle.priorModel.influences.size()) * static_cast<uint64_t>(sizeof(ReducedModelInfluence));
            bytes += static_cast<uint64_t>(bundle.residualModel.influences.size()) * static_cast<uint64_t>(sizeof(ReducedModelInfluence));
            bytes += static_cast<uint64_t>(bundle.priorModel.pointBiases.size()) * static_cast<uint64_t>(sizeof(Vec3));
            bytes += static_cast<uint64_t>(bundle.residualModel.pointBiases.size()) * static_cast<uint64_t>(sizeof(Vec3));
            bytes += static_cast<uint64_t>(bundle.activeSetPredictor.featureMean.size() + bundle.activeSetPredictor.featureStd.size() +
                                           bundle.activeSetPredictor.weights.size() + bundle.activeSetPredictor.hiddenWeights.size() +
                                           bundle.activeSetPredictor.hiddenBias.size() + bundle.activeSetPredictor.outputWeights.size()) * static_cast<uint64_t>(sizeof(float));
            bytes += static_cast<uint64_t>(bundle.priorTemporalNetwork.inputMean.size() + bundle.priorTemporalNetwork.inputStd.size() +
                                           bundle.priorTemporalNetwork.targetMean.size() + bundle.priorTemporalNetwork.targetStd.size() +
                                           bundle.priorTemporalNetwork.hiddenWeights.size() + bundle.priorTemporalNetwork.hiddenBias.size() +
                                           bundle.priorTemporalNetwork.hiddenWeights2.size() + bundle.priorTemporalNetwork.hiddenBias2.size() +
                                           bundle.priorTemporalNetwork.outputWeights.size() + bundle.priorTemporalNetwork.outputBias.size()) * static_cast<uint64_t>(sizeof(float));
            bytes += static_cast<uint64_t>(bundle.residualTemporalNetwork.inputMean.size() + bundle.residualTemporalNetwork.inputStd.size() +
                                           bundle.residualTemporalNetwork.targetMean.size() + bundle.residualTemporalNetwork.targetStd.size() +
                                           bundle.residualTemporalNetwork.hiddenWeights.size() + bundle.residualTemporalNetwork.hiddenBias.size() +
                                           bundle.residualTemporalNetwork.hiddenWeights2.size() + bundle.residualTemporalNetwork.hiddenBias2.size() +
                                           bundle.residualTemporalNetwork.outputWeights.size() + bundle.residualTemporalNetwork.outputBias.size()) * static_cast<uint64_t>(sizeof(float));
            bytes += static_cast<uint64_t>(cachedFramePointCount) * static_cast<uint64_t>(sizeof(Vec3)) * 2u;
            return static_cast<float>(static_cast<double>(bytes) / (1024.0 * 1024.0));
        }

        float StrandSegmentRestLength(const HairAsset& asset, uint32_t strandIndex, uint32_t pointIndex)
        {
            if (strandIndex >= asset.strands.size())
            {
                return 0.0f;
            }
            const StrandRange& strand = asset.strands[strandIndex];
            if (pointIndex == 0u || pointIndex >= strand.pointCount)
            {
                return 0.0f;
            }
            const uint32_t prevIndex = strand.pointOffset + pointIndex - 1u;
            const uint32_t currIndex = strand.pointOffset + pointIndex;
            if (currIndex >= asset.restPoints.size() || prevIndex >= asset.restPoints.size())
            {
                return 0.0f;
            }
            return Length(asset.restPoints[currIndex] - asset.restPoints[prevIndex]);
        }

        PenetrationResult ResolveAgainstFloor(const Vec3& point, float floorY, float shellDistance)
        {
            PenetrationResult result = {};
            result.correctedPoint = point;
            const float floorLimit = floorY + shellDistance;
            if (point.y < floorLimit)
            {
                result.penetration = floorLimit - point.y;
                result.correctedPoint.y = floorLimit;
                result.corrected = true;
            }
            return result;
        }

        PenetrationResult ResolveAgainstSphere(const Vec3& point, const SphereCollider& sphere, float shellDistance)
        {
            PenetrationResult result = {};
            result.correctedPoint = point;
            const Vec3 delta = point - sphere.center;
            const float distance = Length(delta);
            const float targetDistance = sphere.radius + shellDistance;
            if (distance < targetDistance)
            {
                const Vec3 normal = (distance > 1.0e-6f) ? (delta / distance) : Vec3(0.0f, 1.0f, 0.0f);
                result.penetration = targetDistance - distance;
                result.correctedPoint = sphere.center + normal * targetDistance;
                result.corrected = true;
            }
            return result;
        }

        PenetrationResult ResolveAgainstCapsule(const Vec3& point, const CapsuleCollider& capsule, float shellDistance)
        {
            PenetrationResult result = {};
            result.correctedPoint = point;
            const CapsuleProjection projection = ProjectToCapsule(capsule, point, shellDistance);
            if (projection.distance < projection.radius)
            {
                result.penetration = projection.radius - projection.distance;
                result.correctedPoint = projection.closest + projection.normal * projection.radius;
                result.corrected = true;
            }
            return result;
        }

        bool HasAnyCollider(const ColliderSet& colliders)
        {
            return !colliders.spheres.empty() ||
                   !colliders.capsules.empty() ||
                   !colliders.planes.empty() ||
                   !colliders.aabbs.empty() ||
                   !colliders.sdfGrids.empty();
        }

        void CopyStrandPoints(const HairAsset& asset,
                              const std::vector<Vec3>& sourcePoints,
                              uint32_t strandIndex,
                              std::vector<Vec3>& outPoints)
        {
            outPoints.clear();
            if (strandIndex >= asset.strands.size())
            {
                return;
            }
            const StrandRange& strand = asset.strands[strandIndex];
            outPoints.reserve(strand.pointCount);
            for (uint32_t pointIndex = 0u; pointIndex < strand.pointCount; ++pointIndex)
            {
                const uint32_t globalIndex = strand.pointOffset + pointIndex;
                outPoints.push_back((globalIndex < sourcePoints.size()) ? sourcePoints[globalIndex] : Vec3(0.0f, 0.0f, 0.0f));
            }
        }

        void WriteStrandPoints(const HairAsset& asset,
                               uint32_t strandIndex,
                               const std::vector<Vec3>& strandPoints,
                               std::vector<Vec3>& inOutPoints)
        {
            if (strandIndex >= asset.strands.size())
            {
                return;
            }
            const StrandRange& strand = asset.strands[strandIndex];
            for (uint32_t pointIndex = 0u; pointIndex < strand.pointCount && pointIndex < strandPoints.size(); ++pointIndex)
            {
                const uint32_t globalIndex = strand.pointOffset + pointIndex;
                if (globalIndex < inOutPoints.size())
                {
                    inOutPoints[globalIndex] = strandPoints[pointIndex];
                }
            }
        }

        void BuildRootAnchoredTierPose(const HairAsset& asset,
                                       uint32_t strandIndex,
                                       const std::vector<Vec3>& currentPoints,
                                       const std::vector<Vec3>& anchorPoints,
                                       float rootFollow,
                                       float anchorBlend,
                                       std::vector<Vec3>& outStrandPoints)
        {
            outStrandPoints.clear();
            if (strandIndex >= asset.strands.size())
            {
                return;
            }
            const StrandRange& strand = asset.strands[strandIndex];
            if (strand.pointCount == 0u)
            {
                return;
            }

            const uint32_t rootIndex = strand.pointOffset;
            if (rootIndex >= currentPoints.size() || rootIndex >= anchorPoints.size())
            {
                return;
            }

            const Vec3 currentRoot = currentPoints[rootIndex];
            const Vec3 anchorRoot = anchorPoints[rootIndex];
            const Vec3 translation = (currentRoot - anchorRoot) * Clamp(rootFollow, 0.0f, 1.0f);
            const float blend = Clamp(anchorBlend, 0.0f, 1.0f);
            outStrandPoints.reserve(strand.pointCount);
            for (uint32_t pointIndex = 0u; pointIndex < strand.pointCount; ++pointIndex)
            {
                const uint32_t globalIndex = strand.pointOffset + pointIndex;
                const Vec3 current = (globalIndex < currentPoints.size()) ? currentPoints[globalIndex] : Vec3(0.0f, 0.0f, 0.0f);
                const Vec3 anchored = (globalIndex < anchorPoints.size()) ? (anchorPoints[globalIndex] + translation) : current;
                outStrandPoints.push_back(Lerp(current, anchored, blend));
            }
            if (!outStrandPoints.empty())
            {
                outStrandPoints[0] = Lerp(currentRoot, anchorRoot + translation, blend);
            }
        }


        void SmoothGuidePoints(std::vector<Vec3>& smoothed,
                               const std::vector<Vec3>& current,
                               float alpha)
        {
            const float a = Clamp(alpha, 0.01f, 1.0f);
            const size_t count = std::min(smoothed.size(), current.size());
            for (size_t i = 0u; i < count; ++i)
            {
                smoothed[i] = Lerp(smoothed[i], current[i], a);
            }
        }

        std::vector<Vec3> PredictSecondOrderGuidePoints(const std::vector<Vec3>& current,
                                                        const std::vector<Vec3>& previous,
                                                        const std::vector<Vec3>& previousPrevious,
                                                        float velocityGain,
                                                        float accelerationGain)
        {
            std::vector<Vec3> predicted = current;
            const size_t count = std::min(predicted.size(), previous.size());
            for (size_t i = 0u; i < count; ++i)
            {
                const Vec3 velocity = current[i] - previous[i];
                predicted[i] += velocity * velocityGain;
                if (i < previousPrevious.size())
                {
                    const Vec3 acceleration = current[i] - previous[i] * 2.0f + previousPrevious[i];
                    predicted[i] += acceleration * accelerationGain;
                }
            }
            return predicted;
        }

        void BlendGuidePoints(std::vector<Vec3>& base,
                              const std::vector<Vec3>& target,
                              float blend)
        {
            const float t = Clamp(blend, 0.0f, 1.0f);
            const size_t count = std::min(base.size(), target.size());
            for (size_t i = 0u; i < count; ++i)
            {
                base[i] = Lerp(base[i], target[i], t);
            }
        }

        float EstimateSecondOrderPredictionGain(const std::vector<Vec3>& previousPreviousPoints,
                                                const std::vector<Vec3>& previousPoints,
                                                const std::vector<Vec3>& currentPoints,
                                                float velocityGain,
                                                float fallbackGain,
                                                float maxGain)
        {
            const size_t pointCount = std::min(previousPreviousPoints.size(), std::min(previousPoints.size(), currentPoints.size()));
            if (pointCount == 0u)
            {
                return Clamp(fallbackGain, 0.0f, maxGain);
            }

            double numerator = 0.0;
            double denominator = 0.0;
            for (size_t i = 0u; i < pointCount; ++i)
            {
                const Vec3 velocity = currentPoints[i] - previousPoints[i];
                const Vec3 acceleration = currentPoints[i] - previousPoints[i] * 2.0f + previousPreviousPoints[i];
                const Vec3 desiredResidual = velocity * Clamp(velocityGain, 0.0f, 1.0f);
                numerator += static_cast<double>(Dot(desiredResidual, acceleration));
                denominator += static_cast<double>(Dot(acceleration, acceleration));
            }
            if (denominator <= 1.0e-8)
            {
                return Clamp(fallbackGain, 0.0f, maxGain);
            }
            return Clamp(static_cast<float>(numerator / denominator), 0.0f, maxGain);
        }

        float SignedDistanceToSphere(const Vec3& point, const SphereCollider& sphere)
        {
            return Length(point - sphere.center) - sphere.radius;
        }

        float SignedDistanceToCapsule(const Vec3& point, const CapsuleCollider& capsule)
        {
            const CapsuleProjection projection = ProjectToCapsule(capsule, point);
            return projection.distance - projection.radius;
        }

        float SignedDistanceToPlane(const Vec3& point, const PlaneCollider& plane)
        {
            const Vec3 normal = Normalize(plane.normal);
            const Vec3 safeNormal = (Length(normal) > 1.0e-6f) ? normal : Vec3(0.0f, 1.0f, 0.0f);
            return Dot(point - plane.point, safeNormal);
        }

        Vec3 ClosestPointOnAabb(const Vec3& point, const AabbCollider& box)
        {
            return Vec3(
                Clamp(point.x, std::min(box.minBounds.x, box.maxBounds.x), std::max(box.minBounds.x, box.maxBounds.x)),
                Clamp(point.y, std::min(box.minBounds.y, box.maxBounds.y), std::max(box.minBounds.y, box.maxBounds.y)),
                Clamp(point.z, std::min(box.minBounds.z, box.maxBounds.z), std::max(box.minBounds.z, box.maxBounds.z)));
        }

        float SignedDistanceToAabb(const Vec3& point, const AabbCollider& box)
        {
            const Vec3 minB(
                std::min(box.minBounds.x, box.maxBounds.x),
                std::min(box.minBounds.y, box.maxBounds.y),
                std::min(box.minBounds.z, box.maxBounds.z));
            const Vec3 maxB(
                std::max(box.minBounds.x, box.maxBounds.x),
                std::max(box.minBounds.y, box.maxBounds.y),
                std::max(box.minBounds.z, box.maxBounds.z));
            const Vec3 center = (minB + maxB) * 0.5f;
            const Vec3 halfExtents = (maxB - minB) * 0.5f;
            const Vec3 local = point - center;
            const Vec3 q(
                std::fabs(local.x) - halfExtents.x,
                std::fabs(local.y) - halfExtents.y,
                std::fabs(local.z) - halfExtents.z);
            const Vec3 outside(
                std::max(q.x, 0.0f),
                std::max(q.y, 0.0f),
                std::max(q.z, 0.0f));
            const float outsideDistance = Length(outside);
            const float insideDistance = std::min(std::max(q.x, std::max(q.y, q.z)), 0.0f);
            return outsideDistance + insideDistance;
        }

        PenetrationResult ResolveAgainstPlane(const Vec3& point, const PlaneCollider& plane, float shellDistance)
        {
            PenetrationResult result = {};
            result.correctedPoint = point;
            const Vec3 normal = Normalize(plane.normal);
            const Vec3 safeNormal = (Length(normal) > 1.0e-6f) ? normal : Vec3(0.0f, 1.0f, 0.0f);
            const float signedDistance = Dot(point - plane.point, safeNormal);
            if (signedDistance < shellDistance)
            {
                result.penetration = shellDistance - signedDistance;
                result.correctedPoint = point + safeNormal * result.penetration;
                result.corrected = true;
            }
            return result;
        }

        PenetrationResult ResolveAgainstAabb(const Vec3& point, const AabbCollider& box, float shellDistance)
        {
            PenetrationResult result = {};
            result.correctedPoint = point;
            const float signedDistance = SignedDistanceToAabb(point, box);
            if (signedDistance >= shellDistance)
            {
                return result;
            }

            Vec3 closest = ClosestPointOnAabb(point, box);
            Vec3 delta = point - closest;
            Vec3 normal = Normalize(delta);
            if (Length(normal) <= 1.0e-6f)
            {
                const Vec3 minB(
                    std::min(box.minBounds.x, box.maxBounds.x),
                    std::min(box.minBounds.y, box.maxBounds.y),
                    std::min(box.minBounds.z, box.maxBounds.z));
                const Vec3 maxB(
                    std::max(box.minBounds.x, box.maxBounds.x),
                    std::max(box.minBounds.y, box.maxBounds.y),
                    std::max(box.minBounds.z, box.maxBounds.z));
                const float dx = std::min(std::fabs(point.x - minB.x), std::fabs(maxB.x - point.x));
                const float dy = std::min(std::fabs(point.y - minB.y), std::fabs(maxB.y - point.y));
                const float dz = std::min(std::fabs(point.z - minB.z), std::fabs(maxB.z - point.z));
                if (dx <= dy && dx <= dz)
                {
                    normal = Vec3((point.x - (minB.x + maxB.x) * 0.5f) >= 0.0f ? 1.0f : -1.0f, 0.0f, 0.0f);
                }
                else if (dy <= dz)
                {
                    normal = Vec3(0.0f, (point.y - (minB.y + maxB.y) * 0.5f) >= 0.0f ? 1.0f : -1.0f, 0.0f);
                }
                else
                {
                    normal = Vec3(0.0f, 0.0f, (point.z - (minB.z + maxB.z) * 0.5f) >= 0.0f ? 1.0f : -1.0f);
                }
            }

            result.penetration = shellDistance - signedDistance;
            result.correctedPoint = point + normal * result.penetration;
            result.corrected = true;
            return result;
        }

        PenetrationResult ResolveAgainstSdfGrid(const Vec3& point, const SdfGridCollider& grid, float shellDistance)
        {
            PenetrationResult result = {};
            result.correctedPoint = point;
            const float signedDistance = SampleSdfGridTrilinear(grid, point);
            if (signedDistance >= shellDistance || signedDistance == std::numeric_limits<float>::max())
            {
                return result;
            }

            const Vec3 normal = EstimateSdfGridGradient(grid, point);
            result.penetration = shellDistance - signedDistance;
            result.correctedPoint = point + normal * result.penetration;
            result.corrected = true;
            return result;
        }

        float ComputeContactScoreFromSignedDistance(float signedDistance, float shellDistance, float bandScale)
        {
            const float contactBand = std::max(shellDistance * bandScale, 1.0e-4f);
            return Clamp((contactBand - signedDistance) / contactBand, 0.0f, 1.0f);
        }

        float ComputeColliderContactScore(const Vec3& point, const ColliderSet& colliders, float floorY, float shellDistance, bool includeFloor)
        {
            float bestScore = 0.0f;
            if (includeFloor)
            {
                bestScore = std::max(bestScore, ComputeContactScoreFromSignedDistance(point.y - floorY, shellDistance, 2.0f));
            }
            for (const SphereCollider& sphere : colliders.spheres)
            {
                bestScore = std::max(bestScore, ComputeContactScoreFromSignedDistance(SignedDistanceToSphere(point, sphere), shellDistance, 3.0f));
            }
            for (const CapsuleCollider& capsule : colliders.capsules)
            {
                bestScore = std::max(bestScore, ComputeContactScoreFromSignedDistance(SignedDistanceToCapsule(point, capsule), shellDistance, 3.5f));
            }
            for (const PlaneCollider& plane : colliders.planes)
            {
                bestScore = std::max(bestScore, ComputeContactScoreFromSignedDistance(SignedDistanceToPlane(point, plane), shellDistance, 2.0f));
            }
            for (const AabbCollider& box : colliders.aabbs)
            {
                bestScore = std::max(bestScore, ComputeContactScoreFromSignedDistance(SignedDistanceToAabb(point, box), shellDistance, 3.0f));
            }
            for (const SdfGridCollider& grid : colliders.sdfGrids)
            {
                bestScore = std::max(bestScore, ComputeContactScoreFromSignedDistance(SampleSdfGridTrilinear(grid, point), shellDistance, 3.0f));
            }
            return Clamp(bestScore, 0.0f, 1.0f);
        }

        PenetrationResult ResolveAgainstColliderSet(const Vec3& point,
                                                    const ColliderSet& colliders,
                                                    float shellDistance,
                                                    bool affectFloor,
                                                    float floorY)
        {
            PenetrationResult best = {};
            best.correctedPoint = point;
            if (affectFloor)
            {
                PenetrationResult floorResult = ResolveAgainstFloor(point, floorY, shellDistance);
                if (floorResult.penetration > best.penetration)
                {
                    best = floorResult;
                }
            }
            for (const SphereCollider& sphere : colliders.spheres)
            {
                PenetrationResult result = ResolveAgainstSphere(point, sphere, shellDistance);
                if (result.penetration > best.penetration)
                {
                    best = result;
                }
            }
            for (const CapsuleCollider& capsule : colliders.capsules)
            {
                PenetrationResult result = ResolveAgainstCapsule(point, capsule, shellDistance);
                if (result.penetration > best.penetration)
                {
                    best = result;
                }
            }
            for (const PlaneCollider& plane : colliders.planes)
            {
                PenetrationResult result = ResolveAgainstPlane(point, plane, shellDistance);
                if (result.penetration > best.penetration)
                {
                    best = result;
                }
            }
            for (const AabbCollider& box : colliders.aabbs)
            {
                PenetrationResult result = ResolveAgainstAabb(point, box, shellDistance);
                if (result.penetration > best.penetration)
                {
                    best = result;
                }
            }
            for (const SdfGridCollider& grid : colliders.sdfGrids)
            {
                PenetrationResult result = ResolveAgainstSdfGrid(point, grid, shellDistance);
                if (result.penetration > best.penetration)
                {
                    best = result;
                }
            }
            return best;
        }

        ColliderSet BuildFallbackTeacherColliders(const HairAsset& asset)
        {
            ColliderSet colliders;
            if (asset.restPoints.empty())
            {
                return colliders;
            }

            Vec3 minP = asset.restPoints.front();
            Vec3 maxP = asset.restPoints.front();
            for (const Vec3& p : asset.restPoints)
            {
                minP.x = std::min(minP.x, p.x);
                minP.y = std::min(minP.y, p.y);
                minP.z = std::min(minP.z, p.z);
                maxP.x = std::max(maxP.x, p.x);
                maxP.y = std::max(maxP.y, p.y);
                maxP.z = std::max(maxP.z, p.z);
            }
            const Vec3 center = (minP + maxP) * 0.5f;
            const float height = std::max(maxP.y - minP.y, 0.2f);
            const float width = std::max(maxP.x - minP.x, 0.12f);
            const float depth = std::max(maxP.z - minP.z, 0.12f);

            SphereCollider head = {};
            head.center = Vec3(center.x, minP.y + height * 0.70f, center.z + depth * 0.06f);
            head.radius = std::max({ width, depth, height * 0.55f }) * 0.34f;
            colliders.spheres.push_back(head);

            CapsuleCollider shoulder = {};
            shoulder.pointA = Vec3(center.x - width * 0.30f, minP.y + height * 0.24f, center.z + depth * 0.08f);
            shoulder.pointB = Vec3(center.x + width * 0.30f, minP.y + height * 0.24f, center.z + depth * 0.08f);
            shoulder.radiusA = width * 0.16f;
            shoulder.radiusB = width * 0.16f;
            colliders.capsules.push_back(shoulder);

            AabbCollider torso = {};
            torso.minBounds = Vec3(center.x - width * 0.22f, minP.y + height * 0.02f, center.z - depth * 0.10f);
            torso.maxBounds = Vec3(center.x + width * 0.22f, minP.y + height * 0.36f, center.z + depth * 0.16f);
            colliders.aabbs.push_back(torso);

            PlaneCollider floor = {};
            floor.point = Vec3(0.0f, minP.y - std::max(height * 0.12f, 0.06f), 0.0f);
            floor.normal = Vec3(0.0f, 1.0f, 0.0f);
            colliders.planes.push_back(floor);
            return colliders;
        }

        void BuildProxyFramePoints(const HairAsset& asset,
                                   float timeSeconds,
                                   float swayAmplitude,
                                   float tipNoiseAmplitude,
                                   float windCyclesPerSecond,
                                   std::vector<Vec3>& outPoints)
        {
            outPoints = asset.restPoints;
            const float basePhase = timeSeconds * 2.0f * kPi * windCyclesPerSecond;
            for (uint32_t strandIndex = 0u; strandIndex < asset.strands.size(); ++strandIndex)
            {
                const StrandRange& strand = asset.strands[strandIndex];
                if (strand.pointCount == 0u)
                {
                    continue;
                }
                const float strandPhase = static_cast<float>(strand.groupId) * 0.41f + static_cast<float>(strandIndex) * 0.083f;
                const float strandBias = std::sin(basePhase * 0.47f + strandPhase * 1.3f);
                for (uint32_t localPoint = 0u; localPoint < strand.pointCount; ++localPoint)
                {
                    const uint32_t pointIndex = strand.pointOffset + localPoint;
                    if (pointIndex >= outPoints.size())
                    {
                        continue;
                    }

                    const float t = (strand.pointCount > 1u) ? (static_cast<float>(localPoint) / static_cast<float>(strand.pointCount - 1u)) : 0.0f;
                    if (localPoint == 0u)
                    {
                        continue;
                    }

                    const float sway = std::sin(basePhase + strandPhase + t * 0.85f);
                    const float twist = std::cos(basePhase * 1.17f + strandPhase * 1.7f + t * 1.3f);
                    const float gust = std::sin(basePhase * 0.63f + outPoints[pointIndex].z * 5.5f - t * 1.1f);
                    const float contactPulse = std::max(0.0f, std::sin(basePhase * 0.32f + outPoints[pointIndex].x * 10.0f + t * 0.8f));
                    const float tipScale = std::pow(t, 1.15f);

                    Vec3 offset = {};
                    offset.x = swayAmplitude * tipScale * (0.75f * sway + 0.25f * strandBias);
                    offset.y = -swayAmplitude * tipScale * 0.22f * std::fabs(gust) - swayAmplitude * tipScale * 0.12f * contactPulse;
                    offset.z = swayAmplitude * tipScale * (0.55f * twist + 0.35f * gust);

                    const float noise = tipNoiseAmplitude * tipScale;
                    offset.x += noise * std::sin(basePhase * 1.91f + static_cast<float>(pointIndex) * 0.37f);
                    offset.z += noise * std::cos(basePhase * 1.53f + static_cast<float>(pointIndex) * 0.23f);

                    outPoints[pointIndex] = outPoints[pointIndex] + offset;
                }
            }
        }

        void BuildGuideRootsFromPoints(const HairAsset& asset,
                                       const std::vector<Vec3>& points,
                                       std::vector<Vec3>& outRoots)
        {
            const uint32_t guideCount = std::min<uint32_t>(asset.guideStrandCount, static_cast<uint32_t>(asset.strands.size()));
            outRoots.assign(guideCount, Vec3(0.0f, 0.0f, 0.0f));
            for (uint32_t guideIndex = 0u; guideIndex < guideCount; ++guideIndex)
            {
                const StrandRange& strand = asset.strands[guideIndex];
                if (strand.pointCount > 0u && strand.pointOffset < points.size())
                {
                    outRoots[guideIndex] = points[strand.pointOffset];
                }
            }
        }

