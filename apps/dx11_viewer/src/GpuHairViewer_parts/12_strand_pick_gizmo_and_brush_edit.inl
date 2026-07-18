// GpuHairViewer.cpp ordered part 12: strand_pick_gizmo_and_brush_edit
// This file is included by ../GpuHairViewer.cpp; do not compile it directly.

        switch (m_groomTool)
        {
        case GroomTool::Comb:
            layer.edit.rootOffset = worldDelta * 0.10f;
            layer.edit.tipOffset = worldDelta;
            layer.edit.sideOffset = hairgpu::Clamp(dxPixels * 0.0008f, -0.20f, 0.20f);
            layer.edit.yawRadians = hairgpu::Clamp(angle * 0.20f, -1.0f, 1.0f);
            break;
        case GroomTool::Grab:
            layer.edit.rootOffset = worldDelta * 0.35f;
            layer.edit.tipOffset = worldDelta;
            break;
        case GroomTool::Smooth:
            layer.layerWeight = hairgpu::Clamp(0.35f + signedMagnitude * 0.35f, 0.0f, 1.0f);
            layer.edit.rootOffset = worldDelta * 0.03f;
            layer.edit.tipOffset = worldDelta * 0.08f;
            layer.edit.widthScale = 1.0f;
            layer.edit.lengthScale = 1.0f;
            layer.edit.followerInfluence = 1.25f;
            break;
        case GroomTool::Length:
            layer.edit.lengthScale = hairgpu::Clamp(1.0f + verticalSign * signedMagnitude * 0.35f, 0.6f, 1.4f);
            layer.edit.tipOffset.y += verticalSign * signedMagnitude * 0.015f;
            break;
        case GroomTool::Volume:
            layer.edit.widthScale = hairgpu::Clamp(1.0f + verticalSign * signedMagnitude * 0.55f, 0.5f, 1.8f);
            layer.edit.sideOffset = hairgpu::Clamp(verticalSign * signedMagnitude * 0.08f, -0.20f, 0.20f);
            layer.edit.tipOffset = worldDelta * 0.20f;
            break;
        case GroomTool::Part:
            layer.region.angleCenterRadians = angle;
            layer.edit.yawRadians = hairgpu::Clamp(angle * 0.35f, -1.2f, 1.2f);
            layer.edit.sideOffset = hairgpu::Clamp(dxPixels * 0.0007f, -0.20f, 0.20f);
            layer.edit.tipOffset = worldDelta * 0.40f;
            break;
        case GroomTool::Freeze:
            layer.layerWeight = 1.0f;
            layer.edit.lengthScale = 1.0f;
            layer.edit.widthScale = 1.0f;
            layer.edit.followerInfluence = 1.0f;
            break;
        default:
            break;
        }

        const uint32_t guideCount = std::min<uint32_t>(m_asset.guideStrandCount, static_cast<uint32_t>(m_asset.strands.size()));
        const float radiusUSq = std::max(layer.region.radiusUv.x * layer.region.radiusUv.x, 1.0e-6f);
        const float radiusVSq = std::max(layer.region.radiusUv.y * layer.region.radiusUv.y, 1.0e-6f);
        uint32_t affected = 0u;
        if (m_asset.rootBindings.size() >= guideCount)
        {
            for (uint32_t guideIndex = 0u; guideIndex < guideCount; ++guideIndex)
            {
                if (IsStrandLocked(guideIndex) && m_groomTool != GroomTool::Freeze)
                {
                    continue;
                }
                const Vec2 uv = m_asset.rootBindings[guideIndex].rootUv;
                const float du = uv.x - centerUv.x;
                const float dv = uv.y - centerUv.y;
                const float ellipse = (du * du) / radiusUSq + (dv * dv) / radiusVSq;
                if (ellipse <= 1.0f)
                {
                    layer.strandSelection.push_back(guideIndex);
                    ++affected;
                }
            }
        }
        layer.strandSelectionMode = hairgpu::StrandSelectionMode::GuideFamily;
        layer.strandSelectionWeight = 1.0f;
        if (outAffectedGuideCount)
        {
            *outAffectedGuideCount = affected;
        }
        return layer;
    }

        bool GpuHairViewer::BeginViewportBrushStroke(POINT clientPoint)
    {
        Vec2 centerUv = { 0.5f, 0.5f };
        int32_t guideIndex = -1;
        float screenDistance = 0.0f;
        if (!ComputeBrushCenterUvFromScreenPoint(clientPoint, centerUv, &guideIndex, &screenDistance))
        {
            return false;
        }
        m_selectedStrandIndex = guideIndex;
        m_lastSelectedStrandScreenDistance = screenDistance;
        m_brushStrokeActive = true;
        m_brushStrokeSnapshotActive = false;
        m_brushStrokeStartPoint = clientPoint;
        m_brushStrokeLastPoint = clientPoint;
        m_brushStrokeStartUv = centerUv;
        m_brushStrokeCurrentUv = centerUv;
        m_brushStrokeAffectedGuideCount = 0u;
        m_brushStrokeBaseStack = {};
        const std::string inlineUtf8 = WideToUtf8(GetInlineEditStackText());
        std::string parseError;
        if (!inlineUtf8.empty())
        {
            LocalLoadEditStackCsvText(inlineUtf8, m_brushStrokeBaseStack, parseError);
        }
        if (!ApplyViewportBrushStroke(clientPoint, false))
        {
            m_brushStrokeActive = false;
            return false;
        }
        return true;
    }


    void GpuHairViewer::UpdateViewportBrushStroke(POINT clientPoint)
    {
        if (!m_brushStrokeActive)
        {
            return;
        }
        m_brushStrokeLastPoint = clientPoint;
        ApplyViewportBrushStroke(clientPoint, false);
    }

        bool GpuHairViewer::ApplyViewportBrushStroke(POINT clientPoint, bool commitStroke)
    {
        if (!m_brushStrokeActive && !commitStroke)
        {
            return false;
        }

        Vec2 centerUv = m_brushStrokeCurrentUv;
        int32_t guideIndex = m_selectedStrandIndex;
        float screenDistance = 0.0f;
        if (!ComputeBrushCenterUvFromScreenPoint(clientPoint, centerUv, &guideIndex, &screenDistance))
        {
            return false;
        }
        m_brushStrokeCurrentUv = centerUv;
        m_selectedStrandIndex = guideIndex;
        m_lastSelectedStrandScreenDistance = screenDistance;

        hairgpu::EditStack previewStack = m_brushStrokeBaseStack;
        previewStack.boundarySmoothing = hairgpu::Clamp(std::max(m_quickEditBoundarySmoothing,
            (m_groomTool == GroomTool::Smooth) ? 0.72f : ((m_groomTool == GroomTool::Comb || m_groomTool == GroomTool::Volume) ? 0.50f : m_quickEditBoundarySmoothing)), 0.0f, 1.0f);
        previewStack.boundaryRadiusUv = hairgpu::Clamp(std::max(m_quickEditBoundaryRadiusUv,
            (m_groomTool == GroomTool::Smooth) ? 0.14f : m_quickEditBoundaryRadiusUv), 0.01f, 0.30f);

        hairgpu::EditLayer layer = BuildViewportBrushLayer(
            centerUv, m_brushStrokeStartUv, m_brushStrokeCurrentUv, clientPoint,
            &m_brushStrokeAffectedGuideCount);
        if (layer.strandSelection.empty())
        {
            return false;
        }

        if (!m_brushStrokeSnapshotActive)
        {
            PushStrandEditUndoSnapshot(layer.strandSelection);
            m_brushStrokeSnapshotActive = true;
        }
        else
        {
            ExtendLastStrandEditUndoSnapshot(layer.strandSelection);
        }

        if (m_groomTool == GroomTool::Freeze)
        {
            const std::vector<uint32_t> familyStrands = ExpandGuideFamilies(layer.strandSelection);
            if (m_lockedStrands.size() < m_asset.strands.size())
            {
                m_lockedStrands.resize(m_asset.strands.size(), 0u);
            }
            for (uint32_t strandIndex : familyStrands)
            {
                if (strandIndex < m_lockedStrands.size())
                {
                    m_lockedStrands[strandIndex] = 1u;
                }
            }
            if (commitStroke)
            {
                FinalizeLastStrandEditUndoSnapshot();
                RecordViewportHistory(L"Freeze brush locked " + std::to_wstring(layer.strandSelection.size()) + L" guides");
            }
            UpdateWindowTitle();
            RefreshQuickEditFormLabels();
            return true;
        }

        previewStack.layers.push_back(layer);
        const hairgpu::HairAsset& baseAsset = m_baseAsset.strands.empty() ? m_asset : m_baseAsset;
        hairgpu::SparseEditResult sparseResult = {};
        std::string outError;
        hairgpu::RuntimeEditState runtimeState = {};
        if (!hairgpu::ApplyEditStackToGuideFamiliesSparse(
                m_asset, baseAsset, previewStack, layer.strandSelection,
                &sparseResult, &runtimeState, outError))
        {
            return false;
        }
        m_runtimeEditState = std::move(runtimeState);
        CommitEditedPointIndices(sparseResult.changedPointIndices);

        const std::wstring inlineText = Utf8ToWide(SerializeEditStackToCsvText(previewStack));
        SetWindowTextW(m_editInlineEditStack, inlineText.c_str());
        m_selectedInlineLayerIndex = static_cast<int>(previewStack.layers.size()) - 1;
        LoadQuickEditFormFromInlineText(m_selectedInlineLayerIndex);

        if (commitStroke)
        {
            FinalizeLastStrandEditUndoSnapshot();
            std::wstring entry = std::wstring(GetGroomToolDisplayName()) +
                L" stroke | guides=" + std::to_wstring(m_brushStrokeAffectedGuideCount) +
                L" | overlay=" + GetMaskOverlayModeDisplayName();
            RecordViewportHistory(entry);
        }
        UpdateWindowTitle();
        return true;
    }


        void GpuHairViewer::EndViewportBrushStroke(bool cancelStroke)
    {
        if (!m_brushStrokeActive)
        {
            return;
        }
        if (cancelStroke)
        {
            if (m_brushStrokeSnapshotActive && !m_strandUndoStack.empty())
            {
                StrandEditSnapshot snapshot = std::move(m_strandUndoStack.back());
                m_strandUndoStack.pop_back();
                ApplyStrandEditSnapshot(snapshot, false);
            }
        }
        else
        {
            ApplyViewportBrushStroke(m_brushStrokeLastPoint, true);
        }
        m_brushStrokeActive = false;
        m_brushStrokeSnapshotActive = false;
        m_brushStrokeBaseStack = {};
        m_brushStrokeAffectedGuideCount = 0u;
    }


    void GpuHairViewer::RecordViewportHistory(const std::wstring& entry)
    {
        if (entry.empty())
        {
            return;
        }
        m_viewportHistory.push_back(entry);
        if (m_viewportHistory.size() > 10u)
        {
            m_viewportHistory.erase(m_viewportHistory.begin());
        }
        RefreshQuickEditFormLabels();
    }

    std::wstring GpuHairViewer::BuildHistorySummaryText() const
    {
        std::wstring text = L"History / Notes\r\n";
        if (m_viewportHistory.empty())
        {
            text += L"- No viewport strokes yet.\r\n";
        }
        else
        {
            for (auto it = m_viewportHistory.rbegin(); it != m_viewportHistory.rend(); ++it)
            {
                text += L"- ";
                text += *it;
                text += L"\r\n";
            }
        }
        uint32_t lockedCount = 0u;
        for (uint8_t v : m_lockedStrands)
        {
            lockedCount += (v != 0u) ? 1u : 0u;
        }
        text += L"Locked strands: " + std::to_wstring(lockedCount);
        if (m_brushStrokeActive)
        {
            text += L"\r\nBrush active: ";
            text += GetGroomToolDisplayName();
            text += L" guides=" + std::to_wstring(m_brushStrokeAffectedGuideCount);
        }
        return text;
    }

    Vec3 GpuHairViewer::GetSelectedStrandAnchorPoint(bool root) const
    {
        if (m_selectedStrandIndex < 0 || static_cast<size_t>(m_selectedStrandIndex) >= m_asset.strands.size())
        {
            return Vec3(0.0f, 0.0f, 0.0f);
        }
        const StrandRange& strand = m_asset.strands[static_cast<size_t>(m_selectedStrandIndex)];
        if (strand.pointCount == 0u)
        {
            return Vec3(0.0f, 0.0f, 0.0f);
        }
        const uint32_t pointIndex = strand.pointOffset + (root ? 0u : (strand.pointCount - 1u));
        return m_asset.restPoints[pointIndex];
    }

        bool GpuHairViewer::BeginStrandGizmoDrag(POINT clientPoint)
    {
        if (m_selectedStrandIndex < 0 || static_cast<size_t>(m_selectedStrandIndex) >= m_asset.strands.size())
        {
            return false;
        }
        const bool rootMode = (m_strandGizmoMode == StrandGizmoMode::RootTranslate);
        const Vec3 anchor = GetSelectedStrandAnchorPoint(rootMode);
        float ax = 0.0f, ay = 0.0f;
        if (!ProjectWorldToScreen(anchor, ax, ay))
        {
            return false;
        }
        const float gizmoScale = std::max(0.06f, m_cameraDistance * 0.10f);
        struct Candidate { GizmoAxis axis; float distSq; } best = { GizmoAxis::None, 20.0f * 20.0f };
        const std::array<std::pair<GizmoAxis, Vec3>, 3> axes = {
            std::make_pair(GizmoAxis::X, Vec3(gizmoScale, 0.0f, 0.0f)),
            std::make_pair(GizmoAxis::Y, Vec3(0.0f, gizmoScale, 0.0f)),
            std::make_pair(GizmoAxis::Z, Vec3(0.0f, 0.0f, gizmoScale)) };
        for (const auto& candidate : axes)
        {
            float bx = 0.0f, by = 0.0f;
            if (!ProjectWorldToScreen(anchor + candidate.second, bx, by)) continue;
            const float abx = bx - ax, aby = by - ay;
            const float denom = std::max(abx * abx + aby * aby, 1.0e-5f);
            const float t = std::clamp(((clientPoint.x - ax) * abx + (clientPoint.y - ay) * aby) / denom, 0.0f, 1.0f);
            const float dx = static_cast<float>(clientPoint.x) - (ax + abx * t);
            const float dy = static_cast<float>(clientPoint.y) - (ay + aby * t);
            const float distSq = dx * dx + dy * dy;
            if (distSq < best.distSq) best = { candidate.first, distSq };
        }
        if (best.axis == GizmoAxis::None) return false;
        PushStrandEditUndoSnapshot();
        if (m_strandUndoStack.empty()) return false;
        m_gizmoDragSnapshot = m_strandUndoStack.back();
        m_activeGizmoAxis = best.axis;
        m_gizmoDragStartPoint = clientPoint;
        m_gizmoDragging = true;
        return true;
    }


        void GpuHairViewer::UpdateStrandGizmoDrag(POINT clientPoint)
    {
        if (!m_gizmoDragging || m_activeGizmoAxis == GizmoAxis::None ||
            m_selectedStrandIndex < 0 || static_cast<size_t>(m_selectedStrandIndex) >= m_asset.strands.size())
        {
            return;
        }
        const StrandRange& strand = m_asset.strands[static_cast<size_t>(m_selectedStrandIndex)];
        if (strand.pointCount == 0u) return;
        for (const SparsePointEdit& point : m_gizmoDragSnapshot.points)
        {
            if (point.pointIndex < m_asset.restPoints.size()) m_asset.restPoints[point.pointIndex] = point.before;
        }
        const float dx = static_cast<float>(clientPoint.x - m_gizmoDragStartPoint.x);
        const float dy = static_cast<float>(clientPoint.y - m_gizmoDragStartPoint.y);
        const float signedPixels = (m_activeGizmoAxis == GizmoAxis::Y) ? -dy : dx;
        const float scale = std::max(0.0008f, m_cameraDistance * 0.0009f);
        Vec3 delta = {};
        if (m_activeGizmoAxis == GizmoAxis::X) delta.x = signedPixels * scale;
        else if (m_activeGizmoAxis == GizmoAxis::Y) delta.y = signedPixels * scale;
        else if (m_activeGizmoAxis == GizmoAxis::Z) delta.z = signedPixels * scale;
        Vec3 rootDelta = {}, tipDelta = {};
        if (m_strandGizmoMode == StrandGizmoMode::WholeTranslate) rootDelta = tipDelta = delta;
        else if (m_strandGizmoMode == StrandGizmoMode::RootTranslate) rootDelta = delta;
        else tipDelta = delta;
        std::vector<uint32_t> changed;
        changed.reserve(strand.pointCount);
        for (uint32_t i = 0u; i < strand.pointCount; ++i)
        {
            const uint32_t pointIndex = strand.pointOffset + i;
            const float t = strand.pointCount > 1u ? static_cast<float>(i) / static_cast<float>(strand.pointCount - 1u) : 0.0f;
            m_asset.restPoints[pointIndex] += Lerp(rootDelta, tipDelta, t);
            changed.push_back(pointIndex);
        }
        CommitEditedPointIndices(changed);
    }


        void GpuHairViewer::EndStrandGizmoDrag(bool cancelDrag)
    {
        if (!m_gizmoDragging) return;
        if (cancelDrag)
        {
            ApplyStrandEditSnapshot(m_gizmoDragSnapshot, false);
            if (!m_strandUndoStack.empty()) m_strandUndoStack.pop_back();
        }
        else
        {
            FinalizeLastStrandEditUndoSnapshot();
        }
        m_gizmoDragging = false;
        m_activeGizmoAxis = GizmoAxis::None;
        m_gizmoDragSnapshot = {};
    }


        bool GpuHairViewer::PickStrandAtPoint(POINT clientPoint)
    {
        const uint32_t guideCount = std::min<uint32_t>(
            m_asset.guideStrandCount, static_cast<uint32_t>(m_asset.strands.size()));
        if (guideCount == 0u || m_asset.restPoints.empty())
        {
            m_selectedStrandIndex = -1;
            return false;
        }
        const RECT renderRect = GetRenderViewportRect();
        const float renderWidth = static_cast<float>(std::max<LONG>(renderRect.right - renderRect.left, 1));
        const float renderHeight = static_cast<float>(std::max<LONG>(renderRect.bottom - renderRect.top, 1));
        const float aspectRatio = std::max(renderWidth / std::max(renderHeight, 1.0f), 0.6f);
        constexpr uint32_t kPickStride = 4u;
        float bestDistSq = 18.0f * 18.0f;
        int32_t bestGuide = -1;
        for (uint32_t guideIndex = 0u; guideIndex < guideCount; ++guideIndex)
        {
            const StrandRange& strand = m_asset.strands[guideIndex];
            for (uint32_t i = 0u; i + 1u < strand.pointCount; i += kPickStride)
            {
                const uint32_t next = std::min<uint32_t>(i + kPickStride, strand.pointCount - 1u);
                const Vec3& a = m_asset.restPoints[strand.pointOffset + i];
                const Vec3& b = m_asset.restPoints[strand.pointOffset + next];
                Vec3 ndcA, ndcB;
                if (!ProjectWorldToNdc(a, m_cameraPosition, m_cameraTarget, aspectRatio, ndcA) ||
                    !ProjectWorldToNdc(b, m_cameraPosition, m_cameraTarget, aspectRatio, ndcB)) continue;
                const float ax = renderRect.left + (ndcA.x * 0.5f + 0.5f) * renderWidth;
                const float ay = renderRect.top + (-ndcA.y * 0.5f + 0.5f) * renderHeight;
                const float bx = renderRect.left + (ndcB.x * 0.5f + 0.5f) * renderWidth;
                const float by = renderRect.top + (-ndcB.y * 0.5f + 0.5f) * renderHeight;
                const float abx = bx - ax, aby = by - ay;
                const float denom = std::max(abx * abx + aby * aby, 1.0e-5f);
                const float t = std::clamp(((clientPoint.x - ax) * abx + (clientPoint.y - ay) * aby) / denom, 0.0f, 1.0f);
                const float dx = static_cast<float>(clientPoint.x) - (ax + abx * t);
                const float dy = static_cast<float>(clientPoint.y) - (ay + aby * t);
                const float distSq = dx * dx + dy * dy;
                if (distSq < bestDistSq) { bestDistSq = distSq; bestGuide = static_cast<int32_t>(guideIndex); }
            }
        }
        m_selectedStrandIndex = bestGuide;
        m_lastSelectedStrandScreenDistance = bestGuide >= 0 ? std::sqrt(bestDistSq) : 0.0f;
        return bestGuide >= 0;
    }



