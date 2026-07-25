// GpuHairViewer.cpp ordered part 13: edit_stack_inline_layers_and_masks
// This file is included by ../GpuHairViewer.cpp; do not compile it directly.

    bool GpuHairViewer::SaveStrandEditSessionToPath(const std::filesystem::path& path) const
    {
        std::ofstream out(path);
        if (!out)
        {
            return false;
        }

        // Version 3 makes the Edit Stack the only persistent edit source.
        // Derived point coordinates are intentionally not serialized.
        out << "version=3\n";
        out << "selected=" << m_selectedStrandIndex << "\n";
        out << "selectedLayer=" << m_selectedInlineLayerIndex << "\n";

        uint32_t lockedCount = 0u;
        for (uint8_t value : m_lockedStrands)
        {
            lockedCount += (value != 0u) ? 1u : 0u;
        }
        out << "lockedCount=" << lockedCount << "\n";
        out << "locked=";
        bool first = true;
        for (size_t index = 0u; index < m_lockedStrands.size(); ++index)
        {
            if (m_lockedStrands[index] == 0u)
            {
                continue;
            }
            if (!first)
            {
                out << ',';
            }
            out << index;
            first = false;
        }
        out << "\n";

        const std::wstring inlineStack = GetInlineEditStackText();
        out << "inlineStackBegin\n";
        out << WideToUtf8(inlineStack);
        if (inlineStack.empty() ||
            (inlineStack.back() != L'\n' && inlineStack.back() != L'\r'))
        {
            out << "\n";
        }
        out << "inlineStackEnd\n";
        return static_cast<bool>(out);
    }

    bool GpuHairViewer::LoadStrandEditSessionFromPath(const std::filesystem::path& path)
    {
        std::ifstream in(path);
        if (!in)
        {
            return false;
        }

        std::string line;
        uint32_t sessionVersion = 0u;
        int32_t selected = -1;
        int32_t selectedLayer = -1;
        std::vector<uint8_t> lockedStrands(m_asset.strands.size(), 0u);
        std::string inlineStackUtf8;
        bool readingInlineStack = false;
        bool sawLegacyPointPayload = false;

        while (std::getline(in, line))
        {
            if (readingInlineStack)
            {
                if (line == "inlineStackEnd")
                {
                    readingInlineStack = false;
                }
                else
                {
                    inlineStackUtf8 += line;
                    inlineStackUtf8.push_back('\n');
                }
                continue;
            }
            if (line == "inlineStackBegin")
            {
                readingInlineStack = true;
                continue;
            }
            if (line.rfind("version=", 0) == 0)
            {
                sessionVersion = static_cast<uint32_t>(std::stoul(line.substr(8)));
                continue;
            }
            if (line.rfind("selected=", 0) == 0)
            {
                selected = static_cast<int32_t>(std::stoi(line.substr(9)));
                continue;
            }
            if (line.rfind("selectedLayer=", 0) == 0)
            {
                selectedLayer = static_cast<int32_t>(std::stoi(line.substr(14)));
                continue;
            }
            if (line.rfind("locked=", 0) == 0)
            {
                std::stringstream stream(line.substr(7));
                std::string token;
                while (std::getline(stream, token, ','))
                {
                    if (token.empty())
                    {
                        continue;
                    }
                    const size_t index = static_cast<size_t>(std::stoull(token));
                    if (index < lockedStrands.size())
                    {
                        lockedStrands[index] = 1u;
                    }
                }
                continue;
            }
            if (line.rfind("pointCount=", 0) == 0)
            {
                // Version 2 stored a second authoritative coordinate payload.
                // It is ignored during migration; the inline Edit Stack wins.
                sawLegacyPointPayload = true;
                continue;
            }
            if (sawLegacyPointPayload)
            {
                // Ignore legacy x,y,z rows instead of applying them over the
                // result reconstructed from the Edit Stack.
                continue;
            }
        }

        if (readingInlineStack || sessionVersion == 0u)
        {
            return false;
        }

        hairgpu::EditStack stack = {};
        std::string parseError;
        if (!inlineStackUtf8.empty() &&
            !LocalLoadEditStackCsvText(inlineStackUtf8, stack, parseError))
        {
            return false;
        }

        // A legacy point-only session cannot be migrated without restoring the
        // duplicate coordinate authority that this fix removes.
        if (sessionVersion < 3u && stack.layers.empty())
        {
            return false;
        }

        const hairgpu::HairAsset& canonicalBase =
            m_baseAsset.strands.empty() ? m_asset : m_baseAsset;
        hairgpu::HairAsset rebuiltAsset = canonicalBase;
        hairgpu::RuntimeEditState runtimeState = {};
        std::string applyError;
        if (!hairgpu::ApplyEditStackToAsset(
                rebuiltAsset,
                canonicalBase,
                stack,
                &runtimeState,
                applyError))
        {
            return false;
        }

        if (selected < 0 ||
            static_cast<size_t>(selected) >= rebuiltAsset.strands.size())
        {
            selected = -1;
        }
        if (selectedLayer < 0 ||
            static_cast<size_t>(selectedLayer) >= stack.layers.size())
        {
            selectedLayer =
                stack.layers.empty()
                    ? -1
                    : static_cast<int32_t>(stack.layers.size() - 1u);
        }

        m_asset = std::move(rebuiltAsset);
        m_runtimeEditState = std::move(runtimeState);
        m_lockedStrands = std::move(lockedStrands);
        m_selectedStrandIndex = selected;
        m_selectedInlineLayerIndex = selectedLayer;
        const std::wstring canonicalStackText =
            Utf8ToWide(SerializeEditStackToCsvText(stack));
        SetWindowTextW(m_editInlineEditStack, canonicalStackText.c_str());
        LoadQuickEditFormFromInlineText(m_selectedInlineLayerIndex);

        // Old snapshots can contain coordinates derived from a different stack.
        // Clearing them prevents an undo from reintroducing dual ownership.
        m_strandUndoStack.clear();
        m_strandRedoStack.clear();
        CommitEditedRestState();
        RebuildActiveSetFoundation();
        RecomputeAssetBounds();
        QueueVisibilityRecovery();
        RefreshQuickEditFormLabels();
        RecordViewportHistory(L"Loaded strand edit session (Edit Stack canonical)");
        return true;
    }

    bool GpuHairViewer::LoadHairAssetFromPath(const std::wstring& filePath)
    {
        const std::filesystem::path path(filePath);
        const std::wstring ext = NormalizePathExtensionLower(path);

        hairgpu::HairAsset importedAsset;
        std::string errorText;
        bool loaded = false;
        if (ext == L".csv")
        {
            loaded = hairgpu::LoadHairAssetFromCurvesCsv(path, importedAsset, errorText);
        }
        else if (ext == L".usd" || ext == L".usda")
        {
            loaded = hairgpu::LoadHairAssetFromUsda(path, importedAsset, errorText);
        }
        else if (ext == L".abc")
        {
            loaded = hairgpu::LoadHairAssetFromAlembic(path, importedAsset, errorText);
        }
        else if (ext == L".hairbin" || ext == L".hair")
        {
            loaded = hairgpu::LoadHairAssetFromHairBinary(path, importedAsset, errorText);
        }
        else
        {
            errorText = "Unsupported hair asset file type. Use .csv / .usda / .usd, or .abc with an Alembic-enabled build.";
        }

        if (!loaded)
        {
            m_lastImportStatus = Utf8ToWide(errorText);
            RefreshAssetStatsText();
            return false;
        }

        m_asset = std::move(importedAsset);
        std::wstring scalpRepairStatus;
        EnsureAssetScalpConformance(m_asset, &scalpRepairStatus);
        // Avoid double wig styling in viewer; sample/import paths already shape the asset.
        m_builderGuideCount = m_asset.guideStrandCount;
        m_builderFollowersPerGuide = (m_asset.guideStrandCount > 0) ? (m_asset.followerStrandCount / m_asset.guideStrandCount) : 0u;
        m_builderParticlesPerStrand = m_asset.particlesPerStrand;
        m_activePresetName = L"Imported Asset";
        m_lastAssetSource = path.filename().wstring();
        m_lastImportStatus = (ext == L".csv") ? L"Curves CSV imported" : ((ext == L".usd" || ext == L".usda") ? L"USD ASCII imported" : ((ext == L".hairbin" || ext == L".hair") ? L"Hair binary imported" : (std::wstring(L"Alembic imported (") + std::wstring(hairgpu::GetAlembicBackendDescription(), hairgpu::GetAlembicBackendDescription() + std::strlen(hairgpu::GetAlembicBackendDescription())) + L")")));
        if (!scalpRepairStatus.empty())
        {
            m_lastImportStatus += L" | ";
            m_lastImportStatus += scalpRepairStatus;
        }
        m_baseAsset = m_asset;
        m_selectedStrandIndex = -1;
        m_strandUndoStack.clear();
        m_strandRedoStack.clear();
        m_lockedStrands.assign(m_asset.strands.size(), 0u);
        m_viewportHistory.clear();
        hairgpu::HairAsset editedAsset = m_baseAsset;
        if (m_editStackPath.empty())
        {
            m_runtimeEditState = {};
        }
        else
        {
            ApplyEditStackToAsset(editedAsset, nullptr);
        }
        m_asset = std::move(editedAsset);

        if (!CreateSimulationResources())
        {
            m_lastImportStatus = L"Import succeeded, but GPU resources failed to rebuild";
            RefreshAssetStatsText();
            return false;
        }

        RebuildActiveSetFoundation();
        RecomputeAssetBounds();
        AutoFrameCameraToAsset(true);
        QueueVisibilityRecovery();
        RefreshAssetStatsText();
        ResetSimulation();
        SyncControlsFromState();
        return true;
    }


    void GpuHairViewer::BuildHairMeshVertices(const std::vector<GpuParticle>& particles, float aspectRatio)
    {
        m_hairMeshVertices.clear();
        if (particles.size() < m_asset.restPoints.size() || m_asset.strands.empty())
        {
            return;
        }
        if (m_debugColorMode == DebugColorMode::Editability && m_editVisualizationCategories.size() != m_asset.strands.size())
        {
            BuildEditVisualizationCategories(m_editVisualizationCategories);
        }

        auto particleAt = [&](uint32_t particleIndex) -> Vec3
        {
            const GpuParticle& p = particles[particleIndex];
            return Vec3(p.x, p.y, p.z);
        };

        auto projectPoint = [&](const Vec3& p, float& x, float& y, float& z, float& w) -> bool
        {
            Vec3 ndc;
            if (!ProjectWorldToNdc(p, m_cameraPosition, m_cameraTarget, aspectRatio, ndc))
            {
                return false;
            }
            x = ndc.x;
            y = ndc.y;
            z = ndc.z;
            w = 1.0f;
            return true;
        };

        auto computeColor = [&](uint32_t strandIndex, float widthN) -> Vec3
        {
            if (m_debugColorMode == DebugColorMode::GuideFollower)
            {
                return (strandIndex < m_asset.guideStrandCount) ? Vec3(0.18f, 0.95f, 0.85f) : Vec3(0.98f, 0.52f, 0.18f);
            }
            if (m_debugColorMode == DebugColorMode::WidthHeatmap)
            {
                const float t = Clamp(widthN, 0.0f, 1.0f);
                return Vec3(Lerp(0.12f, 1.00f, t), Lerp(0.35f, 0.20f, t), Lerp(1.00f, 0.10f, t));
            }
            if (m_debugColorMode == DebugColorMode::Editability)
            {
                const uint32_t category = (strandIndex < m_editVisualizationCategories.size()) ? m_editVisualizationCategories[strandIndex] : 1u;
                if (IsStrandLocked(strandIndex))
                {
                    return Vec3(0.58f, 0.58f, 0.66f);
                }
                switch (category)
                {
                case 0u: return Vec3(0.12f, 0.95f, 0.48f);
                case 2u: return Vec3(1.00f, 0.18f, 0.86f);
                case 3u: return Vec3(1.00f, 0.72f, 0.20f);
                default: return Vec3(0.38f, 0.42f, 0.48f);
                }
            }
            if (static_cast<int32_t>(strandIndex) == m_selectedStrandIndex)
            {
                return Vec3(1.00f, 0.12f, 0.82f);
            }
            const Vec3 rootColor(1.00f, 0.88f, 0.38f);
            const Vec3 tipColor(0.78f, 0.58f, 0.18f);
            return Lerp(tipColor, rootColor, 0.35f + 0.65f * Clamp(widthN, 0.0f, 1.0f));
        };

        const Vec3 lightDir = Normalize(Vec3(-0.45f, 0.90f, 0.20f));
        auto shadeColor = [&](const Vec3& worldPos, const Vec3& normal, const Vec3& baseColor, float alpha) -> HairMeshVertex
        {
            float cx = 0.0f, cy = 0.0f, cz = 0.0f, cw = 1.0f;
            projectPoint(worldPos, cx, cy, cz, cw);
            const Vec3 n = Normalize(normal);
            const Vec3 l = Normalize(lightDir * -1.0f);
            const Vec3 v = Normalize(m_cameraPosition - worldPos);
            const float diffuse = Clamp(Dot(n, l), 0.0f, 1.0f);
            const Vec3 h = Normalize(l + v);
            const float spec = std::pow(Clamp(Dot(n, h), 0.0f, 1.0f), 20.0f) * 0.18f;
            const float rim = std::pow(1.0f - Clamp(Dot(n, v), 0.0f, 1.0f), 2.2f) * 0.08f;
            const float lighting = 0.24f + 0.76f * diffuse;
            const Vec3 shaded = baseColor * lighting + Vec3(spec + rim, spec + rim, spec + rim);
            return { cx, cy, cz, cw, shaded.x, shaded.y, shaded.z, alpha };
        };

        auto appendTri = [&](const Vec3& p0, const Vec3& n0, const Vec3& c0,
                             const Vec3& p1, const Vec3& n1, const Vec3& c1,
                             const Vec3& p2, const Vec3& n2, const Vec3& c2,
                             float alpha)
        {
            float x, y, z, w;
            if (!projectPoint(p0, x, y, z, w) || !projectPoint(p1, x, y, z, w) || !projectPoint(p2, x, y, z, w))
            {
                return;
            }
            m_hairMeshVertices.push_back(shadeColor(p0, n0, c0, alpha));
            m_hairMeshVertices.push_back(shadeColor(p1, n1, c1, alpha));
            m_hairMeshVertices.push_back(shadeColor(p2, n2, c2, alpha));
        };

        auto appendQuad = [&](const Vec3& a, const Vec3& na, const Vec3& ca,
                              const Vec3& b, const Vec3& nb, const Vec3& cb,
                              const Vec3& c, const Vec3& nc, const Vec3& cc,
                              const Vec3& d, const Vec3& nd, const Vec3& cd,
                              float alpha)
        {
            appendTri(a, na, ca, b, nb, cb, c, nc, cc, alpha);
            appendTri(c, nc, cc, b, nb, cb, d, nd, cd, alpha);
        };

        // Project and append scalp mesh first so the head is always visible behind the hair.
        if (m_showScalpMesh && !m_asset.scalpMesh.positions.empty() && !m_asset.scalpMesh.triangles.empty())
        {
            const size_t scalpCount = std::min(m_animatedScalpParticles.size(), m_asset.scalpMesh.normals.size());
            for (const TriangleIndices& tri : m_asset.scalpMesh.triangles)
            {
                if (tri.a >= scalpCount || tri.b >= scalpCount || tri.c >= scalpCount)
                {
                    continue;
                }
                const GpuParticle& pa = m_animatedScalpParticles[tri.a];
                const GpuParticle& pb = m_animatedScalpParticles[tri.b];
                const GpuParticle& pc = m_animatedScalpParticles[tri.c];
                const Vec3 a(pa.x, pa.y, pa.z);
                const Vec3 b(pb.x, pb.y, pb.z);
                const Vec3 c(pc.x, pc.y, pc.z);
                const Vec3 na = m_asset.scalpMesh.normals[tri.a];
                const Vec3 nb = m_asset.scalpMesh.normals[tri.b];
                const Vec3 nc = m_asset.scalpMesh.normals[tri.c];
                const Vec3 scalpColor(0.76f, 0.62f, 0.52f);
                appendTri(a, na, scalpColor, b, nb, scalpColor, c, nc, scalpColor, 1.0f);
            }
        }


        if (m_showTestMesh)
        {
            auto appendTubeBetween = [&](const Vec3& p0, const Vec3& p1, float r0, float r1, const Vec3& color0, const Vec3& color1, int sides)
            {
                const Vec3 tangent = Normalize(p1 - p0);
                if (Length(tangent) <= 1.0e-5f)
                {
                    return;
                }
                Vec3 ref = (std::fabs(tangent.y) < 0.9f) ? Vec3(0.0f, 1.0f, 0.0f) : Vec3(1.0f, 0.0f, 0.0f);
                Vec3 side = Normalize(Cross(ref, tangent));
                if (Length(side) <= 1.0e-5f)
                {
                    side = Normalize(Cross(Vec3(0.0f, 0.0f, 1.0f), tangent));
                }
                Vec3 binormal = Normalize(Cross(tangent, side));
                for (int sideIndex = 0; sideIndex < sides; ++sideIndex)
                {
                    const float a0 = static_cast<float>(sideIndex) * 6.28318530718f / static_cast<float>(sides);
                    const float a1 = static_cast<float>(sideIndex + 1) * 6.28318530718f / static_cast<float>(sides);
                    const Vec3 radial0 = Normalize(side * std::cos(a0) + binormal * std::sin(a0));
                    const Vec3 radial1 = Normalize(side * std::cos(a1) + binormal * std::sin(a1));
                    const Vec3 v00 = p0 + radial0 * r0;
                    const Vec3 v01 = p0 + radial1 * r0;
                    const Vec3 v10 = p1 + radial0 * r1;
                    const Vec3 v11 = p1 + radial1 * r1;
                    appendQuad(v00, radial0, color0,
                               v01, radial1, color0,
                               v10, radial0, color1,
                               v11, radial1, color1,
                               1.0f);
                }
            };

            const std::array<Vec3, 10> testPoints = {
                Vec3(-0.06f, 1.88f, 0.10f),
                Vec3(-0.03f, 1.66f, 0.14f),
                Vec3( 0.00f, 1.42f, 0.18f),
                Vec3( 0.03f, 1.18f, 0.22f),
                Vec3( 0.05f, 0.95f, 0.24f),
                Vec3( 0.06f, 0.68f, 0.26f),
                Vec3( 0.06f, 0.38f, 0.26f),
                Vec3( 0.05f, 0.08f, 0.24f),
                Vec3( 0.03f,-0.20f, 0.21f),
                Vec3( 0.00f,-0.46f, 0.18f)
            };
            const Vec3 testColorRoot(1.00f, 0.20f, 0.20f);
            const Vec3 testColorTip(1.00f, 0.82f, 0.18f);
            for (size_t i = 0; i + 1 < testPoints.size(); ++i)
            {
                const float t0 = static_cast<float>(i) / static_cast<float>(testPoints.size() - 1);
                const float t1 = static_cast<float>(i + 1) / static_cast<float>(testPoints.size() - 1);
                const Vec3 c0 = Lerp(testColorRoot, testColorTip, t0);
                const Vec3 c1 = Lerp(testColorRoot, testColorTip, t1);
                const float r0 = 0.030f - 0.014f * t0;
                const float r1 = 0.030f - 0.014f * t1;
                appendTubeBetween(testPoints[i], testPoints[i + 1], r0, r1, c0, c1, 8);
            }

            // Add a short bright marker cylinder near the face so a guaranteed 3D object is visible
            // even when the simulated strands are off-screen or readback is invalid.
            appendTubeBetween(Vec3(0.18f, 1.35f, 0.10f), Vec3(0.18f, 1.05f, 0.10f), 0.040f, 0.040f,
                              Vec3(0.15f, 0.75f, 1.00f), Vec3(0.15f, 0.95f, 0.65f), 10);
        }

        constexpr int kTubeSides = 6;
        const float widthScale = std::max(m_widthScale, 0.2f);
        for (uint32_t strandIndex = 0; strandIndex < static_cast<uint32_t>(m_asset.strands.size()); ++strandIndex)
        {
            const StrandRange& strand = m_asset.strands[strandIndex];
            if (strand.pointCount < 2u)
            {
                continue;
            }

            const hairgpu::SimTier tier = (strandIndex < m_activeSetPlan.recommendedSimulationTiers.size())
                ? m_activeSetPlan.recommendedSimulationTiers[strandIndex]
                : hairgpu::SimTier::Reduced;
            uint32_t segmentStride = 1u;
            int tubeSides = kTubeSides;
            float alpha = 0.98f;
            switch (tier)
            {
            case hairgpu::SimTier::Full:
                segmentStride = 1u;
                tubeSides = kTubeSides;
                alpha = 0.98f;
                break;
            case hairgpu::SimTier::Reduced:
                segmentStride = 2u;
                tubeSides = 5;
                alpha = 0.90f;
                break;
            case hairgpu::SimTier::Frozen:
                segmentStride = 3u;
                tubeSides = 4;
                alpha = 0.78f;
                break;
            case hairgpu::SimTier::Cached:
                if (strandIndex >= m_asset.guideStrandCount)
                {
                    continue;
                }
                segmentStride = 4u;
                tubeSides = 3;
                alpha = 0.58f;
                break;
            }

            for (uint32_t i = 0; i + 1u < strand.pointCount; i += segmentStride)
            {
                const uint32_t nextPoint = std::min<uint32_t>(i + segmentStride, strand.pointCount - 1u);
                if (nextPoint == i)
                {
                    continue;
                }
                const uint32_t idx0 = strand.pointOffset + i;
                const uint32_t idx1 = strand.pointOffset + nextPoint;
                const Vec3 p0 = particleAt(idx0);
                const Vec3 p1 = particleAt(idx1);
                const Vec3 tangent = Normalize(p1 - p0);
                if (Length(tangent) <= 1.0e-5f)
                {
                    continue;
                }

                Vec3 ref = (std::fabs(tangent.y) < 0.9f) ? Vec3(0.0f, 1.0f, 0.0f) : Vec3(1.0f, 0.0f, 0.0f);
                Vec3 side = Normalize(Cross(ref, tangent));
                if (Length(side) <= 1.0e-5f)
                {
                    side = Normalize(Cross(Vec3(0.0f, 0.0f, 1.0f), tangent));
                }
                Vec3 binormal = Normalize(Cross(tangent, side));

                const float width0 = (idx0 < m_asset.pointWidths.size()) ? m_asset.pointWidths[idx0] : 0.0035f;
                const float width1 = (idx1 < m_asset.pointWidths.size()) ? m_asset.pointWidths[idx1] : 0.0030f;
                const float tierRadiusScale = (tier == hairgpu::SimTier::Full) ? 1.00f : ((tier == hairgpu::SimTier::Reduced) ? 0.92f : ((tier == hairgpu::SimTier::Frozen) ? 0.84f : 0.76f));
                const float r0 = std::max(width0 * widthScale * 0.55f * tierRadiusScale, 0.0030f);
                const float r1 = std::max(width1 * widthScale * 0.55f * tierRadiusScale, 0.0026f);
                const float widthN0 = Clamp((width0 * widthScale - 0.0010f) / 0.0120f, 0.0f, 1.0f);
                const float widthN1 = Clamp((width1 * widthScale - 0.0010f) / 0.0120f, 0.0f, 1.0f);
                const Vec3 color0 = computeColor(strandIndex, widthN0);
                const Vec3 color1 = computeColor(strandIndex, widthN1);

                for (int sideIndex = 0; sideIndex < tubeSides; ++sideIndex)
                {
                    const float a0 = static_cast<float>(sideIndex) * 6.28318530718f / static_cast<float>(tubeSides);
                    const float a1 = static_cast<float>(sideIndex + 1) * 6.28318530718f / static_cast<float>(tubeSides);
                    const Vec3 radial0 = Normalize(side * std::cos(a0) + binormal * std::sin(a0));
                    const Vec3 radial1 = Normalize(side * std::cos(a1) + binormal * std::sin(a1));
                    const Vec3 v00 = p0 + radial0 * r0;
                    const Vec3 v01 = p0 + radial1 * r0;
                    const Vec3 v10 = p1 + radial0 * r1;
                    const Vec3 v11 = p1 + radial1 * r1;
                    appendQuad(v00, radial0, color0,
                               v01, radial1, color0,
                               v10, radial0, color1,
                               v11, radial1, color1,
                               alpha);
                }
            }
        }
    }

    bool GpuHairViewer::EnsureHairMeshVertexCapacity(size_t vertexCount)
    {
        if (vertexCount == 0)
        {
            return true;
        }
        if (vertexCount <= m_hairMeshVertexCapacity && m_hairMeshVertexBuffer)
        {
            return true;
        }

        const size_t newCapacity = std::max<size_t>(vertexCount, m_hairMeshVertexCapacity > 0 ? m_hairMeshVertexCapacity * 2ull : 4096ull);
        if (newCapacity > static_cast<size_t>(std::numeric_limits<UINT>::max() / sizeof(HairMeshVertex)))
        {
            return false;
        }

        D3D11_BUFFER_DESC desc = {};
        desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        desc.ByteWidth = static_cast<UINT>(newCapacity * sizeof(HairMeshVertex));
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(m_device->CreateBuffer(&desc, nullptr, &m_hairMeshVertexBuffer)))
        {
            return false;
        }

        m_hairMeshVertexCapacity = newCapacity;
        return true;
    }

    bool GpuHairViewer::EnsureGpuDebugLineCapacity(size_t lineCount)
    {
        if (lineCount == 0)
        {
            return true;
        }
        if (m_debugLineEntriesBuffer.buffer && m_debugLineEntryCapacity >= lineCount)
        {
            return true;
        }

        const size_t newCapacity = std::max<size_t>(lineCount, m_debugLineEntryCapacity > 0 ? m_debugLineEntryCapacity * 2ull : 4096ull);
        if (newCapacity > static_cast<size_t>(std::numeric_limits<UINT>::max() / sizeof(GpuDebugLineEntry)))
        {
            return false;
        }

        m_debugLineEntriesBuffer = {};
        m_debugLineEntryCapacity = newCapacity;

        D3D11_BUFFER_DESC desc = {};
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.ByteWidth = static_cast<UINT>(newCapacity * sizeof(GpuDebugLineEntry));
        desc.StructureByteStride = sizeof(GpuDebugLineEntry);
        desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        desc.Usage = D3D11_USAGE_DEFAULT;
        if (FAILED(m_device->CreateBuffer(&desc, nullptr, &m_debugLineEntriesBuffer.buffer)))
        {
            return false;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements = static_cast<UINT>(newCapacity);
        return SUCCEEDED(m_device->CreateShaderResourceView(m_debugLineEntriesBuffer.buffer.Get(), &srvDesc, &m_debugLineEntriesBuffer.srv));
    }

    void GpuHairViewer::RebuildGpuDebugLineEntries()
    {
        m_gpuDebugLineEntries.clear();
        m_debugLineStats = {};

        auto addWorldLine = [&](const Vec3& a, const Vec3& b, float r, float g, float bl, float alpha = 1.0f)
        {
            GpuDebugLineEntry entry = {};
            entry.type = 0u;
            entry.data0[0] = a.x; entry.data0[1] = a.y; entry.data0[2] = a.z;
            entry.data1[0] = b.x; entry.data1[1] = b.y; entry.data1[2] = b.z;
            entry.color[0] = r; entry.color[1] = g; entry.color[2] = bl; entry.color[3] = alpha;
            m_gpuDebugLineEntries.push_back(entry);
            ++m_debugLineStats.worldLineCount;
        };

        auto addHairLine = [&](uint32_t aIndex, uint32_t bIndex, float r, float g, float bl, float alpha = 1.0f)
        {
            GpuDebugLineEntry entry = {};
            entry.type = 1u;
            entry.indexA = aIndex;
            entry.indexB = bIndex;
            entry.color[0] = r; entry.color[1] = g; entry.color[2] = bl; entry.color[3] = alpha;
            m_gpuDebugLineEntries.push_back(entry);
            ++m_debugLineStats.hairLineCount;
        };

        auto addScalpLine = [&](uint32_t aIndex, uint32_t bIndex, float r, float g, float bl, float alpha = 1.0f)
        {
            GpuDebugLineEntry entry = {};
            entry.type = 2u;
            entry.indexA = aIndex;
            entry.indexB = bIndex;
            entry.color[0] = r; entry.color[1] = g; entry.color[2] = bl; entry.color[3] = alpha;
            m_gpuDebugLineEntries.push_back(entry);
            ++m_debugLineStats.scalpLineCount;
        };

        auto addRootMarker = [&](uint32_t particleIndex, bool isGuide)
        {
            const float s = isGuide ? 0.018f : 0.012f;
            const float r = isGuide ? 0.10f : 0.98f;
            const float g = isGuide ? 0.92f : 0.52f;
            const float b = isGuide ? 1.00f : 0.18f;
            for (uint32_t axis = 0; axis < 3u; ++axis)
            {
                GpuDebugLineEntry entry = {};
                entry.type = 3u;
                entry.indexA = particleIndex;
                entry.axisOrFlags = axis;
                entry.data0[3] = s;
                entry.color[0] = r; entry.color[1] = g; entry.color[2] = b; entry.color[3] = 0.95f;
                m_gpuDebugLineEntries.push_back(entry);
                ++m_debugLineStats.rootMarkerCount;
            }
        };

        if (m_showFloorGuide)
        {
            const float floorY = m_simConfig.floorY;
            const float halfExtent = 1.4f;
            const float step = 0.2f;
            for (float x = -halfExtent; x <= halfExtent + 0.001f; x += step)
            {
                addWorldLine(Vec3(x, floorY, -halfExtent), Vec3(x, floorY, halfExtent), 0.32f, 0.58f, 0.38f, 0.40f);
            }
            for (float z = -halfExtent; z <= halfExtent + 0.001f; z += step)
            {
                addWorldLine(Vec3(-halfExtent, floorY, z), Vec3(halfExtent, floorY, z), 0.32f, 0.58f, 0.38f, 0.40f);
            }
        }

        if (m_showGridBounds)
        {
            const Vec3 origin = m_simConfig.selfCollisionGridOrigin;
            const Vec3 extents(
                static_cast<float>(m_simConfig.selfCollisionGridDimX) * m_simConfig.selfCollisionCellSize,
                static_cast<float>(m_simConfig.selfCollisionGridDimY) * m_simConfig.selfCollisionCellSize,
                static_cast<float>(m_simConfig.selfCollisionGridDimZ) * m_simConfig.selfCollisionCellSize);
            const Vec3 p000 = origin;
            const Vec3 p100 = origin + Vec3(extents.x, 0.0f, 0.0f);
            const Vec3 p010 = origin + Vec3(0.0f, extents.y, 0.0f);
            const Vec3 p110 = origin + Vec3(extents.x, extents.y, 0.0f);
            const Vec3 p001 = origin + Vec3(0.0f, 0.0f, extents.z);
            const Vec3 p101 = origin + Vec3(extents.x, 0.0f, extents.z);
            const Vec3 p011 = origin + Vec3(0.0f, extents.y, extents.z);
            const Vec3 p111 = origin + extents;
            const float r = 0.15f, g = 0.95f, b = 0.65f, a = 0.75f;
            addWorldLine(p000, p100, r, g, b, a); addWorldLine(p000, p010, r, g, b, a); addWorldLine(p000, p001, r, g, b, a);
            addWorldLine(p111, p101, r, g, b, a); addWorldLine(p111, p110, r, g, b, a); addWorldLine(p111, p011, r, g, b, a);
            addWorldLine(p100, p110, r, g, b, a); addWorldLine(p100, p101, r, g, b, a);
            addWorldLine(p010, p110, r, g, b, a); addWorldLine(p010, p011, r, g, b, a);
            addWorldLine(p001, p101, r, g, b, a); addWorldLine(p001, p011, r, g, b, a);
        }
