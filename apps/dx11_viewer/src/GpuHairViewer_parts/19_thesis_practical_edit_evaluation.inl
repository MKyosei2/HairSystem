// Thesis practical edit evaluation.
// B0 and P receive the same sparse physical edit. P invalidates only directly
// touched SG-APT blocks. CSV output is buffered until post-measurement flush.

namespace hairgpu::viewer
{
    namespace
    {
        struct ThesisPracticalEpisodeRecord
        {
            hairgpu::ThesisEditEpisodeSummary summary = {};
            uint32_t methodId = 0u;
            std::string methodLabel;
            std::string caseName;
        };

        struct ThesisPracticalGpuState
        {
            uint64_t nextEpisodeId = 1u;
            bool pending = false;
            bool localInvalidationQueued = false;
            bool forceFallback = false;
            bool prepared = false;
            bool prepareFailed = false;
            std::vector<uint32_t> pendingGroups;
            std::vector<uint32_t> pendingBlocks;
            std::vector<uint32_t> lastEditedGuides;
            bool lastVisualizationFallback = false;
            std::vector<ThesisPracticalEpisodeRecord> bufferedEpisodes;
            Microsoft::WRL::ComPtr<ID3D11Buffer> blockIndexBuffer;
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> blockIndexSrv;
            Microsoft::WRL::ComPtr<ID3D11ComputeShader> invalidateBlocksCS;
            uint32_t blockIndexCapacity = 0u;
        };

        std::unordered_map<GpuHairViewer*, ThesisPracticalGpuState>& ThesisPracticalStates()
        {
            static std::unordered_map<GpuHairViewer*, ThesisPracticalGpuState> states;
            return states;
        }

        bool ThesisEnabled(const wchar_t* name, bool fallback = false)
        {
            const std::wstring value = GetEnvironmentVariableString(name);
            return value.empty() ? fallback : EnvironmentValueIsTrue(value);
        }

        uint32_t ThesisUInt(const wchar_t* name, uint32_t fallback,
                            uint32_t minimumValue, uint32_t maximumValue)
        {
            return std::clamp(EnvironmentValueToUInt(
                GetEnvironmentVariableString(name), fallback),
                minimumValue, maximumValue);
        }

        float ThesisFloat(const wchar_t* name, float fallback,
                          float minimumValue, float maximumValue)
        {
            const std::wstring value = GetEnvironmentVariableString(name);
            if (value.empty()) return fallback;
            try { return std::clamp(std::stof(value), minimumValue, maximumValue); }
            catch (...) { return fallback; }
        }

        const char* ThesisMethodLabel(GpuHairViewer::ExperimentMethodId method)
        {
            if (method == GpuHairViewer::ExperimentMethodId::B0_FullDynamic) return "B0";
            if (method == GpuHairViewer::ExperimentMethodId::P_IntegratedSegmentGroup) return "P";
            return "OTHER";
        }

        bool ThesisCreateStructuredUintSrv(
            ID3D11Device* device, uint32_t elementCount,
            Microsoft::WRL::ComPtr<ID3D11Buffer>& buffer,
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>& srv)
        {
            if (!device || elementCount == 0u) return false;
            D3D11_BUFFER_DESC desc = {};
            desc.ByteWidth = static_cast<UINT>(sizeof(uint32_t) *
                static_cast<uint64_t>(elementCount));
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
            desc.StructureByteStride = sizeof(uint32_t);
            if (FAILED(device->CreateBuffer(&desc, nullptr, &buffer)) || !buffer) return false;
            D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Format = DXGI_FORMAT_UNKNOWN;
            srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
            srvDesc.Buffer.NumElements = elementCount;
            if (FAILED(device->CreateShaderResourceView(buffer.Get(), &srvDesc, &srv)) || !srv)
            {
                buffer.Reset();
                return false;
            }
            return true;
        }

        uint64_t ThesisHashMix64(uint64_t hash, uint64_t value)
        {
            for (uint32_t byteIndex = 0u; byteIndex < 8u; ++byteIndex)
            {
                hash ^= static_cast<uint8_t>((value >> (byteIndex * 8u)) & 0xffu);
                hash *= 1099511628211ull;
            }
            return hash;
        }
    }

