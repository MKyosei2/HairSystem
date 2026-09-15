// Thesis practical edit evaluation.
// Included by ../GpuHairViewer.cpp after the existing Novelty01 runtime parts.
// Reuses the Viewer's common environment/string helpers instead of duplicating them.

namespace hairgpu::viewer
{
    namespace
    {
        struct ThesisPracticalGpuState
        {
            uint64_t nextEpisodeId = 1u;
            bool pending = false;
            bool localInvalidationQueued = false;
            bool forceFallback = false;

            std::vector<uint32_t> pendingGroups;
            std::vector<uint32_t> pendingBlocks;

            Microsoft::WRL::ComPtr<ID3D11Buffer> blockIndexBuffer;
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> blockIndexSrv;
            Microsoft::WRL::ComPtr<ID3D11ComputeShader> invalidateBlocksCS;
            uint32_t blockIndexCapacity = 0u;
        };

        std::unordered_map<GpuHairViewer*, ThesisPracticalGpuState>&
        ThesisPracticalStates()
        {
            static std::unordered_map<GpuHairViewer*, ThesisPracticalGpuState> states;
            return states;
        }

        bool ThesisEnabled(const wchar_t* name, bool fallback = false)
        {
            const std::wstring value = GetEnvironmentVariableString(name);
            return value.empty() ? fallback : EnvironmentValueIsTrue(value);
        }

        uint32_t ThesisUInt(
            const wchar_t* name,
            uint32_t fallback,
            uint32_t minimumValue,
            uint32_t maximumValue)
        {
            return std::clamp(
                EnvironmentValueToUInt(
                    GetEnvironmentVariableString(name),
                    fallback),
                minimumValue,
                maximumValue);
        }

        float ThesisFloat(
            const wchar_t* name,
            float fallback,
            float minimumValue,
            float maximumValue)
        {
            const std::wstring value = GetEnvironmentVariableString(name);
            if (value.empty())
            {
                return fallback;
            }
            try
            {
                return std::clamp(std::stof(value), minimumValue, maximumValue);
            }
            catch (...)
            {
                return fallback;
            }
        }

        const char* ThesisMethodLabel(GpuHairViewer::ExperimentMethodId method)
        {
            if (method == GpuHairViewer::ExperimentMethodId::B0_FullDynamic)
            {
                return "B0";
            }
            if (method == GpuHairViewer::ExperimentMethodId::P_IntegratedSegmentGroup)
            {
                return "P";
            }
            return "OTHER";
        }

        bool ThesisCreateStructuredUintSrv(
            ID3D11Device* device,
            uint32_t elementCount,
            Microsoft::WRL::ComPtr<ID3D11Buffer>& buffer,
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>& srv)
        {
            if (!device || elementCount == 0u)
            {
                return false;
            }

            D3D11_BUFFER_DESC desc = {};
            desc.ByteWidth = static_cast<UINT>(
                sizeof(uint32_t) * static_cast<uint64_t>(elementCount));
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
            desc.StructureByteStride = sizeof(uint32_t);

            if (FAILED(device->CreateBuffer(&desc, nullptr, &buffer)) || !buffer)
            {
                return false;
            }

            D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Format = DXGI_FORMAT_UNKNOWN;
            srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
            srvDesc.Buffer.NumElements = elementCount;

            if (FAILED(device->CreateShaderResourceView(
                    buffer.Get(), &srvDesc, &srv)) || !srv)
            {
                buffer.Reset();
                return false;
            }
            return true;
        }
    }

