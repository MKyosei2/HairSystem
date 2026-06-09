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
        PushStrandEditUndoSnapshot();
        m_selectedStrandIndex = guideIndex;
        m_lastSelectedStrandScreenDistance = screenDistance;
        m_brushStrokeActive = true;
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

        hairgpu::EditLayer layer = BuildViewportBrushLayer(centerUv, m_brushStrokeStartUv, m_brushStrokeCurrentUv, clientPoint, &m_brushStrokeAffectedGuideCount);
        if (m_groomTool == GroomTool::Freeze)
        {
            for (uint32_t guideIndex : layer.strandSelection)
            {
                if (guideIndex >= m_lockedStrands.size())
                {
                    m_lockedStrands.resize(m_asset.strands.size(), 0u);
                }
                m_lockedStrands[guideIndex] = 1u;
                for (uint32_t strandIndex = m_asset.guideStrandCount; strandIndex < m_asset.strands.size() && strandIndex < m_asset.followData.size(); ++strandIndex)
                {
                    if (m_asset.followData[strandIndex].parentGuide == static_cast<int32_t>(guideIndex))
                    {
                        if (strandIndex >= m_lockedStrands.size())
                        {
                            m_lockedStrands.resize(m_asset.strands.size(), 0u);
                        }
                        m_lockedStrands[strandIndex] = 1u;
                    }
                }
            }
            UpdateWindowTitle();
            RefreshQuickEditFormLabels();
            if (commitStroke)
            {
                RecordViewportHistory(L"Freeze brush locked " + std::to_wstring(layer.strandSelection.size()) + L" guides");
            }
            return true;
        }

        if (layer.strandSelection.empty())
        {
            return false;
        }
        previewStack.layers.push_back(layer);

        hairgpu::HairAsset previewAsset = m_baseAsset.strands.empty() ? m_asset : m_baseAsset;
        std::string outError;
        hairgpu::RuntimeEditState runtimeState = {};
        if (!hairgpu::ApplyEditStackToAsset(previewAsset, m_baseAsset.strands.empty() ? m_asset : m_baseAsset, previewStack, &runtimeState, outError))
        {
            return false;
        }
        m_runtimeEditState = runtimeState;
        m_asset = std::move(previewAsset);
        if (m_initialParticles.size() != m_asset.restPoints.size())
        {
            CreateSimulationResources();
            RebuildActiveSetFoundation();
        }
        else
        {
            for (size_t i = 0; i < m_asset.restPoints.size() && i < m_initialParticles.size(); ++i)
            {
                m_initialParticles[i].x = m_asset.restPoints[i].x;
                m_initialParticles[i].y = m_asset.restPoints[i].y;
                m_initialParticles[i].z = m_asset.restPoints[i].z;
            }
            CommitEditedRestState();
        }
        QueueVisibilityRecovery();

        std::wstring inlineText = Utf8ToWide(SerializeEditStackToCsvText(previewStack));
        SetWindowTextW(m_editInlineEditStack, inlineText.c_str());
        m_selectedInlineLayerIndex = static_cast<int>(previewStack.layers.size()) - 1;
        LoadQuickEditFormFromInlineText(m_selectedInlineLayerIndex);

        if (commitStroke)
        {
            std::wstring entry = std::wstring(GetGroomToolDisplayName()) + L" stroke | guides=" + std::to_wstring(m_brushStrokeAffectedGuideCount) +
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
            if (!m_strandUndoStack.empty())
            {
                StrandEditSnapshot snapshot = m_strandUndoStack.back();
                m_strandUndoStack.pop_back();
                m_asset.restPoints = std::move(snapshot.restPoints);
                m_initialParticles = std::move(snapshot.initialParticles);
                m_selectedStrandIndex = snapshot.selectedStrandIndex;
                m_lockedStrands = std::move(snapshot.lockedStrands);
                SetWindowTextW(m_editInlineEditStack, snapshot.inlineEditStackText.c_str());
                m_selectedInlineLayerIndex = snapshot.selectedInlineLayerIndex;
                LoadQuickEditFormFromInlineText(m_selectedInlineLayerIndex);
                CommitEditedRestState();
            }
        }
        else
        {
            ApplyViewportBrushStroke(m_brushStrokeLastPoint, true);
        }
        m_brushStrokeActive = false;
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
        const std::array<std::pair<GizmoAxis, Vec3>, 3> axes = { std::make_pair(GizmoAxis::X, Vec3(gizmoScale, 0.0f, 0.0f)), std::make_pair(GizmoAxis::Y, Vec3(0.0f, gizmoScale, 0.0f)), std::make_pair(GizmoAxis::Z, Vec3(0.0f, 0.0f, gizmoScale)) };
        for (const auto& candidate : axes)
        {
            float bx = 0.0f, by = 0.0f;
            if (!ProjectWorldToScreen(anchor + candidate.second, bx, by))
            {
                continue;
            }
            const float abx = bx - ax;
            const float aby = by - ay;
            const float denom = std::max(abx * abx + aby * aby, 1.0e-5f);
            const float t = std::clamp(((clientPoint.x - ax) * abx + (clientPoint.y - ay) * aby) / denom, 0.0f, 1.0f);
            const float px = ax + abx * t;
            const float py = ay + aby * t;
            const float dx = static_cast<float>(clientPoint.x) - px;
            const float dy = static_cast<float>(clientPoint.y) - py;
            const float distSq = dx * dx + dy * dy;
            if (distSq < best.distSq)
            {
                best = { candidate.first, distSq };
            }
        }
        if (best.axis == GizmoAxis::None)
        {
            return false;
        }
        PushStrandEditUndoSnapshot();
        m_gizmoDragBaseRestPoints = m_asset.restPoints;
        m_activeGizmoAxis = best.axis;
        m_gizmoDragStartPoint = clientPoint;
        m_gizmoDragging = true;
        return true;
    }

    void GpuHairViewer::UpdateStrandGizmoDrag(POINT clientPoint)
    {
        if (!m_gizmoDragging || m_activeGizmoAxis == GizmoAxis::None || m_selectedStrandIndex < 0 || static_cast<size_t>(m_selectedStrandIndex) >= m_asset.strands.size())
        {
            return;
        }
        const StrandRange& strand = m_asset.strands[static_cast<size_t>(m_selectedStrandIndex)];
        if (m_gizmoDragBaseRestPoints.size() != m_asset.restPoints.size() || strand.pointCount == 0u)
        {
            return;
        }
        m_asset.restPoints = m_gizmoDragBaseRestPoints;
        const float dx = static_cast<float>(clientPoint.x - m_gizmoDragStartPoint.x);
        const float dy = static_cast<float>(clientPoint.y - m_gizmoDragStartPoint.y);
        const float signedPixels = (m_activeGizmoAxis == GizmoAxis::Y) ? (-dy) : dx;
        const float scale = std::max(0.0008f, m_cameraDistance * 0.0009f);
        Vec3 delta(0.0f, 0.0f, 0.0f);
        switch (m_activeGizmoAxis)
        {
        case GizmoAxis::X: delta.x = signedPixels * scale; break;
        case GizmoAxis::Y: delta.y = signedPixels * scale; break;
        case GizmoAxis::Z: delta.z = signedPixels * scale; break;
        default: break;
        }
        Vec3 rootDelta(0.0f, 0.0f, 0.0f);
        Vec3 tipDelta(0.0f, 0.0f, 0.0f);
        if (m_strandGizmoMode == StrandGizmoMode::WholeTranslate)
        {
            rootDelta = delta;
            tipDelta = delta;
        }
        else if (m_strandGizmoMode == StrandGizmoMode::RootTranslate)
        {
            rootDelta = delta;
        }
        else
        {
            tipDelta = delta;
        }
        for (uint32_t i = 0; i < strand.pointCount; ++i)
        {
            const uint32_t pointIndex = strand.pointOffset + i;
            const float t = (strand.pointCount > 1u) ? (static_cast<float>(i) / static_cast<float>(strand.pointCount - 1u)) : 0.0f;
            m_asset.restPoints[pointIndex] += Lerp(rootDelta, tipDelta, t);
            if (pointIndex < m_initialParticles.size())
            {
                m_initialParticles[pointIndex].x = m_asset.restPoints[pointIndex].x;
                m_initialParticles[pointIndex].y = m_asset.restPoints[pointIndex].y;
                m_initialParticles[pointIndex].z = m_asset.restPoints[pointIndex].z;
            }
        }
        CommitEditedRestState();
    }

    void GpuHairViewer::EndStrandGizmoDrag(bool cancelDrag)
    {
        if (!m_gizmoDragging)
        {
            return;
        }
        if (cancelDrag && m_gizmoDragBaseRestPoints.size() == m_asset.restPoints.size())
        {
            m_asset.restPoints = m_gizmoDragBaseRestPoints;
            for (size_t i = 0; i < m_asset.restPoints.size() && i < m_initialParticles.size(); ++i)
            {
                m_initialParticles[i].x = m_asset.restPoints[i].x;
                m_initialParticles[i].y = m_asset.restPoints[i].y;
                m_initialParticles[i].z = m_asset.restPoints[i].z;
            }
            CommitEditedRestState();
        }
        m_gizmoDragging = false;
        m_activeGizmoAxis = GizmoAxis::None;
        m_gizmoDragBaseRestPoints.clear();
    }

    bool GpuHairViewer::PickStrandAtPoint(POINT clientPoint)
    {
        std::vector<GpuParticle> particles;
        if (!ReadBackCurrentParticlePositions(particles))
        {
            particles = m_initialParticles;
        }
        if (particles.size() < m_asset.restPoints.size() || m_asset.strands.empty())
        {
            m_selectedStrandIndex = -1;
            return false;
        }

        const RECT renderRect = GetRenderViewportRect();
        const float renderWidth = static_cast<float>(std::max<LONG>(renderRect.right - renderRect.left, 1));
        const float renderHeight = static_cast<float>(std::max<LONG>(renderRect.bottom - renderRect.top, 1));
        const float aspectRatio = std::max(renderWidth / std::max(renderHeight, 1.0f), 0.6f);

        float bestDistSq = 18.0f * 18.0f;
        int32_t bestStrand = -1;
        for (uint32_t strandIndex = 0; strandIndex < static_cast<uint32_t>(m_asset.strands.size()); ++strandIndex)
        {
            const StrandRange& strand = m_asset.strands[strandIndex];
            for (uint32_t i = 0; i + 1u < strand.pointCount; ++i)
            {
                const GpuParticle& a = particles[strand.pointOffset + i];
                const GpuParticle& b = particles[strand.pointOffset + i + 1u];
                Vec3 ndcA, ndcB;
                if (!ProjectWorldToNdc(Vec3(a.x, a.y, a.z), m_cameraPosition, m_cameraTarget, aspectRatio, ndcA) ||
                    !ProjectWorldToNdc(Vec3(b.x, b.y, b.z), m_cameraPosition, m_cameraTarget, aspectRatio, ndcB))
                {
                    continue;
                }
                const float ax = renderRect.left + (ndcA.x * 0.5f + 0.5f) * renderWidth;
                const float ay = renderRect.top + (-ndcA.y * 0.5f + 0.5f) * renderHeight;
                const float bx = renderRect.left + (ndcB.x * 0.5f + 0.5f) * renderWidth;
                const float by = renderRect.top + (-ndcB.y * 0.5f + 0.5f) * renderHeight;
                const float abx = bx - ax;
                const float aby = by - ay;
                const float denom = std::max(abx * abx + aby * aby, 1.0e-5f);
                const float t = std::clamp(((clientPoint.x - ax) * abx + (clientPoint.y - ay) * aby) / denom, 0.0f, 1.0f);
                const float px = ax + abx * t;
                const float py = ay + aby * t;
                const float dx = static_cast<float>(clientPoint.x) - px;
                const float dy = static_cast<float>(clientPoint.y) - py;
                const float distSq = dx * dx + dy * dy;
                if (distSq < bestDistSq)
                {
                    bestDistSq = distSq;
                    bestStrand = static_cast<int32_t>(strandIndex);
                }
            }
        }

        m_selectedStrandIndex = bestStrand;
        m_lastSelectedStrandScreenDistance = (bestStrand >= 0) ? std::sqrt(bestDistSq) : 0.0f;
        return bestStrand >= 0;
    }