bool GpuHairViewer::ApplySelectedStrandOffset(const Vec3& rootOffset, const Vec3& tipOffset)
{
    if (m_selectedStrandIndex < 0 || static_cast<size_t>(m_selectedStrandIndex) >= m_asset.strands.size()) return false;
    const StrandRange& strand = m_asset.strands[static_cast<size_t>(m_selectedStrandIndex)];
    if (strand.pointCount == 0u) return false;
    PushStrandEditUndoSnapshot();
    std::vector<uint32_t> changed;
    changed.reserve(strand.pointCount);
    for (uint32_t i = 0u; i < strand.pointCount; ++i)
    {
        const uint32_t pointIndex = strand.pointOffset + i;
        const float t = strand.pointCount > 1u ? static_cast<float>(i) / static_cast<float>(strand.pointCount - 1u) : 0.0f;
        m_asset.restPoints[pointIndex] += Lerp(rootOffset, tipOffset, t);
        changed.push_back(pointIndex);
    }
    hairgpu::EditStack stack = {};
    std::string parseError;
    const std::string inlineUtf8 = WideToUtf8(GetInlineEditStackText());
    if (!inlineUtf8.empty()) LocalLoadEditStackCsvText(inlineUtf8, stack, parseError);
    hairgpu::EditLayer layer = {};
    layer.name = "single_strand_fix";
    layer.enabled = true;
    layer.layerWeight = 1.0f;
    layer.region.mode = hairgpu::RegionSelectMode::All;
    layer.edit.rootOffset = rootOffset;
    layer.edit.tipOffset = tipOffset;
    layer.strandSelection = { static_cast<uint32_t>(m_selectedStrandIndex) };
    layer.strandSelectionMode = hairgpu::StrandSelectionMode::Replace;
    stack.layers.push_back(layer);
    SetWindowTextW(m_editInlineEditStack, Utf8ToWide(SerializeEditStackToCsvText(stack)).c_str());
    m_selectedInlineLayerIndex = static_cast<int>(stack.layers.size()) - 1;
    LoadQuickEditFormFromInlineText(m_selectedInlineLayerIndex);
    CommitEditedPointIndices(changed);
    FinalizeLastStrandEditUndoSnapshot();
    return true;
}


        void GpuHairViewer::CommitEditedRestState()
    {
        std::vector<uint32_t> changed;
        changed.reserve(4096u);
        const size_t count = std::min(m_asset.restPoints.size(), m_initialParticles.size());
        for (size_t index = 0u; index < count; ++index)
        {
            const Vec3& p = m_asset.restPoints[index];
            const GpuParticle& initial = m_initialParticles[index];
            if (std::fabs(p.x - initial.x) > 1.0e-7f ||
                std::fabs(p.y - initial.y) > 1.0e-7f ||
                std::fabs(p.z - initial.z) > 1.0e-7f)
            {
                changed.push_back(static_cast<uint32_t>(index));
            }
        }
        CommitEditedPointIndices(changed);
    }



    std::vector<uint32_t> GpuHairViewer::ExpandGuideFamilies(const std::vector<uint32_t>& guideIndices) const
    {
        std::unordered_set<uint32_t> guideSet;
        for (uint32_t guide : guideIndices)
        {
            if (guide < m_asset.guideStrandCount) guideSet.insert(guide);
            else if (guide < m_asset.followData.size() && m_asset.followData[guide].parentGuide >= 0)
                guideSet.insert(static_cast<uint32_t>(m_asset.followData[guide].parentGuide));
        }
        std::vector<uint32_t> result;
        result.reserve(guideSet.size() * (kFixedFollowersPerGuide + 1u));
        for (uint32_t guide : guideSet) result.push_back(guide);
        for (uint32_t strand = m_asset.guideStrandCount; strand < m_asset.strands.size() && strand < m_asset.followData.size(); ++strand)
        {
            const int32_t parent = m_asset.followData[strand].parentGuide;
            if (parent >= 0 && guideSet.count(static_cast<uint32_t>(parent)) != 0u) result.push_back(strand);
        }
        std::sort(result.begin(), result.end());
        result.erase(std::unique(result.begin(), result.end()), result.end());
        return result;
    }

    void GpuHairViewer::PushStrandEditUndoSnapshot(const std::vector<uint32_t>& guideIndices)
    {
        StrandEditSnapshot snapshot = {};
        const std::vector<uint32_t> strands = ExpandGuideFamilies(guideIndices);
        for (uint32_t strandIndex : strands)
        {
            if (strandIndex >= m_asset.strands.size()) continue;
            const StrandRange& strand = m_asset.strands[strandIndex];
            for (uint32_t local = 0u; local < strand.pointCount; ++local)
            {
                const uint32_t pointIndex = strand.pointOffset + local;
                if (pointIndex < m_asset.restPoints.size())
                {
                    const Vec3 value = m_asset.restPoints[pointIndex];
                    snapshot.points.push_back({ pointIndex, value, value });
                }
            }
        }
        snapshot.inlineEditStackTextBefore = GetInlineEditStackText();
        snapshot.inlineEditStackTextAfter = snapshot.inlineEditStackTextBefore;
        snapshot.lockedStrandsBefore = m_lockedStrands;
        snapshot.lockedStrandsAfter = snapshot.lockedStrandsBefore;
        snapshot.selectedStrandIndexBefore = m_selectedStrandIndex;
        snapshot.selectedStrandIndexAfter = m_selectedStrandIndex;
        snapshot.selectedInlineLayerIndexBefore = m_selectedInlineLayerIndex;
        snapshot.selectedInlineLayerIndexAfter = m_selectedInlineLayerIndex;
        m_strandUndoStack.push_back(std::move(snapshot));
        if (m_strandUndoStack.size() > kMaximumUndoCount) m_strandUndoStack.erase(m_strandUndoStack.begin());
        m_strandRedoStack.clear();
    }

    void GpuHairViewer::PushStrandEditUndoSnapshot()
    {
        if (m_selectedStrandIndex < 0) return;
        PushStrandEditUndoSnapshot({ static_cast<uint32_t>(m_selectedStrandIndex) });
    }

    void GpuHairViewer::ExtendLastStrandEditUndoSnapshot(const std::vector<uint32_t>& guideIndices)
    {
        if (m_strandUndoStack.empty()) return;
        StrandEditSnapshot& snapshot = m_strandUndoStack.back();
        std::unordered_set<uint32_t> existing;
        existing.reserve(snapshot.points.size());
        for (const SparsePointEdit& point : snapshot.points) existing.insert(point.pointIndex);
        for (uint32_t strandIndex : ExpandGuideFamilies(guideIndices))
        {
            if (strandIndex >= m_asset.strands.size()) continue;
            const StrandRange& strand = m_asset.strands[strandIndex];
            for (uint32_t local = 0u; local < strand.pointCount; ++local)
            {
                const uint32_t pointIndex = strand.pointOffset + local;
                if (pointIndex < m_asset.restPoints.size() && existing.insert(pointIndex).second)
                {
                    const Vec3 value = m_asset.restPoints[pointIndex];
                    snapshot.points.push_back({ pointIndex, value, value });
                }
            }
        }
    }

    void GpuHairViewer::FinalizeLastStrandEditUndoSnapshot()
    {
        if (m_strandUndoStack.empty()) return;
        StrandEditSnapshot& snapshot = m_strandUndoStack.back();
        for (SparsePointEdit& point : snapshot.points)
            if (point.pointIndex < m_asset.restPoints.size()) point.after = m_asset.restPoints[point.pointIndex];
        snapshot.points.erase(std::remove_if(snapshot.points.begin(), snapshot.points.end(), [](const SparsePointEdit& point)
        {
            return Length(point.after - point.before) <= 1.0e-8f;
        }), snapshot.points.end());
        snapshot.inlineEditStackTextAfter = GetInlineEditStackText();
        snapshot.lockedStrandsAfter = m_lockedStrands;
        snapshot.selectedStrandIndexAfter = m_selectedStrandIndex;
        snapshot.selectedInlineLayerIndexAfter = m_selectedInlineLayerIndex;
        if (snapshot.points.empty() && snapshot.lockedStrandsBefore == snapshot.lockedStrandsAfter &&
            snapshot.inlineEditStackTextBefore == snapshot.inlineEditStackTextAfter)
            m_strandUndoStack.pop_back();
    }

    void GpuHairViewer::ApplyStrandEditSnapshot(const StrandEditSnapshot& snapshot, bool applyAfter)
    {
        std::vector<uint32_t> changed;
        changed.reserve(snapshot.points.size());
        for (const SparsePointEdit& point : snapshot.points)
        {
            if (point.pointIndex >= m_asset.restPoints.size()) continue;
            m_asset.restPoints[point.pointIndex] = applyAfter ? point.after : point.before;
            changed.push_back(point.pointIndex);
        }
        m_lockedStrands = applyAfter ? snapshot.lockedStrandsAfter : snapshot.lockedStrandsBefore;
        m_selectedStrandIndex = applyAfter ? snapshot.selectedStrandIndexAfter : snapshot.selectedStrandIndexBefore;
        m_selectedInlineLayerIndex = applyAfter ? snapshot.selectedInlineLayerIndexAfter : snapshot.selectedInlineLayerIndexBefore;
        SetWindowTextW(m_editInlineEditStack, (applyAfter ? snapshot.inlineEditStackTextAfter : snapshot.inlineEditStackTextBefore).c_str());
        LoadQuickEditFormFromInlineText(m_selectedInlineLayerIndex);
        CommitEditedPointIndices(changed);
    }

    void GpuHairViewer::CommitEditedPointIndices(const std::vector<uint32_t>& pointIndices)
    {
        if (!m_context || pointIndices.empty()) return;
        std::vector<uint32_t> sorted = pointIndices;
        std::sort(sorted.begin(), sorted.end());
        sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
        sorted.erase(std::remove_if(sorted.begin(), sorted.end(), [&](uint32_t index)
        {
            return index >= m_asset.restPoints.size() || index >= m_initialParticles.size();
        }), sorted.end());
        if (sorted.empty()) return;

        for (uint32_t index : sorted)
        {
            const Vec3& p = m_asset.restPoints[index];
            m_initialParticles[index] = { p.x, p.y, p.z, 1.0f };
        }

        size_t cursor = 0u;
        while (cursor < sorted.size())
        {
            const uint32_t first = sorted[cursor];
            uint32_t last = first;
            while (cursor + 1u < sorted.size() && sorted[cursor + 1u] == last + 1u)
            {
                ++cursor;
                last = sorted[cursor];
            }
            D3D11_BOX box = {};
            box.left = first * static_cast<UINT>(sizeof(GpuParticle));
            box.right = (last + 1u) * static_cast<UINT>(sizeof(GpuParticle));
            box.top = 0u; box.bottom = 1u; box.front = 0u; box.back = 1u;
            const GpuParticle* source = m_initialParticles.data() + first;
            if (m_restPositions.buffer) m_context->UpdateSubresource(m_restPositions.buffer.Get(), 0u, &box, source, 0u, 0u);
            for (uint32_t buffer = 0u; buffer < 2u; ++buffer)
            {
                if (m_positions[buffer].buffer) m_context->UpdateSubresource(m_positions[buffer].buffer.Get(), 0u, &box, source, 0u, 0u);
                if (m_previousPositions[buffer].buffer) m_context->UpdateSubresource(m_previousPositions[buffer].buffer.Get(), 0u, &box, source, 0u, 0u);
            }
            ++cursor;
        }
        if (m_distanceLambdaBuffer.buffer && !m_zeroDistanceLambdas.empty())
            m_context->UpdateSubresource(m_distanceLambdaBuffer.buffer.Get(), 0u, nullptr, m_zeroDistanceLambdas.data(), 0u, 0u);
        if (m_bendLambdaBuffer.buffer && !m_zeroBendLambdas.empty())
            m_context->UpdateSubresource(m_bendLambdaBuffer.buffer.Get(), 0u, nullptr, m_zeroBendLambdas.data(), 0u, 0u);
        m_gpuDebugLinesDirty = true;
        UpdateWindowTitle();
    }

    bool GpuHairViewer::ReadBackCurrentParticlePositions(std::vector<GpuParticle>& outParticles)
    {
        if (!m_positions[m_currentBufferIndex].buffer)
        {
            return false;
        }

        D3D11_BUFFER_DESC desc = {};
        m_positions[m_currentBufferIndex].buffer->GetDesc(&desc);
        D3D11_BUFFER_DESC stagingDesc = desc;
        stagingDesc.BindFlags = 0;
        stagingDesc.Usage = D3D11_USAGE_STAGING;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        stagingDesc.MiscFlags = 0;
        stagingDesc.StructureByteStride = 0;

        ComPtr<ID3D11Buffer> staging;
        if (FAILED(m_device->CreateBuffer(&stagingDesc, nullptr, &staging)))
        {
            return false;
        }

        m_context->CopyResource(staging.Get(), m_positions[m_currentBufferIndex].buffer.Get());

        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (FAILED(m_context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
        {
            return false;
        }

        outParticles.resize(m_asset.restPoints.size());
        std::memcpy(outParticles.data(), mapped.pData, outParticles.size() * sizeof(GpuParticle));
        m_context->Unmap(staging.Get(), 0);
        return true;
    }

    
        bool GpuHairViewer::UndoStrandEdit()
    {
        if (m_strandUndoStack.empty()) return false;
        StrandEditSnapshot snapshot = std::move(m_strandUndoStack.back());
        m_strandUndoStack.pop_back();
        ApplyStrandEditSnapshot(snapshot, false);
        m_strandRedoStack.push_back(std::move(snapshot));
        RecordViewportHistory(L"Undo");
        return true;
    }


        bool GpuHairViewer::RedoStrandEdit()
    {
        if (m_strandRedoStack.empty()) return false;
        StrandEditSnapshot snapshot = std::move(m_strandRedoStack.back());
        m_strandRedoStack.pop_back();
        ApplyStrandEditSnapshot(snapshot, true);
        m_strandUndoStack.push_back(std::move(snapshot));
        if (m_strandUndoStack.size() > kMaximumUndoCount) m_strandUndoStack.erase(m_strandUndoStack.begin());
        RecordViewportHistory(L"Redo");
        return true;
    }


