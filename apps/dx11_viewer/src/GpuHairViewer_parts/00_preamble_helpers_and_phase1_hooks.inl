// GpuHairViewer.cpp ordered part 00: preamble_helpers_and_phase1_hooks
// This file is included by ../GpuHairViewer.cpp; do not compile it directly.

namespace hairgpu::viewer
{
    namespace
    {
        constexpr wchar_t kWindowClassName[] = L"HairGpuViewerWindow";
        constexpr int kUiPanelWidth = 500;
        constexpr int kUiMargin = 12;
        constexpr float kTargetAverageChainLengthMeters = 0.50f;

        float SegmentLengthForParticleCount(uint32_t particlesPerStrand)
        {
            const uint32_t safeParticles = std::max<uint32_t>(particlesPerStrand, 2u);
            const float segmentCount = static_cast<float>(std::max<uint32_t>(safeParticles - 1u, 1u));
            return std::max(kTargetAverageChainLengthMeters / segmentCount, 0.0015f);
        }

        struct PreviewMaskImage
        {
            uint32_t width = 0;
            uint32_t height = 0;
            uint32_t channels = 0;
            std::vector<unsigned char> samples;
            bool valid() const { return width > 0 && height > 0 && channels > 0 && samples.size() == static_cast<size_t>(width) * height * channels; }
        };

        bool EndsWithInsensitiveW(const std::wstring& s, const wchar_t* ext)
        {
            std::wstring a = s; std::wstring b = ext;
            std::transform(a.begin(), a.end(), a.begin(), ::towlower);
            std::transform(b.begin(), b.end(), b.begin(), ::towlower);
            return a.size() >= b.size() && a.substr(a.size() - b.size()) == b;
        }

        bool ReadPortableAnyMapToken(std::istream& input, std::string& out)
        {
            out.clear();
            while (input)
            {
                int c = input.peek();
                if (c == '#') { std::string skip; std::getline(input, skip); continue; }
                if (c != EOF && std::isspace(static_cast<unsigned char>(c))) { input.get(); continue; }
                break;
            }
            while (input)
            {
                int c = input.peek();
                if (c == EOF || std::isspace(static_cast<unsigned char>(c)) || c == '#') break;
                out.push_back(static_cast<char>(input.get()));
            }
            return !out.empty();
        }

        bool LoadCsvPreviewMask(const std::filesystem::path& path, uint32_t expectedChannels, PreviewMaskImage& outImage)
        {
            std::ifstream in(path); if (!in) return false;
            std::vector<std::vector<float>> rows; std::string line; size_t cols = 0;
            while (std::getline(in, line))
            {
                if (line.empty()) continue; std::stringstream ss(line); std::string item; std::vector<float> vals;
                while (std::getline(ss, item, ',')) { if (!item.empty()) vals.push_back(std::stof(item)); }
                if (!vals.empty()) { cols = std::max(cols, vals.size()); rows.push_back(std::move(vals)); }
            }
            if (rows.empty() || cols == 0 || (cols % expectedChannels) != 0) return false;
            outImage.height = static_cast<uint32_t>(rows.size()); outImage.width = static_cast<uint32_t>(cols / expectedChannels); outImage.channels = expectedChannels;
            outImage.samples.assign(static_cast<size_t>(outImage.width) * outImage.height * outImage.channels, 0);
            for (uint32_t y=0;y<outImage.height;++y)
                for (uint32_t x=0;x<outImage.width;++x)
                    for (uint32_t c=0;c<outImage.channels;++c)
                    {
                        float v = (x*expectedChannels + c < rows[y].size()) ? rows[y][x*expectedChannels+c] : 0.0f;
                        v = std::clamp(v, 0.0f, 1.0f);
                        outImage.samples[(static_cast<size_t>(y)*outImage.width + x)*outImage.channels + c] = static_cast<unsigned char>(v * 255.0f + 0.5f);
                    }
            return outImage.valid();
        }

