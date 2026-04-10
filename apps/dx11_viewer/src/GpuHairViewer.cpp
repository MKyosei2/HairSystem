#include "GpuHairViewer.h"

#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <numeric>
#include <limits>
#include <sstream>
#include <unordered_map>

#include "hairgpu/ActiveSet.h"
#include "hairgpu/AssetIO.h"
#include "hairgpu/Authoring.h"
#include "hairgpu/Math.h"
#include "hairgpu/Wig.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "comctl32.lib")

using Microsoft::WRL::ComPtr;

namespace hairgpu::viewer
{
    namespace
    {
        constexpr wchar_t kWindowClassName[] = L"HairGpuViewerWindow";
        constexpr int kUiPanelWidth = 380;
        constexpr int kUiMargin = 12;

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
            IDC_BTN_EXPORT_RUNTIME_REPORT
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
            return
                L"stack,0.42,0.10\r\n"
                L"layer,front_sector,1,1.0,sector,0.50,0.50,0.50,0.50,0.00,0.0000000,3.1415926,0.20,,1,1.05,1.00,0.00,-0.01,0.00,0.00,-0.03,0.00,0.00,0.05,1.00\r\n"
                L"layer,tip_refine,1,0.85,ellipse,0.50,0.50,0.16,0.16,0.00,0.00,3.1415926,0.35,,1,0.97,1.00,0.00,0.00,0.00,0.00,0.02,0.00,0.01,-0.08,0.85\r\n";
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
            out << "# layer,name,enabled,layerWeight,mode,cx,cy,rx,ry,innerRadius,angleCenterRadians,angleHalfExtentRadians,feather,groupId,guidesOnly,lengthScale,widthScale,rootOffsetX,rootOffsetY,rootOffsetZ,tipOffsetX,tipOffsetY,tipOffsetZ,sideOffset,yawRadians,followerInfluence\n";
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
                    << layer.edit.followerInfluence << "\n";
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

        float DistanceToCapsuleSurface(const hairgpu::Vec3& p, const hairgpu::CapsuleCollider& capsule)
        {
            const hairgpu::Vec3 ab = capsule.pointB - capsule.pointA;
            const float denom = hairgpu::Dot(ab, ab);
            const float t = (denom > 1.0e-6f)
                ? hairgpu::Clamp(hairgpu::Dot(p - capsule.pointA, ab) / denom, 0.0f, 1.0f)
                : 0.0f;
            const hairgpu::Vec3 closest = capsule.pointA + ab * t;
            const float radius = hairgpu::Lerp(capsule.radiusA, capsule.radiusB, t);
            return hairgpu::Length(p - closest) - radius;
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
    }

    bool GpuHairViewer::Initialize(HINSTANCE instance, int showCommand)
    {
        INITCOMMONCONTROLSEX icc = {};
        icc.dwSize = sizeof(icc);
        icc.dwICC = ICC_BAR_CLASSES | ICC_STANDARD_CLASSES;
        InitCommonControlsEx(&icc);

        m_asset = BuildSampleHybridHairAsset(m_builderGuideCount, m_builderFollowersPerGuide, m_builderParticlesPerStrand, 0.036f);
        {
            WigStyleSettings wig;
            wig.strength = 0.02f;
            wig.capRegion = 0.08f;
            wig.shellOffset = 0.00015f;
            wig.flowBlend = 1.0f;
            wig.outwardBlend = 0.0f;
            wig.restoreOriginalTowardsTip = 0.0f;
            StyleAssetToScalpWig(m_asset, wig);
        }
        m_lastAssetSource = L"Sample Hybrid";
        m_lastImportStatus = L"Sample asset active";
        m_activeSetBudget.maxActiveClusters = 6u;
        m_activeSetBudget.maxActiveGuides = 256u;
        m_activeSetBudget.promoteScoreThreshold = 0.52f;
        m_simConfig.solverIterations = 5;
        m_simConfig.fixedTimeStep = 1.0f / 120.0f;
        m_simConfig.gravity = 18.5f;
        m_simConfig.globalDamping = 0.085f;
        m_simConfig.segmentLength = 0.036f;
        m_simConfig.bendStiffness = 0.14f;
        m_simConfig.followStiffness = 0.988f;
        m_simConfig.windStrength = 0.03f;
        m_simConfig.windDirection = { 0.90f, 0.0f, 0.25f };
        m_simConfig.floorY = -1.05f;
        m_simConfig.selfCollisionRadius = 0.0055f;
        m_simConfig.selfCollisionStiffness = 0.10f;
        m_simConfig.stretchCompliance = 2.0e-7f;
        m_simConfig.bendCompliance = 1.5e-5f;
        m_simConfig.rootStiffness = 0.28f;
        m_simConfig.velocityShockPropagation = 0.72f;
        m_simConfig.selfCollisionGridDimX = 18;
        m_simConfig.selfCollisionGridDimY = 18;
        m_simConfig.selfCollisionGridDimZ = 18;
        m_simConfig.selfCollisionCellCapacity = 64;
        m_simConfig.selfCollisionGridOrigin = { -0.72f, 0.48f, -0.72f };
        m_simConfig.selfCollisionCellSize = 0.060f;
        m_selfCollisionRadius = m_simConfig.selfCollisionRadius;
        m_selfCollisionStiffness = m_simConfig.selfCollisionStiffness;
        m_enableRuntimeAngularBend = true;
        m_runtimeAngularBendRadians = 0.16f;
        m_runtimeAngularBendCompliance = 5.0e-6f;
        m_runtimeAngularBendStiffness = 0.36f;
        m_runtimeAngularBendDamping = 0.08f;
        m_widthScale = 0.56f;
        m_renderMode = RenderMode::StrandRibbon;
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

        if (!CreateSimulationResources())
        {
            ShowStartupDialog(m_hwnd, L"Hair GPU Viewer Startup Failure",
                L"CreateSimulationResources() failed.\r\nThe viewer window was created, but GPU simulation buffers were not initialized.");
            Shutdown();
            return false;
        }

        if (!CreateRenderResources())
        {
            ShowStartupDialog(m_hwnd, L"Hair GPU Viewer Startup Failure",
                L"CreateRenderResources() failed.\r\nThe viewer window was created, but render resources were not initialized.");
            Shutdown();
            return false;
        }

        if (!LoadShaders())
        {
            if (m_hwnd)
            {
                ShowStartupDialog(m_hwnd, L"Hair GPU Viewer Startup Failure",
                    L"LoadShaders() failed.\r\nCheck the shader file paths and HLSL compile error dialog.");
            }
            Shutdown();
            return false;
        }

        RebuildActiveSetFoundation();
        RecomputeAssetBounds();
        AutoFrameCameraToAsset(true);
        ResetSimulation();
        QueryPerformanceFrequency(&m_qpcFrequency);
        QueryPerformanceCounter(&m_lastTick);
        LoadQuickEditFormFromInlineText(m_selectedInlineLayerIndex);
        SyncControlsFromState();
        LayoutUiControls();
        UpdateStatsLabel();
        RefreshAssetStatsText();
        UpdateWindowTitle();
        return true;
    }

    int GpuHairViewer::Run()
    {
        MSG msg = {};
        while (m_running)
        {
            ResolveGpuTimingFrame(m_gpuTimingFrames[m_gpuTimingWriteIndex]);
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                if (msg.message == WM_QUIT)
                {
                    m_running = false;
                    break;
                }

                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }

            LARGE_INTEGER now = {};
            QueryPerformanceCounter(&now);
            const float dt = static_cast<float>(now.QuadPart - m_lastTick.QuadPart) / static_cast<float>(m_qpcFrequency.QuadPart);
            m_lastTick = now;
            m_lastFrameSeconds = dt;
            const float instantFps = (dt > 1.0e-6f) ? (1.0f / dt) : 0.0f;
            m_smoothedFps = (m_smoothedFps <= 0.0f) ? instantFps : (m_smoothedFps * 0.92f + instantFps * 0.08f);
            m_statsUpdateAccumulator += dt;

            GpuTimingFrame& gpuFrame = m_gpuTimingFrames[m_gpuTimingWriteIndex];
            m_activeGpuTimingFrameIndex = -1;
            if (gpuFrame.disjoint)
            {
                m_context->Begin(gpuFrame.disjoint.Get());
                m_context->End(gpuFrame.frameStart.Get());
                gpuFrame.issued = true;
                m_activeGpuTimingFrameIndex = static_cast<int32_t>(m_gpuTimingWriteIndex);
            }

            m_lastSimulationStepMilliseconds = 0.0f;
            if (!m_paused)
            {
                LARGE_INTEGER simBegin = {};
                LARGE_INTEGER simEnd = {};
                QueryPerformanceCounter(&simBegin);
                m_accumulator += std::min(dt, 0.1f);
                while (m_accumulator >= m_simConfig.fixedTimeStep)
                {
                    StepSimulation(m_simConfig.fixedTimeStep);
                    m_accumulator -= m_simConfig.fixedTimeStep;
                    m_elapsedSeconds += m_simConfig.fixedTimeStep;
                }
                QueryPerformanceCounter(&simEnd);
                m_lastSimulationStepMilliseconds = static_cast<float>(simEnd.QuadPart - simBegin.QuadPart) * 1000.0f / static_cast<float>(m_qpcFrequency.QuadPart);
            }
            else if (gpuFrame.issued)
            {
                m_context->End(gpuFrame.simStart.Get());
                m_context->End(gpuFrame.simEnd.Get());
            }

            PerformanceSample sample = {};
            sample.elapsedSeconds = m_elapsedSeconds;
            sample.frameMilliseconds = dt * 1000.0f;
            sample.simulationMilliseconds = m_lastSimulationStepMilliseconds;
            sample.fps = instantFps;
            sample.guideCount = m_asset.guideStrandCount;
            sample.followerCount = m_asset.followerStrandCount;
            sample.totalStrandCount = static_cast<uint32_t>(m_asset.strands.size());
            sample.totalParticleCount = static_cast<uint32_t>(m_asset.restPoints.size());
            sample.activeSetClusterCount = m_activeSetPlan.summary.clusterCount;
            sample.activeSetSelectedClusterCount = m_activeSetPlan.summary.selectedClusterCount;
            sample.activeSetSelectedGuideCount = m_activeSetPlan.summary.selectedGuideCount;
            sample.activeSetSelectedStrandCount = m_activeSetPlan.summary.selectedStrandCount;
            sample.activeSetHighestScore = m_activeSetPlan.summary.highestScore;
            sample.activeSetAverageSelectedScore = m_activeSetPlan.summary.averageSelectedScore;
            sample.activeSetEvaluationMilliseconds = m_activeSetEvaluationMilliseconds;
            sample.activeSetBudgetScale = m_activeSetPlan.summary.budgetScale;
            sample.activeSetRecommendedSolverIterations = m_activeSetPlan.summary.recommendedSolverIterations;
            sample.fullTierCount = m_activeSetPlan.summary.fullTierCount;
            sample.reducedTierCount = m_activeSetPlan.summary.reducedTierCount;
            sample.frozenTierCount = m_activeSetPlan.summary.frozenTierCount;
            sample.cachedTierCount = m_activeSetPlan.summary.cachedTierCount;
            sample.correctiveStrandCount = m_sparseCorrectiveReport.correctedStrandCount;
            sample.correctivePointCount = m_sparseCorrectiveReport.correctedPointCount;
            sample.correctiveAveragePenetration = m_sparseCorrectiveReport.averagePenetration;
            sample.correctiveMaxPenetration = m_sparseCorrectiveReport.maxPenetration;
            sample.correctiveAverageLengthErrorBefore = m_sparseCorrectiveReport.averageLengthErrorBefore;
            sample.correctiveAverageLengthErrorAfter = m_sparseCorrectiveReport.averageLengthErrorAfter;
            m_performanceSamples.push_back(sample);
            if (m_performanceSamples.size() > 12000)
            {
                m_performanceSamples.erase(m_performanceSamples.begin(), m_performanceSamples.begin() + static_cast<std::ptrdiff_t>(m_performanceSamples.size() - 12000));
            }

            UpdateCamera();
            Render();
            if (!m_gpuTimingFrames.empty())
            {
                m_gpuTimingWriteIndex = (m_gpuTimingWriteIndex + 1u) % static_cast<uint32_t>(m_gpuTimingFrames.size());
            }
            m_activeGpuTimingFrameIndex = -1;
            if (m_statsUpdateAccumulator >= 0.2f)
            {
                UpdateStatsLabel();
                m_statsUpdateAccumulator = 0.0f;
            }
        }

        Shutdown();
        return 0;
    }

    bool GpuHairViewer::CreateWindowAndDevice(HINSTANCE instance, int showCommand)
    {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = instance;
        wc.lpszClassName = kWindowClassName;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        {
            const DWORD err = GetLastError();
            ShowStartupDialog(nullptr, L"Hair GPU Viewer Startup Failure",
                std::wstring(L"RegisterClassExW failed. Win32 error=") + Win32Hex(err));
            return false;
        }

        RECT rect = { 0, 0, static_cast<LONG>(m_width), static_cast<LONG>(m_height) };
        AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);

        m_hwnd = CreateWindowExW(
            0,
            kWindowClassName,
            L"Hair GPU Viewer",
            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
            CW_USEDEFAULT, CW_USEDEFAULT,
            rect.right - rect.left,
            rect.bottom - rect.top,
            nullptr,
            nullptr,
            instance,
            this);

        if (!m_hwnd)
        {
            const DWORD err = GetLastError();
            ShowStartupDialog(nullptr, L"Hair GPU Viewer Startup Failure",
                std::wstring(L"CreateWindowExW failed. Win32 error=") + Win32Hex(err));
            return false;
        }

        ShowWindow(m_hwnd, showCommand);
        UpdateWindow(m_hwnd);

        DXGI_SWAP_CHAIN_DESC swapDesc = {};
        swapDesc.BufferCount = 2;
        swapDesc.BufferDesc.Width = m_width;
        swapDesc.BufferDesc.Height = m_height;
        swapDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapDesc.OutputWindow = m_hwnd;
        swapDesc.SampleDesc.Count = 1;
        swapDesc.Windowed = TRUE;
        swapDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        UINT debugFlags = 0;
    #if defined(_DEBUG)
        debugFlags |= D3D11_CREATE_DEVICE_DEBUG;
    #endif

        auto tryCreate = [&](D3D_DRIVER_TYPE driverType, UINT flags, HRESULT& outHr) -> bool
        {
            D3D_FEATURE_LEVEL requested = D3D_FEATURE_LEVEL_11_0;
            outHr = D3D11CreateDeviceAndSwapChain(
                nullptr,
                driverType,
                nullptr,
                flags,
                &requested,
                1,
                D3D11_SDK_VERSION,
                &swapDesc,
                &m_swapChain,
                &m_device,
                nullptr,
                &m_context);
            return SUCCEEDED(outHr);
        };

        HRESULT hrHardwareDebug = E_FAIL;
        HRESULT hrHardware = E_FAIL;
        HRESULT hrWarp = E_FAIL;

        if (!(tryCreate(D3D_DRIVER_TYPE_HARDWARE, debugFlags, hrHardwareDebug) ||
              tryCreate(D3D_DRIVER_TYPE_HARDWARE, 0u, hrHardware) ||
              tryCreate(D3D_DRIVER_TYPE_WARP, 0u, hrWarp)))
        {
            std::wstring message =
                std::wstring(L"D3D11CreateDeviceAndSwapChain failed.\r\n\r\n") +
                L"Hardware + debug: " + HrHex(hrHardwareDebug) + L"\r\n" +
                L"Hardware: " + HrHex(hrHardware) + L"\r\n" +
                L"WARP fallback: " + HrHex(hrWarp) + L"\r\n\r\n" +
                L"Try one of these:\r\n" +
                L"- build/run Release\r\n" +
                L"- install Windows Optional Feature: Graphics Tools\r\n" +
                L"- use x64 configuration if available";
            ShowStartupDialog(m_hwnd, L"Hair GPU Viewer Startup Failure", message);
            return false;
        }

        Resize(m_width, m_height);
        return true;
    }

    bool GpuHairViewer::CreateSimulationResources()
    {
        m_initialParticles.clear();
        m_followGpuData.clear();
        m_restScalpParticles.clear();
        m_animatedScalpParticles.clear();
        m_scalpTriangles.clear();
        m_rootBindingGpuData.clear();
        m_zeroDistanceLambdas.clear();
        m_zeroBendLambdas.clear();

        m_initialParticles.reserve(m_asset.restPoints.size());
        m_followGpuData.reserve(m_asset.followData.size());
        m_restScalpParticles.reserve(m_asset.scalpMesh.positions.size());
        m_animatedScalpParticles.reserve(m_asset.scalpMesh.positions.size());
        m_scalpTriangles.reserve(m_asset.scalpMesh.triangles.size());
        m_rootBindingGpuData.reserve(m_asset.rootBindings.size());

        for (const Vec3& p : m_asset.restPoints)
        {
            m_initialParticles.push_back({ p.x, p.y, p.z, 1.0f });
        }

        for (const FollowData& f : m_asset.followData)
        {
            m_followGpuData.push_back({ f.parentGuide, f.followWeight, f.clumpWeight, f.noiseWeight });
        }

        m_renderData = BuildSegmentRenderData(m_asset);

        for (const Vec3& p : m_asset.scalpMesh.positions)
        {
            m_restScalpParticles.push_back({ p.x, p.y, p.z, 1.0f });
            m_animatedScalpParticles.push_back({ p.x, p.y, p.z, 1.0f });
        }

        for (const TriangleIndices& tri : m_asset.scalpMesh.triangles)
        {
            m_scalpTriangles.push_back({ tri.a, tri.b, tri.c, 0u });
        }

        for (const RootBinding& binding : m_asset.rootBindings)
        {
            RootBindingGpuData gpu = {};
            gpu.triangleIndex = binding.triangleIndex;
            gpu.barycentric[0] = binding.barycentric.x;
            gpu.barycentric[1] = binding.barycentric.y;
            gpu.barycentric[2] = binding.barycentric.z;
            gpu.rootUv[0] = binding.rootUv.x;
            gpu.rootUv[1] = binding.rootUv.y;
            m_rootBindingGpuData.push_back(gpu);
        }

        const UINT totalParticles = static_cast<UINT>(m_initialParticles.size());
        const UINT guideSegments = m_asset.guideStrandCount * (m_asset.particlesPerStrand > 0 ? (m_asset.particlesPerStrand - 1u) : 0u);
        const UINT guideBends = m_asset.guideStrandCount * (m_asset.particlesPerStrand > 2 ? (m_asset.particlesPerStrand - 2u) : 0u);
        m_zeroDistanceLambdas.resize(std::max<UINT>(guideSegments, 1u), 0.0f);
        m_zeroBendLambdas.resize(std::max<UINT>(guideBends, 1u), 0.0f);

        m_positions[0] = CreateStructuredBuffer(sizeof(GpuParticle), totalParticles, m_initialParticles.data(), true);
        m_positions[1] = CreateStructuredBuffer(sizeof(GpuParticle), totalParticles, m_initialParticles.data(), true);
        m_previousPositions[0] = CreateStructuredBuffer(sizeof(GpuParticle), totalParticles, m_initialParticles.data(), true);
        m_previousPositions[1] = CreateStructuredBuffer(sizeof(GpuParticle), totalParticles, m_initialParticles.data(), true);
        m_restPositions = CreateStructuredBuffer(sizeof(GpuParticle), totalParticles, m_initialParticles.data(), false);
        m_pointWidths = CreateStructuredBuffer(sizeof(float), static_cast<UINT>(m_asset.pointWidths.size()), m_asset.pointWidths.data(), false);
        m_followBuffer = CreateStructuredBuffer(sizeof(FollowGpuData), static_cast<UINT>(m_followGpuData.size()), m_followGpuData.data(), false);

        const UINT totalStrands = static_cast<UINT>(std::max<size_t>(m_asset.strands.size(), 1u));
        std::vector<uint32_t> initialRenderTiers(totalStrands, static_cast<uint32_t>(hairgpu::SimTier::Reduced));
        std::vector<uint32_t> initialGuideTiers(std::max<UINT>(m_asset.guideStrandCount, 1u), static_cast<uint32_t>(hairgpu::SimTier::Full));
        std::vector<uint32_t> initialFollowerTiers(std::max<UINT>(m_asset.followerStrandCount, 1u), static_cast<uint32_t>(hairgpu::SimTier::Reduced));
        for (uint32_t strandIndex = 0u; strandIndex < totalStrands; ++strandIndex)
        {
            const bool isGuide = strandIndex < m_asset.guideStrandCount;
            uint32_t tier = isGuide ? static_cast<uint32_t>(hairgpu::SimTier::Full)
                                    : static_cast<uint32_t>(hairgpu::SimTier::Reduced);
            if (strandIndex < m_activeSetPlan.recommendedSimulationTiers.size())
            {
                tier = static_cast<uint32_t>(m_activeSetPlan.recommendedSimulationTiers[strandIndex]);
            }
            initialRenderTiers[strandIndex] = tier;
            if (isGuide)
            {
                initialGuideTiers[strandIndex] = tier;
            }
            else
            {
                initialFollowerTiers[strandIndex - m_asset.guideStrandCount] = tier;
            }
        }
        std::array<uint32_t, 4> initialCompactionCounters = { 0u, 0u, 0u, 0u };
        std::vector<uint32_t> initialCompactedFollowerIndices(std::max<UINT>(m_asset.followerStrandCount, 1u), 0u);
        std::vector<uint32_t> initialCompactedGuideIndices(std::max<UINT>(m_asset.guideStrandCount, 1u), 0u);
        for (uint32_t i = 0u; i < m_asset.followerStrandCount && i < initialCompactedFollowerIndices.size(); ++i)
        {
            initialCompactedFollowerIndices[i] = i;
        }
        for (uint32_t i = 0u; i < m_asset.guideStrandCount && i < initialCompactedGuideIndices.size(); ++i)
        {
            initialCompactedGuideIndices[i] = i;
        }
        m_renderStrandTiersBuffer = CreateStructuredBuffer(sizeof(uint32_t), totalStrands, initialRenderTiers.data(), false);
        m_guideTierAssignmentsBuffer = CreateStructuredBuffer(sizeof(uint32_t), static_cast<UINT>(initialGuideTiers.size()), initialGuideTiers.data(), false);
        m_followerTierAssignmentsBuffer = CreateStructuredBuffer(sizeof(uint32_t), static_cast<UINT>(initialFollowerTiers.size()), initialFollowerTiers.data(), false);
        m_compactedFollowerIndicesBuffer = CreateStructuredBuffer(sizeof(uint32_t), static_cast<UINT>(initialCompactedFollowerIndices.size()), initialCompactedFollowerIndices.data(), true);
        m_compactedFollowerCounterBuffer = CreateStructuredBuffer(sizeof(uint32_t), static_cast<UINT>(initialCompactionCounters.size()), initialCompactionCounters.data(), true);
        m_compactedGuideIndicesBuffer = CreateStructuredBuffer(sizeof(uint32_t), static_cast<UINT>(initialCompactedGuideIndices.size()), initialCompactedGuideIndices.data(), true);
        m_compactedGuideCounterBuffer = CreateStructuredBuffer(sizeof(uint32_t), static_cast<UINT>(initialCompactionCounters.size()), initialCompactionCounters.data(), true);

        m_followDispatchArgsBuffer.Reset();
        m_followDispatchArgsUav.Reset();
        D3D11_BUFFER_DESC indirectArgsDesc = {};
        indirectArgsDesc.ByteWidth = sizeof(UINT) * 4u;
        indirectArgsDesc.Usage = D3D11_USAGE_DEFAULT;
        indirectArgsDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
        indirectArgsDesc.MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS | D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
        if (FAILED(m_device->CreateBuffer(&indirectArgsDesc, nullptr, &m_followDispatchArgsBuffer)))
        {
            return false;
        }

        D3D11_UNORDERED_ACCESS_VIEW_DESC indirectArgsUavDesc = {};
        indirectArgsUavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        indirectArgsUavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
        indirectArgsUavDesc.Buffer.FirstElement = 0;
        indirectArgsUavDesc.Buffer.NumElements = indirectArgsDesc.ByteWidth / sizeof(UINT);
        indirectArgsUavDesc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
        if (FAILED(m_device->CreateUnorderedAccessView(m_followDispatchArgsBuffer.Get(), &indirectArgsUavDesc, &m_followDispatchArgsUav)))
        {
            return false;
        }

        m_scalpPositions = CreateStructuredBuffer(sizeof(GpuParticle), static_cast<UINT>(m_animatedScalpParticles.size()), m_animatedScalpParticles.data(), false);
        m_scalpTrianglesBuffer = CreateStructuredBuffer(sizeof(TriangleGpuData), static_cast<UINT>(m_scalpTriangles.size()), m_scalpTriangles.data(), false);
        m_rootBindingsBuffer = CreateStructuredBuffer(sizeof(RootBindingGpuData), static_cast<UINT>(m_rootBindingGpuData.size()), m_rootBindingGpuData.data(), false);
        m_distanceLambdaBuffer = CreateStructuredBuffer(sizeof(float), static_cast<UINT>(m_zeroDistanceLambdas.size()), m_zeroDistanceLambdas.data(), true);
        m_bendLambdaBuffer = CreateStructuredBuffer(sizeof(float), static_cast<UINT>(m_zeroBendLambdas.size()), m_zeroBendLambdas.data(), true);

        const UINT gridCellCount = std::max<UINT>(m_simConfig.selfCollisionGridDimX * m_simConfig.selfCollisionGridDimY * m_simConfig.selfCollisionGridDimZ, 1u);
        std::vector<uint32_t> zeroCellCounts(gridCellCount, 0u);
        std::vector<uint32_t> zeroCellIndices(std::max<UINT>(gridCellCount * m_simConfig.selfCollisionCellCapacity, 1u), 0xFFFFFFFFu);
        m_gridCellCountsBuffer = CreateStructuredBuffer(sizeof(uint32_t), gridCellCount, zeroCellCounts.data(), true);
        m_gridCellIndicesBuffer = CreateStructuredBuffer(sizeof(uint32_t), static_cast<UINT>(zeroCellIndices.size()), zeroCellIndices.data(), true);

        const UINT hashBucketCount = std::max<UINT>(m_simConfig.selfCollisionHashBucketCount, 1u);
        std::vector<uint32_t> zeroHashHeads(hashBucketCount, 0xFFFFFFFFu);
        std::vector<uint32_t> zeroParticleNext(std::max<UINT>(totalParticles, 1u), 0xFFFFFFFFu);
        m_gridHashHeadsBuffer = CreateStructuredBuffer(sizeof(uint32_t), hashBucketCount, zeroHashHeads.data(), true);
        m_gridParticleNextBuffer = CreateStructuredBuffer(sizeof(uint32_t), static_cast<UINT>(zeroParticleNext.size()), zeroParticleNext.data(), true);

        const UINT totalGuideParticles = std::max<UINT>(m_asset.guideStrandCount * m_asset.particlesPerStrand, 1u);
        m_sortedBroadphasePaddedCount = NextPowerOfTwo(totalGuideParticles);
        std::vector<uint32_t> invalidSortedKeys(m_sortedBroadphasePaddedCount, 0xFFFFFFFFu);
        std::vector<uint32_t> invalidSortedIndices(m_sortedBroadphasePaddedCount, 0xFFFFFFFFu);
        std::vector<uint32_t> invalidSortedStarts(gridCellCount, 0xFFFFFFFFu);
        std::vector<uint32_t> invalidSortedEnds(gridCellCount, 0xFFFFFFFFu);
        m_sortedKeysBuffer = CreateStructuredBuffer(sizeof(uint32_t), m_sortedBroadphasePaddedCount, invalidSortedKeys.data(), true);
        m_sortedParticleIndicesBuffer = CreateStructuredBuffer(sizeof(uint32_t), m_sortedBroadphasePaddedCount, invalidSortedIndices.data(), true);
        m_sortedCellStartsBuffer = CreateStructuredBuffer(sizeof(uint32_t), gridCellCount, invalidSortedStarts.data(), true);
        m_sortedCellEndsBuffer = CreateStructuredBuffer(sizeof(uint32_t), gridCellCount, invalidSortedEnds.data(), true);

        D3D11_BUFFER_DESC vbDesc = {};
        vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        vbDesc.ByteWidth = static_cast<UINT>(m_renderData.segmentVertices.size() * sizeof(SegmentRenderVertex));
        vbDesc.Usage = D3D11_USAGE_IMMUTABLE;
        D3D11_SUBRESOURCE_DATA vbData = {};
        vbData.pSysMem = m_renderData.segmentVertices.data();
        if (FAILED(m_device->CreateBuffer(&vbDesc, &vbData, &m_segmentVertexBuffer)))
        {
            return false;
        }

        m_strandTubeVertexBuffer.Reset();
        m_strandTubeIndexBuffer.Reset();
        m_strandTubeIndexCount = 0;
        m_strandTubeRingSides = 0u;

        std::vector<ScalpVertex> scalpVertices;
        scalpVertices.reserve(m_asset.scalpMesh.positions.size());
        for (size_t i = 0; i < m_asset.scalpMesh.positions.size(); ++i)
        {
            const Vec3 n = (i < m_asset.scalpMesh.normals.size()) ? m_asset.scalpMesh.normals[i] : Vec3(0.0f, 1.0f, 0.0f);
            ScalpVertex v = {};
            v.scalpIndex = static_cast<uint32_t>(i);
            v.nx = n.x;
            v.ny = n.y;
            v.nz = n.z;
            v.r = 0.76f;
            v.g = 0.62f;
            v.b = 0.52f;
            scalpVertices.push_back(v);
        }
        if (!scalpVertices.empty())
        {
            D3D11_BUFFER_DESC scalpVbDesc = {};
            scalpVbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            scalpVbDesc.ByteWidth = static_cast<UINT>(scalpVertices.size() * sizeof(ScalpVertex));
            scalpVbDesc.Usage = D3D11_USAGE_DYNAMIC;
            scalpVbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            D3D11_SUBRESOURCE_DATA scalpVbData = {};
            scalpVbData.pSysMem = scalpVertices.data();
            if (FAILED(m_device->CreateBuffer(&scalpVbDesc, &scalpVbData, &m_scalpVertexBuffer)))
            {
                return false;
            }
        }

        std::vector<uint32_t> scalpIndices;
        scalpIndices.reserve(m_asset.scalpMesh.triangles.size() * 3ull);
        for (const TriangleIndices& tri : m_asset.scalpMesh.triangles)
        {
            scalpIndices.push_back(tri.a);
            scalpIndices.push_back(tri.b);
            scalpIndices.push_back(tri.c);
        }
        m_scalpIndexCount = static_cast<uint32_t>(scalpIndices.size());
        if (!scalpIndices.empty())
        {
            D3D11_BUFFER_DESC scalpIbDesc = {};
            scalpIbDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
            scalpIbDesc.ByteWidth = static_cast<UINT>(scalpIndices.size() * sizeof(uint32_t));
            scalpIbDesc.Usage = D3D11_USAGE_IMMUTABLE;
            D3D11_SUBRESOURCE_DATA scalpIbData = {};
            scalpIbData.pSysMem = scalpIndices.data();
            if (FAILED(m_device->CreateBuffer(&scalpIbDesc, &scalpIbData, &m_scalpIndexBuffer)))
            {
                return false;
            }
        }

        D3D11_BUFFER_DESC cbDesc = {};
        cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cbDesc.Usage = D3D11_USAGE_DYNAMIC;
        cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        cbDesc.ByteWidth = sizeof(SimConstantsHair);
        if (FAILED(m_device->CreateBuffer(&cbDesc, nullptr, &m_simConstantsHair)))
        {
            return false;
        }

        cbDesc.ByteWidth = sizeof(SimConstantsActiveSet);
        if (FAILED(m_device->CreateBuffer(&cbDesc, nullptr, &m_simConstantsActiveSet)))
        {
            return false;
        }

        cbDesc.ByteWidth = sizeof(SimConstantsScalp);
        if (FAILED(m_device->CreateBuffer(&cbDesc, nullptr, &m_simConstantsScalp)))
        {
            return false;
        }

        cbDesc.ByteWidth = sizeof(BitonicConstants);
        if (FAILED(m_device->CreateBuffer(&cbDesc, nullptr, &m_bitonicConstants)))
        {
            return false;
        }

        cbDesc.ByteWidth = sizeof(RenderConstants);
        if (FAILED(m_device->CreateBuffer(&cbDesc, nullptr, &m_renderConstants)))
        {
            return false;
        }

        m_gpuDebugLinesDirty = true;
        m_debugLineStats = {};
        return true;
    }

    bool GpuHairViewer::CreateGpuTimingQueries()
    {
        for (GpuTimingFrame& frame : m_gpuTimingFrames)
        {
            frame = {};
            auto createQuery = [this](D3D11_QUERY query, Microsoft::WRL::ComPtr<ID3D11Query>& outQuery) -> bool
            {
                D3D11_QUERY_DESC desc = {};
                desc.Query = query;
                return SUCCEEDED(m_device->CreateQuery(&desc, &outQuery));
            };
            if (!createQuery(D3D11_QUERY_TIMESTAMP_DISJOINT, frame.disjoint) ||
                !createQuery(D3D11_QUERY_TIMESTAMP, frame.frameStart) ||
                !createQuery(D3D11_QUERY_TIMESTAMP, frame.simStart) ||
                !createQuery(D3D11_QUERY_TIMESTAMP, frame.simEnd) ||
                !createQuery(D3D11_QUERY_TIMESTAMP, frame.hairDrawStart) ||
                !createQuery(D3D11_QUERY_TIMESTAMP, frame.hairDrawEnd) ||
                !createQuery(D3D11_QUERY_TIMESTAMP, frame.debugDrawStart) ||
                !createQuery(D3D11_QUERY_TIMESTAMP, frame.debugDrawEnd) ||
                !createQuery(D3D11_QUERY_TIMESTAMP, frame.frameEnd) ||
                !createQuery(D3D11_QUERY_PIPELINE_STATISTICS, frame.hairPipelineStats) ||
                !createQuery(D3D11_QUERY_PIPELINE_STATISTICS, frame.debugPipelineStats))
            {
                return false;
            }
        }
        m_gpuTimingWriteIndex = 0u;
        m_activeGpuTimingFrameIndex = -1;
        return true;
    }

    void GpuHairViewer::ResolveGpuTimingFrame(GpuTimingFrame& frame)
    {
        if (!frame.issued || !frame.disjoint)
        {
            return;
        }

        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint = {};
        UINT64 frameStart = 0ull;
        UINT64 simStart = 0ull;
        UINT64 simEnd = 0ull;
        UINT64 hairStart = 0ull;
        UINT64 hairEnd = 0ull;
        UINT64 debugStart = 0ull;
        UINT64 debugEnd = 0ull;
        UINT64 frameEnd = 0ull;
        D3D11_QUERY_DATA_PIPELINE_STATISTICS hairStats = {};
        D3D11_QUERY_DATA_PIPELINE_STATISTICS debugStats = {};

        const UINT flags = D3D11_ASYNC_GETDATA_DONOTFLUSH;
        if (m_context->GetData(frame.disjoint.Get(), &disjoint, sizeof(disjoint), flags) != S_OK ||
            m_context->GetData(frame.frameStart.Get(), &frameStart, sizeof(frameStart), flags) != S_OK ||
            m_context->GetData(frame.simStart.Get(), &simStart, sizeof(simStart), flags) != S_OK ||
            m_context->GetData(frame.simEnd.Get(), &simEnd, sizeof(simEnd), flags) != S_OK ||
            m_context->GetData(frame.hairDrawStart.Get(), &hairStart, sizeof(hairStart), flags) != S_OK ||
            m_context->GetData(frame.hairDrawEnd.Get(), &hairEnd, sizeof(hairEnd), flags) != S_OK ||
            m_context->GetData(frame.debugDrawStart.Get(), &debugStart, sizeof(debugStart), flags) != S_OK ||
            m_context->GetData(frame.debugDrawEnd.Get(), &debugEnd, sizeof(debugEnd), flags) != S_OK ||
            m_context->GetData(frame.frameEnd.Get(), &frameEnd, sizeof(frameEnd), flags) != S_OK ||
            m_context->GetData(frame.hairPipelineStats.Get(), &hairStats, sizeof(hairStats), flags) != S_OK ||
            m_context->GetData(frame.debugPipelineStats.Get(), &debugStats, sizeof(debugStats), flags) != S_OK)
        {
            return;
        }

        if (!disjoint.Disjoint && disjoint.Frequency > 0ull)
        {
            const double invFreqMs = 1000.0 / static_cast<double>(disjoint.Frequency);
            m_lastGpuFrameMilliseconds = static_cast<float>(static_cast<double>(frameEnd - frameStart) * invFreqMs);
            m_lastGpuSimulationMilliseconds = static_cast<float>(static_cast<double>(simEnd - simStart) * invFreqMs);
            m_lastGpuHairDrawMilliseconds = static_cast<float>(static_cast<double>(hairEnd - hairStart) * invFreqMs);
            m_lastGpuDebugDrawMilliseconds = static_cast<float>(static_cast<double>(debugEnd - debugStart) * invFreqMs);
        }
        m_lastHairPipelineStats = hairStats;
        m_lastDebugPipelineStats = debugStats;
        frame.issued = false;
    }

    bool GpuHairViewer::CreateRenderResources()
    {
        D3D11_RASTERIZER_DESC raster = {};
        raster.FillMode = D3D11_FILL_SOLID;
        raster.CullMode = D3D11_CULL_NONE;
        raster.DepthClipEnable = TRUE;
        if (FAILED(m_device->CreateRasterizerState(&raster, &m_rasterizerState)))
        {
            return false;
        }

        D3D11_BLEND_DESC blend = {};
        blend.RenderTarget[0].BlendEnable = TRUE;
        blend.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        blend.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        blend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        if (FAILED(m_device->CreateBlendState(&blend, &m_blendState)))
        {
            return false;
        }

        D3D11_DEPTH_STENCIL_DESC depthDesc = {};
        depthDesc.DepthEnable = TRUE;
        depthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        depthDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
        if (FAILED(m_device->CreateDepthStencilState(&depthDesc, &m_depthState)))
        {
            return false;
        }

        if (!m_debugLineTemplateVertexBuffer)
        {
            const DebugLineTemplateVertex debugTemplate[2] = { { 0.0f }, { 1.0f } };
            D3D11_BUFFER_DESC debugTemplateDesc = {};
            debugTemplateDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            debugTemplateDesc.ByteWidth = sizeof(debugTemplate);
            debugTemplateDesc.Usage = D3D11_USAGE_IMMUTABLE;
            D3D11_SUBRESOURCE_DATA debugTemplateData = {};
            debugTemplateData.pSysMem = debugTemplate;
            if (FAILED(m_device->CreateBuffer(&debugTemplateDesc, &debugTemplateData, &m_debugLineTemplateVertexBuffer)))
            {
                return false;
            }
        }

        if (!CreateGpuTimingQueries())
        {
            return false;
        }

        UpdateScalpOverlayVertexBuffer();
        return true;
    }

    bool GpuHairViewer::LoadShaders()
    {
        std::wstring errorText;
        ComPtr<ID3DBlob> vsBlob;
        ComPtr<ID3DBlob> gsBlob;
        ComPtr<ID3DBlob> psBlob;
        ComPtr<ID3DBlob> csBlob;

        if (!CompileShaderFromFile(L"HairParticle.hlsl", "VSMain", "vs_5_0", &vsBlob, errorText) ||
            !CompileShaderFromFile(L"HairParticle.hlsl", "GSPointMain", "gs_5_0", &gsBlob, errorText))
        {
            MessageBoxW(m_hwnd, errorText.c_str(), L"Shader compile failed", MB_ICONERROR);
            return false;
        }
        if (FAILED(m_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &m_particleVS)) ||
            FAILED(m_device->CreateGeometryShader(gsBlob->GetBufferPointer(), gsBlob->GetBufferSize(), nullptr, &m_particlePointGS)))
        {
            return false;
        }
        if (!CompileShaderFromFile(L"HairParticle.hlsl", "GSBillboardMain", "gs_5_0", &gsBlob, errorText) ||
            !CompileShaderFromFile(L"HairParticle.hlsl", "PSMain", "ps_5_0", &psBlob, errorText))
        {
            MessageBoxW(m_hwnd, errorText.c_str(), L"Shader compile failed", MB_ICONERROR);
            return false;
        }
        if (FAILED(m_device->CreateGeometryShader(gsBlob->GetBufferPointer(), gsBlob->GetBufferSize(), nullptr, &m_particleBillboardGS)) ||
            FAILED(m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &m_particlePS)))
        {
            return false;
        }

        if (!CompileShaderFromFile(L"HairRibbon.hlsl", "VSMain", "vs_5_0", &vsBlob, errorText) ||
            !CompileShaderFromFile(L"HairRibbon.hlsl", "GSMain", "gs_5_0", &gsBlob, errorText) ||
            !CompileShaderFromFile(L"HairRibbon.hlsl", "PSMain", "ps_5_0", &psBlob, errorText))
        {
            MessageBoxW(m_hwnd, errorText.c_str(), L"Shader compile failed", MB_ICONERROR);
            return false;
        }
        if (FAILED(m_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &m_ribbonVS)) ||
            FAILED(m_device->CreateGeometryShader(gsBlob->GetBufferPointer(), gsBlob->GetBufferSize(), nullptr, &m_ribbonGS)) ||
            FAILED(m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &m_ribbonPS)))
        {
            return false;
        }

        // Particle-only runtime:
        // keep startup dependencies limited to the shaders that are still used by the viewer.
        if (!CompileShaderFromFile(L"DebugLine.hlsl", "VSMain", "vs_5_0", &vsBlob, errorText) ||
            !CompileShaderFromFile(L"DebugLine.hlsl", "PSMain", "ps_5_0", &psBlob, errorText))
        {
            MessageBoxW(m_hwnd, errorText.c_str(), L"Shader compile failed", MB_ICONERROR);
            return false;
        }

        if (FAILED(m_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &m_debugLineVS)) ||
            FAILED(m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &m_debugLinePS)))
        {
            return false;
        }

        if (!CompileShaderFromFile(L"ScalpMesh.hlsl", "VSMain", "vs_5_0", &vsBlob, errorText) ||
            !CompileShaderFromFile(L"ScalpMesh.hlsl", "PSMain", "ps_5_0", &psBlob, errorText))
        {
            MessageBoxW(m_hwnd, errorText.c_str(), L"Shader compile failed", MB_ICONERROR);
            return false;
        }

        if (FAILED(m_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &m_scalpVS)) ||
            FAILED(m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &m_scalpPS)))
        {
            return false;
        }

        if (!CompileShaderFromFile(L"HairCpuMesh.hlsl", "VSMain", "vs_5_0", &vsBlob, errorText) ||
            !CompileShaderFromFile(L"HairCpuMesh.hlsl", "PSMain", "ps_5_0", &psBlob, errorText))
        {
            MessageBoxW(m_hwnd, errorText.c_str(), L"Shader compile failed", MB_ICONERROR);
            return false;
        }

        if (FAILED(m_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &m_hairMeshVS)) ||
            FAILED(m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &m_hairMeshPS)))
        {
            return false;
        }

        if (!CompileShaderFromFile(L"HairSim.hlsl", "RootBindCS", "cs_5_0", &csBlob, errorText) ||
            FAILED(m_device->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(), nullptr, &m_rootBindCS)) ||
            !CompileShaderFromFile(L"HairSim.hlsl", "IntegrateCS", "cs_5_0", &csBlob, errorText) ||
            FAILED(m_device->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(), nullptr, &m_integrateCS)) ||
            !CompileShaderFromFile(L"HairSim.hlsl", "DistanceCS", "cs_5_0", &csBlob, errorText) ||
            FAILED(m_device->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(), nullptr, &m_distanceCS)) ||
            !CompileShaderFromFile(L"HairSim.hlsl", "BendCS", "cs_5_0", &csBlob, errorText) ||
            FAILED(m_device->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(), nullptr, &m_bendCS)) ||
            !CompileShaderFromFile(L"HairSim.hlsl", "CollisionCS", "cs_5_0", &csBlob, errorText) ||
            FAILED(m_device->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(), nullptr, &m_collisionCS)) ||
            !CompileShaderFromFile(L"HairSim.hlsl", "ClearGridCS", "cs_5_0", &csBlob, errorText) ||
            FAILED(m_device->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(), nullptr, &m_clearGridCS)) ||
            !CompileShaderFromFile(L"HairSim.hlsl", "BuildGridCS", "cs_5_0", &csBlob, errorText) ||
            FAILED(m_device->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(), nullptr, &m_buildGridCS)) ||
            !CompileShaderFromFile(L"HairSim.hlsl", "ClearHashGridCS", "cs_5_0", &csBlob, errorText) ||
            FAILED(m_device->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(), nullptr, &m_clearHashGridCS)) ||
            !CompileShaderFromFile(L"HairSim.hlsl", "BuildHashGridCS", "cs_5_0", &csBlob, errorText) ||
            FAILED(m_device->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(), nullptr, &m_buildHashGridCS)) ||
            !CompileShaderFromFile(L"HairSim.hlsl", "ClearSortedRangesCS", "cs_5_0", &csBlob, errorText) ||
            FAILED(m_device->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(), nullptr, &m_clearSortedRangesCS)) ||
            !CompileShaderFromFile(L"HairSim.hlsl", "BuildSortedEntriesCS", "cs_5_0", &csBlob, errorText) ||
            FAILED(m_device->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(), nullptr, &m_buildSortedEntriesCS)) ||
            !CompileShaderFromFile(L"HairSim.hlsl", "BitonicSortCS", "cs_5_0", &csBlob, errorText) ||
            FAILED(m_device->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(), nullptr, &m_bitonicSortCS)) ||
            !CompileShaderFromFile(L"HairSim.hlsl", "BuildSortedRangesCS", "cs_5_0", &csBlob, errorText) ||
            FAILED(m_device->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(), nullptr, &m_buildSortedRangesCS)) ||
            !CompileShaderFromFile(L"HairSim.hlsl", "SelfCollisionCS", "cs_5_0", &csBlob, errorText) ||
            FAILED(m_device->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(), nullptr, &m_selfCollisionCS)) ||
            !CompileShaderFromFile(L"HairSim.hlsl", "FollowCS", "cs_5_0", &csBlob, errorText) ||
            FAILED(m_device->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(), nullptr, &m_followCS)) ||
            !CompileShaderFromFile(L"HairIndirectArgs.hlsl", "CompactFollowerDispatchCS", "cs_5_0", &csBlob, errorText) ||
            FAILED(m_device->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(), nullptr, &m_compactFollowerDispatchCS)) ||
            !CompileShaderFromFile(L"HairIndirectArgs.hlsl", "BuildFollowDispatchArgsCS", "cs_5_0", &csBlob, errorText) ||
            FAILED(m_device->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(), nullptr, &m_buildFollowDispatchArgsCS)))
        {
            MessageBoxW(m_hwnd, errorText.c_str(), L"Shader compile failed", MB_ICONERROR);
            return false;
        }

        ComPtr<ID3DBlob> debugVsBlob;
        if (!CompileShaderFromFile(L"DebugLine.hlsl", "VSMain", "vs_5_0", &debugVsBlob, errorText))
        {
            MessageBoxW(m_hwnd, errorText.c_str(), L"Shader compile failed", MB_ICONERROR);
            return false;
        }
        const D3D11_INPUT_ELEMENT_DESC debugLayout[] = {
            { "TEXCOORD", 0, DXGI_FORMAT_R32_FLOAT, 0, offsetof(DebugLineTemplateVertex, endpointId), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
        if (FAILED(m_device->CreateInputLayout(debugLayout, 1, debugVsBlob->GetBufferPointer(), debugVsBlob->GetBufferSize(), &m_debugInputLayout)))
        {
            return false;
        }

        ComPtr<ID3DBlob> ribbonVsBlob;
        if (!CompileShaderFromFile(L"HairRibbon.hlsl", "VSMain", "vs_5_0", &ribbonVsBlob, errorText))
        {
            MessageBoxW(m_hwnd, errorText.c_str(), L"Shader compile failed", MB_ICONERROR);
            return false;
        }
        const D3D11_INPUT_ELEMENT_DESC segmentLayout[] = {
            { "PARTICLE_INDEX", 0, DXGI_FORMAT_R32_UINT, 0, offsetof(SegmentRenderVertex, particleIndex), D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "STRAND_INDEX", 0, DXGI_FORMAT_R32_UINT, 0, offsetof(SegmentRenderVertex, strandIndex), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
        if (FAILED(m_device->CreateInputLayout(segmentLayout, 2, ribbonVsBlob->GetBufferPointer(), ribbonVsBlob->GetBufferSize(), &m_segmentInputLayout)))
        {
            return false;
        }

        ComPtr<ID3DBlob> scalpVsBlob;
        if (!CompileShaderFromFile(L"ScalpMesh.hlsl", "VSMain", "vs_5_0", &scalpVsBlob, errorText))
        {
            MessageBoxW(m_hwnd, errorText.c_str(), L"Shader compile failed", MB_ICONERROR);
            return false;
        }
        const D3D11_INPUT_ELEMENT_DESC scalpLayout[] = {
            { "TEXCOORD", 0, DXGI_FORMAT_R32_UINT, 0, offsetof(ScalpVertex, scalpIndex), D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(ScalpVertex, nx), D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(ScalpVertex, r), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
        if (FAILED(m_device->CreateInputLayout(scalpLayout, 3, scalpVsBlob->GetBufferPointer(), scalpVsBlob->GetBufferSize(), &m_scalpInputLayout)))
        {
            return false;
        }

        ComPtr<ID3DBlob> hairMeshVsBlob;
        if (!CompileShaderFromFile(L"HairCpuMesh.hlsl", "VSMain", "vs_5_0", &hairMeshVsBlob, errorText))
        {
            MessageBoxW(m_hwnd, errorText.c_str(), L"Shader compile failed", MB_ICONERROR);
            return false;
        }
        const D3D11_INPUT_ELEMENT_DESC hairMeshLayout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(HairMeshVertex, x), D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(HairMeshVertex, r), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
        if (FAILED(m_device->CreateInputLayout(hairMeshLayout, 2, hairMeshVsBlob->GetBufferPointer(), hairMeshVsBlob->GetBufferSize(), &m_hairMeshInputLayout)))
        {
            return false;
        }

        return true;
    }

    void GpuHairViewer::ResetSimulation()
    {
        m_currentBufferIndex = 0;
        m_context->UpdateSubresource(m_positions[0].buffer.Get(), 0, nullptr, m_initialParticles.data(), 0, 0);
        m_context->UpdateSubresource(m_positions[1].buffer.Get(), 0, nullptr, m_initialParticles.data(), 0, 0);
        m_context->UpdateSubresource(m_previousPositions[0].buffer.Get(), 0, nullptr, m_initialParticles.data(), 0, 0);
        m_context->UpdateSubresource(m_previousPositions[1].buffer.Get(), 0, nullptr, m_initialParticles.data(), 0, 0);
        if (!m_zeroDistanceLambdas.empty())
        {
            m_context->UpdateSubresource(m_distanceLambdaBuffer.buffer.Get(), 0, nullptr, m_zeroDistanceLambdas.data(), 0, 0);
        }
        if (!m_zeroBendLambdas.empty())
        {
            m_context->UpdateSubresource(m_bendLambdaBuffer.buffer.Get(), 0, nullptr, m_zeroBendLambdas.data(), 0, 0);
        }
        const UINT gridCellCount = std::max<UINT>(m_simConfig.selfCollisionGridDimX * m_simConfig.selfCollisionGridDimY * m_simConfig.selfCollisionGridDimZ, 1u);
        std::vector<uint32_t> zeroCellCounts(gridCellCount, 0u);
        std::vector<uint32_t> zeroCellIndices(std::max<UINT>(gridCellCount * m_simConfig.selfCollisionCellCapacity, 1u), 0xFFFFFFFFu);
        m_context->UpdateSubresource(m_gridCellCountsBuffer.buffer.Get(), 0, nullptr, zeroCellCounts.data(), 0, 0);
        m_context->UpdateSubresource(m_gridCellIndicesBuffer.buffer.Get(), 0, nullptr, zeroCellIndices.data(), 0, 0);
        const UINT hashBucketCount = std::max<UINT>(m_simConfig.selfCollisionHashBucketCount, 1u);
        std::vector<uint32_t> zeroHashHeads(hashBucketCount, 0xFFFFFFFFu);
        std::vector<uint32_t> zeroParticleNext(std::max<UINT>(static_cast<UINT>(m_initialParticles.size()), 1u), 0xFFFFFFFFu);
        m_context->UpdateSubresource(m_gridHashHeadsBuffer.buffer.Get(), 0, nullptr, zeroHashHeads.data(), 0, 0);
        m_context->UpdateSubresource(m_gridParticleNextBuffer.buffer.Get(), 0, nullptr, zeroParticleNext.data(), 0, 0);
        std::vector<uint32_t> invalidSortedKeys(std::max<UINT>(m_sortedBroadphasePaddedCount, 1u), 0xFFFFFFFFu);
        std::vector<uint32_t> invalidSortedIndices(std::max<UINT>(m_sortedBroadphasePaddedCount, 1u), 0xFFFFFFFFu);
        std::vector<uint32_t> invalidSortedStarts(gridCellCount, 0xFFFFFFFFu);
        std::vector<uint32_t> invalidSortedEnds(gridCellCount, 0xFFFFFFFFu);
        m_context->UpdateSubresource(m_sortedKeysBuffer.buffer.Get(), 0, nullptr, invalidSortedKeys.data(), 0, 0);
        m_context->UpdateSubresource(m_sortedParticleIndicesBuffer.buffer.Get(), 0, nullptr, invalidSortedIndices.data(), 0, 0);
        m_context->UpdateSubresource(m_sortedCellStartsBuffer.buffer.Get(), 0, nullptr, invalidSortedStarts.data(), 0, 0);
        m_context->UpdateSubresource(m_sortedCellEndsBuffer.buffer.Get(), 0, nullptr, invalidSortedEnds.data(), 0, 0);
        UpdateAnimatedScalp(0.0f);
        m_accumulator = 0.0f;
        m_runtimeFollowerUpdatePhase = 0u;
        m_elapsedSeconds = 0.0f;
        m_lastSimulationStepMilliseconds = 0.0f;
        m_performanceSamples.clear();
        RebuildActiveSetFoundation();
        RecomputeAssetBounds();
        AutoFrameCameraToAsset(true);
        m_lastEstimatedVisibleParticles = 0u;
        m_lastReadbackParticleCount = 0u;
        m_lastVisibilitySuspect = false;
        m_lastUsedCpuVisibilityFallback = false;
        m_lastVisibilityReadbackValid = true;
        m_lastVisibilityUsedInitialPoseFallback = false;
        m_visibilityReadbackFailureCount = 0u;
        m_consecutiveInvisibleFrames = 0u;
        m_visibilityRecoveryCount = 0u;
        m_renderMode = RenderMode::StrandRibbon;
        m_widthScale = std::max(m_widthScale, 0.65f);
        QueueVisibilityRecovery();
        SyncControlsFromState();
        UpdateStatsLabel();
    }

    void GpuHairViewer::StepSingleFrame()
    {
        LARGE_INTEGER simBegin = {};
        LARGE_INTEGER simEnd = {};
        QueryPerformanceCounter(&simBegin);
        StepSimulation(m_simConfig.fixedTimeStep);
        QueryPerformanceCounter(&simEnd);
        m_elapsedSeconds += m_simConfig.fixedTimeStep;
        m_lastSimulationStepMilliseconds = static_cast<float>(simEnd.QuadPart - simBegin.QuadPart) * 1000.0f / static_cast<float>(m_qpcFrequency.QuadPart);

        PerformanceSample sample = {};
        sample.elapsedSeconds = m_elapsedSeconds;
        sample.frameMilliseconds = m_simConfig.fixedTimeStep * 1000.0f;
        sample.simulationMilliseconds = m_lastSimulationStepMilliseconds;
        sample.fps = (m_simConfig.fixedTimeStep > 1.0e-6f) ? (1.0f / m_simConfig.fixedTimeStep) : 0.0f;
        sample.guideCount = m_asset.guideStrandCount;
        sample.followerCount = m_asset.followerStrandCount;
        sample.totalStrandCount = static_cast<uint32_t>(m_asset.strands.size());
        sample.totalParticleCount = static_cast<uint32_t>(m_asset.restPoints.size());
        sample.activeSetClusterCount = m_activeSetPlan.summary.clusterCount;
        sample.activeSetSelectedClusterCount = m_activeSetPlan.summary.selectedClusterCount;
        sample.activeSetSelectedGuideCount = m_activeSetPlan.summary.selectedGuideCount;
        sample.activeSetSelectedStrandCount = m_activeSetPlan.summary.selectedStrandCount;
        sample.activeSetHighestScore = m_activeSetPlan.summary.highestScore;
        sample.activeSetAverageSelectedScore = m_activeSetPlan.summary.averageSelectedScore;
        sample.activeSetEvaluationMilliseconds = m_activeSetEvaluationMilliseconds;
        sample.activeSetBudgetScale = m_activeSetPlan.summary.budgetScale;
        sample.activeSetRecommendedSolverIterations = m_activeSetPlan.summary.recommendedSolverIterations;
        sample.fullTierCount = m_activeSetPlan.summary.fullTierCount;
        sample.reducedTierCount = m_activeSetPlan.summary.reducedTierCount;
        sample.frozenTierCount = m_activeSetPlan.summary.frozenTierCount;
        sample.cachedTierCount = m_activeSetPlan.summary.cachedTierCount;
        sample.correctiveStrandCount = m_sparseCorrectiveReport.correctedStrandCount;
        sample.correctivePointCount = m_sparseCorrectiveReport.correctedPointCount;
        sample.correctiveAveragePenetration = m_sparseCorrectiveReport.averagePenetration;
        sample.correctiveMaxPenetration = m_sparseCorrectiveReport.maxPenetration;
        sample.correctiveAverageLengthErrorBefore = m_sparseCorrectiveReport.averageLengthErrorBefore;
        sample.correctiveAverageLengthErrorAfter = m_sparseCorrectiveReport.averageLengthErrorAfter;
        m_performanceSamples.push_back(sample);
        if (m_performanceSamples.size() > 12000)
        {
            m_performanceSamples.erase(m_performanceSamples.begin(), m_performanceSamples.begin() + static_cast<std::ptrdiff_t>(m_performanceSamples.size() - 12000));
        }
        UpdateStatsLabel();
    }

    Vec3 GpuHairViewer::ApplySampleScalpSkinning(const Vec3& restPosition, const SkinWeight4& weights, float elapsedSeconds) const
    {
        const float motionBlend = m_enableScalpMotion ? 1.0f : 0.0f;
        const float neckYaw = std::sin(elapsedSeconds * 0.83f) * 0.07f * motionBlend;
        const float neckPitch = std::sin(elapsedSeconds * 1.07f) * 0.035f * motionBlend;
        const float headYaw = std::sin(elapsedSeconds * 0.91f) * 0.14f * motionBlend;
        const float headPitch = std::sin(elapsedSeconds * 1.39f) * 0.06f * motionBlend;
        const float bob = std::sin(elapsedSeconds * 1.15f) * 0.012f * motionBlend;

        const Mat4 neck = Multiply(Multiply(Translation(m_neckPivot + Vec3(0.0f, bob * 0.5f, 0.0f)), RotationY(neckYaw)), Multiply(RotationX(neckPitch), Translation(m_neckPivot * -1.0f)));
        const Vec3 headPivotWorld = TransformPoint(neck, m_scalpCenter);
        const Mat4 headLocal = Multiply(Multiply(Translation(headPivotWorld), RotationY(headYaw)), Multiply(RotationX(headPitch), Translation(headPivotWorld * -1.0f)));
        const Mat4 head = Multiply(neck, headLocal);

        Vec3 blended(0.0f, 0.0f, 0.0f);
        for (size_t i = 0; i < 4; ++i)
        {
            const float weight = weights.boneWeights[i];
            if (weight <= 0.0f)
            {
                continue;
            }
            const uint32_t boneIndex = weights.boneIndices[i];
            const Mat4& bone = (boneIndex == 0u) ? neck : head;
            blended += TransformPoint(bone, restPosition) * weight;
        }
        return blended;
    }

    void GpuHairViewer::UpdateAnimatedScalp(float /*elapsedSeconds*/)
    {
        // Particle runtime keeps scalp roots static on the GPU side.
        // No per-frame CPU skinning or readback path is used here.
    }
    namespace
    {
        template <typename TConstants>
        void FillSharedColliderConstants(TConstants& constants, const hairgpu::ColliderSet& colliders)
        {
            constants.planeColliderCount = static_cast<uint32_t>(std::min<size_t>(colliders.planes.size(), 4u));
            constants.aabbColliderCount = static_cast<uint32_t>(std::min<size_t>(colliders.aabbs.size(), 4u));
            constants.useSdfCollider = colliders.sdfGrids.empty() ? 0u : 1u;
            constants.sdfValueCount = colliders.sdfGrids.empty() ? 0u : static_cast<uint32_t>(colliders.sdfGrids.front().values.size());

            for (size_t i = 0; i < static_cast<size_t>(constants.planeColliderCount); ++i)
            {
                const hairgpu::PlaneCollider& plane = colliders.planes[i];
                const hairgpu::Vec3 n = hairgpu::Normalize(plane.normal);
                const float d = -hairgpu::Dot(n, plane.point);
                constants.planeData[i] = { n.x, n.y, n.z, d };
            }

            for (size_t i = 0; i < static_cast<size_t>(constants.aabbColliderCount); ++i)
            {
                const hairgpu::AabbCollider& aabb = colliders.aabbs[i];
                constants.aabbData[i * 2 + 0] = { aabb.minBounds.x, aabb.minBounds.y, aabb.minBounds.z, 0.0f };
                constants.aabbData[i * 2 + 1] = { aabb.maxBounds.x, aabb.maxBounds.y, aabb.maxBounds.z, 0.0f };
            }

            if (!colliders.sdfGrids.empty())
            {
                const hairgpu::SdfGridCollider& sdf = colliders.sdfGrids.front();
                constants.sdfResolution[0] = sdf.resolutionX;
                constants.sdfResolution[1] = sdf.resolutionY;
                constants.sdfResolution[2] = sdf.resolutionZ;
                constants.sdfResolution[3] = 0u;
                constants.sdfBoundsMin = { sdf.boundsMin.x, sdf.boundsMin.y, sdf.boundsMin.z, 0.0f };
                constants.sdfBoundsMax = { sdf.boundsMax.x, sdf.boundsMax.y, sdf.boundsMax.z, 0.0f };
            }
        }
    }


    void GpuHairViewer::FillHairSimConstants(SimConstantsHair& constants, float dt) const
    {
        constants = SimConstantsHair{};
        constants.totalStrandCount = static_cast<uint32_t>(m_asset.strands.size());
        constants.guideStrandCount = m_asset.guideStrandCount;
        constants.followerStrandCount = m_asset.followerStrandCount;
        constants.particlesPerStrand = m_asset.particlesPerStrand;
        constants.solverIterations = m_simConfig.solverIterations;
        constants.sphereColliderCount = static_cast<uint32_t>(std::min<size_t>(m_colliders.spheres.size(), 4u));
        constants.capsuleColliderCount = static_cast<uint32_t>(std::min<size_t>(m_colliders.capsules.size(), 4u));
        constants.enableSelfCollision = m_enableSelfCollision ? 1u : 0u;
        FillSharedColliderConstants(constants, m_colliders);
        constants.deltaTime = dt;
        constants.gravity = m_simConfig.gravity;
        constants.damping = m_simConfig.globalDamping;
        constants.segmentLength = m_simConfig.segmentLength;
        constants.bendStiffness = m_simConfig.bendStiffness;
        constants.followStiffness = m_simConfig.followStiffness;
        constants.windStrength = m_simConfig.windStrength;
        constants.floorY = m_simConfig.floorY;
        constants.elapsedTime = m_elapsedSeconds;
        constants.selfCollisionRadius = m_selfCollisionRadius;
        constants.selfCollisionStiffness = m_selfCollisionStiffness;
        constants.scalpMotionBlend = m_enableScalpMotion ? 1.0f : 0.0f;
        constants.stretchCompliance = m_simConfig.stretchCompliance;
        constants.bendCompliance = m_simConfig.bendCompliance;
        constants.rootStiffness = m_simConfig.rootStiffness;
        constants.velocityShockPropagation = m_simConfig.velocityShockPropagation;
        constants.windDirection[0] = m_simConfig.windDirection.x;
        constants.windDirection[1] = m_simConfig.windDirection.y;
        constants.windDirection[2] = m_simConfig.windDirection.z;
        constants.gridCellSize = m_simConfig.selfCollisionCellSize;
        constants.gridDimX = m_simConfig.selfCollisionGridDimX;
        constants.gridDimY = m_simConfig.selfCollisionGridDimY;
        constants.gridDimZ = m_simConfig.selfCollisionGridDimZ;
        constants.gridCellCapacity = m_simConfig.selfCollisionCellCapacity;
        constants.gridOrigin[0] = m_simConfig.selfCollisionGridOrigin.x;
        constants.gridOrigin[1] = m_simConfig.selfCollisionGridOrigin.y;
        constants.gridOrigin[2] = m_simConfig.selfCollisionGridOrigin.z;
        constants.enableGridSelfCollision = m_enableGridSelfCollision ? 1u : 0u;
        constants.looseConstraintStiffness = m_simConfig.looseConstraintStiffness;
        constants.looseConstraintMaxDistance = m_simConfig.looseConstraintMaxDistance;
        constants.supportCollisionRadius = m_simConfig.supportCollisionRadius;
        constants.supportCollisionStiffness = m_simConfig.supportCollisionStiffness;
        constants.tangentialFriction = m_simConfig.tangentialFriction;
        constants.lateralFriction = m_simConfig.lateralFriction;
        constants.freezeBackThreshold = m_simConfig.freezeBackThreshold;
        constants.freezeBlend = m_simConfig.freezeBlend;
        constants.broadphaseMode = m_selfCollisionBroadphaseMode;
        constants.broadphaseHashBucketCount = std::max<uint32_t>(m_simConfig.selfCollisionHashBucketCount, 1u);
        constants.sortedPaddedEntryCount = std::max<uint32_t>(m_sortedBroadphasePaddedCount, 1u);
        constants.enableRuntimeAngularBend = m_enableRuntimeAngularBend ? 1u : 0u;
        constants.angularBendRadians = m_runtimeAngularBendRadians;
        constants.angularBendCompliance = m_runtimeAngularBendCompliance;
        constants.angularBendStiffness = m_runtimeAngularBendStiffness;
        constants.angularBendDamping = m_runtimeAngularBendDamping;
        for (size_t i = 0; i < static_cast<size_t>(constants.sphereColliderCount); ++i)
        {
            const SphereCollider& s = m_colliders.spheres[i];
            constants.sphereData[i] = { s.center.x, s.center.y, s.center.z, s.radius };
        }
        for (size_t i = 0; i < static_cast<size_t>(constants.capsuleColliderCount); ++i)
        {
            const CapsuleCollider& c = m_colliders.capsules[i];
            constants.capsuleData[i * 2 + 0] = { c.pointA.x, c.pointA.y, c.pointA.z, c.radiusA };
            constants.capsuleData[i * 2 + 1] = { c.pointB.x, c.pointB.y, c.pointB.z, c.radiusB };
        }
        const bool activeSetReady = m_enableActiveSetRuntime && !m_activeSetPlan.recommendedSimulationTiers.empty();
        const uint32_t reducedOrCachedCount = m_activeSetPlan.summary.reducedTierCount + m_activeSetPlan.summary.cachedTierCount;
        constants.followerUpdateStride = activeSetReady ? ((m_activeSetPlan.summary.cachedTierCount > 0u) ? 4u : ((reducedOrCachedCount > 0u) ? 2u : 1u)) : 1u;
        constants.followerUpdatePhase = (constants.followerUpdateStride > 1u) ? (m_runtimeFollowerUpdatePhase % constants.followerUpdateStride) : 0u;
        constants.useCompactedGuideDispatch = 0u;
        constants.compactedGuideCount = 0u;
        constants.useCompactedFollowerDispatch = 0u;
        constants.compactedFollowerCount = 0u;
    }

    void GpuHairViewer::FillActiveSetSimConstants(SimConstantsActiveSet& constants, float dt) const
    {
        constants = SimConstantsActiveSet{};
        constants.totalStrandCount = static_cast<uint32_t>(m_asset.strands.size());
        constants.guideStrandCount = m_asset.guideStrandCount;
        constants.followerStrandCount = m_asset.followerStrandCount;
        constants.particlesPerStrand = m_asset.particlesPerStrand;
        constants.solverIterations = m_simConfig.solverIterations;
        constants.sphereColliderCount = static_cast<uint32_t>(std::min<size_t>(m_colliders.spheres.size(), 4u));
        constants.capsuleColliderCount = static_cast<uint32_t>(std::min<size_t>(m_colliders.capsules.size(), 4u));
        constants.enableSelfCollision = m_enableSelfCollision ? 1u : 0u;
        FillSharedColliderConstants(constants, m_colliders);
        constants.deltaTime = dt;
        constants.gravity = m_simConfig.gravity;
        constants.damping = m_simConfig.globalDamping;
        constants.segmentLength = m_simConfig.segmentLength;
        constants.bendStiffness = m_simConfig.bendStiffness;
        constants.followStiffness = m_simConfig.followStiffness;
        constants.windStrength = m_simConfig.windStrength;
        constants.floorY = m_simConfig.floorY;
        constants.elapsedTime = m_elapsedSeconds;
        constants.selfCollisionRadius = m_selfCollisionRadius;
        constants.selfCollisionStiffness = m_selfCollisionStiffness;
        constants.scalpMotionBlend = m_enableScalpMotion ? 1.0f : 0.0f;
        constants.stretchCompliance = m_simConfig.stretchCompliance;
        constants.bendCompliance = m_simConfig.bendCompliance;
        constants.rootStiffness = m_simConfig.rootStiffness;
        constants.velocityShockPropagation = m_simConfig.velocityShockPropagation;
        constants.windDirection[0] = m_simConfig.windDirection.x;
        constants.windDirection[1] = m_simConfig.windDirection.y;
        constants.windDirection[2] = m_simConfig.windDirection.z;
        constants.gridCellSize = m_simConfig.selfCollisionCellSize;
        constants.gridDimX = m_simConfig.selfCollisionGridDimX;
        constants.gridDimY = m_simConfig.selfCollisionGridDimY;
        constants.gridDimZ = m_simConfig.selfCollisionGridDimZ;
        constants.gridCellCapacity = m_simConfig.selfCollisionCellCapacity;
        constants.gridOrigin[0] = m_simConfig.selfCollisionGridOrigin.x;
        constants.gridOrigin[1] = m_simConfig.selfCollisionGridOrigin.y;
        constants.gridOrigin[2] = m_simConfig.selfCollisionGridOrigin.z;
        constants.enableGridSelfCollision = m_enableGridSelfCollision ? 1u : 0u;
        constants.looseConstraintStiffness = m_simConfig.looseConstraintStiffness;
        constants.looseConstraintMaxDistance = m_simConfig.looseConstraintMaxDistance;
        constants.supportCollisionRadius = m_simConfig.supportCollisionRadius;
        constants.supportCollisionStiffness = m_simConfig.supportCollisionStiffness;
        constants.tangentialFriction = m_simConfig.tangentialFriction;
        constants.lateralFriction = m_simConfig.lateralFriction;
        constants.freezeBackThreshold = m_simConfig.freezeBackThreshold;
        constants.freezeBlend = m_simConfig.freezeBlend;
        constants.broadphaseMode = m_selfCollisionBroadphaseMode;
        constants.broadphaseHashBucketCount = std::max<uint32_t>(m_simConfig.selfCollisionHashBucketCount, 1u);
        constants.sortedPaddedEntryCount = std::max<uint32_t>(m_sortedBroadphasePaddedCount, 1u);
        constants.enableRuntimeAngularBend = m_enableRuntimeAngularBend ? 1u : 0u;
        constants.angularBendRadians = m_runtimeAngularBendRadians;
        constants.angularBendCompliance = m_runtimeAngularBendCompliance;
        constants.angularBendStiffness = m_runtimeAngularBendStiffness;
        constants.angularBendDamping = m_runtimeAngularBendDamping;
        for (size_t i = 0; i < static_cast<size_t>(constants.sphereColliderCount); ++i)
        {
            const SphereCollider& s = m_colliders.spheres[i];
            constants.sphereData[i] = { s.center.x, s.center.y, s.center.z, s.radius };
        }
        for (size_t i = 0; i < static_cast<size_t>(constants.capsuleColliderCount); ++i)
        {
            const CapsuleCollider& c = m_colliders.capsules[i];
            constants.capsuleData[i * 2 + 0] = { c.pointA.x, c.pointA.y, c.pointA.z, c.radiusA };
            constants.capsuleData[i * 2 + 1] = { c.pointB.x, c.pointB.y, c.pointB.z, c.radiusB };
        }
        const bool activeSetReady = m_enableActiveSetRuntime && !m_activeSetPlan.recommendedSimulationTiers.empty();
        const uint32_t reducedOrCachedCount = m_activeSetPlan.summary.reducedTierCount + m_activeSetPlan.summary.cachedTierCount;
        constants.followerUpdateStride = activeSetReady ? ((m_activeSetPlan.summary.cachedTierCount > 0u) ? 4u : ((reducedOrCachedCount > 0u) ? 2u : 1u)) : 1u;
        constants.followerUpdatePhase = (constants.followerUpdateStride > 1u) ? (m_runtimeFollowerUpdatePhase % constants.followerUpdateStride) : 0u;
        constants.useCompactedFollowerDispatch = 0u;
        constants.compactedFollowerCount = 0u;
    }

    void GpuHairViewer::FillScalpSimConstants(SimConstantsScalp& constants, float dt) const
    {
        constants = SimConstantsScalp{};
        constants.totalStrandCount = static_cast<uint32_t>(m_asset.strands.size());
        constants.guideStrandCount = m_asset.guideStrandCount;
        constants.followerStrandCount = m_asset.followerStrandCount;
        constants.particlesPerStrand = m_asset.particlesPerStrand;
        constants.solverIterations = m_simConfig.solverIterations;
        constants.sphereColliderCount = static_cast<uint32_t>(std::min<size_t>(m_colliders.spheres.size(), 4u));
        constants.capsuleColliderCount = static_cast<uint32_t>(std::min<size_t>(m_colliders.capsules.size(), 4u));
        constants.enableSelfCollision = m_enableSelfCollision ? 1u : 0u;
        FillSharedColliderConstants(constants, m_colliders);
        constants.deltaTime = dt;
        constants.gravity = m_simConfig.gravity;
        constants.damping = m_simConfig.globalDamping;
        constants.segmentLength = m_simConfig.segmentLength;
        constants.bendStiffness = m_simConfig.bendStiffness;
        constants.followStiffness = m_simConfig.followStiffness;
        constants.windStrength = m_simConfig.windStrength;
        constants.floorY = m_simConfig.floorY;
        constants.elapsedTime = m_elapsedSeconds;
        constants.selfCollisionRadius = m_selfCollisionRadius;
        constants.selfCollisionStiffness = m_selfCollisionStiffness;
        constants.scalpMotionBlend = m_enableScalpMotion ? 1.0f : 0.0f;
        constants.scalpCenter[0] = m_scalpCenter.x;
        constants.scalpCenter[1] = m_scalpCenter.y;
        constants.scalpCenter[2] = m_scalpCenter.z;
        constants.scalpCenter[3] = 0.0f;
        constants.neckPivot[0] = m_neckPivot.x;
        constants.neckPivot[1] = m_neckPivot.y;
        constants.neckPivot[2] = m_neckPivot.z;
        constants.neckPivot[3] = 0.0f;
        constants.stretchCompliance = m_simConfig.stretchCompliance;
        constants.bendCompliance = m_simConfig.bendCompliance;
        constants.rootStiffness = m_simConfig.rootStiffness;
        constants.velocityShockPropagation = m_simConfig.velocityShockPropagation;
        constants.windDirection[0] = m_simConfig.windDirection.x;
        constants.windDirection[1] = m_simConfig.windDirection.y;
        constants.windDirection[2] = m_simConfig.windDirection.z;
        constants.gridCellSize = m_simConfig.selfCollisionCellSize;
        constants.gridDimX = m_simConfig.selfCollisionGridDimX;
        constants.gridDimY = m_simConfig.selfCollisionGridDimY;
        constants.gridDimZ = m_simConfig.selfCollisionGridDimZ;
        constants.gridCellCapacity = m_simConfig.selfCollisionCellCapacity;
        constants.gridOrigin[0] = m_simConfig.selfCollisionGridOrigin.x;
        constants.gridOrigin[1] = m_simConfig.selfCollisionGridOrigin.y;
        constants.gridOrigin[2] = m_simConfig.selfCollisionGridOrigin.z;
        constants.enableGridSelfCollision = m_enableGridSelfCollision ? 1u : 0u;
        constants.looseConstraintStiffness = m_simConfig.looseConstraintStiffness;
        constants.looseConstraintMaxDistance = m_simConfig.looseConstraintMaxDistance;
        constants.supportCollisionRadius = m_simConfig.supportCollisionRadius;
        constants.supportCollisionStiffness = m_simConfig.supportCollisionStiffness;
        constants.tangentialFriction = m_simConfig.tangentialFriction;
        constants.lateralFriction = m_simConfig.lateralFriction;
        constants.freezeBackThreshold = m_simConfig.freezeBackThreshold;
        constants.freezeBlend = m_simConfig.freezeBlend;
        constants.broadphaseMode = m_selfCollisionBroadphaseMode;
        constants.broadphaseHashBucketCount = std::max<uint32_t>(m_simConfig.selfCollisionHashBucketCount, 1u);
        constants.sortedPaddedEntryCount = std::max<uint32_t>(m_sortedBroadphasePaddedCount, 1u);
        constants.enableRuntimeAngularBend = m_enableRuntimeAngularBend ? 1u : 0u;
        constants.angularBendRadians = m_runtimeAngularBendRadians;
        constants.angularBendCompliance = m_runtimeAngularBendCompliance;
        constants.angularBendStiffness = m_runtimeAngularBendStiffness;
        constants.angularBendDamping = m_runtimeAngularBendDamping;
        for (size_t i = 0; i < static_cast<size_t>(constants.sphereColliderCount); ++i)
        {
            const SphereCollider& s = m_colliders.spheres[i];
            constants.sphereData[i] = { s.center.x, s.center.y, s.center.z, s.radius };
        }
        for (size_t i = 0; i < static_cast<size_t>(constants.capsuleColliderCount); ++i)
        {
            const CapsuleCollider& c = m_colliders.capsules[i];
            constants.capsuleData[i * 2 + 0] = { c.pointA.x, c.pointA.y, c.pointA.z, c.radiusA };
            constants.capsuleData[i * 2 + 1] = { c.pointB.x, c.pointB.y, c.pointB.z, c.radiusB };
        }
    }

    void GpuHairViewer::FillRenderConstants(RenderConstants& constants, const Mat4& viewProjection) const
    {
        constants = RenderConstants{};
        for (int i = 0; i < 16; ++i)
        {
            constants.viewProjection[i] = viewProjection.m[i];
        }
        constants.cameraPosition[0] = m_cameraPosition.x;
        constants.cameraPosition[1] = m_cameraPosition.y;
        constants.cameraPosition[2] = m_cameraPosition.z;
        constants.cameraPosition[3] = 1.0f;
        constants.lightDirection[0] = -0.4f;
        constants.lightDirection[1] = 0.9f;
        constants.lightDirection[2] = 0.2f;
        constants.lightDirection[3] = 0.0f;
        constants.widthScale = m_widthScale;
        constants.debugColorMode = static_cast<uint32_t>(m_debugColorMode);
        constants.guideStrandCount = m_asset.guideStrandCount;
        constants.particlesPerStrand = m_asset.particlesPerStrand;
        constants.totalStrandCount = static_cast<uint32_t>(m_asset.strands.size());
        constants.tubeRingSides = m_strandTubeRingSides;
        constants.renderFollowerStride = std::max<uint32_t>(1u, (m_activeSetPlan.summary.cachedTierCount > 0u) ? 4u : ((m_activeSetPlan.summary.reducedTierCount > 0u) ? 2u : 1u));
        constants.renderGuideOnlyAboveTier = 4u;
        constants.renderReducedWidthBoost = 0.82f;
        constants.renderHiddenWidthThreshold = 0.0f;
        constants.renderStrandAlpha = 1.0f;
        constants.renderPadding0 = 0.0f;
    }

    void GpuHairViewer::SyncActiveSetGpuBuffers()
    {
        const UINT totalStrands = static_cast<UINT>(std::max<size_t>(m_asset.strands.size(), 1u));
        const UINT guideCount = std::max<UINT>(m_asset.guideStrandCount, 1u);
        const UINT followerCount = std::max<UINT>(m_asset.followerStrandCount, 1u);
        std::vector<uint32_t> renderTiers(totalStrands, static_cast<uint32_t>(hairgpu::SimTier::Reduced));
        std::vector<uint32_t> guideTiers(guideCount, static_cast<uint32_t>(hairgpu::SimTier::Full));
        std::vector<uint32_t> followerTiers(followerCount, static_cast<uint32_t>(hairgpu::SimTier::Reduced));

        for (uint32_t strandIndex = 0u; strandIndex < totalStrands; ++strandIndex)
        {
            const bool isGuide = strandIndex < m_asset.guideStrandCount;
            uint32_t tier = isGuide ? static_cast<uint32_t>(hairgpu::SimTier::Full)
                                    : static_cast<uint32_t>(hairgpu::SimTier::Reduced);
            if (strandIndex < m_activeSetPlan.recommendedSimulationTiers.size())
            {
                tier = static_cast<uint32_t>(m_activeSetPlan.recommendedSimulationTiers[strandIndex]);
            }
            renderTiers[strandIndex] = tier;
            if (isGuide)
            {
                guideTiers[strandIndex] = tier;
            }
            else if ((strandIndex - m_asset.guideStrandCount) < followerTiers.size())
            {
                followerTiers[strandIndex - m_asset.guideStrandCount] = tier;
            }
        }

        std::vector<uint32_t> compactedGuideIndices(guideCount, 0u);
        std::vector<uint32_t> compactedFollowerIndices(followerCount, 0u);
        uint32_t compactedGuideCount = 0u;
        uint32_t compactedFollowerCount = 0u;
        const uint32_t runtimeStride = std::max<uint32_t>(1u, (m_activeSetPlan.summary.cachedTierCount > 0u) ? 4u : ((m_activeSetPlan.summary.reducedTierCount > 0u) ? 2u : 1u));
        const uint32_t runtimePhase = (runtimeStride > 1u) ? (m_runtimeFollowerUpdatePhase % runtimeStride) : 0u;

        for (uint32_t guideIndex = 0u; guideIndex < m_asset.guideStrandCount; ++guideIndex)
        {
            if (guideTiers[guideIndex] == static_cast<uint32_t>(hairgpu::SimTier::Full))
            {
                compactedGuideIndices[compactedGuideCount++] = guideIndex;
            }
        }
        for (uint32_t followerIndex = 0u; followerIndex < m_asset.followerStrandCount; ++followerIndex)
        {
            const uint32_t tier = followerTiers[followerIndex];
            bool active = (tier == static_cast<uint32_t>(hairgpu::SimTier::Full));
            if (!active && tier == static_cast<uint32_t>(hairgpu::SimTier::Reduced))
            {
                active = ((followerIndex + runtimePhase) % std::max<uint32_t>(runtimeStride, 2u)) == 0u;
            }
            if (!active && tier == static_cast<uint32_t>(hairgpu::SimTier::Cached))
            {
                active = ((followerIndex + runtimePhase) % std::max<uint32_t>(runtimeStride, 4u)) == 0u;
            }
            if (active)
            {
                compactedFollowerIndices[compactedFollowerCount++] = followerIndex;
            }
        }
        std::array<uint32_t, 4> guideCounters = { compactedGuideCount, 0u, 0u, 0u };
        std::array<uint32_t, 4> followerCounters = { compactedFollowerCount, 0u, 0u, 0u };

        if (m_renderStrandTiersBuffer.buffer)
        {
            m_context->UpdateSubresource(m_renderStrandTiersBuffer.buffer.Get(), 0, nullptr, renderTiers.data(), 0, 0);
        }
        if (m_guideTierAssignmentsBuffer.buffer)
        {
            m_context->UpdateSubresource(m_guideTierAssignmentsBuffer.buffer.Get(), 0, nullptr, guideTiers.data(), 0, 0);
        }
        if (m_followerTierAssignmentsBuffer.buffer)
        {
            m_context->UpdateSubresource(m_followerTierAssignmentsBuffer.buffer.Get(), 0, nullptr, followerTiers.data(), 0, 0);
        }
        if (m_compactedGuideIndicesBuffer.buffer)
        {
            m_context->UpdateSubresource(m_compactedGuideIndicesBuffer.buffer.Get(), 0, nullptr, compactedGuideIndices.data(), 0, 0);
        }
        if (m_compactedGuideCounterBuffer.buffer)
        {
            m_context->UpdateSubresource(m_compactedGuideCounterBuffer.buffer.Get(), 0, nullptr, guideCounters.data(), 0, 0);
        }
        if (m_compactedFollowerIndicesBuffer.buffer)
        {
            m_context->UpdateSubresource(m_compactedFollowerIndicesBuffer.buffer.Get(), 0, nullptr, compactedFollowerIndices.data(), 0, 0);
        }
        if (m_compactedFollowerCounterBuffer.buffer)
        {
            m_context->UpdateSubresource(m_compactedFollowerCounterBuffer.buffer.Get(), 0, nullptr, followerCounters.data(), 0, 0);
        }
    }

    void GpuHairViewer::RebuildActiveSetFoundation()
    {
        m_activeSetClusters = hairgpu::BuildActiveSetClusters(m_asset, m_activeSetClusterSettings);
        m_activeSetPlan = {};
        m_activeSetGuideContactScores.clear();
        m_activeSetGuideUncertainty.clear();
        m_activeSetEvaluationMilliseconds = 0.0f;
        m_sparseCorrectiveReport = {};

        const uint32_t guideCount = std::min<uint32_t>(m_asset.guideStrandCount, static_cast<uint32_t>(m_asset.strands.size()));
        m_activeSetPreviousGuideRoots.assign(guideCount, hairgpu::Vec3(0.0f, 0.0f, 0.0f));
        for (uint32_t guideIndex = 0u; guideIndex < guideCount; ++guideIndex)
        {
            const hairgpu::StrandRange& strand = m_asset.strands[guideIndex];
            if (strand.pointCount > 0u && strand.pointOffset < m_asset.restPoints.size())
            {
                m_activeSetPreviousGuideRoots[guideIndex] = m_asset.restPoints[strand.pointOffset];
            }
        }

        std::wstringstream ss;
        ss << L"Ready (clusters=" << m_activeSetClusters.size() << L")";
        m_activeSetLastStatus = ss.str();
    }

    std::vector<hairgpu::Vec3> GpuHairViewer::BuildGuideRootPositionsFromParticles(const std::vector<GpuParticle>& particles) const
    {
        const uint32_t guideCount = std::min<uint32_t>(m_asset.guideStrandCount, static_cast<uint32_t>(m_asset.strands.size()));
        std::vector<hairgpu::Vec3> roots(guideCount, hairgpu::Vec3(0.0f, 0.0f, 0.0f));
        for (uint32_t guideIndex = 0u; guideIndex < guideCount; ++guideIndex)
        {
            const hairgpu::StrandRange& strand = m_asset.strands[guideIndex];
            if (strand.pointCount > 0u && strand.pointOffset < particles.size())
            {
                const GpuParticle& rootParticle = particles[strand.pointOffset];
                roots[guideIndex] = hairgpu::Vec3(rootParticle.x, rootParticle.y, rootParticle.z);
            }
            else if (strand.pointCount > 0u && strand.pointOffset < m_asset.restPoints.size())
            {
                roots[guideIndex] = m_asset.restPoints[strand.pointOffset];
            }
        }
        return roots;
    }

    std::vector<float> GpuHairViewer::BuildGuideContactScoresFromParticles(const std::vector<GpuParticle>& particles) const
    {
        const uint32_t guideCount = std::min<uint32_t>(m_asset.guideStrandCount, static_cast<uint32_t>(m_asset.strands.size()));
        std::vector<float> scores(guideCount, 0.0f);
        for (uint32_t guideIndex = 0u; guideIndex < guideCount; ++guideIndex)
        {
            const hairgpu::StrandRange& strand = m_asset.strands[guideIndex];
            if (strand.pointCount == 0u)
            {
                continue;
            }

            const uint32_t sampleOffsets[3] = { 0u, strand.pointCount / 2u, strand.pointCount - 1u };
            float bestScore = 0.0f;
            for (uint32_t sampleOffset : sampleOffsets)
            {
                const uint32_t particleIndex = strand.pointOffset + std::min<uint32_t>(sampleOffset, strand.pointCount - 1u);
                if (particleIndex >= particles.size())
                {
                    continue;
                }

                const GpuParticle& particle = particles[particleIndex];
                const hairgpu::Vec3 p(particle.x, particle.y, particle.z);
                float proximityScore = hairgpu::Clamp((0.030f - (p.y - m_simConfig.floorY)) / 0.030f, 0.0f, 1.0f);
                for (const hairgpu::SphereCollider& sphere : m_colliders.spheres)
                {
                    const float surfaceDistance = DistanceToSphereSurface(p, sphere);
                    proximityScore = std::max(proximityScore, hairgpu::Clamp((0.040f - surfaceDistance) / 0.040f, 0.0f, 1.0f));
                }
                for (const hairgpu::CapsuleCollider& capsule : m_colliders.capsules)
                {
                    const float surfaceDistance = DistanceToCapsuleSurface(p, capsule);
                    proximityScore = std::max(proximityScore, hairgpu::Clamp((0.045f - surfaceDistance) / 0.045f, 0.0f, 1.0f));
                }
                bestScore = std::max(bestScore, proximityScore);
            }
            scores[guideIndex] = bestScore;
        }
        return scores;
    }

    void GpuHairViewer::UpdateActiveSetRuntimeFromParticles(const std::vector<GpuParticle>& particles)
    {
        if (!m_enableActiveSetRuntime || m_activeSetClusters.empty())
        {
            m_activeSetPlan = {};
            m_activeSetLastStatus = L"Disabled";
            m_activeSetEvaluationMilliseconds = 0.0f;
            SyncActiveSetGpuBuffers();
            if (IsActiveSetRiskOverlayMode())
            {
                UpdateScalpOverlayVertexBuffer();
            }
            return;
        }

        const uint32_t guideCount = std::min<uint32_t>(m_asset.guideStrandCount, static_cast<uint32_t>(m_asset.strands.size()));
        if (guideCount == 0u)
        {
            m_activeSetPlan = {};
            m_activeSetLastStatus = L"No guides";
            m_activeSetEvaluationMilliseconds = 0.0f;
            SyncActiveSetGpuBuffers();
            if (IsActiveSetRiskOverlayMode())
            {
                UpdateScalpOverlayVertexBuffer();
            }
            return;
        }

        const std::vector<hairgpu::Vec3> currentGuideRoots = BuildGuideRootPositionsFromParticles(particles);
        if (m_activeSetPreviousGuideRoots.size() != currentGuideRoots.size())
        {
            m_activeSetPreviousGuideRoots = currentGuideRoots;
        }
        m_activeSetGuideContactScores = BuildGuideContactScoresFromParticles(particles);
        m_activeSetGuideUncertainty.assign(guideCount, 0.0f);
        const float safeDt = std::max(m_simConfig.fixedTimeStep, 1.0e-4f);
        for (uint32_t guideIndex = 0u; guideIndex < guideCount; ++guideIndex)
        {
            const hairgpu::StrandRange& strand = m_asset.strands[guideIndex];
            const hairgpu::Vec3& currentRoot = currentGuideRoots[guideIndex];
            const hairgpu::Vec3& previousRoot = m_activeSetPreviousGuideRoots[guideIndex];
            const hairgpu::Vec3 restRoot = (strand.pointCount > 0u && strand.pointOffset < m_asset.restPoints.size())
                ? m_asset.restPoints[strand.pointOffset]
                : hairgpu::Vec3(0.0f, 0.0f, 0.0f);
            const float velocityProxy = hairgpu::Clamp(hairgpu::Length(currentRoot - previousRoot) / safeDt / 0.30f, 0.0f, 1.0f);
            const float displacementProxy = hairgpu::Clamp(hairgpu::Length(currentRoot - restRoot) / 0.030f, 0.0f, 1.0f);
            const float contactProxy = (guideIndex < m_activeSetGuideContactScores.size()) ? m_activeSetGuideContactScores[guideIndex] : 0.0f;
            m_activeSetGuideUncertainty[guideIndex] = hairgpu::Clamp(velocityProxy * 0.45f + displacementProxy * 0.30f + contactProxy * 0.25f, 0.0f, 1.0f);
        }

        hairgpu::ActiveSetRuntimeInput runtimeInput = {};
        runtimeInput.currentGuideRoots = currentGuideRoots;
        runtimeInput.previousGuideRoots = m_activeSetPreviousGuideRoots;
        runtimeInput.guideUncertainty = m_activeSetGuideUncertainty;
        runtimeInput.guideContactScores = m_activeSetGuideContactScores;
        runtimeInput.cameraPosition = m_cameraPosition;
        runtimeInput.cameraForward = hairgpu::Normalize(m_cameraTarget - m_cameraPosition);
        runtimeInput.fixedTimeStep = safeDt;
        m_activeSetBudget.previousFrameMilliseconds = m_lastSimulationStepMilliseconds;

        LARGE_INTEGER begin = {};
        LARGE_INTEGER end = {};
        QueryPerformanceCounter(&begin);
        hairgpu::ActiveSetPlan plan;
        std::string error;
        const bool ok = hairgpu::EvaluateActiveSetPlan(m_asset,
                                                       m_activeSetClusters,
                                                       runtimeInput,
                                                       m_activeSetScoringSettings,
                                                       m_activeSetBudget,
                                                       plan,
                                                       error);
        QueryPerformanceCounter(&end);
        m_activeSetEvaluationMilliseconds = static_cast<float>(end.QuadPart - begin.QuadPart) * 1000.0f / static_cast<float>(m_qpcFrequency.QuadPart);

        if (ok)
        {
            m_activeSetPlan = std::move(plan);
            if (m_activeSetPlan.summary.recommendedSolverIterations > 0u)
            {
                m_simConfig.solverIterations = std::max<uint32_t>(1u, m_activeSetPlan.summary.recommendedSolverIterations);
            }
            std::wstringstream ss;
            ss << L"OK  clusters=" << m_activeSetPlan.summary.selectedClusterCount
               << L" guides=" << m_activeSetPlan.summary.selectedGuideCount
               << L" strands=" << m_activeSetPlan.summary.selectedStrandCount
               << L"  budgetScale=" << std::fixed << std::setprecision(2) << m_activeSetPlan.summary.budgetScale
               << L"  solver=" << m_activeSetPlan.summary.recommendedSolverIterations;
            m_activeSetLastStatus = ss.str();
        }
        else
        {
            m_activeSetPlan = std::move(plan);
            m_activeSetLastStatus = Utf8ToWide(error);
        }

        m_activeSetPreviousGuideRoots = currentGuideRoots;
        SyncActiveSetGpuBuffers();
        if (IsActiveSetRiskOverlayMode())
        {
            UpdateScalpOverlayVertexBuffer();
        }
    }

    void GpuHairViewer::StepSimulation(float dt)
    {
        GpuTimingFrame* gpuTimingFrame = (m_activeGpuTimingFrameIndex >= 0 && static_cast<size_t>(m_activeGpuTimingFrameIndex) < m_gpuTimingFrames.size())
            ? &m_gpuTimingFrames[static_cast<size_t>(m_activeGpuTimingFrameIndex)]
            : nullptr;
        if (gpuTimingFrame && gpuTimingFrame->issued)
        {
            m_context->End(gpuTimingFrame->simStart.Get());
        }

        UpdateAnimatedScalp(m_elapsedSeconds);

        if (!m_zeroDistanceLambdas.empty())
        {
            m_context->UpdateSubresource(m_distanceLambdaBuffer.buffer.Get(), 0, nullptr, m_zeroDistanceLambdas.data(), 0, 0);
        }
        if (!m_zeroBendLambdas.empty())
        {
            m_context->UpdateSubresource(m_bendLambdaBuffer.buffer.Get(), 0, nullptr, m_zeroBendLambdas.data(), 0, 0);
        }

        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (SUCCEEDED(m_context->Map(m_simConstantsHair.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            SimConstantsHair constants = {};
            FillHairSimConstants(constants, dt);
            std::memcpy(mapped.pData, &constants, sizeof(constants));
            m_context->Unmap(m_simConstantsHair.Get(), 0);
        }
        if (SUCCEEDED(m_context->Map(m_simConstantsActiveSet.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            SimConstantsActiveSet constants = {};
            FillActiveSetSimConstants(constants, dt);
            std::memcpy(mapped.pData, &constants, sizeof(constants));
            m_context->Unmap(m_simConstantsActiveSet.Get(), 0);
        }
        if (SUCCEEDED(m_context->Map(m_simConstantsScalp.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            SimConstantsScalp constants = {};
            FillScalpSimConstants(constants, dt);
            std::memcpy(mapped.pData, &constants, sizeof(constants));
            m_context->Unmap(m_simConstantsScalp.Get(), 0);
        }

        ID3D11Buffer* simCb = m_simConstantsHair.Get();
        const UINT totalParticleCount = static_cast<UINT>(m_asset.restPoints.size());
        const UINT totalStrandCount = static_cast<UINT>(m_asset.strands.size());
        const UINT guideStrandCount = m_asset.guideStrandCount;
        const UINT followerStrandCount = m_asset.followerStrandCount;
        const UINT guideParticleGroups = DispatchGroups(guideStrandCount * m_asset.particlesPerStrand, 64u);
        const UINT guideStrandGroups = DispatchGroups(guideStrandCount, 64u);
        const UINT followerGroups = DispatchGroups(followerStrandCount, 64u);
        const UINT gridCellCount = std::max<UINT>(m_simConfig.selfCollisionGridDimX * m_simConfig.selfCollisionGridDimY * m_simConfig.selfCollisionGridDimZ, 1u);
        const UINT gridCellGroups = DispatchGroups(gridCellCount, 64u);
        const UINT sortedEntryGroups = DispatchGroups(std::max<UINT>(m_sortedBroadphasePaddedCount, 1u), 64u);
        const UINT bitonicGroups = DispatchGroups(std::max<UINT>(m_sortedBroadphasePaddedCount, 1u), 256u);

        ID3D11ShaderResourceView* bindSrvs[] = {
            m_scalpPositions.srv.Get(),
            m_scalpTrianglesBuffer.srv.Get(),
            m_rootBindingsBuffer.srv.Get()
        };
        ID3D11UnorderedAccessView* bindUavs[] = {
            m_positions[m_currentBufferIndex].uav.Get(),
            m_previousPositions[m_currentBufferIndex].uav.Get()
        };
        m_context->CSSetConstantBuffers(0, 1, &simCb);
        m_context->CSSetShaderResources(4, 3, bindSrvs);
        m_context->CSSetUnorderedAccessViews(0, 2, bindUavs, nullptr);
        m_context->CSSetShader(m_rootBindCS.Get(), nullptr, 0);
        m_context->Dispatch(DispatchGroups(totalStrandCount, 64u), 1, 1);

        ID3D11ShaderResourceView* nullBindSrvs[3] = {};
        ID3D11UnorderedAccessView* nullBindUavs[2] = {};
        m_context->CSSetShader(nullptr, nullptr, 0);
        m_context->CSSetShaderResources(4, 3, nullBindSrvs);
        m_context->CSSetUnorderedAccessViews(0, 2, nullBindUavs, nullptr);

        const uint32_t nextBufferIndex = 1u - m_currentBufferIndex;
        ID3D11ShaderResourceView* integrateSrvs[] = {
            m_positions[m_currentBufferIndex].srv.Get(),
            m_previousPositions[m_currentBufferIndex].srv.Get(),
            m_restPositions.srv.Get(),
            m_followBuffer.srv.Get(),
            m_scalpPositions.srv.Get(),
            m_scalpTrianglesBuffer.srv.Get(),
            m_rootBindingsBuffer.srv.Get()
        };
        ID3D11UnorderedAccessView* broadphaseUav4 = nullptr;
        ID3D11UnorderedAccessView* broadphaseUav5 = nullptr;
        ID3D11UnorderedAccessView* broadphaseUav6 = nullptr;
        ID3D11UnorderedAccessView* broadphaseUav7 = nullptr;
        if (m_selfCollisionBroadphaseMode == 0u)
        {
            broadphaseUav4 = m_gridCellCountsBuffer.uav.Get();
            broadphaseUav5 = m_gridCellIndicesBuffer.uav.Get();
        }
        else if (m_selfCollisionBroadphaseMode == 3u)
        {
            broadphaseUav4 = m_sortedKeysBuffer.uav.Get();
            broadphaseUav5 = m_sortedParticleIndicesBuffer.uav.Get();
            broadphaseUav6 = m_sortedCellStartsBuffer.uav.Get();
            broadphaseUav7 = m_sortedCellEndsBuffer.uav.Get();
        }
        else
        {
            broadphaseUav6 = m_gridHashHeadsBuffer.uav.Get();
            broadphaseUav7 = m_gridParticleNextBuffer.uav.Get();
        }

        ID3D11UnorderedAccessView* simUavs[] = {
            m_positions[nextBufferIndex].uav.Get(),
            m_previousPositions[nextBufferIndex].uav.Get(),
            m_distanceLambdaBuffer.uav.Get(),
            m_bendLambdaBuffer.uav.Get(),
            broadphaseUav4,
            broadphaseUav5,
            broadphaseUav6,
            broadphaseUav7
        };

        m_context->CSSetConstantBuffers(0, 1, &simCb);
        m_context->CSSetShaderResources(0, 7, integrateSrvs);
        m_context->CSSetUnorderedAccessViews(0, 8, simUavs, nullptr);

        m_context->CSSetShader(m_integrateCS.Get(), nullptr, 0);
        m_context->Dispatch(DispatchGroups(totalParticleCount, 64u), 1, 1);

        if (guideStrandCount > 0u)
        {
            for (uint32_t iteration = 0; iteration < m_simConfig.solverIterations; ++iteration)
            {
                m_context->CSSetShader(m_distanceCS.Get(), nullptr, 0);
                m_context->Dispatch(guideStrandGroups, 1, 1);

                m_context->CSSetShader(m_bendCS.Get(), nullptr, 0);
                m_context->Dispatch(guideStrandGroups, 1, 1);

                m_context->CSSetShader(m_collisionCS.Get(), nullptr, 0);
                m_context->Dispatch(guideParticleGroups, 1, 1);

                if (m_enableSelfCollision)
                {
                    if (m_enableGridSelfCollision)
                    {
                        if (m_selfCollisionBroadphaseMode == 0u)
                        {
                            m_context->CSSetShader(m_clearGridCS.Get(), nullptr, 0);
                            m_context->Dispatch(gridCellGroups, 1, 1);

                            m_context->CSSetShader(m_buildGridCS.Get(), nullptr, 0);
                            m_context->Dispatch(guideParticleGroups, 1, 1);
                        }
                        else if (m_selfCollisionBroadphaseMode == 3u)
                        {
                            m_context->CSSetShader(m_clearSortedRangesCS.Get(), nullptr, 0);
                            m_context->Dispatch(gridCellGroups, 1, 1);

                            m_context->CSSetShader(m_buildSortedEntriesCS.Get(), nullptr, 0);
                            m_context->Dispatch(sortedEntryGroups, 1, 1);

                            ID3D11Buffer* bitonicCb = m_bitonicConstants.Get();
                            for (uint32_t k = 2u; k <= std::max<uint32_t>(m_sortedBroadphasePaddedCount, 1u); k <<= 1u)
                            {
                                for (uint32_t j = k >> 1u; j > 0u; j >>= 1u)
                                {
                                    D3D11_MAPPED_SUBRESOURCE bitonicMapped = {};
                                    if (SUCCEEDED(m_context->Map(m_bitonicConstants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &bitonicMapped)))
                                    {
                                        BitonicConstants* constants = reinterpret_cast<BitonicConstants*>(bitonicMapped.pData);
                                        constants->j = j;
                                        constants->k = k;
                                        constants->entryCount = std::max<uint32_t>(m_sortedBroadphasePaddedCount, 1u);
                                        m_context->Unmap(m_bitonicConstants.Get(), 0);
                                    }
                                    m_context->CSSetConstantBuffers(1, 1, &bitonicCb);
                                    m_context->CSSetShader(m_bitonicSortCS.Get(), nullptr, 0);
                                    m_context->Dispatch(bitonicGroups, 1, 1);
                                }
                            }

                            m_context->CSSetShader(m_buildSortedRangesCS.Get(), nullptr, 0);
                            m_context->Dispatch(sortedEntryGroups, 1, 1);
                        }
                        else
                        {
                            const UINT hashBucketGroups = DispatchGroups(std::max<UINT>(m_simConfig.selfCollisionHashBucketCount, 1u), 64u);
                            const UINT hashClearGroups = std::max(hashBucketGroups, guideParticleGroups);
                            m_context->CSSetShader(m_clearHashGridCS.Get(), nullptr, 0);
                            m_context->Dispatch(hashClearGroups, 1, 1);

                            m_context->CSSetShader(m_buildHashGridCS.Get(), nullptr, 0);
                            m_context->Dispatch(guideParticleGroups, 1, 1);
                        }
                    }

                    m_context->CSSetShader(m_selfCollisionCS.Get(), nullptr, 0);
                    m_context->Dispatch(guideParticleGroups, 1, 1);
                }
            }
        }

        if (followerStrandCount > 0u)
        {
            ID3D11ShaderResourceView* followSrvs[] = {
                m_positions[m_currentBufferIndex].srv.Get(),
                m_previousPositions[m_currentBufferIndex].srv.Get(),
                m_restPositions.srv.Get(),
                m_followBuffer.srv.Get(),
                m_scalpPositions.srv.Get(),
                m_scalpTrianglesBuffer.srv.Get(),
                m_rootBindingsBuffer.srv.Get(),
                nullptr,
                m_followerTierAssignmentsBuffer.srv.Get()
            };
            m_context->CSSetShaderResources(0, 9, followSrvs);
            m_context->CSSetUnorderedAccessViews(0, 8, simUavs, nullptr);
            m_context->CSSetShader(m_followCS.Get(), nullptr, 0);
            m_context->Dispatch(followerGroups, 1, 1);
        }

        ID3D11ShaderResourceView* nullSrvs[9] = {};
        ID3D11UnorderedAccessView* nullUavs[8] = {};
        ID3D11Buffer* nullCb = nullptr;
        m_context->CSSetShader(nullptr, nullptr, 0);
        m_context->CSSetShaderResources(0, 9, nullSrvs);
        m_context->CSSetUnorderedAccessViews(0, 8, nullUavs, nullptr);
        m_context->CSSetConstantBuffers(1, 1, &nullCb);

        m_currentBufferIndex = nextBufferIndex;
        const uint32_t runtimeStride = std::max<uint32_t>(1u, (m_activeSetPlan.summary.cachedTierCount > 0u) ? 4u : ((m_activeSetPlan.summary.reducedTierCount > 0u) ? 2u : 1u));
        if (runtimeStride > 1u)
        {
            m_runtimeFollowerUpdatePhase = (m_runtimeFollowerUpdatePhase + 1u) % runtimeStride;
        }
        else
        {
            m_runtimeFollowerUpdatePhase = 0u;
        }
        if (gpuTimingFrame && gpuTimingFrame->issued)
        {
            m_context->End(gpuTimingFrame->simEnd.Get());
        }
    }

    void GpuHairViewer::UpdateCamera()
    {
        const float cosPitch = std::cos(m_cameraPitch);
        const float sinPitch = std::sin(m_cameraPitch);
        const float cosYaw = std::cos(m_cameraYaw);
        const float sinYaw = std::sin(m_cameraYaw);

        const Vec3 offset(
            cosPitch * sinYaw * m_cameraDistance,
            sinPitch * m_cameraDistance,
            cosPitch * cosYaw * m_cameraDistance);

        m_cameraPosition = m_cameraTarget + offset;
    }

    void GpuHairViewer::RecomputeAssetBounds()
    {
        bool initialized = false;
        Vec3 minP(0.0f, 0.0f, 0.0f);
        Vec3 maxP(0.0f, 0.0f, 0.0f);

        auto accumulatePoint = [&](const Vec3& p)
        {
            if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
            {
                return;
            }
            if (!initialized)
            {
                minP = p;
                maxP = p;
                initialized = true;
                return;
            }
            minP.x = std::min(minP.x, p.x);
            minP.y = std::min(minP.y, p.y);
            minP.z = std::min(minP.z, p.z);
            maxP.x = std::max(maxP.x, p.x);
            maxP.y = std::max(maxP.y, p.y);
            maxP.z = std::max(maxP.z, p.z);
        };

        if (!m_initialParticles.empty())
        {
            for (const GpuParticle& p : m_initialParticles)
            {
                accumulatePoint(Vec3(p.x, p.y, p.z));
            }
        }
        else
        {
            for (const Vec3& p : m_asset.restPoints)
            {
                accumulatePoint(p);
            }
        }

        if (!initialized)
        {
            minP = Vec3(-0.5f, 1.0f, -0.5f);
            maxP = Vec3(0.5f, 2.0f, 0.5f);
        }

        m_assetBoundsMin = minP;
        m_assetBoundsMax = maxP;
        m_assetBoundsCenter = (minP + maxP) * 0.5f;
        const Vec3 extents = maxP - minP;
        const float halfDiagonal = 0.5f * std::sqrt(extents.x * extents.x + extents.y * extents.y + extents.z * extents.z);
        m_assetBoundsRadius = std::max(halfDiagonal, 0.20f);
    }

    void GpuHairViewer::AutoFrameCameraToAsset(bool resetAngles)
    {
        RecomputeAssetBounds();

        if (resetAngles)
        {
            m_cameraYaw = 0.0f;
            m_cameraPitch = 0.05f;
        }

        m_cameraTarget = m_assetBoundsCenter;

        RECT renderRect = GetRenderViewportRect();
        const float renderWidth = static_cast<float>(std::max<LONG>(renderRect.right - renderRect.left, 1));
        const float renderHeight = static_cast<float>(std::max<LONG>(renderRect.bottom - renderRect.top, 1));
        const float aspectRatio = std::max(renderWidth / std::max(renderHeight, 1.0f), 0.6f);
        constexpr float kFovY = 60.0f * 3.1415926535f / 180.0f;
        const float fovX = 2.0f * std::atan(std::tan(kFovY * 0.5f) * aspectRatio);
        const float limitingHalfFov = std::max(0.30f, std::min(kFovY, fovX) * 0.5f);
        const float desiredDistance = (m_assetBoundsRadius / std::tan(limitingHalfFov)) * 1.35f;
        m_cameraDistance = Clamp(desiredDistance, 0.80f, 18.0f);
        UpdateCamera();
    }

    void GpuHairViewer::QueueVisibilityRecovery()
    {
        m_visibilityRecoveryPending = true;
        m_framesSinceVisibilityCheck = 0u;
    }

    bool GpuHairViewer::ShouldRunVisibilityRecoveryThisFrame() const
    {
        if (m_cpuReadbackMode == CpuReadbackMode::Off)
        {
            return false;
        }

        if (m_cpuReadbackMode == CpuReadbackMode::FullParticlesDebug)
        {
            return true;
        }

        if (m_visibilityRecoveryPending)
        {
            return true;
        }

        if (m_lastVisibilitySuspect && m_consecutiveInvisibleFrames >= 2u)
        {
            return true;
        }

        return m_framesSinceVisibilityCheck >= 120u;
    }

    bool GpuHairViewer::RestoreGpuParticleStateFromInitialPose(bool resetCamera)
    {
        if (m_initialParticles.empty() || !m_positions[0].buffer || !m_positions[1].buffer ||
            !m_previousPositions[0].buffer || !m_previousPositions[1].buffer)
        {
            return false;
        }

        m_currentBufferIndex = 0u;
        m_context->UpdateSubresource(m_positions[0].buffer.Get(), 0, nullptr, m_initialParticles.data(), 0, 0);
        m_context->UpdateSubresource(m_positions[1].buffer.Get(), 0, nullptr, m_initialParticles.data(), 0, 0);
        m_context->UpdateSubresource(m_previousPositions[0].buffer.Get(), 0, nullptr, m_initialParticles.data(), 0, 0);
        m_context->UpdateSubresource(m_previousPositions[1].buffer.Get(), 0, nullptr, m_initialParticles.data(), 0, 0);
        if (resetCamera)
        {
            AutoFrameCameraToAsset(true);
        }
        return true;
    }

    bool GpuHairViewer::RunVisibilityRecoveryPass(float aspectRatio, bool allowCameraRecovery, std::vector<GpuParticle>* outParticles)
    {
        std::vector<GpuParticle> particles;
        const bool readbackSucceeded = ReadBackCurrentParticlePositions(particles);
        const bool readbackValid = readbackSucceeded &&
            ParticleArrayLooksValid(reinterpret_cast<const float*>(particles.data()), particles.size(), m_asset);

        m_lastVisibilityReadbackValid = readbackValid;
        m_lastVisibilityUsedInitialPoseFallback = false;

        if (!readbackValid)
        {
            ++m_visibilityReadbackFailureCount;
            m_lastVisibilityUsedInitialPoseFallback = RestoreGpuParticleStateFromInitialPose(false);
            particles = m_initialParticles;
        }

        const bool haveParticles = !particles.empty();
        m_lastReadbackParticleCount = static_cast<uint32_t>(particles.size());
        m_lastEstimatedVisibleParticles = EstimateVisibleParticleCount(particles, aspectRatio);
        const uint32_t minimumExpectedVisibleParticles = std::max<uint32_t>(32u, static_cast<uint32_t>(std::max<size_t>(particles.size() / 160u, 1u)));
        m_lastVisibilitySuspect = !readbackValid || (m_lastEstimatedVisibleParticles < minimumExpectedVisibleParticles);

        if (allowCameraRecovery && m_lastVisibilitySuspect)
        {
            ++m_visibilityRecoveryCount;
            AutoFrameCameraToAsset(true);
            m_renderMode = RenderMode::StrandRibbon;
            m_widthScale = std::max(m_widthScale, 0.78f);

            if (!readbackValid)
            {
                m_lastVisibilityUsedInitialPoseFallback = RestoreGpuParticleStateFromInitialPose(false) || m_lastVisibilityUsedInitialPoseFallback;
            }

            m_lastEstimatedVisibleParticles = EstimateVisibleParticleCount(particles, aspectRatio);
            m_lastVisibilitySuspect = !readbackValid || (m_lastEstimatedVisibleParticles < minimumExpectedVisibleParticles);
        }

        m_visibilityRecoveryPending = m_lastVisibilitySuspect;
        m_framesSinceVisibilityCheck = 0u;
        m_consecutiveInvisibleFrames = m_lastVisibilitySuspect ? (m_consecutiveInvisibleFrames + 1u) : 0u;

        if (outParticles != nullptr)
        {
            *outParticles = particles;
        }

        return haveParticles;
    }

    uint32_t GpuHairViewer::EstimateVisibleParticleCount(const std::vector<GpuParticle>& particles, float aspectRatio) const
    {
        if (particles.empty())
        {
            return 0u;
        }

        const size_t stride = std::max<size_t>(particles.size() / 2048u, 1u);
        uint32_t visibleSamples = 0u;
        uint32_t totalSamples = 0u;
        for (size_t i = 0; i < particles.size(); i += stride)
        {
            const GpuParticle& p = particles[i];
            Vec3 ndc;
            if (!ProjectWorldToNdc(Vec3(p.x, p.y, p.z), m_cameraPosition, m_cameraTarget, aspectRatio, ndc))
            {
                continue;
            }
            ++totalSamples;
            if (std::fabs(ndc.x) <= 1.10f && std::fabs(ndc.y) <= 1.10f && ndc.z >= 0.0f && ndc.z <= 1.0f)
            {
                ++visibleSamples;
            }
        }

        if (totalSamples == 0u)
        {
            return 0u;
        }

        const double visibleRatio = static_cast<double>(visibleSamples) / static_cast<double>(totalSamples);
        const double estimated = visibleRatio * static_cast<double>(particles.size());
        return static_cast<uint32_t>(std::max(0.0, estimated));
    }

    void GpuHairViewer::Render()
    {
        if (!m_swapChain || !m_rtv || !m_dsv)
        {
            return;
        }

        const RECT renderRect = GetRenderViewportRect();
        const float renderWidth = static_cast<float>(std::max<LONG>(renderRect.right - renderRect.left, 1));
        const float renderHeight = static_cast<float>(std::max<LONG>(renderRect.bottom - renderRect.top, 1));

        RenderConstants rc = {};
        const Mat4 view = LookAtRH(m_cameraPosition, m_cameraTarget, Vec3(0.0f, 1.0f, 0.0f));
        const Mat4 projection = PerspectiveFovRH(60.0f * 3.1415926535f / 180.0f, renderWidth / renderHeight, 0.01f, 20.0f);
        const Mat4 viewProjection = Multiply(view, projection);
        FillRenderConstants(rc, viewProjection);
        D3D11_MAPPED_SUBRESOURCE mappedRender = {};
        if (SUCCEEDED(m_context->Map(m_renderConstants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedRender)))
        {
            std::memcpy(mappedRender.pData, &rc, sizeof(rc));
            m_context->Unmap(m_renderConstants.Get(), 0);
        }

        const float clearColor[4] = { 0.10f, 0.12f, 0.16f, 1.0f };
        m_context->ClearRenderTargetView(m_rtv.Get(), clearColor);
        m_context->ClearDepthStencilView(m_dsv.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);

        D3D11_VIEWPORT vp = {};
        vp.TopLeftX = static_cast<float>(renderRect.left);
        vp.TopLeftY = static_cast<float>(renderRect.top);
        vp.Width = renderWidth;
        vp.Height = renderHeight;
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        m_context->RSSetViewports(1, &vp);
        m_context->OMSetRenderTargets(1, m_rtv.GetAddressOf(), m_dsv.Get());
        m_context->RSSetState(m_rasterizerState.Get());
        m_context->OMSetDepthStencilState(m_depthState.Get(), 0);
        const float blendFactor[4] = { 0, 0, 0, 0 };
        m_context->OMSetBlendState(m_blendState.Get(), blendFactor, 0xFFFFFFFF);

        ID3D11Buffer* renderCb = m_renderConstants.Get();
        UINT offset = 0;

        const UINT hairStride = sizeof(SegmentRenderVertex);
        ID3D11Buffer* hairVB = m_segmentVertexBuffer.Get();
        ID3D11ShaderResourceView* hairSrvs[] = {
            m_positions[m_currentBufferIndex].srv.Get(),
            m_pointWidths.srv.Get(),
            m_renderStrandTiersBuffer.srv.Get()
        };

        GpuTimingFrame* gpuTimingFrame = (m_activeGpuTimingFrameIndex >= 0 && static_cast<size_t>(m_activeGpuTimingFrameIndex) < m_gpuTimingFrames.size())
            ? &m_gpuTimingFrames[static_cast<size_t>(m_activeGpuTimingFrameIndex)]
            : nullptr;
        m_lastCurrentHairDrawCalls = 0u;
        m_lastCurrentHairVertexCount = 0u;
        m_lastCurrentHairPrimitiveCount = 0u;
        m_lastCurrentHairInstanceCount = 0u;
        m_lastTotalDrawCalls = 0u;

        bool drewCpuHairMesh = false;
        m_lastUsedCpuVisibilityFallback = false;
        std::vector<GpuParticle> cpuMeshParticles;
        const float aspectRatio = renderWidth / renderHeight;
        const bool runVisibilityRecovery = ShouldRunVisibilityRecoveryThisFrame();
        const bool haveReadbackParticles = runVisibilityRecovery
            ? RunVisibilityRecoveryPass(aspectRatio, true, &cpuMeshParticles)
            : false;

        if (!runVisibilityRecovery)
        {
            ++m_framesSinceVisibilityCheck;
        }

        if (haveReadbackParticles)
        {
            UpdateActiveSetRuntimeFromParticles(cpuMeshParticles);
        }

        const bool shouldDrawCpuFallback = !cpuMeshParticles.empty() &&
            (m_cpuReadbackMode == CpuReadbackMode::FullParticlesDebug ||
             (m_cpuReadbackMode == CpuReadbackMode::OnDemandVisibility && m_lastVisibilitySuspect));

        if (shouldDrawCpuFallback)
        {
            std::vector<hairgpu::Vec3> correctedPoints;
            correctedPoints.reserve(cpuMeshParticles.size());
            for (const GpuParticle& particle : cpuMeshParticles)
            {
                correctedPoints.push_back(hairgpu::Vec3(particle.x, particle.y, particle.z));
            }
            m_sparseCorrectiveReport = {};
            if (!correctedPoints.empty())
            {
                std::string correctiveError;
                if (hairgpu::ApplySparseCorrectivePhysics(m_asset,
                                                         m_activeSetPlan,
                                                         m_colliders,
                                                         correctedPoints,
                                                         m_sparseCorrectiveSettings,
                                                         &m_sparseCorrectiveReport,
                                                         correctiveError))
                {
                    for (size_t i = 0; i < std::min(correctedPoints.size(), cpuMeshParticles.size()); ++i)
                    {
                        cpuMeshParticles[i].x = correctedPoints[i].x;
                        cpuMeshParticles[i].y = correctedPoints[i].y;
                        cpuMeshParticles[i].z = correctedPoints[i].z;
                    }
                }
            }
            BuildHairMeshVertices(cpuMeshParticles, aspectRatio);
            if (m_hairMeshVertices.empty() && !cpuMeshParticles.empty())
            {
                AutoFrameCameraToAsset(true);
                BuildHairMeshVertices(cpuMeshParticles, aspectRatio);
            }
            if (!m_hairMeshVertices.empty() && EnsureHairMeshVertexCapacity(m_hairMeshVertices.size()) && m_hairMeshVertexBuffer && m_hairMeshInputLayout && m_hairMeshVS && m_hairMeshPS)
            {
                D3D11_MAPPED_SUBRESOURCE mapped = {};
                if (SUCCEEDED(m_context->Map(m_hairMeshVertexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
                {
                    std::memcpy(mapped.pData, m_hairMeshVertices.data(), m_hairMeshVertices.size() * sizeof(HairMeshVertex));
                    m_context->Unmap(m_hairMeshVertexBuffer.Get(), 0);

                    const UINT cpuMeshStride = sizeof(HairMeshVertex);
                    ID3D11Buffer* cpuMeshVB = m_hairMeshVertexBuffer.Get();
                    m_context->IASetInputLayout(m_hairMeshInputLayout.Get());
                    m_context->IASetVertexBuffers(0, 1, &cpuMeshVB, &cpuMeshStride, &offset);
                    m_context->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
                    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                    m_context->VSSetConstantBuffers(0, 0, nullptr);
                    m_context->PSSetConstantBuffers(0, 0, nullptr);
                    m_context->VSSetShader(m_hairMeshVS.Get(), nullptr, 0);
                    m_context->GSSetShader(nullptr, nullptr, 0);
                    m_context->PSSetShader(m_hairMeshPS.Get(), nullptr, 0);
                    m_context->Draw(static_cast<UINT>(m_hairMeshVertices.size()), 0);
                    drewCpuHairMesh = true;
                    m_lastUsedCpuVisibilityFallback = true;
                    m_lastCurrentHairDrawCalls = 1u;
                    m_lastCurrentHairVertexCount = static_cast<uint64_t>(m_hairMeshVertices.size());
                    m_lastCurrentHairPrimitiveCount = static_cast<uint64_t>(m_hairMeshVertices.size() / 3u);
                    m_lastCurrentHairInstanceCount = 1u;
                    ++m_lastTotalDrawCalls;
                }
            }
        }

        if (!drewCpuHairMesh && m_showScalpMesh && m_scalpVertexBuffer && m_scalpIndexBuffer && m_scalpIndexCount > 0u)
        {
            const UINT scalpStride = sizeof(ScalpVertex);
            ID3D11Buffer* scalpVB = m_scalpVertexBuffer.Get();
            ID3D11ShaderResourceView* scalpSrv = m_scalpPositions.srv.Get();
            m_context->IASetInputLayout(m_scalpInputLayout.Get());
            m_context->IASetVertexBuffers(0, 1, &scalpVB, &scalpStride, &offset);
            m_context->IASetIndexBuffer(m_scalpIndexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
            m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            m_context->VSSetConstantBuffers(0, 1, &renderCb);
            m_context->PSSetConstantBuffers(0, 1, &renderCb);
            m_context->VSSetShaderResources(0, 1, &scalpSrv);
            m_context->VSSetShader(m_scalpVS.Get(), nullptr, 0);
            m_context->GSSetShader(nullptr, nullptr, 0);
            m_context->PSSetShader(m_scalpPS.Get(), nullptr, 0);
            m_context->DrawIndexed(m_scalpIndexCount, 0, 0);
            ID3D11ShaderResourceView* nullScalpSrv[1] = {};
            m_context->VSSetShaderResources(0, 1, nullScalpSrv);
            ++m_lastTotalDrawCalls;
        }

        if (gpuTimingFrame && gpuTimingFrame->issued)
        {
            m_context->End(gpuTimingFrame->hairDrawStart.Get());
            m_context->Begin(gpuTimingFrame->hairPipelineStats.Get());
        }

        if (!drewCpuHairMesh && m_positions[m_currentBufferIndex].srv && m_pointWidths.srv && m_renderStrandTiersBuffer.srv && !m_asset.restPoints.empty())
        {
            m_context->VSSetConstantBuffers(0, 1, &renderCb);
            m_context->GSSetConstantBuffers(0, 1, &renderCb);
            m_context->PSSetConstantBuffers(0, 1, &renderCb);
            m_context->VSSetShaderResources(0, 3, hairSrvs);

            if (m_renderMode == RenderMode::StrandRibbon && m_ribbonVS && m_ribbonGS && m_ribbonPS && m_segmentInputLayout && hairVB && !m_renderData.segmentVertices.empty())
            {
                m_context->IASetInputLayout(m_segmentInputLayout.Get());
                m_context->IASetVertexBuffers(0, 1, &hairVB, &hairStride, &offset);
                m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
                m_context->VSSetShader(m_ribbonVS.Get(), nullptr, 0);
                m_context->GSSetShader(m_ribbonGS.Get(), nullptr, 0);
                m_context->PSSetShader(m_ribbonPS.Get(), nullptr, 0);
                const UINT vertexCount = static_cast<UINT>(m_renderData.segmentVertices.size());
                m_context->Draw(vertexCount, 0);
                m_lastCurrentHairDrawCalls = 1u;
                m_lastCurrentHairVertexCount = static_cast<uint64_t>(vertexCount);
                m_lastCurrentHairPrimitiveCount = static_cast<uint64_t>(m_renderData.segmentCount) * 2ull;
                m_lastCurrentHairInstanceCount = 1u;
            }
            else if (m_particleVS && m_particlePS)
            {
                m_context->IASetInputLayout(nullptr);
                m_context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
                m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
                m_context->VSSetShader(m_particleVS.Get(), nullptr, 0);
                m_context->GSSetShader((m_renderMode == RenderMode::ParticlePointSprite) ? m_particlePointGS.Get() : m_particleBillboardGS.Get(), nullptr, 0);
                m_context->PSSetShader(m_particlePS.Get(), nullptr, 0);
                const UINT particleCount = static_cast<UINT>(m_asset.restPoints.size());
                m_context->Draw(particleCount, 0);
                m_lastCurrentHairDrawCalls = 1u;
                m_lastCurrentHairVertexCount = static_cast<uint64_t>(particleCount);
                m_lastCurrentHairPrimitiveCount = static_cast<uint64_t>(particleCount) * 2ull;
                m_lastCurrentHairInstanceCount = 1u;
            }

            ID3D11ShaderResourceView* nullHairSrvs[3] = {};
            m_context->VSSetShaderResources(0, 3, nullHairSrvs);
        }

        if (gpuTimingFrame && gpuTimingFrame->issued)
        {
            m_context->End(gpuTimingFrame->hairPipelineStats.Get());
            m_context->End(gpuTimingFrame->hairDrawEnd.Get());
            m_context->End(gpuTimingFrame->debugDrawStart.Get());
            m_context->Begin(gpuTimingFrame->debugPipelineStats.Get());
        }

        if (m_gpuDebugLinesDirty)
        {
            RebuildGpuDebugLineEntries();
        }
        if (!m_gpuDebugLineEntries.empty() && EnsureGpuDebugLineCapacity(m_gpuDebugLineEntries.size()) && m_debugLineTemplateVertexBuffer && m_debugLineEntriesBuffer.srv)
        {
            m_context->UpdateSubresource(m_debugLineEntriesBuffer.buffer.Get(), 0, nullptr, m_gpuDebugLineEntries.data(), 0, 0);

            const UINT debugStride = sizeof(DebugLineTemplateVertex);
            ID3D11Buffer* debugBuffer = m_debugLineTemplateVertexBuffer.Get();
            ID3D11ShaderResourceView* debugSrvs[] = {
                m_positions[m_currentBufferIndex].srv.Get(),
                m_scalpPositions.srv.Get(),
                m_debugLineEntriesBuffer.srv.Get()
            };
            m_context->IASetInputLayout(m_debugInputLayout.Get());
            m_context->IASetVertexBuffers(0, 1, &debugBuffer, &debugStride, &offset);
            m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
            m_context->VSSetShader(m_debugLineVS.Get(), nullptr, 0);
            m_context->GSSetShader(nullptr, nullptr, 0);
            m_context->PSSetShader(m_debugLinePS.Get(), nullptr, 0);
            m_context->VSSetConstantBuffers(0, 1, &renderCb);
            m_context->PSSetConstantBuffers(0, 1, &renderCb);
            m_context->VSSetShaderResources(0, 3, debugSrvs);
            m_context->DrawInstanced(2u, static_cast<UINT>(m_gpuDebugLineEntries.size()), 0u, 0u);
            ID3D11ShaderResourceView* nullDebugSrvs[3] = {};
            m_context->VSSetShaderResources(0, 3, nullDebugSrvs);
            ++m_lastTotalDrawCalls;
        }

        if (gpuTimingFrame && gpuTimingFrame->issued)
        {
            m_context->End(gpuTimingFrame->debugPipelineStats.Get());
            m_context->End(gpuTimingFrame->debugDrawEnd.Get());
            m_context->End(gpuTimingFrame->frameEnd.Get());
            m_context->End(gpuTimingFrame->disjoint.Get());
        }

        m_lastTotalDrawCalls += m_lastCurrentHairDrawCalls;
        UpdateRenderStatistics();

        m_swapChain->Present(1, 0);
        UpdateWindowTitle();
    }

    void GpuHairViewer::Resize(UINT width, UINT height)
    {
        m_width = std::max<UINT>(width, 1u);
        m_height = std::max<UINT>(height, 1u);
        if (!m_swapChain)
        {
            return;
        }

        m_rtv.Reset();
        m_dsv.Reset();
        m_depthTexture.Reset();
        m_context->OMSetRenderTargets(0, nullptr, nullptr);

        m_swapChain->ResizeBuffers(0, m_width, m_height, DXGI_FORMAT_UNKNOWN, 0);

        ComPtr<ID3D11Texture2D> backBuffer;
        m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
        m_device->CreateRenderTargetView(backBuffer.Get(), nullptr, &m_rtv);

        D3D11_TEXTURE2D_DESC depthDesc = {};
        depthDesc.Width = m_width;
        depthDesc.Height = m_height;
        depthDesc.ArraySize = 1;
        depthDesc.MipLevels = 1;
        depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        depthDesc.SampleDesc.Count = 1;
        m_device->CreateTexture2D(&depthDesc, nullptr, &m_depthTexture);
        m_device->CreateDepthStencilView(m_depthTexture.Get(), nullptr, &m_dsv);
        QueueVisibilityRecovery();
        LayoutUiControls();
    }

    void GpuHairViewer::UpdateWindowTitle()
    {
        std::wstringstream ss;
        ss << L"Hair GPU Viewer ActiveSet Runtime [GPU_LAYOUT_FIX_V3_NO_COMPACTION]  |  preset=" << m_activePresetName
           << L"  fps=" << std::fixed << std::setprecision(1) << m_smoothedFps
           << L"  guides=" << m_asset.guideStrandCount
           << L"  followers=" << m_asset.followerStrandCount
           << L"  totalStrands=" << m_asset.strands.size()
           << L"  particles=" << m_asset.restPoints.size()
           << L"  render=" << (m_renderMode == RenderMode::ParticlePointSprite ? L"point" : (m_renderMode == RenderMode::ParticleBillboard ? L"billboard" : L"ribbon"))
           << L"  gpuFrameMs=" << std::setprecision(2) << m_lastGpuFrameMilliseconds
           << L"  vis=" << m_lastEstimatedVisibleParticles
           << L"  suspect=" << (m_lastVisibilitySuspect ? L"yes" : L"no")
           << L"  cpuFallback=" << (m_lastUsedCpuVisibilityFallback ? L"yes" : L"no")
           << L"  readback=" << (m_cpuReadbackMode == CpuReadbackMode::Off ? L"off" : (m_cpuReadbackMode == CpuReadbackMode::FullParticlesDebug ? L"full-debug" : L"on-demand"))
           << L"  readbackValid=" << (m_lastVisibilityReadbackValid ? L"yes" : L"no")
           << L"  initialPoseFallback=" << (m_lastVisibilityUsedInitialPoseFallback ? L"yes" : L"no")
           << L"  recoveryPending=" << (m_visibilityRecoveryPending ? L"yes" : L"no")
           << L"  recoveries=" << m_visibilityRecoveryCount
           << L"  readbackFailures=" << m_visibilityReadbackFailureCount
           << L"  widthScale=" << m_widthScale
           << L"  stretchCompliance=" << m_simConfig.stretchCompliance
           << L"  selfCollision=" << (m_enableSelfCollision ? L"density-grid" : L"off")
           << L"  angularXPBD=" << (m_enableRuntimeAngularBend ? L"on" : L"off")
           << L"  scalpRoots=static-gpu"
           << (m_paused ? L"  [PAUSED]" : L"");
        SetWindowTextW(m_hwnd, ss.str().c_str());
    }

    void GpuHairViewer::Shutdown()
    {
        if (m_context)
        {
            m_context->ClearState();
        }
    }

    bool GpuHairViewer::CompileShaderFromFile(const wchar_t* filePath, const char* entryPoint, const char* target, ID3DBlob** blobOut, std::wstring& errorText)
    {
        UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
    #if defined(_DEBUG)
        flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
    #endif
        ComPtr<ID3DBlob> shaderBlob;
        ComPtr<ID3DBlob> errorBlob;

                wchar_t modulePath[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
        const std::filesystem::path exeDir = std::filesystem::path(modulePath).parent_path();

        std::vector<std::filesystem::path> candidates;
        auto pushCandidate = [&candidates](const std::filesystem::path& candidate)
        {
            if (candidate.empty())
            {
                return;
            }
            const auto normalized = candidate.lexically_normal();
            if (std::find(candidates.begin(), candidates.end(), normalized) == candidates.end())
            {
                candidates.push_back(normalized);
            }
        };

        pushCandidate(std::filesystem::path(filePath));
        pushCandidate(std::filesystem::path(HAIRGPU_VIEWER_SHADER_DIR) / filePath);
        pushCandidate(exeDir / filePath);
        pushCandidate(exeDir / L"shaders" / filePath);
        pushCandidate(std::filesystem::current_path() / filePath);
        pushCandidate(std::filesystem::current_path() / L"shaders" / filePath);

        std::filesystem::path searchRoot = exeDir;
        for (int i = 0; i < 6; ++i)
        {
            pushCandidate(searchRoot / L"apps" / L"dx11_viewer" / L"shaders" / filePath);
            pushCandidate(searchRoot / L"dx11_viewer" / L"shaders" / filePath);
            if (!searchRoot.has_parent_path())
            {
                break;
            }
            const auto parent = searchRoot.parent_path();
            if (parent == searchRoot)
            {
                break;
            }
            searchRoot = parent;
        }

        HRESULT hr = E_FAIL;
        std::wstringstream tried;
        for (const auto& candidate : candidates)
        {
            tried << candidate.c_str();
            tried << (std::filesystem::exists(candidate) ? L"  [exists]" : L"  [missing]") << L"\r\n";
            errorBlob.Reset();
            shaderBlob.Reset();
            hr = D3DCompileFromFile(candidate.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, entryPoint, target, flags, 0, &shaderBlob, &errorBlob);
            if (SUCCEEDED(hr))
            {
                break;
            }
        }
        if (FAILED(hr))
        {
            if (errorBlob)
            {
                const std::string message(static_cast<const char*>(errorBlob->GetBufferPointer()), errorBlob->GetBufferSize());
                errorText.assign(message.begin(), message.end());
            }
            else
            {
                errorText = L"Unknown shader compilation error.";
                std::wstringstream hrHex;
                hrHex << std::hex << std::uppercase << static_cast<unsigned long>(hr);
                errorText += L"\r\nHRESULT: 0x";
                errorText += hrHex.str();
            }
            errorText += L"\r\n\r\nTried paths:\r\n";
            errorText += tried.str();
            errorText += L"\r\nHint: verify that the .hlsl files were copied next to the executable or into its shaders subfolder.";
            return false;
        }
        *blobOut = shaderBlob.Detach();
        return true;
    }

    GpuHairViewer::StructuredBuffer GpuHairViewer::CreateStructuredBuffer(UINT elementSize, UINT elementCount, const void* initialData, bool unorderedAccess)
    {
        StructuredBuffer result;

        D3D11_BUFFER_DESC desc = {};
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | (unorderedAccess ? D3D11_BIND_UNORDERED_ACCESS : 0u);
        desc.ByteWidth = std::max<UINT>(elementSize * elementCount, elementSize);
        desc.StructureByteStride = elementSize;
        desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        desc.Usage = D3D11_USAGE_DEFAULT;

        D3D11_SUBRESOURCE_DATA data = {};
        data.pSysMem = initialData;
        m_device->CreateBuffer(&desc, initialData ? &data : nullptr, &result.buffer);

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements = elementCount;
        m_device->CreateShaderResourceView(result.buffer.Get(), &srvDesc, &result.srv);

        if (unorderedAccess)
        {
            D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
            uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
            uavDesc.Format = DXGI_FORMAT_UNKNOWN;
            uavDesc.Buffer.FirstElement = 0;
            uavDesc.Buffer.NumElements = elementCount;
            m_device->CreateUnorderedAccessView(result.buffer.Get(), &uavDesc, &result.uav);
        }

        return result;
    }


    RECT GpuHairViewer::GetRenderViewportRect() const
    {
        RECT rect = { 0, 0, static_cast<LONG>(std::max<UINT>(m_width > static_cast<UINT>(kUiPanelWidth) ? m_width - kUiPanelWidth : m_width, 1u)),
                      static_cast<LONG>(std::max<UINT>(m_height, 1u)) };
        return rect;
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
        m_btnBrowseEditStack = createControl(0, L"BUTTON", L"Load Edit Stack...", BS_PUSHBUTTON, IDC_BTN_BROWSE_EDIT_STACK);
        m_btnClearEditStack = createControl(0, L"BUTTON", L"Clear Edit", BS_PUSHBUTTON, IDC_BTN_CLEAR_EDIT_STACK);
        m_labelEditStack = createControl(0, L"STATIC", L"Edit stack: none", SS_LEFT, 0);
        m_labelInlineEditStack = createControl(0, L"STATIC", L"Inline Edit Stack", SS_LEFT, IDC_LABEL_INLINE_EDIT_STACK);
        m_editInlineEditStack = createControl(WS_EX_CLIENTEDGE, L"EDIT", BuildDefaultInlineEditStackText().c_str(), ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL, IDC_EDIT_INLINE_EDIT_STACK);
        m_btnApplyInlineEditStack = createControl(0, L"BUTTON", L"Apply Inline", BS_PUSHBUTTON, IDC_BTN_APPLY_INLINE_EDIT_STACK);
        m_btnSaveInlineEditStack = createControl(0, L"BUTTON", L"Save Inline...", BS_PUSHBUTTON, IDC_BTN_SAVE_INLINE_EDIT_STACK);
        m_btnResetInlineEditStack = createControl(0, L"BUTTON", L"Reset Inline", BS_PUSHBUTTON, IDC_BTN_RESET_INLINE_EDIT_STACK);
        m_labelQuickEditForm = createControl(0, L"STATIC", L"Quick Edit Form", SS_LEFT, IDC_LABEL_QUICK_EDIT_FORM);
        m_labelLayerList = createControl(0, L"STATIC", L"Layer List", SS_LEFT, IDC_LABEL_INLINE_LAYER_LIST);
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
        m_btnQuickEditReplace = createControl(0, L"BUTTON", L"Replace Selected", BS_PUSHBUTTON, IDC_BTN_QUICK_EDIT_REPLACE);
        m_btnQuickEditAppend = createControl(0, L"BUTTON", L"Append Layer", BS_PUSHBUTTON, IDC_BTN_QUICK_EDIT_APPEND);
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
        SendMessageW(m_comboMaskOverlay, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Parting"));
        SendMessageW(m_comboMaskOverlay, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Region"));
        SendMessageW(m_comboMaskOverlay, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Edit Influence / Selected Layer"));
        SendMessageW(m_comboMaskOverlay, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Boundary Before / After"));
        SendMessageW(m_comboMaskOverlay, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Active-Set Risk / Selection"));
        SendMessageW(m_comboMaskOverlay, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Active-Set Motion"));
        SendMessageW(m_comboMaskOverlay, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Active-Set Contact"));
        SendMessageW(m_comboMaskOverlay, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Active-Set Uncertainty"));
        SendMessageW(m_comboMaskOverlay, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Active-Set Edit"));
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

        createControl(0, L"STATIC", L"Color Debug", SS_LEFT, 0);
        m_comboDebugColor = createControl(0, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL | CBS_HASSTRINGS, IDC_COMBO_DEBUG_COLOR);
        SendMessageW(m_comboDebugColor, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Natural"));
        SendMessageW(m_comboDebugColor, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Guide / Follower"));
        SendMessageW(m_comboDebugColor, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Width Heatmap"));
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

        return m_comboPreset && m_comboRenderMode && m_comboDebugColor && m_comboMaskSource && m_comboMaskOverlay && m_previewPartingMask && m_previewRegionMask && m_btnPlayPause && m_btnStep && m_btnImportAsset && m_btnBrowseEditStack && m_btnClearEditStack && m_labelEditStack && m_labelInlineEditStack && m_editInlineEditStack && m_btnApplyInlineEditStack && m_btnSaveInlineEditStack && m_btnResetInlineEditStack && m_labelQuickEditForm && m_labelLayerList && m_listInlineLayers && m_labelQuickEditName && m_editQuickEditName && m_btnQuickEditMoveUp && m_btnQuickEditMoveDown && m_checkQuickEditEnabled && m_btnQuickEditDelete && m_btnQuickEditDuplicate && m_comboQuickEditMode && m_trackQuickEditFeather && m_trackQuickEditLengthScale && m_trackQuickEditLayerWeight && m_trackQuickEditCenterU && m_trackQuickEditCenterV && m_trackQuickEditRadiusU && m_trackQuickEditRadiusV && m_trackQuickEditInnerRadius && m_trackQuickEditAngleCenter && m_trackQuickEditAngleHalfExtent && m_trackQuickEditRootOffsetX && m_trackQuickEditRootOffsetY && m_trackQuickEditRootOffsetZ && m_trackQuickEditTipOffsetX && m_trackQuickEditTipOffsetY && m_trackQuickEditTipOffsetZ && m_trackQuickEditSideOffset && m_trackQuickEditYaw && m_trackQuickEditWidthScale && m_trackQuickEditFollowerInfluence && m_trackQuickEditBoundarySmoothing && m_trackQuickEditBoundaryRadiusUv && m_editQuickEditGroupId && m_checkQuickEditGuidesOnly && m_labelQuickEditLayerWeight && m_labelQuickEditCenterU && m_labelQuickEditCenterV && m_labelQuickEditRadiusU && m_labelQuickEditRadiusV && m_labelQuickEditInnerRadius && m_labelQuickEditAngleCenter && m_labelQuickEditAngleHalfExtent && m_labelQuickEditRootOffsetX && m_labelQuickEditRootOffsetY && m_labelQuickEditRootOffsetZ && m_labelQuickEditTipOffsetX && m_labelQuickEditTipOffsetY && m_labelQuickEditTipOffsetZ && m_labelQuickEditSideOffset && m_labelQuickEditYaw && m_labelQuickEditWidthScale && m_labelQuickEditFollowerInfluence && m_labelQuickEditBoundarySmoothing && m_labelQuickEditBoundaryRadiusUv && m_labelQuickEditGroupId && m_labelQuickEditVisualization && m_btnQuickEditReplace && m_btnQuickEditAppend && m_trackWidth && m_labelActiveSetTuning && m_checkActiveSetAdaptiveBudget && m_checkActiveSetHardClamp && m_labelActiveSetMaxClusters && m_trackActiveSetMaxClusters && m_labelActiveSetMaxGuides && m_trackActiveSetMaxGuides && m_labelActiveSetThreshold && m_trackActiveSetThreshold && m_labelActiveSetTargetFrame && m_trackActiveSetTargetFrame && m_labelActiveSetFixedCost && m_trackActiveSetFixedCost && m_labelActiveSetClusterCost && m_trackActiveSetClusterCost && m_labelActiveSetScoreWeights && m_labelActiveSetMotionWeight && m_trackActiveSetMotionWeight && m_labelActiveSetContactWeight && m_trackActiveSetContactWeight && m_labelActiveSetUncertaintyWeight && m_trackActiveSetUncertaintyWeight && m_labelActiveSetEditWeight && m_trackActiveSetEditWeight && m_btnExportActiveSetJson && m_btnExportRuntimeReport && m_labelStats && m_labelAssetStats && m_checkShowScalpMesh;
    }

    void GpuHairViewer::LayoutUiControls()
    {
        if (!m_hwnd || !m_uiPanel)
        {
            return;
        }

        RECT client = {};
        GetClientRect(m_hwnd, &client);
        const int panelX = (std::max)(static_cast<int>(client.right) - kUiPanelWidth, 0);
        const int panelHeight = (std::max)(static_cast<int>(client.bottom), 1);
        MoveWindow(m_uiPanel, panelX, 0, kUiPanelWidth, panelHeight, TRUE);

        const int fullWidth = kUiPanelWidth - kUiMargin * 2;
        const int threeButtonWidth = (fullWidth - 16) / 3;
        const int halfButtonWidth = (fullWidth - 8) / 2;
        const int editWidth = 76;
        const int x = panelX + kUiMargin;
        int y = kUiMargin;
        const int labelHeight = 18;
        const int controlHeight = 24;
        const int sliderHeight = 28;
        const int gap = 6;

        auto next = [&](int h) { int old = y; y += h; return old; };
        auto place = [&](HWND hwnd, int px, int py, int w, int h)
        {
            if (hwnd) { MoveWindow(hwnd, px, py, w, h, TRUE); }
        };

        std::vector<HWND> unlabeledStatics;
        for (HWND child = GetWindow(m_hwnd, GW_CHILD); child != nullptr; child = GetWindow(child, GW_HWNDNEXT))
        {
            if (child == m_uiPanel || child == m_btnPlayPause || child == m_btnReset || child == m_btnStep || child == m_btnApplyAsset ||
                child == m_btnImportAsset || child == m_btnSaveSettings || child == m_btnLoadSettings || child == m_btnExportCsv || child == m_btnExportPerfCsv || child == m_btnBrowseEditStack || child == m_btnClearEditStack || child == m_labelEditStack || child == m_labelInlineEditStack || child == m_editInlineEditStack || child == m_btnApplyInlineEditStack || child == m_btnSaveInlineEditStack || child == m_btnResetInlineEditStack || child == m_labelQuickEditForm || child == m_labelLayerList || child == m_listInlineLayers || child == m_labelQuickEditName || child == m_editQuickEditName || child == m_btnQuickEditMoveUp || child == m_btnQuickEditMoveDown || child == m_checkQuickEditEnabled || child == m_btnQuickEditDelete || child == m_btnQuickEditDuplicate || child == m_comboQuickEditMode || child == m_labelQuickEditFeather || child == m_trackQuickEditFeather || child == m_labelQuickEditLengthScale || child == m_trackQuickEditLengthScale || child == m_labelQuickEditLayerWeight || child == m_trackQuickEditLayerWeight || child == m_labelQuickEditCenterU || child == m_trackQuickEditCenterU || child == m_labelQuickEditCenterV || child == m_trackQuickEditCenterV || child == m_labelQuickEditRadiusU || child == m_trackQuickEditRadiusU || child == m_labelQuickEditRadiusV || child == m_trackQuickEditRadiusV || child == m_labelQuickEditInnerRadius || child == m_trackQuickEditInnerRadius || child == m_labelQuickEditAngleCenter || child == m_trackQuickEditAngleCenter || child == m_labelQuickEditAngleHalfExtent || child == m_trackQuickEditAngleHalfExtent || child == m_labelQuickEditRootOffsetX || child == m_trackQuickEditRootOffsetX || child == m_labelQuickEditRootOffsetY || child == m_trackQuickEditRootOffsetY || child == m_labelQuickEditRootOffsetZ || child == m_trackQuickEditRootOffsetZ || child == m_labelQuickEditTipOffsetX || child == m_trackQuickEditTipOffsetX || child == m_labelQuickEditTipOffsetY || child == m_trackQuickEditTipOffsetY || child == m_labelQuickEditTipOffsetZ || child == m_trackQuickEditTipOffsetZ || child == m_labelQuickEditSideOffset || child == m_trackQuickEditSideOffset || child == m_labelQuickEditYaw || child == m_trackQuickEditYaw || child == m_labelQuickEditWidthScale || child == m_trackQuickEditWidthScale || child == m_labelQuickEditFollowerInfluence || child == m_trackQuickEditFollowerInfluence || child == m_labelQuickEditBoundarySmoothing || child == m_trackQuickEditBoundarySmoothing || child == m_labelQuickEditBoundaryRadiusUv || child == m_trackQuickEditBoundaryRadiusUv || child == m_labelQuickEditGroupId || child == m_editQuickEditGroupId || child == m_checkQuickEditGuidesOnly || child == m_labelQuickEditVisualization || child == m_btnQuickEditReplace || child == m_btnQuickEditAppend || child == m_btnBrowsePartingMask || child == m_btnBrowseRegionMask || child == m_comboPreset || child == m_comboMaskSource || child == m_comboMaskOverlay || child == m_labelMaskSource || child == m_labelMaskOverlay || child == m_labelPartingMask || child == m_labelRegionMask || child == m_previewPartingMask || child == m_previewRegionMask ||
                child == m_comboRenderMode || child == m_comboDebugColor || child == m_checkSelfCollision || child == m_checkGrid || child == m_checkScalpMotion ||
                child == m_checkShowColliders || child == m_checkShowScalpMesh || child == m_checkShowScalp || child == m_checkShowGridBounds || child == m_checkShowFloor ||
                child == m_checkShowGuides || child == m_checkShowFollowers || child == m_checkShowRoots || child == m_checkShowLinks ||
                child == m_editGuides || child == m_editFollowers || child == m_editParticles || child == m_trackWidth || child == m_trackGravity ||
                child == m_trackDamping || child == m_trackWind || child == m_trackIterations || child == m_trackFollow || child == m_trackSelfRadius ||
                child == m_trackSelfStiffness || child == m_trackGridCellSize || child == m_trackStretchCompliance || child == m_trackBendCompliance ||
                child == m_labelStats || child == m_labelAssetStats || child == m_labelWidth || child == m_labelGravity || child == m_labelDamping ||
                child == m_labelWind || child == m_labelIterations || child == m_labelFollow || child == m_labelSelfRadius || child == m_labelSelfStiffness ||
                child == m_labelGridCellSize || child == m_labelStretchCompliance || child == m_labelBendCompliance ||
                child == m_labelActiveSetTuning || child == m_checkActiveSetAdaptiveBudget || child == m_checkActiveSetHardClamp || child == m_labelActiveSetMaxClusters || child == m_trackActiveSetMaxClusters || child == m_labelActiveSetMaxGuides || child == m_trackActiveSetMaxGuides || child == m_labelActiveSetThreshold || child == m_trackActiveSetThreshold || child == m_labelActiveSetTargetFrame || child == m_trackActiveSetTargetFrame || child == m_labelActiveSetFixedCost || child == m_trackActiveSetFixedCost || child == m_labelActiveSetClusterCost || child == m_trackActiveSetClusterCost || child == m_labelActiveSetScoreWeights || child == m_labelActiveSetMotionWeight || child == m_trackActiveSetMotionWeight || child == m_labelActiveSetContactWeight || child == m_trackActiveSetContactWeight || child == m_labelActiveSetUncertaintyWeight || child == m_trackActiveSetUncertaintyWeight || child == m_labelActiveSetEditWeight || child == m_trackActiveSetEditWeight || child == m_btnExportActiveSetJson || child == m_btnExportRuntimeReport)
            {
                continue;
            }
            wchar_t klass[32] = {};
            GetClassNameW(child, klass, 32);
            if (wcscmp(klass, L"Static") == 0)
            {
                unlabeledStatics.push_back(child);
            }
        }

        size_t staticIndex = 0;
        auto placeAnonymousStatic = [&](int px, int py, int w, int h)
        {
            if (staticIndex < unlabeledStatics.size())
            {
                MoveWindow(unlabeledStatics[staticIndex++], px, py, w, h, TRUE);
            }
        };

        placeAnonymousStatic(x, next(labelHeight), fullWidth, labelHeight);               // Preset
        place(m_comboPreset, x, next(controlHeight), fullWidth, 220);

        const int rowButtonsY = next(controlHeight + gap);
        place(m_btnPlayPause, x, rowButtonsY, threeButtonWidth, controlHeight);
        place(m_btnReset, x + threeButtonWidth + 8, rowButtonsY, threeButtonWidth, controlHeight);
        place(m_btnStep, x + (threeButtonWidth + 8) * 2, rowButtonsY, threeButtonWidth, controlHeight);

        const int assetRowY = next(controlHeight + gap);
        placeAnonymousStatic(x, assetRowY, 50, labelHeight);                               // Guides
        place(m_editGuides, x + 52, assetRowY - 2, editWidth, controlHeight);
        placeAnonymousStatic(x + 136, assetRowY, 100, labelHeight);                        // Followers / guide
        place(m_editFollowers, x + 238, assetRowY - 2, editWidth, controlHeight);
        placeAnonymousStatic(x, next(controlHeight + gap), 110, labelHeight);              // Particles / strand
        place(m_editParticles, x + 114, y - controlHeight - gap - 2, editWidth, controlHeight);
        const int importButtonsX = x + 204;
        const int importButtonsWidth = fullWidth - 204;
        const int importButtonHalf = (importButtonsWidth - 8) / 2;
        place(m_btnApplyAsset, importButtonsX, y - controlHeight - gap - 2, importButtonHalf, controlHeight);
        place(m_btnImportAsset, importButtonsX + importButtonHalf + 8, y - controlHeight - gap - 2, importButtonsWidth - importButtonHalf - 8, controlHeight);
        place(m_btnBrowseEditStack, x, next(controlHeight + gap), halfButtonWidth, controlHeight);
        place(m_btnClearEditStack, x + halfButtonWidth + 8, y - controlHeight, halfButtonWidth, controlHeight);
        place(m_labelEditStack, x, next(labelHeight), fullWidth, labelHeight);
        place(m_labelInlineEditStack, x, next(labelHeight + 2), fullWidth, labelHeight);
        place(m_editInlineEditStack, x, next(50), fullWidth, 44);
        const int inlineButtonRowY = next(controlHeight + gap);
        place(m_btnApplyInlineEditStack, x, inlineButtonRowY, threeButtonWidth, controlHeight);
        place(m_btnSaveInlineEditStack, x + threeButtonWidth + 8, inlineButtonRowY, threeButtonWidth, controlHeight);
        place(m_btnResetInlineEditStack, x + (threeButtonWidth + 8) * 2, inlineButtonRowY, threeButtonWidth, controlHeight);
        place(m_labelQuickEditForm, x, next(labelHeight), fullWidth, labelHeight);
        place(m_labelLayerList, x, next(labelHeight), fullWidth, labelHeight);
        place(m_listInlineLayers, x, next(74), fullWidth, 68);
        const int layerMoveRowY = next(controlHeight + gap);
        place(m_btnQuickEditMoveUp, x, layerMoveRowY, halfButtonWidth, controlHeight);
        place(m_btnQuickEditMoveDown, x + halfButtonWidth + 8, layerMoveRowY, halfButtonWidth, controlHeight);
        place(m_checkQuickEditEnabled, x, next(controlHeight), fullWidth, controlHeight);
        place(m_labelQuickEditName, x, next(labelHeight), fullWidth, labelHeight);
        place(m_editQuickEditName, x, next(controlHeight), fullWidth, controlHeight);
        const int layerActionRowY = next(controlHeight + gap);
        place(m_btnQuickEditDuplicate, x, layerActionRowY, halfButtonWidth, controlHeight);
        place(m_btnQuickEditDelete, x + halfButtonWidth + 8, layerActionRowY, halfButtonWidth, controlHeight);
        place(m_comboQuickEditMode, x, next(controlHeight), fullWidth, 120);
        place(m_labelQuickEditFeather, x, next(labelHeight), fullWidth, labelHeight);
        place(m_trackQuickEditFeather, x, next(sliderHeight), fullWidth, sliderHeight);
        place(m_labelQuickEditLengthScale, x, next(labelHeight), fullWidth, labelHeight);
        place(m_trackQuickEditLengthScale, x, next(sliderHeight), fullWidth, sliderHeight);
        place(m_labelQuickEditLayerWeight, x, next(labelHeight), fullWidth, labelHeight);
        place(m_trackQuickEditLayerWeight, x, next(sliderHeight), fullWidth, sliderHeight);
        place(m_labelQuickEditCenterU, x, next(labelHeight), fullWidth, labelHeight);
        place(m_trackQuickEditCenterU, x, next(sliderHeight), fullWidth, sliderHeight);
        place(m_labelQuickEditCenterV, x, next(labelHeight), fullWidth, labelHeight);
        place(m_trackQuickEditCenterV, x, next(sliderHeight), fullWidth, sliderHeight);
        place(m_labelQuickEditRadiusU, x, next(labelHeight), fullWidth, labelHeight);
        place(m_trackQuickEditRadiusU, x, next(sliderHeight), fullWidth, sliderHeight);
        place(m_labelQuickEditRadiusV, x, next(labelHeight), fullWidth, labelHeight);
        place(m_trackQuickEditRadiusV, x, next(sliderHeight), fullWidth, sliderHeight);
        place(m_labelQuickEditInnerRadius, x, next(labelHeight), fullWidth, labelHeight);
        place(m_trackQuickEditInnerRadius, x, next(sliderHeight), fullWidth, sliderHeight);
        place(m_labelQuickEditAngleCenter, x, next(labelHeight), fullWidth, labelHeight);
        place(m_trackQuickEditAngleCenter, x, next(sliderHeight), fullWidth, sliderHeight);
        place(m_labelQuickEditAngleHalfExtent, x, next(labelHeight), fullWidth, labelHeight);
        place(m_trackQuickEditAngleHalfExtent, x, next(sliderHeight), fullWidth, sliderHeight);
        place(m_labelQuickEditRootOffsetX, x, next(labelHeight), fullWidth, labelHeight);
        place(m_trackQuickEditRootOffsetX, x, next(sliderHeight), fullWidth, sliderHeight);
        place(m_labelQuickEditRootOffsetY, x, next(labelHeight), fullWidth, labelHeight);
        place(m_trackQuickEditRootOffsetY, x, next(sliderHeight), fullWidth, sliderHeight);
        place(m_labelQuickEditRootOffsetZ, x, next(labelHeight), fullWidth, labelHeight);
        place(m_trackQuickEditRootOffsetZ, x, next(sliderHeight), fullWidth, sliderHeight);
        place(m_labelQuickEditTipOffsetX, x, next(labelHeight), fullWidth, labelHeight);
        place(m_trackQuickEditTipOffsetX, x, next(sliderHeight), fullWidth, sliderHeight);
        place(m_labelQuickEditTipOffsetY, x, next(labelHeight), fullWidth, labelHeight);
        place(m_trackQuickEditTipOffsetY, x, next(sliderHeight), fullWidth, sliderHeight);
        place(m_labelQuickEditTipOffsetZ, x, next(labelHeight), fullWidth, labelHeight);
        place(m_trackQuickEditTipOffsetZ, x, next(sliderHeight), fullWidth, sliderHeight);
        place(m_labelQuickEditSideOffset, x, next(labelHeight), fullWidth, labelHeight);
        place(m_trackQuickEditSideOffset, x, next(sliderHeight), fullWidth, sliderHeight);
        place(m_labelQuickEditYaw, x, next(labelHeight), fullWidth, labelHeight);
        place(m_trackQuickEditYaw, x, next(sliderHeight), fullWidth, sliderHeight);
        place(m_labelQuickEditWidthScale, x, next(labelHeight), fullWidth, labelHeight);
        place(m_trackQuickEditWidthScale, x, next(sliderHeight), fullWidth, sliderHeight);
        place(m_labelQuickEditFollowerInfluence, x, next(labelHeight), fullWidth, labelHeight);
        place(m_trackQuickEditFollowerInfluence, x, next(sliderHeight), fullWidth, sliderHeight);
        place(m_labelQuickEditBoundarySmoothing, x, next(labelHeight), fullWidth, labelHeight);
        place(m_trackQuickEditBoundarySmoothing, x, next(sliderHeight), fullWidth, sliderHeight);
        place(m_labelQuickEditBoundaryRadiusUv, x, next(labelHeight), fullWidth, labelHeight);
        place(m_trackQuickEditBoundaryRadiusUv, x, next(sliderHeight), fullWidth, sliderHeight);
        place(m_labelQuickEditGroupId, x, next(labelHeight), fullWidth, labelHeight);
        place(m_editQuickEditGroupId, x, next(controlHeight), fullWidth, controlHeight);
        place(m_checkQuickEditGuidesOnly, x, next(controlHeight), fullWidth, controlHeight);
        place(m_labelQuickEditVisualization, x, next(labelHeight + 4), fullWidth, labelHeight + 4);
        const int quickButtonRowY = next(controlHeight + gap);
        place(m_btnQuickEditReplace, x, quickButtonRowY, halfButtonWidth, controlHeight);
        place(m_btnQuickEditAppend, x + halfButtonWidth + 8, quickButtonRowY, halfButtonWidth, controlHeight);
        place(m_btnBrowsePartingMask, x, next(controlHeight + gap), halfButtonWidth, controlHeight);
        place(m_btnBrowseRegionMask, x + halfButtonWidth + 8, y - controlHeight, halfButtonWidth, controlHeight);
        place(m_labelMaskSource, x, next(labelHeight), fullWidth, labelHeight);
        place(m_comboMaskSource, x, next(controlHeight), fullWidth, 180);
        place(m_labelMaskOverlay, x, next(labelHeight), fullWidth, labelHeight);
        place(m_comboMaskOverlay, x, next(controlHeight), fullWidth, 140);
        const int previewLabelY = next(labelHeight + 2);
        place(m_labelPartingMask, x, previewLabelY, halfButtonWidth, labelHeight);
        place(m_labelRegionMask, x + halfButtonWidth + 8, previewLabelY, halfButtonWidth, labelHeight);
        const int previewY = next(58);
        place(m_previewPartingMask, x, previewY, halfButtonWidth, 52);
        place(m_previewRegionMask, x + halfButtonWidth + 8, previewY, halfButtonWidth, 52);

        placeAnonymousStatic(x, next(labelHeight), fullWidth, labelHeight);                // Render
        place(m_comboRenderMode, x, next(controlHeight), fullWidth, 220);
        placeAnonymousStatic(x, next(labelHeight), fullWidth, labelHeight);                // Color Debug
        place(m_comboDebugColor, x, next(controlHeight), fullWidth, 220);

        place(m_checkSelfCollision, x, next(controlHeight), halfButtonWidth, controlHeight);
        place(m_checkGrid, x + halfButtonWidth + 8, y - controlHeight, halfButtonWidth, controlHeight);
        place(m_checkScalpMotion, x, next(controlHeight), halfButtonWidth, controlHeight);
        place(m_checkShowColliders, x + halfButtonWidth + 8, y - controlHeight, halfButtonWidth, controlHeight);
        place(m_checkShowScalpMesh, x, next(controlHeight), halfButtonWidth, controlHeight);
        place(m_checkShowScalp, x + halfButtonWidth + 8, y - controlHeight, halfButtonWidth, controlHeight);
        place(m_checkShowGridBounds, x, next(controlHeight), halfButtonWidth, controlHeight);
        place(m_checkShowFloor, x + halfButtonWidth + 8, y - controlHeight, halfButtonWidth, controlHeight);
        place(m_checkShowGuides, x, next(controlHeight), halfButtonWidth, controlHeight);
        place(m_checkShowFollowers, x + halfButtonWidth + 8, y - controlHeight, halfButtonWidth, controlHeight);
        place(m_checkShowRoots, x, next(controlHeight), halfButtonWidth, controlHeight);
        place(m_checkShowLinks, x + halfButtonWidth + 8, y - controlHeight, halfButtonWidth, controlHeight);
        y += 4;

        auto placeLabelSlider = [&](HWND label, HWND slider)
        {
            place(label, x, next(labelHeight), fullWidth, labelHeight);
            place(slider, x, next(sliderHeight), fullWidth, sliderHeight);
        };
        placeLabelSlider(m_labelWidth, m_trackWidth);
        placeLabelSlider(m_labelGravity, m_trackGravity);
        placeLabelSlider(m_labelDamping, m_trackDamping);
        placeLabelSlider(m_labelWind, m_trackWind);
        placeLabelSlider(m_labelIterations, m_trackIterations);
        placeLabelSlider(m_labelFollow, m_trackFollow);
        placeLabelSlider(m_labelSelfRadius, m_trackSelfRadius);
        placeLabelSlider(m_labelSelfStiffness, m_trackSelfStiffness);
        placeLabelSlider(m_labelGridCellSize, m_trackGridCellSize);
        placeLabelSlider(m_labelStretchCompliance, m_trackStretchCompliance);
        placeLabelSlider(m_labelBendCompliance, m_trackBendCompliance);

        place(m_labelActiveSetTuning, x, next(labelHeight), fullWidth, labelHeight);
        place(m_checkActiveSetAdaptiveBudget, x, next(controlHeight), halfButtonWidth, controlHeight);
        place(m_checkActiveSetHardClamp, x + halfButtonWidth + 8, y - controlHeight, halfButtonWidth, controlHeight);
        placeLabelSlider(m_labelActiveSetMaxClusters, m_trackActiveSetMaxClusters);
        placeLabelSlider(m_labelActiveSetMaxGuides, m_trackActiveSetMaxGuides);
        placeLabelSlider(m_labelActiveSetThreshold, m_trackActiveSetThreshold);
        placeLabelSlider(m_labelActiveSetTargetFrame, m_trackActiveSetTargetFrame);
        placeLabelSlider(m_labelActiveSetFixedCost, m_trackActiveSetFixedCost);
        placeLabelSlider(m_labelActiveSetClusterCost, m_trackActiveSetClusterCost);
        place(m_labelActiveSetScoreWeights, x, next(labelHeight), fullWidth, labelHeight);
        placeLabelSlider(m_labelActiveSetMotionWeight, m_trackActiveSetMotionWeight);
        placeLabelSlider(m_labelActiveSetContactWeight, m_trackActiveSetContactWeight);
        placeLabelSlider(m_labelActiveSetUncertaintyWeight, m_trackActiveSetUncertaintyWeight);
        placeLabelSlider(m_labelActiveSetEditWeight, m_trackActiveSetEditWeight);

        const int saveRowY = next(controlHeight + gap);
        place(m_btnSaveSettings, x, saveRowY, halfButtonWidth, controlHeight);
        place(m_btnLoadSettings, x + halfButtonWidth + 8, saveRowY, halfButtonWidth, controlHeight);
        place(m_btnExportCsv, x, next(controlHeight + gap), halfButtonWidth, controlHeight);
        place(m_btnExportPerfCsv, x + halfButtonWidth + 8, y - controlHeight - gap, halfButtonWidth, controlHeight);
        const int reportRowY = next(controlHeight + gap);
        place(m_btnExportActiveSetJson, x, reportRowY, halfButtonWidth, controlHeight);
        place(m_btnExportRuntimeReport, x + halfButtonWidth + 8, reportRowY, halfButtonWidth, controlHeight);

        place(m_labelAssetStats, x, next(76), fullWidth, 72);
        place(m_labelStats, x, next(150), fullWidth, 142);
    }

    void GpuHairViewer::RefreshAssetStatsText()
    {
        const std::filesystem::path inlineTempPath = GetInlineEditStackTempPath();
        const std::wstring editStackDisplayName = m_editStackPath.empty() ? std::wstring(L"none") : ((std::filesystem::path(m_editStackPath) == inlineTempPath) ? std::wstring(L"[inline]") : std::filesystem::path(m_editStackPath).filename().wstring());

        std::wstringstream ss;
        ss << L"Asset\n"
           << L"Source: " << m_lastAssetSource << L"\n"
           << L"Guides: " << m_asset.guideStrandCount
           << L"  Followers: " << m_asset.followerStrandCount << L"\n"
           << L"Total strands: " << m_asset.strands.size() << L"\n"
           << L"Particles/strand: " << m_asset.particlesPerStrand
           << L"  Local segments/strand: " << (m_asset.particlesPerStrand > 0u ? (m_asset.particlesPerStrand - 1u) : 0u) << L"\n"
           << L"Segment length: " << std::fixed << std::setprecision(4) << m_simConfig.segmentLength << L" m\n"
           << L"Total particles: " << m_asset.restPoints.size() << L"\n"
           << L"Segments: " << m_renderData.segmentCount << L"\n"
           << L"Import: " << m_lastImportStatus << L"\n"
           << L"Edit stack: " << (m_editStackPath.empty() ? L"none" : std::filesystem::path(m_editStackPath).filename().wstring()) << L"\n"
           << L"Edit status: " << m_lastEditStackStatus << L"\n"
           << L"Active edit guides: " << m_runtimeEditState.activeGuideCount << L" / " << m_asset.guideStrandCount << L"\n"
           << L"Runtime: particle-only GPU path\nEdit boxes auto-rebuild";
        if (m_labelEditStack)
        {
            SetWindowTextFormat(m_labelEditStack, L"Edit Stack: " + editStackDisplayName + L"  [" + m_lastEditStackStatus + L"]");
        }
        SetWindowTextFormat(m_labelAssetStats, ss.str());
    }

    void GpuHairViewer::SyncControlsFromState()
    {
        m_syncingControls = true;

        SetDlgItemInt(m_hwnd, IDC_EDIT_GUIDES, m_builderGuideCount, FALSE);
        SetDlgItemInt(m_hwnd, IDC_EDIT_FOLLOWERS, m_builderFollowersPerGuide, FALSE);
        SetDlgItemInt(m_hwnd, IDC_EDIT_PARTICLES, m_builderParticlesPerStrand, FALSE);

        SendMessageW(m_comboPreset, CB_SETCURSEL,
            (m_activePresetName == L"Calm Studio") ? 1 :
            (m_activePresetName == L"Windy Demo") ? 2 :
            (m_activePresetName == L"Heavy Grounded") ? 3 :
            (m_activePresetName == L"Game-ish LOD") ? 4 : 0, 0);
        SendMessageW(m_comboRenderMode, CB_SETCURSEL,
            (m_renderMode == RenderMode::ParticlePointSprite) ? 0 :
            (m_renderMode == RenderMode::ParticleBillboard) ? 1 : 2, 0);
        SendMessageW(m_comboDebugColor, CB_SETCURSEL, static_cast<WPARAM>(m_debugColorMode), 0);
        SendMessageW(m_comboMaskSource, CB_SETCURSEL, static_cast<WPARAM>(m_maskSourceMode), 0);
        SendMessageW(m_comboMaskOverlay, CB_SETCURSEL, static_cast<WPARAM>(m_maskOverlayMode), 0);

        SendMessageW(m_checkSelfCollision, BM_SETCHECK, m_enableSelfCollision ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(m_checkGrid, BM_SETCHECK, BST_CHECKED, 0);
        SendMessageW(m_checkScalpMotion, BM_SETCHECK, m_enableScalpMotion ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(m_checkShowColliders, BM_SETCHECK, m_showColliderDebug ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(m_checkShowScalpMesh, BM_SETCHECK, m_showScalpMesh ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(m_checkShowScalp, BM_SETCHECK, m_showScalpWireframe ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(m_checkShowGridBounds, BM_SETCHECK, m_showGridBounds ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(m_checkShowFloor, BM_SETCHECK, m_showFloorGuide ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(m_checkShowGuides, BM_SETCHECK, m_showGuideSkeleton ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(m_checkShowFollowers, BM_SETCHECK, m_showFollowerSkeleton ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(m_checkShowRoots, BM_SETCHECK, m_showRootMarkers ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(m_checkShowLinks, BM_SETCHECK, m_showFollowLinks ? BST_CHECKED : BST_UNCHECKED, 0);

        SendMessageW(m_trackWidth, TBM_SETPOS, TRUE, SliderFromFloat(m_widthScale, 0.2f, 8.0f));
        SendMessageW(m_trackGravity, TBM_SETPOS, TRUE, SliderFromFloat(m_simConfig.gravity, 0.0f, 30.0f));
        SendMessageW(m_trackDamping, TBM_SETPOS, TRUE, SliderFromFloat(m_simConfig.globalDamping, 0.0f, 0.15f));
        SendMessageW(m_trackWind, TBM_SETPOS, TRUE, SliderFromFloat(m_simConfig.windStrength, 0.0f, 4.0f));
        SendMessageW(m_trackIterations, TBM_SETPOS, TRUE, SliderFromFloat(static_cast<float>(m_simConfig.solverIterations), 1.0f, 16.0f));
        SendMessageW(m_trackFollow, TBM_SETPOS, TRUE, SliderFromFloat(m_simConfig.followStiffness, 0.0f, 1.0f));
        SendMessageW(m_trackSelfRadius, TBM_SETPOS, TRUE, SliderFromFloat(m_selfCollisionRadius, 0.004f, 0.040f));
        SendMessageW(m_trackSelfStiffness, TBM_SETPOS, TRUE, SliderFromFloat(m_selfCollisionStiffness, 0.0f, 1.0f));
        SendMessageW(m_trackGridCellSize, TBM_SETPOS, TRUE, SliderFromFloat(m_simConfig.selfCollisionCellSize, 0.03f, 0.20f));
        const float stretchT = (std::log10(std::max(m_simConfig.stretchCompliance, 1.0e-9f)) + 9.0f) / 5.0f;
        const float bendT = (std::log10(std::max(m_simConfig.bendCompliance, 1.0e-8f)) + 8.0f) / 5.0f;
        SendMessageW(m_trackStretchCompliance, TBM_SETPOS, TRUE, SliderFromFloat(stretchT, 0.0f, 1.0f));
        SendMessageW(m_trackBendCompliance, TBM_SETPOS, TRUE, SliderFromFloat(bendT, 0.0f, 1.0f));
        SendMessageW(m_checkActiveSetAdaptiveBudget, BM_SETCHECK, m_activeSetBudget.enableBudgetAdaptation ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(m_checkActiveSetHardClamp, BM_SETCHECK, m_activeSetBudget.enableHardBudgetClamp ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(m_trackActiveSetMaxClusters, TBM_SETPOS, TRUE, SliderFromFloat(static_cast<float>(m_activeSetBudget.maxActiveClusters), 1.0f, 64.0f));
        SendMessageW(m_trackActiveSetMaxGuides, TBM_SETPOS, TRUE, SliderFromFloat(static_cast<float>(m_activeSetBudget.maxActiveGuides), 8.0f, 2048.0f));
        SendMessageW(m_trackActiveSetThreshold, TBM_SETPOS, TRUE, SliderFromFloat(m_activeSetBudget.promoteScoreThreshold, 0.0f, 1.0f));
        SendMessageW(m_trackActiveSetTargetFrame, TBM_SETPOS, TRUE, SliderFromFloat(m_activeSetBudget.targetFrameMilliseconds, 4.0f, 16.67f));
        SendMessageW(m_trackActiveSetFixedCost, TBM_SETPOS, TRUE, SliderFromFloat(m_activeSetBudget.estimatedFixedCostMilliseconds, 0.0f, 16.0f));
        SendMessageW(m_trackActiveSetClusterCost, TBM_SETPOS, TRUE, SliderFromFloat(m_activeSetBudget.estimatedClusterCostMilliseconds, 0.05f, 4.0f));
        SendMessageW(m_trackActiveSetMotionWeight, TBM_SETPOS, TRUE, SliderFromFloat(m_activeSetScoringSettings.motionWeight, 0.0f, 1.0f));
        SendMessageW(m_trackActiveSetContactWeight, TBM_SETPOS, TRUE, SliderFromFloat(m_activeSetScoringSettings.contactWeight, 0.0f, 1.0f));
        SendMessageW(m_trackActiveSetUncertaintyWeight, TBM_SETPOS, TRUE, SliderFromFloat(m_activeSetScoringSettings.uncertaintyWeight, 0.0f, 1.0f));
        SendMessageW(m_trackActiveSetEditWeight, TBM_SETPOS, TRUE, SliderFromFloat(m_activeSetScoringSettings.editWeight, 0.0f, 1.0f));
        SendMessageW(m_comboQuickEditMode, CB_SETCURSEL,
            (m_quickEditMode == hairgpu::RegionSelectMode::Rectangle) ? 1 :
            (m_quickEditMode == hairgpu::RegionSelectMode::Sector) ? 2 : 0, 0);
        SendMessageW(m_checkQuickEditEnabled, BM_SETCHECK, m_quickEditEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
        SetWindowTextFormat(m_editQuickEditName, m_quickEditLayerName);
        SendMessageW(m_trackQuickEditFeather, TBM_SETPOS, TRUE, SliderFromFloat(m_quickEditFeather, 0.0f, 1.0f));
        SendMessageW(m_trackQuickEditLengthScale, TBM_SETPOS, TRUE, SliderFromFloat(m_quickEditLengthScale, 0.6f, 1.4f));
        SendMessageW(m_trackQuickEditBoundarySmoothing, TBM_SETPOS, TRUE, SliderFromFloat(m_quickEditBoundarySmoothing, 0.0f, 1.0f));
        SendMessageW(m_trackQuickEditBoundaryRadiusUv, TBM_SETPOS, TRUE, SliderFromFloat(m_quickEditBoundaryRadiusUv, 0.01f, 0.30f));

        SetWindowTextFormat(m_labelWidth, L"Width Scale: " + FormatFloat(m_widthScale, 2));
        SetWindowTextFormat(m_labelGravity, L"Gravity: " + FormatFloat(m_simConfig.gravity, 2));
        SetWindowTextFormat(m_labelDamping, L"Damping: " + FormatFloat(m_simConfig.globalDamping, 3));
        SetWindowTextFormat(m_labelWind, L"Wind Strength: " + FormatFloat(m_simConfig.windStrength, 2));
        SetWindowTextFormat(m_labelIterations, L"Solver Iterations: " + std::to_wstring(m_simConfig.solverIterations));
        SetWindowTextFormat(m_labelFollow, L"Follow Stiffness: " + FormatFloat(m_simConfig.followStiffness, 3));
        SetWindowTextFormat(m_labelSelfRadius, L"Self Radius: " + FormatFloat(m_selfCollisionRadius, 4));
        SetWindowTextFormat(m_labelSelfStiffness, L"Self Stiffness: " + FormatFloat(m_selfCollisionStiffness, 3));
        SetWindowTextFormat(m_labelGridCellSize, L"Grid Cell Size: " + FormatFloat(m_simConfig.selfCollisionCellSize, 3));
        SetWindowTextFormat(m_labelStretchCompliance, L"Stretch Compliance: " + FormatScientific(m_simConfig.stretchCompliance));
        SetWindowTextFormat(m_labelBendCompliance, L"Bend Compliance: " + FormatScientific(m_simConfig.bendCompliance));
        RefreshActiveSetTuningLabels();
        RefreshInlineLayerListFromText(m_selectedInlineLayerIndex);
        RefreshQuickEditFormLabels();
        SetWindowTextFormat(m_btnPlayPause, m_paused ? L"Play" : L"Pause");
        RefreshAssetStatsText();

        m_gpuDebugLinesDirty = true;
        m_syncingControls = false;
    }

    void GpuHairViewer::SyncStateFromControls()
    {
        if (m_syncingControls)
        {
            return;
        }
        m_enableSelfCollision = (SendMessageW(m_checkSelfCollision, BM_GETCHECK, 0, 0) == BST_CHECKED);
        m_enableGridSelfCollision = true;
        m_enableScalpMotion = false;
        m_showColliderDebug = (SendMessageW(m_checkShowColliders, BM_GETCHECK, 0, 0) == BST_CHECKED);
        m_showScalpMesh = (SendMessageW(m_checkShowScalpMesh, BM_GETCHECK, 0, 0) == BST_CHECKED);
        m_maskOverlayMode = static_cast<MaskOverlayMode>(SendMessageW(m_comboMaskOverlay, CB_GETCURSEL, 0, 0));
        m_showScalpWireframe = (SendMessageW(m_checkShowScalp, BM_GETCHECK, 0, 0) == BST_CHECKED);
        m_showGridBounds = (SendMessageW(m_checkShowGridBounds, BM_GETCHECK, 0, 0) == BST_CHECKED);
        m_showFloorGuide = (SendMessageW(m_checkShowFloor, BM_GETCHECK, 0, 0) == BST_CHECKED);
        m_showGuideSkeleton = (SendMessageW(m_checkShowGuides, BM_GETCHECK, 0, 0) == BST_CHECKED);
        m_showFollowerSkeleton = (SendMessageW(m_checkShowFollowers, BM_GETCHECK, 0, 0) == BST_CHECKED);
        m_showRootMarkers = (SendMessageW(m_checkShowRoots, BM_GETCHECK, 0, 0) == BST_CHECKED);
        m_showFollowLinks = (SendMessageW(m_checkShowLinks, BM_GETCHECK, 0, 0) == BST_CHECKED);
        {
            const LRESULT renderSel = SendMessageW(m_comboRenderMode, CB_GETCURSEL, 0, 0);
            m_renderMode = (renderSel <= 0) ? RenderMode::ParticlePointSprite : ((renderSel == 1) ? RenderMode::ParticleBillboard : RenderMode::StrandRibbon);
        }
        m_debugColorMode = static_cast<DebugColorMode>(std::max<LRESULT>(0, SendMessageW(m_comboDebugColor, CB_GETCURSEL, 0, 0)));

        m_widthScale = FloatFromSlider(static_cast<int>(SendMessageW(m_trackWidth, TBM_GETPOS, 0, 0)), 0.2f, 8.0f);
        m_simConfig.gravity = FloatFromSlider(static_cast<int>(SendMessageW(m_trackGravity, TBM_GETPOS, 0, 0)), 0.0f, 30.0f);
        m_simConfig.globalDamping = FloatFromSlider(static_cast<int>(SendMessageW(m_trackDamping, TBM_GETPOS, 0, 0)), 0.0f, 0.15f);
        m_simConfig.windStrength = FloatFromSlider(static_cast<int>(SendMessageW(m_trackWind, TBM_GETPOS, 0, 0)), 0.0f, 4.0f);
        m_simConfig.solverIterations = static_cast<uint32_t>(std::round(FloatFromSlider(static_cast<int>(SendMessageW(m_trackIterations, TBM_GETPOS, 0, 0)), 1.0f, 16.0f)));
        m_simConfig.followStiffness = FloatFromSlider(static_cast<int>(SendMessageW(m_trackFollow, TBM_GETPOS, 0, 0)), 0.0f, 1.0f);
        m_selfCollisionRadius = FloatFromSlider(static_cast<int>(SendMessageW(m_trackSelfRadius, TBM_GETPOS, 0, 0)), 0.004f, 0.040f);
        m_selfCollisionStiffness = FloatFromSlider(static_cast<int>(SendMessageW(m_trackSelfStiffness, TBM_GETPOS, 0, 0)), 0.0f, 1.0f);
        m_simConfig.selfCollisionCellSize = FloatFromSlider(static_cast<int>(SendMessageW(m_trackGridCellSize, TBM_GETPOS, 0, 0)), 0.03f, 0.20f);
        const float stretchT = FloatFromSlider(static_cast<int>(SendMessageW(m_trackStretchCompliance, TBM_GETPOS, 0, 0)), 0.0f, 1.0f);
        const float bendT = FloatFromSlider(static_cast<int>(SendMessageW(m_trackBendCompliance, TBM_GETPOS, 0, 0)), 0.0f, 1.0f);
        m_simConfig.stretchCompliance = std::pow(10.0f, -9.0f + stretchT * 5.0f);
        m_simConfig.bendCompliance = std::pow(10.0f, -8.0f + bendT * 5.0f);
        {
            const LRESULT quickModeSel = SendMessageW(m_comboQuickEditMode, CB_GETCURSEL, 0, 0);
            m_quickEditMode = (quickModeSel == 1) ? hairgpu::RegionSelectMode::Rectangle : ((quickModeSel == 2) ? hairgpu::RegionSelectMode::Sector : hairgpu::RegionSelectMode::Ellipse);
        }
        m_quickEditEnabled = (SendMessageW(m_checkQuickEditEnabled, BM_GETCHECK, 0, 0) == BST_CHECKED);
        m_quickEditLayerName = GetWindowTextString(m_editQuickEditName);
        m_quickEditFeather = FloatFromSlider(static_cast<int>(SendMessageW(m_trackQuickEditFeather, TBM_GETPOS, 0, 0)), 0.0f, 1.0f);
        m_quickEditLengthScale = FloatFromSlider(static_cast<int>(SendMessageW(m_trackQuickEditLengthScale, TBM_GETPOS, 0, 0)), 0.6f, 1.4f);
        m_quickEditLayerWeight = FloatFromSlider(static_cast<int>(SendMessageW(m_trackQuickEditLayerWeight, TBM_GETPOS, 0, 0)), 0.0f, 1.5f);
        m_quickEditCenterU = FloatFromSlider(static_cast<int>(SendMessageW(m_trackQuickEditCenterU, TBM_GETPOS, 0, 0)), 0.0f, 1.0f);
        m_quickEditCenterV = FloatFromSlider(static_cast<int>(SendMessageW(m_trackQuickEditCenterV, TBM_GETPOS, 0, 0)), 0.0f, 1.0f);
        m_quickEditRadiusU = FloatFromSlider(static_cast<int>(SendMessageW(m_trackQuickEditRadiusU, TBM_GETPOS, 0, 0)), 0.01f, 0.60f);
        m_quickEditRadiusV = FloatFromSlider(static_cast<int>(SendMessageW(m_trackQuickEditRadiusV, TBM_GETPOS, 0, 0)), 0.01f, 0.60f);
        m_quickEditInnerRadius = FloatFromSlider(static_cast<int>(SendMessageW(m_trackQuickEditInnerRadius, TBM_GETPOS, 0, 0)), 0.0f, 0.95f);
        m_quickEditAngleCenterRadians = FloatFromSlider(static_cast<int>(SendMessageW(m_trackQuickEditAngleCenter, TBM_GETPOS, 0, 0)), -3.1415926f, 3.1415926f);
        m_quickEditAngleHalfExtentRadians = FloatFromSlider(static_cast<int>(SendMessageW(m_trackQuickEditAngleHalfExtent, TBM_GETPOS, 0, 0)), 0.05f, 3.1415926f);
        m_quickEditRootOffsetX = FloatFromSlider(static_cast<int>(SendMessageW(m_trackQuickEditRootOffsetX, TBM_GETPOS, 0, 0)), -0.12f, 0.12f);
        m_quickEditRootOffsetY = FloatFromSlider(static_cast<int>(SendMessageW(m_trackQuickEditRootOffsetY, TBM_GETPOS, 0, 0)), -0.12f, 0.12f);
        m_quickEditRootOffsetZ = FloatFromSlider(static_cast<int>(SendMessageW(m_trackQuickEditRootOffsetZ, TBM_GETPOS, 0, 0)), -0.12f, 0.12f);
        m_quickEditTipOffsetX = FloatFromSlider(static_cast<int>(SendMessageW(m_trackQuickEditTipOffsetX, TBM_GETPOS, 0, 0)), -0.20f, 0.20f);
        m_quickEditTipOffsetY = FloatFromSlider(static_cast<int>(SendMessageW(m_trackQuickEditTipOffsetY, TBM_GETPOS, 0, 0)), -0.20f, 0.20f);
        m_quickEditTipOffsetZ = FloatFromSlider(static_cast<int>(SendMessageW(m_trackQuickEditTipOffsetZ, TBM_GETPOS, 0, 0)), -0.20f, 0.20f);
        m_quickEditSideOffset = FloatFromSlider(static_cast<int>(SendMessageW(m_trackQuickEditSideOffset, TBM_GETPOS, 0, 0)), -0.20f, 0.20f);
        m_quickEditYawRadians = FloatFromSlider(static_cast<int>(SendMessageW(m_trackQuickEditYaw, TBM_GETPOS, 0, 0)), -1.5707963f, 1.5707963f);
        m_quickEditWidthScale = FloatFromSlider(static_cast<int>(SendMessageW(m_trackQuickEditWidthScale, TBM_GETPOS, 0, 0)), 0.50f, 1.80f);
        m_quickEditFollowerInfluence = FloatFromSlider(static_cast<int>(SendMessageW(m_trackQuickEditFollowerInfluence, TBM_GETPOS, 0, 0)), 0.0f, 1.5f);
        m_quickEditBoundarySmoothing = FloatFromSlider(static_cast<int>(SendMessageW(m_trackQuickEditBoundarySmoothing, TBM_GETPOS, 0, 0)), 0.0f, 1.0f);
        m_quickEditBoundaryRadiusUv = FloatFromSlider(static_cast<int>(SendMessageW(m_trackQuickEditBoundaryRadiusUv, TBM_GETPOS, 0, 0)), 0.01f, 0.30f);
        m_quickEditGroupId = ParseOptionalGroupIdText(GetWindowTextString(m_editQuickEditGroupId));
        m_quickEditGuidesOnly = (SendMessageW(m_checkQuickEditGuidesOnly, BM_GETCHECK, 0, 0) == BST_CHECKED);
        m_quickEditInnerRadius = hairgpu::Clamp(m_quickEditInnerRadius, 0.0f, 0.95f);
        const float quickEditMinRadius = std::min(m_quickEditRadiusU, m_quickEditRadiusV);
        if (quickEditMinRadius > 1.0e-4f)
        {
            m_quickEditInnerRadius = std::min(m_quickEditInnerRadius, quickEditMinRadius * 0.95f);
        }
        m_quickEditBoundarySmoothing = hairgpu::Clamp(m_quickEditBoundarySmoothing, 0.0f, 1.0f);
        m_quickEditBoundaryRadiusUv = hairgpu::Clamp(m_quickEditBoundaryRadiusUv, 0.01f, 0.30f);

        m_activeSetBudget.enableBudgetAdaptation = (SendMessageW(m_checkActiveSetAdaptiveBudget, BM_GETCHECK, 0, 0) == BST_CHECKED);
        m_activeSetBudget.enableHardBudgetClamp = (SendMessageW(m_checkActiveSetHardClamp, BM_GETCHECK, 0, 0) == BST_CHECKED);
        m_activeSetBudget.maxActiveClusters = static_cast<uint32_t>(std::round(FloatFromSlider(static_cast<int>(SendMessageW(m_trackActiveSetMaxClusters, TBM_GETPOS, 0, 0)), 1.0f, 64.0f)));
        m_activeSetBudget.maxActiveGuides = static_cast<uint32_t>(std::round(FloatFromSlider(static_cast<int>(SendMessageW(m_trackActiveSetMaxGuides, TBM_GETPOS, 0, 0)), 8.0f, 2048.0f)));
        m_activeSetBudget.promoteScoreThreshold = hairgpu::Clamp(FloatFromSlider(static_cast<int>(SendMessageW(m_trackActiveSetThreshold, TBM_GETPOS, 0, 0)), 0.0f, 1.0f), 0.0f, 1.0f);
        m_activeSetBudget.targetFrameMilliseconds = hairgpu::Clamp(FloatFromSlider(static_cast<int>(SendMessageW(m_trackActiveSetTargetFrame, TBM_GETPOS, 0, 0)), 4.0f, 16.67f), 4.0f, 16.67f);
        m_activeSetBudget.estimatedFixedCostMilliseconds = hairgpu::Clamp(FloatFromSlider(static_cast<int>(SendMessageW(m_trackActiveSetFixedCost, TBM_GETPOS, 0, 0)), 0.0f, 16.0f), 0.0f, 16.0f);
        m_activeSetBudget.estimatedClusterCostMilliseconds = hairgpu::Clamp(FloatFromSlider(static_cast<int>(SendMessageW(m_trackActiveSetClusterCost, TBM_GETPOS, 0, 0)), 0.05f, 4.0f), 0.05f, 4.0f);
        m_activeSetBudget.minActiveClusters = std::min<uint32_t>(m_activeSetBudget.minActiveClusters, m_activeSetBudget.maxActiveClusters);
        m_activeSetBudget.maxActiveGuides = std::max<uint32_t>(m_activeSetBudget.maxActiveGuides, m_activeSetBudget.maxActiveClusters);
        m_activeSetBudget.frameBudgetMilliseconds = 16.67f;
        m_activeSetScoringSettings.motionWeight = hairgpu::Clamp(FloatFromSlider(static_cast<int>(SendMessageW(m_trackActiveSetMotionWeight, TBM_GETPOS, 0, 0)), 0.0f, 1.0f), 0.0f, 1.0f);
        m_activeSetScoringSettings.contactWeight = hairgpu::Clamp(FloatFromSlider(static_cast<int>(SendMessageW(m_trackActiveSetContactWeight, TBM_GETPOS, 0, 0)), 0.0f, 1.0f), 0.0f, 1.0f);
        m_activeSetScoringSettings.uncertaintyWeight = hairgpu::Clamp(FloatFromSlider(static_cast<int>(SendMessageW(m_trackActiveSetUncertaintyWeight, TBM_GETPOS, 0, 0)), 0.0f, 1.0f), 0.0f, 1.0f);
        m_activeSetScoringSettings.editWeight = hairgpu::Clamp(FloatFromSlider(static_cast<int>(SendMessageW(m_trackActiveSetEditWeight, TBM_GETPOS, 0, 0)), 0.0f, 1.0f), 0.0f, 1.0f);

        BOOL ok = FALSE;
        UINT value = GetDlgItemInt(m_hwnd, IDC_EDIT_GUIDES, &ok, FALSE);
        if (ok) { m_builderGuideCount = std::max<UINT>(64u, std::min<UINT>(value, 4096u)); }
        value = GetDlgItemInt(m_hwnd, IDC_EDIT_FOLLOWERS, &ok, FALSE);
        if (ok) { m_builderFollowersPerGuide = std::min<UINT>(value, 64u); }
        value = GetDlgItemInt(m_hwnd, IDC_EDIT_PARTICLES, &ok, FALSE);
        if (ok) { m_builderParticlesPerStrand = std::max<UINT>(4u, std::min<UINT>(value, 64u)); }

        m_gpuDebugLinesDirty = true;
        SyncControlsFromState();
        if (IsActiveSetRiskOverlayMode())
        {
            std::vector<GpuParticle> particles;
            if (ReadBackCurrentParticlePositions(particles))
            {
                UpdateActiveSetRuntimeFromParticles(particles);
            }
        }
        else if (IsEditInfluenceOverlayMode())
        {
            UpdateScalpOverlayVertexBuffer();
        }
    }

    void GpuHairViewer::ApplyPreset(int presetIndex)
    {
        m_renderMode = RenderMode::StrandRibbon;
        m_showScalpMesh = true;
        m_showGuideSkeleton = false;
        m_showFollowerSkeleton = false;
        m_showRootMarkers = false;
        m_showFollowLinks = false;
        m_showFloorGuide = false;
        m_showColliderDebug = false;
        switch (presetIndex)
        {
        default:
        case 0:
            m_activePresetName = L"Rapunzel Preview";
            m_builderGuideCount = 2500;
            m_builderFollowersPerGuide = 9;
            m_builderParticlesPerStrand = 17;
            m_widthScale = 0.56f;
            m_simConfig.gravity = 18.5f;
            m_simConfig.globalDamping = 0.085f;
            m_simConfig.windStrength = 0.03f;
            m_simConfig.solverIterations = 8;
            m_simConfig.followStiffness = 0.988f;
            m_selfCollisionRadius = 0.0055f;
            m_selfCollisionStiffness = 0.10f;
            m_simConfig.selfCollisionCellSize = 0.060f;
            m_simConfig.stretchCompliance = 2.0e-7f;
            m_simConfig.bendCompliance = 1.5e-5f;
            break;
        case 1:
            m_activePresetName = L"Calm Studio";
            m_builderGuideCount = 2200;
            m_builderFollowersPerGuide = 10;
            m_builderParticlesPerStrand = 15;
            m_widthScale = 0.60f;
            m_simConfig.gravity = 18.0f;
            m_simConfig.globalDamping = 0.060f;
            m_simConfig.windStrength = 0.08f;
            m_simConfig.solverIterations = 8;
            m_simConfig.followStiffness = 0.96f;
            m_selfCollisionRadius = 0.012f;
            m_selfCollisionStiffness = 0.38f;
            m_simConfig.selfCollisionCellSize = 0.085f;
            m_simConfig.stretchCompliance = 1.0e-7f;
            m_simConfig.bendCompliance = 1.0e-5f;
            break;
        case 2:
            m_activePresetName = L"Windy Demo";
            m_builderGuideCount = 2500;
            m_builderFollowersPerGuide = 9;
            m_builderParticlesPerStrand = 17;
            m_widthScale = 0.58f;
            m_simConfig.gravity = 18.5f;
            m_simConfig.globalDamping = 0.030f;
            m_simConfig.windStrength = 1.30f;
            m_simConfig.solverIterations = 7;
            m_simConfig.followStiffness = 0.90f;
            m_selfCollisionRadius = 0.011f;
            m_selfCollisionStiffness = 0.32f;
            m_simConfig.selfCollisionCellSize = 0.080f;
            m_simConfig.stretchCompliance = 2.5e-7f;
            m_simConfig.bendCompliance = 1.8e-5f;
            break;
        case 3:
            m_activePresetName = L"Heavy Grounded";
            m_builderGuideCount = 2800;
            m_builderFollowersPerGuide = 8;
            m_builderParticlesPerStrand = 17;
            m_widthScale = 0.62f;
            m_simConfig.gravity = 22.0f;
            m_simConfig.globalDamping = 0.072f;
            m_simConfig.windStrength = 0.10f;
            m_simConfig.solverIterations = 10;
            m_simConfig.followStiffness = 0.97f;
            m_selfCollisionRadius = 0.015f;
            m_selfCollisionStiffness = 0.52f;
            m_simConfig.selfCollisionCellSize = 0.095f;
            m_simConfig.stretchCompliance = 8.0e-8f;
            m_simConfig.bendCompliance = 6.0e-6f;
            break;
        case 4:
            m_activePresetName = L"Game-ish LOD";
            m_builderGuideCount = 1800;
            m_builderFollowersPerGuide = 6;
            m_builderParticlesPerStrand = 15;
            m_widthScale = 0.54f;
            m_simConfig.gravity = 18.0f;
            m_simConfig.globalDamping = 0.050f;
            m_simConfig.windStrength = 0.25f;
            m_simConfig.solverIterations = 4;
            m_simConfig.followStiffness = 0.90f;
            m_selfCollisionRadius = 0.010f;
            m_selfCollisionStiffness = 0.28f;
            m_simConfig.selfCollisionCellSize = 0.090f;
            m_simConfig.stretchCompliance = 3.5e-7f;
            m_simConfig.bendCompliance = 2.5e-5f;
            break;
        }
        m_enableSelfCollision = false;
        m_enableGridSelfCollision = false;
        m_enableScalpMotion = false;
        SyncControlsFromState();
        RebuildAsset(m_builderGuideCount, m_builderFollowersPerGuide, m_builderParticlesPerStrand);
    }

    bool GpuHairViewer::RebuildAsset(uint32_t guideCount, uint32_t followersPerGuide, uint32_t particlesPerStrand)
    {
        const uint32_t previousParticlesPerStrand = std::max<uint32_t>(m_builderParticlesPerStrand, 2u);
        const float preservedChainLength = m_simConfig.segmentLength * static_cast<float>(std::max<uint32_t>(previousParticlesPerStrand - 1u, 1u));

        m_builderGuideCount = guideCount;
        m_builderFollowersPerGuide = followersPerGuide;
        m_builderParticlesPerStrand = std::max<uint32_t>(particlesPerStrand, 4u);
        const float rebuiltSegmentLength = preservedChainLength / static_cast<float>(std::max<uint32_t>(m_builderParticlesPerStrand - 1u, 1u));
        m_simConfig.segmentLength = std::clamp(rebuiltSegmentLength, 0.015f, 0.060f);

        ApplySampleMaskOverrides();
        m_asset = BuildSampleHybridHairAsset(guideCount, followersPerGuide, m_builderParticlesPerStrand, m_simConfig.segmentLength);
        {
            WigStyleSettings wig;
            wig.strength = 0.02f;
            wig.capRegion = 0.08f;
            wig.shellOffset = 0.00015f;
            wig.flowBlend = 1.0f;
            wig.outwardBlend = 0.0f;
            wig.restoreOriginalTowardsTip = 0.0f;
            StyleAssetToScalpWig(m_asset, wig);
        }
        m_lastAssetSource = L"Sample Hybrid";
        m_lastImportStatus = L"Procedural sample asset rebuilt";
        m_baseAsset = m_asset;
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
            MessageBoxW(m_hwnd, L"Failed to rebuild simulation resources after changing the asset.", L"Rebuild Failed", MB_OK | MB_ICONERROR);
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

    void GpuHairViewer::UpdateStatsLabel()
    {
        const float avgFrameMs = !m_performanceSamples.empty()
            ? std::accumulate(m_performanceSamples.begin(), m_performanceSamples.end(), 0.0f,
                [](float acc, const PerformanceSample& s) { return acc + s.frameMilliseconds; }) / static_cast<float>(m_performanceSamples.size())
            : 0.0f;
        const float avgSimMs = !m_performanceSamples.empty()
            ? std::accumulate(m_performanceSamples.begin(), m_performanceSamples.end(), 0.0f,
                [](float acc, const PerformanceSample& s) { return acc + s.simulationMilliseconds; }) / static_cast<float>(m_performanceSamples.size())
            : 0.0f;

        const uint64_t totalParticles = static_cast<uint64_t>(m_asset.restPoints.size());
        const uint64_t guideParticles = static_cast<uint64_t>(m_asset.guideStrandCount) * static_cast<uint64_t>(m_asset.particlesPerStrand);
        const uint64_t followerParticles = totalParticles > guideParticles ? (totalParticles - guideParticles) : 0ull;

        std::wstringstream ss;
        ss << L"Runtime\n"
           << L"Preset: " << m_activePresetName << L"\n"
           << L"FPS: " << std::fixed << std::setprecision(1) << m_smoothedFps
           << L"  Frame: " << std::setprecision(2) << (m_lastFrameSeconds * 1000.0f) << L" ms\n"
           << L"Sim Step: " << std::setprecision(2) << m_lastSimulationStepMilliseconds << L" ms"
           << L"  AvgSim: " << avgSimMs << L" ms\n"
           << L"GPU Frame: " << m_lastGpuFrameMilliseconds << L" ms"
           << L"  GPU Sim: " << m_lastGpuSimulationMilliseconds << L" ms\n"
           << L"GPU Hair: " << m_lastGpuHairDrawMilliseconds << L" ms"
           << L"  GPU Debug: " << m_lastGpuDebugDrawMilliseconds << L" ms\n"
           << L"Avg Frame: " << avgFrameMs << L" ms"
           << L"  Draws: " << m_lastTotalDrawCalls << L"\n"
           << L"Mode: " << (m_renderMode == RenderMode::ParticlePointSprite ? L"Particle Point Sprite" : (m_renderMode == RenderMode::ParticleBillboard ? L"Particle Billboard" : L"Strand Ribbon"))
           << L"  Quads: " << totalParticles
           << L"  HairPrims: " << m_lastCurrentHairPrimitiveCount << L"\n"
           << L"Guide particles: " << guideParticles
           << L"  Follower particles: " << followerParticles << L"\n"
           << L"Debug GPU Lines: total=" << m_debugLineStats.totalLineCount
           << L"  world=" << m_debugLineStats.worldLineCount
           << L"  hair=" << m_debugLineStats.hairLineCount
           << L"  scalp=" << m_debugLineStats.scalpLineCount
           << L"  roots=" << m_debugLineStats.rootMarkerCount << L"\n"
           << L"Hair Pipe: VS=" << m_lastHairPipelineStats.VSInvocations
           << L"  GSPrims=" << m_lastHairPipelineStats.GSPrimitives
           << L"  PS=" << m_lastHairPipelineStats.PSInvocations << L"\n"
           << L"Debug Pipe: VS=" << m_lastDebugPipelineStats.VSInvocations
           << L"  Prims=" << m_lastDebugPipelineStats.IAPrimitives
           << L"  PS=" << m_lastDebugPipelineStats.PSInvocations << L"\n"
           << L"Colors: " << (m_debugColorMode == DebugColorMode::GuideFollower ? L"Guide/Follower" : (m_debugColorMode == DebugColorMode::WidthHeatmap ? L"Width" : L"Natural"))
           << L"  Scalp: " << GetMaskOverlayModeDisplayName() << L"\n";
        ss << L"Active Set: " << (m_enableActiveSetRuntime ? L"On" : L"Off")
           << L"  Plan: " << m_activeSetPlan.summary.selectedClusterCount << L"/" << m_activeSetPlan.summary.clusterCount << L" clusters"
           << L"  Guides: " << m_activeSetPlan.summary.selectedGuideCount
           << L"  Eval: " << std::setprecision(2) << m_activeSetEvaluationMilliseconds << L" ms\n";
        ss << L"Active Score: hi=" << m_activeSetPlan.summary.highestScore
           << L"  avg=" << m_activeSetPlan.summary.averageSelectedScore
           << L"  status=" << m_activeSetLastStatus << L"\n";
        ss << L"Budget: scale=" << m_activeSetPlan.summary.budgetScale
           << L"  solver=" << m_activeSetPlan.summary.recommendedSolverIterations
           << L"  tiers F/R/Fz/C=" << m_activeSetPlan.summary.fullTierCount << L"/"
           << m_activeSetPlan.summary.reducedTierCount << L"/"
           << m_activeSetPlan.summary.frozenTierCount << L"/"
           << m_activeSetPlan.summary.cachedTierCount << L"\n";
        ss << L"Tune: maxC=" << m_activeSetBudget.maxActiveClusters
           << L" maxG=" << m_activeSetBudget.maxActiveGuides
           << L" thr=" << m_activeSetBudget.promoteScoreThreshold
           << L" tgt=" << m_activeSetBudget.targetFrameMilliseconds << L" ms"
           << (m_activeSetBudget.enableBudgetAdaptation ? L" adapt" : L" fixed")
           << (m_activeSetBudget.enableHardBudgetClamp ? L" clamp" : L" no-clamp") << L"\n";
        ss << L"Weights: motion=" << m_activeSetScoringSettings.motionWeight
           << L" contact=" << m_activeSetScoringSettings.contactWeight
           << L" uncert=" << m_activeSetScoringSettings.uncertaintyWeight
           << L" edit=" << m_activeSetScoringSettings.editWeight << L"\n";
        ss << L"Corrective: strands=" << m_sparseCorrectiveReport.correctedStrandCount
           << L"  points=" << m_sparseCorrectiveReport.correctedPointCount
           << L"  avgPen=" << m_sparseCorrectiveReport.averagePenetration
           << L"  maxPen=" << m_sparseCorrectiveReport.maxPenetration << L"\n";
        ss << L"GPU runtime: guide solve + follower interpolate\n";
        ss << L"Self Collision: " << (m_enableSelfCollision ? L"Density Grid Repulsion" : L"Off")
           << L"  Angular XPBD: " << (m_enableRuntimeAngularBend ? L"On" : L"Off")
           << L"  Samples: " << m_performanceSamples.size()
           << (m_paused ? L"\n[PAUSED]" : L"");
        SetWindowTextFormat(m_labelStats, ss.str());
    }
    bool GpuHairViewer::SaveSettingsToFile(const std::wstring& filePath) const
    {
        std::ofstream out{ std::filesystem::path(filePath) };
        if (!out)
        {
            return false;
        }
        out << "preset=" << WideToUtf8(m_activePresetName) << "\n";
        out << "guides=" << m_builderGuideCount << "\n";
        out << "followersPerGuide=" << m_builderFollowersPerGuide << "\n";
        out << "particlesPerStrand=" << m_builderParticlesPerStrand << "\n";
        out << "widthScale=" << m_widthScale << "\n";
        out << "gravity=" << m_simConfig.gravity << "\n";
        out << "damping=" << m_simConfig.globalDamping << "\n";
        out << "windStrength=" << m_simConfig.windStrength << "\n";
        out << "solverIterations=" << m_simConfig.solverIterations << "\n";
        out << "followStiffness=" << m_simConfig.followStiffness << "\n";
        out << "selfCollisionRadius=" << m_selfCollisionRadius << "\n";
        out << "selfCollisionStiffness=" << m_selfCollisionStiffness << "\n";
        out << "gridCellSize=" << m_simConfig.selfCollisionCellSize << "\n";
        out << "broadphaseMode=" << m_selfCollisionBroadphaseMode << "\n";
        out << "hashBucketCount=" << m_simConfig.selfCollisionHashBucketCount << "\n";
        out << "stretchCompliance=" << m_simConfig.stretchCompliance << "\n";
        out << "bendCompliance=" << m_simConfig.bendCompliance << "\n";
        out << "enableSelfCollision=" << (m_enableSelfCollision ? 1 : 0) << "\n";
        out << "enableGridSelfCollision=" << (m_enableGridSelfCollision ? 1 : 0) << "\n";
        out << "enableRuntimeAngularBend=" << (m_enableRuntimeAngularBend ? 1 : 0) << "\n";
        out << "runtimeAngularBendRadians=" << m_runtimeAngularBendRadians << "\n";
        out << "runtimeAngularBendCompliance=" << m_runtimeAngularBendCompliance << "\n";
        out << "runtimeAngularBendStiffness=" << m_runtimeAngularBendStiffness << "\n";
        out << "runtimeAngularBendDamping=" << m_runtimeAngularBendDamping << "\n";
        out << "enableScalpMotion=" << (m_enableScalpMotion ? 1 : 0) << "\n";
        out << "showColliderDebug=" << (m_showColliderDebug ? 1 : 0) << "\n";
        out << "showScalpMesh=" << (m_showScalpMesh ? 1 : 0) << "\n";
        out << "showScalpWireframe=" << (m_showScalpWireframe ? 1 : 0) << "\n";
        out << "showGridBounds=" << (m_showGridBounds ? 1 : 0) << "\n";
        out << "showFloorGuide=" << (m_showFloorGuide ? 1 : 0) << "\n";
        out << "showGuideSkeleton=" << (m_showGuideSkeleton ? 1 : 0) << "\n";
        out << "showFollowerSkeleton=" << (m_showFollowerSkeleton ? 1 : 0) << "\n";
        out << "showRootMarkers=" << (m_showRootMarkers ? 1 : 0) << "\n";
        out << "showFollowLinks=" << (m_showFollowLinks ? 1 : 0) << "\n";
        out << "renderMode=" << (m_renderMode == RenderMode::ParticlePointSprite ? 0 : (m_renderMode == RenderMode::ParticleBillboard ? 1 : 2)) << "\n";
        out << "debugColorMode=" << static_cast<uint32_t>(m_debugColorMode) << "\n";
        out << "maskSourceMode=" << static_cast<uint32_t>(m_maskSourceMode) << "\n";
        out << "maskOverlayMode=" << static_cast<uint32_t>(m_maskOverlayMode) << "\n";
        out << "editStackPath=" << WideToUtf8(m_editStackPath) << "\n";
        out << "partingMaskPath=" << WideToUtf8(m_partingMaskPath) << "\n";
        out << "regionMaskPath=" << WideToUtf8(m_regionMaskPath) << "\n";
        out << "inlineEditStackText=" << EscapeSettingsText(WideToUtf8(GetInlineEditStackText())) << "\n";
        out << "selectedInlineLayerIndex=" << std::max(m_selectedInlineLayerIndex, 0) << "\n";
        out << "activeSetAdaptiveBudget=" << (m_activeSetBudget.enableBudgetAdaptation ? 1 : 0) << "\n";
        out << "activeSetHardClamp=" << (m_activeSetBudget.enableHardBudgetClamp ? 1 : 0) << "\n";
        out << "activeSetMaxClusters=" << m_activeSetBudget.maxActiveClusters << "\n";
        out << "activeSetMaxGuides=" << m_activeSetBudget.maxActiveGuides << "\n";
        out << "activeSetPromoteThreshold=" << m_activeSetBudget.promoteScoreThreshold << "\n";
        out << "activeSetTargetFrameMs=" << m_activeSetBudget.targetFrameMilliseconds << "\n";
        out << "activeSetFixedCostMs=" << m_activeSetBudget.estimatedFixedCostMilliseconds << "\n";
        out << "activeSetClusterCostMs=" << m_activeSetBudget.estimatedClusterCostMilliseconds << "\n";
        out << "activeSetMotionWeight=" << m_activeSetScoringSettings.motionWeight << "\n";
        out << "activeSetContactWeight=" << m_activeSetScoringSettings.contactWeight << "\n";
        out << "activeSetUncertaintyWeight=" << m_activeSetScoringSettings.uncertaintyWeight << "\n";
        out << "activeSetEditWeight=" << m_activeSetScoringSettings.editWeight << "\n";
        return true;
    }

    bool GpuHairViewer::LoadSettingsFromFile(const std::wstring& filePath)
    {
        std::ifstream in{ std::filesystem::path(filePath) };
        if (!in)
        {
            return false;
        }

        std::unordered_map<std::string, std::string> values;
        std::string line;
        while (std::getline(in, line))
        {
            const size_t eq = line.find('=');
            if (eq == std::string::npos)
            {
                continue;
            }
            values[line.substr(0, eq)] = line.substr(eq + 1);
        }

        auto getFloat = [&](const char* key, float current)
        {
            auto it = values.find(key);
            return (it != values.end()) ? std::stof(it->second) : current;
        };
        auto getUInt = [&](const char* key, uint32_t current)
        {
            auto it = values.find(key);
            return (it != values.end()) ? static_cast<uint32_t>(std::stoul(it->second)) : current;
        };
        auto getBool = [&](const char* key, bool current)
        {
            auto it = values.find(key);
            return (it != values.end()) ? (std::stoi(it->second) != 0) : current;
        };

        if (auto it = values.find("preset"); it != values.end())
        {
            m_activePresetName = Utf8ToWide(it->second);
        }
        m_builderGuideCount = getUInt("guides", m_builderGuideCount);
        m_builderFollowersPerGuide = getUInt("followersPerGuide", m_builderFollowersPerGuide);
        m_builderParticlesPerStrand = getUInt("particlesPerStrand", m_builderParticlesPerStrand);
        m_widthScale = getFloat("widthScale", m_widthScale);
        m_simConfig.gravity = getFloat("gravity", m_simConfig.gravity);
        m_simConfig.globalDamping = getFloat("damping", m_simConfig.globalDamping);
        m_simConfig.windStrength = getFloat("windStrength", m_simConfig.windStrength);
        m_simConfig.solverIterations = getUInt("solverIterations", m_simConfig.solverIterations);
        m_simConfig.followStiffness = getFloat("followStiffness", m_simConfig.followStiffness);
        m_selfCollisionRadius = getFloat("selfCollisionRadius", m_selfCollisionRadius);
        m_selfCollisionStiffness = getFloat("selfCollisionStiffness", m_selfCollisionStiffness);
        m_simConfig.selfCollisionCellSize = getFloat("gridCellSize", m_simConfig.selfCollisionCellSize);
        m_selfCollisionBroadphaseMode = 0u;
        m_simConfig.selfCollisionHashBucketCount = std::max<uint32_t>(getUInt("hashBucketCount", m_simConfig.selfCollisionHashBucketCount), 1u);
        m_enableRuntimeAngularBend = getBool("enableRuntimeAngularBend", m_enableRuntimeAngularBend);
        m_runtimeAngularBendRadians = getFloat("runtimeAngularBendRadians", m_runtimeAngularBendRadians);
        m_runtimeAngularBendCompliance = getFloat("runtimeAngularBendCompliance", m_runtimeAngularBendCompliance);
        m_runtimeAngularBendStiffness = getFloat("runtimeAngularBendStiffness", m_runtimeAngularBendStiffness);
        m_runtimeAngularBendDamping = getFloat("runtimeAngularBendDamping", m_runtimeAngularBendDamping);
        m_simConfig.stretchCompliance = getFloat("stretchCompliance", m_simConfig.stretchCompliance);
        m_simConfig.bendCompliance = getFloat("bendCompliance", m_simConfig.bendCompliance);
        m_enableSelfCollision = getBool("enableSelfCollision", m_enableSelfCollision);
        m_enableGridSelfCollision = true;
        m_enableScalpMotion = false;
        m_showColliderDebug = getBool("showColliderDebug", m_showColliderDebug);
        m_showScalpMesh = getBool("showScalpMesh", m_showScalpMesh);
        m_showScalpWireframe = getBool("showScalpWireframe", m_showScalpWireframe);
        m_showGridBounds = getBool("showGridBounds", m_showGridBounds);
        m_showFloorGuide = getBool("showFloorGuide", m_showFloorGuide);
        m_showGuideSkeleton = getBool("showGuideSkeleton", m_showGuideSkeleton);
        m_showFollowerSkeleton = getBool("showFollowerSkeleton", m_showFollowerSkeleton);
        m_showRootMarkers = getBool("showRootMarkers", m_showRootMarkers);
        m_showFollowLinks = getBool("showFollowLinks", m_showFollowLinks);
        { const int renderModeValue = static_cast<int>(getUInt("renderMode", static_cast<unsigned>(m_renderMode == RenderMode::ParticlePointSprite ? 0 : (m_renderMode == RenderMode::ParticleBillboard ? 1 : 2))));
          m_renderMode = (renderModeValue <= 0) ? RenderMode::ParticlePointSprite : ((renderModeValue == 1) ? RenderMode::ParticleBillboard : RenderMode::StrandRibbon); }
        m_debugColorMode = static_cast<DebugColorMode>(std::min<uint32_t>(getUInt("debugColorMode", static_cast<uint32_t>(m_debugColorMode)), 2u));
        if (auto it = values.find("editStackPath"); it != values.end()) m_editStackPath = Utf8ToWide(it->second);
        if (auto it = values.find("partingMaskPath"); it != values.end()) m_partingMaskPath = Utf8ToWide(it->second);
        if (auto it = values.find("regionMaskPath"); it != values.end()) m_regionMaskPath = Utf8ToWide(it->second);
        std::wstring inlineEditStackText = BuildDefaultInlineEditStackText();
        if (auto it = values.find("inlineEditStackText"); it != values.end()) inlineEditStackText = Utf8ToWide(UnescapeSettingsText(it->second));
        if (auto it = values.find("selectedInlineLayerIndex"); it != values.end()) m_selectedInlineLayerIndex = std::max(0, std::stoi(it->second));
        m_maskSourceMode = static_cast<MaskSourceMode>(std::min<uint32_t>(getUInt("maskSourceMode", static_cast<uint32_t>(m_maskSourceMode)), 2u));
        m_maskOverlayMode = static_cast<MaskOverlayMode>(std::min<uint32_t>(getUInt("maskOverlayMode", static_cast<uint32_t>(m_maskOverlayMode)), 9u));
        m_activeSetBudget.enableBudgetAdaptation = getBool("activeSetAdaptiveBudget", m_activeSetBudget.enableBudgetAdaptation);
        m_activeSetBudget.enableHardBudgetClamp = getBool("activeSetHardClamp", m_activeSetBudget.enableHardBudgetClamp);
        m_activeSetBudget.maxActiveClusters = std::max<uint32_t>(1u, getUInt("activeSetMaxClusters", m_activeSetBudget.maxActiveClusters));
        m_activeSetBudget.maxActiveGuides = std::max<uint32_t>(m_activeSetBudget.maxActiveClusters, getUInt("activeSetMaxGuides", m_activeSetBudget.maxActiveGuides));
        m_activeSetBudget.promoteScoreThreshold = hairgpu::Clamp(getFloat("activeSetPromoteThreshold", m_activeSetBudget.promoteScoreThreshold), 0.0f, 1.0f);
        m_activeSetBudget.targetFrameMilliseconds = hairgpu::Clamp(getFloat("activeSetTargetFrameMs", m_activeSetBudget.targetFrameMilliseconds), 4.0f, 16.67f);
        m_activeSetBudget.estimatedFixedCostMilliseconds = hairgpu::Clamp(getFloat("activeSetFixedCostMs", m_activeSetBudget.estimatedFixedCostMilliseconds), 0.0f, 16.0f);
        m_activeSetBudget.estimatedClusterCostMilliseconds = hairgpu::Clamp(getFloat("activeSetClusterCostMs", m_activeSetBudget.estimatedClusterCostMilliseconds), 0.05f, 4.0f);
        m_activeSetBudget.frameBudgetMilliseconds = 16.67f;
        m_activeSetScoringSettings.motionWeight = hairgpu::Clamp(getFloat("activeSetMotionWeight", m_activeSetScoringSettings.motionWeight), 0.0f, 1.0f);
        m_activeSetScoringSettings.contactWeight = hairgpu::Clamp(getFloat("activeSetContactWeight", m_activeSetScoringSettings.contactWeight), 0.0f, 1.0f);
        m_activeSetScoringSettings.uncertaintyWeight = hairgpu::Clamp(getFloat("activeSetUncertaintyWeight", m_activeSetScoringSettings.uncertaintyWeight), 0.0f, 1.0f);
        m_activeSetScoringSettings.editWeight = hairgpu::Clamp(getFloat("activeSetEditWeight", m_activeSetScoringSettings.editWeight), 0.0f, 1.0f);

        SyncControlsFromState();
        SetWindowTextW(m_editInlineEditStack, inlineEditStackText.c_str());
        LoadQuickEditFormFromInlineText(m_selectedInlineLayerIndex);
        if (!inlineEditStackText.empty())
        {
            const std::filesystem::path tempInlinePath = GetInlineEditStackTempPath();
            WriteUtf8TextFile(tempInlinePath, WideToUtf8(inlineEditStackText));
            if (m_editStackPath.empty() || !std::filesystem::exists(std::filesystem::path(m_editStackPath)))
            {
                m_editStackPath = tempInlinePath.wstring();
            }
        }
        RefreshMaskPreviewControls();
        return RebuildAsset(m_builderGuideCount, m_builderFollowersPerGuide, m_builderParticlesPerStrand);
    }

    bool GpuHairViewer::BrowseAndSetEditStackPath()
    {
        wchar_t fileName[MAX_PATH] = L"";
        OPENFILENAMEW ofn = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = m_hwnd;
        ofn.lpstrFilter = L"Edit Stack CSV (*.csv)\0*.csv\0All Files\0*.*\0";
        ofn.lpstrFile = fileName;
        ofn.nMaxFile = MAX_PATH;
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
        if (!GetOpenFileNameW(&ofn))
        {
            return false;
        }
        m_editStackPath = fileName;
        try
        {
            std::ifstream in(std::filesystem::path(fileName), std::ios::binary);
            std::string inlineText((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            if (!inlineText.empty())
            {
                SetWindowTextW(m_editInlineEditStack, Utf8ToWide(inlineText).c_str());
                LoadQuickEditFormFromInlineText(m_selectedInlineLayerIndex);
            }
        }
        catch (...)
        {
        }
        return ApplyEditStackToCurrentAsset(true);
    }

    bool GpuHairViewer::ClearEditStackAndRestoreAsset()
    {
        m_editStackPath.clear();
        m_lastEditStackStatus = L"No edit stack";
        m_runtimeEditState = {};
        if (m_baseAsset.strands.empty())
        {
            RefreshAssetStatsText();
            SyncControlsFromState();
            return true;
        }
        m_asset = m_baseAsset;
        if (!CreateSimulationResources())
        {
            m_lastEditStackStatus = L"Restore failed";
            RefreshAssetStatsText();
            return false;
        }
        RebuildActiveSetFoundation();
        RecomputeAssetBounds();
        QueueVisibilityRecovery();
        RefreshAssetStatsText();
        ResetSimulation();
        SyncControlsFromState();
        UpdateScalpOverlayVertexBuffer();
        return true;
    }

    bool GpuHairViewer::ApplyEditStackToAsset(hairgpu::HairAsset& asset, std::wstring* outStatusText)
    {
        auto setStatus = [&](const std::wstring& status)
        {
            m_lastEditStackStatus = status;
            if (outStatusText)
            {
                *outStatusText = status;
            }
        };

        m_runtimeEditState = {};
        m_runtimeEditState.guideInfluence.assign(std::min<uint32_t>(asset.guideStrandCount, static_cast<uint32_t>(asset.strands.size())), 0.0f);
        m_runtimeEditState.strandInfluence.assign(asset.strands.size(), 0.0f);

        if (m_editStackPath.empty())
        {
            setStatus(L"No edit stack");
            return true;
        }

        hairgpu::EditStack stack = {};
        std::string error;
        if (!LocalLoadEditStackCsv(std::filesystem::path(m_editStackPath), stack, error))
        {
            setStatus(L"Load failed");
            return false;
        }

        hairgpu::HairAsset edited = asset;
        hairgpu::RuntimeEditState state = {};
        if (!hairgpu::ApplyEditStackToAsset(edited, asset, stack, &state, error))
        {
            setStatus(L"Apply failed");
            return false;
        }

        asset = std::move(edited);
        m_runtimeEditState = std::move(state);
        std::wstringstream ss;
        ss << L"Applied " << stack.layers.size() << L" layers";
        setStatus(ss.str());
        return true;
    }

    bool GpuHairViewer::ApplyEditStackToCurrentAsset(bool showMessageOnFailure)
    {
        if (m_baseAsset.strands.empty())
        {
            m_baseAsset = m_asset;
        }
        hairgpu::HairAsset edited = m_baseAsset;
        std::wstring status;
        if (!ApplyEditStackToAsset(edited, &status))
        {
            RefreshAssetStatsText();
            UpdateScalpOverlayVertexBuffer();
            if (showMessageOnFailure)
            {
                const std::wstring message = L"Edit stack apply failed.\r\n\r\n" + status + L"\r\nPath: " + m_editStackPath;
                MessageBoxW(m_hwnd, message.c_str(), L"Edit Stack", MB_OK | MB_ICONERROR);
            }
            return false;
        }
        m_asset = std::move(edited);
        if (!CreateSimulationResources())
        {
            if (showMessageOnFailure)
            {
                MessageBoxW(m_hwnd, L"Edit stack was applied, but GPU resources failed to rebuild.", L"Edit Stack", MB_OK | MB_ICONERROR);
            }
            return false;
        }
        RebuildActiveSetFoundation();
        RecomputeAssetBounds();
        QueueVisibilityRecovery();
        RefreshAssetStatsText();
        ResetSimulation();
        SyncControlsFromState();
        UpdateScalpOverlayVertexBuffer();
        return true;
    }

    std::wstring GpuHairViewer::GetInlineEditStackText() const
    {
        return GetWindowTextString(m_editInlineEditStack);
    }

    std::filesystem::path GpuHairViewer::GetInlineEditStackTempPath() const
    {
        try
        {
            return std::filesystem::temp_directory_path() / "hairgpu_viewer_inline_edit_stack.csv";
        }
        catch (...)
        {
            return std::filesystem::path("hairgpu_viewer_inline_edit_stack.csv");
        }
    }

    void GpuHairViewer::ResetInlineEditStackText()
    {
        SetWindowTextW(m_editInlineEditStack, BuildDefaultInlineEditStackText().c_str());
        LoadQuickEditFormFromInlineText(m_selectedInlineLayerIndex);
    }

    bool GpuHairViewer::SaveInlineEditStackToPath(const std::wstring& filePath, bool applyAfterSave)
    {
        const std::wstring inlineText = GetInlineEditStackText();
        if (!WriteUtf8TextFile(std::filesystem::path(filePath), WideToUtf8(inlineText)))
        {
            return false;
        }

        m_editStackPath = filePath;
        if (applyAfterSave)
        {
            return ApplyEditStackToCurrentAsset(true);
        }

        SyncControlsFromState();
        return true;
    }

    bool GpuHairViewer::ApplyInlineEditStackFromUi(bool showMessageOnFailure)
    {
        const std::wstring inlineText = GetInlineEditStackText();
        const std::filesystem::path tempPath = GetInlineEditStackTempPath();
        if (!WriteUtf8TextFile(tempPath, WideToUtf8(inlineText)))
        {
            if (showMessageOnFailure)
            {
                MessageBoxW(m_hwnd, L"Failed to stage the inline edit stack text.", L"Inline Edit Stack", MB_OK | MB_ICONERROR);
            }
            return false;
        }

        m_editStackPath = tempPath.wstring();
        return ApplyEditStackToCurrentAsset(showMessageOnFailure);
    }

    void GpuHairViewer::RefreshActiveSetTuningLabels()
    {
        if (!m_labelActiveSetMaxClusters)
        {
            return;
        }
        SetWindowTextFormat(m_labelActiveSetMaxClusters, L"Max Active Clusters: " + std::to_wstring(m_activeSetBudget.maxActiveClusters));
        SetWindowTextFormat(m_labelActiveSetMaxGuides, L"Max Active Guides: " + std::to_wstring(m_activeSetBudget.maxActiveGuides));
        SetWindowTextFormat(m_labelActiveSetThreshold, L"Promote Threshold: " + FormatFloat(m_activeSetBudget.promoteScoreThreshold, 2));
        SetWindowTextFormat(m_labelActiveSetTargetFrame, L"Target Frame: " + FormatFloat(m_activeSetBudget.targetFrameMilliseconds, 2) + L" ms");
        SetWindowTextFormat(m_labelActiveSetFixedCost, L"Estimated Fixed Cost: " + FormatFloat(m_activeSetBudget.estimatedFixedCostMilliseconds, 2) + L" ms");
        SetWindowTextFormat(m_labelActiveSetClusterCost, L"Estimated Cluster Cost: " + FormatFloat(m_activeSetBudget.estimatedClusterCostMilliseconds, 2) + L" ms");
        SetWindowTextFormat(m_labelActiveSetMotionWeight, L"Motion Weight: " + FormatFloat(m_activeSetScoringSettings.motionWeight, 2));
        SetWindowTextFormat(m_labelActiveSetContactWeight, L"Contact Weight: " + FormatFloat(m_activeSetScoringSettings.contactWeight, 2));
        SetWindowTextFormat(m_labelActiveSetUncertaintyWeight, L"Uncertainty Weight: " + FormatFloat(m_activeSetScoringSettings.uncertaintyWeight, 2));
        SetWindowTextFormat(m_labelActiveSetEditWeight, L"Edit Weight: " + FormatFloat(m_activeSetScoringSettings.editWeight, 2));
        std::wstring tuningHeader = L"Active-Set Runtime Tuning";
        tuningHeader += m_activeSetBudget.enableBudgetAdaptation ? L" [adaptive]" : L" [fixed]";
        tuningHeader += m_activeSetBudget.enableHardBudgetClamp ? L" [clamp]" : L" [open]";
        SetWindowTextFormat(m_labelActiveSetTuning, tuningHeader);
    }

    void GpuHairViewer::RefreshQuickEditFormLabels()
    {
        std::wstring selectedLabel = L"Selected Layer: ";
        const int selectedIndex = GetSelectedInlineLayerIndex();
        if (selectedIndex >= 0)
        {
            selectedLabel += std::to_wstring(selectedIndex + 1);
            selectedLabel += m_quickEditEnabled ? L" [on]" : L" [off]";
        }
        else
        {
            selectedLabel += L"none";
        }
        SetWindowTextFormat(m_labelLayerList, selectedLabel);
        constexpr float kRadToDeg = 57.2957795f;
        SetWindowTextFormat(m_labelQuickEditFeather, L"Feather: " + FormatFloat(m_quickEditFeather, 2));
        SetWindowTextFormat(m_labelQuickEditLengthScale, L"Length Scale: " + FormatFloat(m_quickEditLengthScale, 2));
        SetWindowTextFormat(m_labelQuickEditLayerWeight, L"Layer Weight: " + FormatFloat(m_quickEditLayerWeight, 2));
        SetWindowTextFormat(m_labelQuickEditCenterU, L"Center U: " + FormatFloat(m_quickEditCenterU, 2));
        SetWindowTextFormat(m_labelQuickEditCenterV, L"Center V: " + FormatFloat(m_quickEditCenterV, 2));
        SetWindowTextFormat(m_labelQuickEditRadiusU, L"Radius U: " + FormatFloat(m_quickEditRadiusU, 2));
        SetWindowTextFormat(m_labelQuickEditRadiusV, L"Radius V: " + FormatFloat(m_quickEditRadiusV, 2));
        SetWindowTextFormat(m_labelQuickEditInnerRadius, L"Inner Radius: " + FormatFloat(m_quickEditInnerRadius, 2));
        SetWindowTextFormat(m_labelQuickEditAngleCenter, L"Angle Center: " + FormatFloat(m_quickEditAngleCenterRadians * kRadToDeg, 1) + L" deg");
        SetWindowTextFormat(m_labelQuickEditAngleHalfExtent, L"Angle Half Extent: " + FormatFloat(m_quickEditAngleHalfExtentRadians * kRadToDeg, 1) + L" deg");
        SetWindowTextFormat(m_labelQuickEditRootOffsetX, L"Root Offset X: " + FormatFloat(m_quickEditRootOffsetX, 3));
        SetWindowTextFormat(m_labelQuickEditRootOffsetY, L"Root Offset Y: " + FormatFloat(m_quickEditRootOffsetY, 3));
        SetWindowTextFormat(m_labelQuickEditRootOffsetZ, L"Root Offset Z: " + FormatFloat(m_quickEditRootOffsetZ, 3));
        SetWindowTextFormat(m_labelQuickEditTipOffsetX, L"Tip Offset X: " + FormatFloat(m_quickEditTipOffsetX, 3));
        SetWindowTextFormat(m_labelQuickEditTipOffsetY, L"Tip Offset Y: " + FormatFloat(m_quickEditTipOffsetY, 3));
        SetWindowTextFormat(m_labelQuickEditTipOffsetZ, L"Tip Offset Z: " + FormatFloat(m_quickEditTipOffsetZ, 3));
        SetWindowTextFormat(m_labelQuickEditSideOffset, L"Side Offset: " + FormatFloat(m_quickEditSideOffset, 3));
        SetWindowTextFormat(m_labelQuickEditYaw, L"Yaw: " + FormatFloat(m_quickEditYawRadians * kRadToDeg, 1) + L" deg");
        SetWindowTextFormat(m_labelQuickEditWidthScale, L"Width Scale: " + FormatFloat(m_quickEditWidthScale, 2));
        SetWindowTextFormat(m_labelQuickEditFollowerInfluence, L"Follower Influence: " + FormatFloat(m_quickEditFollowerInfluence, 2));
        SetWindowTextFormat(m_labelQuickEditBoundarySmoothing, L"Boundary Smoothing: " + FormatFloat(m_quickEditBoundarySmoothing, 2));
        SetWindowTextFormat(m_labelQuickEditBoundaryRadiusUv, L"Boundary Radius UV: " + FormatFloat(m_quickEditBoundaryRadiusUv, 2));
        SetWindowTextFormat(m_labelQuickEditGroupId, L"Group Filter: " + FormatOptionalGroupId(m_quickEditGroupId));

        std::wstring previewLabel = L"Preview: combined edit influence";
        std::vector<float> previewInfluence;
        uint32_t previewGuideCount = 0u;
        float previewMaxInfluence = 0.0f;
        if (TryBuildSelectedInlineLayerPreview(previewInfluence, previewGuideCount, previewMaxInfluence))
        {
            if (GetSelectedInlineLayerIndex() >= 0)
            {
                previewLabel = L"Preview: selected layer guides " + std::to_wstring(previewGuideCount) +
                    L", max " + FormatFloat(previewMaxInfluence, 2) +
                    L", center (" + FormatFloat(m_quickEditCenterU, 2) + L", " + FormatFloat(m_quickEditCenterV, 2) + L")" +
                    L", tipZ " + FormatFloat(m_quickEditTipOffsetZ, 2) +
                    L", yaw " + FormatFloat(m_quickEditYawRadians * kRadToDeg, 0) + L" deg" +
                    L", width " + FormatFloat(m_quickEditWidthScale, 2) +
                    L", follow " + FormatFloat(m_quickEditFollowerInfluence, 2) +
                    L", smooth " + FormatFloat(m_quickEditBoundarySmoothing, 2) +
                    L", radius " + FormatFloat(m_quickEditBoundaryRadiusUv, 2) +
                    L", group " + FormatOptionalGroupId(m_quickEditGroupId) +
                    (m_quickEditGuidesOnly ? L", guides only" : L", guides+followers") +
                    (IsBoundaryBeforeAfterOverlayMode() ? L" (Boundary Before/After: left raw, right tinted)" : (IsActiveSetRiskOverlayMode() ? L" (Active-Set overlay shows risk heat + selected clusters)" : L" (Edit Influence overlay adds boundary tint)"));
            }
        }
        SetWindowTextFormat(m_labelQuickEditVisualization, previewLabel);
    }

    int GpuHairViewer::GetSelectedInlineLayerIndex() const
    {
        if (!m_listInlineLayers)
        {
            return m_selectedInlineLayerIndex;
        }
        const LRESULT selected = SendMessageW(m_listInlineLayers, LB_GETCURSEL, 0, 0);
        if (selected == LB_ERR)
        {
            return -1;
        }
        return static_cast<int>(selected);
    }

    void GpuHairViewer::SetSelectedInlineLayerIndex(int layerIndex)
    {
        m_selectedInlineLayerIndex = layerIndex;
        if (!m_listInlineLayers)
        {
            return;
        }
        const LRESULT count = SendMessageW(m_listInlineLayers, LB_GETCOUNT, 0, 0);
        if (count == LB_ERR || count <= 0)
        {
            SendMessageW(m_listInlineLayers, LB_SETCURSEL, static_cast<WPARAM>(-1), 0);
            m_selectedInlineLayerIndex = -1;
            return;
        }
        const int clamped = std::clamp(layerIndex, 0, static_cast<int>(count) - 1);
        SendMessageW(m_listInlineLayers, LB_SETCURSEL, static_cast<WPARAM>(clamped), 0);
        m_selectedInlineLayerIndex = clamped;
    }

    void GpuHairViewer::RefreshInlineLayerListFromText(int preferredSelection)
    {
        if (!m_listInlineLayers)
        {
            return;
        }

        hairgpu::EditStack stack = {};
        std::string error;
        const bool parsed = LocalLoadEditStackCsvText(WideToUtf8(GetInlineEditStackText()), stack, error);

        const bool prevSyncing = m_syncingControls;
        m_syncingControls = true;
        SendMessageW(m_listInlineLayers, LB_RESETCONTENT, 0, 0);

        if (!parsed || stack.layers.empty())
        {
            m_selectedInlineLayerIndex = -1;
            m_syncingControls = prevSyncing;
            RefreshQuickEditFormLabels();
            return;
        }

        auto modeLabel = [](hairgpu::RegionSelectMode mode)
        {
            switch (mode)
            {
            case hairgpu::RegionSelectMode::Ellipse: return L"Ellipse";
            case hairgpu::RegionSelectMode::Rectangle: return L"Rectangle";
            case hairgpu::RegionSelectMode::Sector: return L"Sector";
            default: return L"All";
            }
        };

        for (size_t i = 0; i < stack.layers.size(); ++i)
        {
            const hairgpu::EditLayer& layer = stack.layers[i];
            std::wstringstream ss;
            ss << (i + 1) << L". ";
            if (!layer.enabled)
            {
                ss << L"[off] ";
            }
            ss << Utf8ToWide(layer.name.empty() ? std::string("layer") : layer.name);
            ss << L"  (" << modeLabel(layer.region.mode) << L", w " << FormatFloat(layer.layerWeight, 2) << L", len " << FormatFloat(layer.edit.lengthScale, 2)
               << L", width " << FormatFloat(layer.edit.widthScale, 2) << L", follow " << FormatFloat(layer.edit.followerInfluence, 2);
            if (layer.region.groupId != UINT32_MAX)
            {
                ss << L", g " << layer.region.groupId;
            }
            ss << (layer.region.guidesOnly ? L", guides" : L", all") << L")";
            SendMessageW(m_listInlineLayers, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(ss.str().c_str()));
        }

        const int target = (preferredSelection >= 0) ? preferredSelection : ((m_selectedInlineLayerIndex >= 0) ? m_selectedInlineLayerIndex : 0);
        m_syncingControls = prevSyncing;
        SetSelectedInlineLayerIndex(target);
        RefreshQuickEditFormLabels();
    }

    void GpuHairViewer::SyncQuickEditFormControlsFromState()
    {
        const bool prevSyncing = m_syncingControls;
        m_syncingControls = true;
        SendMessageW(m_comboQuickEditMode, CB_SETCURSEL,
            (m_quickEditMode == hairgpu::RegionSelectMode::Rectangle) ? 1 :
            (m_quickEditMode == hairgpu::RegionSelectMode::Sector) ? 2 : 0, 0);
        SendMessageW(m_checkQuickEditEnabled, BM_SETCHECK, m_quickEditEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
        SetWindowTextFormat(m_editQuickEditName, m_quickEditLayerName);
        SendMessageW(m_trackQuickEditFeather, TBM_SETPOS, TRUE, SliderFromFloat(m_quickEditFeather, 0.0f, 1.0f));
        SendMessageW(m_trackQuickEditLengthScale, TBM_SETPOS, TRUE, SliderFromFloat(m_quickEditLengthScale, 0.6f, 1.4f));
        SendMessageW(m_trackQuickEditBoundarySmoothing, TBM_SETPOS, TRUE, SliderFromFloat(m_quickEditBoundarySmoothing, 0.0f, 1.0f));
        SendMessageW(m_trackQuickEditBoundaryRadiusUv, TBM_SETPOS, TRUE, SliderFromFloat(m_quickEditBoundaryRadiusUv, 0.01f, 0.30f));
        SendMessageW(m_trackQuickEditLayerWeight, TBM_SETPOS, TRUE, SliderFromFloat(m_quickEditLayerWeight, 0.0f, 1.5f));
        SendMessageW(m_trackQuickEditCenterU, TBM_SETPOS, TRUE, SliderFromFloat(m_quickEditCenterU, 0.0f, 1.0f));
        SendMessageW(m_trackQuickEditCenterV, TBM_SETPOS, TRUE, SliderFromFloat(m_quickEditCenterV, 0.0f, 1.0f));
        SendMessageW(m_trackQuickEditRadiusU, TBM_SETPOS, TRUE, SliderFromFloat(m_quickEditRadiusU, 0.01f, 0.60f));
        SendMessageW(m_trackQuickEditRadiusV, TBM_SETPOS, TRUE, SliderFromFloat(m_quickEditRadiusV, 0.01f, 0.60f));
        SendMessageW(m_trackQuickEditInnerRadius, TBM_SETPOS, TRUE, SliderFromFloat(m_quickEditInnerRadius, 0.0f, 0.95f));
        SendMessageW(m_trackQuickEditAngleCenter, TBM_SETPOS, TRUE, SliderFromFloat(m_quickEditAngleCenterRadians, -3.1415926f, 3.1415926f));
        SendMessageW(m_trackQuickEditAngleHalfExtent, TBM_SETPOS, TRUE, SliderFromFloat(m_quickEditAngleHalfExtentRadians, 0.05f, 3.1415926f));
        SendMessageW(m_trackQuickEditRootOffsetX, TBM_SETPOS, TRUE, SliderFromFloat(m_quickEditRootOffsetX, -0.12f, 0.12f));
        SendMessageW(m_trackQuickEditRootOffsetY, TBM_SETPOS, TRUE, SliderFromFloat(m_quickEditRootOffsetY, -0.12f, 0.12f));
        SendMessageW(m_trackQuickEditRootOffsetZ, TBM_SETPOS, TRUE, SliderFromFloat(m_quickEditRootOffsetZ, -0.12f, 0.12f));
        SendMessageW(m_trackQuickEditTipOffsetX, TBM_SETPOS, TRUE, SliderFromFloat(m_quickEditTipOffsetX, -0.20f, 0.20f));
        SendMessageW(m_trackQuickEditTipOffsetY, TBM_SETPOS, TRUE, SliderFromFloat(m_quickEditTipOffsetY, -0.20f, 0.20f));
        SendMessageW(m_trackQuickEditTipOffsetZ, TBM_SETPOS, TRUE, SliderFromFloat(m_quickEditTipOffsetZ, -0.20f, 0.20f));
        SendMessageW(m_trackQuickEditSideOffset, TBM_SETPOS, TRUE, SliderFromFloat(m_quickEditSideOffset, -0.20f, 0.20f));
        SendMessageW(m_trackQuickEditYaw, TBM_SETPOS, TRUE, SliderFromFloat(m_quickEditYawRadians, -1.5707963f, 1.5707963f));
        SendMessageW(m_trackQuickEditWidthScale, TBM_SETPOS, TRUE, SliderFromFloat(m_quickEditWidthScale, 0.50f, 1.80f));
        SendMessageW(m_trackQuickEditFollowerInfluence, TBM_SETPOS, TRUE, SliderFromFloat(m_quickEditFollowerInfluence, 0.0f, 1.5f));
        SendMessageW(m_trackQuickEditBoundarySmoothing, TBM_SETPOS, TRUE, SliderFromFloat(m_quickEditBoundarySmoothing, 0.0f, 1.0f));
        SendMessageW(m_trackQuickEditBoundaryRadiusUv, TBM_SETPOS, TRUE, SliderFromFloat(m_quickEditBoundaryRadiusUv, 0.01f, 0.30f));
        SetWindowTextFormat(m_editQuickEditGroupId, (m_quickEditGroupId == UINT32_MAX) ? std::wstring(L"") : std::to_wstring(m_quickEditGroupId));
        SendMessageW(m_checkQuickEditGuidesOnly, BM_SETCHECK, m_quickEditGuidesOnly ? BST_CHECKED : BST_UNCHECKED, 0);
        RefreshQuickEditFormLabels();
        m_syncingControls = prevSyncing;
    }

    void GpuHairViewer::LoadQuickEditFormFromInlineText(int preferredSelection)
    {
        hairgpu::EditStack stack = {};
        std::string error;
        if (!LocalLoadEditStackCsvText(WideToUtf8(GetInlineEditStackText()), stack, error) || stack.layers.empty())
        {
            m_quickEditLayerName = L"quick_form";
            m_quickEditCenterU = 0.50f;
            m_quickEditCenterV = 0.50f;
            m_quickEditRadiusU = 0.18f;
            m_quickEditRadiusV = 0.18f;
            m_quickEditInnerRadius = 0.00f;
            m_quickEditAngleCenterRadians = 0.00f;
            m_quickEditAngleHalfExtentRadians = 3.1415926f;
            m_quickEditRootOffsetX = 0.00f;
            m_quickEditRootOffsetY = 0.00f;
            m_quickEditRootOffsetZ = 0.00f;
            m_quickEditTipOffsetX = 0.02f;
            m_quickEditTipOffsetY = 0.00f;
            m_quickEditTipOffsetZ = -0.08f;
            m_quickEditSideOffset = 0.00f;
            m_quickEditYawRadians = 0.00f;
            m_quickEditWidthScale = 1.00f;
            m_quickEditFollowerInfluence = 1.00f;
            m_quickEditBoundarySmoothing = 0.42f;
            m_quickEditBoundaryRadiusUv = 0.10f;
            m_quickEditGroupId = UINT32_MAX;
            m_quickEditGuidesOnly = true;
            RefreshInlineLayerListFromText(-1);
            SyncQuickEditFormControlsFromState();
            if (IsEditInfluenceOverlayMode())
            {
                UpdateScalpOverlayVertexBuffer();
            }
            return;
        }

        m_quickEditBoundarySmoothing = hairgpu::Clamp(stack.boundarySmoothing, 0.0f, 1.0f);
        if (m_quickEditBoundarySmoothing <= 0.0f)
        {
            m_quickEditBoundarySmoothing = 0.42f;
        }
        m_quickEditBoundaryRadiusUv = hairgpu::Clamp(stack.boundaryRadiusUv, 0.01f, 0.30f);

        int layerIndex = preferredSelection;
        if (layerIndex < 0)
        {
            layerIndex = GetSelectedInlineLayerIndex();
        }
        if (layerIndex < 0)
        {
            layerIndex = m_selectedInlineLayerIndex;
        }
        layerIndex = std::clamp(layerIndex, 0, static_cast<int>(stack.layers.size()) - 1);
        m_selectedInlineLayerIndex = layerIndex;

        const hairgpu::EditLayer& layer = stack.layers[static_cast<size_t>(layerIndex)];
        m_quickEditLayerName = Utf8ToWide(layer.name.empty() ? std::string("layer") : layer.name);
        m_quickEditEnabled = layer.enabled;
        m_quickEditMode = layer.region.mode;
        if (m_quickEditMode == hairgpu::RegionSelectMode::All)
        {
            m_quickEditMode = hairgpu::RegionSelectMode::Ellipse;
        }
        m_quickEditFeather = hairgpu::Clamp(layer.region.feather, 0.0f, 1.0f);
        m_quickEditLengthScale = hairgpu::Clamp(layer.edit.lengthScale, 0.6f, 1.4f);
        m_quickEditLayerWeight = hairgpu::Clamp(layer.layerWeight, 0.0f, 1.5f);
        m_quickEditCenterU = hairgpu::Clamp(layer.region.centerUv.x, 0.0f, 1.0f);
        m_quickEditCenterV = hairgpu::Clamp(layer.region.centerUv.y, 0.0f, 1.0f);
        m_quickEditRadiusU = hairgpu::Clamp(layer.region.radiusUv.x, 0.01f, 0.60f);
        m_quickEditRadiusV = hairgpu::Clamp(layer.region.radiusUv.y, 0.01f, 0.60f);
        m_quickEditInnerRadius = hairgpu::Clamp(layer.region.innerRadius, 0.0f, 0.95f);
        m_quickEditAngleCenterRadians = hairgpu::Clamp(layer.region.angleCenterRadians, -3.1415926f, 3.1415926f);
        m_quickEditAngleHalfExtentRadians = hairgpu::Clamp(layer.region.angleHalfExtentRadians, 0.05f, 3.1415926f);
        m_quickEditRootOffsetX = hairgpu::Clamp(layer.edit.rootOffset.x, -0.12f, 0.12f);
        m_quickEditRootOffsetY = hairgpu::Clamp(layer.edit.rootOffset.y, -0.12f, 0.12f);
        m_quickEditRootOffsetZ = hairgpu::Clamp(layer.edit.rootOffset.z, -0.12f, 0.12f);
        m_quickEditTipOffsetX = hairgpu::Clamp(layer.edit.tipOffset.x, -0.20f, 0.20f);
        m_quickEditTipOffsetY = hairgpu::Clamp(layer.edit.tipOffset.y, -0.20f, 0.20f);
        m_quickEditTipOffsetZ = hairgpu::Clamp(layer.edit.tipOffset.z, -0.20f, 0.20f);
        m_quickEditSideOffset = hairgpu::Clamp(layer.edit.sideOffset, -0.20f, 0.20f);
        m_quickEditYawRadians = hairgpu::Clamp(layer.edit.yawRadians, -1.5707963f, 1.5707963f);
        m_quickEditWidthScale = hairgpu::Clamp(layer.edit.widthScale, 0.50f, 1.80f);
        m_quickEditFollowerInfluence = hairgpu::Clamp(layer.edit.followerInfluence, 0.0f, 1.5f);
        m_quickEditGroupId = layer.region.groupId;
        m_quickEditGuidesOnly = layer.region.guidesOnly;
        RefreshInlineLayerListFromText(layerIndex);
        SyncQuickEditFormControlsFromState();
        if (IsEditInfluenceOverlayMode())
        {
            UpdateScalpOverlayVertexBuffer();
        }
    }

    bool GpuHairViewer::IsEditInfluenceOverlayMode() const
    {
        return m_maskOverlayMode == MaskOverlayMode::EditInfluence || m_maskOverlayMode == MaskOverlayMode::EditInfluenceBeforeAfter;
    }

    bool GpuHairViewer::IsBoundaryBeforeAfterOverlayMode() const
    {
        return m_maskOverlayMode == MaskOverlayMode::EditInfluenceBeforeAfter;
    }

    bool GpuHairViewer::IsActiveSetRiskOverlayMode() const
    {
        return m_maskOverlayMode == MaskOverlayMode::ActiveSetRiskSelection ||
               m_maskOverlayMode == MaskOverlayMode::ActiveSetMotion ||
               m_maskOverlayMode == MaskOverlayMode::ActiveSetContact ||
               m_maskOverlayMode == MaskOverlayMode::ActiveSetUncertainty ||
               m_maskOverlayMode == MaskOverlayMode::ActiveSetEdit;
    }

    const wchar_t* GpuHairViewer::GetMaskOverlayModeDisplayName() const
    {
        switch (m_maskOverlayMode)
        {
        case MaskOverlayMode::Parting: return L"Parting";
        case MaskOverlayMode::Region: return L"Region";
        case MaskOverlayMode::EditInfluence: return L"Edit Influence";
        case MaskOverlayMode::EditInfluenceBeforeAfter: return L"Boundary Before/After";
        case MaskOverlayMode::ActiveSetRiskSelection: return L"Active-Set Risk";
        case MaskOverlayMode::ActiveSetMotion: return L"Active-Set Motion";
        case MaskOverlayMode::ActiveSetContact: return L"Active-Set Contact";
        case MaskOverlayMode::ActiveSetUncertainty: return L"Active-Set Uncertainty";
        case MaskOverlayMode::ActiveSetEdit: return L"Active-Set Edit";
        default: return L"None";
        }
    }

    float GpuHairViewer::GetActiveSetOverlayScoreValue(const hairgpu::ActiveSetClusterScore& score) const
    {
        switch (m_maskOverlayMode)
        {
        case MaskOverlayMode::ActiveSetMotion:
            return hairgpu::Clamp(score.motionScore, 0.0f, 1.0f);
        case MaskOverlayMode::ActiveSetContact:
            return hairgpu::Clamp(score.contactScore, 0.0f, 1.0f);
        case MaskOverlayMode::ActiveSetUncertainty:
            return hairgpu::Clamp(score.uncertaintyScore, 0.0f, 1.0f);
        case MaskOverlayMode::ActiveSetEdit:
            return hairgpu::Clamp(score.editScore, 0.0f, 1.0f);
        case MaskOverlayMode::ActiveSetRiskSelection:
        default:
            return hairgpu::Clamp(score.finalScore, 0.0f, 1.0f);
        }
    }

    bool GpuHairViewer::TryBuildSelectedInlineLayerPreview(std::vector<float>& outGuideInfluence, uint32_t& outActiveGuideCount, float& outMaxInfluence) const
    {
        outGuideInfluence.clear();
        outActiveGuideCount = 0u;
        outMaxInfluence = 0.0f;

        hairgpu::EditStack stack = {};
        std::string error;
        if (!LocalLoadEditStackCsvText(WideToUtf8(GetInlineEditStackText()), stack, error) || stack.layers.empty())
        {
            return false;
        }

        int layerIndex = GetSelectedInlineLayerIndex();
        if (layerIndex < 0)
        {
            layerIndex = m_selectedInlineLayerIndex;
        }
        if (layerIndex < 0 || stack.layers.empty())
        {
            return false;
        }
        layerIndex = std::clamp(layerIndex, 0, static_cast<int>(stack.layers.size()) - 1);

        const hairgpu::HairAsset& previewAsset = m_baseAsset.strands.empty() ? m_asset : m_baseAsset;
        const uint32_t guideCount = std::min<uint32_t>(previewAsset.guideStrandCount, static_cast<uint32_t>(previewAsset.strands.size()));
        if (guideCount == 0u)
        {
            return false;
        }

        outGuideInfluence.assign(guideCount, 0.0f);
        const hairgpu::EditLayer& layer = stack.layers[static_cast<size_t>(layerIndex)];
        if (!layer.enabled)
        {
            return true;
        }

        const hairgpu::RegionSelectionResult selection = hairgpu::SelectRegion(previewAsset, layer.region);
        for (const hairgpu::SelectedStrand& selected : selection.strands)
        {
            if (selected.strandIndex >= guideCount)
            {
                continue;
            }
            const float influence = hairgpu::Clamp(selected.weight * layer.layerWeight, 0.0f, 1.0f);
            outGuideInfluence[selected.strandIndex] = std::max(outGuideInfluence[selected.strandIndex], influence);
        }

        for (float influence : outGuideInfluence)
        {
            if (influence > 1.0e-4f)
            {
                ++outActiveGuideCount;
                outMaxInfluence = std::max(outMaxInfluence, influence);
            }
        }
        return true;
    }

    hairgpu::EditLayer GpuHairViewer::BuildQuickEditLayerFromUi() const
    {
        hairgpu::EditLayer layer = {};
        std::string layerName = WideToUtf8(m_quickEditLayerName);
        for (char& c : layerName)
        {
            if (c == ',' || c == '\r' || c == '\n')
            {
                c = '_';
            }
        }
        if (layerName.empty())
        {
            layerName = "quick_form";
        }
        layer.name = layerName;
        layer.enabled = m_quickEditEnabled;
        layer.layerWeight = m_quickEditLayerWeight;
        layer.region.mode = m_quickEditMode;
        layer.region.centerUv = { hairgpu::Clamp(m_quickEditCenterU, 0.0f, 1.0f), hairgpu::Clamp(m_quickEditCenterV, 0.0f, 1.0f) };
        layer.region.radiusUv = { hairgpu::Clamp(m_quickEditRadiusU, 0.01f, 0.60f), hairgpu::Clamp(m_quickEditRadiusV, 0.01f, 0.60f) };
        const float quickEditMinRadius = std::min(layer.region.radiusUv.x, layer.region.radiusUv.y);
        layer.region.innerRadius = hairgpu::Clamp(m_quickEditInnerRadius, 0.0f, quickEditMinRadius * 0.95f);
        layer.region.angleCenterRadians = hairgpu::Clamp(m_quickEditAngleCenterRadians, -3.1415926f, 3.1415926f);
        layer.region.angleHalfExtentRadians = hairgpu::Clamp(m_quickEditAngleHalfExtentRadians, 0.05f, 3.1415926f);
        layer.region.feather = m_quickEditFeather;
        layer.region.groupId = m_quickEditGroupId;
        layer.region.guidesOnly = m_quickEditGuidesOnly;
        layer.edit.lengthScale = m_quickEditLengthScale;
        layer.edit.widthScale = hairgpu::Clamp(m_quickEditWidthScale, 0.50f, 1.80f);
        layer.edit.followerInfluence = hairgpu::Clamp(m_quickEditFollowerInfluence, 0.0f, 1.5f);
        layer.edit.rootOffset = {
            hairgpu::Clamp(m_quickEditRootOffsetX, -0.12f, 0.12f),
            hairgpu::Clamp(m_quickEditRootOffsetY, -0.12f, 0.12f),
            hairgpu::Clamp(m_quickEditRootOffsetZ, -0.12f, 0.12f)
        };
        layer.edit.tipOffset = {
            hairgpu::Clamp(m_quickEditTipOffsetX, -0.20f, 0.20f),
            hairgpu::Clamp(m_quickEditTipOffsetY, -0.20f, 0.20f),
            hairgpu::Clamp(m_quickEditTipOffsetZ, -0.20f, 0.20f)
        };
        layer.edit.sideOffset = hairgpu::Clamp(m_quickEditSideOffset, -0.20f, 0.20f);
        layer.edit.yawRadians = hairgpu::Clamp(m_quickEditYawRadians, -1.5707963f, 1.5707963f);
        return layer;
    }

    bool GpuHairViewer::PushQuickEditFormToInlineText(bool replaceSelectedLayer, bool applyAfterPush)
    {
        SyncStateFromControls();

        hairgpu::EditStack stack = {};
        std::string error;
        const std::string inlineUtf8 = WideToUtf8(GetInlineEditStackText());
        if (!inlineUtf8.empty())
        {
            LocalLoadEditStackCsvText(inlineUtf8, stack, error);
        }
        stack.boundarySmoothing = hairgpu::Clamp(m_quickEditBoundarySmoothing, 0.0f, 1.0f);
        stack.boundaryRadiusUv = hairgpu::Clamp(m_quickEditBoundaryRadiusUv, 0.01f, 0.30f);
        if (stack.boundarySmoothing <= 0.0f)
        {
            stack.boundarySmoothing = 0.42f;
        }

        hairgpu::EditLayer layer = BuildQuickEditLayerFromUi();
        int selectionAfterWrite = static_cast<int>(stack.layers.size());
        if (replaceSelectedLayer && !stack.layers.empty())
        {
            const int selectedIndex = std::clamp(GetSelectedInlineLayerIndex(), 0, static_cast<int>(stack.layers.size()) - 1);
            stack.layers[static_cast<size_t>(selectedIndex)] = layer;
            selectionAfterWrite = selectedIndex;
        }
        else
        {
            stack.layers.push_back(layer);
        }

        const std::wstring inlineText = Utf8ToWide(SerializeEditStackToCsvText(stack));
        SetWindowTextW(m_editInlineEditStack, inlineText.c_str());
        LoadQuickEditFormFromInlineText(selectionAfterWrite);
        if (applyAfterPush)
        {
            return ApplyInlineEditStackFromUi(true);
        }
        return true;
    }

    bool GpuHairViewer::MoveSelectedInlineLayer(int direction, bool applyAfterMove)
    {
        hairgpu::EditStack stack = {};
        std::string error;
        if (!LocalLoadEditStackCsvText(WideToUtf8(GetInlineEditStackText()), stack, error) || stack.layers.size() < 2)
        {
            return false;
        }

        const int selectedIndex = std::clamp(GetSelectedInlineLayerIndex(), 0, static_cast<int>(stack.layers.size()) - 1);
        const int targetIndex = std::clamp(selectedIndex + direction, 0, static_cast<int>(stack.layers.size()) - 1);
        if (targetIndex == selectedIndex)
        {
            return false;
        }

        std::swap(stack.layers[static_cast<size_t>(selectedIndex)], stack.layers[static_cast<size_t>(targetIndex)]);
        const std::wstring inlineText = Utf8ToWide(SerializeEditStackToCsvText(stack));
        SetWindowTextW(m_editInlineEditStack, inlineText.c_str());
        LoadQuickEditFormFromInlineText(targetIndex);
        if (applyAfterMove)
        {
            return ApplyInlineEditStackFromUi(true);
        }
        return true;
    }

    bool GpuHairViewer::ToggleSelectedInlineLayerEnabled(bool applyAfterToggle)
    {
        hairgpu::EditStack stack = {};
        std::string error;
        if (!LocalLoadEditStackCsvText(WideToUtf8(GetInlineEditStackText()), stack, error) || stack.layers.empty())
        {
            return false;
        }

        const int selectedIndex = std::clamp(GetSelectedInlineLayerIndex(), 0, static_cast<int>(stack.layers.size()) - 1);
        hairgpu::EditLayer& layer = stack.layers[static_cast<size_t>(selectedIndex)];
        layer.enabled = !layer.enabled;
        m_quickEditEnabled = layer.enabled;
        const std::wstring inlineText = Utf8ToWide(SerializeEditStackToCsvText(stack));
        SetWindowTextW(m_editInlineEditStack, inlineText.c_str());
        LoadQuickEditFormFromInlineText(selectedIndex);
        if (applyAfterToggle)
        {
            return ApplyInlineEditStackFromUi(true);
        }
        return true;
    }

    bool GpuHairViewer::DuplicateSelectedInlineLayer(bool applyAfterDuplicate)
    {
        hairgpu::EditStack stack = {};
        std::string error;
        if (!LocalLoadEditStackCsvText(WideToUtf8(GetInlineEditStackText()), stack, error) || stack.layers.empty())
        {
            return false;
        }

        const int selectedIndex = std::clamp(GetSelectedInlineLayerIndex(), 0, static_cast<int>(stack.layers.size()) - 1);
        hairgpu::EditLayer duplicate = stack.layers[static_cast<size_t>(selectedIndex)];
        duplicate.name += "_copy";
        stack.layers.insert(stack.layers.begin() + selectedIndex + 1, duplicate);
        const std::wstring inlineText = Utf8ToWide(SerializeEditStackToCsvText(stack));
        SetWindowTextW(m_editInlineEditStack, inlineText.c_str());
        LoadQuickEditFormFromInlineText(selectedIndex + 1);
        if (applyAfterDuplicate)
        {
            return ApplyInlineEditStackFromUi(true);
        }
        return true;
    }

    bool GpuHairViewer::DeleteSelectedInlineLayer(bool applyAfterDelete)
    {
        hairgpu::EditStack stack = {};
        std::string error;
        if (!LocalLoadEditStackCsvText(WideToUtf8(GetInlineEditStackText()), stack, error) || stack.layers.empty())
        {
            return false;
        }

        const int selectedIndex = std::clamp(GetSelectedInlineLayerIndex(), 0, static_cast<int>(stack.layers.size()) - 1);
        stack.layers.erase(stack.layers.begin() + selectedIndex);
        const std::wstring inlineText = Utf8ToWide(SerializeEditStackToCsvText(stack));
        SetWindowTextW(m_editInlineEditStack, inlineText.c_str());
        const int nextSelection = stack.layers.empty() ? -1 : std::min<int>(selectedIndex, static_cast<int>(stack.layers.size()) - 1);
        LoadQuickEditFormFromInlineText(nextSelection);
        if (applyAfterDelete)
        {
            return ApplyInlineEditStackFromUi(true);
        }
        return true;
    }

    bool GpuHairViewer::BrowseAndSetMaskPath(bool partingMask)
    {
        wchar_t fileName[MAX_PATH] = L"";
        OPENFILENAMEW ofn = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = m_hwnd;
        ofn.lpstrFilter = L"Mask Files (*.bmp;*.pgm;*.ppm;*.csv)\0*.bmp;*.pgm;*.ppm;*.csv\0Bitmap (*.bmp)\0*.bmp\0Portable Graymap (*.pgm)\0*.pgm\0Portable Pixmap (*.ppm)\0*.ppm\0CSV (*.csv)\0*.csv\0All Files\0*.*\0";
        ofn.lpstrFile = fileName;
        ofn.nMaxFile = MAX_PATH;
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
        if (!GetOpenFileNameW(&ofn))
        {
            return false;
        }
        if (partingMask)
        {
            m_partingMaskPath = fileName;
        }
        else
        {
            m_regionMaskPath = fileName;
        }
        RefreshAssetStatsText();
        RefreshMaskPreviewControls();
        return true;
    }

    void GpuHairViewer::ApplySampleMaskOverrides()
    {
        std::string parting = WideToUtf8(m_partingMaskPath);
        std::string region = WideToUtf8(m_regionMaskPath);
        hairgpu::SetSampleMaskOverridePaths(parting.empty() ? nullptr : parting.c_str(), region.empty() ? nullptr : region.c_str());
        hairgpu::SetSampleMaskSourceMode(static_cast<hairgpu::SampleMaskSourceMode>(m_maskSourceMode));
    }

    void GpuHairViewer::ReleaseMaskPreviewBitmaps()
    {
        if (m_partingPreviewBitmap) { DeleteObject(m_partingPreviewBitmap); m_partingPreviewBitmap = nullptr; }
        if (m_regionPreviewBitmap) { DeleteObject(m_regionPreviewBitmap); m_regionPreviewBitmap = nullptr; }
    }

    void GpuHairViewer::RefreshMaskPreviewControls()
    {
        ReleaseMaskPreviewBitmaps();
        std::wstring partingLabel = L"Parting preview: ";
        std::wstring regionLabel = L"Region preview: ";
        if (m_maskSourceMode == MaskSourceMode::Procedural)
        {
            partingLabel += L"procedural";
            regionLabel += L"procedural";
        }
        else
        {
            partingLabel += m_partingMaskPath.empty() ? (m_maskSourceMode == MaskSourceMode::Auto ? L"auto" : L"none") : std::filesystem::path(m_partingMaskPath).filename().wstring();
            regionLabel += m_regionMaskPath.empty() ? (m_maskSourceMode == MaskSourceMode::Auto ? L"auto" : L"none") : std::filesystem::path(m_regionMaskPath).filename().wstring();
        }
        if (m_labelPartingMask) SetWindowTextW(m_labelPartingMask, partingLabel.c_str());
        if (m_labelRegionMask) SetWindowTextW(m_labelRegionMask, regionLabel.c_str());

        if (m_maskSourceMode != MaskSourceMode::Procedural)
        {
            PreviewMaskImage partImg, regionImg;
            std::wstring effectiveParting = m_partingMaskPath;
            std::wstring effectiveRegion = m_regionMaskPath;
            if (m_maskSourceMode == MaskSourceMode::Auto)
            {
                if (effectiveParting.empty()) effectiveParting = ResolveViewerDefaultMaskPath(L"scalp_parting_mask");
                if (effectiveRegion.empty()) effectiveRegion = ResolveViewerDefaultMaskPath(L"scalp_region_mask");
            }
            if (!effectiveParting.empty() && LoadPreviewMaskAny(effectiveParting, 1u, partImg))
            {
                m_partingPreviewBitmap = CreatePreviewBitmapFromMask(partImg, true);
                if (m_partingMaskPath.empty()) partingLabel = L"Parting preview: auto file";
            }
            if (!effectiveRegion.empty() && LoadPreviewMaskAny(effectiveRegion, 3u, regionImg))
            {
                m_regionPreviewBitmap = CreatePreviewBitmapFromMask(regionImg, false);
                if (m_regionMaskPath.empty()) regionLabel = L"Region preview: auto file";
            }
        }
        SendMessageW(m_previewPartingMask, STM_SETIMAGE, IMAGE_BITMAP, reinterpret_cast<LPARAM>(m_partingPreviewBitmap));
        SendMessageW(m_previewRegionMask, STM_SETIMAGE, IMAGE_BITMAP, reinterpret_cast<LPARAM>(m_regionPreviewBitmap));
        UpdateScalpOverlayVertexBuffer();
    }

    void GpuHairViewer::ShowMaskPreviewPopup(bool partingMask)
    {
        HBITMAP bitmap = partingMask ? m_partingPreviewBitmap : m_regionPreviewBitmap;
        if (!bitmap)
        {
            MessageBoxW(m_hwnd, L"No preview bitmap is available for this mask.", L"Mask Preview", MB_OK | MB_ICONINFORMATION);
            return;
        }
        const wchar_t* title = partingMask ? L"Parting Mask Preview" : L"Region Mask Preview";
        HWND popup = CreateWindowExW(WS_EX_TOOLWINDOW, L"STATIC", title, WS_OVERLAPPEDWINDOW | WS_VISIBLE | SS_BITMAP | SS_CENTERIMAGE,
            CW_USEDEFAULT, CW_USEDEFAULT, 420, 420, m_hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (popup)
        {
            SendMessageW(popup, STM_SETIMAGE, IMAGE_BITMAP, reinterpret_cast<LPARAM>(bitmap));
            ShowWindow(popup, SW_SHOW);
            UpdateWindow(popup);
        }
    }

    void GpuHairViewer::UpdateScalpOverlayVertexBuffer()
    {
        if (!m_scalpVertexBuffer || m_asset.scalpMesh.positions.empty())
        {
            return;
        }

        PreviewMaskImage partImg;
        PreviewMaskImage regionImg;
        std::wstring effectiveParting = m_partingMaskPath;
        std::wstring effectiveRegion = m_regionMaskPath;
        if (m_maskSourceMode == MaskSourceMode::Auto)
        {
            if (effectiveParting.empty()) effectiveParting = ResolveViewerDefaultMaskPath(L"scalp_parting_mask");
            if (effectiveRegion.empty()) effectiveRegion = ResolveViewerDefaultMaskPath(L"scalp_region_mask");
        }
        if (!effectiveParting.empty()) LoadPreviewMaskAny(effectiveParting, 1u, partImg);
        if (!effectiveRegion.empty()) LoadPreviewMaskAny(effectiveRegion, 3u, regionImg);

        std::vector<Vec2> guideRootUvs;
        std::vector<float> guideRootInfluence;
        std::vector<float> guideRootRisk;
        std::vector<uint8_t> guideRootActive;
        bool usingSelectedLayerPreview = false;
        const bool needsGuideOverlayData = IsEditInfluenceOverlayMode() || IsActiveSetRiskOverlayMode();
        if (needsGuideOverlayData)
        {
            if (IsEditInfluenceOverlayMode())
            {
                uint32_t previewGuideCount = 0u;
                float previewMaxInfluence = 0.0f;
                usingSelectedLayerPreview = TryBuildSelectedInlineLayerPreview(guideRootInfluence, previewGuideCount, previewMaxInfluence);
                if (!usingSelectedLayerPreview)
                {
                    guideRootInfluence = m_runtimeEditState.guideInfluence;
                }
            }

            uint32_t guideCount = std::min<uint32_t>(m_asset.guideStrandCount, static_cast<uint32_t>(m_asset.strands.size()));
            if (IsEditInfluenceOverlayMode() && !guideRootInfluence.empty())
            {
                guideCount = std::min<uint32_t>(guideCount, static_cast<uint32_t>(guideRootInfluence.size()));
            }
            if (guideCount > 0u)
            {
                guideRootUvs.reserve(guideCount);
                if (!guideRootInfluence.empty())
                {
                    guideRootInfluence.resize(guideCount);
                }
                if (IsActiveSetRiskOverlayMode())
                {
                    guideRootRisk.assign(guideCount, 0.0f);
                    guideRootActive.assign(guideCount, 0u);
                    for (const hairgpu::ActiveSetClusterScore& score : m_activeSetPlan.scores)
                    {
                        if (score.clusterIndex >= m_activeSetClusters.size())
                        {
                            continue;
                        }
                        const hairgpu::ActiveSetCluster& cluster = m_activeSetClusters[score.clusterIndex];
                        const float risk = GetActiveSetOverlayScoreValue(score);
                        for (uint32_t guideIndex : cluster.guideStrandIndices)
                        {
                            if (guideIndex >= guideCount)
                            {
                                continue;
                            }
                            guideRootRisk[guideIndex] = std::max(guideRootRisk[guideIndex], risk);
                            if (score.selected)
                            {
                                guideRootActive[guideIndex] = 1u;
                            }
                        }
                    }
                }

                for (uint32_t guideIndex = 0; guideIndex < guideCount; ++guideIndex)
                {
                    Vec2 uv = { 0.5f, 0.5f };
                    if (guideIndex < m_asset.rootBindings.size())
                    {
                        uv = m_asset.rootBindings[guideIndex].rootUv;
                    }
                    else if (guideIndex < m_asset.strands.size() && m_asset.strands[guideIndex].pointCount > 0u)
                    {
                        const Vec3 root = m_asset.restPoints[m_asset.strands[guideIndex].pointOffset];
                        const Vec3 previewUv = ComputeScalpPreviewUv(root);
                        uv = { previewUv.x, previewUv.y };
                    }
                    guideRootUvs.push_back(uv);
                }
            }
        }

        std::vector<ScalpVertex> scalpVertices;
        scalpVertices.reserve(m_asset.scalpMesh.positions.size());
        for (size_t i = 0; i < m_asset.scalpMesh.positions.size(); ++i)
        {
            const Vec3 p = m_asset.scalpMesh.positions[i];
            const Vec3 n = (i < m_asset.scalpMesh.normals.size()) ? m_asset.scalpMesh.normals[i] : Vec3(0.0f, 1.0f, 0.0f);
            float r = 0.76f, g = 0.62f, b = 0.52f;
            const Vec3 uvw = ComputeScalpPreviewUv(p);
            if (m_maskOverlayMode == MaskOverlayMode::Parting)
            {
                const float maskValue = (partImg.width > 0) ? SamplePreviewMaskScalar(partImg, uvw.x, uvw.y, 0) : ProceduralPartingPreview(uvw.x, uvw.y);
                r = 0.28f + 0.72f * maskValue;
                g = 0.20f + 0.38f * (1.0f - maskValue);
                b = 0.55f * (1.0f - maskValue);
            }
            else if (m_maskOverlayMode == MaskOverlayMode::Region)
            {
                Vec3 region = (regionImg.width > 0) ? Vec3(SamplePreviewMaskScalar(regionImg, uvw.x, uvw.y, 0), SamplePreviewMaskScalar(regionImg, uvw.x, uvw.y, 1), SamplePreviewMaskScalar(regionImg, uvw.x, uvw.y, 2)) : ProceduralRegionPreview(uvw.x, uvw.y);
                r = 0.20f + 0.80f * region.x;
                g = 0.20f + 0.80f * region.y;
                b = 0.20f + 0.80f * region.z;
            }
            else if (IsEditInfluenceOverlayMode() && !guideRootUvs.empty())
            {
                float bestDistSq = std::numeric_limits<float>::max();
                float influence = 0.0f;
                float nearestSelectedDistSq = std::numeric_limits<float>::max();
                float nearestOutsideDistSq = std::numeric_limits<float>::max();
                for (size_t guideIndex = 0; guideIndex < guideRootUvs.size(); ++guideIndex)
                {
                    const float du = uvw.x - guideRootUvs[guideIndex].x;
                    const float dv = uvw.y - guideRootUvs[guideIndex].y;
                    const float distSq = du * du + dv * dv;
                    const float guideInfluence = hairgpu::Clamp(guideRootInfluence[guideIndex], 0.0f, 1.0f);
                    if (distSq < bestDistSq)
                    {
                        bestDistSq = distSq;
                        influence = guideInfluence;
                    }
                    if (guideInfluence >= 0.01f)
                    {
                        nearestSelectedDistSq = std::min(nearestSelectedDistSq, distSq);
                    }
                    else
                    {
                        nearestOutsideDistSq = std::min(nearestOutsideDistSq, distSq);
                    }
                }
                influence = Clamp(influence, 0.0f, 1.0f);
                const float rawR = 0.18f + 0.82f * influence;
                const float rawG = 0.28f + 0.50f * (1.0f - influence);
                const float rawB = 0.92f - 0.74f * influence;
                float afterR = rawR;
                float afterG = rawG;
                float afterB = rawB;
                if (usingSelectedLayerPreview && nearestSelectedDistSq < std::numeric_limits<float>::max() && nearestOutsideDistSq < std::numeric_limits<float>::max())
                {
                    const float ds = std::sqrt(nearestSelectedDistSq);
                    const float dof = std::sqrt(nearestOutsideDistSq);
                    const float denom = std::max(std::max(ds, dof), 1.0e-4f);
                    const float boundary = Clamp(1.0f - std::abs(ds - dof) / denom, 0.0f, 1.0f);
                    afterR = Clamp(afterR + 0.32f * boundary, 0.0f, 1.0f);
                    afterG = Clamp(afterG + 0.20f * boundary, 0.0f, 1.0f);
                    afterB = Clamp(afterB * (1.0f - 0.42f * boundary), 0.0f, 1.0f);
                }

                if (IsBoundaryBeforeAfterOverlayMode())
                {
                    const bool showAfter = uvw.x >= 0.5f;
                    r = showAfter ? afterR : rawR;
                    g = showAfter ? afterG : rawG;
                    b = showAfter ? afterB : rawB;

                    const float divider = Clamp(1.0f - std::abs(uvw.x - 0.5f) / 0.018f, 0.0f, 1.0f);
                    if (divider > 0.0f)
                    {
                        r = Clamp(r * (1.0f - 0.65f * divider) + 0.95f * divider, 0.0f, 1.0f);
                        g = Clamp(g * (1.0f - 0.65f * divider) + 0.95f * divider, 0.0f, 1.0f);
                        b = Clamp(b * (1.0f - 0.65f * divider) + 0.95f * divider, 0.0f, 1.0f);
                    }
                }
                else
                {
                    r = afterR;
                    g = afterG;
                    b = afterB;
                }
            }
            else if (IsActiveSetRiskOverlayMode() && !guideRootUvs.empty() && guideRootRisk.size() == guideRootUvs.size())
            {
                float bestDistSq = std::numeric_limits<float>::max();
                float risk = 0.0f;
                bool nearestActive = false;
                float nearestActiveDistSq = std::numeric_limits<float>::max();
                float nearestInactiveDistSq = std::numeric_limits<float>::max();
                for (size_t guideIndex = 0; guideIndex < guideRootUvs.size(); ++guideIndex)
                {
                    const float du = uvw.x - guideRootUvs[guideIndex].x;
                    const float dv = uvw.y - guideRootUvs[guideIndex].y;
                    const float distSq = du * du + dv * dv;
                    const float guideRisk = hairgpu::Clamp(guideRootRisk[guideIndex], 0.0f, 1.0f);
                    const bool guideActive = guideIndex < guideRootActive.size() && guideRootActive[guideIndex] != 0u;
                    if (distSq < bestDistSq)
                    {
                        bestDistSq = distSq;
                        risk = guideRisk;
                        nearestActive = guideActive;
                    }
                    if (guideActive)
                    {
                        nearestActiveDistSq = std::min(nearestActiveDistSq, distSq);
                    }
                    else
                    {
                        nearestInactiveDistSq = std::min(nearestInactiveDistSq, distSq);
                    }
                }
                risk = Clamp(risk, 0.0f, 1.0f);
                r = 0.16f + 0.84f * risk;
                g = 0.24f + 0.56f * (1.0f - risk);
                b = 0.96f - 0.82f * risk;
                if (m_maskOverlayMode == MaskOverlayMode::ActiveSetMotion)
                {
                    g = Clamp(g + 0.06f * risk, 0.0f, 1.0f);
                    b = Clamp(b + 0.04f * (1.0f - risk), 0.0f, 1.0f);
                }
                else if (m_maskOverlayMode == MaskOverlayMode::ActiveSetContact)
                {
                    r = Clamp(r + 0.12f * risk, 0.0f, 1.0f);
                    b = Clamp(b * (1.0f - 0.10f * risk), 0.0f, 1.0f);
                }
                else if (m_maskOverlayMode == MaskOverlayMode::ActiveSetUncertainty)
                {
                    r = Clamp(r * (1.0f - 0.10f * risk) + 0.12f * risk, 0.0f, 1.0f);
                    g = Clamp(g + 0.10f * (1.0f - risk), 0.0f, 1.0f);
                    b = Clamp(b + 0.08f, 0.0f, 1.0f);
                }
                else if (m_maskOverlayMode == MaskOverlayMode::ActiveSetEdit)
                {
                    r = Clamp(r + 0.08f * risk, 0.0f, 1.0f);
                    g = Clamp(g + 0.18f * risk, 0.0f, 1.0f);
                    b = Clamp(b * (1.0f - 0.18f * risk), 0.0f, 1.0f);
                }
                if (nearestActive)
                {
                    r = Clamp(r + 0.10f, 0.0f, 1.0f);
                    g = Clamp(g + 0.22f, 0.0f, 1.0f);
                    b = Clamp(b * 0.62f, 0.0f, 1.0f);
                }
                if (nearestActiveDistSq < std::numeric_limits<float>::max() && nearestInactiveDistSq < std::numeric_limits<float>::max())
                {
                    const float ds = std::sqrt(nearestActiveDistSq);
                    const float dof = std::sqrt(nearestInactiveDistSq);
                    const float denom = std::max(std::max(ds, dof), 1.0e-4f);
                    const float boundary = Clamp(1.0f - std::abs(ds - dof) / denom, 0.0f, 1.0f);
                    r = Clamp(r * (1.0f - 0.38f * boundary) + 0.98f * boundary, 0.0f, 1.0f);
                    g = Clamp(g * (1.0f - 0.22f * boundary) + 0.95f * boundary, 0.0f, 1.0f);
                    b = Clamp(b + 0.10f * boundary, 0.0f, 1.0f);
                }
            }
            ScalpVertex v = {};
            v.scalpIndex = static_cast<uint32_t>(i);
            v.nx = n.x;
            v.ny = n.y;
            v.nz = n.z;
            v.r = r;
            v.g = g;
            v.b = b;
            scalpVertices.push_back(v);
        }

        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (SUCCEEDED(m_context->Map(m_scalpVertexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            std::memcpy(mapped.pData, scalpVertices.data(), scalpVertices.size() * sizeof(ScalpVertex));
            m_context->Unmap(m_scalpVertexBuffer.Get(), 0);
        }
    }

    bool GpuHairViewer::ExportCurrentFrameCsv(const std::wstring& filePath)
    {
        std::vector<GpuParticle> particles;
        if (!ReadBackCurrentParticlePositions(particles))
        {
            return false;
        }

        std::ofstream out{ std::filesystem::path(filePath) };
        if (!out)
        {
            return false;
        }

        out << "strandIndex,pointIndex,x,y,z,width,isGuide,parentGuide,followWeight,clumpWeight,noiseWeight\n";
        for (size_t strandIndex = 0; strandIndex < m_asset.strands.size(); ++strandIndex)
        {
            const StrandRange& strand = m_asset.strands[strandIndex];
            const bool isGuide = strandIndex < m_asset.guideStrandCount;
            const FollowData follow = (strandIndex < m_asset.followData.size()) ? m_asset.followData[strandIndex] : FollowData{};
            for (uint32_t i = 0; i < strand.pointCount; ++i)
            {
                const uint32_t particleIndex = strand.pointOffset + i;
                const GpuParticle& p = particles[particleIndex];
                const float width = (particleIndex < m_asset.pointWidths.size()) ? m_asset.pointWidths[particleIndex] : 0.0f;
                out << strandIndex << "," << i << "," << p.x << "," << p.y << "," << p.z << "," << width << ","
                    << (isGuide ? 1 : 0) << "," << follow.parentGuide << "," << follow.followWeight << "," << follow.clumpWeight << "," << follow.noiseWeight << "\n";
            }
        }

        return true;
    }

    bool GpuHairViewer::ExportPerformanceCsv(const std::wstring& filePath) const
    {
        std::ofstream out{ std::filesystem::path(filePath) };
        if (!out)
        {
            return false;
        }

        out << "sampleIndex,elapsedSeconds,frameMilliseconds,simulationMilliseconds,fps,guideCount,followerCount,totalStrandCount,totalParticleCount,activeSetClusterCount,activeSetSelectedClusterCount,activeSetSelectedGuideCount,activeSetSelectedStrandCount,activeSetHighestScore,activeSetAverageSelectedScore,activeSetEvaluationMilliseconds,activeSetBudgetScale,activeSetRecommendedSolverIterations,fullTierCount,reducedTierCount,frozenTierCount,cachedTierCount,correctiveStrandCount,correctivePointCount,correctiveAveragePenetration,correctiveMaxPenetration,correctiveAverageLengthErrorBefore,correctiveAverageLengthErrorAfter\n";
        for (size_t i = 0; i < m_performanceSamples.size(); ++i)
        {
            const PerformanceSample& s = m_performanceSamples[i];
            out << i << ","
                << s.elapsedSeconds << ","
                << s.frameMilliseconds << ","
                << s.simulationMilliseconds << ","
                << s.fps << ","
                << s.guideCount << ","
                << s.followerCount << ","
                << s.totalStrandCount << ","
                << s.totalParticleCount << ","
                << s.activeSetClusterCount << ","
                << s.activeSetSelectedClusterCount << ","
                << s.activeSetSelectedGuideCount << ","
                << s.activeSetSelectedStrandCount << ","
                << s.activeSetHighestScore << ","
                << s.activeSetAverageSelectedScore << ","
                << s.activeSetEvaluationMilliseconds << ","
                << s.activeSetBudgetScale << ","
                << s.activeSetRecommendedSolverIterations << ","
                << s.fullTierCount << ","
                << s.reducedTierCount << ","
                << s.frozenTierCount << ","
                << s.cachedTierCount << ","
                << s.correctiveStrandCount << ","
                << s.correctivePointCount << ","
                << s.correctiveAveragePenetration << ","
                << s.correctiveMaxPenetration << ","
                << s.correctiveAverageLengthErrorBefore << ","
                << s.correctiveAverageLengthErrorAfter << "\n";
        }
        return true;
    }

    bool GpuHairViewer::ExportActiveSetPlanJson(const std::wstring& filePath) const
    {
        std::string error;
        return hairgpu::WriteActiveSetPlanJson(std::filesystem::path(filePath), m_asset, m_activeSetPlan, error);
    }

    bool GpuHairViewer::ExportRuntimeReport(const std::wstring& filePath) const
    {
        const std::filesystem::path summaryPath(filePath);
        std::ofstream out(summaryPath);
        if (!out)
        {
            return false;
        }

        auto p95 = [](std::vector<float> values) -> float
        {
            if (values.empty())
            {
                return 0.0f;
            }
            std::sort(values.begin(), values.end());
            const size_t idx = std::min(values.size() - 1, static_cast<size_t>(std::floor(0.95 * static_cast<double>(values.size() - 1))));
            return values[idx];
        };
        auto average = [](const std::vector<float>& values) -> float
        {
            if (values.empty())
            {
                return 0.0f;
            }
            const double sum = std::accumulate(values.begin(), values.end(), 0.0);
            return static_cast<float>(sum / static_cast<double>(values.size()));
        };

        std::vector<float> frameMs;
        std::vector<float> simMs;
        std::vector<float> selectedClusters;
        frameMs.reserve(m_performanceSamples.size());
        simMs.reserve(m_performanceSamples.size());
        selectedClusters.reserve(m_performanceSamples.size());
        for (const PerformanceSample& s : m_performanceSamples)
        {
            frameMs.push_back(s.frameMilliseconds);
            simMs.push_back(s.simulationMilliseconds);
            selectedClusters.push_back(static_cast<float>(s.activeSetSelectedClusterCount));
        }

        const std::filesystem::path baseDir = summaryPath.parent_path();
        const std::wstring stem = summaryPath.stem().wstring();
        const std::filesystem::path perfPath = baseDir / (stem + L"_performance.csv");
        const std::filesystem::path planPath = baseDir / (stem + L"_activeset_plan.json");
        const std::filesystem::path settingsPath = baseDir / (stem + L"_viewer_settings.ini");
        const bool perfOk = ExportPerformanceCsv(perfPath.wstring());
        const bool planOk = ExportActiveSetPlanJson(planPath.wstring());
        const bool settingsOk = SaveSettingsToFile(settingsPath.wstring());

        const float frameMax = frameMs.empty() ? 0.0f : *std::max_element(frameMs.begin(), frameMs.end());
        const float simMax = simMs.empty() ? 0.0f : *std::max_element(simMs.begin(), simMs.end());
        const std::string assetSourceUtf8 = WideToUtf8(m_lastAssetSource);
        const std::string importStatusUtf8 = WideToUtf8(m_lastImportStatus);
        const std::string editStatusUtf8 = WideToUtf8(m_lastEditStackStatus);
        const std::string perfPathUtf8 = WideToUtf8(perfPath.wstring());
        const std::string planPathUtf8 = WideToUtf8(planPath.wstring());
        const std::string settingsPathUtf8 = WideToUtf8(settingsPath.wstring());

        out << "HairGPU Viewer Runtime Report\n";
        out << "=============================\n";
        out << "assetSource=" << assetSourceUtf8 << "\n";
        out << "importStatus=" << importStatusUtf8 << "\n";
        out << "editStatus=" << editStatusUtf8 << "\n";
        out << "guides=" << m_asset.guideStrandCount;
        out << " followers=" << m_asset.followerStrandCount;
        out << " totalStrands=" << m_asset.strands.size();
        out << " particles=" << m_asset.restPoints.size() << "\n";
        out << "frame.avgMs=" << average(frameMs);
        out << " frame.p95Ms=" << p95(frameMs);
        out << " frame.maxMs=" << frameMax << "\n";
        out << "sim.avgMs=" << average(simMs);
        out << " sim.p95Ms=" << p95(simMs);
        out << " sim.maxMs=" << simMax << "\n";
        out << "activeset.avgSelectedClusters=" << average(selectedClusters);
        out << " currentSelectedClusters=" << m_activeSetPlan.summary.selectedClusterCount;
        out << " currentSelectedGuides=" << m_activeSetPlan.summary.selectedGuideCount;
        out << " budgetScale=" << m_activeSetPlan.summary.budgetScale << "\n";
        out << "budget.maxClusters=" << m_activeSetBudget.maxActiveClusters;
        out << " budget.maxGuides=" << m_activeSetBudget.maxActiveGuides;
        out << " budget.threshold=" << m_activeSetBudget.promoteScoreThreshold;
        out << " budget.targetFrameMs=" << m_activeSetBudget.targetFrameMilliseconds;
        out << " budget.fixedCostMs=" << m_activeSetBudget.estimatedFixedCostMilliseconds;
        out << " budget.clusterCostMs=" << m_activeSetBudget.estimatedClusterCostMilliseconds;
        out << " budget.adaptive=" << (m_activeSetBudget.enableBudgetAdaptation ? 1 : 0);
        out << " budget.hardClamp=" << (m_activeSetBudget.enableHardBudgetClamp ? 1 : 0) << "\n";
        out << "weights.motion=" << m_activeSetScoringSettings.motionWeight;
        out << " weights.contact=" << m_activeSetScoringSettings.contactWeight;
        out << " weights.uncertainty=" << m_activeSetScoringSettings.uncertaintyWeight;
        out << " weights.edit=" << m_activeSetScoringSettings.editWeight << "\n";
        out << "exports.performanceCsv=" << (perfOk ? 1 : 0) << " path=" << perfPathUtf8 << "\n";
        out << "exports.activeSetJson=" << (planOk ? 1 : 0) << " path=" << planPathUtf8 << "\n";
        out << "exports.viewerSettings=" << (settingsOk ? 1 : 0) << " path=" << settingsPathUtf8 << "\n";
        return true;
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
        {
            WigStyleSettings wig;
            wig.strength = 0.72f;
            wig.capRegion = 0.36f;
            wig.shellOffset = 0.0035f;
            wig.flowBlend = 0.86f;
            wig.outwardBlend = 0.10f;
            wig.restoreOriginalTowardsTip = 0.72f;
            StyleAssetToScalpWig(m_asset, wig);
        }
        m_builderGuideCount = m_asset.guideStrandCount;
        m_builderFollowersPerGuide = (m_asset.guideStrandCount > 0) ? (m_asset.followerStrandCount / m_asset.guideStrandCount) : 0u;
        m_builderParticlesPerStrand = m_asset.particlesPerStrand;
        m_activePresetName = L"Imported Asset";
        m_lastAssetSource = path.filename().wstring();
        m_lastImportStatus = (ext == L".csv") ? L"Curves CSV imported" : ((ext == L".usd" || ext == L".usda") ? L"USD ASCII imported" : ((ext == L".hairbin" || ext == L".hair") ? L"Hair binary imported" : L"Alembic imported"));
        m_baseAsset = m_asset;
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

        if (m_showScalpWireframe)
        {
            for (const TriangleGpuData& tri : m_scalpTriangles)
            {
                if (tri.a >= m_animatedScalpParticles.size() || tri.b >= m_animatedScalpParticles.size() || tri.c >= m_animatedScalpParticles.size())
                {
                    continue;
                }
                addScalpLine(tri.a, tri.b, 0.25f, 0.85f, 1.0f, 0.55f);
                addScalpLine(tri.b, tri.c, 0.25f, 0.85f, 1.0f, 0.55f);
                addScalpLine(tri.c, tri.a, 0.25f, 0.85f, 1.0f, 0.55f);
            }
        }

        if (m_showColliderDebug)
        {
            constexpr int segments = 24;
            for (const SphereCollider& sphere : m_colliders.spheres)
            {
                for (int axis = 0; axis < 3; ++axis)
                {
                    for (int i = 0; i < segments; ++i)
                    {
                        const float a0 = (static_cast<float>(i) / segments) * 6.28318530718f;
                        const float a1 = (static_cast<float>(i + 1) / segments) * 6.28318530718f;
                        Vec3 p0 = sphere.center;
                        Vec3 p1 = sphere.center;
                        if (axis == 0)
                        {
                            p0.y += std::cos(a0) * sphere.radius; p0.z += std::sin(a0) * sphere.radius;
                            p1.y += std::cos(a1) * sphere.radius; p1.z += std::sin(a1) * sphere.radius;
                        }
                        else if (axis == 1)
                        {
                            p0.x += std::cos(a0) * sphere.radius; p0.z += std::sin(a0) * sphere.radius;
                            p1.x += std::cos(a1) * sphere.radius; p1.z += std::sin(a1) * sphere.radius;
                        }
                        else
                        {
                            p0.x += std::cos(a0) * sphere.radius; p0.y += std::sin(a0) * sphere.radius;
                            p1.x += std::cos(a1) * sphere.radius; p1.y += std::sin(a1) * sphere.radius;
                        }
                        addWorldLine(p0, p1, 1.0f, 0.55f, 0.15f, 0.8f);
                    }
                }
            }

            for (const CapsuleCollider& capsule : m_colliders.capsules)
            {
                const Vec3 axis = Normalize(capsule.pointB - capsule.pointA);
                Vec3 temp = (std::fabs(axis.y) < 0.95f) ? Vec3(0.0f, 1.0f, 0.0f) : Vec3(1.0f, 0.0f, 0.0f);
                const Vec3 tangent = Normalize(Cross(axis, temp));
                const Vec3 bitangent = Normalize(Cross(axis, tangent));
                constexpr int segments = 24;
                for (int i = 0; i < segments; ++i)
                {
                    const float a0 = (static_cast<float>(i) / segments) * 6.28318530718f;
                    const float a1 = (static_cast<float>(i + 1) / segments) * 6.28318530718f;
                    const Vec3 ring0 = tangent * std::cos(a0) + bitangent * std::sin(a0);
                    const Vec3 ring1 = tangent * std::cos(a1) + bitangent * std::sin(a1);
                    const Vec3 a0p = capsule.pointA + ring0 * capsule.radiusA;
                    const Vec3 a1p = capsule.pointA + ring1 * capsule.radiusA;
                    const Vec3 b0p = capsule.pointB + ring0 * capsule.radiusB;
                    const Vec3 b1p = capsule.pointB + ring1 * capsule.radiusB;
                    addWorldLine(a0p, a1p, 0.85f, 0.35f, 0.90f, 0.75f);
                    addWorldLine(b0p, b1p, 0.85f, 0.35f, 0.90f, 0.75f);
                    if ((i % 6) == 0)
                    {
                        addWorldLine(a0p, b0p, 0.85f, 0.35f, 0.90f, 0.75f);
                    }
                }
            }
        }

        const bool needStrandDebug = m_showGuideSkeleton || m_showFollowerSkeleton || m_showRootMarkers || m_showFollowLinks;
        if (needStrandDebug)
        {
            const uint32_t guideCount = m_asset.guideStrandCount;
            const uint32_t followerCount = m_asset.followerStrandCount;
            const uint32_t totalCount = static_cast<uint32_t>(m_asset.strands.size());
            const uint32_t followerStride = std::max<uint32_t>(1u, followerCount / 96u + ((followerCount > 96u) ? 1u : 0u));

            auto addStrandSkeleton = [&](uint32_t strandIndex, float r, float g, float b, float alpha)
            {
                if (strandIndex >= totalCount)
                {
                    return;
                }
                const StrandRange& strand = m_asset.strands[strandIndex];
                for (uint32_t i = 1; i < strand.pointCount; ++i)
                {
                    const uint32_t aIndex = strand.pointOffset + i - 1u;
                    const uint32_t bIndex = strand.pointOffset + i;
                    addHairLine(aIndex, bIndex, r, g, b, alpha);
                }
            };

            if (m_showGuideSkeleton || m_showRootMarkers)
            {
                for (uint32_t strandIndex = 0; strandIndex < guideCount; ++strandIndex)
                {
                    const StrandRange& strand = m_asset.strands[strandIndex];
                    if (m_showGuideSkeleton)
                    {
                        addStrandSkeleton(strandIndex, 0.12f, 0.95f, 0.92f, 0.85f);
                    }
                    if (m_showRootMarkers)
                    {
                        addRootMarker(strand.pointOffset, true);
                    }
                }
            }

            if (m_showFollowerSkeleton || m_showRootMarkers || m_showFollowLinks)
            {
                for (uint32_t followerLocalIndex = 0; followerLocalIndex < followerCount; ++followerLocalIndex)
                {
                    if (followerLocalIndex % followerStride != 0u)
                    {
                        continue;
                    }
                    const uint32_t strandIndex = guideCount + followerLocalIndex;
                    if (strandIndex >= totalCount)
                    {
                        continue;
                    }
                    const StrandRange& strand = m_asset.strands[strandIndex];
                    if (m_showFollowerSkeleton)
                    {
                        addStrandSkeleton(strandIndex, 1.00f, 0.52f, 0.18f, 0.42f);
                    }
                    if (m_showRootMarkers)
                    {
                        addRootMarker(strand.pointOffset, false);
                    }
                    if (m_showFollowLinks && strandIndex < m_asset.followData.size())
                    {
                        const int32_t parentGuide = m_asset.followData[strandIndex].parentGuide;
                        if (parentGuide >= 0 && static_cast<uint32_t>(parentGuide) < guideCount)
                        {
                            addHairLine(strand.pointOffset, m_asset.strands[static_cast<uint32_t>(parentGuide)].pointOffset, 0.86f, 0.30f, 1.00f, 0.32f);
                        }
                    }
                }
            }
        }

        m_debugLineStats.totalLineCount = m_debugLineStats.worldLineCount + m_debugLineStats.hairLineCount + m_debugLineStats.scalpLineCount + m_debugLineStats.rootMarkerCount;
        m_gpuDebugLinesDirty = false;
    }

    void GpuHairViewer::UpdateRenderStatistics()
    {
        const uint64_t totalParticles = static_cast<uint64_t>(m_asset.restPoints.size());
        const uint64_t totalStrands = static_cast<uint64_t>(m_asset.strands.size());
        const uint64_t guideParticles = static_cast<uint64_t>(m_asset.guideStrandCount) * static_cast<uint64_t>(m_asset.particlesPerStrand);
        const uint64_t followerParticles = (totalParticles >= guideParticles) ? (totalParticles - guideParticles) : 0ull;
        (void)totalStrands;
        (void)guideParticles;
        (void)followerParticles;
    }

    LRESULT CALLBACK GpuHairViewer::WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        GpuHairViewer* app = reinterpret_cast<GpuHairViewer*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE)
        {
            const CREATESTRUCTW* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            app = reinterpret_cast<GpuHairViewer*>(create->lpCreateParams);
            if (app)
            {
                app->m_hwnd = hwnd;
            }
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        }

        return app ? app->HandleMessage(hwnd, message, wParam, lParam) : DefWindowProcW(hwnd, message, wParam, lParam);
    }

    LRESULT GpuHairViewer::HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message)
        {
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        case WM_SIZE:
            Resize(LOWORD(lParam), HIWORD(lParam));
            return 0;
        case WM_LBUTTONDOWN:
        {
            const RECT renderRect = GetRenderViewportRect();
            const POINT p = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            if (PtInRect(&renderRect, p))
            {
                m_dragging = true;
                m_lastMouse = p;
                SetCapture(m_hwnd);
            }
            return 0;
        }
        case WM_LBUTTONUP:
            m_dragging = false;
            ReleaseCapture();
            return 0;
        case WM_MOUSEMOVE:
            if (m_dragging)
            {
                POINT p = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                const float dx = static_cast<float>(p.x - m_lastMouse.x);
                const float dy = static_cast<float>(p.y - m_lastMouse.y);
                m_lastMouse = p;
                m_cameraYaw += dx * 0.01f;
                m_cameraPitch = Clamp(m_cameraPitch + dy * 0.01f, -1.2f, 1.2f);
            }
            return 0;
        case WM_MOUSEWHEEL:
        {
            POINT screenPoint = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hwnd, &screenPoint);
            const RECT renderRect = GetRenderViewportRect();
            if (PtInRect(&renderRect, screenPoint))
            {
                const float wheel = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / static_cast<float>(WHEEL_DELTA);
                m_cameraDistance = Clamp(m_cameraDistance - wheel * 0.15f, 0.6f, 8.0f);
            }
            return 0;
        }
        case WM_HSCROLL:
            if (!m_syncingControls)
            {
                SyncStateFromControls();
            }
            return 0;
        case WM_COMMAND:
        {
            if (m_syncingControls)
            {
                return 0;
            }

            const int id = LOWORD(wParam);
            const int code = HIWORD(wParam);
            if (id == IDC_LIST_INLINE_LAYERS && code == LBN_SELCHANGE)
            {
                m_selectedInlineLayerIndex = GetSelectedInlineLayerIndex();
                LoadQuickEditFormFromInlineText(m_selectedInlineLayerIndex);
                return 0;
            }
            switch (id)
            {
            case IDC_BTN_PLAYPAUSE:
                m_paused = !m_paused;
                SyncControlsFromState();
                return 0;
            case IDC_BTN_RESET:
                ResetSimulation();
                return 0;
            case IDC_BTN_STEP:
                StepSingleFrame();
                return 0;
            case IDC_BTN_APPLY_ASSET:
                SyncStateFromControls();
                RebuildAsset(m_builderGuideCount, m_builderFollowersPerGuide, m_builderParticlesPerStrand);
                return 0;
            case IDC_BTN_BROWSE_EDIT_STACK:
                BrowseAndSetEditStackPath();
                return 0;
            case IDC_BTN_CLEAR_EDIT_STACK:
                if (!ClearEditStackAndRestoreAsset())
                {
                    MessageBoxW(hwnd, L"Failed to clear the current edit stack.", L"Edit Stack", MB_OK | MB_ICONERROR);
                }
                return 0;
            case IDC_BTN_APPLY_INLINE_EDIT_STACK:
                ApplyInlineEditStackFromUi(true);
                return 0;
            case IDC_BTN_SAVE_INLINE_EDIT_STACK:
            {
                wchar_t fileName[MAX_PATH] = L"viewer_inline_edit_stack.csv";
                OPENFILENAMEW ofn = {};
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = hwnd;
                ofn.lpstrFilter = L"Edit Stack CSV (*.csv)\0*.csv\0All Files\0*.*\0";
                ofn.lpstrFile = fileName;
                ofn.nMaxFile = MAX_PATH;
                ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
                if (GetSaveFileNameW(&ofn))
                {
                    if (!SaveInlineEditStackToPath(fileName, true))
                    {
                        MessageBoxW(hwnd, L"Failed to save the inline edit stack.", L"Inline Edit Stack", MB_OK | MB_ICONERROR);
                    }
                }
                return 0;
            }
            case IDC_BTN_RESET_INLINE_EDIT_STACK:
                ResetInlineEditStackText();
                return 0;
            case IDC_BTN_QUICK_EDIT_REPLACE:
                PushQuickEditFormToInlineText(true, true);
                return 0;
            case IDC_BTN_QUICK_EDIT_APPEND:
                PushQuickEditFormToInlineText(false, true);
                return 0;
            case IDC_BTN_QUICK_EDIT_MOVE_UP:
                if (!MoveSelectedInlineLayer(-1, true))
                {
                    MessageBoxW(hwnd, L"The selected inline layer could not be moved up.", L"Quick Edit Form", MB_OK | MB_ICONWARNING);
                }
                return 0;
            case IDC_BTN_QUICK_EDIT_MOVE_DOWN:
                if (!MoveSelectedInlineLayer(1, true))
                {
                    MessageBoxW(hwnd, L"The selected inline layer could not be moved down.", L"Quick Edit Form", MB_OK | MB_ICONWARNING);
                }
                return 0;
            case IDC_CHECK_QUICK_EDIT_ENABLED:
                if (!m_syncingControls)
                {
                    ToggleSelectedInlineLayerEnabled(true);
                }
                return 0;
            case IDC_CHECK_QUICK_EDIT_GUIDES_ONLY:
                SyncStateFromControls();
                RefreshQuickEditFormLabels();
                if (IsEditInfluenceOverlayMode())
                {
                    UpdateScalpOverlayVertexBuffer();
                }
                return 0;
            case IDC_EDIT_QUICK_EDIT_GROUP_ID:
                if (code == EN_CHANGE)
                {
                    SyncStateFromControls();
                    RefreshQuickEditFormLabels();
                    if (IsEditInfluenceOverlayMode())
                    {
                        UpdateScalpOverlayVertexBuffer();
                    }
                }
                return 0;
            case IDC_BTN_QUICK_EDIT_DUPLICATE:
                if (!DuplicateSelectedInlineLayer(true))
                {
                    MessageBoxW(hwnd, L"No inline layer could be duplicated.", L"Quick Edit Form", MB_OK | MB_ICONWARNING);
                }
                return 0;
            case IDC_BTN_QUICK_EDIT_DELETE:
                if (!DeleteSelectedInlineLayer(true))
                {
                    MessageBoxW(hwnd, L"No inline layer could be deleted.", L"Quick Edit Form", MB_OK | MB_ICONWARNING);
                }
                return 0;
            case IDC_BTN_IMPORT_ASSET:
            {
                wchar_t fileName[MAX_PATH] = L"";
                OPENFILENAMEW ofn = {};
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = hwnd;
                ofn.lpstrFilter = L"Hair Assets (*.csv;*.abc;*.hairbin;*.hair)\0*.csv;*.abc;*.hairbin;*.hair\0Curves CSV (*.csv)\0*.csv\0Alembic (*.abc)\0*.abc\0Hair Binary (*.hairbin;*.hair)\0*.hairbin;*.hair\0All Files\0*.*\0";
                ofn.lpstrFile = fileName;
                ofn.nMaxFile = MAX_PATH;
                ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
                if (GetOpenFileNameW(&ofn))
                {
                    if (!LoadHairAssetFromPath(fileName))
                    {
                        const std::wstring message = L"Import failed.\r\n\r\n" + m_lastImportStatus;
                        MessageBoxW(hwnd, message.c_str(), L"Hair Import", MB_OK | MB_ICONERROR);
                    }
                }
                return 0;
            }
            case IDC_BTN_SAVE_SETTINGS:
            {
                wchar_t fileName[MAX_PATH] = L"hair_viewer_settings.ini";
                OPENFILENAMEW ofn = {};
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = hwnd;
                ofn.lpstrFilter = L"INI Files\0*.ini\0All Files\0*.*\0";
                ofn.lpstrFile = fileName;
                ofn.nMaxFile = MAX_PATH;
                ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
                if (GetSaveFileNameW(&ofn))
                {
                    if (!SaveSettingsToFile(fileName))
                    {
                        MessageBoxW(hwnd, L"Failed to save settings.", L"Save Failed", MB_OK | MB_ICONERROR);
                    }
                }
                return 0;
            }
            case IDC_BTN_LOAD_SETTINGS:
            {
                wchar_t fileName[MAX_PATH] = L"";
                OPENFILENAMEW ofn = {};
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = hwnd;
                ofn.lpstrFilter = L"INI Files\0*.ini\0All Files\0*.*\0";
                ofn.lpstrFile = fileName;
                ofn.nMaxFile = MAX_PATH;
                ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
                if (GetOpenFileNameW(&ofn))
                {
                    if (!LoadSettingsFromFile(fileName))
                    {
                        MessageBoxW(hwnd, L"Failed to load settings.", L"Load Failed", MB_OK | MB_ICONERROR);
                    }
                }
                return 0;
            }
            case IDC_BTN_EXPORT_CSV:
            {
                wchar_t fileName[MAX_PATH] = L"hair_frame.csv";
                OPENFILENAMEW ofn = {};
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = hwnd;
                ofn.lpstrFilter = L"CSV Files\0*.csv\0All Files\0*.*\0";
                ofn.lpstrFile = fileName;
                ofn.nMaxFile = MAX_PATH;
                ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
                if (GetSaveFileNameW(&ofn))
                {
                    if (!ExportCurrentFrameCsv(fileName))
                    {
                        MessageBoxW(hwnd, L"Failed to export current strand positions.", L"Export Failed", MB_OK | MB_ICONERROR);
                    }
                }
                return 0;
            }
            case IDC_BTN_EXPORT_PERF_CSV:
            {
                wchar_t fileName[MAX_PATH] = L"hair_performance.csv";
                OPENFILENAMEW ofn = {};
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = hwnd;
                ofn.lpstrFilter = L"CSV Files\0*.csv\0All Files\0*.*\0";
                ofn.lpstrFile = fileName;
                ofn.nMaxFile = MAX_PATH;
                ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
                if (GetSaveFileNameW(&ofn))
                {
                    if (!ExportPerformanceCsv(fileName))
                    {
                        MessageBoxW(hwnd, L"Failed to export performance history.", L"Export Failed", MB_OK | MB_ICONERROR);
                    }
                }
                return 0;
            }
            case IDC_BTN_EXPORT_ACTIVESET_JSON:
            {
                wchar_t fileName[MAX_PATH] = L"active_set_plan.json";
                OPENFILENAMEW ofn = {};
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = hwnd;
                ofn.lpstrFilter = L"JSON Files\0*.json\0All Files\0*.*\0";
                ofn.lpstrFile = fileName;
                ofn.nMaxFile = MAX_PATH;
                ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
                if (GetSaveFileNameW(&ofn))
                {
                    if (!ExportActiveSetPlanJson(fileName))
                    {
                        MessageBoxW(hwnd, L"Failed to export the active-set plan JSON.", L"Export Failed", MB_OK | MB_ICONERROR);
                    }
                }
                return 0;
            }
            case IDC_BTN_EXPORT_RUNTIME_REPORT:
            {
                wchar_t fileName[MAX_PATH] = L"hair_runtime_report.txt";
                OPENFILENAMEW ofn = {};
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = hwnd;
                ofn.lpstrFilter = L"Text Files\0*.txt\0All Files\0*.*\0";
                ofn.lpstrFile = fileName;
                ofn.nMaxFile = MAX_PATH;
                ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
                if (GetSaveFileNameW(&ofn))
                {
                    if (!ExportRuntimeReport(fileName))
                    {
                        MessageBoxW(hwnd, L"Failed to export the runtime report bundle.", L"Export Failed", MB_OK | MB_ICONERROR);
                    }
                }
                return 0;
            }
            case IDC_BTN_BROWSE_PARTING_MASK:
                if (BrowseAndSetMaskPath(true))
                {
                    RebuildAsset(m_builderGuideCount, m_builderFollowersPerGuide, m_builderParticlesPerStrand);
                }
                return 0;
            case IDC_BTN_BROWSE_REGION_MASK:
                if (BrowseAndSetMaskPath(false))
                {
                    RebuildAsset(m_builderGuideCount, m_builderFollowersPerGuide, m_builderParticlesPerStrand);
                }
                return 0;
            case IDC_PREVIEW_PARTING_MASK:
                if (code == STN_CLICKED) { ShowMaskPreviewPopup(true); }
                return 0;
            case IDC_PREVIEW_REGION_MASK:
                if (code == STN_CLICKED) { ShowMaskPreviewPopup(false); }
                return 0;
            case IDC_COMBO_PRESET:
                if (code == CBN_SELCHANGE)
                {
                    const int preset = static_cast<int>(SendMessageW(m_comboPreset, CB_GETCURSEL, 0, 0));
                    ApplyPreset(preset);
                    RebuildAsset(m_builderGuideCount, m_builderFollowersPerGuide, m_builderParticlesPerStrand);
                }
                return 0;
            case IDC_COMBO_RENDER_MODE:
            case IDC_COMBO_DEBUG_COLOR:
            case IDC_COMBO_MASK_SOURCE:
            case IDC_COMBO_MASK_OVERLAY:
            case IDC_COMBO_QUICK_EDIT_MODE:
                if (code == CBN_SELCHANGE)
                {
                    SyncStateFromControls();
                    RefreshMaskPreviewControls();
                    if (LOWORD(wParam) == IDC_COMBO_MASK_SOURCE)
                    {
                        RebuildAsset(m_builderGuideCount, m_builderFollowersPerGuide, m_builderParticlesPerStrand);
                    }
                    else if (LOWORD(wParam) == IDC_COMBO_MASK_OVERLAY)
                    {
                        UpdateScalpOverlayVertexBuffer();
                    }
                }
                return 0;
            case IDC_EDIT_QUICK_EDIT_NAME:
                if (code == EN_KILLFOCUS)
                {
                    SyncStateFromControls();
                }
                return 0;
            case IDC_EDIT_GUIDES:
            case IDC_EDIT_FOLLOWERS:
            case IDC_EDIT_PARTICLES:
                if (code == EN_KILLFOCUS)
                {
                    SyncStateFromControls();
                    RebuildAsset(m_builderGuideCount, m_builderFollowersPerGuide, m_builderParticlesPerStrand);
                }
                return 0;
            case IDC_CHECK_SELF_COLLISION:
            case IDC_CHECK_GRID:
            case IDC_CHECK_SCALP_MOTION:
            case IDC_CHECK_SHOW_COLLIDERS:
            case IDC_CHECK_SHOW_SCALP_MESH:
            case IDC_CHECK_SHOW_SCALP:
            case IDC_CHECK_SHOW_GRID_BOUNDS:
            case IDC_CHECK_SHOW_FLOOR:
            case IDC_CHECK_SHOW_GUIDES:
            case IDC_CHECK_SHOW_FOLLOWERS:
            case IDC_CHECK_SHOW_ROOTS:
            case IDC_CHECK_SHOW_LINKS:
            case IDC_CHECK_ACTIVESET_ADAPTIVE_BUDGET:
            case IDC_CHECK_ACTIVESET_HARD_CLAMP:
                if (code == BN_CLICKED)
                {
                    SyncStateFromControls();
                }
                return 0;
            default:
                break;
            }
            break;
        }
        case WM_KEYDOWN:
            switch (wParam)
            {
            case VK_SPACE:
                m_paused = !m_paused;
                SyncControlsFromState();
                return 0;
            case 'R':
                ResetSimulation();
                return 0;
            case 'N':
                if (m_paused)
                {
                    StepSingleFrame();
                }
                return 0;
            case 'C':
                m_debugColorMode = static_cast<DebugColorMode>((static_cast<uint32_t>(m_debugColorMode) + 1u) % 3u);
                SyncControlsFromState();
                return 0;
            case 'F':
                m_showFloorGuide = !m_showFloorGuide;
                SyncControlsFromState();
                return 0;
            case 'H':
                m_showGridBounds = !m_showGridBounds;
                SyncControlsFromState();
                return 0;
            case '1':
                m_widthScale = Clamp(m_widthScale * 0.85f, 0.2f, 8.0f);
                SyncControlsFromState();
                return 0;
            case '2':
                m_widthScale = Clamp(m_widthScale * 1.15f, 0.2f, 8.0f);
                SyncControlsFromState();
                return 0;
            case '3':
                m_enableSelfCollision = !m_enableSelfCollision;
                SyncControlsFromState();
                return 0;
            case '4':
                SyncControlsFromState();
                return 0;
            case '5':
                m_selfCollisionRadius = Clamp(m_selfCollisionRadius * 0.90f, 0.004f, 0.040f);
                SyncControlsFromState();
                return 0;
            case '6':
                m_selfCollisionRadius = Clamp(m_selfCollisionRadius * 1.10f, 0.004f, 0.040f);
                SyncControlsFromState();
                return 0;
            case '7':
                m_simConfig.stretchCompliance *= 0.5f;
                SyncControlsFromState();
                return 0;
            case '8':
                m_simConfig.stretchCompliance *= 2.0f;
                SyncControlsFromState();
                return 0;
            case '9':
                m_simConfig.bendCompliance *= 0.5f;
                SyncControlsFromState();
                return 0;
            case '0':
                m_simConfig.bendCompliance *= 2.0f;
                SyncControlsFromState();
                return 0;
            case 'G':
                m_enableGridSelfCollision = !m_enableGridSelfCollision;
                SyncControlsFromState();
                return 0;
            case 'B':
                m_selfCollisionBroadphaseMode = 0u;
                m_enableGridSelfCollision = true;
                SyncControlsFromState();
                return 0;
            case 'M':
                m_showScalpMesh = !m_showScalpMesh;
                SyncControlsFromState();
                return 0;
            case 'J':
                m_showGuideSkeleton = !m_showGuideSkeleton;
                SyncControlsFromState();
                return 0;
            case 'K':
                m_showFollowerSkeleton = !m_showFollowerSkeleton;
                SyncControlsFromState();
                return 0;
            case 'L':
                m_showFollowLinks = !m_showFollowLinks;
                SyncControlsFromState();
                return 0;
            case 'U':
                m_showRootMarkers = !m_showRootMarkers;
                SyncControlsFromState();
                return 0;
            case 'T':
                m_renderMode = (m_renderMode == RenderMode::ParticlePointSprite) ? RenderMode::ParticleBillboard :
                               (m_renderMode == RenderMode::ParticleBillboard) ? RenderMode::StrandRibbon :
                               RenderMode::ParticlePointSprite;
                SyncControlsFromState();
                return 0;
            case VK_OEM_MINUS:
                m_simConfig.selfCollisionCellSize = Clamp(m_simConfig.selfCollisionCellSize * 0.92f, 0.03f, 0.20f);
                SyncControlsFromState();
                return 0;
            case VK_OEM_PLUS:
                m_simConfig.selfCollisionCellSize = Clamp(m_simConfig.selfCollisionCellSize * 1.08f, 0.03f, 0.20f);
                SyncControlsFromState();
                return 0;
            default:
                break;
            }
            break;
        default:
            break;
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}
