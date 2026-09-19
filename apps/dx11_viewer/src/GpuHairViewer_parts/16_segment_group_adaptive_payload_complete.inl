// GpuHairViewer.cpp ordered part 16: complete Novelty 01 SG-APT runtime.
// This file is included by ../GpuHairViewer.cpp; do not compile it directly.
//
// The implementation is isolated behind ExperimentMethodId::P_IntegratedSegmentGroup.
// B0-B8 continue to execute the existing baseline paths without SG-APT state.

    namespace
    {
        using N01Core = hairgpu::novelty01::SegmentGroupCore;
        using N01Extension = hairgpu::novelty01::CoreExtension;
        using N01Cache = hairgpu::novelty01::StaticCache;
        using N01Payload = hairgpu::novelty01::DynamicPayload;
        using N01PayloadOwner = hairgpu::novelty01::PayloadOwner;
        using N01GuideDirtyRange = hairgpu::novelty01::GuideDirtyRange;
        using N01FollowerDirtyRange = hairgpu::novelty01::FollowerDirtyRange;
        using N01RenderDirtyRange = hairgpu::novelty01::RenderDirtyRange;
        using N01GroundTruthMetric = hairgpu::novelty01::GroundTruthMetric;
        using N01Descriptor = hairgpu::novelty01::ReuseDescriptor;
        using N01Block = hairgpu::novelty01::BlockDescriptor;
        using N01Candidate = hairgpu::novelty01::SharedPointCandidate;
        using N01Constants = hairgpu::novelty01::RuntimeConstants;
        using N01CounterIndex = hairgpu::novelty01::CounterIndex;
        using N01State = hairgpu::novelty01::RuntimeState;

        constexpr uint32_t N01RuntimeFlag_RootChanged = 1u << 0u;
        constexpr uint32_t N01RuntimeFlag_ForceChanged = 1u << 1u;
        constexpr uint32_t N01RuntimeFlag_ColliderChanged = 1u << 2u;
        constexpr uint32_t N01RuntimeFlag_TopologyChanged = 1u << 3u;
        constexpr uint32_t N01RuntimeFlag_EditChanged = 1u << 4u;
        constexpr uint32_t N01RuntimeFlag_DependencyChanged = 1u << 5u;
        constexpr uint32_t N01RuntimeFlag_ForceAllDirty = 1u << 6u;
        constexpr uint32_t N01RuntimeFlag_ForceAllDynamic = 1u << 7u;
        constexpr uint32_t N01RuntimeFlag_ForceAllStatic = 1u << 8u;

        uint64_t N01ReadFeatureMaskForVariant(
            hairgpu::novelty01::ExperimentVariant variant)
        {
            using namespace hairgpu::novelty01;
            uint64_t mask = kAllFeatures;
            switch (variant)
            {
            case ExperimentVariant::B1_OptimizedFullDynamic:
                mask &= ~(Feature_OptionalPayload |
                          Feature_GroundTruth |
                          Feature_ShadowDynamic |
                          Feature_TransitionBudget);
                break;
            case ExperimentVariant::B2_SleepingFlag:
                mask &= ~(Feature_OptionalPayload |
                          Feature_StableCompaction |
                          Feature_FollowerDependency |
                          Feature_RenderDirtyRange |
                          Feature_FullDynamicFastPath |
                          Feature_GroundTruth);
                break;
            case ExperimentVariant::B3_ActiveList:
                mask &= ~(Feature_OptionalPayload |
                          Feature_FollowerDependency |
                          Feature_RenderDirtyRange |
                          Feature_GroundTruth);
                break;
            case ExperimentVariant::B4_SeparateStaticDynamicBuffers:
                mask &= ~(Feature_OptionalPayload |
                          Feature_FixedGroupIndex |
                          Feature_GroundTruth);
                break;
            case ExperimentVariant::B5_FixedCoreSimplePayload:
                mask =
                    Feature_OptionalPayload |
                    Feature_FixedGroupIndex |
                    Feature_StableCompaction |
                    Feature_IndirectDispatch |
                    Feature_BaselineEquivalentSolve |
                    Feature_FormalTelemetry;
                break;
            case ExperimentVariant::B6_NoBlockInvalidation:
                mask &= ~Feature_IncrementalBlocks;
                break;
            case ExperimentVariant::B7_GuideLocalOnly:
                mask &= ~(Feature_FollowerDependency |
                          Feature_RenderDirtyRange);
                break;
            case ExperimentVariant::B8_StrongestPractical:
                mask &= ~(Feature_OptionalPayload |
                          Feature_PayloadOwnerCheck |
                          Feature_DelayedPayloadReuse |
                          Feature_GroundTruth);
                break;
            case ExperimentVariant::A1_NoOptionalPayload:
                mask &= ~Feature_OptionalPayload; break;
            case ExperimentVariant::A2_NoFixedGroupIndex:
                mask &= ~Feature_FixedGroupIndex; break;
            case ExperimentVariant::A3_NoVersionCheck:
                mask &= ~Feature_VersionCheck; break;
            case ExperimentVariant::A4_NoDelayedFree:
                mask &= ~Feature_DelayedPayloadReuse; break;
            case ExperimentVariant::A5_NoHysteresis:
                mask &= ~Feature_Hysteresis; break;
            case ExperimentVariant::A6_NoMinimumResidence:
                mask &= ~Feature_MinimumResidence; break;
            case ExperimentVariant::A7_NoStaticCache:
                mask &= ~Feature_StaticCache; break;
            case ExperimentVariant::A8_NoIncrementalBlockInvalidation:
                mask &= ~Feature_IncrementalBlocks; break;
            case ExperimentVariant::A9_NoStableSharedPoint:
                mask &= ~Feature_StableSharedPoint; break;
            case ExperimentVariant::A10_NoFollowerDependency:
                mask &= ~Feature_FollowerDependency; break;
            case ExperimentVariant::A11_NoRenderDirtyRange:
                mask &= ~Feature_RenderDirtyRange; break;
            case ExperimentVariant::A12_NoFullDynamicFastPath:
                mask &= ~Feature_FullDynamicFastPath; break;
            case ExperimentVariant::A13_NoShadowDynamic:
                mask &= ~Feature_ShadowDynamic; break;
            case ExperimentVariant::A14_NoWatchdog:
                mask &= ~Feature_Watchdog; break;
            case ExperimentVariant::A15_NoStableCompaction:
                mask &= ~Feature_StableCompaction; break;
            case ExperimentVariant::P:
            default:
                break;
            }
            return mask;
        }

        hairgpu::novelty01::ExperimentVariant N01ParseVariant(
            const std::wstring& value)
        {
            using V = hairgpu::novelty01::ExperimentVariant;
            const std::wstring normalized = value.empty() ? L"P" : value;
            static const std::pair<const wchar_t*, V> mappings[] =
            {
                { L"P", V::P },
                { L"B1", V::B1_OptimizedFullDynamic },
                { L"B2", V::B2_SleepingFlag },
                { L"B3", V::B3_ActiveList },
                { L"B4", V::B4_SeparateStaticDynamicBuffers },
                { L"B5", V::B5_FixedCoreSimplePayload },
                { L"B6", V::B6_NoBlockInvalidation },
                { L"B7", V::B7_GuideLocalOnly },
                { L"B8", V::B8_StrongestPractical },
                { L"A1", V::A1_NoOptionalPayload },
                { L"A2", V::A2_NoFixedGroupIndex },
                { L"A3", V::A3_NoVersionCheck },
                { L"A4", V::A4_NoDelayedFree },
                { L"A5", V::A5_NoHysteresis },
                { L"A6", V::A6_NoMinimumResidence },
                { L"A7", V::A7_NoStaticCache },
                { L"A8", V::A8_NoIncrementalBlockInvalidation },
                { L"A9", V::A9_NoStableSharedPoint },
                { L"A10", V::A10_NoFollowerDependency },
                { L"A11", V::A11_NoRenderDirtyRange },
                { L"A12", V::A12_NoFullDynamicFastPath },
                { L"A13", V::A13_NoShadowDynamic },
                { L"A14", V::A14_NoWatchdog },
                { L"A15", V::A15_NoStableCompaction }
            };
            for (const auto& mapping : mappings)
            {
                if (_wcsicmp(normalized.c_str(), mapping.first) == 0)
                {
                    return mapping.second;
                }
            }
            return V::P;
        }

        uint32_t N01ParseDesignScenarioIndex(
            const std::wstring& value,
            uint32_t fallback)
        {
            if (value.size() == 3u &&
                (value[0] == L'S' || value[0] == L's') &&
                value[1] >= L'0' && value[1] <= L'9' &&
                value[2] >= L'0' && value[2] <= L'9')
            {
                const uint32_t oneBased =
                    static_cast<uint32_t>(value[1] - L'0') * 10u +
                    static_cast<uint32_t>(value[2] - L'0');
                if (oneBased >= 1u && oneBased <= 18u)
                {
                    return oneBased - 1u;
                }
            }
            return fallback < 18u ? fallback : 0u;
        }

        bool N01EnvironmentEnabled(const wchar_t* name, bool fallback = false)
        {
            const std::wstring value = GetEnvironmentVariableString(name);
            if (value.empty())
            {
                return fallback;
            }
            return value != L"0" &&
                _wcsicmp(value.c_str(), L"false") != 0 &&
                _wcsicmp(value.c_str(), L"off") != 0;
        }

        uint64_t N01HashBytes(uint64_t hash, const void* bytes, size_t size)
        {
            const uint8_t* cursor = static_cast<const uint8_t*>(bytes);
            for (size_t i = 0u; i < size; ++i)
            {
                hash ^= cursor[i];
                hash *= 1099511628211ull;
            }
            return hash;
        }

        template <typename TParticle>
        uint64_t N01HashScalp(const std::vector<TParticle>& values)
        {
            uint64_t hash = 1469598103934665603ull;
            if (values.empty())
            {
                return hash;
            }
            const size_t stride = std::max<size_t>(values.size() / 32u, 1u);
            for (size_t i = 0u; i < values.size(); i += stride)
            {
                hash = N01HashBytes(hash, &values[i], sizeof(values[i]));
            }
            hash = N01HashBytes(hash, &values.back(), sizeof(values.back()));
            return hash;
        }

        uint64_t N01HashSimulationForces(const hairgpu::GpuSimulationConfig& config)
        {
            uint64_t hash = 1469598103934665603ull;
            hash = N01HashBytes(hash, &config.gravity, sizeof(config.gravity));
            hash = N01HashBytes(hash, &config.globalDamping, sizeof(config.globalDamping));
            hash = N01HashBytes(hash, &config.windStrength, sizeof(config.windStrength));
            hash = N01HashBytes(hash, &config.windDirection, sizeof(config.windDirection));
            hash = N01HashBytes(hash, &config.stretchCompliance, sizeof(config.stretchCompliance));
            hash = N01HashBytes(hash, &config.bendCompliance, sizeof(config.bendCompliance));
            hash = N01HashBytes(hash, &config.bendStiffness, sizeof(config.bendStiffness));
            hash = N01HashBytes(hash, &config.solverIterations, sizeof(config.solverIterations));
            return hash;
        }

        uint32_t N01DispatchGroups(uint32_t count, uint32_t threads)
        {
            if (count == 0u || threads == 0u)
            {
                return 0u;
            }
            return (count + threads - 1u) / threads;
        }

        void N01UnbindCompute(ID3D11DeviceContext* context)
        {
            if (!context)
            {
                return;
            }
            ID3D11ShaderResourceView* nullSrvs[16] = {};
            ID3D11UnorderedAccessView* nullUavs[8] = {};
            ID3D11Buffer* nullConstants[2] = {};
            context->CSSetShader(nullptr, nullptr, 0u);
            context->CSSetShaderResources(0u, 16u, nullSrvs);
            context->CSSetUnorderedAccessViews(0u, 8u, nullUavs, nullptr);
            context->CSSetConstantBuffers(0u, 2u, nullConstants);
        }

        bool N01CompileShader(
            ID3D11Device* device,
            const char* entryPoint,
            uint32_t bindingMode,
            Microsoft::WRL::ComPtr<ID3D11ComputeShader>& destination,
            std::wstring& errorText)
        {
            if (!device || !entryPoint)
            {
                errorText = L"Invalid Novelty 01 shader compile request.";
                return false;
            }

            wchar_t modulePath[MAX_PATH] = {};
            GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
            const std::filesystem::path executableDirectory =
                std::filesystem::path(modulePath).parent_path();
            const wchar_t* fileName =
                L"HairSegmentGroupAdaptivePayloadRuntime.hlsl";

            std::vector<std::filesystem::path> candidates;
            const auto pushCandidate = [&candidates](const std::filesystem::path& path)
            {
                if (path.empty())
                {
                    return;
                }
                const std::filesystem::path normalized = path.lexically_normal();
                if (std::find(candidates.begin(), candidates.end(), normalized) == candidates.end())
                {
                    candidates.push_back(normalized);
                }
            };

            pushCandidate(std::filesystem::path(fileName));
#if defined(HAIRGPU_VIEWER_SHADER_DIR)
            pushCandidate(std::filesystem::path(HAIRGPU_VIEWER_SHADER_DIR) / fileName);
#endif
            pushCandidate(executableDirectory / fileName);
            pushCandidate(executableDirectory / L"shaders" / fileName);
            pushCandidate(std::filesystem::current_path() / fileName);
            pushCandidate(std::filesystem::current_path() / L"shaders" / fileName);

            std::filesystem::path searchRoot = executableDirectory;
            for (int depth = 0; depth < 7; ++depth)
            {
                pushCandidate(searchRoot / L"apps" / L"dx11_viewer" / L"shaders" / fileName);
                pushCandidate(searchRoot / L"dx11_viewer" / L"shaders" / fileName);
                if (!searchRoot.has_parent_path())
                {
                    break;
                }
                const std::filesystem::path parent = searchRoot.parent_path();
                if (parent == searchRoot)
                {
                    break;
                }
                searchRoot = parent;
            }

            const std::string modeText = std::to_string(bindingMode);
            const D3D_SHADER_MACRO macros[] =
            {
                { "N01_BINDING_MODE", modeText.c_str() },
                { nullptr, nullptr }
            };

            UINT compileFlags =
                D3DCOMPILE_ENABLE_STRICTNESS |
                D3DCOMPILE_OPTIMIZATION_LEVEL3;
#if defined(_DEBUG)
            compileFlags |= D3DCOMPILE_DEBUG;
#endif

            HRESULT lastResult = HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
            Microsoft::WRL::ComPtr<ID3DBlob> byteCode;
            Microsoft::WRL::ComPtr<ID3DBlob> errors;
            std::wstringstream triedPaths;

            for (const std::filesystem::path& candidate : candidates)
            {
                const bool exists = std::filesystem::exists(candidate);
                triedPaths << candidate.c_str()
                    << (exists ? L" [exists]" : L" [missing]")
                    << L"\r\n";
                if (!exists)
                {
                    continue;
                }

                std::ifstream input(candidate, std::ios::binary);
                if (!input)
                {
                    continue;
                }
                const std::string source{
                    std::istreambuf_iterator<char>(input),
                    std::istreambuf_iterator<char>()};
                const std::string sourceName = candidate.string();
                byteCode.Reset();
                errors.Reset();
                lastResult = D3DCompile(
                    source.data(),
                    source.size(),
                    sourceName.c_str(),
                    macros,
                    nullptr,
                    entryPoint,
                    "cs_5_0",
                    compileFlags,
                    0u,
                    &byteCode,
                    &errors);
                if (SUCCEEDED(lastResult))
                {
                    break;
                }
            }

            if (FAILED(lastResult) || !byteCode)
            {
                std::wstringstream message;
                message << L"Novelty 01 shader compilation failed for "
                    << std::wstring(entryPoint, entryPoint + std::strlen(entryPoint))
                    << L" in binding mode " << bindingMode << L".\r\n";
                if (errors)
                {
                    const std::string narrow(
                        static_cast<const char*>(errors->GetBufferPointer()),
                        errors->GetBufferSize());
                    message << std::wstring(narrow.begin(), narrow.end()) << L"\r\n";
                }
                message << L"Tried paths:\r\n" << triedPaths.str();
                errorText = message.str();
                return false;
            }

            if (FAILED(device->CreateComputeShader(
                    byteCode->GetBufferPointer(),
                    byteCode->GetBufferSize(),
                    nullptr,
                    &destination)))
            {
                errorText = L"Failed to create a Novelty 01 compute shader object.";
                return false;
            }
            return true;
        }

        template <typename TValue>
        std::vector<TValue> N01AtLeastOne(std::vector<TValue> values)
        {
            if (values.empty())
            {
                values.emplace_back();
            }
            return values;
        }
    }

    struct GpuHairViewer::Novelty01RuntimeState
    {
        struct ReadbackSlot
        {
            Microsoft::WRL::ComPtr<ID3D11Buffer> counters;
            Microsoft::WRL::ComPtr<ID3D11Buffer> dynamicList;
            Microsoft::WRL::ComPtr<ID3D11Buffer> repairList;
            Microsoft::WRL::ComPtr<ID3D11Buffer> formalMetrics;
            uint32_t sourceFrame = 0u;
            uint32_t ageFrames = 0u;
            bool issued = false;
            bool formalMetricsIssued = false;
        };

        bool ready = false;
        bool initialized = false;
        bool latestCountersValid = false;
        bool forceAllFollowers = true;
        bool lastFrameUsedFullDynamic = true;
        bool usesResampledRestPositions = false;
        bool formalGroundTruthEnabled = false;
        bool formalTelemetryEnabled = true;
        bool formalGroundTruthValidThisFrame = false;
        bool indirectDispatchReady = false;

        hairgpu::novelty01::ExperimentVariant variant =
            hairgpu::novelty01::ExperimentVariant::P;
        uint64_t featureMask = hairgpu::novelty01::kAllFeatures;
        hairgpu::novelty01::FormalAggregate formalAggregate = {};

        uint32_t observedSimulationResetVersion = 0u;
        uint32_t observedTopologyVersion = 0u;
        uint32_t rootTransformVersion = 1u;
        uint32_t forceVersion = 1u;
        uint32_t colliderVersion = 1u;
        uint32_t topologyVersion = 1u;
        uint32_t editVersion = 1u;
        uint32_t dependencyVersion = 1u;
        uint32_t groupCount = 0u;
        uint32_t blockCount = 0u;
        uint32_t scanBlockCount = 0u;
        uint32_t followerScanBlockCount = 0u;
        uint32_t payloadCapacity = 0u;
        uint32_t payloadReadIndex = 0u;
        uint32_t initialFullDynamicFramesRemaining = 8u;
        uint32_t fullDynamicHoldFrames = 0u;
        uint32_t readbackWriteIndex = 0u;
        uint32_t latestCounterSourceFrame = 0u;
        uint32_t lastFastPathDecisionSourceFrame = 0xffffffffu;
        uint32_t groundTruthOutputBufferIndex = 0u;
        uint32_t groundTruthIntervalFrames = 16u;
        uint32_t fastPathEnterCounter = 0u;
        uint32_t fastPathExitCounter = 0u;
        uint32_t payloadGeneration = 0u;
        uint32_t initialPayloadActiveCount = 0u;
        uint32_t designScenarioIndex = 0u;
        uint32_t scenarioFlags = 0u;
        uint32_t scenarioTargetGroup = hairgpu::novelty01::kInvalidIndex;
        hairgpu::Vec3 scenarioRootTranslation = hairgpu::Vec3(0.0f, 0.0f, 0.0f);
        float scenarioRootYawRadians = 0.0f;

        uint64_t rootHash = 0ull;
        uint64_t forceHash = 0ull;
        uint64_t colliderHash = 0ull;

        float proposalMillisecondsEwma = 0.0f;
        float fullDynamicMillisecondsEwma = 0.0f;
        float dynamicRatioThreshold = 0.65f;
        uint32_t lastConsumedGpuTimingSourceFrame = 0u;
        bool hasConsumedGpuTiming = false;
        std::array<uint8_t, 256u> fullDynamicBySourceFrame = {};

        hairgpu::Vec3 previousColliderBoundsMin =
            hairgpu::Vec3(1.0e9f, 1.0e9f, 1.0e9f);
        hairgpu::Vec3 previousColliderBoundsMax =
            hairgpu::Vec3(-1.0e9f, -1.0e9f, -1.0e9f);

        N01Constants constants = {};
        hairgpu::novelty01::RuntimeCounterSnapshot latestCounters = {};

        std::vector<N01Core> cpuCores;
        std::vector<N01Block> cpuBlocks;
        std::vector<uint32_t> followerOffsets;
        std::vector<uint32_t> followerEntries;
        std::vector<uint32_t> activeGuides;
        std::vector<uint32_t> activeFollowers;
        // Interactive thesis visualization only. Formal runs force Natural
        // rendering and never consume this CPU-side diagnostic classification.
        std::vector<uint8_t> latestThesisUpdateCategories;
        uint32_t latestThesisUpdateSourceFrame = 0u;
        bool latestThesisUpdateCategoriesValid = false;
        std::vector<GpuParticle> previousScalpCpu;
        std::vector<GpuParticle> proposalRestCpu;
        std::vector<N01PayloadOwner> zeroPayloadOwners;
        std::vector<N01GuideDirtyRange> zeroGuideDirtyRanges;
        std::vector<N01FollowerDirtyRange> zeroFollowerDirtyRanges;
        std::vector<N01RenderDirtyRange> zeroRenderDirtyRanges;
        std::vector<N01GroundTruthMetric> zeroGroundTruthMetrics;

        StructuredBuffer cores;
        StructuredBuffer extensions;
        StructuredBuffer caches;
        StructuredBuffer descriptors;
        StructuredBuffer blocks;
        StructuredBuffer flags;
        StructuredBuffer prefix;
        StructuredBuffer scanBlockCounts;
        StructuredBuffer scanBlockOffsets;
        StructuredBuffer followerFlags;
        StructuredBuffer followerPrefix;
        StructuredBuffer followerScanBlockCounts;
        StructuredBuffer followerScanBlockOffsets;
        StructuredBuffer dynamicList;
        StructuredBuffer staticList;
        StructuredBuffer repairList;
        StructuredBuffer invalidList;
        StructuredBuffer payloads[hairgpu::novelty01::kPayloadGenerationCount];
        StructuredBuffer payloadOwners[hairgpu::novelty01::kPayloadGenerationCount];
        StructuredBuffer counters;
        StructuredBuffer sharedCandidates;
        StructuredBuffer hashHeads;
        StructuredBuffer particleNext;
        StructuredBuffer activeFollowerList;
        StructuredBuffer guideDirtyRanges;
        StructuredBuffer followerDirtyRanges;
        StructuredBuffer renderDirtyRanges;
        StructuredBuffer migratingDynamicCores;
        StructuredBuffer migratingStaticCores;
        StructuredBuffer groundTruthMetrics;
        StructuredBuffer groundTruthPositions[2];
        StructuredBuffer groundTruthPreviousPositions[2];
        StructuredBuffer groundTruthDistanceLambdas;
        StructuredBuffer groundTruthBendLambdas;
        StructuredBuffer previousScalp;
        StructuredBuffer proposalRestPositions;

        Microsoft::WRL::ComPtr<ID3D11Buffer> indirectArguments;
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> indirectArgumentsUav;
        Microsoft::WRL::ComPtr<ID3D11Buffer> constantBuffer;

        Microsoft::WRL::ComPtr<ID3D11ComputeShader> initializeCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> buildBlocksCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> invalidateBlocksCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> describeClassifyCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> finalizeBlocksCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> applyTransitionsCS;

        Microsoft::WRL::ComPtr<ID3D11ComputeShader> scanLocalCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> scanBlocksCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> scatterListsCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> buildSleepingIdentityListCS;

        Microsoft::WRL::ComPtr<ID3D11ComputeShader> carryNonOptionalPayloadsCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> remapPayloadCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> applyStaticCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> repairCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> updateCacheCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> shadowValidateCS;

        Microsoft::WRL::ComPtr<ID3D11ComputeShader> clearDynamicGuideLambdasCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> integrateDynamicGuidesCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> distanceDynamicGuidesCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> bendDynamicGuidesCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> collisionDynamicGuidesCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> stabilityGuardDynamicGuidesCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> integrateDynamicCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> distanceConstraintCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> bendConstraintCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> collisionCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> clearHashCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> buildHashCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> selfCollisionCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> velocityRecoveryCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> watchdogCS;

        Microsoft::WRL::ComPtr<ID3D11ComputeShader> rootBindCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> followerLocalCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> buildFollowerFlagsCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> buildSharedCandidatesCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> resolveSharedPointsCS;

        Microsoft::WRL::ComPtr<ID3D11ComputeShader> validatePayloadOwnersCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> writePayloadOwnersCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> retirePayloadOwnersCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> clearGuideDirtyRangesCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> buildGuideDirtyRangesCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> propagateFollowerRenderRangesCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> finalizeGuideDirtyRangeCountersCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> compareGroundTruthCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> verifyInvariantsCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> buildIndirectArgumentsCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> migrateCoreBuffersCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> clearAtomicCompactionCountersCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> atomicCompactCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> injectDesignScenarioCS;

        struct FormalTelemetryRow
        {
            uint32_t frame = 0u;
            uint32_t sourceFrame = 0u;
            uint32_t variant = 0u;
            uint64_t featureMask = 0u;
            float proposalMillisecondsEwma = 0.0f;
            float fullDynamicMillisecondsEwma = 0.0f;
            uint32_t usedFullDynamic = 0u;
            std::array<uint32_t, hairgpu::novelty01::kCounterCount> counters = {};
            uint64_t groundTruthSamples = 0u;
            uint64_t falseStaticSamples = 0u;
            uint64_t falseDynamicSamples = 0u;
            double positionRmseMm = 0.0;
            double positionMaxMm = 0.0;
            double followerErrorMm = 0.0;
            double renderErrorMm = 0.0;
        };

        std::vector<FormalTelemetryRow> formalTelemetryRows;
        std::filesystem::path formalTelemetryDirectory;
        std::wstring formalTelemetryRunName;

        void FlushFormalTelemetry() noexcept
        {
            if (formalTelemetryRows.empty() || formalTelemetryDirectory.empty())
            {
                return;
            }
            try
            {
                std::error_code error;
                std::filesystem::create_directories(formalTelemetryDirectory, error);
                if (error)
                {
                    return;
                }
                const std::wstring runName = formalTelemetryRunName.empty()
                    ? std::wstring(L"novelty01")
                    : formalTelemetryRunName;
                const std::filesystem::path csvPath =
                    formalTelemetryDirectory / (runName + L"_novelty01.csv");
                const std::filesystem::path jsonlPath =
                    formalTelemetryDirectory / (runName + L"_novelty01.jsonl");
                const std::filesystem::path summaryPath =
                    formalTelemetryDirectory / (runName + L"_novelty01_summary.json");

                std::ofstream csv(csvPath, std::ios::trunc);
                if (csv)
                {
                    csv <<
                        "schema_version,frame,source_frame,variant,feature_mask,"
                        "proposal_ms_ewma,full_dynamic_ms_ewma,used_full_dynamic";
                    for (uint32_t i = 0u;
                         i < hairgpu::novelty01::kCounterCount;
                         ++i)
                    {
                        csv << ",counter_" << i;
                    }
                    csv <<
                        ",ground_truth_samples,false_static_samples,false_dynamic_samples,"
                        "position_rmse_mm,position_max_mm,"
                        "follower_error_mm,render_error_mm\n";
                    for (const FormalTelemetryRow& row : formalTelemetryRows)
                    {
                        csv <<
                            "n01.v3," << row.frame << ',' << row.sourceFrame << ',' <<
                            row.variant << ',' << row.featureMask << ',' <<
                            row.proposalMillisecondsEwma << ',' <<
                            row.fullDynamicMillisecondsEwma << ',' <<
                            row.usedFullDynamic;
                        for (uint32_t value : row.counters)
                        {
                            csv << ',' << value;
                        }
                        csv << ',' << row.groundTruthSamples << ',' <<
                            row.falseStaticSamples << ',' << row.falseDynamicSamples << ',' <<
                            row.positionRmseMm << ',' <<
                            row.positionMaxMm << ',' << row.followerErrorMm << ',' <<
                            row.renderErrorMm << '\n';
                    }
                }

                std::ofstream jsonl(jsonlPath, std::ios::trunc);
                if (jsonl)
                {
                    for (const FormalTelemetryRow& row : formalTelemetryRows)
                    {
                        jsonl <<
                            "{\"schema\":\"n01.v3\",\"frame\":" << row.frame <<
                            ",\"source_frame\":" << row.sourceFrame <<
                            ",\"variant\":" << row.variant <<
                            ",\"feature_mask\":" << row.featureMask <<
                            ",\"proposal_ms_ewma\":" << row.proposalMillisecondsEwma <<
                            ",\"full_dynamic_ms_ewma\":" << row.fullDynamicMillisecondsEwma <<
                            ",\"used_full_dynamic\":" <<
                                (row.usedFullDynamic != 0u ? "true" : "false") <<
                            ",\"ground_truth\":{\"samples\":" << row.groundTruthSamples <<
                            ",\"false_static\":" << row.falseStaticSamples <<
                            ",\"false_dynamic\":" << row.falseDynamicSamples <<
                            ",\"rmse_mm\":" << row.positionRmseMm <<
                            ",\"max_mm\":" << row.positionMaxMm <<
                            "},\"counters\":[";
                        for (size_t i = 0u; i < row.counters.size(); ++i)
                        {
                            if (i > 0u) jsonl << ',';
                            jsonl << row.counters[i];
                        }
                        jsonl << "]}\n";
                    }
                }

                const FormalTelemetryRow& last = formalTelemetryRows.back();
                std::ofstream summary(summaryPath, std::ios::trunc);
                if (summary)
                {
                    summary <<
                        "{\n"
                        "  \"schema\": \"n01.v3\",\n"
                        "  \"base_commit\": "
                        "\"8d19bb546f3505fe25436d6ffa5b390a9b83897a\",\n"
                        "  \"variant\": " << last.variant << ",\n"
                        "  \"feature_mask\": " << last.featureMask << ",\n"
                        "  \"row_count\": " << formalTelemetryRows.size() << ",\n"
                        "  \"ground_truth_samples\": " << last.groundTruthSamples << ",\n"
                        "  \"false_static_count\": " << last.falseStaticSamples << ",\n"
                        "  \"false_dynamic_count\": " << last.falseDynamicSamples << ",\n"
                        "  \"position_rmse_mm\": " << last.positionRmseMm << ",\n"
                        "  \"position_max_mm\": " << last.positionMaxMm << "\n"
                        "}\n";
                }
            }
            catch (...)
            {
            }
        }

        ~Novelty01RuntimeState()
        {
            FlushFormalTelemetry();
        }

        std::array<ReadbackSlot, 8u> readbacks = {};
    };

    namespace
    {
        using N01RuntimePointer =
            std::shared_ptr<GpuHairViewer::Novelty01RuntimeState>;

        std::unordered_map<GpuHairViewer*, N01RuntimePointer>&
        N01RuntimeInstances()
        {
            static std::unordered_map<GpuHairViewer*, N01RuntimePointer> instances;
            return instances;
        }

        N01RuntimePointer N01FindRuntime(GpuHairViewer* viewer)
        {
            auto& instances = N01RuntimeInstances();
            const auto found = instances.find(viewer);
            return found != instances.end()
                ? found->second
                : N01RuntimePointer{};
        }
    }

    void GpuHairViewer::ResetNovelty01RuntimeIfNeeded()
    {
        const N01RuntimePointer runtime = N01FindRuntime(this);
        if (!runtime)
        {
            return;
        }
        if (runtime->observedSimulationResetVersion !=
                m_simulationResetVersion ||
            runtime->observedTopologyVersion !=
                m_asset.segmentGroupTopology.topologyVersion)
        {
            N01RuntimeInstances().erase(this);
        }
    }

    bool GpuHairViewer::EnsureNovelty01Runtime(float dt)
    {
        (void)dt;
        ResetNovelty01RuntimeIfNeeded();
        const N01RuntimePointer existing = N01FindRuntime(this);
        if (existing && existing->ready)
        {
            return true;
        }

        if (!m_device || !m_context ||
            m_asset.guideStrandCount == 0u ||
            m_asset.particlesPerStrand < 2u ||
            m_asset.restPoints.empty())
        {
            SetFatalExperimentFailure(
                1401u,
                L"Novelty 01 cannot initialize without a valid guide topology.");
            return false;
        }

        std::shared_ptr<Novelty01RuntimeState> runtime =
            std::make_shared<Novelty01RuntimeState>();
        runtime->variant = N01ParseVariant(
            GetEnvironmentVariableString(L"HAIRGPU_N01_VARIANT"));
        runtime->designScenarioIndex = N01ParseDesignScenarioIndex(
            GetEnvironmentVariableString(L"HAIRGPU_N01_DESIGN_SCENARIO"),
            static_cast<uint32_t>(m_experimentCase.scenarioId));
        runtime->featureMask =
            N01ReadFeatureMaskForVariant(runtime->variant);
        runtime->formalGroundTruthEnabled =
            N01EnvironmentEnabled(L"HAIRGPU_N01_GROUND_TRUTH", false);
        runtime->formalTelemetryEnabled =
            N01EnvironmentEnabled(L"HAIRGPU_N01_FORMAL_TELEMETRY", false);
        const std::wstring groundTruthIntervalText =
            GetEnvironmentVariableString(L"HAIRGPU_N01_GROUND_TRUTH_INTERVAL");
        if (!groundTruthIntervalText.empty())
        {
            wchar_t* intervalEnd = nullptr;
            const unsigned long intervalValue =
                std::wcstoul(
                    groundTruthIntervalText.c_str(),
                    &intervalEnd,
                    10);
            if (intervalEnd != groundTruthIntervalText.c_str())
            {
                runtime->groundTruthIntervalFrames =
                    std::clamp<uint32_t>(
                        static_cast<uint32_t>(intervalValue),
                        1u,
                        600u);
            }
        }
        if (runtime->formalGroundTruthEnabled)
        {
            runtime->featureMask |=
                hairgpu::novelty01::Feature_GroundTruth;
        }
        runtime->observedSimulationResetVersion = m_simulationResetVersion;
        runtime->observedTopologyVersion = m_asset.segmentGroupTopology.topologyVersion;
        runtime->topologyVersion = std::max<uint32_t>(m_asset.segmentGroupTopology.topologyVersion, 1u);
        runtime->editVersion = std::max<uint32_t>(m_editVersion, 1u);
        runtime->rootTransformVersion = std::max<uint32_t>(m_headTransformVersion, 1u);
        runtime->colliderVersion = std::max<uint32_t>(m_colliderVersion, 1u);
        runtime->forceVersion = 1u;
        runtime->dependencyVersion = 1u;
        runtime->rootHash = N01HashScalp(m_animatedScalpParticles);
        runtime->forceHash = N01HashSimulationForces(m_simConfig);
        runtime->colliderHash = ComputeColliderStateHash();

        const uint32_t guideCount = std::min<uint32_t>(
            m_asset.guideStrandCount,
            static_cast<uint32_t>(m_asset.strands.size()));
        const uint32_t particlesPerStrand = m_asset.particlesPerStrand;
        const uint32_t groupsPerBlock =
            hairgpu::novelty01::kDefaultGroupsPerBlock;
        const uint32_t invalidIndex = hairgpu::novelty01::kInvalidIndex;

        const bool uniformRestLength =
            EnvironmentValueIsTrue(
                GetEnvironmentVariableString(
                    L"HAIRGPU_N01_UNIFORM_REST_LENGTH"));
        runtime->usesResampledRestPositions = uniformRestLength;
        if (uniformRestLength)
        {
            runtime->proposalRestCpu.resize(m_asset.restPoints.size());
            for (size_t pointIndex = 0u;
                 pointIndex < m_asset.restPoints.size();
                 ++pointIndex)
            {
                const hairgpu::Vec3& point = m_asset.restPoints[pointIndex];
                runtime->proposalRestCpu[pointIndex] =
                    GpuParticle{ point.x, point.y, point.z, 1.0f };
            }
        }

        for (uint32_t guideIndex = 0u;
             guideIndex < guideCount;
             ++guideIndex)
        {
            const hairgpu::StrandRange& strand = m_asset.strands[guideIndex];
            if (strand.pointCount < 2u ||
                static_cast<size_t>(strand.pointOffset) + strand.pointCount >
                    m_asset.restPoints.size())
            {
                SetFatalExperimentFailure(
                    1402u,
                    L"Novelty 01 guide topology references an invalid point range.");
                return false;
            }

            if (uniformRestLength)
            {
                std::vector<float> cumulative(strand.pointCount, 0.0f);
                for (uint32_t localPoint = 1u;
                     localPoint < strand.pointCount;
                     ++localPoint)
                {
                    const hairgpu::Vec3& a =
                        m_asset.restPoints[strand.pointOffset + localPoint - 1u];
                    const hairgpu::Vec3& b =
                        m_asset.restPoints[strand.pointOffset + localPoint];
                    cumulative[localPoint] =
                        cumulative[localPoint - 1u] +
                        std::max(hairgpu::Length(b - a), 1.0e-7f);
                }
                const float totalLength = cumulative.back();
                if (totalLength > 1.0e-6f)
                {
                    uint32_t sourceSegment = 0u;
                    for (uint32_t localPoint = 0u;
                         localPoint < strand.pointCount;
                         ++localPoint)
                    {
                        const float target =
                            totalLength *
                            static_cast<float>(localPoint) /
                            static_cast<float>(strand.pointCount - 1u);
                        while (sourceSegment + 1u < strand.pointCount - 1u &&
                               cumulative[sourceSegment + 1u] < target)
                        {
                            ++sourceSegment;
                        }
                        const float beginDistance = cumulative[sourceSegment];
                        const float endDistance = cumulative[sourceSegment + 1u];
                        const float t =
                            endDistance > beginDistance
                            ? std::clamp(
                                (target - beginDistance) /
                                (endDistance - beginDistance),
                                0.0f,
                                1.0f)
                            : 0.0f;
                        const hairgpu::Vec3& a =
                            m_asset.restPoints[strand.pointOffset + sourceSegment];
                        const hairgpu::Vec3& b =
                            m_asset.restPoints[strand.pointOffset + sourceSegment + 1u];
                        const hairgpu::Vec3 p = a + (b - a) * t;
                        runtime->proposalRestCpu[strand.pointOffset + localPoint] =
                            GpuParticle{ p.x, p.y, p.z, 1.0f };
                    }
                }
            }

            const uint32_t firstGroup =
                static_cast<uint32_t>(runtime->cpuCores.size());
            const uint32_t strandGroupCount = strand.pointCount - 1u;
            const uint32_t firstBlock =
                static_cast<uint32_t>(runtime->cpuBlocks.size());

            for (uint32_t blockStart = 0u;
                 blockStart < strandGroupCount;
                 blockStart += groupsPerBlock)
            {
                N01Block block = {};
                block.firstGroupIndex = firstGroup + blockStart;
                block.groupCount = std::min(
                    groupsPerBlock,
                    strandGroupCount - blockStart);
                block.dirtyFlags =
                    hairgpu::novelty01::Dirty_Topology |
                    hairgpu::novelty01::Dirty_DescriptorAge;
                runtime->cpuBlocks.push_back(block);
            }

            for (uint32_t localGroup = 0u;
                 localGroup < strandGroupCount;
                 ++localGroup)
            {
                const uint32_t groupIndex =
                    static_cast<uint32_t>(runtime->cpuCores.size());
                const uint32_t startPoint = strand.pointOffset + localGroup;
                const uint32_t endPoint = startPoint + 1u;
                const hairgpu::Vec3 start = uniformRestLength
                    ? hairgpu::Vec3(
                        runtime->proposalRestCpu[startPoint].x,
                        runtime->proposalRestCpu[startPoint].y,
                        runtime->proposalRestCpu[startPoint].z)
                    : m_asset.restPoints[startPoint];
                const hairgpu::Vec3 end = uniformRestLength
                    ? hairgpu::Vec3(
                        runtime->proposalRestCpu[endPoint].x,
                        runtime->proposalRestCpu[endPoint].y,
                        runtime->proposalRestCpu[endPoint].z)
                    : m_asset.restPoints[endPoint];
                const hairgpu::Vec3 delta = end - start;
                const float lengthValue =
                    std::max(hairgpu::Length(delta), 1.0e-6f);
                const hairgpu::Vec3 direction = delta * (1.0f / lengthValue);

                N01Core core = {};
                core.strandId = guideIndex;
                core.localGroupIndex = localGroup;
                core.startPointIndex = startPoint;
                core.endPointIndex = endPoint;
                core.parentGroupIndex =
                    localGroup > 0u ? groupIndex - 1u : invalidIndex;
                core.childGroupIndex =
                    localGroup + 1u < strandGroupCount
                    ? groupIndex + 1u
                    : invalidIndex;
                core.ownerGroupIndex =
                    localGroup > 0u ? groupIndex - 1u : groupIndex;
                core.blockIndex = firstBlock + localGroup / groupsPerBlock;
                core.restLength = lengthValue;
                core.restDirection[0] = direction.x;
                core.restDirection[1] = direction.y;
                core.restDirection[2] = direction.z;
                core.state = static_cast<uint32_t>(N01State::Dynamic);
                core.previousState = core.state;
                core.dirtyFlags =
                    hairgpu::novelty01::Dirty_Topology |
                    hairgpu::novelty01::Dirty_DescriptorAge;
                core.flags = uniformRestLength
                    ? hairgpu::novelty01::CoreFlag_UniformRestLength
                    : hairgpu::novelty01::CoreFlag_None;
                runtime->cpuCores.push_back(core);
            }
        }

        runtime->groupCount =
            static_cast<uint32_t>(runtime->cpuCores.size());
        runtime->blockCount =
            static_cast<uint32_t>(runtime->cpuBlocks.size());
        runtime->scanBlockCount =
            N01DispatchGroups(runtime->groupCount, 256u);
        runtime->followerScanBlockCount =
            N01DispatchGroups(
                std::max<uint32_t>(m_asset.followerStrandCount, 1u),
                256u);

        if (runtime->groupCount == 0u || runtime->blockCount == 0u)
        {
            SetFatalExperimentFailure(
                1403u,
                L"Novelty 01 produced an empty guide Segment Group topology.");
            return false;
        }

        uint32_t payloadPercent = 50u;
        const std::wstring payloadPercentText =
            GetEnvironmentVariableString(L"HAIRGPU_N01_PAYLOAD_PERCENT");
        if (!payloadPercentText.empty())
        {
            wchar_t* end = nullptr;
            const unsigned long parsed =
                std::wcstoul(payloadPercentText.c_str(), &end, 10);
            if (end != payloadPercentText.c_str())
            {
                payloadPercent = std::clamp<uint32_t>(
                    static_cast<uint32_t>(parsed),
                    5u,
                    100u);
            }
        }
        if ((runtime->featureMask &
             hairgpu::novelty01::Feature_OptionalPayload) == 0u)
        {
            payloadPercent = 100u;
        }
        runtime->payloadCapacity = std::max<uint32_t>(
            1u,
            static_cast<uint32_t>(
                (static_cast<uint64_t>(runtime->groupCount) *
                 payloadPercent + 99ull) /
                100ull));

        const uint32_t totalStrandCount =
            static_cast<uint32_t>(m_asset.strands.size());
        runtime->followerOffsets.assign(guideCount + 1u, 0u);
        std::vector<std::vector<uint32_t>> followersByGuide(guideCount);
        for (uint32_t strandIndex = guideCount;
             strandIndex < totalStrandCount &&
             strandIndex < m_followGpuData.size();
             ++strandIndex)
        {
            const int32_t parent = m_followGpuData[strandIndex].parentGuide;
            if (parent >= 0 && static_cast<uint32_t>(parent) < guideCount)
            {
                followersByGuide[static_cast<uint32_t>(parent)].push_back(strandIndex);
            }
        }
        for (uint32_t guideIndex = 0u; guideIndex < guideCount; ++guideIndex)
        {
            runtime->followerOffsets[guideIndex] =
                static_cast<uint32_t>(runtime->followerEntries.size());
            runtime->followerEntries.insert(
                runtime->followerEntries.end(),
                followersByGuide[guideIndex].begin(),
                followersByGuide[guideIndex].end());
        }
        runtime->followerOffsets[guideCount] =
            static_cast<uint32_t>(runtime->followerEntries.size());

        std::vector<N01Extension> extensions(runtime->groupCount);
        std::vector<N01Cache> caches(runtime->groupCount);
        std::vector<N01Descriptor> descriptors(runtime->groupCount);
        std::vector<std::array<uint32_t, 4>> zeroFlags(runtime->groupCount);
        std::vector<std::array<uint32_t, 4>> zeroPrefix(runtime->groupCount);
        std::vector<std::array<uint32_t, 4>> zeroScanBlocks(
            std::max<uint32_t>(runtime->scanBlockCount, 1u));
        std::vector<std::array<uint32_t, 4>> zeroFollowerFlags(
            std::max<uint32_t>(m_asset.followerStrandCount, 1u));
        std::vector<std::array<uint32_t, 4>> zeroFollowerScanBlocks(
            std::max<uint32_t>(runtime->followerScanBlockCount, 1u));
        std::vector<uint32_t> zeroLists(runtime->groupCount, 0u);
        std::vector<N01Payload> zeroPayloads(runtime->payloadCapacity);
        runtime->zeroPayloadOwners.assign(
            runtime->payloadCapacity,
            N01PayloadOwner{});
        runtime->zeroGuideDirtyRanges.assign(
            std::max<uint32_t>(guideCount, 1u),
            N01GuideDirtyRange{});
        for (N01GuideDirtyRange& range : runtime->zeroGuideDirtyRanges)
        {
            range.segmentBegin = invalidIndex;
            range.dependencyVersion = runtime->dependencyVersion;
        }
        runtime->zeroFollowerDirtyRanges.assign(
            std::max<uint32_t>(m_asset.followerStrandCount, 1u),
            N01FollowerDirtyRange{});
        for (uint32_t followerIndex = 0u;
             followerIndex < runtime->zeroFollowerDirtyRanges.size();
             ++followerIndex)
        {
            N01FollowerDirtyRange& range =
                runtime->zeroFollowerDirtyRanges[followerIndex];
            range.followerStrandIndex = guideCount + followerIndex;
            range.segmentBegin = invalidIndex;
            range.sourceGuideIndex = invalidIndex;
            range.dependencyVersion = runtime->dependencyVersion;
            range.guardBand = 1u;
        }
        runtime->zeroRenderDirtyRanges.assign(
            std::max<uint32_t>(m_asset.followerStrandCount, 1u),
            N01RenderDirtyRange{});
        for (uint32_t followerIndex = 0u;
             followerIndex < runtime->zeroRenderDirtyRanges.size();
             ++followerIndex)
        {
            runtime->zeroRenderDirtyRanges[followerIndex].strandIndex =
                guideCount + followerIndex;
            runtime->zeroRenderDirtyRanges[followerIndex].pointBegin =
                invalidIndex;
        }
        runtime->zeroGroundTruthMetrics.assign(
            runtime->groupCount,
            N01GroundTruthMetric{});
        std::vector<uint32_t> zeroCounters(
            hairgpu::novelty01::kCounterCount,
            0u);
        std::vector<N01Candidate> zeroCandidates(
            static_cast<size_t>(runtime->groupCount) * 2u);
        std::vector<uint32_t> invalidHashHeads(
            std::max<uint32_t>(m_simConfig.selfCollisionHashBucketCount, 1u),
            invalidIndex);
        std::vector<uint32_t> invalidParticleNext(
            std::max<uint32_t>(guideCount * particlesPerStrand, 1u),
            invalidIndex);
        std::vector<uint32_t> followerListStorage(
            std::max<uint32_t>(m_asset.followerStrandCount, 1u),
            guideCount);

        runtime->previousScalpCpu = m_animatedScalpParticles;
        if (runtime->previousScalpCpu.empty())
        {
            runtime->previousScalpCpu.emplace_back();
        }

        runtime->cores = CreateStructuredBuffer(
            sizeof(N01Core), runtime->groupCount,
            runtime->cpuCores.data(), true);
        runtime->extensions = CreateStructuredBuffer(
            sizeof(N01Extension), runtime->groupCount,
            extensions.data(), true);
        runtime->caches = CreateStructuredBuffer(
            sizeof(N01Cache), runtime->groupCount,
            caches.data(), true);
        runtime->descriptors = CreateStructuredBuffer(
            sizeof(N01Descriptor), runtime->groupCount,
            descriptors.data(), true);
        runtime->blocks = CreateStructuredBuffer(
            sizeof(N01Block), runtime->blockCount,
            runtime->cpuBlocks.data(), true);
        runtime->flags = CreateStructuredBuffer(
            sizeof(std::array<uint32_t, 4>), runtime->groupCount,
            zeroFlags.data(), true);
        runtime->prefix = CreateStructuredBuffer(
            sizeof(std::array<uint32_t, 4>), runtime->groupCount,
            zeroPrefix.data(), true);
        runtime->scanBlockCounts = CreateStructuredBuffer(
            sizeof(std::array<uint32_t, 4>),
            std::max<uint32_t>(runtime->scanBlockCount, 1u),
            zeroScanBlocks.data(), true);
        runtime->scanBlockOffsets = CreateStructuredBuffer(
            sizeof(std::array<uint32_t, 4>),
            std::max<uint32_t>(runtime->scanBlockCount, 1u),
            zeroScanBlocks.data(), true);
        runtime->followerFlags = CreateStructuredBuffer(
            sizeof(std::array<uint32_t, 4>),
            static_cast<UINT>(zeroFollowerFlags.size()),
            zeroFollowerFlags.data(), true);
        runtime->followerPrefix = CreateStructuredBuffer(
            sizeof(std::array<uint32_t, 4>),
            static_cast<UINT>(zeroFollowerFlags.size()),
            zeroFollowerFlags.data(), true);
        runtime->followerScanBlockCounts = CreateStructuredBuffer(
            sizeof(std::array<uint32_t, 4>),
            static_cast<UINT>(zeroFollowerScanBlocks.size()),
            zeroFollowerScanBlocks.data(), true);
        runtime->followerScanBlockOffsets = CreateStructuredBuffer(
            sizeof(std::array<uint32_t, 4>),
            static_cast<UINT>(zeroFollowerScanBlocks.size()),
            zeroFollowerScanBlocks.data(), true);
        runtime->dynamicList = CreateStructuredBuffer(
            sizeof(uint32_t), runtime->groupCount,
            zeroLists.data(), true);
        runtime->staticList = CreateStructuredBuffer(
            sizeof(uint32_t), runtime->groupCount,
            zeroLists.data(), true);
        runtime->repairList = CreateStructuredBuffer(
            sizeof(uint32_t), runtime->groupCount,
            zeroLists.data(), true);
        runtime->invalidList = CreateStructuredBuffer(
            sizeof(uint32_t), runtime->groupCount,
            zeroLists.data(), true);
        for (uint32_t payloadGeneration = 0u;
             payloadGeneration <
                 hairgpu::novelty01::kPayloadGenerationCount;
             ++payloadGeneration)
        {
            runtime->payloads[payloadGeneration] = CreateStructuredBuffer(
                sizeof(N01Payload), runtime->payloadCapacity,
                zeroPayloads.data(), true);
            runtime->payloadOwners[payloadGeneration] =
                CreateStructuredBuffer(
                    sizeof(N01PayloadOwner),
                    runtime->payloadCapacity,
                    runtime->zeroPayloadOwners.data(),
                    true);
        }
        runtime->counters = CreateStructuredBuffer(
            sizeof(uint32_t),
            hairgpu::novelty01::kCounterCount,
            zeroCounters.data(), true);
        runtime->sharedCandidates = CreateStructuredBuffer(
            sizeof(N01Candidate),
            static_cast<UINT>(zeroCandidates.size()),
            zeroCandidates.data(), true);
        runtime->hashHeads = CreateStructuredBuffer(
            sizeof(uint32_t),
            static_cast<UINT>(invalidHashHeads.size()),
            invalidHashHeads.data(), true);
        runtime->particleNext = CreateStructuredBuffer(
            sizeof(uint32_t),
            static_cast<UINT>(invalidParticleNext.size()),
            invalidParticleNext.data(), true);
        runtime->activeFollowerList = CreateStructuredBuffer(
            sizeof(uint32_t),
            static_cast<UINT>(followerListStorage.size()),
            followerListStorage.data(), true);
        runtime->guideDirtyRanges = CreateStructuredBuffer(
            sizeof(N01GuideDirtyRange),
            static_cast<UINT>(runtime->zeroGuideDirtyRanges.size()),
            runtime->zeroGuideDirtyRanges.data(),
            true);
        runtime->followerDirtyRanges = CreateStructuredBuffer(
            sizeof(N01FollowerDirtyRange),
            static_cast<UINT>(runtime->zeroFollowerDirtyRanges.size()),
            runtime->zeroFollowerDirtyRanges.data(),
            true);
        runtime->renderDirtyRanges = CreateStructuredBuffer(
            sizeof(N01RenderDirtyRange),
            static_cast<UINT>(runtime->zeroRenderDirtyRanges.size()),
            runtime->zeroRenderDirtyRanges.data(),
            true);
        runtime->migratingDynamicCores = CreateStructuredBuffer(
            sizeof(N01Core),
            runtime->groupCount,
            runtime->cpuCores.data(),
            true);
        runtime->migratingStaticCores = CreateStructuredBuffer(
            sizeof(N01Core),
            runtime->groupCount,
            runtime->cpuCores.data(),
            true);
        runtime->groundTruthMetrics = CreateStructuredBuffer(
            sizeof(N01GroundTruthMetric),
            runtime->groupCount,
            runtime->zeroGroundTruthMetrics.data(),
            true);
        runtime->previousScalp = CreateStructuredBuffer(
            sizeof(GpuParticle),
            static_cast<UINT>(runtime->previousScalpCpu.size()),
            runtime->previousScalpCpu.data(), true);
        if (runtime->usesResampledRestPositions)
        {
            runtime->proposalRestPositions = CreateStructuredBuffer(
                sizeof(GpuParticle),
                static_cast<UINT>(runtime->proposalRestCpu.size()),
                runtime->proposalRestCpu.data(), false);
        }

        if (runtime->formalGroundTruthEnabled)
        {
            std::vector<GpuParticle> groundTruthInitial =
                m_initialParticles;
            if (groundTruthInitial.empty())
            {
                groundTruthInitial.resize(m_asset.restPoints.size());
                for (size_t i = 0u; i < m_asset.restPoints.size(); ++i)
                {
                    const hairgpu::Vec3& value = m_asset.restPoints[i];
                    groundTruthInitial[i] =
                        GpuParticle{ value.x, value.y, value.z, 1.0f };
                }
            }
            for (uint32_t bufferIndex = 0u; bufferIndex < 2u; ++bufferIndex)
            {
                runtime->groundTruthPositions[bufferIndex] =
                    CreateStructuredBuffer(
                        sizeof(GpuParticle),
                        static_cast<UINT>(groundTruthInitial.size()),
                        groundTruthInitial.data(),
                        true);
                runtime->groundTruthPreviousPositions[bufferIndex] =
                    CreateStructuredBuffer(
                        sizeof(GpuParticle),
                        static_cast<UINT>(groundTruthInitial.size()),
                        groundTruthInitial.data(),
                        true);
            }
            const std::vector<float> groundTruthDistance =
                N01AtLeastOne(m_zeroDistanceLambdas);
            const std::vector<float> groundTruthBend =
                N01AtLeastOne(m_zeroBendLambdas);
            runtime->groundTruthDistanceLambdas =
                CreateStructuredBuffer(
                    sizeof(float),
                    static_cast<UINT>(groundTruthDistance.size()),
                    groundTruthDistance.data(),
                    true);
            runtime->groundTruthBendLambdas =
                CreateStructuredBuffer(
                    sizeof(float),
                    static_cast<UINT>(groundTruthBend.size()),
                    groundTruthBend.data(),
                    true);
        }

        {
            D3D11_BUFFER_DESC argumentDesc = {};
            argumentDesc.ByteWidth = 96u;
            argumentDesc.Usage = D3D11_USAGE_DEFAULT;
            argumentDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
            argumentDesc.MiscFlags =
                D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS |
                D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;
            if (SUCCEEDED(m_device->CreateBuffer(
                    &argumentDesc,
                    nullptr,
                    &runtime->indirectArguments)))
            {
                D3D11_UNORDERED_ACCESS_VIEW_DESC argumentUavDesc = {};
                argumentUavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
                argumentUavDesc.ViewDimension =
                    D3D11_UAV_DIMENSION_BUFFER;
                argumentUavDesc.Buffer.FirstElement = 0u;
                argumentUavDesc.Buffer.NumElements =
                    argumentDesc.ByteWidth / sizeof(uint32_t);
                argumentUavDesc.Buffer.Flags =
                    D3D11_BUFFER_UAV_FLAG_RAW;
                runtime->indirectDispatchReady =
                    SUCCEEDED(m_device->CreateUnorderedAccessView(
                        runtime->indirectArguments.Get(),
                        &argumentUavDesc,
                        &runtime->indirectArgumentsUav));
            }
        }

        const bool resourcesReady =
            runtime->cores.buffer && runtime->extensions.buffer &&
            runtime->caches.buffer && runtime->descriptors.buffer &&
            runtime->blocks.buffer && runtime->flags.buffer &&
            runtime->prefix.buffer && runtime->scanBlockCounts.buffer &&
            runtime->scanBlockOffsets.buffer && runtime->followerFlags.buffer &&
            runtime->followerPrefix.buffer &&
            runtime->followerScanBlockCounts.buffer &&
            runtime->followerScanBlockOffsets.buffer &&
            runtime->dynamicList.buffer &&
            runtime->staticList.buffer && runtime->repairList.buffer &&
            runtime->invalidList.buffer && runtime->payloads[0].buffer &&
            runtime->payloads[1].buffer && runtime->payloads[2].buffer &&
            runtime->payloads[3].buffer &&
            runtime->payloadOwners[0].buffer &&
            runtime->payloadOwners[1].buffer &&
            runtime->payloadOwners[2].buffer &&
            runtime->payloadOwners[3].buffer &&
            runtime->counters.buffer &&
            runtime->sharedCandidates.buffer && runtime->hashHeads.buffer &&
            runtime->particleNext.buffer && runtime->activeFollowerList.buffer &&
            runtime->guideDirtyRanges.buffer &&
            runtime->followerDirtyRanges.buffer &&
            runtime->renderDirtyRanges.buffer &&
            runtime->migratingDynamicCores.buffer &&
            runtime->migratingStaticCores.buffer &&
            runtime->groundTruthMetrics.buffer &&
            runtime->previousScalp.buffer &&
            runtime->indirectArguments &&
            runtime->indirectArgumentsUav &&
            (!runtime->formalGroundTruthEnabled ||
             (runtime->groundTruthPositions[0].buffer &&
              runtime->groundTruthPositions[1].buffer &&
              runtime->groundTruthPreviousPositions[0].buffer &&
              runtime->groundTruthPreviousPositions[1].buffer &&
              runtime->groundTruthDistanceLambdas.buffer &&
              runtime->groundTruthBendLambdas.buffer)) &&
            (!runtime->usesResampledRestPositions ||
             runtime->proposalRestPositions.buffer);
        if (!resourcesReady)
        {
            SetFatalExperimentFailure(
                1404u,
                L"Novelty 01 failed to create one or more GPU buffers.");
            return false;
        }

        D3D11_BUFFER_DESC constantDesc = {};
        constantDesc.ByteWidth = sizeof(N01Constants);
        constantDesc.Usage = D3D11_USAGE_DEFAULT;
        constantDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(m_device->CreateBuffer(
                &constantDesc, nullptr, &runtime->constantBuffer)))
        {
            SetFatalExperimentFailure(
                1405u,
                L"Novelty 01 failed to create its constant buffer.");
            return false;
        }

        std::wstring shaderError;
        bool shaderReady = true;
        const auto load = [&](const char* entry, uint32_t mode,
                              Microsoft::WRL::ComPtr<ID3D11ComputeShader>& target)
        {
            return N01CompileShader(m_device.Get(), entry, mode, target, shaderError);
        };

        shaderReady &= load("N01InitializeCS", 0u, runtime->initializeCS);
        shaderReady &= load("N01BuildBlocksCS", 0u, runtime->buildBlocksCS);
        shaderReady &= load("N01InvalidateBlocksCS", 0u, runtime->invalidateBlocksCS);
        shaderReady &= load("N01DescribeClassifyCS", 0u, runtime->describeClassifyCS);
        shaderReady &= load("N01FinalizeBlocksCS", 0u, runtime->finalizeBlocksCS);
        shaderReady &= load("N01ApplyTransitionsFinalFlagsCS", 0u, runtime->applyTransitionsCS);
        shaderReady &= load("N01ScanLocalCS", 1u, runtime->scanLocalCS);
        shaderReady &= load("N01ScanBlocksCS", 1u, runtime->scanBlocksCS);
        shaderReady &= load("N01ScatterListsCS", 1u, runtime->scatterListsCS);
        shaderReady &= load("N01BuildSleepingIdentityListCS", 1u, runtime->buildSleepingIdentityListCS);
        shaderReady &= load("N01CarryNonOptionalPayloadsCS", 2u, runtime->carryNonOptionalPayloadsCS);
        shaderReady &= load("N01RemapPayloadCS", 2u, runtime->remapPayloadCS);
        shaderReady &= load("N01ApplyStaticCS", 2u, runtime->applyStaticCS);
        shaderReady &= load("N01RepairCS", 2u, runtime->repairCS);
        shaderReady &= load("N01UpdateCacheCS", 2u, runtime->updateCacheCS);
        shaderReady &= load("N01ShadowValidateCS", 2u, runtime->shadowValidateCS);
        shaderReady &= load("N01ClearDynamicGuideLambdasCS", 3u, runtime->clearDynamicGuideLambdasCS);
        shaderReady &= load("N01IntegrateDynamicGuidesCS", 3u, runtime->integrateDynamicGuidesCS);
        shaderReady &= load("N01DistanceDynamicGuidesCS", 3u, runtime->distanceDynamicGuidesCS);
        shaderReady &= load("N01BendDynamicGuidesCS", 3u, runtime->bendDynamicGuidesCS);
        shaderReady &= load("N01CollisionDynamicGuidesCS", 3u, runtime->collisionDynamicGuidesCS);
        shaderReady &= load("N01StabilityGuardDynamicGuidesCS", 3u, runtime->stabilityGuardDynamicGuidesCS);
        shaderReady &= load("N01IntegrateDynamicCS", 3u, runtime->integrateDynamicCS);
        shaderReady &= load("N01DistanceConstraintCS", 3u, runtime->distanceConstraintCS);
        shaderReady &= load("N01BendConstraintCS", 3u, runtime->bendConstraintCS);
        shaderReady &= load("N01CollisionCS", 3u, runtime->collisionCS);
        shaderReady &= load("N01ClearHashCS", 3u, runtime->clearHashCS);
        shaderReady &= load("N01BuildHashCS", 3u, runtime->buildHashCS);
        shaderReady &= load("N01SelfCollisionCS", 3u, runtime->selfCollisionCS);
        shaderReady &= load("N01VelocityRecoveryCS", 3u, runtime->velocityRecoveryCS);
        shaderReady &= load("N01WatchdogCS", 3u, runtime->watchdogCS);
        shaderReady &= load("N01RootBindCS", 4u, runtime->rootBindCS);
        shaderReady &= load("N01FollowerLocalCS", 4u, runtime->followerLocalCS);
        shaderReady &= load("N01BuildFollowerFlagsCS", 5u, runtime->buildFollowerFlagsCS);
        shaderReady &= load("N01BuildSharedCandidatesCS", 6u, runtime->buildSharedCandidatesCS);
        shaderReady &= load("N01ResolveSharedPointsCS", 6u, runtime->resolveSharedPointsCS);
        shaderReady &= load("N01ValidatePayloadOwnersCS", 7u, runtime->validatePayloadOwnersCS);
        shaderReady &= load("N01WritePayloadOwnersCS", 7u, runtime->writePayloadOwnersCS);
        shaderReady &= load("N01RetirePayloadOwnersCS", 7u, runtime->retirePayloadOwnersCS);
        shaderReady &= load("N01ClearGuideDirtyRangesCS", 8u, runtime->clearGuideDirtyRangesCS);
        shaderReady &= load("N01BuildGuideDirtyRangesCS", 8u, runtime->buildGuideDirtyRangesCS);
        shaderReady &= load("N01PropagateFollowerRenderRangesCS", 8u, runtime->propagateFollowerRenderRangesCS);
        shaderReady &= load("N01FinalizeGuideDirtyRangeCountersCS", 8u, runtime->finalizeGuideDirtyRangeCountersCS);
        shaderReady &= load("N01CompareGroundTruthCS", 9u, runtime->compareGroundTruthCS);
        shaderReady &= load("N01VerifyInvariantsCS", 9u, runtime->verifyInvariantsCS);
        shaderReady &= load("N01BuildIndirectArgumentsCS", 10u, runtime->buildIndirectArgumentsCS);
        shaderReady &= load("N01MigrateCoreBuffersCS", 10u, runtime->migrateCoreBuffersCS);
        shaderReady &= load("N01ClearAtomicCompactionCountersCS", 11u, runtime->clearAtomicCompactionCountersCS);
        shaderReady &= load("N01AtomicCompactCS", 11u, runtime->atomicCompactCS);
        shaderReady &= load("N01InjectDesignScenarioCS", 12u, runtime->injectDesignScenarioCS);

        if (!shaderReady)
        {
            SetFatalExperimentFailure(1406u, shaderError);
            return false;
        }

        runtime->ready = true;
        N01RuntimeInstances()[this] = runtime;
        return true;
    }

    void GpuHairViewer::ApplyNovelty01DesignScenarioForTick(float dt)
    {
        const N01RuntimePointer runtimePointer = N01FindRuntime(this);
        if (!runtimePointer)
        {
            return;
        }
        Novelty01RuntimeState& runtime = *runtimePointer;
        runtime.scenarioFlags = 0u;
        runtime.scenarioTargetGroup = hairgpu::novelty01::kInvalidIndex;
        runtime.scenarioRootTranslation = hairgpu::Vec3(0.0f, 0.0f, 0.0f);
        runtime.scenarioRootYawRadians = 0.0f;

        const uint32_t scenario = runtime.designScenarioIndex;
        const uint32_t eventStart = std::max<uint32_t>(
            m_experimentCase.warmupFrames,
            1u);
        const float time = static_cast<float>(m_simulationTickIndex) * dt;
        bool eventApplied = false;

        switch (scenario)
        {
        case 0u:
            m_enableScalpMotion = false;
            m_simConfig.windStrength = 0.0f;
            break;
        case 1u:
            m_enableScalpMotion = false;
            m_simConfig.windStrength = 0.0f;
            runtime.scenarioRootTranslation.x = 0.06f * std::sin(time * 1.1f);
            eventApplied = true;
            break;
        case 2u:
            m_enableScalpMotion = false;
            m_simConfig.windStrength = 0.0f;
            runtime.scenarioRootYawRadians = 0.55f * std::sin(time * 0.85f);
            eventApplied = true;
            break;
        case 3u:
            m_enableScalpMotion = false;
            m_simConfig.windStrength = 0.18f;
            eventApplied = true;
            break;
        case 4u:
            m_enableScalpMotion = true;
            m_simConfig.windStrength = 0.08f;
            eventApplied = true;
            break;
        case 5u:
            m_enableScalpMotion = false;
            m_simConfig.windStrength = 0.04f;
            if (!m_colliders.spheres.empty())
            {
                hairgpu::SphereCollider& collider = m_colliders.spheres.front();
                collider.center = m_scalpCenter + hairgpu::Vec3(
                    0.11f + 0.035f * std::sin(time * 0.9f),
                    0.02f,
                    0.0f);
                eventApplied = true;
                ++m_scenarioCollisionCandidateCount;
            }
            break;
        case 6u:
            m_enableScalpMotion = true;
            m_simConfig.windStrength = 0.12f;
            for (size_t index = 0u; index < m_colliders.spheres.size(); ++index)
            {
                const float phase = time * (0.55f + 0.11f * static_cast<float>(index));
                m_colliders.spheres[index].center.x += 0.0008f * std::sin(phase);
                m_colliders.spheres[index].center.z += 0.0008f * std::cos(phase);
            }
            eventApplied = !m_colliders.spheres.empty() || !m_colliders.capsules.empty();
            m_scenarioCollisionCandidateCount += eventApplied ? 1u : 0u;
            break;
        case 7u:
            if (m_simulationTickIndex == eventStart)
            {
                runtime.scenarioFlags |= 1u;
                runtime.scenarioTargetGroup = std::min<uint32_t>(
                    1000u * (m_asset.particlesPerStrand - 1u) + 63u,
                    std::max<uint32_t>(runtime.groupCount, 1u) - 1u);
                eventApplied = true;
                ++m_scenarioEditInvalidationCount;
            }
            break;
        case 8u:
            runtime.scenarioFlags |= 2u;
            runtime.scenarioTargetGroup = std::min<uint32_t>(
                ((m_simulationTickIndex / 8u) % std::max(m_asset.guideStrandCount, 1u)) *
                    (m_asset.particlesPerStrand - 1u) + 40u,
                std::max<uint32_t>(runtime.groupCount, 1u) - 1u);
            eventApplied = true;
            ++m_scenarioEditInvalidationCount;
            break;
        case 9u:
            m_enableScalpMotion = true;
            m_simConfig.windStrength = 1.20f;
            eventApplied = true;
            break;
        case 10u:
            runtime.scenarioFlags = 0u;
            eventApplied = true;
            break;
        case 11u:
            runtime.scenarioFlags |= 4u;
            runtime.scenarioTargetGroup = std::min<uint32_t>(
                2000u * (m_asset.particlesPerStrand - 1u) + 62u,
                std::max<uint32_t>(runtime.groupCount, 2u) - 2u);
            eventApplied = true;
            break;
        case 12u:
            runtime.scenarioFlags |= 8u;
            m_simConfig.windStrength = ((m_simulationTickIndex / 8u) & 1u) != 0u
                ? 0.65f : 0.0f;
            eventApplied = true;
            ++m_scenarioStateTransitionCount;
            break;
        case 13u:
            runtime.scenarioFlags |= 16u;
            eventApplied = true;
            break;
        case 14u:
            m_enableScalpMotion = true;
            m_simConfig.windStrength = 0.15f;
            eventApplied = true;
            break;
        case 15u:
            m_enableScalpMotion = true;
            m_simConfig.windStrength = 0.20f;
            eventApplied = true;
            break;
        case 16u: // S17 Payload pool stress
            m_enableScalpMotion = true;
            m_simConfig.windStrength = 0.95f;
            eventApplied = true;
            break;
        case 17u: // S18 Version/descriptor fault injection
            if (m_simulationTickIndex == eventStart)
            {
                runtime.scenarioFlags |= 32u;
                runtime.scenarioTargetGroup = std::min<uint32_t>(
                    1777u * (m_asset.particlesPerStrand - 1u) + 77u,
                    std::max<uint32_t>(runtime.groupCount, 1u) - 1u);
                eventApplied = true;
            }
            break;
        default:
            break;
        }

        if (eventApplied)
        {
            ++m_scenarioAppliedEventCount;
            ++m_noveltyTelemetry.scenarioAppliedEventCount;
            m_noveltyTelemetry.availabilityMask |=
                hairgpu::HairTelemetryAvailability_Scenario;
        }
    }

    void GpuHairViewer::ExecuteNovelty01ExperimentTick(float dt)
    {
        if (m_experimentCase.methodId !=
            ExperimentMethodId::P_IntegratedSegmentGroup)
        {
            ExecuteExperimentSimulationTickLegacy(dt);
            return;
        }

        ResetNoveltyTelemetryForFrame();
        m_noveltyTelemetry.sourceFrameIndex = m_globalFrameIndex;
        if (m_experimentRunStatus == ExperimentRunStatus::Failed)
        {
            return;
        }

        if (m_experimentCase.appliedFlags.enablePerStrandEdit)
        {
            ExecutePerStrandEditing();
        }

        ExecuteNovelty01Complete(dt);
        ++m_simulationTickIndex;
    }

    void GpuHairViewer::StepNovelty01SingleFrame()
    {
        LARGE_INTEGER begin = {};
        LARGE_INTEGER end = {};
        QueryPerformanceCounter(&begin);
        ExecuteNovelty01ExperimentTick(m_simConfig.fixedTimeStep);
        QueryPerformanceCounter(&end);
        m_elapsedSeconds += m_simConfig.fixedTimeStep;
        m_lastSimulationStepMilliseconds =
            static_cast<float>(end.QuadPart - begin.QuadPart) *
            1000.0f /
            static_cast<float>(m_qpcFrequency.QuadPart);
        PerformanceSample sample = BuildPerformanceSample(
            m_simConfig.fixedTimeStep,
            m_simConfig.fixedTimeStep > 1.0e-6f
            ? 1.0f / m_simConfig.fixedTimeStep
            : 0.0f);
        sample.globalFrameIndex = m_globalFrameIndex++;
        sample.measuredFrameIndex = m_measuredFrameIndex++;
        m_performanceSamples.push_back(sample);
        if (m_performanceSamples.size() > 12000u)
        {
            m_performanceSamples.erase(
                m_performanceSamples.begin(),
                m_performanceSamples.begin() +
                    static_cast<std::ptrdiff_t>(
                        m_performanceSamples.size() - 12000u));
        }
        UpdateStatsLabel();
    }

    void GpuHairViewer::ExecuteNovelty01Complete(float dt)
    {
        if (!EnsureNovelty01Runtime(dt))
        {
            StepFullDynamic(dt);
            return;
        }

        const N01RuntimePointer runtimePointer = N01FindRuntime(this);
        if (!runtimePointer)
        {
            StepFullDynamic(dt);
            return;
        }
        Novelty01RuntimeState& runtime = *runtimePointer;
        PrepareThesisPracticalEditEvaluation();
        ApplyThesisPracticalPendingEditInvalidation();
        ApplyNovelty01DesignScenarioForTick(dt);

        for (Novelty01RuntimeState::ReadbackSlot& slot : runtime.readbacks)
        {
            if (slot.issued)
            {
                ++slot.ageFrames;
            }
            if (!slot.issued || slot.ageFrames < 4u)
            {
                continue;
            }

            D3D11_MAPPED_SUBRESOURCE counterMap = {};
            const HRESULT counterResult = m_context->Map(
                slot.counters.Get(),
                0u,
                D3D11_MAP_READ,
                D3D11_MAP_FLAG_DO_NOT_WAIT,
                &counterMap);
            if (counterResult != S_OK)
            {
                continue;
            }

            D3D11_MAPPED_SUBRESOURCE dynamicMap = {};
            const HRESULT dynamicResult = m_context->Map(
                slot.dynamicList.Get(),
                0u,
                D3D11_MAP_READ,
                D3D11_MAP_FLAG_DO_NOT_WAIT,
                &dynamicMap);
            if (dynamicResult != S_OK)
            {
                m_context->Unmap(slot.counters.Get(), 0u);
                continue;
            }

            D3D11_MAPPED_SUBRESOURCE repairMap = {};
            const HRESULT repairResult = m_context->Map(
                slot.repairList.Get(),
                0u,
                D3D11_MAP_READ,
                D3D11_MAP_FLAG_DO_NOT_WAIT,
                &repairMap);
            if (repairResult != S_OK)
            {
                m_context->Unmap(slot.dynamicList.Get(), 0u);
                m_context->Unmap(slot.counters.Get(), 0u);
                continue;
            }

            D3D11_MAPPED_SUBRESOURCE formalMap = {};
            bool formalMapped = false;
            if (slot.formalMetricsIssued && slot.formalMetrics)
            {
                const HRESULT formalResult = m_context->Map(
                    slot.formalMetrics.Get(),
                    0u,
                    D3D11_MAP_READ,
                    D3D11_MAP_FLAG_DO_NOT_WAIT,
                    &formalMap);
                if (formalResult == S_OK)
                {
                    formalMapped = true;
                    const N01GroundTruthMetric* metrics =
                        static_cast<const N01GroundTruthMetric*>(
                            formalMap.pData);
                    for (uint32_t groupIndex = 0u;
                         groupIndex < runtime.groupCount;
                         ++groupIndex)
                    {
                        const N01GroundTruthMetric& metric =
                            metrics[groupIndex];
                        if (!std::isfinite(
                                metric.squaredPositionErrorMm2) ||
                            !std::isfinite(
                                metric.maximumPositionErrorMm))
                        {
                            continue;
                        }
                        ++runtime.formalAggregate.sampleCount;
                        runtime.formalAggregate.falseStaticCount +=
                            metric.falseStatic != 0u ? 1u : 0u;
                        runtime.formalAggregate.squaredPositionErrorMm2 +=
                            metric.squaredPositionErrorMm2;
                        runtime.formalAggregate.maximumPositionErrorMm =
                            std::max(
                                runtime.formalAggregate.maximumPositionErrorMm,
                                static_cast<double>(
                                    metric.maximumPositionErrorMm));
                        runtime.formalAggregate.segmentLengthErrorRatio +=
                            metric.segmentLengthErrorRatio;
                        runtime.formalAggregate.boundaryErrorMm +=
                            metric.boundaryErrorMm;
                        runtime.formalAggregate.followerPositionErrorMm +=
                            metric.followerPositionErrorMm;
                        runtime.formalAggregate.renderGeometryErrorMm +=
                            metric.renderGeometryErrorMm;
                    }
                }
            }

            std::memcpy(
                runtime.latestCounters.values.data(),
                counterMap.pData,
                sizeof(uint32_t) * hairgpu::novelty01::kCounterCount);
            runtime.latestCountersValid = true;
            runtime.latestCounterSourceFrame = slot.sourceFrame;
            runtime.activeGuides.clear();
            std::vector<uint8_t> guideSelected(
                m_asset.guideStrandCount,
                0u);

            const uint32_t dynamicCount = std::min<uint32_t>(
                runtime.latestCounters.Get(
                    hairgpu::novelty01::Counter_Dynamic),
                runtime.groupCount);
            const uint32_t repairCount = std::min<uint32_t>(
                runtime.latestCounters.Get(
                    hairgpu::novelty01::Counter_Repair),
                runtime.groupCount);
            const uint32_t* dynamicIndices =
                static_cast<const uint32_t*>(dynamicMap.pData);
            const uint32_t* repairIndices =
                static_cast<const uint32_t*>(repairMap.pData);

            if (!IsExperimentModeEnabled() &&
                m_debugColorMode == DebugColorMode::ThesisUpdateState)
            {
                runtime.latestThesisUpdateCategories.assign(
                    runtime.groupCount, 0u); // 0 = reused/static path
                const bool fullFallback =
                    runtime.latestCounters.Get(
                        hairgpu::novelty01::Counter_FullDynamic) > 0u;
                if (fullFallback)
                {
                    std::fill(
                        runtime.latestThesisUpdateCategories.begin(),
                        runtime.latestThesisUpdateCategories.end(),
                        static_cast<uint8_t>(3u));
                }
                else
                {
                    const auto markUpdated = [&](const uint32_t* groups, uint32_t count)
                    {
                        for (uint32_t index = 0u; index < count; ++index)
                        {
                            const uint32_t groupIndex = groups[index];
                            if (groupIndex < runtime.latestThesisUpdateCategories.size())
                            {
                                runtime.latestThesisUpdateCategories[groupIndex] = 1u;
                            }
                        }
                    };
                    markUpdated(dynamicIndices, dynamicCount);
                    markUpdated(repairIndices, repairCount);
                }
                runtime.latestThesisUpdateSourceFrame = slot.sourceFrame;
                runtime.latestThesisUpdateCategoriesValid = true;
                m_gpuDebugLinesDirty = true;
            }

            const auto addGroups = [&](const uint32_t* groups, uint32_t count)
            {
                for (uint32_t index = 0u; index < count; ++index)
                {
                    const uint32_t groupIndex = groups[index];
                    if (groupIndex >= runtime.cpuCores.size())
                    {
                        continue;
                    }
                    const uint32_t guideIndex =
                        runtime.cpuCores[groupIndex].strandId;
                    if (guideIndex < guideSelected.size() &&
                        guideSelected[guideIndex] == 0u)
                    {
                        guideSelected[guideIndex] = 1u;
                        runtime.activeGuides.push_back(guideIndex);
                    }
                }
            };
            addGroups(dynamicIndices, dynamicCount);
            addGroups(repairIndices, repairCount);

            runtime.activeFollowers.clear();
            const bool transitionUnstable =
                runtime.latestCounters.Get(
                    hairgpu::novelty01::Counter_Transitions) > 0u ||
                runtime.latestCounters.Get(
                    hairgpu::novelty01::Counter_FalseStatic) > 0u ||
                runtime.latestCounters.Get(
                    hairgpu::novelty01::Counter_RepairFailure) > 0u;

            if (runtime.forceAllFollowers || transitionUnstable)
            {
                runtime.activeFollowers = runtime.followerEntries;
            }
            else
            {
                for (uint32_t guideIndex : runtime.activeGuides)
                {
                    if (guideIndex + 1u >= runtime.followerOffsets.size())
                    {
                        continue;
                    }
                    const uint32_t begin = runtime.followerOffsets[guideIndex];
                    const uint32_t end = runtime.followerOffsets[guideIndex + 1u];
                    runtime.activeFollowers.insert(
                        runtime.activeFollowers.end(),
                        runtime.followerEntries.begin() + begin,
                        runtime.followerEntries.begin() + end);
                }
            }

            if (formalMapped)
            {
                m_context->Unmap(slot.formalMetrics.Get(), 0u);
            }
            m_context->Unmap(slot.repairList.Get(), 0u);
            m_context->Unmap(slot.dynamicList.Get(), 0u);
            m_context->Unmap(slot.counters.Get(), 0u);
            slot.issued = false;
            slot.formalMetricsIssued = false;
            slot.ageFrames = 0u;
            ++m_counterReadbackResolvedCount;
        }

        bool consumedResolvedGpuTiming = false;
        for (auto sampleIt = m_performanceSamples.rbegin();
             sampleIt != m_performanceSamples.rend();
             ++sampleIt)
        {
            const PerformanceSample& resolved = *sampleIt;
            if (resolved.gpuSimulationMilliseconds <= 0.0f)
            {
                continue;
            }
            if (runtime.hasConsumedGpuTiming &&
                resolved.sourceFrameIndex <=
                    runtime.lastConsumedGpuTimingSourceFrame)
            {
                break;
            }

            const bool sourceWasFullDynamic =
                runtime.fullDynamicBySourceFrame[
                    resolved.sourceFrameIndex %
                    runtime.fullDynamicBySourceFrame.size()] != 0u;
            float& targetEwma = sourceWasFullDynamic
                ? runtime.fullDynamicMillisecondsEwma
                : runtime.proposalMillisecondsEwma;
            targetEwma = targetEwma > 0.0f
                ? targetEwma * 0.90f +
                    resolved.gpuSimulationMilliseconds * 0.10f
                : resolved.gpuSimulationMilliseconds;
            runtime.lastConsumedGpuTimingSourceFrame =
                resolved.sourceFrameIndex;
            runtime.hasConsumedGpuTiming = true;
            consumedResolvedGpuTiming = true;
            break;
        }

        runtime.previousScalpCpu = m_animatedScalpParticles;
        if (runtime.previousScalpCpu.empty())
        {
            runtime.previousScalpCpu.emplace_back();
        }
        if (runtime.previousScalp.buffer)
        {
            m_context->UpdateSubresource(
                runtime.previousScalp.buffer.Get(),
                0u,
                nullptr,
                runtime.previousScalpCpu.data(),
                0u,
                0u);
        }
        UpdateAnimatedScalp(m_elapsedSeconds);
        const float scenarioTranslationLengthSquared =
            runtime.scenarioRootTranslation.x * runtime.scenarioRootTranslation.x +
            runtime.scenarioRootTranslation.y * runtime.scenarioRootTranslation.y +
            runtime.scenarioRootTranslation.z * runtime.scenarioRootTranslation.z;
        if (runtime.scenarioRootYawRadians != 0.0f ||
            scenarioTranslationLengthSquared > 0.0f)
        {
            const float cosine = std::cos(runtime.scenarioRootYawRadians);
            const float sine = std::sin(runtime.scenarioRootYawRadians);
            for (GpuParticle& particle : m_animatedScalpParticles)
            {
                const float localX = particle.x - m_scalpCenter.x;
                const float localZ = particle.z - m_scalpCenter.z;
                particle.x = m_scalpCenter.x + localX * cosine + localZ * sine +
                    runtime.scenarioRootTranslation.x;
                particle.y += runtime.scenarioRootTranslation.y;
                particle.z = m_scalpCenter.z - localX * sine + localZ * cosine +
                    runtime.scenarioRootTranslation.z;
            }
            if (m_scalpPositions.buffer && !m_animatedScalpParticles.empty())
            {
                m_context->UpdateSubresource(
                    m_scalpPositions.buffer.Get(), 0u, nullptr,
                    m_animatedScalpParticles.data(), 0u, 0u);
            }
        }

        uint32_t runtimeFlags = 0u;
        const uint64_t newRootHash = N01HashScalp(m_animatedScalpParticles);
        if (newRootHash != runtime.rootHash)
        {
            runtime.rootHash = newRootHash;
            ++runtime.rootTransformVersion;
            runtimeFlags |= N01RuntimeFlag_RootChanged;
            runtime.forceAllFollowers = true;
        }
        const uint64_t newForceHash = N01HashSimulationForces(m_simConfig);
        if (newForceHash != runtime.forceHash)
        {
            runtime.forceHash = newForceHash;
            ++runtime.forceVersion;
            runtimeFlags |= N01RuntimeFlag_ForceChanged;
        }
        const uint64_t newColliderHash = ComputeColliderStateHash();
        if (newColliderHash != runtime.colliderHash)
        {
            runtime.colliderHash = newColliderHash;
            ++runtime.colliderVersion;
            runtimeFlags |= N01RuntimeFlag_ColliderChanged;
        }
        if (runtime.editVersion != m_editVersion)
        {
            runtime.editVersion = m_editVersion;
            runtimeFlags |= N01RuntimeFlag_EditChanged;
        }
        if (runtime.topologyVersion != m_asset.segmentGroupTopology.topologyVersion)
        {
            runtime.topologyVersion = m_asset.segmentGroupTopology.topologyVersion;
            runtimeFlags |= N01RuntimeFlag_TopologyChanged;
        }
        if (m_forceAllSegmentGroupsDirty)
        {
            runtimeFlags |= N01RuntimeFlag_ForceAllDirty;
            m_forceAllSegmentGroupsDirty = false;
        }
        const uint32_t n01ScenarioId = runtime.designScenarioIndex;
        if (n01ScenarioId == 10u || n01ScenarioId == 16u)
        {
            runtimeFlags |= N01RuntimeFlag_ForceAllDynamic;
        }
        if (n01ScenarioId == 0u)
        {
            runtimeFlags |= N01RuntimeFlag_ForceAllStatic;
        }

        const auto includePoint = [](
            hairgpu::Vec3& minimum,
            hairgpu::Vec3& maximum,
            const hairgpu::Vec3& point)
        {
            minimum.x = std::min(minimum.x, point.x);
            minimum.y = std::min(minimum.y, point.y);
            minimum.z = std::min(minimum.z, point.z);
            maximum.x = std::max(maximum.x, point.x);
            maximum.y = std::max(maximum.y, point.y);
            maximum.z = std::max(maximum.z, point.z);
        };
        hairgpu::Vec3 currentMin(1.0e9f, 1.0e9f, 1.0e9f);
        hairgpu::Vec3 currentMax(-1.0e9f, -1.0e9f, -1.0e9f);
        for (const hairgpu::SphereCollider& sphere : m_colliders.spheres)
        {
            const hairgpu::Vec3 radius(sphere.radius, sphere.radius, sphere.radius);
            includePoint(currentMin, currentMax, sphere.center - radius);
            includePoint(currentMin, currentMax, sphere.center + radius);
        }
        for (const hairgpu::CapsuleCollider& capsule : m_colliders.capsules)
        {
            const float radiusValue = std::max(capsule.radiusA, capsule.radiusB);
            const hairgpu::Vec3 radius(radiusValue, radiusValue, radiusValue);
            includePoint(currentMin, currentMax, capsule.pointA - radius);
            includePoint(currentMin, currentMax, capsule.pointA + radius);
            includePoint(currentMin, currentMax, capsule.pointB - radius);
            includePoint(currentMin, currentMax, capsule.pointB + radius);
        }
        for (const hairgpu::AabbCollider& aabb : m_colliders.aabbs)
        {
            includePoint(currentMin, currentMax, aabb.minBounds);
            includePoint(currentMin, currentMax, aabb.maxBounds);
        }

        N01Constants constants = {};
        constants.groupCount = runtime.groupCount;
        constants.blockCount = runtime.blockCount;
        constants.scanBlockCount = runtime.scanBlockCount;
        constants.groupsPerBlock = hairgpu::novelty01::kDefaultGroupsPerBlock;
        constants.guideCount = m_asset.guideStrandCount;
        constants.followerCount = m_asset.followerStrandCount;
        constants.particlesPerStrand = m_asset.particlesPerStrand;
        constants.payloadCapacity = runtime.payloadCapacity;
        constants.frameIndex = m_globalFrameIndex;
        constants.rootTransformVersion = runtime.rootTransformVersion;
        constants.forceVersion = runtime.forceVersion;
        constants.colliderVersion = runtime.colliderVersion;
        constants.topologyVersion = runtime.topologyVersion;
        constants.editVersion = runtime.editVersion;
        constants.dependencyVersion = runtime.dependencyVersion;
        constants.descriptorMaximumAge = 64u;
        constants.minimumResidenceFrames = 12u;
        constants.consecutiveStaticFrames = 8u;
        constants.staticTransitionBudget = std::max<uint32_t>(
            1024u,
            runtime.groupCount / 16u);
        constants.payloadRetireDelayFrames =
            hairgpu::novelty01::kPayloadGenerationCount;
        constants.featureMaskLow =
            static_cast<uint32_t>(runtime.featureMask);
        constants.featureMaskHigh =
            static_cast<uint32_t>(runtime.featureMask >> 32u);
        constants.experimentVariant =
            static_cast<uint32_t>(runtime.variant);
        constants.payloadGeneration = runtime.payloadGeneration;
        constants.deltaTime = std::max(dt, 1.0e-6f);
        constants.staticPositionThresholdMm = 0.10f;
        constants.dynamicPositionThresholdMm = 0.20f;
        constants.staticVelocityThresholdMm = 0.10f;
        constants.dynamicVelocityThresholdMm = 0.20f;
        constants.lengthErrorThresholdRatio = 0.02f;
        constants.boundaryErrorThresholdMm = 2.0f;
        constants.collisionSafetyPaddingMm = 2.0f;
        constants.shadowErrorThresholdMm = 0.50f;
        constants.gravity = m_simConfig.gravity;
        constants.damping = m_simConfig.globalDamping;
        constants.windStrength = m_simConfig.windStrength;
        constants.windDirection[0] = m_simConfig.windDirection.x;
        constants.windDirection[1] = m_simConfig.windDirection.y;
        constants.windDirection[2] = m_simConfig.windDirection.z;
        constants.floorY = m_simConfig.floorY;
        constants.stretchCompliance = m_simConfig.stretchCompliance;
        constants.bendCompliance = m_simConfig.bendCompliance;
        constants.bendStiffness = m_simConfig.bendStiffness;
        constants.selfCollisionRadius = m_selfCollisionRadius;
        constants.selfCollisionStiffness = m_selfCollisionStiffness;
        constants.gridCellSize = m_simConfig.selfCollisionCellSize;
        constants.hashBucketCount = std::max<uint32_t>(
            m_simConfig.selfCollisionHashBucketCount,
            1u);
        constants.enableSelfCollision =
            m_enableSelfCollision ? 1u : 0u;
        constants.rootStiffness = m_simConfig.rootStiffness;
        constants.supportCollisionRadius = m_simConfig.supportCollisionRadius;
        constants.supportCollisionStiffness = m_simConfig.supportCollisionStiffness;
        constants.tangentialFriction = m_simConfig.tangentialFriction;
        constants.lateralFriction = m_simConfig.lateralFriction;
        constants.angularBendRadians = m_runtimeAngularBendRadians;
        constants.angularBendCompliance = m_runtimeAngularBendCompliance;
        constants.angularBendStiffness = m_runtimeAngularBendStiffness;
        constants.angularBendDamping = m_runtimeAngularBendDamping;
        constants.fullDynamicEnterRatio = 0.70f;
        constants.fullDynamicExitRatio = 0.55f;
        constants.fullDynamicDecisionFrames = 8u;
        constants.velocityShockPropagation = m_simConfig.velocityShockPropagation;
        constants.freezeBackThreshold = m_simConfig.freezeBackThreshold;
        constants.freezeBlend = m_simConfig.freezeBlend;
        constants.looseConstraintStiffness = m_simConfig.looseConstraintStiffness;
        constants.looseConstraintMaxDistance = m_simConfig.looseConstraintMaxDistance;
        constants.solverIterations = std::max<uint32_t>(m_simConfig.solverIterations, 1u);
        constants.scalpMotionBlend = m_enableScalpMotion ? 1.0f : 0.0f;
        constants.elapsedTime = m_elapsedSeconds;
        constants.scenarioId = static_cast<uint32_t>(m_experimentCase.scenarioId);
        constants.scenarioTick = m_simulationTickIndex;
        constants.scenarioFlags = runtime.scenarioFlags;
        constants.scenarioTargetGroup = runtime.scenarioTargetGroup;
        constants.useSdfCollider = m_colliders.sdfGrids.empty() ? 0u : 1u;
        constants.sdfValueCount = m_colliders.sdfGrids.empty()
            ? 0u
            : static_cast<uint32_t>(m_colliders.sdfGrids.front().values.size());
        if (!m_colliders.sdfGrids.empty())
        {
            const hairgpu::SdfGridCollider& sdf = m_colliders.sdfGrids.front();
            constants.sdfResolution[0] = sdf.resolutionX;
            constants.sdfResolution[1] = sdf.resolutionY;
            constants.sdfResolution[2] = sdf.resolutionZ;
            constants.sdfBoundsMin[0] = sdf.boundsMin.x;
            constants.sdfBoundsMin[1] = sdf.boundsMin.y;
            constants.sdfBoundsMin[2] = sdf.boundsMin.z;
            constants.sdfBoundsMax[0] = sdf.boundsMax.x;
            constants.sdfBoundsMax[1] = sdf.boundsMax.y;
            constants.sdfBoundsMax[2] = sdf.boundsMax.z;
        }
        constants.sphereColliderCount = static_cast<uint32_t>(
            std::min<size_t>(m_colliders.spheres.size(), 4u));
        constants.capsuleColliderCount = static_cast<uint32_t>(
            std::min<size_t>(m_colliders.capsules.size(), 4u));
        constants.planeColliderCount = static_cast<uint32_t>(
            std::min<size_t>(m_colliders.planes.size(), 4u));
        constants.aabbColliderCount = static_cast<uint32_t>(
            std::min<size_t>(m_colliders.aabbs.size(), 4u));
        constants.runtimeFlags = runtimeFlags;

        hairgpu::Vec3 rootSweptMin(1.0e9f, 1.0e9f, 1.0e9f);
        hairgpu::Vec3 rootSweptMax(-1.0e9f, -1.0e9f, -1.0e9f);
        const size_t scalpCount = std::min(
            m_animatedScalpParticles.size(),
            runtime.previousScalpCpu.size());
        for (size_t scalpIndex = 0u; scalpIndex < scalpCount; ++scalpIndex)
        {
            const GpuParticle& current = m_animatedScalpParticles[scalpIndex];
            const GpuParticle& previous = runtime.previousScalpCpu[scalpIndex];
            rootSweptMin.x = std::min(rootSweptMin.x, std::min(current.x, previous.x));
            rootSweptMin.y = std::min(rootSweptMin.y, std::min(current.y, previous.y));
            rootSweptMin.z = std::min(rootSweptMin.z, std::min(current.z, previous.z));
            rootSweptMax.x = std::max(rootSweptMax.x, std::max(current.x, previous.x));
            rootSweptMax.y = std::max(rootSweptMax.y, std::max(current.y, previous.y));
            rootSweptMax.z = std::max(rootSweptMax.z, std::max(current.z, previous.z));
        }
        if (scalpCount == 0u)
        {
            rootSweptMin = hairgpu::Vec3(1.0e9f, 1.0e9f, 1.0e9f);
            rootSweptMax = hairgpu::Vec3(-1.0e9f, -1.0e9f, -1.0e9f);
        }
        constexpr float kRootSweptPadding = 0.020f;
        rootSweptMin = rootSweptMin - hairgpu::Vec3(kRootSweptPadding, kRootSweptPadding, kRootSweptPadding);
        rootSweptMax = rootSweptMax + hairgpu::Vec3(kRootSweptPadding, kRootSweptPadding, kRootSweptPadding);
        constants.sweptRootBoundsMin[0] = rootSweptMin.x;
        constants.sweptRootBoundsMin[1] = rootSweptMin.y;
        constants.sweptRootBoundsMin[2] = rootSweptMin.z;
        constants.sweptRootBoundsMax[0] = rootSweptMax.x;
        constants.sweptRootBoundsMax[1] = rootSweptMax.y;
        constants.sweptRootBoundsMax[2] = rootSweptMax.z;

        const hairgpu::Vec3 sweptMin(
            std::min(currentMin.x, runtime.previousColliderBoundsMin.x),
            std::min(currentMin.y, runtime.previousColliderBoundsMin.y),
            std::min(currentMin.z, runtime.previousColliderBoundsMin.z));
        const hairgpu::Vec3 sweptMax(
            std::max(currentMax.x, runtime.previousColliderBoundsMax.x),
            std::max(currentMax.y, runtime.previousColliderBoundsMax.y),
            std::max(currentMax.z, runtime.previousColliderBoundsMax.z));
        constants.sweptColliderBoundsMin[0] = sweptMin.x;
        constants.sweptColliderBoundsMin[1] = sweptMin.y;
        constants.sweptColliderBoundsMin[2] = sweptMin.z;
        constants.sweptColliderBoundsMax[0] = sweptMax.x;
        constants.sweptColliderBoundsMax[1] = sweptMax.y;
        constants.sweptColliderBoundsMax[2] = sweptMax.z;
        runtime.previousColliderBoundsMin = currentMin;
        runtime.previousColliderBoundsMax = currentMax;

        for (uint32_t i = 0u; i < constants.sphereColliderCount; ++i)
        {
            const hairgpu::SphereCollider& collider = m_colliders.spheres[i];
            constants.sphereData[i][0] = collider.center.x;
            constants.sphereData[i][1] = collider.center.y;
            constants.sphereData[i][2] = collider.center.z;
            constants.sphereData[i][3] = collider.radius;
        }
        for (uint32_t i = 0u; i < constants.capsuleColliderCount; ++i)
        {
            const hairgpu::CapsuleCollider& collider = m_colliders.capsules[i];
            constants.capsuleData[i * 2u + 0u][0] = collider.pointA.x;
            constants.capsuleData[i * 2u + 0u][1] = collider.pointA.y;
            constants.capsuleData[i * 2u + 0u][2] = collider.pointA.z;
            constants.capsuleData[i * 2u + 0u][3] = collider.radiusA;
            constants.capsuleData[i * 2u + 1u][0] = collider.pointB.x;
            constants.capsuleData[i * 2u + 1u][1] = collider.pointB.y;
            constants.capsuleData[i * 2u + 1u][2] = collider.pointB.z;
            constants.capsuleData[i * 2u + 1u][3] = collider.radiusB;
        }
        for (uint32_t i = 0u; i < constants.planeColliderCount; ++i)
        {
            const hairgpu::PlaneCollider& collider = m_colliders.planes[i];
            const hairgpu::Vec3 normal = hairgpu::Normalize(collider.normal);
            constants.planeData[i][0] = normal.x;
            constants.planeData[i][1] = normal.y;
            constants.planeData[i][2] = normal.z;
            constants.planeData[i][3] = -hairgpu::Dot(normal, collider.point);
        }
        for (uint32_t i = 0u; i < constants.aabbColliderCount; ++i)
        {
            const hairgpu::AabbCollider& collider = m_colliders.aabbs[i];
            constants.aabbData[i * 2u + 0u][0] = collider.minBounds.x;
            constants.aabbData[i * 2u + 0u][1] = collider.minBounds.y;
            constants.aabbData[i * 2u + 0u][2] = collider.minBounds.z;
            constants.aabbData[i * 2u + 1u][0] = collider.maxBounds.x;
            constants.aabbData[i * 2u + 1u][1] = collider.maxBounds.y;
            constants.aabbData[i * 2u + 1u][2] = collider.maxBounds.z;
        }
        runtime.constants = constants;
        runtime.formalGroundTruthValidThisFrame = false;
        if (runtime.formalGroundTruthEnabled &&
            (m_globalFrameIndex % runtime.groundTruthIntervalFrames) == 0u &&
            m_experimentCase.measurementMode != MeasurementMode::Performance)
        {
            runtime.formalGroundTruthValidThisFrame =
                RunNovelty01GroundTruth(dt);
        }

        bool useFullDynamic =
            runtime.variant ==
                hairgpu::novelty01::ExperimentVariant::B1_OptimizedFullDynamic;
        const bool warmingUp = runtime.initialFullDynamicFramesRemaining > 0u;
        if (warmingUp)
        {
            --runtime.initialFullDynamicFramesRemaining;
            useFullDynamic = true;
        }
        if (runtime.fullDynamicHoldFrames > 0u)
        {
            --runtime.fullDynamicHoldFrames;
            useFullDynamic = true;
        }
        if (!runtime.latestCountersValid && warmingUp)
        {
            useFullDynamic = true;
        }
        if ((runtimeFlags &
             (N01RuntimeFlag_TopologyChanged |
              N01RuntimeFlag_EditChanged |
              N01RuntimeFlag_DependencyChanged)) != 0u)
        {
            useFullDynamic = true;
            runtime.fullDynamicHoldFrames = 2u;
        }

        if ((runtime.featureMask &
             hairgpu::novelty01::Feature_FullDynamicFastPath) != 0u &&
            runtime.latestCountersValid &&
            runtime.latestCounterSourceFrame !=
                runtime.lastFastPathDecisionSourceFrame)
        {
            runtime.lastFastPathDecisionSourceFrame =
                runtime.latestCounterSourceFrame;
            const float dynamicRatio =
                static_cast<float>(runtime.latestCounters.Get(
                    hairgpu::novelty01::Counter_Dynamic)) /
                static_cast<float>(std::max(runtime.groupCount, 1u));
            if (dynamicRatio >= runtime.dynamicRatioThreshold ||
                runtime.latestCounters.Get(
                    hairgpu::novelty01::Counter_PayloadOverflow) > 0u ||
                runtime.latestCounters.Get(
                    hairgpu::novelty01::Counter_FullDynamicRequested) > 0u ||
                runtime.latestCounters.Get(
                    hairgpu::novelty01::Counter_RepairFailure) > 0u ||
                runtime.latestCounters.Get(
                    hairgpu::novelty01::Counter_FalseStatic) > 0u)
            {
                useFullDynamic = true;
                runtime.fullDynamicHoldFrames = 2u;
            }
        }
        const bool scheduledProposalProbe =
            (m_globalFrameIndex % 120u) == 0u;
        if ((runtime.featureMask &
             hairgpu::novelty01::Feature_FullDynamicFastPath) != 0u &&
            !scheduledProposalProbe &&
            runtime.proposalMillisecondsEwma > 0.0f &&
            runtime.fullDynamicMillisecondsEwma > 0.0f &&
            runtime.proposalMillisecondsEwma >=
                runtime.fullDynamicMillisecondsEwma * 0.98f)
        {
            useFullDynamic = true;
        }

        LARGE_INTEGER begin = {};
        LARGE_INTEGER end = {};
        QueryPerformanceCounter(&begin);

        if (useFullDynamic)
        {
            StepFullDynamic(dt);
            runtime.lastFrameUsedFullDynamic = true;
            runtime.forceAllFollowers = true;
            runtime.initialized = false;
            runtime.payloadReadIndex = 0u;
            m_noveltyTelemetry.fallbackCount += 1u;
            m_noveltyTelemetry.physicsFallbackCount += 1u;
            m_noveltyTelemetry.dynamicGroupCount = runtime.groupCount;
            m_noveltyTelemetry.totalSegmentGroupCount = runtime.groupCount;
        }
        else
        {
            RunNovelty01PartialSimulation(dt);
            runtime.lastFrameUsedFullDynamic = false;
        }

        runtime.fullDynamicBySourceFrame[
            m_globalFrameIndex %
            runtime.fullDynamicBySourceFrame.size()] =
            useFullDynamic ? 1u : 0u;

        QueryPerformanceCounter(&end);
        const float elapsedMilliseconds =
            static_cast<float>(end.QuadPart - begin.QuadPart) *
            1000.0f /
            static_cast<float>(m_qpcFrequency.QuadPart);
        if (!consumedResolvedGpuTiming)
        {
            float& ewma = useFullDynamic
                ? runtime.fullDynamicMillisecondsEwma
                : runtime.proposalMillisecondsEwma;
            ewma = ewma > 0.0f
                ? ewma * 0.95f + elapsedMilliseconds * 0.05f
                : elapsedMilliseconds;
        }

        ValidateNovelty01FormalInvariants();
        WriteNovelty01FormalTelemetry();

        if (runtime.latestCountersValid)
        {
            const auto get = [&](N01CounterIndex index)
            {
                return runtime.latestCounters.Get(index);
            };
            const uint32_t staticCombined = get(
                hairgpu::novelty01::Counter_StaticReuse);
            const uint32_t staticTransform = std::min(
                get(hairgpu::novelty01::Counter_StaticTransform),
                staticCombined);
            m_noveltyTelemetry.totalSegmentGroupCount = runtime.groupCount;
            m_noveltyTelemetry.staticTransformedGroupCount = staticTransform;
            m_noveltyTelemetry.staticCachedGroupCount = staticCombined - staticTransform;
            m_noveltyTelemetry.dynamicGroupCount =
                runtime.variant ==
                    hairgpu::novelty01::ExperimentVariant::B2_SleepingFlag
                ? get(hairgpu::novelty01::Counter_CandidateDynamic)
                : get(hairgpu::novelty01::Counter_Dynamic);
            m_noveltyTelemetry.repairRequiredGroupCount = get(
                hairgpu::novelty01::Counter_Repair);
            m_noveltyTelemetry.fallbackGroupCount = get(
                hairgpu::novelty01::Counter_Fallback);
            m_noveltyTelemetry.falseStaticCount = get(
                hairgpu::novelty01::Counter_FalseStatic);
            m_noveltyTelemetry.stateTransitionCount = get(
                hairgpu::novelty01::Counter_Transitions);
            m_noveltyTelemetry.stateOscillationCount = get(
                hairgpu::novelty01::Counter_Oscillations);
            m_noveltyTelemetry.dirtyBlockCount = get(
                hairgpu::novelty01::Counter_DirtyBlocks);
            m_noveltyTelemetry.dirtyGroupCount = get(
                hairgpu::novelty01::Counter_DirtyGroups);
            m_noveltyTelemetry.cacheHitCount = get(
                hairgpu::novelty01::Counter_CacheHits);
            m_noveltyTelemetry.cacheMissCount = get(
                hairgpu::novelty01::Counter_CacheMisses);
            m_noveltyTelemetry.cacheInvalidationCount = get(
                hairgpu::novelty01::Counter_CacheInvalidations);
            m_noveltyTelemetry.overflowCount += get(
                hairgpu::novelty01::Counter_PayloadOverflow);
            m_noveltyTelemetry.sharedPointConflictCount = get(
                hairgpu::novelty01::Counter_SharedPointConflicts);
            m_noveltyTelemetry.watchdogCheckCount = get(
                hairgpu::novelty01::Counter_WatchdogChecks);
            m_noveltyTelemetry.watchdogRepairCount = get(
                hairgpu::novelty01::Counter_RepairSuccess);
            m_noveltyTelemetry.totalSegmentGroupBlockCount = runtime.blockCount;
            m_noveltyTelemetry.dynamicCompactionInputCount = get(
                hairgpu::novelty01::Counter_CandidateDynamic);
            m_noveltyTelemetry.dynamicCompactionOutputCount = get(
                hairgpu::novelty01::Counter_Dynamic);
            m_noveltyTelemetry.repairCompactionOutputCount = get(
                hairgpu::novelty01::Counter_Repair);
            m_noveltyTelemetry.certificateValidGroupCount = get(
                hairgpu::novelty01::Counter_DescriptorValid);
            m_noveltyTelemetry.certificateInvalidGroupCount =
                runtime.groupCount > m_noveltyTelemetry.certificateValidGroupCount
                ? runtime.groupCount -
                    m_noveltyTelemetry.certificateValidGroupCount
                : 0u;
            m_noveltyTelemetry.staticReuseAttemptCount = staticCombined;
            m_noveltyTelemetry.repairAttemptCount = get(
                hairgpu::novelty01::Counter_Repair);
            m_noveltyTelemetry.repairSuccessCount = get(
                hairgpu::novelty01::Counter_RepairSuccess);
            m_noveltyTelemetry.repairFailureCount = get(
                hairgpu::novelty01::Counter_RepairFailure);
            m_noveltyTelemetry.directionCacheMemoryMb =
                static_cast<float>(
                    static_cast<double>(runtime.groupCount) *
                    (sizeof(float) * 4u) /
                    (1024.0 * 1024.0));
            m_noveltyTelemetry.fullPositionMemoryMb =
                static_cast<float>(
                    static_cast<double>(runtime.groupCount) *
                    sizeof(GpuParticle) * 2.0 /
                    (1024.0 * 1024.0));
            m_noveltyTelemetry.directionCacheMemorySavingRate =
                m_noveltyTelemetry.fullPositionMemoryMb > 1.0e-6f
                ? 1.0f -
                    m_noveltyTelemetry.directionCacheMemoryMb /
                    m_noveltyTelemetry.fullPositionMemoryMb
                : 0.0f;
            const uint32_t shadowSamples = get(
                hairgpu::novelty01::Counter_ShadowSamples);
            m_noveltyTelemetry.predictiveWakeEvaluatedCount = shadowSamples;
            m_noveltyTelemetry.followerReconstructionGpuMilliseconds =
                0.0f;
            const uint32_t activeFollowerCount =
                static_cast<uint32_t>(runtime.activeFollowers.size());
            m_noveltyTelemetry.renderSkipCount =
                activeFollowerCount < m_asset.followerStrandCount
                ? m_asset.followerStrandCount - activeFollowerCount
                : 0u;
            m_noveltyTelemetry.availabilityMask |=
                hairgpu::HairTelemetryAvailability_GpuCounters;
            m_noveltyTelemetry.metricAvailabilityMask |=
                hairgpu::HairTelemetryAvailability_GpuCounters;

            const uint32_t groundTruthSamples = get(
                hairgpu::novelty01::Counter_GroundTruthSamples);
            if (groundTruthSamples > 0u)
            {
                const uint32_t groundTruthFalseStatic = get(
                    hairgpu::novelty01::Counter_GroundTruthFalseStatic);
                m_noveltyTelemetry.falseStaticGroundTruthFrameCount = 1u;
                m_noveltyTelemetry.falseStaticCount = groundTruthFalseStatic;
                m_noveltyTelemetry.falseStaticRate =
                    static_cast<float>(groundTruthFalseStatic) /
                    static_cast<float>(groundTruthSamples);
                m_noveltyTelemetry.availabilityMask |=
                    hairgpu::HairTelemetryAvailability_FalseStaticGroundTruth;
                m_noveltyTelemetry.metricAvailabilityMask |=
                    hairgpu::HairTelemetryAvailability_FalseStaticGroundTruth;
            }
        }
    }

    void GpuHairViewer::RunNovelty01PartialSimulation(float dt)
    {
        const N01RuntimePointer runtimePointer = N01FindRuntime(this);
        if (!runtimePointer)
        {
            StepFullDynamic(dt);
            return;
        }
        Novelty01RuntimeState& runtime = *runtimePointer;
        N01Constants constants = runtime.constants;
        ID3D11ShaderResourceView* proposalRestSrv =
            runtime.usesResampledRestPositions
            ? runtime.proposalRestPositions.srv.Get()
            : m_restPositions.srv.Get();

        const auto updateConstants = [&](uint32_t passKind,
                                         uint32_t solverIteration,
                                         uint32_t solverPhase)
        {
            constants.passKind = passKind;
            constants.solverIteration = solverIteration;
            constants.solverPhase = solverPhase;
            m_context->UpdateSubresource(
                runtime.constantBuffer.Get(),
                0u,
                nullptr,
                &constants,
                0u,
                0u);
            ID3D11Buffer* cb = runtime.constantBuffer.Get();
            m_context->CSSetConstantBuffers(0u, 1u, &cb);
        };

        const auto dispatch = [&](ID3D11ComputeShader* shader,
                                  uint32_t count,
                                  uint32_t threads)
        {
            const uint32_t groups = N01DispatchGroups(count, threads);
            if (!shader || groups == 0u)
            {
                return;
            }
            m_context->CSSetShader(shader, nullptr, 0u);
            m_context->Dispatch(groups, 1u, 1u);
        };

        const std::array<uint32_t, hairgpu::novelty01::kCounterCount>
            zeroCounters = {};
        m_context->UpdateSubresource(
            runtime.counters.buffer.Get(),
            0u,
            nullptr,
            zeroCounters.data(),
            0u,
            0u);

        const auto bindMode0 = [&]()
        {
            ID3D11ShaderResourceView* srvs[10] = {};
            srvs[0] = m_positions[m_currentBufferIndex].srv.Get();
            srvs[1] = m_previousPositions[m_currentBufferIndex].srv.Get();
            srvs[2] = runtime.payloads[runtime.payloadReadIndex].srv.Get();
            srvs[8] = runtime.prefix.srv.Get();
            srvs[9] = runtime.scanBlockOffsets.srv.Get();
            ID3D11UnorderedAccessView* uavs[8] =
            {
                runtime.cores.uav.Get(),
                runtime.extensions.uav.Get(),
                runtime.caches.uav.Get(),
                runtime.descriptors.uav.Get(),
                runtime.blocks.uav.Get(),
                runtime.flags.uav.Get(),
                runtime.counters.uav.Get(),
                runtime.sharedCandidates.uav.Get()
            };
            m_context->CSSetShaderResources(0u, 10u, srvs);
            m_context->CSSetUnorderedAccessViews(0u, 8u, uavs, nullptr);
        };

        const auto bindMode1ScanLocal = [&]()
        {
            ID3D11ShaderResourceView* srvs[1] =
            {
                runtime.flags.srv.Get()
            };
            ID3D11UnorderedAccessView* uavs[2] =
            {
                runtime.prefix.uav.Get(),
                runtime.scanBlockCounts.uav.Get()
            };
            m_context->CSSetShaderResources(0u, 1u, srvs);
            m_context->CSSetUnorderedAccessViews(0u, 2u, uavs, nullptr);
        };

        const auto bindMode1ScanBlocks = [&]()
        {
            ID3D11UnorderedAccessView* uavs[4] =
            {
                nullptr,
                runtime.scanBlockCounts.uav.Get(),
                runtime.scanBlockOffsets.uav.Get(),
                runtime.counters.uav.Get()
            };
            m_context->CSSetUnorderedAccessViews(0u, 4u, uavs, nullptr);
        };

        const auto bindMode1Scatter = [&]()
        {
            // HAIRGPU_N01_BINDING_COUNT_GUARD_V1
            // Binding mode 1 declares SRVs t0/t1/t2 only.  Keep the
            // D3D11 count equal to the actual C++ array length; passing
            // 4 here used to read a nonexistent fourth stack pointer on
            // the first SG-APT partial-simulation frame.
            ID3D11ShaderResourceView* srvs[3] =
            {
                runtime.flags.srv.Get(),
                runtime.prefix.srv.Get(),
                runtime.scanBlockOffsets.srv.Get()
            };
            ID3D11UnorderedAccessView* uavs[8] =
            {
                nullptr,
                nullptr,
                nullptr,
                nullptr,
                runtime.dynamicList.uav.Get(),
                runtime.staticList.uav.Get(),
                runtime.repairList.uav.Get(),
                runtime.invalidList.uav.Get()
            };
            m_context->CSSetShaderResources(0u, 3u, srvs);
            m_context->CSSetUnorderedAccessViews(0u, 8u, uavs, nullptr);
        };

        const auto runScan = [&](uint32_t passKind)
        {
            updateConstants(passKind, 0u, 0u);
            bindMode1ScanLocal();
            dispatch(runtime.scanLocalCS.Get(), runtime.groupCount, 256u);
            N01UnbindCompute(m_context.Get());

            updateConstants(passKind, 0u, 0u);
            bindMode1ScanBlocks();
            dispatch(runtime.scanBlocksCS.Get(), 1u, 1u);
            N01UnbindCompute(m_context.Get());
        };

        if (!runtime.initialized)
        {
            updateConstants(0u, 0u, 0u);
            bindMode0();
            dispatch(runtime.initializeCS.Get(), runtime.groupCount, 64u);
            N01UnbindCompute(m_context.Get());
            runtime.initialized = true;
        }

        if (runtime.scenarioFlags != 0u && runtime.injectDesignScenarioCS)
        {
            updateConstants(0u, 0u, 0u);
            ID3D11UnorderedAccessView* scenarioUavs[6] =
            {
                m_positions[m_currentBufferIndex].uav.Get(),
                m_previousPositions[m_currentBufferIndex].uav.Get(),
                runtime.cores.uav.Get(),
                runtime.extensions.uav.Get(),
                runtime.descriptors.uav.Get(),
                runtime.counters.uav.Get()
            };
            m_context->CSSetUnorderedAccessViews(0u, 6u, scenarioUavs, nullptr);
            dispatch(runtime.injectDesignScenarioCS.Get(), runtime.groupCount, 64u);
            N01UnbindCompute(m_context.Get());
        }

        ExecuteNovelty01PreClassificationValidation();

        BeginGpuPassTiming(GpuPassId::BlockClassification);
        updateConstants(0u, 0u, 0u);
        bindMode0();
        dispatch(runtime.invalidateBlocksCS.Get(), runtime.blockCount, 64u);
        N01UnbindCompute(m_context.Get());

        updateConstants(0u, 0u, 0u);
        bindMode0();
        dispatch(runtime.buildBlocksCS.Get(), runtime.blockCount, 64u);
        N01UnbindCompute(m_context.Get());
        EndGpuPassTiming(GpuPassId::BlockClassification);

        BeginGpuPassTiming(GpuPassId::GroupClassification);
        updateConstants(0u, 0u, 0u);
        bindMode0();
        dispatch(runtime.describeClassifyCS.Get(), runtime.groupCount, 64u);
        N01UnbindCompute(m_context.Get());
        EndGpuPassTiming(GpuPassId::GroupClassification);

        BeginGpuPassTiming(GpuPassId::Compaction);
        runScan(0u);

        updateConstants(0u, 0u, 0u);
        bindMode0();
        dispatch(runtime.applyTransitionsCS.Get(), runtime.groupCount, 64u);
        N01UnbindCompute(m_context.Get());

        updateConstants(0u, 0u, 0u);
        bindMode0();
        dispatch(runtime.finalizeBlocksCS.Get(), runtime.blockCount, 64u);
        N01UnbindCompute(m_context.Get());

        if (runtime.variant ==
            hairgpu::novelty01::ExperimentVariant::B2_SleepingFlag)
        {
            updateConstants(1u, 0u, 0u);
            ID3D11ShaderResourceView* identitySrvs[1] =
            {
                runtime.flags.srv.Get()
            };
            ID3D11UnorderedAccessView* identityUavs[8] =
            {
                nullptr,
                nullptr,
                nullptr,
                runtime.counters.uav.Get(),
                runtime.dynamicList.uav.Get(),
                runtime.staticList.uav.Get(),
                runtime.repairList.uav.Get(),
                runtime.invalidList.uav.Get()
            };
            m_context->CSSetShaderResources(0u, 1u, identitySrvs);
            m_context->CSSetUnorderedAccessViews(0u, 8u, identityUavs, nullptr);
            m_context->CSSetShader(
                runtime.buildSleepingIdentityListCS.Get(), nullptr, 0u);
            m_context->Dispatch(
                N01DispatchGroups(runtime.groupCount, 64u), 1u, 1u);
            N01UnbindCompute(m_context.Get());
        }
        else if ((runtime.featureMask &
             hairgpu::novelty01::Feature_StableCompaction) != 0u)
        {
            runScan(1u);
            updateConstants(1u, 0u, 0u);
            bindMode1Scatter();
            dispatch(runtime.scatterListsCS.Get(), runtime.groupCount, 256u);
            N01UnbindCompute(m_context.Get());
        }
        else
        {
            updateConstants(1u, 0u, 0u);
            ID3D11ShaderResourceView* atomicSrvs[1] =
            {
                runtime.flags.srv.Get()
            };
            ID3D11UnorderedAccessView* atomicUavs[5] =
            {
                runtime.counters.uav.Get(),
                runtime.dynamicList.uav.Get(),
                runtime.staticList.uav.Get(),
                runtime.repairList.uav.Get(),
                runtime.invalidList.uav.Get()
            };
            m_context->CSSetShaderResources(0u, 1u, atomicSrvs);
            m_context->CSSetUnorderedAccessViews(
                0u, 5u, atomicUavs, nullptr);
            m_context->CSSetShader(
                runtime.clearAtomicCompactionCountersCS.Get(),
                nullptr,
                0u);
            m_context->Dispatch(1u, 1u, 1u);
            N01UnbindCompute(m_context.Get());

            ID3D11Buffer* atomicCb = runtime.constantBuffer.Get();
            m_context->CSSetConstantBuffers(0u, 1u, &atomicCb);
            m_context->CSSetShaderResources(0u, 1u, atomicSrvs);
            m_context->CSSetUnorderedAccessViews(
                0u, 5u, atomicUavs, nullptr);
            m_context->CSSetShader(
                runtime.atomicCompactCS.Get(),
                nullptr,
                0u);
            m_context->Dispatch(
                N01DispatchGroups(runtime.groupCount, 64u),
                1u,
                1u);
            N01UnbindCompute(m_context.Get());
        }

        runtime.payloadGeneration =
            (runtime.payloadReadIndex + 1u) %
            hairgpu::novelty01::kPayloadGenerationCount;
        ExecuteNovelty01B4MigrationBaseline();
        ExecuteNovelty01BuildIndirectArguments();

        const uint32_t nextPayloadIndex =
            runtime.payloadGeneration;
        const auto bindMode2 = [&](uint32_t oldPayloadIndex,
                                   uint32_t newPayloadIndex)
        {
            ID3D11ShaderResourceView* srvs[12] =
            {
                runtime.dynamicList.srv.Get(),
                runtime.staticList.srv.Get(),
                runtime.repairList.srv.Get(),
                nullptr,
                proposalRestSrv,
                m_scalpPositions.srv.Get(),
                m_scalpPositions.srv.Get(),
                runtime.previousScalp.srv.Get(),
                m_scalpTrianglesBuffer.srv.Get(),
                m_rootBindingsBuffer.srv.Get(),
                runtime.payloads[oldPayloadIndex].srv.Get(),
                runtime.payloadOwners[oldPayloadIndex].srv.Get()
            };
            ID3D11UnorderedAccessView* uavs[8] =
            {
                runtime.cores.uav.Get(),
                runtime.extensions.uav.Get(),
                runtime.caches.uav.Get(),
                runtime.descriptors.uav.Get(),
                runtime.payloads[newPayloadIndex].uav.Get(),
                m_positions[m_currentBufferIndex].uav.Get(),
                m_previousPositions[m_currentBufferIndex].uav.Get(),
                runtime.counters.uav.Get()
            };
            m_context->CSSetShaderResources(0u, 12u, srvs);
            m_context->CSSetUnorderedAccessViews(0u, 8u, uavs, nullptr);
        };

        updateConstants(0u, 0u, 0u);
        bindMode2(runtime.payloadReadIndex, nextPayloadIndex);
        if ((runtime.featureMask &
             hairgpu::novelty01::Feature_OptionalPayload) == 0u)
        {
            dispatch(
                runtime.carryNonOptionalPayloadsCS.Get(),
                runtime.groupCount,
                64u);
            N01UnbindCompute(m_context.Get());
            updateConstants(0u, 0u, 0u);
            bindMode2(runtime.payloadReadIndex, nextPayloadIndex);
        }
        dispatch(runtime.remapPayloadCS.Get(), runtime.groupCount, 64u);
        N01UnbindCompute(m_context.Get());
        ExecuteNovelty01PostCompactionLifecycle();
        EndGpuPassTiming(GpuPassId::Compaction);

        const auto bindMode4 = [&]()
        {
            ID3D11ShaderResourceView* srvs[8] =
            {
                runtime.activeFollowerList.srv.Get(),
                m_followBuffer.srv.Get(),
                proposalRestSrv,
                m_scalpPositions.srv.Get(),
                runtime.previousScalp.srv.Get(),
                m_scalpTrianglesBuffer.srv.Get(),
                m_rootBindingsBuffer.srv.Get(),
                runtime.followerDirtyRanges.srv.Get()
            };
            ID3D11UnorderedAccessView* uavs[3] =
            {
                m_positions[m_currentBufferIndex].uav.Get(),
                m_previousPositions[m_currentBufferIndex].uav.Get(),
                runtime.counters.uav.Get()
            };
            m_context->CSSetShaderResources(0u, 8u, srvs);
            m_context->CSSetUnorderedAccessViews(0u, 3u, uavs, nullptr);
        };

        updateConstants(0u, 0u, 0u);
        bindMode4();
        dispatch(
            runtime.rootBindCS.Get(),
            m_asset.guideStrandCount,
            64u);
        N01UnbindCompute(m_context.Get());

        BeginGpuPassTiming(GpuPassId::StaticTransform);
        updateConstants(0u, 0u, 0u);
        bindMode2(runtime.payloadReadIndex, nextPayloadIndex);
        dispatch(runtime.applyStaticCS.Get(), runtime.groupCount, 64u);
        N01UnbindCompute(m_context.Get());
        EndGpuPassTiming(GpuPassId::StaticTransform);

        const auto bindMode3 = [&]()
        {
            ID3D11ShaderResourceView* srvs[4] =
            {
                runtime.dynamicList.srv.Get(),
                runtime.guideDirtyRanges.srv.Get(),
                proposalRestSrv,
                m_sdfValuesBuffer.srv.Get()
            };
            ID3D11UnorderedAccessView* uavs[8] =
            {
                m_positions[m_currentBufferIndex].uav.Get(),
                m_previousPositions[m_currentBufferIndex].uav.Get(),
                runtime.cores.uav.Get(),
                runtime.extensions.uav.Get(),
                runtime.payloads[nextPayloadIndex].uav.Get(),
                runtime.hashHeads.uav.Get(),
                runtime.particleNext.uav.Get(),
                runtime.counters.uav.Get()
            };
            // Binding mode 3 declares SRVs t0..t3, including SDF at t3.
            m_context->CSSetShaderResources(0u, 4u, srvs);
            m_context->CSSetUnorderedAccessViews(0u, 8u, uavs, nullptr);
        };

        ExecuteNovelty01DirtyRangePropagation();

        BeginGpuPassTiming(GpuPassId::DynamicSolve);
        const bool useBaselineEquivalentGuideSolve =
            (runtime.featureMask &
             hairgpu::novelty01::Feature_BaselineEquivalentSolve) != 0u;

        if (useBaselineEquivalentGuideSolve)
        {
            updateConstants(0u, 0u, 0u);
            bindMode3();
            dispatch(
                runtime.clearDynamicGuideLambdasCS.Get(),
                m_asset.guideStrandCount,
                64u);
            N01UnbindCompute(m_context.Get());

            updateConstants(0u, 0u, 0u);
            bindMode3();
            dispatch(
                runtime.integrateDynamicGuidesCS.Get(),
                m_asset.guideStrandCount,
                64u);
            N01UnbindCompute(m_context.Get());

            const uint32_t iterations = std::max<uint32_t>(
                m_simConfig.solverIterations,
                1u);
            for (uint32_t iteration = 0u; iteration < iterations; ++iteration)
            {
                updateConstants(0u, iteration, 0u);
                bindMode3();
                dispatch(
                    runtime.distanceDynamicGuidesCS.Get(),
                    m_asset.guideStrandCount,
                    64u);
                N01UnbindCompute(m_context.Get());

                updateConstants(0u, iteration, 0u);
                bindMode3();
                dispatch(
                    runtime.bendDynamicGuidesCS.Get(),
                    m_asset.guideStrandCount,
                    64u);
                N01UnbindCompute(m_context.Get());

                updateConstants(0u, iteration, 0u);
                bindMode3();
                dispatch(
                    runtime.collisionDynamicGuidesCS.Get(),
                    m_asset.guideStrandCount,
                    64u);
                N01UnbindCompute(m_context.Get());

                if (m_enableSelfCollision)
                {
                    const uint32_t hashClearCount = std::max<uint32_t>(
                        runtime.constants.hashBucketCount,
                        m_asset.guideStrandCount * m_asset.particlesPerStrand);
                    updateConstants(0u, iteration, 0u);
                    bindMode3();
                    dispatch(runtime.clearHashCS.Get(), hashClearCount, 64u);
                    N01UnbindCompute(m_context.Get());

                    updateConstants(0u, iteration, 0u);
                    bindMode3();
                    dispatch(
                        runtime.buildHashCS.Get(),
                        m_asset.guideStrandCount * m_asset.particlesPerStrand,
                        64u);
                    N01UnbindCompute(m_context.Get());

                    updateConstants(0u, iteration, 0u);
                    bindMode3();
                    dispatch(runtime.selfCollisionCS.Get(), runtime.groupCount, 64u);
                    N01UnbindCompute(m_context.Get());
                }
            }

            updateConstants(0u, 0u, 0u);
            bindMode3();
            dispatch(
                runtime.stabilityGuardDynamicGuidesCS.Get(),
                m_asset.guideStrandCount,
                64u);
            N01UnbindCompute(m_context.Get());
        }
        else
        {
            updateConstants(0u, 0u, 0u);
            bindMode3();
            dispatch(runtime.integrateDynamicCS.Get(), runtime.groupCount, 64u);
            N01UnbindCompute(m_context.Get());

            const uint32_t iterations = std::max<uint32_t>(
                m_simConfig.solverIterations,
                1u);
            for (uint32_t iteration = 0u; iteration < iterations; ++iteration)
            {
                for (uint32_t phase = 0u; phase < 2u; ++phase)
                {
                    updateConstants(0u, iteration, phase);
                    bindMode3();
                    dispatch(runtime.distanceConstraintCS.Get(), runtime.groupCount, 64u);
                    N01UnbindCompute(m_context.Get());
                }
                for (uint32_t phase = 0u; phase < 3u; ++phase)
                {
                    updateConstants(0u, iteration, phase);
                    bindMode3();
                    dispatch(runtime.bendConstraintCS.Get(), runtime.groupCount, 64u);
                    N01UnbindCompute(m_context.Get());
                }
                updateConstants(0u, iteration, 0u);
                bindMode3();
                dispatch(runtime.collisionCS.Get(), runtime.groupCount, 64u);
                N01UnbindCompute(m_context.Get());
            }
        }

        const auto bindMode6 = [&]()
        {
            ID3D11ShaderResourceView* srvs[3] =
            {
                runtime.cores.srv.Get(),
                runtime.extensions.srv.Get(),
                runtime.descriptors.srv.Get()
            };
            ID3D11UnorderedAccessView* uavs[4] =
            {
                m_positions[m_currentBufferIndex].uav.Get(),
                m_previousPositions[m_currentBufferIndex].uav.Get(),
                runtime.sharedCandidates.uav.Get(),
                runtime.counters.uav.Get()
            };
            m_context->CSSetShaderResources(0u, 3u, srvs);
            m_context->CSSetUnorderedAccessViews(0u, 4u, uavs, nullptr);
        };

        updateConstants(0u, 0u, 0u);
        bindMode6();
        dispatch(runtime.buildSharedCandidatesCS.Get(), runtime.groupCount, 64u);
        N01UnbindCompute(m_context.Get());

        updateConstants(0u, 0u, 0u);
        bindMode6();
        dispatch(runtime.resolveSharedPointsCS.Get(), runtime.groupCount, 64u);
        N01UnbindCompute(m_context.Get());
        EndGpuPassTiming(GpuPassId::DynamicSolve);

        BeginGpuPassTiming(GpuPassId::VelocityRecovery);
        updateConstants(0u, 0u, 0u);
        bindMode3();
        dispatch(runtime.velocityRecoveryCS.Get(), runtime.groupCount, 64u);
        N01UnbindCompute(m_context.Get());
        EndGpuPassTiming(GpuPassId::VelocityRecovery);

        BeginGpuPassTiming(GpuPassId::RepairSolve);
        updateConstants(0u, 0u, 0u);
        bindMode2(runtime.payloadReadIndex, nextPayloadIndex);
        dispatch(runtime.repairCS.Get(), runtime.groupCount, 64u);
        N01UnbindCompute(m_context.Get());
        EndGpuPassTiming(GpuPassId::RepairSolve);

        BeginGpuPassTiming(GpuPassId::FollowerReconstruction);

        ExecuteNovelty01DirtyRangePropagation();

        N01Constants followerConstants = constants;
        followerConstants.groupCount =
            std::max<uint32_t>(m_asset.followerStrandCount, 1u);
        followerConstants.scanBlockCount =
            runtime.followerScanBlockCount;
        constants = followerConstants;

        updateConstants(2u, 0u, 0u);
        {
            ID3D11ShaderResourceView* srvs[1] =
            {
                runtime.followerFlags.srv.Get()
            };
            ID3D11UnorderedAccessView* uavs[2] =
            {
                runtime.followerPrefix.uav.Get(),
                runtime.followerScanBlockCounts.uav.Get()
            };
            m_context->CSSetShaderResources(0u, 1u, srvs);
            m_context->CSSetUnorderedAccessViews(0u, 2u, uavs, nullptr);
            dispatch(
                runtime.scanLocalCS.Get(),
                m_asset.followerStrandCount,
                256u);
            N01UnbindCompute(m_context.Get());
        }

        updateConstants(2u, 0u, 0u);
        {
            ID3D11UnorderedAccessView* uavs[4] =
            {
                nullptr,
                runtime.followerScanBlockCounts.uav.Get(),
                runtime.followerScanBlockOffsets.uav.Get(),
                runtime.counters.uav.Get()
            };
            m_context->CSSetUnorderedAccessViews(0u, 4u, uavs, nullptr);
            dispatch(runtime.scanBlocksCS.Get(), 1u, 1u);
            N01UnbindCompute(m_context.Get());
        }

        updateConstants(2u, 0u, 0u);
        {
            ID3D11ShaderResourceView* srvs[3] =
            {
                runtime.followerFlags.srv.Get(),
                runtime.followerPrefix.srv.Get(),
                runtime.followerScanBlockOffsets.srv.Get()
            };
            ID3D11UnorderedAccessView* uavs[5] =
            {
                nullptr, nullptr, nullptr, nullptr,
                runtime.activeFollowerList.uav.Get()
            };
            m_context->CSSetShaderResources(0u, 3u, srvs);
            m_context->CSSetUnorderedAccessViews(0u, 5u, uavs, nullptr);
            dispatch(
                runtime.scatterListsCS.Get(),
                m_asset.followerStrandCount,
                256u);
            N01UnbindCompute(m_context.Get());
        }

        ExecuteNovelty01BuildIndirectArguments();

        constants = runtime.constants;
        updateConstants(1u, 0u, 0u);
        bindMode4();
        if (runtime.indirectDispatchReady &&
            (runtime.featureMask &
             hairgpu::novelty01::Feature_IndirectDispatch) != 0u)
        {
            m_context->CSSetShader(runtime.rootBindCS.Get(), nullptr, 0u);
            m_context->DispatchIndirect(
                runtime.indirectArguments.Get(),
                36u);
        }
        else
        {
            dispatch(
                runtime.rootBindCS.Get(),
                m_asset.followerStrandCount,
                64u);
        }
        N01UnbindCompute(m_context.Get());

        updateConstants(1u, 0u, 0u);
        bindMode4();
        if (runtime.indirectDispatchReady &&
            (runtime.featureMask &
             hairgpu::novelty01::Feature_IndirectDispatch) != 0u)
        {
            m_context->CSSetShader(runtime.followerLocalCS.Get(), nullptr, 0u);
            m_context->DispatchIndirect(
                runtime.indirectArguments.Get(),
                48u);
        }
        else
        {
            dispatch(
                runtime.followerLocalCS.Get(),
                m_asset.followerStrandCount *
                    m_asset.particlesPerStrand,
                256u);
        }
        N01UnbindCompute(m_context.Get());
        EndGpuPassTiming(GpuPassId::FollowerReconstruction);

        if ((runtime.featureMask &
             hairgpu::novelty01::Feature_Watchdog) != 0u)
        {
            BeginGpuPassTiming(GpuPassId::Watchdog);
            updateConstants(0u, 0u, 0u);
            bindMode3();
            dispatch(runtime.watchdogCS.Get(), runtime.groupCount, 64u);
            N01UnbindCompute(m_context.Get());
            EndGpuPassTiming(GpuPassId::Watchdog);
        }

        BeginGpuPassTiming(GpuPassId::CacheUpdate);
        updateConstants(0u, 0u, 0u);
        bindMode2(runtime.payloadReadIndex, nextPayloadIndex);
        dispatch(runtime.updateCacheCS.Get(), runtime.groupCount, 64u);
        N01UnbindCompute(m_context.Get());

        if ((runtime.featureMask &
             hairgpu::novelty01::Feature_ShadowDynamic) != 0u)
        {
            updateConstants(0u, 0u, 0u);
            bindMode2(runtime.payloadReadIndex, nextPayloadIndex);
            dispatch(runtime.shadowValidateCS.Get(), runtime.groupCount, 64u);
            N01UnbindCompute(m_context.Get());
        }
        EndGpuPassTiming(GpuPassId::CacheUpdate);

        runtime.payloadReadIndex = nextPayloadIndex;
        runtime.forceAllFollowers = false;

        if (runtime.formalGroundTruthValidThisFrame)
        {
            CompareNovelty01GroundTruth();
        }

        Novelty01RuntimeState::ReadbackSlot& slot =
            runtime.readbacks[runtime.readbackWriteIndex];
        if (!slot.issued)
        {
            const auto ensureStaging = [&](ID3D11Buffer* source,
                                           Microsoft::WRL::ComPtr<ID3D11Buffer>& staging)
            {
                if (staging)
                {
                    return true;
                }
                D3D11_BUFFER_DESC desc = {};
                source->GetDesc(&desc);
                desc.Usage = D3D11_USAGE_STAGING;
                desc.BindFlags = 0u;
                desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                desc.MiscFlags = 0u;
                desc.StructureByteStride = 0u;
                return SUCCEEDED(m_device->CreateBuffer(
                    &desc,
                    nullptr,
                    &staging));
            };

            const bool formalStagingReady =
                !runtime.formalGroundTruthValidThisFrame ||
                ensureStaging(
                    runtime.groundTruthMetrics.buffer.Get(),
                    slot.formalMetrics);
            if (ensureStaging(runtime.counters.buffer.Get(), slot.counters) &&
                ensureStaging(runtime.dynamicList.buffer.Get(), slot.dynamicList) &&
                ensureStaging(runtime.repairList.buffer.Get(), slot.repairList) &&
                formalStagingReady)
            {
                m_context->CopyResource(
                    slot.counters.Get(),
                    runtime.counters.buffer.Get());
                m_context->CopyResource(
                    slot.dynamicList.Get(),
                    runtime.dynamicList.buffer.Get());
                m_context->CopyResource(
                    slot.repairList.Get(),
                    runtime.repairList.buffer.Get());
                slot.formalMetricsIssued =
                    runtime.formalGroundTruthValidThisFrame;
                if (slot.formalMetricsIssued)
                {
                    m_context->CopyResource(
                        slot.formalMetrics.Get(),
                        runtime.groundTruthMetrics.buffer.Get());
                }
                slot.sourceFrame = m_globalFrameIndex;
                slot.ageFrames = 0u;
                slot.issued = true;
                runtime.readbackWriteIndex =
                    (runtime.readbackWriteIndex + 1u) %
                    static_cast<uint32_t>(runtime.readbacks.size());
            }
        }
    }