    void GpuHairViewer::PrepareThesisPracticalEditEvaluation()
    {
        if (!ThesisEnabled(L"HAIRGPU_THESIS_EDIT_EVAL")) return;
        ThesisPracticalGpuState& state = ThesisPracticalStates()[this];
        if (state.prepared || state.prepareFailed) return;
        const N01RuntimePointer runtime = N01FindRuntime(this);
        if (!runtime || !runtime->ready || !m_device || !m_context) return;

        ID3DBlob* rawBlob = nullptr;
        std::wstring errorText;
        if (!CompileShaderFromFile(L"HairThesisLocalEditInvalidate.hlsl",
                "ThesisInvalidateBlocksCS", "cs_5_0", &rawBlob, errorText) || !rawBlob)
        {
            state.prepareFailed = true;
            return;
        }
        Microsoft::WRL::ComPtr<ID3DBlob> blob;
        blob.Attach(rawBlob);
        if (FAILED(m_device->CreateComputeShader(blob->GetBufferPointer(),
                blob->GetBufferSize(), nullptr, &state.invalidateBlocksCS)) ||
            !state.invalidateBlocksCS)
        {
            state.prepareFailed = true;
            return;
        }

        state.blockIndexCapacity = std::max<uint32_t>(runtime->blockCount + 1u, 64u);
        if (!ThesisCreateStructuredUintSrv(m_device.Get(), state.blockIndexCapacity,
                state.blockIndexBuffer, state.blockIndexSrv))
        {
            state.prepareFailed = true;
            state.invalidateBlocksCS.Reset();
            return;
        }
        state.prepared = true;
    }