    void GpuHairViewer::RecordThesisPracticalEditEpisode(
        const std::vector<uint32_t>& pointIndices)
    {
        if (!ThesisEnabled(L"HAIRGPU_THESIS_EDIT_EVAL") || pointIndices.empty())
        {
            return;
        }

        ThesisPracticalGpuState& state = ThesisPracticalStates()[this];
        state.pendingGroups.clear();
        state.pendingBlocks.clear();
        state.pending = false;
        state.localInvalidationQueued = false;
        state.forceFallback =
            ThesisEnabled(L"HAIRGPU_THESIS_FORCE_FULL_FALLBACK");

        const uint32_t guideCount = std::min<uint32_t>(
            m_asset.guideStrandCount,
            static_cast<uint32_t>(m_asset.strands.size()));

        std::unordered_set<uint32_t> touchedGuides;
        std::unordered_set<uint32_t> touchedFollowers;
        std::unordered_set<uint32_t> directGroups;

        size_t strandCursor = 0u;
        for (uint32_t pointIndex : pointIndices)
        {
            while (strandCursor + 1u < m_asset.strands.size() &&
                   pointIndex >= m_asset.strands[strandCursor + 1u].pointOffset)
            {
                ++strandCursor;
            }
            if (strandCursor >= m_asset.strands.size())
            {
                continue;
            }

            const StrandRange& strand = m_asset.strands[strandCursor];
            if (pointIndex < strand.pointOffset ||
                pointIndex >= strand.pointOffset + strand.pointCount ||
                strand.pointCount < 2u)
            {
                continue;
            }

            uint32_t ownerGuide = static_cast<uint32_t>(strandCursor);
            if (ownerGuide >= guideCount)
            {
                touchedFollowers.insert(ownerGuide);
                if (ownerGuide >= m_asset.followData.size() ||
                    m_asset.followData[ownerGuide].parentGuide < 0)
                {
                    continue;
                }
                ownerGuide =
                    static_cast<uint32_t>(m_asset.followData[ownerGuide].parentGuide);
            }

            if (ownerGuide >= guideCount ||
                ownerGuide >= m_asset.segmentGroupTopology.strandGroupRanges.size())
            {
                continue;
            }

            touchedGuides.insert(ownerGuide);
            const uint32_t localPoint = pointIndex - strand.pointOffset;
            const SegmentGroupRange& range =
                m_asset.segmentGroupTopology.strandGroupRanges[ownerGuide];

            auto appendSegment = [&](uint32_t localSegment)
            {
                if (localSegment < range.groupCount)
                {
                    directGroups.insert(range.firstGroup + localSegment);
                }
            };

            if (localPoint > 0u)
            {
                appendSegment(localPoint - 1u);
            }
            if (localPoint + 1u < strand.pointCount)
            {
                appendSegment(localPoint);
            }
        }

        state.pendingGroups.assign(directGroups.begin(), directGroups.end());
        std::sort(state.pendingGroups.begin(), state.pendingGroups.end());

        const N01RuntimePointer runtime = N01FindRuntime(this);
        if (runtime &&
            m_experimentCase.methodId ==
                ExperimentMethodId::P_IntegratedSegmentGroup)
        {
            for (uint32_t groupIndex : state.pendingGroups)
            {
                if (groupIndex < runtime->cpuCores.size())
                {
                    state.pendingBlocks.push_back(
                        runtime->cpuCores[groupIndex].blockIndex);
                }
            }
            std::sort(state.pendingBlocks.begin(), state.pendingBlocks.end());
            state.pendingBlocks.erase(
                std::unique(state.pendingBlocks.begin(), state.pendingBlocks.end()),
                state.pendingBlocks.end());

            state.pending = !state.pendingBlocks.empty() && !state.forceFallback;
            state.localInvalidationQueued = state.pending;
        }

        const uint32_t totalGroups =
            runtime ? runtime->groupCount
                    : m_asset.segmentGroupTopology.totalGroupCount;
        const uint32_t totalBlocks =
            runtime ? runtime->blockCount
                    : static_cast<uint32_t>(
                        m_asset.segmentGroupTopology.blocks.size());

        std::wstring telemetryDirText =
            GetEnvironmentVariableString(L"HAIRGPU_TELEMETRY_DIR");
        if (telemetryDirText.empty())
        {
            telemetryDirText =
                GetEnvironmentVariableString(L"HAIRGPU_N01_TELEMETRY_DIR");
        }
        if (telemetryDirText.empty())
        {
            return;
        }

        std::error_code error;
        const std::filesystem::path telemetryDir(telemetryDirText);
        std::filesystem::create_directories(telemetryDir, error);
        if (error)
        {
            return;
        }

        std::wstring runName =
            GetEnvironmentVariableString(L"HAIRGPU_TELEMETRY_RUN_NAME");
        if (runName.empty())
        {
            runName = L"thesis_practical";
        }

        const std::filesystem::path csvPath =
            telemetryDir / (runName + L"_thesis_edits.csv");
        const bool writeHeader =
            !std::filesystem::exists(csvPath) ||
            std::filesystem::file_size(csvPath, error) == 0u;

        std::ofstream out(csvPath, std::ios::binary | std::ios::app);
        if (!out)
        {
            return;
        }

        if (writeHeader)
        {
            out
                << "schema,case,episode,frame,method_id,method_label,"
                   "changed_point_count,touched_guide_count,touched_follower_count,"
                   "direct_group_count,direct_block_count,total_group_count,"
                   "total_block_count,direct_group_ratio,direct_block_ratio,"
                   "local_invalidation_queued,forced_fallback_requested\n";
        }

        std::wstring caseName =
            GetEnvironmentVariableString(L"HAIRGPU_THESIS_CASE");
        if (caseName.empty())
        {
            caseName = L"unspecified";
        }

        out
            << hairgpu::kThesisEditEvaluationSchema << ','
            << WideToUtf8(caseName) << ','
            << state.nextEpisodeId++ << ','
            << m_globalFrameIndex << ','
            << static_cast<uint32_t>(m_experimentCase.methodId) << ','
            << ThesisMethodLabel(m_experimentCase.methodId) << ','
            << pointIndices.size() << ','
            << touchedGuides.size() << ','
            << touchedFollowers.size() << ','
            << state.pendingGroups.size() << ','
            << state.pendingBlocks.size() << ','
            << totalGroups << ','
            << totalBlocks << ','
            << hairgpu::ThesisSafeRatio(state.pendingGroups.size(), totalGroups) << ','
            << hairgpu::ThesisSafeRatio(state.pendingBlocks.size(), totalBlocks) << ','
            << (state.localInvalidationQueued ? 1 : 0) << ','
            << (state.forceFallback ? 1 : 0) << '\n';
    }

