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
                return {};
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


    void GpuHairViewer::EnforceFixedWorkload()
    {
        m_builderGuideCount = kFixedGuideCount;
        m_builderFollowersPerGuide = kFixedFollowersPerGuide;
        m_builderParticlesPerStrand = kFixedParticlesPerStrand;
        m_simConfig.solverIterations = kFixedSolverIterations;
        m_simConfig.fixedTimeStep = kFixedTimeStepSeconds;
        m_experimentCase.requestedGuideCount = kFixedGuideCount;
        m_experimentCase.requestedDisplayStrandCount = kFixedDisplayStrandCount;
        m_experimentCase.requestedParticlesPerGuide = kFixedParticlesPerStrand;
        m_experimentCase.requestedSolverIterations = kFixedSolverIterations;
        m_experimentCase.fixedTimeStepSeconds = kFixedTimeStepSeconds;
    }

    bool GpuHairViewer::CreateUiControls()
    {
        HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        auto createControl = [&](DWORD exStyle, const wchar_t* klass, const wchar_t* text, DWORD style, int id) -> HWND
        {
            HWND hwnd = CreateWindowExW(exStyle, klass, text, style | WS_CHILD | WS_VISIBLE,
                0, 0, 10, 10, m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
            if (hwnd && font)
            {
                SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            }
            return hwnd;
        };

        m_uiPanel = createControl(WS_EX_CLIENTEDGE, L"STATIC", L"", SS_LEFT, 0);
        if (!m_uiPanel)
        {
            return false;
        }

        createControl(0, L"STATIC", L"Preset", SS_LEFT, 0);
        m_comboPreset = createControl(0, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL | CBS_HASSTRINGS, IDC_COMBO_PRESET);
        SendMessageW(m_comboPreset, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Rapunzel Preview"));
        SendMessageW(m_comboPreset, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Calm Studio"));
        SendMessageW(m_comboPreset, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Windy Demo"));
        SendMessageW(m_comboPreset, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Heavy Grounded"));
        SendMessageW(m_comboPreset, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Game-ish LOD"));

        m_btnPlayPause = createControl(0, L"BUTTON", L"Pause", BS_PUSHBUTTON, IDC_BTN_PLAYPAUSE);
        m_btnReset = createControl(0, L"BUTTON", L"Reset", BS_PUSHBUTTON, IDC_BTN_RESET);
        m_btnStep = createControl(0, L"BUTTON", L"Step", BS_PUSHBUTTON, IDC_BTN_STEP);
        m_btnApplyAsset = createControl(0, L"BUTTON", L"Rebuild Hair", BS_PUSHBUTTON, IDC_BTN_APPLY_ASSET);
        m_btnImportAsset = createControl(0, L"BUTTON", L"Import Hair...", BS_PUSHBUTTON, IDC_BTN_IMPORT_ASSET);
        m_btnSaveSettings = createControl(0, L"BUTTON", L"Save Settings", BS_PUSHBUTTON, IDC_BTN_SAVE_SETTINGS);
        m_btnLoadSettings = createControl(0, L"BUTTON", L"Load Settings", BS_PUSHBUTTON, IDC_BTN_LOAD_SETTINGS);
        m_btnExportCsv = createControl(0, L"BUTTON", L"Export Frame CSV", BS_PUSHBUTTON, IDC_BTN_EXPORT_CSV);
        m_btnExportPerfCsv = createControl(0, L"BUTTON", L"Export Perf CSV", BS_PUSHBUTTON, IDC_BTN_EXPORT_PERF_CSV);
        m_btnBrowseEditStack = createControl(0, L"BUTTON", L"Open Saved Edits...", BS_PUSHBUTTON, IDC_BTN_BROWSE_EDIT_STACK);
        m_btnClearEditStack = createControl(0, L"BUTTON", L"Reset All Edits", BS_PUSHBUTTON, IDC_BTN_CLEAR_EDIT_STACK);
        m_labelEditStack = createControl(0, L"STATIC", L"Edit stack: none", SS_LEFT, 0);
        m_labelInlineEditStack = createControl(0, L"STATIC", L"Advanced Stack Text (optional)", SS_LEFT, IDC_LABEL_INLINE_EDIT_STACK);
        m_editInlineEditStack = createControl(WS_EX_CLIENTEDGE, L"EDIT", BuildDefaultInlineEditStackText().c_str(), ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL, IDC_EDIT_INLINE_EDIT_STACK);
        m_btnApplyInlineEditStack = createControl(0, L"BUTTON", L"Apply Inline", BS_PUSHBUTTON, IDC_BTN_APPLY_INLINE_EDIT_STACK);
        m_btnSaveInlineEditStack = createControl(0, L"BUTTON", L"Save Inline...", BS_PUSHBUTTON, IDC_BTN_SAVE_INLINE_EDIT_STACK);
        m_btnResetInlineEditStack = createControl(0, L"BUTTON", L"Reset Inline", BS_PUSHBUTTON, IDC_BTN_RESET_INLINE_EDIT_STACK);
        m_labelQuickEditForm = createControl(0, L"STATIC", L"3) Edit the Selected Layer", SS_LEFT, IDC_LABEL_QUICK_EDIT_FORM);
        m_labelLayerList = createControl(0, L"STATIC", L"2) Pick a Layer", SS_LEFT, IDC_LABEL_INLINE_LAYER_LIST);
        m_listInlineLayers = createControl(WS_EX_CLIENTEDGE, WC_LISTBOXW, L"", LBS_NOTIFY | WS_VSCROLL | LBS_HASSTRINGS, IDC_LIST_INLINE_LAYERS);
        m_labelQuickEditName = createControl(0, L"STATIC", L"Layer Name", SS_LEFT, IDC_LABEL_QUICK_EDIT_NAME);
        m_editQuickEditName = createControl(WS_EX_CLIENTEDGE, L"EDIT", L"quick_form", ES_AUTOHSCROLL, IDC_EDIT_QUICK_EDIT_NAME);
        m_btnQuickEditMoveUp = createControl(0, L"BUTTON", L"Move Up", BS_PUSHBUTTON, IDC_BTN_QUICK_EDIT_MOVE_UP);
        m_btnQuickEditMoveDown = createControl(0, L"BUTTON", L"Move Down", BS_PUSHBUTTON, IDC_BTN_QUICK_EDIT_MOVE_DOWN);
        m_checkQuickEditEnabled = createControl(0, L"BUTTON", L"Layer Enabled", BS_AUTOCHECKBOX, IDC_CHECK_QUICK_EDIT_ENABLED);
        m_btnQuickEditDelete = createControl(0, L"BUTTON", L"Delete Layer", BS_PUSHBUTTON, IDC_BTN_QUICK_EDIT_DELETE);
        m_btnQuickEditDuplicate = createControl(0, L"BUTTON", L"Duplicate Layer", BS_PUSHBUTTON, IDC_BTN_QUICK_EDIT_DUPLICATE);
        m_comboQuickEditMode = createControl(0, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL | CBS_HASSTRINGS, IDC_COMBO_QUICK_EDIT_MODE);
        SendMessageW(m_comboQuickEditMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Ellipse"));
        SendMessageW(m_comboQuickEditMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Rectangle"));
        SendMessageW(m_comboQuickEditMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Sector"));
        SendMessageW(m_comboQuickEditMode, CB_SETCURSEL, 0, 0);
        m_labelQuickEditFeather = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_QUICK_EDIT_FEATHER);
        m_labelQuickEditLengthScale = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_QUICK_EDIT_LENGTHSCALE);
        m_labelQuickEditLayerWeight = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_QUICK_EDIT_LAYERWEIGHT);
        m_labelQuickEditCenterU = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_QUICK_EDIT_CENTER_U);
        m_labelQuickEditCenterV = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_QUICK_EDIT_CENTER_V);
        m_labelQuickEditRadiusU = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_QUICK_EDIT_RADIUS_U);
        m_labelQuickEditRadiusV = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_QUICK_EDIT_RADIUS_V);
        m_labelQuickEditInnerRadius = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_QUICK_EDIT_INNER_RADIUS);
        m_labelQuickEditAngleCenter = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_QUICK_EDIT_ANGLE_CENTER);
        m_labelQuickEditAngleHalfExtent = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_QUICK_EDIT_ANGLE_HALF_EXTENT);
        m_labelQuickEditRootOffsetX = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_QUICK_EDIT_ROOT_OFFSET_X);
        m_labelQuickEditRootOffsetY = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_QUICK_EDIT_ROOT_OFFSET_Y);
        m_labelQuickEditRootOffsetZ = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_QUICK_EDIT_ROOT_OFFSET_Z);
        m_labelQuickEditTipOffsetX = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_QUICK_EDIT_TIP_OFFSET_X);
        m_labelQuickEditTipOffsetY = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_QUICK_EDIT_TIP_OFFSET_Y);
        m_labelQuickEditTipOffsetZ = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_QUICK_EDIT_TIP_OFFSET_Z);
        m_labelQuickEditSideOffset = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_QUICK_EDIT_SIDE_OFFSET);
        m_labelQuickEditYaw = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_QUICK_EDIT_YAW);
        m_labelQuickEditWidthScale = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_QUICK_EDIT_WIDTHSCALE);
        m_labelQuickEditFollowerInfluence = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_QUICK_EDIT_FOLLOWER_INFLUENCE);
        m_labelQuickEditBoundarySmoothing = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_QUICK_EDIT_BOUNDARY_SMOOTHING);
        m_labelQuickEditBoundaryRadiusUv = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_QUICK_EDIT_BOUNDARY_RADIUS_UV);
        m_labelQuickEditGroupId = createControl(0, L"STATIC", L"Group Filter", SS_LEFT, IDC_LABEL_QUICK_EDIT_GROUP_ID);
        m_editQuickEditGroupId = createControl(WS_EX_CLIENTEDGE, L"EDIT", L"", ES_AUTOHSCROLL, IDC_EDIT_QUICK_EDIT_GROUP_ID);
        m_checkQuickEditGuidesOnly = createControl(0, L"BUTTON", L"Guides Only", BS_AUTOCHECKBOX, IDC_CHECK_QUICK_EDIT_GUIDES_ONLY);
        m_labelQuickEditVisualization = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_QUICK_EDIT_VISUALIZATION);
        m_btnQuickEditReplace = createControl(0, L"BUTTON", L"Apply to This Layer", BS_PUSHBUTTON, IDC_BTN_QUICK_EDIT_REPLACE);
        m_btnQuickEditAppend = createControl(0, L"BUTTON", L"Add New Layer", BS_PUSHBUTTON, IDC_BTN_QUICK_EDIT_APPEND);
        m_btnBrowsePartingMask = createControl(0, L"BUTTON", L"Browse Parting Mask...", BS_PUSHBUTTON, IDC_BTN_BROWSE_PARTING_MASK);
        m_btnBrowseRegionMask = createControl(0, L"BUTTON", L"Browse Region Mask...", BS_PUSHBUTTON, IDC_BTN_BROWSE_REGION_MASK);
        m_labelMaskSource = createControl(0, L"STATIC", L"Mask Source", SS_LEFT, IDC_LABEL_MASK_SOURCE);
        m_comboMaskSource = createControl(0, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL | CBS_HASSTRINGS, IDC_COMBO_MASK_SOURCE);
        SendMessageW(m_comboMaskSource, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Auto"));
        SendMessageW(m_comboMaskSource, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"External Files"));
        SendMessageW(m_comboMaskSource, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Procedural"));
        SendMessageW(m_comboMaskSource, CB_SETCURSEL, 0, 0);
        m_labelMaskOverlay = createControl(0, L"STATIC", L"Scalp Overlay", SS_LEFT, IDC_LABEL_SELF_RADIUS + 100);
        m_comboMaskOverlay = createControl(0, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL | CBS_HASSTRINGS, IDC_COMBO_MASK_OVERLAY);
        SendMessageW(m_comboMaskOverlay, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"None"));
        SendMessageW(m_comboMaskOverlay, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Parting Overlay"));
        SendMessageW(m_comboMaskOverlay, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Region Overlay"));
        SendMessageW(m_comboMaskOverlay, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Edit Influence"));
        SendMessageW(m_comboMaskOverlay, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Boundary Compare"));
        SendMessageW(m_comboMaskOverlay, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Selection / Risk"));
        SendMessageW(m_comboMaskOverlay, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Motion Score"));
        SendMessageW(m_comboMaskOverlay, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Contact Score"));
        SendMessageW(m_comboMaskOverlay, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Uncertainty Score"));
        SendMessageW(m_comboMaskOverlay, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Edit Score"));
        SendMessageW(m_comboMaskOverlay, CB_SETCURSEL, 0, 0);
        m_labelPartingMask = createControl(0, L"STATIC", L"Parting preview", SS_LEFT, IDC_LABEL_PARTING_MASK_NAME);
        m_previewPartingMask = createControl(WS_EX_CLIENTEDGE, L"STATIC", L"", SS_BITMAP | SS_CENTERIMAGE | SS_NOTIFY, IDC_PREVIEW_PARTING_MASK);
        m_labelRegionMask = createControl(0, L"STATIC", L"Region preview", SS_LEFT, IDC_LABEL_REGION_MASK_NAME);
        m_previewRegionMask = createControl(WS_EX_CLIENTEDGE, L"STATIC", L"", SS_BITMAP | SS_CENTERIMAGE | SS_NOTIFY, IDC_PREVIEW_REGION_MASK);

        createControl(0, L"STATIC", L"Guides", SS_LEFT, 0);
        m_editGuides = createControl(WS_EX_CLIENTEDGE, L"EDIT", L"", ES_AUTOHSCROLL | ES_NUMBER, IDC_EDIT_GUIDES);
        createControl(0, L"STATIC", L"Followers / guide", SS_LEFT, 0);
        m_editFollowers = createControl(WS_EX_CLIENTEDGE, L"EDIT", L"", ES_AUTOHSCROLL | ES_NUMBER, IDC_EDIT_FOLLOWERS);
        createControl(0, L"STATIC", L"Particles / strand", SS_LEFT, 0);
        m_editParticles = createControl(WS_EX_CLIENTEDGE, L"EDIT", L"", ES_AUTOHSCROLL | ES_NUMBER, IDC_EDIT_PARTICLES);

        createControl(0, L"STATIC", L"Render", SS_LEFT, 0);
        m_comboRenderMode = createControl(0, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL | CBS_HASSTRINGS, IDC_COMBO_RENDER_MODE);
        SendMessageW(m_comboRenderMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Particle Point Sprite"));
        SendMessageW(m_comboRenderMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Particle Billboard"));
        SendMessageW(m_comboRenderMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Strand Ribbon"));
        SendMessageW(m_comboRenderMode, CB_SETCURSEL, 2, 0);

        createControl(0, L"STATIC", L"Interaction", SS_LEFT, 0);
        m_comboInteractionMode = createControl(0, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL | CBS_HASSTRINGS, IDC_COMBO_INTERACTION_MODE);
        SendMessageW(m_comboInteractionMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"View Mode"));
        SendMessageW(m_comboInteractionMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Edit Mode"));
        SendMessageW(m_comboInteractionMode, CB_SETCURSEL, 1, 0);

        createControl(0, L"STATIC", L"Color Debug", SS_LEFT, 0);
        m_comboDebugColor = createControl(0, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL | CBS_HASSTRINGS, IDC_COMBO_DEBUG_COLOR);
        SendMessageW(m_comboDebugColor, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Natural"));
        SendMessageW(m_comboDebugColor, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Guide / Follower"));
        SendMessageW(m_comboDebugColor, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Width Heatmap"));
        SendMessageW(m_comboDebugColor, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Editable / Locked"));
        SendMessageW(m_comboDebugColor, CB_SETCURSEL, 0, 0);

        m_checkSelfCollision = createControl(0, L"BUTTON", L"Self Collision", BS_AUTOCHECKBOX, IDC_CHECK_SELF_COLLISION);
        m_checkGrid = createControl(0, L"BUTTON", L"Density Grid", BS_AUTOCHECKBOX, IDC_CHECK_GRID);
        m_checkScalpMotion = createControl(0, L"BUTTON", L"Scalp Motion (disabled)", BS_AUTOCHECKBOX, IDC_CHECK_SCALP_MOTION);
        m_checkShowColliders = createControl(0, L"BUTTON", L"Show Colliders", BS_AUTOCHECKBOX, IDC_CHECK_SHOW_COLLIDERS);
        m_checkShowScalpMesh = createControl(0, L"BUTTON", L"Show Scalp Mesh", BS_AUTOCHECKBOX, IDC_CHECK_SHOW_SCALP_MESH);
        m_checkShowScalp = createControl(0, L"BUTTON", L"Show Scalp Wireframe", BS_AUTOCHECKBOX, IDC_CHECK_SHOW_SCALP);
        m_checkShowGridBounds = createControl(0, L"BUTTON", L"Show Grid Bounds", BS_AUTOCHECKBOX, IDC_CHECK_SHOW_GRID_BOUNDS);
        m_checkShowFloor = createControl(0, L"BUTTON", L"Show Floor Guide", BS_AUTOCHECKBOX, IDC_CHECK_SHOW_FLOOR);
        m_checkShowGuides = createControl(0, L"BUTTON", L"Show Guide Skeletons", BS_AUTOCHECKBOX, IDC_CHECK_SHOW_GUIDES);
        m_checkShowFollowers = createControl(0, L"BUTTON", L"Show Follower Skeletons", BS_AUTOCHECKBOX, IDC_CHECK_SHOW_FOLLOWERS);
        m_checkShowRoots = createControl(0, L"BUTTON", L"Show Root Markers", BS_AUTOCHECKBOX, IDC_CHECK_SHOW_ROOTS);
        m_checkShowLinks = createControl(0, L"BUTTON", L"Show Follow Links", BS_AUTOCHECKBOX, IDC_CHECK_SHOW_LINKS);

        auto createSlider = [&](int id) -> HWND
        {
            HWND hwnd = createControl(0, TRACKBAR_CLASSW, L"", TBS_AUTOTICKS | TBS_HORZ, id);
            SendMessageW(hwnd, TBM_SETRANGEMIN, FALSE, 0);
            SendMessageW(hwnd, TBM_SETRANGEMAX, TRUE, 1000);
            return hwnd;
        };

        m_labelScalpCoverageStrength = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_SCALP_COVERAGE_STRENGTH);
        m_trackScalpCoverageStrength = createSlider(IDC_TRACK_SCALP_COVERAGE_STRENGTH);
        m_labelScalpPartWidth = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_SCALP_PART_WIDTH);
        m_trackScalpPartWidth = createSlider(IDC_TRACK_SCALP_PART_WIDTH);
        m_labelScalpDarkness = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_SCALP_DARKNESS);
        m_trackScalpDarkness = createSlider(IDC_TRACK_SCALP_DARKNESS);
        m_labelScalpHairBlend = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_SCALP_HAIR_BLEND);
        m_trackScalpHairBlend = createSlider(IDC_TRACK_SCALP_HAIR_BLEND);

        m_trackQuickEditFeather = createSlider(IDC_TRACK_QUICK_EDIT_FEATHER);
        m_trackQuickEditLengthScale = createSlider(IDC_TRACK_QUICK_EDIT_LENGTHSCALE);
        m_trackQuickEditLayerWeight = createSlider(IDC_TRACK_QUICK_EDIT_LAYERWEIGHT);
        m_trackQuickEditCenterU = createSlider(IDC_TRACK_QUICK_EDIT_CENTER_U);
        m_trackQuickEditCenterV = createSlider(IDC_TRACK_QUICK_EDIT_CENTER_V);
        m_trackQuickEditRadiusU = createSlider(IDC_TRACK_QUICK_EDIT_RADIUS_U);
        m_trackQuickEditRadiusV = createSlider(IDC_TRACK_QUICK_EDIT_RADIUS_V);
        m_trackQuickEditInnerRadius = createSlider(IDC_TRACK_QUICK_EDIT_INNER_RADIUS);
        m_trackQuickEditAngleCenter = createSlider(IDC_TRACK_QUICK_EDIT_ANGLE_CENTER);
        m_trackQuickEditAngleHalfExtent = createSlider(IDC_TRACK_QUICK_EDIT_ANGLE_HALF_EXTENT);
        m_trackQuickEditRootOffsetX = createSlider(IDC_TRACK_QUICK_EDIT_ROOT_OFFSET_X);
        m_trackQuickEditRootOffsetY = createSlider(IDC_TRACK_QUICK_EDIT_ROOT_OFFSET_Y);
        m_trackQuickEditRootOffsetZ = createSlider(IDC_TRACK_QUICK_EDIT_ROOT_OFFSET_Z);
        m_trackQuickEditTipOffsetX = createSlider(IDC_TRACK_QUICK_EDIT_TIP_OFFSET_X);
        m_trackQuickEditTipOffsetY = createSlider(IDC_TRACK_QUICK_EDIT_TIP_OFFSET_Y);
        m_trackQuickEditTipOffsetZ = createSlider(IDC_TRACK_QUICK_EDIT_TIP_OFFSET_Z);
        m_trackQuickEditSideOffset = createSlider(IDC_TRACK_QUICK_EDIT_SIDE_OFFSET);
        m_trackQuickEditYaw = createSlider(IDC_TRACK_QUICK_EDIT_YAW);
        m_trackQuickEditWidthScale = createSlider(IDC_TRACK_QUICK_EDIT_WIDTHSCALE);
        m_trackQuickEditFollowerInfluence = createSlider(IDC_TRACK_QUICK_EDIT_FOLLOWER_INFLUENCE);
        m_trackQuickEditBoundarySmoothing = createSlider(IDC_TRACK_QUICK_EDIT_BOUNDARY_SMOOTHING);
        m_trackQuickEditBoundaryRadiusUv = createSlider(IDC_TRACK_QUICK_EDIT_BOUNDARY_RADIUS_UV);
        m_labelWidth = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_WIDTH);
        m_trackWidth = createSlider(IDC_TRACK_WIDTH);
        m_labelGravity = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_GRAVITY);
        m_trackGravity = createSlider(IDC_TRACK_GRAVITY);
        m_labelDamping = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_DAMPING);
        m_trackDamping = createSlider(IDC_TRACK_DAMPING);
        m_labelWind = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_WIND);
        m_trackWind = createSlider(IDC_TRACK_WIND);
        m_labelIterations = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_ITERATIONS);
        m_trackIterations = createSlider(IDC_TRACK_ITERATIONS);
        m_labelFollow = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_FOLLOW);
        m_trackFollow = createSlider(IDC_TRACK_FOLLOW);
        m_labelSelfRadius = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_SELF_RADIUS);
        m_trackSelfRadius = createSlider(IDC_TRACK_SELF_RADIUS);
        m_labelSelfStiffness = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_SELF_STIFFNESS);
        m_trackSelfStiffness = createSlider(IDC_TRACK_SELF_STIFFNESS);
        m_labelGridCellSize = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_GRID_CELL);
        m_trackGridCellSize = createSlider(IDC_TRACK_GRID_CELL);
        m_labelStretchCompliance = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_STRETCH);
        m_trackStretchCompliance = createSlider(IDC_TRACK_STRETCH);
        m_labelBendCompliance = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_BEND);
        m_trackBendCompliance = createSlider(IDC_TRACK_BEND);
        m_labelActiveSetTuning = createControl(0, L"STATIC", L"Active-Set Runtime Tuning", SS_LEFT, IDC_LABEL_ACTIVESET_TUNING);
        m_checkActiveSetAdaptiveBudget = createControl(0, L"BUTTON", L"Adaptive Budget", BS_AUTOCHECKBOX, IDC_CHECK_ACTIVESET_ADAPTIVE_BUDGET);
        m_checkActiveSetHardClamp = createControl(0, L"BUTTON", L"Hard Budget Clamp", BS_AUTOCHECKBOX, IDC_CHECK_ACTIVESET_HARD_CLAMP);
        m_labelActiveSetMaxClusters = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_ACTIVESET_MAX_CLUSTERS);
        m_trackActiveSetMaxClusters = createSlider(IDC_TRACK_ACTIVESET_MAX_CLUSTERS);
        m_labelActiveSetMaxGuides = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_ACTIVESET_MAX_GUIDES);
        m_trackActiveSetMaxGuides = createSlider(IDC_TRACK_ACTIVESET_MAX_GUIDES);
        m_labelActiveSetThreshold = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_ACTIVESET_THRESHOLD);
        m_trackActiveSetThreshold = createSlider(IDC_TRACK_ACTIVESET_THRESHOLD);
        m_labelActiveSetTargetFrame = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_ACTIVESET_TARGET_FRAME);
        m_trackActiveSetTargetFrame = createSlider(IDC_TRACK_ACTIVESET_TARGET_FRAME);
        m_labelActiveSetFixedCost = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_ACTIVESET_FIXED_COST);
        m_trackActiveSetFixedCost = createSlider(IDC_TRACK_ACTIVESET_FIXED_COST);
        m_labelActiveSetClusterCost = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_ACTIVESET_CLUSTER_COST);
        m_trackActiveSetClusterCost = createSlider(IDC_TRACK_ACTIVESET_CLUSTER_COST);
        m_labelActiveSetScoreWeights = createControl(0, L"STATIC", L"Active-Set Score Weights", SS_LEFT, IDC_LABEL_ACTIVESET_SCORE_WEIGHTS);
        m_labelActiveSetMotionWeight = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_ACTIVESET_MOTION_WEIGHT);
        m_trackActiveSetMotionWeight = createSlider(IDC_TRACK_ACTIVESET_MOTION_WEIGHT);
        m_labelActiveSetContactWeight = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_ACTIVESET_CONTACT_WEIGHT);
        m_trackActiveSetContactWeight = createSlider(IDC_TRACK_ACTIVESET_CONTACT_WEIGHT);
        m_labelActiveSetUncertaintyWeight = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_ACTIVESET_UNCERTAINTY_WEIGHT);
        m_trackActiveSetUncertaintyWeight = createSlider(IDC_TRACK_ACTIVESET_UNCERTAINTY_WEIGHT);
        m_labelActiveSetEditWeight = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_ACTIVESET_EDIT_WEIGHT);
        m_trackActiveSetEditWeight = createSlider(IDC_TRACK_ACTIVESET_EDIT_WEIGHT);
        m_btnExportActiveSetJson = createControl(0, L"BUTTON", L"Export Active-Set JSON", BS_PUSHBUTTON, IDC_BTN_EXPORT_ACTIVESET_JSON);
        m_btnExportRuntimeReport = createControl(0, L"BUTTON", L"Export Runtime Report", BS_PUSHBUTTON, IDC_BTN_EXPORT_RUNTIME_REPORT);

        m_labelStats = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_STATS);
        m_labelAssetStats = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_ASSET_STATS);
        m_labelEditWorkflow = createControl(WS_EX_CLIENTEDGE, L"STATIC", L"", SS_LEFT, 0);
        m_labelEditLegend = createControl(WS_EX_CLIENTEDGE, L"STATIC", L"", SS_LEFT, 0);

        m_btnWorkspaceGroom = createControl(0, L"BUTTON", L"Groom", BS_PUSHBUTTON, IDC_BTN_WORKSPACE_GROOM);
        m_btnWorkspaceSim = createControl(0, L"BUTTON", L"Sim", BS_PUSHBUTTON, IDC_BTN_WORKSPACE_SIM);
        m_btnWorkspaceReview = createControl(0, L"BUTTON", L"Review", BS_PUSHBUTTON, IDC_BTN_WORKSPACE_REVIEW);
        m_btnWorkspaceReport = createControl(0, L"BUTTON", L"Report", BS_PUSHBUTTON, IDC_BTN_WORKSPACE_REPORT);
        m_btnToolSelect = createControl(0, L"BUTTON", L"Select", BS_PUSHBUTTON, IDC_BTN_TOOL_SELECT);
        m_btnToolComb = createControl(0, L"BUTTON", L"Comb", BS_PUSHBUTTON, IDC_BTN_TOOL_COMB);
        m_btnToolGrab = createControl(0, L"BUTTON", L"Grab", BS_PUSHBUTTON, IDC_BTN_TOOL_GRAB);
        m_btnToolSmooth = createControl(0, L"BUTTON", L"Smooth", BS_PUSHBUTTON, IDC_BTN_TOOL_SMOOTH);
        m_btnToolLength = createControl(0, L"BUTTON", L"Length", BS_PUSHBUTTON, IDC_BTN_TOOL_LENGTH);
        m_btnToolVolume = createControl(0, L"BUTTON", L"Volume", BS_PUSHBUTTON, IDC_BTN_TOOL_VOLUME);
        m_btnToolPart = createControl(0, L"BUTTON", L"Part", BS_PUSHBUTTON, IDC_BTN_TOOL_PART);
        m_btnToolFreeze = createControl(0, L"BUTTON", L"Freeze", BS_PUSHBUTTON, IDC_BTN_TOOL_FREEZE);
        m_btnToolStrandFix = createControl(0, L"BUTTON", L"Strand Fix", BS_PUSHBUTTON, IDC_BTN_TOOL_STRAND_FIX);
        m_btnUndoEdit = createControl(0, L"BUTTON", L"Undo", BS_PUSHBUTTON, IDC_BTN_UNDO_EDIT);
        m_btnRedoEdit = createControl(0, L"BUTTON", L"Redo", BS_PUSHBUTTON, IDC_BTN_REDO_EDIT);
        m_btnTrayLayers = createControl(0, L"BUTTON", L"Layers", BS_PUSHBUTTON, IDC_BTN_TRAY_LAYERS);
        m_btnTrayHistory = createControl(0, L"BUTTON", L"History", BS_PUSHBUTTON, IDC_BTN_TRAY_HISTORY);
        m_btnTrayMetrics = createControl(0, L"BUTTON", L"Metrics", BS_PUSHBUTTON, IDC_BTN_TRAY_METRICS);
        m_btnToggleInspector = createControl(0, L"BUTTON", L"Inspector", BS_PUSHBUTTON, IDC_BTN_TOGGLE_INSPECTOR);
        m_btnToggleTray = createControl(0, L"BUTTON", L"Layers", BS_PUSHBUTTON, IDC_BTN_TOGGLE_TRAY);
        m_btnToggleAdvanced = createControl(0, L"BUTTON", L"Advanced", BS_PUSHBUTTON, IDC_BTN_TOGGLE_ADVANCED);
        m_btnFastViewport = createControl(0, L"BUTTON", L"Fast View", BS_PUSHBUTTON, IDC_BTN_FAST_VIEWPORT);
        m_labelWorkspaceTitle = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_WORKSPACE_TITLE);
        m_labelInspectorTitle = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_INSPECTOR_TITLE);
        m_labelTrayTitle = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_TRAY_TITLE);
        m_labelToolHint = createControl(WS_EX_CLIENTEDGE, L"STATIC", L"", SS_LEFT, IDC_LABEL_TOOL_HINT);
        m_labelSessionStatus = createControl(0, L"STATIC", L"", SS_LEFT, IDC_LABEL_SESSION_STATUS);

        return m_comboPreset && m_comboRenderMode && m_comboInteractionMode && m_comboDebugColor && m_comboMaskSource && m_comboMaskOverlay && m_previewPartingMask && m_previewRegionMask && m_btnPlayPause && m_btnStep && m_btnImportAsset && m_btnBrowseEditStack && m_btnClearEditStack && m_labelEditStack && m_labelInlineEditStack && m_editInlineEditStack && m_btnApplyInlineEditStack && m_btnSaveInlineEditStack && m_btnResetInlineEditStack && m_labelQuickEditForm && m_labelLayerList && m_listInlineLayers && m_labelQuickEditName && m_editQuickEditName && m_btnQuickEditMoveUp && m_btnQuickEditMoveDown && m_checkQuickEditEnabled && m_btnQuickEditDelete && m_btnQuickEditDuplicate && m_comboQuickEditMode && m_trackQuickEditFeather && m_trackQuickEditLengthScale && m_trackQuickEditLayerWeight && m_trackQuickEditCenterU && m_trackQuickEditCenterV && m_trackQuickEditRadiusU && m_trackQuickEditRadiusV && m_trackQuickEditInnerRadius && m_trackQuickEditAngleCenter && m_trackQuickEditAngleHalfExtent && m_trackQuickEditRootOffsetX && m_trackQuickEditRootOffsetY && m_trackQuickEditRootOffsetZ && m_trackQuickEditTipOffsetX && m_trackQuickEditTipOffsetY && m_trackQuickEditTipOffsetZ && m_trackQuickEditSideOffset && m_trackQuickEditYaw && m_trackQuickEditWidthScale && m_trackQuickEditFollowerInfluence && m_trackQuickEditBoundarySmoothing && m_trackQuickEditBoundaryRadiusUv && m_editQuickEditGroupId && m_checkQuickEditGuidesOnly && m_labelQuickEditLayerWeight && m_labelQuickEditCenterU && m_labelQuickEditCenterV && m_labelQuickEditRadiusU && m_labelQuickEditRadiusV && m_labelQuickEditInnerRadius && m_labelQuickEditAngleCenter && m_labelQuickEditAngleHalfExtent && m_labelQuickEditRootOffsetX && m_labelQuickEditRootOffsetY && m_labelQuickEditRootOffsetZ && m_labelQuickEditTipOffsetX && m_labelQuickEditTipOffsetY && m_labelQuickEditTipOffsetZ && m_labelQuickEditSideOffset && m_labelQuickEditYaw && m_labelQuickEditWidthScale && m_labelQuickEditFollowerInfluence && m_labelQuickEditBoundarySmoothing && m_labelQuickEditBoundaryRadiusUv && m_labelQuickEditGroupId && m_labelQuickEditVisualization && m_btnQuickEditReplace && m_btnQuickEditAppend && m_trackWidth && m_labelActiveSetTuning && m_checkActiveSetAdaptiveBudget && m_checkActiveSetHardClamp && m_labelActiveSetMaxClusters && m_trackActiveSetMaxClusters && m_labelActiveSetMaxGuides && m_trackActiveSetMaxGuides && m_labelActiveSetThreshold && m_trackActiveSetThreshold && m_labelActiveSetTargetFrame && m_trackActiveSetTargetFrame && m_labelActiveSetFixedCost && m_trackActiveSetFixedCost && m_labelActiveSetClusterCost && m_trackActiveSetClusterCost && m_labelActiveSetScoreWeights && m_labelActiveSetMotionWeight && m_trackActiveSetMotionWeight && m_labelActiveSetContactWeight && m_trackActiveSetContactWeight && m_labelActiveSetUncertaintyWeight && m_trackActiveSetUncertaintyWeight && m_labelActiveSetEditWeight && m_trackActiveSetEditWeight && m_btnExportActiveSetJson && m_btnExportRuntimeReport && m_labelStats && m_labelAssetStats && m_labelEditWorkflow && m_labelEditLegend && m_checkShowScalpMesh && m_labelScalpCoverageStrength && m_trackScalpCoverageStrength && m_labelScalpPartWidth && m_trackScalpPartWidth && m_labelScalpDarkness && m_trackScalpDarkness && m_labelScalpHairBlend && m_trackScalpHairBlend && m_btnWorkspaceGroom && m_btnWorkspaceSim && m_btnWorkspaceReview && m_btnWorkspaceReport && m_btnToolSelect && m_btnToolComb && m_btnToolGrab && m_btnToolSmooth && m_btnToolLength && m_btnToolVolume && m_btnToolPart && m_btnToolFreeze && m_btnToolStrandFix && m_btnUndoEdit && m_btnRedoEdit && m_btnTrayLayers && m_btnTrayHistory && m_btnTrayMetrics && m_btnToggleInspector && m_btnToggleTray && m_btnToggleAdvanced && m_btnFastViewport && m_labelWorkspaceTitle && m_labelInspectorTitle && m_labelTrayTitle && m_labelToolHint && m_labelSessionStatus;
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
        EnforceFixedWorkload();

        const float initialSegmentLength = SegmentLengthForParticleCount(m_builderParticlesPerStrand);

        // HAIRGPU_FORMAL_ASSET_BINDING_V1: a formal run may never fall back to a generated/cache asset.
        if (m_experimentCase.enabled)
        {
            const std::wstring formalAssetPathText = GetEnvironmentVariableString(L"HAIRGPU_FORMAL_ASSET_PATH");
            if (formalAssetPathText.empty())
            {
                ShowStartupDialog(nullptr, L"Hair GPU Viewer Formal Asset Failure",
                    L"HAIRGPU_FORMAL_ASSET_PATH is missing. Formal runs cannot use the interactive asset cache.");
                return false;
            }

            m_experimentCase.formalAssetPath = formalAssetPathText;
            m_experimentCase.formalAssetSha256 = GetEnvironmentVariableString(L"HAIRGPU_FORMAL_ASSET_SHA256");
            m_experimentCase.formalTopologySha256 = GetEnvironmentVariableString(L"HAIRGPU_FORMAL_TOPOLOGY_SHA256");
            m_experimentCase.generatorSettingsSha256 = GetEnvironmentVariableString(L"HAIRGPU_GENERATOR_SETTINGS_SHA256");
            if (m_experimentCase.formalAssetSha256.empty() ||
                m_experimentCase.formalTopologySha256.empty() ||
                m_experimentCase.generatorSettingsSha256.empty())
            {
                ShowStartupDialog(nullptr, L"Hair GPU Viewer Formal Asset Failure",
                    L"Formal asset, topology, and generator-settings hashes are required.");
                return false;
            }

            const std::filesystem::path formalAssetPath(formalAssetPathText);
            std::error_code formalAssetFileError;
            hairgpu::HairAsset formalAsset = {};
            std::string formalAssetError;
            if (!std::filesystem::is_regular_file(formalAssetPath, formalAssetFileError) || formalAssetFileError ||
                !hairgpu::LoadHairAssetFromHairBinary(formalAssetPath, formalAsset, formalAssetError))
            {
                const std::wstring details = L"Failed to load the frozen formal asset: " + formalAssetPath.wstring() +
                    L"\r\n" + Utf8ToWide(formalAssetError);
                ShowStartupDialog(nullptr, L"Hair GPU Viewer Formal Asset Failure", details);
                return false;
            }

            const uint64_t expectedFormalStrands =
                static_cast<uint64_t>(kFixedGuideCount) *
                static_cast<uint64_t>(kFixedFollowersPerGuide + 1u);
            const uint64_t expectedFormalPoints =
                expectedFormalStrands * static_cast<uint64_t>(kFixedParticlesPerStrand);
            const bool formalShapeValid =
                formalAsset.guideStrandCount == kFixedGuideCount &&
                formalAsset.followerStrandCount == kFixedGuideCount * kFixedFollowersPerGuide &&
                formalAsset.particlesPerStrand == kFixedParticlesPerStrand &&
                formalAsset.strands.size() == expectedFormalStrands &&
                formalAsset.restPoints.size() == expectedFormalPoints &&
                formalAsset.pointWidths.size() == expectedFormalPoints &&
                formalAsset.rootBindings.size() == expectedFormalStrands &&
                formalAsset.followData.size() == expectedFormalStrands;
            if (!formalShapeValid)
            {
                ShowStartupDialog(nullptr, L"Hair GPU Viewer Formal Asset Failure",
                    L"The frozen asset does not satisfy 4000 guides / 24 followers / 128 points / 100000 displayed strands.");
                return false;
            }

            std::error_code canonicalError;
            const std::filesystem::path observedPath = std::filesystem::weakly_canonical(formalAssetPath, canonicalError);
            m_asset = std::move(formalAsset);
            m_experimentCase.observedAssetPath = canonicalError ? formalAssetPath.wstring() : observedPath.wstring();
            m_experimentCase.formalAssetLoaded = true;
            m_lastAssetSource = L"Frozen Formal Asset";
            m_lastImportStatus = L"Frozen formal asset loaded and topology-count validated";
        }
        else
        {
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
        }
        m_activeSetBudget.maxActiveClusters = 6u;
        m_activeSetBudget.maxActiveGuides = 256u;
        m_activeSetBudget.promoteScoreThreshold = 0.52f;
        m_simConfig.solverIterations = kFixedSolverIterations;
        m_simConfig.fixedTimeStep = kFixedTimeStepSeconds;
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
            m_vsyncEnabled = m_experimentCase.vsyncEnabled;
            m_presentSyncInterval = m_vsyncEnabled
                ? std::clamp<UINT>(m_experimentCase.presentSyncInterval, 1u, 4u)
                : 0u;
        }
        EnforceFixedWorkload();

        m_colliders.spheres.push_back({ { 0.0f, 1.66f, 0.0f }, 0.215f });
        m_colliders.capsules.push_back({ { -0.19f, 1.36f, 0.02f }, 0.11f, { -0.42f, 1.15f, 0.04f }, 0.08f });
        m_colliders.capsules.push_back({ {  0.19f, 1.36f, 0.02f }, 0.11f, {  0.42f, 1.15f, 0.04f }, 0.08f });
        m_colliders.capsules.push_back({ {  0.00f, 1.44f, -0.08f }, 0.14f, {  0.00f, 0.92f, -0.10f }, 0.16f });

        if (!CreateWindowAndDevice(instance, showCommand))
        {
            return false;
        }

        // Capture adapter identity from DXGI. This is the authoritative evidence
        // for Formal Runs; nvidia-smi is only an optional thermal/clock sampler.
        bool adapterIdentityAvailable = false;
        if (m_device)
        {
            ComPtr<IDXGIDevice> dxgiDevice;
            ComPtr<IDXGIAdapter> adapter;
            DXGI_ADAPTER_DESC adapterDesc = {};
            if (SUCCEEDED(m_device.As(&dxgiDevice)) &&
                SUCCEEDED(dxgiDevice->GetAdapter(&adapter)) && adapter &&
                SUCCEEDED(adapter->GetDesc(&adapterDesc)))
            {
                m_observedGpuName = adapterDesc.Description;
                m_observedGpuVendorId = adapterDesc.VendorId;
                m_observedGpuDeviceId = adapterDesc.DeviceId;
                m_observedDedicatedVideoMemory =
                    static_cast<uint64_t>(adapterDesc.DedicatedVideoMemory);
                m_experimentCase.observedGpuName = m_observedGpuName;
                adapterIdentityAvailable =
                    adapterDesc.VendorId != 0u && adapterDesc.DeviceId != 0u &&
                    adapterDesc.DedicatedVideoMemory > 0u;
            }
        }
        if (adapterIdentityAvailable)
        {
            m_noveltyTelemetry.availabilityMask |=
                hairgpu::HairTelemetryAvailability_AdapterIdentity;
            m_noveltyTelemetry.metricAvailabilityMask |=
                hairgpu::HairTelemetryAvailability_AdapterIdentity;
        }
        else if (m_experimentCase.enabled && m_experimentCase.hardwareAdapterRequired)
        {
            ShowStartupDialog(m_hwnd, L"Hair GPU Viewer Formal Run Failure",
                L"DXGI adapter identity or dedicated VRAM is unavailable.");
            Shutdown();
            return false;
        }
        if (m_experimentCase.enabled && m_experimentCase.hardwareAdapterRequired &&
            m_observedDriverType != D3D_DRIVER_TYPE_HARDWARE)
        {
            ShowStartupDialog(m_hwnd, L"Hair GPU Viewer Formal Run Failure",
                L"Formal runs require a hardware D3D11 adapter; WARP/reference is rejected.");
            Shutdown();
            return false;
        }

        if (!CreateUiControls())
        {
            ShowStartupDialog(m_hwnd, L"Hair GPU Viewer Startup Failure", L"CreateUiControls() failed.");
            Shutdown();
            return false;
        }