    void GpuHairViewer::RecordThesisPracticalEditEpisode(
        const std::vector<uint32_t>& pointIndices)
    {
        if (!ThesisEnabled(L"HAIRGPU_THESIS_EDIT_EVAL") || pointIndices.empty()) return;

        ThesisPracticalGpuState& state = ThesisPracticalStates()[this];
        state.pendingGroups.clear();
        state.pendingBlocks.clear();
        state.pending = false;
        state.localInvalidationQueued = false;
        state.forceFallback = ThesisEnabled(L"HAIRGPU_THESIS_FORCE_FULL_FALLBACK");

        const uint32_t guideCount = std::min<uint32_t>(
            m_asset.guideStrandCount, static_cast<uint32_t>(m_asset.strands.size()));
        std::unordered_set<uint32_t> touchedGuides;
        std::unordered_set<uint32_t> touchedFollowers;
        std::unordered_set<uint32_t> directGroups;

        size_t strandCursor = 0u;
        for (uint32_t pointIndex : pointIndices)
        {
            while (strandCursor + 1u < m_asset.strands.size() &&
                   pointIndex >= m_asset.strands[strandCursor + 1u].pointOffset)
                ++strandCursor;
            if (strandCursor >= m_asset.strands.size()) continue;
            const StrandRange& strand = m_asset.strands[strandCursor];
            if (pointIndex < strand.pointOffset ||
                pointIndex >= strand.pointOffset + strand.pointCount ||
                strand.pointCount < 2u) continue;

            uint32_t ownerGuide = static_cast<uint32_t>(strandCursor);
            if (ownerGuide >= guideCount)
            {
                touchedFollowers.insert(ownerGuide);
                if (ownerGuide >= m_asset.followData.size() ||
                    m_asset.followData[ownerGuide].parentGuide < 0) continue;
                ownerGuide = static_cast<uint32_t>(m_asset.followData[ownerGuide].parentGuide);
            }
            if (ownerGuide >= guideCount ||
                ownerGuide >= m_asset.segmentGroupTopology.strandGroupRanges.size()) continue;

            touchedGuides.insert(ownerGuide);
            const uint32_t localPoint = pointIndex - strand.pointOffset;
            const SegmentGroupRange& range =
                m_asset.segmentGroupTopology.strandGroupRanges[ownerGuide];
            auto appendSegment = [&](uint32_t localSegment)
            {
                if (localSegment < range.groupCount)
                    directGroups.insert(range.firstGroup + localSegment);
            };
            if (localPoint > 0u) appendSegment(localPoint - 1u);
            if (localPoint + 1u < strand.pointCount) appendSegment(localPoint);
        }

        state.pendingGroups.assign(directGroups.begin(), directGroups.end());
        std::sort(state.pendingGroups.begin(), state.pendingGroups.end());
        const N01RuntimePointer runtime = N01FindRuntime(this);
        if (runtime && m_experimentCase.methodId == ExperimentMethodId::P_IntegratedSegmentGroup)
        {
            for (uint32_t groupIndex : state.pendingGroups)
                if (groupIndex < runtime->cpuCores.size())
                    state.pendingBlocks.push_back(runtime->cpuCores[groupIndex].blockIndex);
            std::sort(state.pendingBlocks.begin(), state.pendingBlocks.end());
            state.pendingBlocks.erase(std::unique(state.pendingBlocks.begin(),
                state.pendingBlocks.end()), state.pendingBlocks.end());
            state.pending = state.prepared && !state.prepareFailed &&
                !state.pendingBlocks.empty() && !state.forceFallback;
            state.localInvalidationQueued = state.pending;
        }

        const uint32_t totalGroups = runtime ? runtime->groupCount :
            m_asset.segmentGroupTopology.totalGroupCount;
        const uint32_t totalBlocks = runtime ? runtime->blockCount :
            static_cast<uint32_t>(m_asset.segmentGroupTopology.blocks.size());

        uint64_t physicalEditHash = 1469598103934665603ull;
        for (uint32_t pointIndex : pointIndices)
        {
            physicalEditHash = ThesisHashMix64(physicalEditHash, pointIndex);
            if (pointIndex < m_asset.restPoints.size())
            {
                const Vec3& p = m_asset.restPoints[pointIndex];
                const int64_t qx = static_cast<int64_t>(std::llround(static_cast<double>(p.x) * 1000000.0));
                const int64_t qy = static_cast<int64_t>(std::llround(static_cast<double>(p.y) * 1000000.0));
                const int64_t qz = static_cast<int64_t>(std::llround(static_cast<double>(p.z) * 1000000.0));
                physicalEditHash = ThesisHashMix64(physicalEditHash, static_cast<uint64_t>(qx));
                physicalEditHash = ThesisHashMix64(physicalEditHash, static_cast<uint64_t>(qy));
                physicalEditHash = ThesisHashMix64(physicalEditHash, static_cast<uint64_t>(qz));
            }
        }

        ThesisPracticalEpisodeRecord record = {};
        auto& s = record.summary;
        s.episodeId = state.nextEpisodeId++;
        s.frameIndex = m_globalFrameIndex;
        s.physicalEditHash = physicalEditHash;
        s.requestedGuideCount = ThesisUInt(L"HAIRGPU_THESIS_EDIT_GUIDE_COUNT", 1u, 1u,
            std::max<uint32_t>(guideCount, 1u));
        s.requestedTipOffsetMm = ThesisFloat(L"HAIRGPU_THESIS_EDIT_TIP_MM", 3.0f, 0.1f, 50.0f);
        s.changedPointCount = static_cast<uint32_t>(pointIndices.size());
        s.touchedGuideCount = static_cast<uint32_t>(touchedGuides.size());
        s.touchedFollowerCount = static_cast<uint32_t>(touchedFollowers.size());
        s.directGroupCount = static_cast<uint32_t>(state.pendingGroups.size());
        s.directBlockCount = static_cast<uint32_t>(state.pendingBlocks.size());
        s.totalGroupCount = totalGroups;
        s.totalBlockCount = totalBlocks;
        s.localInvalidationQueued = state.localInvalidationQueued ? 1u : 0u;
        s.forcedFallbackRequested = state.forceFallback ? 1u : 0u;
        s.directGroupRatio = hairgpu::ThesisSafeRatio(s.directGroupCount, totalGroups);
        s.directBlockRatio = hairgpu::ThesisSafeRatio(s.directBlockCount, totalBlocks);

        LARGE_INTEGER editCommit = {};
        QueryPerformanceCounter(&editCommit);
        s.editCommitQpc = static_cast<uint64_t>(editCommit.QuadPart);

        state.lastEditedGuides.assign(touchedGuides.begin(), touchedGuides.end());
        std::sort(state.lastEditedGuides.begin(), state.lastEditedGuides.end());
        state.lastVisualizationFallback =
            state.forceFallback &&
            m_experimentCase.methodId == ExperimentMethodId::P_IntegratedSegmentGroup;

        record.methodId = static_cast<uint32_t>(m_experimentCase.methodId);
        record.methodLabel = ThesisMethodLabel(m_experimentCase.methodId);
        record.caseName = WideToUtf8(GetEnvironmentVariableString(L"HAIRGPU_THESIS_CASE"));
        if (record.caseName.empty()) record.caseName = "unspecified";
        state.bufferedEpisodes.push_back(std::move(record));
    }