        bool LoadPnmPreviewMask(const std::filesystem::path& path, uint32_t expectedChannels, PreviewMaskImage& outImage)
        {
            std::ifstream in(path, std::ios::binary); if (!in) return false; std::string tok; if (!ReadPortableAnyMapToken(in, tok)) return false;
            const bool gray = tok == "P5" || tok == "P2"; const bool color = tok == "P6" || tok == "P3"; const bool binary = tok == "P5" || tok == "P6";
            if ((!gray && !color) || (gray ? 1u : 3u) != expectedChannels) return false;
            std::string w,h,m; if(!ReadPortableAnyMapToken(in,w)||!ReadPortableAnyMapToken(in,h)||!ReadPortableAnyMapToken(in,m)) return false;
            outImage.width=std::stoul(w); outImage.height=std::stoul(h); outImage.channels=expectedChannels; outImage.samples.assign(static_cast<size_t>(outImage.width)*outImage.height*outImage.channels,0);
            int maxv = std::stoi(m); if (maxv<=0) return false;
            if (binary)
            {
                in.get();
                in.read(reinterpret_cast<char*>(outImage.samples.data()), static_cast<std::streamsize>(outImage.samples.size()));
                if (!in) return false;
                if (maxv != 255) for (auto& s : outImage.samples) s = static_cast<unsigned char>((int(s) * 255) / maxv);
            }
            else
            {
                for (auto& s : outImage.samples) { std::string v; if(!ReadPortableAnyMapToken(in,v)) return false; s = static_cast<unsigned char>((std::stoi(v) * 255) / maxv); }
            }
            return outImage.valid();
        }