    bool GpuHairViewer::InjectThesisPracticalLocalEdit()
    {
        if (!ThesisEnabled(L"HAIRGPU_THESIS_EDIT_EVAL"))
        {
            return false;
        }

        const uint32_t guideCount = std::min<uint32_t>(
            m_asset.guideStrandCount,
            static_cast<uint32_t>(m_asset.strands.size()));
        if (guideCount == 0u || m_baseAsset.strands.empty())
        {
            return false;
        }

        const uint32_t requestedGuideCount = ThesisUInt(
            L"HAIRGPU_THESIS_EDIT_GUIDE_COUNT", 1u, 1u, guideCount);
        const float tipOffsetMm = ThesisFloat(
            L"HAIRGPU_THESIS_EDIT_TIP_MM", 3.0f, 0.1f, 50.0f);

        std::vector<uint32_t> guides;
        guides.reserve(requestedGuideCount);
        const uint32_t start =
            (m_scenarioEditInvalidationCount * 977u +
             m_globalFrameIndex * 17u) % guideCount;
        constexpr uint32_t kStride = 131u;

        for (uint32_t i = 0u;
             i < guideCount && guides.size() < requestedGuideCount;
             ++i)
        {
            const uint32_t guide = (start + i * kStride) % guideCount;
            if (std::find(guides.begin(), guides.end(), guide) == guides.end())
            {
                guides.push_back(guide);
            }
        }
        if (guides.empty())
        {
            return false;
        }

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

        const float sign =
            ((m_scenarioEditInvalidationCount & 1u) == 0u) ? 1.0f : -1.0f;
        layer.edit.tipOffset = hairgpu::Vec3(
            sign * tipOffsetMm * 0.001f,
            tipOffsetMm * 0.00035f,
            0.0f);
        layer.edit.followerInfluence = 0.0f;
        layer.strandSelection = guides;
        layer.strandSelectionMode = hairgpu::StrandSelectionMode::GuideFamily;
        layer.strandSelectionWeight = 1.0f;
        stack.layers.push_back(layer);

        hairgpu::SparseEditResult sparseResult = {};
        hairgpu::RuntimeEditState runtimeState = {};
        std::string error;
        if (!hairgpu::ApplyEditStackToGuideFamiliesSparse(
                m_asset,
                m_baseAsset,
                stack,
                guides,
                &sparseResult,
                &runtimeState,
                error) ||
            sparseResult.changedPointIndices.empty())
        {
            return false;
        }

        m_runtimeEditState = std::move(runtimeState);
        CommitEditedPointIndices(sparseResult.changedPointIndices);

        const ThesisPracticalGpuState& state = ThesisPracticalStates()[this];
        return
            m_experimentCase.methodId ==
                ExperimentMethodId::P_IntegratedSegmentGroup &&
            state.localInvalidationQueued &&
            !state.forceFallback;
    }