    bool GpuHairViewer::InjectThesisPracticalLocalEdit()
    {
        if (!ThesisEnabled(L"HAIRGPU_THESIS_EDIT_EVAL")) return false;
        PrepareThesisPracticalEditEvaluation();
        const uint32_t guideCount = std::min<uint32_t>(m_asset.guideStrandCount,
            static_cast<uint32_t>(m_asset.strands.size()));
        if (guideCount == 0u || m_baseAsset.strands.empty()) return false;

        const uint32_t requestedGuideCount = ThesisUInt(
            L"HAIRGPU_THESIS_EDIT_GUIDE_COUNT", 1u, 1u, guideCount);
        const float tipOffsetMm = ThesisFloat(
            L"HAIRGPU_THESIS_EDIT_TIP_MM", 3.0f, 0.1f, 50.0f);
        std::vector<uint32_t> guides;
        guides.reserve(requestedGuideCount);
        const uint32_t start = (m_scenarioEditInvalidationCount * 977u +
            m_globalFrameIndex * 17u) % guideCount;
        constexpr uint32_t kStride = 131u;
        for (uint32_t i = 0u; i < guideCount && guides.size() < requestedGuideCount; ++i)
        {
            const uint32_t guide = (start + i * kStride) % guideCount;
            if (std::find(guides.begin(), guides.end(), guide) == guides.end())
                guides.push_back(guide);
        }
        if (guides.empty()) return false;

        hairgpu::EditStack stack = {};
        hairgpu::EditLayer layer = {};
        layer.name = "thesis_practical_local_edit";
        layer.enabled = true;
        layer.layerWeight = 1.0f;
        layer.region.mode = hairgpu::RegionSelectMode::All;
        layer.region.guidesOnly = true;
        layer.edit.semantic = hairgpu::GroomEditSemantic::StrandFix;
        layer.edit.lengthScale = 1.0f;
        layer.edit.widthScale = 1.0f;
        layer.edit.rootOffset = hairgpu::Vec3(0.0f, 0.0f, 0.0f);
        const float sign = ((m_scenarioEditInvalidationCount & 1u) == 0u) ? 1.0f : -1.0f;
        layer.edit.tipOffset = hairgpu::Vec3(sign * tipOffsetMm * 0.001f,
            tipOffsetMm * 0.00035f, 0.0f);
        layer.edit.followerInfluence = 0.0f;
        layer.strandSelection = guides;
        layer.strandSelectionMode = hairgpu::StrandSelectionMode::GuideFamily;
        layer.strandSelectionWeight = 1.0f;
        stack.layers.push_back(layer);

        hairgpu::SparseEditResult sparseResult = {};
        hairgpu::RuntimeEditState runtimeState = {};
        std::string error;
        if (!hairgpu::ApplyEditStackToGuideFamiliesSparse(m_asset, m_baseAsset, stack,
                guides, &sparseResult, &runtimeState, error) ||
            sparseResult.changedPointIndices.empty()) return false;
        m_runtimeEditState = std::move(runtimeState);
        CommitEditedPointIndices(sparseResult.changedPointIndices);
        const ThesisPracticalGpuState& state = ThesisPracticalStates()[this];
        return m_experimentCase.methodId == ExperimentMethodId::P_IntegratedSegmentGroup &&
            state.localInvalidationQueued && !state.forceFallback;
    }