        bool LoadBmpPreviewMask(const std::filesystem::path& path, uint32_t expectedChannels, PreviewMaskImage& outImage)
        {
            std::ifstream in(path, std::ios::binary); if (!in) return false; std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)), {}); if (bytes.size() < 54 || bytes[0] != 'B' || bytes[1] != 'M') return false;
            auto r32=[&](size_t o){return uint32_t(bytes[o]) | (uint32_t(bytes[o+1])<<8) | (uint32_t(bytes[o+2])<<16) | (uint32_t(bytes[o+3])<<24);};
            auto r16=[&](size_t o){return uint16_t(bytes[o]) | (uint16_t(bytes[o+1])<<8);};
            uint32_t off=r32(10), dib=r32(14), w=r32(18), h=r32(22); uint16_t bpp=r16(28); uint32_t comp=r32(30); if (dib < 40 || comp != 0 || (bpp != 24 && bpp != 32)) return false;
            outImage.width=w; outImage.height=h; outImage.channels=expectedChannels; outImage.samples.assign(static_cast<size_t>(w)*h*expectedChannels,0); size_t stride=((size_t(w)*(bpp/8))+3)&~size_t(3); if (off + stride*size_t(h) > bytes.size()) return false;
            for(uint32_t y=0;y<h;++y){ const unsigned char* row=bytes.data()+off+stride*(h-1-y); for(uint32_t x=0;x<w;++x){ const unsigned char* px=row + x*(bpp/8); if(expectedChannels==1){ outImage.samples[y*w + x]=static_cast<unsigned char>((uint32_t(px[0])+px[1]+px[2])/3); } else { size_t i=(size_t(y)*w+x)*3; outImage.samples[i+0]=px[2]; outImage.samples[i+1]=px[1]; outImage.samples[i+2]=px[0]; } } }
            return outImage.valid();
        }

        bool LoadPreviewMaskAny(const std::wstring& path, uint32_t expectedChannels, PreviewMaskImage& outImage)
        {
            const std::filesystem::path p(path);
            if (!std::filesystem::exists(p)) return false;
            if (EndsWithInsensitiveW(path, L".csv")) return LoadCsvPreviewMask(p, expectedChannels, outImage);
            if (EndsWithInsensitiveW(path, L".pgm") || EndsWithInsensitiveW(path, L".ppm")) return LoadPnmPreviewMask(p, expectedChannels, outImage);
            if (EndsWithInsensitiveW(path, L".bmp")) return LoadBmpPreviewMask(p, expectedChannels, outImage);
            return false;
        }

        std::wstring ResolveViewerDefaultMaskPath(const wchar_t* baseName)
        {
            wchar_t modulePath[MAX_PATH] = {};
            GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
            std::filesystem::path exeDir = std::filesystem::path(modulePath).parent_path();
            std::vector<std::filesystem::path> roots = { std::filesystem::current_path(), exeDir, exeDir.parent_path(), exeDir.parent_path().parent_path(), exeDir.parent_path().parent_path().parent_path() };
            const wchar_t* exts[] = { L".bmp", L".pgm", L".ppm", L".csv" };
            for (const auto& root : roots)
            {
                for (const wchar_t* ext : exts)
                {
                    std::filesystem::path p = root / L"docs" / L"sample_assets" / (std::wstring(baseName) + ext);
                    if (std::filesystem::exists(p)) return p.wstring();
                    p = root / L"sample_assets" / (std::wstring(baseName) + ext);
                    if (std::filesystem::exists(p)) return p.wstring();
                }
            }
            return L"";
        }


        std::wstring GetEnvironmentVariableString(const wchar_t* name)
        {
            const DWORD required = GetEnvironmentVariableW(name, nullptr, 0u);
            if (required == 0u)
            {
                return L"";
            }
            std::wstring value(required, L'\0');
            const DWORD written = GetEnvironmentVariableW(name, value.data(), required);
            value.resize((written > 0u) ? (written - 1u) : 0u);
            return value;
        }

        bool EnvironmentValueIsTrue(const std::wstring& value)
        {
            std::wstring normalized = value;
            std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
            return normalized == L"1" || normalized == L"true" || normalized == L"yes" || normalized == L"on";
        }

        uint32_t EnvironmentValueToUInt(const std::wstring& value, uint32_t fallback = 0u)
        {
            if (value.empty())
            {
                return fallback;
            }
            try
            {
                return static_cast<uint32_t>(std::stoul(value));
            }
            catch (...)
            {
                return fallback;
            }
        }

        std::string CsvEscapeUtf8(const std::string& value)
        {
            std::string out = "\"";
            for (char ch : value)
            {
                if (ch == '"')
                {
                    out += "\"\"";
                }
                else
                {
                    out.push_back(ch);
                }
            }
            out += "\"";
            return out;
        }

        std::string JsonEscape(const std::string& value)
        {
            std::ostringstream out;
            for (unsigned char ch : value)
            {
                switch (ch)
                {
                case '\\': out << "\\\\"; break;
                case '"': out << "\\\""; break;
                case '\n': out << "\\n"; break;
                case '\r': out << "\\r"; break;
                case '\t': out << "\\t"; break;
                default:
                    if (ch < 0x20u)
                    {
                        out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(ch) << std::dec;
                    }
                    else
                    {
                        out << static_cast<char>(ch);
                    }
                    break;
                }
            }
            return out.str();
        }

        float ComputePercentile(std::vector<float> values, float percentile)
        {
            if (values.empty())
            {
                return 0.0f;
            }
            std::sort(values.begin(), values.end());
            const float clamped = std::clamp(percentile, 0.0f, 100.0f);
            const float rank = (clamped / 100.0f) * static_cast<float>(values.size() - 1u);
            const size_t lo = static_cast<size_t>(std::floor(rank));
            const size_t hi = static_cast<size_t>(std::ceil(rank));
            if (lo == hi)
            {
                return values[lo];
            }
            const float t = rank - static_cast<float>(lo);
            return values[lo] * (1.0f - t) + values[hi] * t;
        }

        float ComputePercentile95(std::vector<float> values)
        {
            return ComputePercentile(std::move(values), 95.0f);
        }

        float ComputeAverage(const std::vector<float>& values)
        {
            if (values.empty())
            {
                return 0.0f;
            }
            const double sum = std::accumulate(values.begin(), values.end(), 0.0);
            return static_cast<float>(sum / static_cast<double>(values.size()));
        }

        HBITMAP CreatePreviewBitmapFromMask(const PreviewMaskImage& image, bool grayscale)
        {
            const int dstW = 96, dstH = 72;
            BITMAPINFO bmi = {}; bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); bmi.bmiHeader.biWidth = dstW; bmi.bmiHeader.biHeight = -dstH; bmi.bmiHeader.biPlanes = 1; bmi.bmiHeader.biBitCount = 32; bmi.bmiHeader.biCompression = BI_RGB;
            void* bits = nullptr; HDC hdc = GetDC(nullptr); HBITMAP bmp = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0); ReleaseDC(nullptr, hdc); if (!bmp || !bits) return bmp;
            auto* out = reinterpret_cast<unsigned int*>(bits);
            for (int y=0;y<dstH;++y) for (int x=0;x<dstW;++x)
            {
                uint32_t sx = std::min<uint32_t>(image.width - 1, static_cast<uint32_t>((float(x) / std::max(1, dstW-1)) * (image.width - 1)));
                uint32_t sy = std::min<uint32_t>(image.height - 1, static_cast<uint32_t>((float(y) / std::max(1, dstH-1)) * (image.height - 1)));
                unsigned char r=26,g=26,b=26;
                if (image.valid())
                {
                    if (grayscale || image.channels == 1) { unsigned char v = image.samples[sy*image.width + sx]; r=g=b=v; }
                    else { size_t i=(static_cast<size_t>(sy)*image.width+sx)*image.channels; r=image.samples[i+0]; g=image.samples[i+1]; b=image.samples[i+2]; }
                }
                out[y*dstW+x] = 0xFF000000u | (unsigned(r) << 16) | (unsigned(g) << 8) | unsigned(b);
            }
            return bmp;
        }

        float SamplePreviewMaskScalar(const PreviewMaskImage& image, float u, float v, uint32_t channel)
        {
            if (image.width == 0 || image.height == 0 || image.channels == 0 || image.samples.empty())
            {
                return 0.0f;
            }
            const uint32_t x = std::min<uint32_t>(static_cast<uint32_t>(hairgpu::Clamp(u, 0.0f, 1.0f) * static_cast<float>(std::max<int>(static_cast<int>(image.width) - 1, 0))), image.width - 1);
            const uint32_t y = std::min<uint32_t>(static_cast<uint32_t>(hairgpu::Clamp(v, 0.0f, 1.0f) * static_cast<float>(std::max<int>(static_cast<int>(image.height) - 1, 0))), image.height - 1);
            const uint32_t c = std::min<uint32_t>(channel, image.channels - 1);
            return static_cast<float>(image.samples[(y * image.width + x) * image.channels + c]) / 255.0f;
        }

        hairgpu::Vec3 ComputeScalpPreviewUv(const hairgpu::Vec3& p)
        {
            hairgpu::Vec3 q = p - hairgpu::Vec3(0.0f, 1.60f, 0.0f);
            const float len = std::max(hairgpu::Length(q), 1.0e-5f);
            q = q / len;
            const float u = 0.5f + std::atan2(q.z, q.x) / (2.0f * 3.1415926535f);
            const float v = hairgpu::Clamp(0.5f - q.y * 0.5f, 0.0f, 1.0f);
            return hairgpu::Vec3(u, v, 0.0f);
        }

        float ProceduralPartingPreview(float u, float v)
        {
            const float center = 0.58f;
            const float dist = std::fabs(u - center);
            const float line = hairgpu::Clamp(1.0f - dist / 0.06f, 0.0f, 1.0f);
            const float crown = hairgpu::Clamp(1.0f - std::fabs(v - 0.28f) / 0.20f, 0.0f, 1.0f);
            return line * (0.45f + 0.55f * crown);
        }

        hairgpu::Vec3 ProceduralRegionPreview(float u, float v)
        {
            const float crown = hairgpu::Clamp(1.0f - std::sqrt((u - 0.54f)*(u - 0.54f) + (v - 0.24f)*(v - 0.24f)) / 0.34f, 0.0f, 1.0f);
            const float temple = hairgpu::Clamp(1.0f - std::min(std::sqrt((u - 0.18f)*(u - 0.18f) + (v - 0.40f)*(v - 0.40f)), std::sqrt((u - 0.86f)*(u - 0.86f) + (v - 0.42f)*(v - 0.42f))) / 0.26f, 0.0f, 1.0f);
            const float nape = hairgpu::Clamp(1.0f - std::sqrt((u - 0.54f)*(u - 0.54f) + (v - 0.80f)*(v - 0.80f)) / 0.32f, 0.0f, 1.0f);
            return hairgpu::Vec3(crown, temple, nape);
        }

        enum : int
        {
            IDC_BTN_PLAYPAUSE = 1001,
            IDC_BTN_RESET,
            IDC_BTN_STEP,
            IDC_BTN_APPLY_ASSET,
            IDC_BTN_IMPORT_ASSET,
            IDC_BTN_SAVE_SETTINGS,
            IDC_BTN_LOAD_SETTINGS,
            IDC_BTN_EXPORT_CSV,
            IDC_BTN_EXPORT_PERF_CSV,
            IDC_BTN_BROWSE_EDIT_STACK,
            IDC_BTN_CLEAR_EDIT_STACK,
            IDC_BTN_APPLY_INLINE_EDIT_STACK,
            IDC_BTN_SAVE_INLINE_EDIT_STACK,
            IDC_BTN_RESET_INLINE_EDIT_STACK,
            IDC_EDIT_INLINE_EDIT_STACK,
            IDC_LABEL_INLINE_EDIT_STACK,
            IDC_LABEL_QUICK_EDIT_FORM,
            IDC_LABEL_INLINE_LAYER_LIST,
            IDC_LIST_INLINE_LAYERS,
            IDC_LABEL_QUICK_EDIT_NAME,
            IDC_EDIT_QUICK_EDIT_NAME,
            IDC_BTN_QUICK_EDIT_MOVE_UP,
            IDC_BTN_QUICK_EDIT_MOVE_DOWN,
            IDC_CHECK_QUICK_EDIT_ENABLED,
            IDC_BTN_QUICK_EDIT_DELETE,
            IDC_BTN_QUICK_EDIT_DUPLICATE,
            IDC_COMBO_QUICK_EDIT_MODE,
            IDC_TRACK_QUICK_EDIT_FEATHER,
            IDC_TRACK_QUICK_EDIT_LENGTHSCALE,
            IDC_TRACK_QUICK_EDIT_LAYERWEIGHT,
            IDC_TRACK_QUICK_EDIT_CENTER_U,
            IDC_TRACK_QUICK_EDIT_CENTER_V,
            IDC_TRACK_QUICK_EDIT_RADIUS_U,
            IDC_TRACK_QUICK_EDIT_RADIUS_V,
            IDC_TRACK_QUICK_EDIT_INNER_RADIUS,
            IDC_TRACK_QUICK_EDIT_ANGLE_CENTER,
            IDC_TRACK_QUICK_EDIT_ANGLE_HALF_EXTENT,
            IDC_TRACK_QUICK_EDIT_ROOT_OFFSET_X,
            IDC_TRACK_QUICK_EDIT_ROOT_OFFSET_Y,
            IDC_TRACK_QUICK_EDIT_ROOT_OFFSET_Z,
            IDC_TRACK_QUICK_EDIT_TIP_OFFSET_X,
            IDC_TRACK_QUICK_EDIT_TIP_OFFSET_Y,
            IDC_TRACK_QUICK_EDIT_TIP_OFFSET_Z,
            IDC_TRACK_QUICK_EDIT_SIDE_OFFSET,
            IDC_TRACK_QUICK_EDIT_YAW,
            IDC_TRACK_QUICK_EDIT_WIDTHSCALE,
            IDC_TRACK_QUICK_EDIT_FOLLOWER_INFLUENCE,
            IDC_TRACK_QUICK_EDIT_BOUNDARY_SMOOTHING,
            IDC_TRACK_QUICK_EDIT_BOUNDARY_RADIUS_UV,
            IDC_EDIT_QUICK_EDIT_GROUP_ID,
            IDC_CHECK_QUICK_EDIT_GUIDES_ONLY,
            IDC_LABEL_QUICK_EDIT_FEATHER,
            IDC_LABEL_QUICK_EDIT_LENGTHSCALE,
            IDC_LABEL_QUICK_EDIT_LAYERWEIGHT,
            IDC_LABEL_QUICK_EDIT_CENTER_U,
            IDC_LABEL_QUICK_EDIT_CENTER_V,
            IDC_LABEL_QUICK_EDIT_RADIUS_U,
            IDC_LABEL_QUICK_EDIT_RADIUS_V,
            IDC_LABEL_QUICK_EDIT_INNER_RADIUS,
            IDC_LABEL_QUICK_EDIT_ANGLE_CENTER,
            IDC_LABEL_QUICK_EDIT_ANGLE_HALF_EXTENT,
            IDC_LABEL_QUICK_EDIT_ROOT_OFFSET_X,
            IDC_LABEL_QUICK_EDIT_ROOT_OFFSET_Y,
            IDC_LABEL_QUICK_EDIT_ROOT_OFFSET_Z,
            IDC_LABEL_QUICK_EDIT_TIP_OFFSET_X,
            IDC_LABEL_QUICK_EDIT_TIP_OFFSET_Y,
            IDC_LABEL_QUICK_EDIT_TIP_OFFSET_Z,
            IDC_LABEL_QUICK_EDIT_SIDE_OFFSET,
            IDC_LABEL_QUICK_EDIT_YAW,
            IDC_LABEL_QUICK_EDIT_WIDTHSCALE,
            IDC_LABEL_QUICK_EDIT_FOLLOWER_INFLUENCE,
            IDC_LABEL_QUICK_EDIT_BOUNDARY_SMOOTHING,
            IDC_LABEL_QUICK_EDIT_BOUNDARY_RADIUS_UV,
            IDC_LABEL_QUICK_EDIT_GROUP_ID,
            IDC_LABEL_QUICK_EDIT_VISUALIZATION,
            IDC_BTN_QUICK_EDIT_REPLACE,
            IDC_BTN_QUICK_EDIT_APPEND,
            IDC_BTN_BROWSE_PARTING_MASK,
            IDC_BTN_BROWSE_REGION_MASK,
            IDC_COMBO_MASK_SOURCE,
            IDC_COMBO_MASK_OVERLAY,
            IDC_PREVIEW_PARTING_MASK,
            IDC_PREVIEW_REGION_MASK,
            IDC_COMBO_PRESET,
            IDC_COMBO_RENDER_MODE,
            IDC_COMBO_INTERACTION_MODE,
            IDC_COMBO_DEBUG_COLOR,
            IDC_CHECK_SELF_COLLISION,
            IDC_CHECK_GRID,
            IDC_CHECK_SCALP_MOTION,
            IDC_CHECK_SHOW_COLLIDERS,
            IDC_CHECK_SHOW_SCALP,
            IDC_CHECK_SHOW_SCALP_MESH,
            IDC_CHECK_SHOW_GRID_BOUNDS,
            IDC_CHECK_SHOW_FLOOR,
            IDC_CHECK_SHOW_GUIDES,
            IDC_CHECK_SHOW_FOLLOWERS,
            IDC_CHECK_SHOW_ROOTS,
            IDC_CHECK_SHOW_LINKS,
            IDC_CHECK_SHOW_TEST_MESH,
            IDC_EDIT_GUIDES,
            IDC_EDIT_FOLLOWERS,
            IDC_EDIT_PARTICLES,
            IDC_TRACK_WIDTH,
            IDC_TRACK_GRAVITY,
            IDC_TRACK_DAMPING,
            IDC_TRACK_WIND,
            IDC_TRACK_ITERATIONS,
            IDC_TRACK_FOLLOW,
            IDC_TRACK_SELF_RADIUS,
            IDC_TRACK_SELF_STIFFNESS,
            IDC_TRACK_GRID_CELL,
            IDC_TRACK_STRETCH,
            IDC_TRACK_BEND,
            IDC_LABEL_STATS,
            IDC_LABEL_ASSET_STATS,
            IDC_LABEL_WIDTH,
            IDC_LABEL_GRAVITY,
            IDC_LABEL_DAMPING,
            IDC_LABEL_WIND,
            IDC_LABEL_ITERATIONS,
            IDC_LABEL_FOLLOW,
            IDC_LABEL_SELF_RADIUS,
            IDC_LABEL_MASK_SOURCE,
            IDC_LABEL_PARTING_MASK_NAME,
            IDC_LABEL_REGION_MASK_NAME,
            IDC_LABEL_SELF_STIFFNESS,
            IDC_LABEL_GRID_CELL,
            IDC_LABEL_STRETCH,
            IDC_LABEL_BEND,
            IDC_LABEL_ACTIVESET_TUNING,
            IDC_CHECK_ACTIVESET_ADAPTIVE_BUDGET,
            IDC_CHECK_ACTIVESET_HARD_CLAMP,
            IDC_LABEL_ACTIVESET_MAX_CLUSTERS,
            IDC_TRACK_ACTIVESET_MAX_CLUSTERS,
            IDC_LABEL_ACTIVESET_MAX_GUIDES,
            IDC_TRACK_ACTIVESET_MAX_GUIDES,
            IDC_LABEL_ACTIVESET_THRESHOLD,
            IDC_TRACK_ACTIVESET_THRESHOLD,
            IDC_LABEL_ACTIVESET_TARGET_FRAME,
            IDC_TRACK_ACTIVESET_TARGET_FRAME,
            IDC_LABEL_ACTIVESET_FIXED_COST,
            IDC_TRACK_ACTIVESET_FIXED_COST,
            IDC_LABEL_ACTIVESET_CLUSTER_COST,
            IDC_TRACK_ACTIVESET_CLUSTER_COST,
            IDC_LABEL_ACTIVESET_SCORE_WEIGHTS,
            IDC_LABEL_ACTIVESET_MOTION_WEIGHT,
            IDC_TRACK_ACTIVESET_MOTION_WEIGHT,
            IDC_LABEL_ACTIVESET_CONTACT_WEIGHT,
            IDC_TRACK_ACTIVESET_CONTACT_WEIGHT,
            IDC_LABEL_ACTIVESET_UNCERTAINTY_WEIGHT,
            IDC_TRACK_ACTIVESET_UNCERTAINTY_WEIGHT,
            IDC_LABEL_ACTIVESET_EDIT_WEIGHT,
            IDC_TRACK_ACTIVESET_EDIT_WEIGHT,
            IDC_BTN_EXPORT_ACTIVESET_JSON,
            IDC_BTN_EXPORT_RUNTIME_REPORT,
            IDC_LABEL_SCALP_COVERAGE_STRENGTH,
            IDC_TRACK_SCALP_COVERAGE_STRENGTH,
            IDC_LABEL_SCALP_PART_WIDTH,
            IDC_TRACK_SCALP_PART_WIDTH,
            IDC_LABEL_SCALP_DARKNESS,
            IDC_TRACK_SCALP_DARKNESS,
            IDC_LABEL_SCALP_HAIR_BLEND,
            IDC_TRACK_SCALP_HAIR_BLEND,
            IDC_BTN_WORKSPACE_GROOM,
            IDC_BTN_WORKSPACE_SIM,
            IDC_BTN_WORKSPACE_REVIEW,
            IDC_BTN_WORKSPACE_REPORT,
            IDC_BTN_TOOL_SELECT,
            IDC_BTN_TOOL_COMB,
            IDC_BTN_TOOL_GRAB,
            IDC_BTN_TOOL_SMOOTH,
            IDC_BTN_TOOL_LENGTH,
            IDC_BTN_TOOL_VOLUME,
            IDC_BTN_TOOL_PART,
            IDC_BTN_TOOL_FREEZE,
            IDC_BTN_TOOL_STRAND_FIX,
            IDC_BTN_UNDO_EDIT,
            IDC_BTN_REDO_EDIT,
            IDC_BTN_TRAY_LAYERS,
            IDC_BTN_TRAY_HISTORY,
            IDC_BTN_TRAY_METRICS,
            IDC_BTN_TOGGLE_INSPECTOR,
            IDC_BTN_TOGGLE_TRAY,
            IDC_BTN_TOGGLE_ADVANCED,
            IDC_BTN_FAST_VIEWPORT,
            IDC_LABEL_WORKSPACE_TITLE,
            IDC_LABEL_INSPECTOR_TITLE,
            IDC_LABEL_TRAY_TITLE,
            IDC_LABEL_TOOL_HINT,
            IDC_LABEL_SESSION_STATUS
        };

        int SliderFromFloat(float value, float minValue, float maxValue, int sliderMax = 1000)
        {
            const float t = (value - minValue) / std::max(maxValue - minValue, 1.0e-6f);
            return static_cast<int>(Clamp(t, 0.0f, 1.0f) * static_cast<float>(sliderMax));
        }

        float FloatFromSlider(int sliderValue, float minValue, float maxValue, int sliderMax = 1000)
        {
            const float t = Clamp(static_cast<float>(sliderValue) / static_cast<float>(std::max(sliderMax, 1)), 0.0f, 1.0f);
            return minValue + (maxValue - minValue) * t;
        }

        std::wstring FormatFloat(float value, int precision = 3)
        {
            std::wstringstream ss;
            ss << std::fixed << std::setprecision(precision) << value;
            return ss.str();
        }

        std::wstring FormatScientific(float value)
        {
            std::wstringstream ss;
            ss << std::scientific << std::setprecision(2) << value;
            return ss.str();
        }

        void SetWindowTextFormat(HWND hwnd, const std::wstring& text)
        {
            if (hwnd)
            {
                SetWindowTextW(hwnd, text.c_str());
            }
        }

        std::wstring GetWindowTextString(HWND hwnd)
        {
            if (!hwnd)
            {
                return {};
            }
            const int length = GetWindowTextLengthW(hwnd);
            if (length <= 0)
            {
                return {};
            }
            std::vector<wchar_t> buffer(static_cast<size_t>(length) + 1u, L'\0');
            GetWindowTextW(hwnd, buffer.data(), length + 1);
            return std::wstring(buffer.data());
        }

        std::wstring TrimCopy(const std::wstring& text)
        {
            const size_t begin = text.find_first_not_of(L" \t\r\n");
            if (begin == std::wstring::npos)
            {
                return {};
            }
            const size_t end = text.find_last_not_of(L" \t\r\n");
            return text.substr(begin, end - begin + 1u);
        }

        uint32_t ParseOptionalGroupIdText(const std::wstring& text)
        {
            const std::wstring trimmed = TrimCopy(text);
            if (trimmed.empty() || trimmed == L"*" || trimmed == L"any" || trimmed == L"ANY")
            {
                return UINT32_MAX;
            }
            try
            {
                return static_cast<uint32_t>(std::stoul(trimmed));
            }
            catch (...)
            {
                return UINT32_MAX;
            }
        }

        std::wstring FormatOptionalGroupId(uint32_t groupId)
        {
            return (groupId == UINT32_MAX) ? std::wstring(L"Any") : std::to_wstring(groupId);
        }

        std::wstring BuildDefaultInlineEditStackText()
        {
            return L"stack,0.00,0.00\r\n";
        }

        std::vector<std::string> SplitSimpleCsvText(const std::string& line)
        {
            std::vector<std::string> tokens;
            std::string current;
            bool inQuotes = false;
            for (size_t i = 0; i < line.size(); ++i)
            {
                const char c = line[i];
                if (c == '"')
                {
                    if (inQuotes && i + 1 < line.size() && line[i + 1] == '"')
                    {
                        current.push_back('"');
                        ++i;
                    }
                    else
                    {
                        inQuotes = !inQuotes;
                    }
                }
                else if (c == ',' && !inQuotes)
                {
                    tokens.push_back(current);
                    current.clear();
                }
                else
                {
                    current.push_back(c);
                }
            }
            tokens.push_back(current);
            return tokens;
        }

        std::string EscapeCsvTokenLocal(const std::string& token)
        {
            if (token.find_first_of(",\"\n\r") == std::string::npos)
            {
                return token;
            }
            std::string escaped = "\"";
            for (char c : token)
            {
                if (c == '\"')
                {
                    escaped += "\"\"";
                }
                else
                {
                    escaped.push_back(c);
                }
            }
            escaped.push_back('\"');
            return escaped;
        }

        std::string TrimAsciiCopy(std::string text)
        {
            const auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
            while (!text.empty() && isSpace(static_cast<unsigned char>(text.front())))
            {
                text.erase(text.begin());
            }
            while (!text.empty() && isSpace(static_cast<unsigned char>(text.back())))
            {
                text.pop_back();
            }
            return text;
        }

        std::string ToLowerAsciiCopy(std::string text)
        {
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c)
            {
                return static_cast<char>(std::tolower(c));
            });
            return text;
        }

        bool TryParseFloatTokenLocal(const std::string& token, float& outValue)
        {
            try
            {
                size_t consumed = 0u;
                outValue = std::stof(token, &consumed);
                return consumed == token.size();
            }
            catch (...)
            {
                return false;
            }
        }

