// GpuHairViewer.cpp ordered part 01: window_device_and_ui_setup
// This file is included by ../GpuHairViewer.cpp; do not compile it directly.

        bool TryParseUIntTokenLocal(const std::string& token, uint32_t& outValue)
        {
            try
            {
                size_t consumed = 0u;
                const unsigned long value = std::stoul(token, &consumed, 10);
                if (consumed != token.size())
                {
                    return false;
                }
                outValue = static_cast<uint32_t>(value);
                return true;
            }
            catch (...)
            {
                return false;
            }
        }

        bool TryParseBoolTokenLocal(const std::string& token, bool& outValue)
        {
            const std::string lowered = ToLowerAsciiCopy(TrimAsciiCopy(token));
            if (lowered == "1" || lowered == "true" || lowered == "yes" || lowered == "on")
            {
                outValue = true;
                return true;
            }
            if (lowered == "0" || lowered == "false" || lowered == "no" || lowered == "off")
            {
                outValue = false;
                return true;
            }
            return false;
        }

        bool ParseRegionModeTokenLocal(const std::string& text, hairgpu::RegionSelectMode& outMode)
        {
            const std::string lowered = ToLowerAsciiCopy(TrimAsciiCopy(text));
            if (lowered.empty() || lowered == "all")
            {
                outMode = hairgpu::RegionSelectMode::All;
                return true;
            }
            if (lowered == "ellipse" || lowered == "circle")
            {
                outMode = hairgpu::RegionSelectMode::Ellipse;
                return true;
            }
            if (lowered == "rectangle" || lowered == "rect" || lowered == "box")
            {
                outMode = hairgpu::RegionSelectMode::Rectangle;
                return true;
            }
            if (lowered == "sector" || lowered == "fan")
            {
                outMode = hairgpu::RegionSelectMode::Sector;
                return true;
            }
            return false;
        }

        bool LocalLoadEditStackCsvFromStream(std::istream& in, hairgpu::EditStack& outStack, std::string& outError)
        {
            outStack = {};
            std::string line;
            uint32_t lineNumber = 0u;
            while (std::getline(in, line))
            {
                ++lineNumber;
                line = TrimAsciiCopy(line);
                if (line.empty() || line[0] == '#')
                {
                    continue;
                }

                std::vector<std::string> tokens = SplitSimpleCsvText(line);
                for (std::string& token : tokens)
                {
                    token = TrimAsciiCopy(token);
                }
                if (tokens.empty())
                {
                    continue;
                }

                const std::string kind = ToLowerAsciiCopy(tokens[0]);
                if (kind == "stack")
                {
                    if (tokens.size() >= 2u && !tokens[1].empty() && !TryParseFloatTokenLocal(tokens[1], outStack.boundarySmoothing))
                    {
                        outError = "Malformed boundary smoothing at line " + std::to_string(lineNumber);
                        return false;
                    }
                    if (tokens.size() >= 3u && !tokens[2].empty() && !TryParseFloatTokenLocal(tokens[2], outStack.boundaryRadiusUv))
                    {
                        outError = "Malformed boundary radius at line " + std::to_string(lineNumber);
                        return false;
                    }
                    continue;
                }

                if (kind != "layer")
                {
                    outError = "Unsupported edit stack row at line " + std::to_string(lineNumber) + ": " + line;
                    return false;
                }

                hairgpu::EditLayer layer = {};
                layer.name = (tokens.size() >= 2u && !tokens[1].empty()) ? tokens[1] : ("layer_" + std::to_string(outStack.layers.size()));
                bool enabled = true;
                if (tokens.size() >= 3u && !tokens[2].empty() && !TryParseBoolTokenLocal(tokens[2], enabled))
                {
                    outError = "Malformed enabled flag at line " + std::to_string(lineNumber);
                    return false;
                }
                layer.enabled = enabled;
                if (tokens.size() >= 4u && !tokens[3].empty() && !TryParseFloatTokenLocal(tokens[3], layer.layerWeight))
                {
                    outError = "Malformed layer weight at line " + std::to_string(lineNumber);
                    return false;
                }
                if (tokens.size() >= 5u && !tokens[4].empty() && !ParseRegionModeTokenLocal(tokens[4], layer.region.mode))
                {
                    outError = "Malformed region mode at line " + std::to_string(lineNumber);
                    return false;
                }

                auto parseFloatOptional = [&](size_t index, float& value, const char* label) -> bool
                {
                    if (index >= tokens.size() || tokens[index].empty())
                    {
                        return true;
                    }
                    if (!TryParseFloatTokenLocal(tokens[index], value))
                    {
                        outError = std::string("Malformed ") + label + " at line " + std::to_string(lineNumber);
                        return false;
                    }
                    return true;
                };
                auto parseUIntOptional = [&](size_t index, uint32_t& value, const char* label) -> bool
                {
                    if (index >= tokens.size() || tokens[index].empty())
                    {
                        return true;
                    }
                    if (!TryParseUIntTokenLocal(tokens[index], value))
                    {
                        outError = std::string("Malformed ") + label + " at line " + std::to_string(lineNumber);
                        return false;
                    }
                    return true;
                };
                auto parseBoolOptional = [&](size_t index, bool& value, const char* label) -> bool
                {
                    if (index >= tokens.size() || tokens[index].empty())
                    {
                        return true;
                    }
                    if (!TryParseBoolTokenLocal(tokens[index], value))
                    {
                        outError = std::string("Malformed ") + label + " at line " + std::to_string(lineNumber);
                        return false;
                    }
                    return true;
                };

                uint32_t groupId = UINT32_MAX;
                if (!parseFloatOptional(5u, layer.region.centerUv.x, "center u") ||
                    !parseFloatOptional(6u, layer.region.centerUv.y, "center v") ||
                    !parseFloatOptional(7u, layer.region.radiusUv.x, "radius x") ||
                    !parseFloatOptional(8u, layer.region.radiusUv.y, "radius y") ||
                    !parseFloatOptional(9u, layer.region.innerRadius, "inner radius") ||
                    !parseFloatOptional(10u, layer.region.angleCenterRadians, "angle center") ||
                    !parseFloatOptional(11u, layer.region.angleHalfExtentRadians, "angle half extent") ||
                    !parseFloatOptional(12u, layer.region.feather, "feather") ||
                    !parseUIntOptional(13u, groupId, "groupId") ||
                    !parseBoolOptional(14u, layer.region.guidesOnly, "guidesOnly") ||
                    !parseFloatOptional(15u, layer.edit.lengthScale, "lengthScale") ||
                    !parseFloatOptional(16u, layer.edit.widthScale, "widthScale") ||
                    !parseFloatOptional(17u, layer.edit.rootOffset.x, "rootOffsetX") ||
                    !parseFloatOptional(18u, layer.edit.rootOffset.y, "rootOffsetY") ||
                    !parseFloatOptional(19u, layer.edit.rootOffset.z, "rootOffsetZ") ||
                    !parseFloatOptional(20u, layer.edit.tipOffset.x, "tipOffsetX") ||
                    !parseFloatOptional(21u, layer.edit.tipOffset.y, "tipOffsetY") ||
                    !parseFloatOptional(22u, layer.edit.tipOffset.z, "tipOffsetZ") ||
                    !parseFloatOptional(23u, layer.edit.sideOffset, "sideOffset") ||
                    !parseFloatOptional(24u, layer.edit.yawRadians, "yawRadians") ||
                    !parseFloatOptional(25u, layer.edit.followerInfluence, "followerInfluence"))
                {
                    return false;
                }
                layer.region.groupId = groupId;
                if (tokens.size() >= 27u && !tokens[26].empty())
                {
                    if (!hairgpu::ParseStrandSelectionSpec(tokens[26], layer.strandSelection, outError))
                    {
                        outError += " at line " + std::to_string(lineNumber);
                        return false;
                    }
                }
                if (tokens.size() >= 28u && !tokens[27].empty())
                {
                    const std::string mode = ToLowerAsciiCopy(TrimAsciiCopy(tokens[27]));
                    if (mode == "union") layer.strandSelectionMode = hairgpu::StrandSelectionMode::Union;
                    else if (mode == "replace" || mode == "exact") layer.strandSelectionMode = hairgpu::StrandSelectionMode::Replace;
                    else if (mode == "guide_family" || mode == "guidefamily" || mode == "family") layer.strandSelectionMode = hairgpu::StrandSelectionMode::GuideFamily;
                    else if (mode.empty() || mode == "none") layer.strandSelectionMode = hairgpu::StrandSelectionMode::None;
                    else
                    {
                        outError = "Malformed strand selection mode at line " + std::to_string(lineNumber);
                        return false;
                    }
                }
                if (tokens.size() >= 29u && !tokens[28].empty() && !TryParseFloatTokenLocal(tokens[28], layer.strandSelectionWeight))
                {
                    outError = "Malformed strand selection weight at line " + std::to_string(lineNumber);
                    return false;
                }
                if (!layer.strandSelection.empty() && layer.strandSelectionMode == hairgpu::StrandSelectionMode::None)
                {
                    layer.strandSelectionMode = hairgpu::StrandSelectionMode::Union;
                }
                outStack.layers.push_back(layer);
            }

            outError.clear();
            return true;
        }

        bool LocalLoadEditStackCsv(const std::filesystem::path& path, hairgpu::EditStack& outStack, std::string& outError)
        {
            std::ifstream in(path);
            if (!in)
            {
                outError = "Failed to open edit stack CSV: " + path.string();
                return false;
            }
            return LocalLoadEditStackCsvFromStream(in, outStack, outError);
        }

        bool LocalLoadEditStackCsvText(const std::string& text, hairgpu::EditStack& outStack, std::string& outError)
        {
            std::istringstream in(text);
            return LocalLoadEditStackCsvFromStream(in, outStack, outError);
        }

        std::string SerializeEditStackToCsvText(const hairgpu::EditStack& stack)
        {
            std::ostringstream out;
            out << std::fixed << std::setprecision(6);
            out << "# edit stack csv\n";
            out << "# stack,boundarySmoothing,boundaryRadiusUv\n";
            out << "stack," << stack.boundarySmoothing << "," << stack.boundaryRadiusUv << "\n";
            out << "# layer,name,enabled,layerWeight,mode,cx,cy,rx,ry,innerRadius,angleCenterRadians,angleHalfExtentRadians,feather,groupId,guidesOnly,lengthScale,widthScale,rootOffsetX,rootOffsetY,rootOffsetZ,tipOffsetX,tipOffsetY,tipOffsetZ,sideOffset,yawRadians,followerInfluence,strandSelection,strandSelectionMode,strandSelectionWeight\n";
            auto modeToToken = [](hairgpu::RegionSelectMode mode)
            {
                switch (mode)
                {
                case hairgpu::RegionSelectMode::Ellipse: return "ellipse";
                case hairgpu::RegionSelectMode::Rectangle: return "rectangle";
                case hairgpu::RegionSelectMode::Sector: return "sector";
                default: return "all";
                }
            };
            auto selectionModeToToken = [](hairgpu::StrandSelectionMode mode)
            {
                switch (mode)
                {
                case hairgpu::StrandSelectionMode::Union: return "union";
                case hairgpu::StrandSelectionMode::Replace: return "replace";
                case hairgpu::StrandSelectionMode::GuideFamily: return "guide_family";
                default: return "none";
                }
            };
            for (const hairgpu::EditLayer& layer : stack.layers)
            {
                out << "layer,"
                    << layer.name << ","
                    << (layer.enabled ? 1 : 0) << ","
                    << layer.layerWeight << ","
                    << modeToToken(layer.region.mode) << ","
                    << layer.region.centerUv.x << ","
                    << layer.region.centerUv.y << ","
                    << layer.region.radiusUv.x << ","
                    << layer.region.radiusUv.y << ","
                    << layer.region.innerRadius << ","
                    << layer.region.angleCenterRadians << ","
                    << layer.region.angleHalfExtentRadians << ","
                    << layer.region.feather << ",";
                if (layer.region.groupId != UINT32_MAX)
                {
                    out << layer.region.groupId;
                }
                out << ","
                    << (layer.region.guidesOnly ? 1 : 0) << ","
                    << layer.edit.lengthScale << ","
                    << layer.edit.widthScale << ","
                    << layer.edit.rootOffset.x << ","
                    << layer.edit.rootOffset.y << ","
                    << layer.edit.rootOffset.z << ","
                    << layer.edit.tipOffset.x << ","
                    << layer.edit.tipOffset.y << ","
                    << layer.edit.tipOffset.z << ","
                    << layer.edit.sideOffset << ","
                    << layer.edit.yawRadians << ","
                    << layer.edit.followerInfluence << ","
                    << hairgpu::FormatStrandSelectionSpec(layer.strandSelection) << ","
                    << selectionModeToToken(layer.strandSelectionMode) << ","
                    << layer.strandSelectionWeight << "\n";
            }
            return out.str();
        }

        bool WriteUtf8TextFile(const std::filesystem::path& path, const std::string& text)
        {
            std::ofstream out(path, std::ios::binary);
            if (!out)
            {
                return false;
            }
            out.write(text.data(), static_cast<std::streamsize>(text.size()));
            return static_cast<bool>(out);
        }

        std::string EscapeSettingsText(const std::string& text)
        {
            std::string escaped;
            escaped.reserve(text.size());
            for (char c : text)
            {
                switch (c)
                {
                case '\\': escaped += "\\\\"; break;
                case '\n': escaped += "\\n"; break;
                case '\r': escaped += "\\r"; break;
                default: escaped.push_back(c); break;
                }
            }
            return escaped;
        }

        std::string UnescapeSettingsText(const std::string& text)
        {
            std::string unescaped;
            unescaped.reserve(text.size());
            bool escaping = false;
            for (char c : text)
            {
                if (!escaping)
                {
                    if (c == '\\')
                    {
                        escaping = true;
                    }
                    else
                    {
                        unescaped.push_back(c);
                    }
                    continue;
                }

                switch (c)
                {
                case 'n': unescaped.push_back('\n'); break;
                case 'r': unescaped.push_back('\r'); break;
                case '\\': unescaped.push_back('\\'); break;
                default:
                    unescaped.push_back(c);
                    break;
                }
                escaping = false;
            }
            if (escaping)
            {
                unescaped.push_back('\\');
            }
            return unescaped;
        }

        hairgpu::Mat4 TransposeMatrixForHlsl(const hairgpu::Mat4& m)
        {
            hairgpu::Mat4 t = {};
            for (int row = 0; row < 4; ++row)
            {
                for (int col = 0; col < 4; ++col)
                {
                    t.m[row * 4 + col] = m.m[col * 4 + row];
                }
            }
            return t;
        }

        bool ParticleArrayLooksValid(const float* particleData, size_t particleCount, const hairgpu::HairAsset& asset)
        {
            if (particleData == nullptr || particleCount < asset.restPoints.size() || asset.restPoints.empty())
            {
                return false;
            }

            float minX = std::numeric_limits<float>::max();
            float minY = std::numeric_limits<float>::max();
            float minZ = std::numeric_limits<float>::max();
            float maxX = -std::numeric_limits<float>::max();
            float maxY = -std::numeric_limits<float>::max();
            float maxZ = -std::numeric_limits<float>::max();
            size_t finiteCount = 0;

            const size_t stride = std::max<size_t>(particleCount / 256, 1ull);
            for (size_t i = 0; i < particleCount; i += stride)
            {
                const float* p = particleData + i * 4;
                if (!std::isfinite(p[0]) || !std::isfinite(p[1]) || !std::isfinite(p[2]))
                {
                    continue;
                }
                ++finiteCount;
                minX = std::min(minX, p[0]); minY = std::min(minY, p[1]); minZ = std::min(minZ, p[2]);
                maxX = std::max(maxX, p[0]); maxY = std::max(maxY, p[1]); maxZ = std::max(maxZ, p[2]);
            }

            if (finiteCount == 0)
            {
                return false;
            }

            const float extentX = maxX - minX;
            const float extentY = maxY - minY;
            const float extentZ = maxZ - minZ;
            if (!std::isfinite(extentX) || !std::isfinite(extentY) || !std::isfinite(extentZ))
            {
                return false;
            }

            return extentX < 20.0f && extentY < 20.0f && extentZ < 20.0f && minY > -20.0f && maxY < 20.0f;
        }

        float DistanceToSphereSurface(const hairgpu::Vec3& p, const hairgpu::SphereCollider& sphere)
        {
            return hairgpu::Length(p - sphere.center) - sphere.radius;
        }

        bool ProjectWorldToNdc(const hairgpu::Vec3& world, const hairgpu::Vec3& eye, const hairgpu::Vec3& target, float aspectRatio, hairgpu::Vec3& outNdc)
        {
            constexpr float kFovY = 60.0f * 3.1415926535f / 180.0f;
            constexpr float kNearZ = 0.01f;
            constexpr float kFarZ = 20.0f;

            const hairgpu::Vec3 worldUp(0.0f, 1.0f, 0.0f);
            const hairgpu::Vec3 forward = hairgpu::Normalize(target - eye);
            hairgpu::Vec3 right = hairgpu::Normalize(hairgpu::Cross(forward, worldUp));
            if (hairgpu::Length(right) <= 1.0e-6f)
            {
                right = hairgpu::Vec3(1.0f, 0.0f, 0.0f);
            }
            const hairgpu::Vec3 up = hairgpu::Normalize(hairgpu::Cross(right, forward));
            const hairgpu::Vec3 delta = world - eye;

            const float vx = hairgpu::Dot(delta, right);
            const float vy = hairgpu::Dot(delta, up);
            const float vz = hairgpu::Dot(delta, forward);
            if (!std::isfinite(vx) || !std::isfinite(vy) || !std::isfinite(vz) || vz <= kNearZ)
            {
                return false;
            }

            const float tanHalfY = std::tan(kFovY * 0.5f);
            const float tanHalfX = tanHalfY * std::max(aspectRatio, 0.001f);
            outNdc.x = vx / (vz * tanHalfX);
            outNdc.y = vy / (vz * tanHalfY);
            outNdc.z = hairgpu::Clamp((vz - kNearZ) / (kFarZ - kNearZ), 0.0f, 1.0f);
            return std::isfinite(outNdc.x) && std::isfinite(outNdc.y) && std::isfinite(outNdc.z);
        }


        UINT DispatchGroups(UINT itemCount, UINT groupSize)
        {
            return (itemCount + groupSize - 1u) / groupSize;
        }

        std::wstring HrHex(HRESULT hr)
        {
            std::wstringstream ss;
            ss << L"0x" << std::uppercase << std::hex << static_cast<unsigned long>(hr);
            return ss.str();
        }

        std::wstring Win32Hex(DWORD value)
        {
            std::wstringstream ss;
            ss << L"0x" << std::uppercase << std::hex << static_cast<unsigned long>(value);
            return ss.str();
        }

        uint32_t NextPowerOfTwo(uint32_t value)
        {
            if (value <= 1u)
            {
                return 1u;
            }
            --value;
            value |= value >> 1u;
            value |= value >> 2u;
            value |= value >> 4u;
            value |= value >> 8u;
            value |= value >> 16u;
            return value + 1u;
        }

        const wchar_t* BroadphaseModeShortName(uint32_t mode)
        {
            switch (mode % 4u)
            {
            case 0u: return L"dense-grid";
            case 1u: return L"spatial-hash";
            case 2u: return L"adaptive-hash";
            case 3u: return L"sorted-grid";
            default: return L"unknown";
            }
        }

        const wchar_t* BroadphaseModeLongName(uint32_t mode)
        {
            switch (mode % 4u)
            {
            case 0u: return L"Dense Grid";
            case 1u: return L"Spatial Hash";
            case 2u: return L"Adaptive Hash";
            case 3u: return L"Sorted Grid";
            default: return L"Unknown";
            }
        }

        void ShowStartupDialog(HWND owner, const wchar_t* title, const std::wstring& message)
        {
            MessageBoxW(owner, message.c_str(), title, MB_OK | MB_ICONERROR);
        }

        std::wstring Utf8ToWide(const std::string& text)
        {
            if (text.empty())
            {
                return {};
            }
            const int length = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
            if (length <= 0)
            {
                return std::wstring(text.begin(), text.end());
            }
            std::wstring wide(static_cast<size_t>(length), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wide.data(), length);
            if (!wide.empty() && wide.back() == L'\0')
            {
                wide.pop_back();
            }
            return wide;
        }

        std::string WideToUtf8(const std::wstring& text)
        {
            if (text.empty())
            {
                return {};
            }
            const int length = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
            if (length <= 0)
            {
                return std::string(text.begin(), text.end());
            }
            std::string utf8(static_cast<size_t>(length), '\0');
            WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, utf8.data(), length, nullptr, nullptr);
            if (!utf8.empty() && utf8.back() == '\0')
            {
                utf8.pop_back();
            }
            return utf8;
        }

        std::wstring NormalizePathExtensionLower(const std::filesystem::path& path)
        {
            std::wstring ext = path.extension().wstring();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
            return ext;
        }

        bool EnsureAssetScalpConformance(hairgpu::HairAsset& asset, std::wstring* statusOut = nullptr)
        {
            if (asset.strands.empty() || asset.restPoints.empty() || asset.scalpMesh.positions.empty() || asset.scalpMesh.triangles.empty())
            {
                if (statusOut != nullptr)
                {
                    *statusOut = L"No scalp conformance pass applied";
                }
                return false;
            }

            hairgpu::RootBindingQualityReport report = {};
            if (!hairgpu::RepairHairAssetSkinning(asset, true, &report))
            {
                if (statusOut != nullptr)
                {
                    *statusOut = L"Scalp conformance repair failed";
                }
                return false;
            }

            if (statusOut != nullptr)
            {
                std::wstringstream ss;
                ss << L"Roots snapped to scalp (avg=" << std::fixed << std::setprecision(4)
                   << report.averageRootDistance << L", max=" << report.maxRootDistance
                   << L", invalid=" << report.invalidStrandCount << L")";
                *statusOut = ss.str();
            }
            return true;
        }
    }

    bool GpuHairViewer::Initialize(HINSTANCE instance, int showCommand)
    {
        INITCOMMONCONTROLSEX icc = {};
        icc.dwSize = sizeof(icc);
        icc.dwICC = ICC_BAR_CLASSES | ICC_STANDARD_CLASSES;
        InitCommonControlsEx(&icc);

        // HAIRGPU_EXPERIMENT_RUNTIME_PATCH_V1: configuration must be resolved before window, asset,
        // GPU buffers, and shaders are created.
        if (!LoadExperimentCaseFromEnvironment())
        {
            ShowStartupDialog(nullptr, L"Hair GPU Viewer Experiment Configuration Failure",
                L"HAIRGPU_CASE_JSON or experiment environment values are invalid.");
            return false;
        }
        ApplyExperimentConfigBeforeAssetCreation();

        const float initialSegmentLength = SegmentLengthForParticleCount(m_builderParticlesPerStrand);

        // The formal 100k asset is expensive to procedurally rebuild. Reuse the existing
        // Hair binary I/O path and cache only topology-identical assets. This changes no
        // simulation, novelty, or strand-count semantics.
        wchar_t assetCacheModulePath[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, assetCacheModulePath, MAX_PATH);
        const std::filesystem::path assetCacheDirectory =
            std::filesystem::path(assetCacheModulePath).parent_path() / L"asset_cache";
        const std::wstring assetCacheFileName =
            L"sample_hybrid_cache_v1_g" + std::to_wstring(m_builderGuideCount) +
            L"_f" + std::to_wstring(m_builderFollowersPerGuide) +
            L"_p" + std::to_wstring(m_builderParticlesPerStrand) + L".hairbin";
        const std::filesystem::path assetCachePath = assetCacheDirectory / assetCacheFileName;

        const uint64_t expectedCachedStrands =
            static_cast<uint64_t>(m_builderGuideCount) *
            static_cast<uint64_t>(m_builderFollowersPerGuide + 1u);
        const uint64_t expectedCachedPoints =
            expectedCachedStrands * static_cast<uint64_t>(m_builderParticlesPerStrand);

        hairgpu::HairAsset cachedAsset = {};
        std::string assetCacheError;
        bool loadedCachedAsset = false;
        std::error_code assetCacheEc;
        if (std::filesystem::exists(assetCachePath, assetCacheEc) && !assetCacheEc &&
            hairgpu::LoadHairAssetFromHairBinary(assetCachePath, cachedAsset, assetCacheError))
        {
            loadedCachedAsset =
                cachedAsset.guideStrandCount == m_builderGuideCount &&
                cachedAsset.followerStrandCount == m_builderGuideCount * m_builderFollowersPerGuide &&
                cachedAsset.particlesPerStrand == m_builderParticlesPerStrand &&
                cachedAsset.strands.size() == expectedCachedStrands &&
                cachedAsset.restPoints.size() == expectedCachedPoints &&
                cachedAsset.pointWidths.size() == expectedCachedPoints &&
                cachedAsset.rootBindings.size() == expectedCachedStrands &&
                cachedAsset.followData.size() == expectedCachedStrands;
        }

        if (loadedCachedAsset)
        {
            m_asset = std::move(cachedAsset);
            m_lastAssetSource = L"Sample Hybrid Binary Cache";
            m_lastImportStatus = L"Validated binary asset cache loaded";
        }
        else
        {
            m_asset = BuildSampleHybridHairAsset(
                m_builderGuideCount,
                m_builderFollowersPerGuide,
                m_builderParticlesPerStrand,
                initialSegmentLength);
            // Avoid double wig styling in viewer; sample/import paths already shape the asset.
            EnsureAssetScalpConformance(m_asset, &m_lastImportStatus);
            m_lastAssetSource = L"Sample Hybrid (cache rebuilt)";
            if (m_lastImportStatus.empty())
            {
                m_lastImportStatus = L"Sample asset active";
            }

            std::filesystem::create_directories(assetCacheDirectory, assetCacheEc);
            if (!assetCacheEc)
            {
                std::string saveCacheError;
                hairgpu::SaveHairAssetToHairBinary(assetCachePath, m_asset, saveCacheError);
            }
        }
        m_activeSetBudget.maxActiveClusters = 6u;
        m_activeSetBudget.maxActiveGuides = 256u;
        m_activeSetBudget.promoteScoreThreshold = 0.52f;
        m_simConfig.solverIterations = 12;
        m_simConfig.fixedTimeStep = 1.0f / 120.0f;
        m_simConfig.gravity = 18.5f;
        m_simConfig.globalDamping = 0.100f;
        m_simConfig.segmentLength = initialSegmentLength;
        m_simConfig.bendStiffness = 0.14f;
        m_simConfig.followStiffness = 0.985f;
        m_simConfig.windStrength = 0.03f;
        m_simConfig.windDirection = { 0.90f, 0.0f, 0.25f };
        m_simConfig.floorY = -1.05f;
        m_simConfig.selfCollisionRadius = 0.0042f;
        m_simConfig.selfCollisionStiffness = 0.18f;
        m_simConfig.stretchCompliance = 2.0e-7f;
        m_simConfig.bendCompliance = 1.5e-5f;
        m_simConfig.rootStiffness = 0.28f;
        m_simConfig.velocityShockPropagation = 0.72f;
        m_simConfig.selfCollisionGridDimX = 18;
        m_simConfig.selfCollisionGridDimY = 18;
        m_simConfig.selfCollisionGridDimZ = 18;
        m_simConfig.selfCollisionCellCapacity = 64;
        m_simConfig.selfCollisionGridOrigin = { -0.72f, 0.48f, -0.72f };
        m_simConfig.selfCollisionCellSize = 0.040f;
        m_selfCollisionRadius = m_simConfig.selfCollisionRadius;
        m_selfCollisionStiffness = m_simConfig.selfCollisionStiffness;
        m_enableRuntimeAngularBend = true;
        m_runtimeAngularBendRadians = 0.16f;
        m_runtimeAngularBendCompliance = 5.0e-6f;
        m_runtimeAngularBendStiffness = 0.36f;
        m_runtimeAngularBendDamping = 0.08f;
        m_widthScale = 0.56f;
        m_renderMode = RenderMode::ParticleBillboard;
        m_showScalpMesh = true;
        m_showScalpWireframe = false;
        m_showGridBounds = false;
        m_showGuideSkeleton = false;
        m_showFollowerSkeleton = false;
        m_showRootMarkers = false;
        m_showFollowLinks = false;
        m_showFloorGuide = false;
        m_showColliderDebug = false;
        m_enableSelfCollision = false;
        m_enableGridSelfCollision = false;
        m_selfCollisionBroadphaseMode = 1u; // Spatial hash; bitonic mode is comparison-only.

        if (m_experimentCase.enabled)
        {
            m_simConfig.solverIterations = std::max<uint32_t>(m_experimentCase.requestedSolverIterations, 1u);
            if (m_experimentCase.fixedTimeStepEnabled && m_experimentCase.fixedTimeStepSeconds > 0.0f)
            {
                m_simConfig.fixedTimeStep = m_experimentCase.fixedTimeStepSeconds;
            }
            m_vsyncEnabled = m_experimentCase.vsyncEnabled;
            m_presentSyncInterval = m_vsyncEnabled
                ? std::clamp<UINT>(m_experimentCase.presentSyncInterval, 1u, 4u)
                : 0u;
        }

        m_colliders.spheres.push_back({ { 0.0f, 1.66f, 0.0f }, 0.215f });
        m_colliders.capsules.push_back({ { -0.19f, 1.36f, 0.02f }, 0.11f, { -0.42f, 1.15f, 0.04f }, 0.08f });
        m_colliders.capsules.push_back({ {  0.19f, 1.36f, 0.02f }, 0.11f, {  0.42f, 1.15f, 0.04f }, 0.08f });
        m_colliders.capsules.push_back({ {  0.00f, 1.44f, -0.08f }, 0.14f, {  0.00f, 0.92f, -0.10f }, 0.16f });

        if (!CreateWindowAndDevice(instance, showCommand))
        {
            return false;
        }

        if (!CreateUiControls())
        {
            ShowStartupDialog(m_hwnd, L"Hair GPU Viewer Startup Failure", L"CreateUiControls() failed.");
            Shutdown();
            return false;
        }