    void GpuHairViewer::ApplyThesisPracticalPendingEditInvalidation()
    {
        if (!ThesisEnabled(L"HAIRGPU_THESIS_EDIT_EVAL")) return;
        ThesisPracticalGpuState& state = ThesisPracticalStates()[this];
        if (!state.pending)
        {
            if (state.forceFallback)
            {
                state.forceFallback = false;
                state.pendingBlocks.clear();
                state.pendingGroups.clear();
            }
            return;
        }
        auto failClosed = [&]()
        {
            ++m_editVersion;
            m_forceAllSegmentGroupsDirty = true;
            state.pending = false;
            state.localInvalidationQueued = false;
            state.forceFallback = false;
            state.pendingBlocks.clear();
            state.pendingGroups.clear();
        };
        const N01RuntimePointer runtime = N01FindRuntime(this);
        if (!runtime || !runtime->ready || !runtime->blocks.uav || !m_context ||
            !state.prepared || !state.invalidateBlocksCS || !state.blockIndexBuffer ||
            !state.blockIndexSrv || state.pendingBlocks.empty() ||
            state.pendingBlocks.size() + 1u > state.blockIndexCapacity)
        {
            failClosed();
            return;
        }
        std::vector<uint32_t> upload(state.pendingBlocks.size() + 1u, 0xffffffffu);
        upload[0] = static_cast<uint32_t>(state.pendingBlocks.size());
        std::copy(state.pendingBlocks.begin(), state.pendingBlocks.end(), upload.begin() + 1u);
        m_context->UpdateSubresource(state.blockIndexBuffer.Get(), 0u, nullptr,
            upload.data(), 0u, 0u);
        ID3D11ShaderResourceView* srv = state.blockIndexSrv.Get();
        ID3D11UnorderedAccessView* uav = runtime->blocks.uav.Get();
        m_context->CSSetShader(state.invalidateBlocksCS.Get(), nullptr, 0u);
        m_context->CSSetShaderResources(0u, 1u, &srv);
        m_context->CSSetUnorderedAccessViews(0u, 1u, &uav, nullptr);
        m_context->Dispatch(static_cast<UINT>((state.pendingBlocks.size() + 63u) / 64u), 1u, 1u);
        ID3D11ShaderResourceView* nullSrv = nullptr;
        ID3D11UnorderedAccessView* nullUav = nullptr;
        m_context->CSSetShaderResources(0u, 1u, &nullSrv);
        m_context->CSSetUnorderedAccessViews(0u, 1u, &nullUav, nullptr);
        m_context->CSSetShader(nullptr, nullptr, 0u);
        state.pending = false;
        state.forceFallback = false;
        state.pendingBlocks.clear();
        state.pendingGroups.clear();
    }

    void GpuHairViewer::MarkThesisPracticalFirstPresent(HRESULT presentResult)
    {
        if (!ThesisEnabled(L"HAIRGPU_THESIS_EDIT_EVAL") || FAILED(presentResult)) return;
        auto found = ThesisPracticalStates().find(this);
        if (found == ThesisPracticalStates().end()) return;

        LARGE_INTEGER now = {};
        QueryPerformanceCounter(&now);
        ThesisPracticalGpuState& state = found->second;
        for (ThesisPracticalEpisodeRecord& record : state.bufferedEpisodes)
        {
            auto& s = record.summary;
            if (s.firstPresentObserved != 0u || s.editCommitQpc == 0ull ||
                s.frameIndex > m_globalFrameIndex)
            {
                continue;
            }
            s.firstPresentObserved = 1u;
            s.firstPresentedFrameIndex = m_globalFrameIndex;
            s.firstPresentQpc = static_cast<uint64_t>(now.QuadPart);
            if (m_qpcFrequency.QuadPart > 0 &&
                s.firstPresentQpc >= s.editCommitQpc)
            {
                s.editToFirstPresentMilliseconds =
                    static_cast<double>(s.firstPresentQpc - s.editCommitQpc) *
                    1000.0 /
                    static_cast<double>(m_qpcFrequency.QuadPart);
                m_noveltyTelemetry.editLatencyMs =
                    static_cast<float>(s.editToFirstPresentMilliseconds);
            }
        }
    }