    void GpuHairViewer::ApplyThesisPracticalPendingEditInvalidation()
    {
        if (!ThesisEnabled(L"HAIRGPU_THESIS_EDIT_EVAL"))
        {
            return;
        }

        ThesisPracticalGpuState& state = ThesisPracticalStates()[this];

        auto failClosed = [&]()
        {
            ++m_editVersion;
            m_forceAllSegmentGroupsDirty = true;
            state.pending = false;
            state.forceFallback = false;
            state.pendingBlocks.clear();
            state.pendingGroups.clear();
        };

        if (!state.pending)
        {
            if (state.forceFallback)
            {
                failClosed();
            }
            return;
        }

        const N01RuntimePointer runtime = N01FindRuntime(this);
        if (!runtime ||
            !runtime->ready ||
            !runtime->blocks.uav ||
            !m_device ||
            !m_context ||
            state.pendingBlocks.empty())
        {
            failClosed();
            return;
        }

        if (!state.invalidateBlocksCS)
        {
            ID3DBlob* rawBlob = nullptr;
            std::wstring errorText;
            if (!CompileShaderFromFile(
                    L"HairThesisLocalEditInvalidate.hlsl",
                    "ThesisInvalidateBlocksCS",
                    "cs_5_0",
                    &rawBlob,
                    errorText) ||
                !rawBlob)
            {
                failClosed();
                return;
            }

            Microsoft::WRL::ComPtr<ID3DBlob> blob;
            blob.Attach(rawBlob);
            if (FAILED(m_device->CreateComputeShader(
                    blob->GetBufferPointer(),
                    blob->GetBufferSize(),
                    nullptr,
                    &state.invalidateBlocksCS)) ||
                !state.invalidateBlocksCS)
            {
                failClosed();
                return;
            }
        }

        const uint32_t requiredElements =
            static_cast<uint32_t>(state.pendingBlocks.size()) + 1u;
        if (!state.blockIndexBuffer ||
            state.blockIndexCapacity < requiredElements)
        {
            state.blockIndexBuffer.Reset();
            state.blockIndexSrv.Reset();
            state.blockIndexCapacity = std::max<uint32_t>(
                requiredElements,
                std::max<uint32_t>(runtime->blockCount + 1u, 64u));

            if (!ThesisCreateStructuredUintSrv(
                    m_device.Get(),
                    state.blockIndexCapacity,
                    state.blockIndexBuffer,
                    state.blockIndexSrv))
            {
                failClosed();
                return;
            }
        }

        std::vector<uint32_t> upload(state.blockIndexCapacity, 0xffffffffu);
        upload[0] = static_cast<uint32_t>(state.pendingBlocks.size());
        std::copy(
            state.pendingBlocks.begin(),
            state.pendingBlocks.end(),
            upload.begin() + 1u);
        m_context->UpdateSubresource(
            state.blockIndexBuffer.Get(), 0u, nullptr, upload.data(), 0u, 0u);

        ID3D11ShaderResourceView* srv = state.blockIndexSrv.Get();
        ID3D11UnorderedAccessView* uav = runtime->blocks.uav.Get();
        m_context->CSSetShader(state.invalidateBlocksCS.Get(), nullptr, 0u);
        m_context->CSSetShaderResources(0u, 1u, &srv);
        m_context->CSSetUnorderedAccessViews(0u, 1u, &uav, nullptr);
        m_context->Dispatch(
            static_cast<UINT>((state.pendingBlocks.size() + 63u) / 64u),
            1u,
            1u);

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
}