bool GpuHairViewer::ApplySelectedStrandOffset(const Vec3& rootOffset, const Vec3& tipOffset)
{
    if (m_selectedStrandIndex < 0 || static_cast<size_t>(m_selectedStrandIndex) >= m_asset.strands.size())
    {
        return false;
    }
    const StrandRange& strand = m_asset.strands[static_cast<size_t>(m_selectedStrandIndex)];
    if (strand.pointCount == 0u)
    {
        return false;
    }
    PushStrandEditUndoSnapshot();
    for (uint32_t i = 0; i < strand.pointCount; ++i)
    {
        const uint32_t pointIndex = strand.pointOffset + i;
        const float tt = (strand.pointCount > 1u) ? (static_cast<float>(i) / static_cast<float>(strand.pointCount - 1u)) : 0.0f;
        const Vec3 delta = Lerp(rootOffset, tipOffset, tt);
        m_asset.restPoints[pointIndex] += delta;
        if (pointIndex < m_initialParticles.size())
        {
            m_initialParticles[pointIndex].x = m_asset.restPoints[pointIndex].x;
            m_initialParticles[pointIndex].y = m_asset.restPoints[pointIndex].y;
            m_initialParticles[pointIndex].z = m_asset.restPoints[pointIndex].z;
        }
    }

    hairgpu::EditStack stack = {};
    std::string parseError;
    const std::string inlineUtf8 = WideToUtf8(GetInlineEditStackText());
    if (!inlineUtf8.empty())
    {
        LocalLoadEditStackCsvText(inlineUtf8, stack, parseError);
    }
    stack.boundarySmoothing = hairgpu::Clamp(m_quickEditBoundarySmoothing, 0.0f, 1.0f);
    stack.boundaryRadiusUv = hairgpu::Clamp(m_quickEditBoundaryRadiusUv, 0.01f, 0.30f);

    hairgpu::EditLayer layer = {};
    layer.name = "single_strand_fix";
    layer.enabled = true;
    layer.layerWeight = 1.0f;
    layer.region.mode = hairgpu::RegionSelectMode::All;
    layer.edit.rootOffset = rootOffset;
    layer.edit.tipOffset = tipOffset;
    layer.edit.lengthScale = 1.0f;
    layer.edit.widthScale = 1.0f;
    layer.edit.followerInfluence = 1.0f;
    layer.strandSelection = { static_cast<uint32_t>(m_selectedStrandIndex) };
    layer.strandSelectionMode = hairgpu::StrandSelectionMode::Replace;
    layer.strandSelectionWeight = 1.0f;
    stack.layers.push_back(layer);

    const std::wstring inlineText = Utf8ToWide(SerializeEditStackToCsvText(stack));
    SetWindowTextW(m_editInlineEditStack, inlineText.c_str());
    m_selectedInlineLayerIndex = static_cast<int>(stack.layers.size()) - 1;
    LoadQuickEditFormFromInlineText(m_selectedInlineLayerIndex);

    CommitEditedRestState();
    return true;
}

    void GpuHairViewer::CommitEditedRestState()
    {
        if (m_restPositions.buffer && !m_initialParticles.empty())
        {
            m_context->UpdateSubresource(m_restPositions.buffer.Get(), 0, nullptr, m_initialParticles.data(), 0, 0);
        }
        ResetSimulation();
        QueueVisibilityRecovery();
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

    void GpuHairViewer::PushStrandEditUndoSnapshot()
    {
        StrandEditSnapshot snapshot = {};
        snapshot.restPoints = m_asset.restPoints;
        snapshot.initialParticles = m_initialParticles;
        snapshot.inlineEditStackText = GetInlineEditStackText();
        snapshot.lockedStrands = m_lockedStrands;
        snapshot.selectedStrandIndex = m_selectedStrandIndex;
        snapshot.selectedInlineLayerIndex = m_selectedInlineLayerIndex;
        m_strandUndoStack.push_back(std::move(snapshot));
        if (m_strandUndoStack.size() > 64u)
        {
            m_strandUndoStack.erase(m_strandUndoStack.begin());
        }
        m_strandRedoStack.clear();
    }

    bool GpuHairViewer::UndoStrandEdit()
    {
        if (m_strandUndoStack.empty())
        {
            return false;
        }

        StrandEditSnapshot current = {};
        current.restPoints = m_asset.restPoints;
        current.initialParticles = m_initialParticles;
        current.inlineEditStackText = GetInlineEditStackText();
        current.lockedStrands = m_lockedStrands;
        current.selectedStrandIndex = m_selectedStrandIndex;
        current.selectedInlineLayerIndex = m_selectedInlineLayerIndex;
        m_strandRedoStack.push_back(std::move(current));

        StrandEditSnapshot snapshot = std::move(m_strandUndoStack.back());
        m_strandUndoStack.pop_back();
        m_asset.restPoints = std::move(snapshot.restPoints);
        m_initialParticles = std::move(snapshot.initialParticles);
        m_lockedStrands = std::move(snapshot.lockedStrands);
        m_selectedStrandIndex = snapshot.selectedStrandIndex;
        m_selectedInlineLayerIndex = snapshot.selectedInlineLayerIndex;
        SetWindowTextW(m_editInlineEditStack, snapshot.inlineEditStackText.c_str());
        LoadQuickEditFormFromInlineText(m_selectedInlineLayerIndex);
        CommitEditedRestState();
        RecordViewportHistory(L"Undo");
        return true;
    }

    bool GpuHairViewer::RedoStrandEdit()
    {
        if (m_strandRedoStack.empty())
        {
            return false;
        }

        StrandEditSnapshot current = {};
        current.restPoints = m_asset.restPoints;
        current.initialParticles = m_initialParticles;
        current.inlineEditStackText = GetInlineEditStackText();
        current.lockedStrands = m_lockedStrands;
        current.selectedStrandIndex = m_selectedStrandIndex;
        current.selectedInlineLayerIndex = m_selectedInlineLayerIndex;
        m_strandUndoStack.push_back(std::move(current));

        StrandEditSnapshot snapshot = std::move(m_strandRedoStack.back());
        m_strandRedoStack.pop_back();
        m_asset.restPoints = std::move(snapshot.restPoints);
        m_initialParticles = std::move(snapshot.initialParticles);
        m_lockedStrands = std::move(snapshot.lockedStrands);
        m_selectedStrandIndex = snapshot.selectedStrandIndex;
        m_selectedInlineLayerIndex = snapshot.selectedInlineLayerIndex;
        SetWindowTextW(m_editInlineEditStack, snapshot.inlineEditStackText.c_str());
        LoadQuickEditFormFromInlineText(m_selectedInlineLayerIndex);
        CommitEditedRestState();
        RecordViewportHistory(L"Redo");
        return true;
    }