    bool GpuHairViewer::BuildThesisUpdateVisualizationCategories(
        std::vector<uint32_t>& outCategories) const
    {
        constexpr uint32_t kReused = 0u;
        constexpr uint32_t kUpdated = 1u;
        constexpr uint32_t kEdited = 2u;
        constexpr uint32_t kFallback = 3u;

        outCategories.assign(m_asset.strands.size(), kReused);
        auto found = ThesisPracticalStates().find(
            const_cast<GpuHairViewer*>(this));
        if (found == ThesisPracticalStates().end())
        {
            return false;
        }

        const ThesisPracticalGpuState& state = found->second;
        const uint32_t guideCount = std::min<uint32_t>(
            m_asset.guideStrandCount,
            static_cast<uint32_t>(m_asset.strands.size()));

        auto markGuideFamily = [&](uint32_t guideIndex, uint32_t category)
        {
            if (guideIndex >= guideCount) return;
            outCategories[guideIndex] =
                std::max(outCategories[guideIndex], category);
            for (uint32_t strandIndex = guideCount;
                 strandIndex < m_asset.strands.size() &&
                 strandIndex < m_asset.followData.size();
                 ++strandIndex)
            {
                if (m_asset.followData[strandIndex].parentGuide ==
                    static_cast<int32_t>(guideIndex))
                {
                    outCategories[strandIndex] =
                        std::max(outCategories[strandIndex], category);
                }
            }
        };

        const N01RuntimePointer runtime =
            N01FindRuntime(const_cast<GpuHairViewer*>(this));
        const bool runtimeFallback =
            runtime && runtime->lastFrameUsedFullDynamic;
        if (m_experimentCase.methodId ==
                ExperimentMethodId::P_IntegratedSegmentGroup &&
            (state.lastVisualizationFallback || runtimeFallback))
        {
            std::fill(outCategories.begin(), outCategories.end(), kFallback);
            return true;
        }

        if (m_experimentCase.methodId ==
            ExperimentMethodId::B0_FullDynamic)
        {
            for (uint32_t guideIndex = 0u;
                 guideIndex < guideCount;
                 ++guideIndex)
            {
                markGuideFamily(guideIndex, kUpdated);
            }
        }
        else if (runtime)
        {
            for (uint32_t guideIndex : runtime->activeGuides)
            {
                markGuideFamily(guideIndex, kUpdated);
            }
        }

        for (uint32_t guideIndex : state.lastEditedGuides)
        {
            markGuideFamily(guideIndex, kEdited);
        }
        return true;
    }

    void GpuHairViewer::FlushThesisPracticalEditEpisodes()
    {
        if (!ThesisEnabled(L"HAIRGPU_THESIS_EDIT_EVAL")) return;
        ThesisPracticalGpuState& state = ThesisPracticalStates()[this];
        if (state.bufferedEpisodes.empty()) return;
        std::wstring telemetryDirText = GetEnvironmentVariableString(L"HAIRGPU_TELEMETRY_DIR");
        if (telemetryDirText.empty()) telemetryDirText =
            GetEnvironmentVariableString(L"HAIRGPU_N01_TELEMETRY_DIR");
        if (telemetryDirText.empty()) return;
        std::error_code error;
        const std::filesystem::path telemetryDir(telemetryDirText);
        std::filesystem::create_directories(telemetryDir, error);
        if (error) return;
        std::wstring runName = GetEnvironmentVariableString(L"HAIRGPU_TELEMETRY_RUN_NAME");
        if (runName.empty()) runName = L"thesis_practical";
        const std::filesystem::path csvPath = telemetryDir / (runName + L"_thesis_edits.csv");
        const bool writeHeader = !std::filesystem::exists(csvPath) ||
            std::filesystem::file_size(csvPath, error) == 0u;
        std::ofstream out(csvPath, std::ios::binary | std::ios::app);
        if (!out) return;
        if (writeHeader)
            out << "schema,case,episode,frame,method_id,method_label,physical_edit_hash,"
                   "requested_guide_count,requested_tip_mm,changed_point_count,"
                   "touched_guide_count,touched_follower_count,direct_group_count,"
                   "direct_block_count,total_group_count,total_block_count,"
                   "direct_group_ratio,direct_block_ratio,local_invalidation_queued,"
                   "forced_fallback_requested,edit_commit_qpc,first_present_qpc,"
                   "first_present_frame,first_present_observed,edit_to_first_present_ms\n";
        for (const ThesisPracticalEpisodeRecord& record : state.bufferedEpisodes)
        {
            const auto& s = record.summary;
            out << hairgpu::kThesisEditEvaluationSchema << ',' << record.caseName << ','
                << s.episodeId << ',' << s.frameIndex << ',' << record.methodId << ','
                << record.methodLabel << ',' << s.physicalEditHash << ','
                << s.requestedGuideCount << ',' << s.requestedTipOffsetMm << ','
                << s.changedPointCount << ',' << s.touchedGuideCount << ','
                << s.touchedFollowerCount << ',' << s.directGroupCount << ','
                << s.directBlockCount << ',' << s.totalGroupCount << ','
                << s.totalBlockCount << ',' << s.directGroupRatio << ','
                << s.directBlockRatio << ',' << s.localInvalidationQueued << ','
                << s.forcedFallbackRequested << ',' << s.editCommitQpc << ','
                << s.firstPresentQpc << ',' << s.firstPresentedFrameIndex << ','
                << s.firstPresentObserved << ','
                << s.editToFirstPresentMilliseconds << '\n';
        }
        if (out) state.bufferedEpisodes.clear();
    }
}
