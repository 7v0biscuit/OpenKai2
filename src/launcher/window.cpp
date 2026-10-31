// SPDX-License-Identifier: GPL-3.0-only
#include "launcher/window.h"
#include "discord_presence.h"
#include "launcher/display_font.h"
#include "disc_image.h"
#include "launcher/saves.h"
#include "launcher/session.h"
#include "launcher/settings.h"
#include "launcher/update.h"
#include "launcher/version.h"
#include "mods/budokai3.h"
#include "mods/disc_identity.h"
#include "mods/importer.h"
#include "mods/mod_library.h"
#include "mods/roster.h"
#include "mods/stages.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cwchar>
#include <cwctype>
#include <commctrl.h>
#include <commdlg.h>
#include <fstream>
#include <functional>
#include <future>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <vector>
#include <windowsx.h>

namespace openkai2::launcher {
namespace {
constexpr COLORREF background = RGB(9, 11, 19), sidebar = RGB(12, 14, 24), panel = RGB(17, 20, 33);
constexpr COLORREF field = RGB(22, 26, 42), raised = RGB(29, 34, 53), hover = RGB(39, 45, 69);
constexpr COLORREF border = RGB(35, 41, 61), ink = RGB(242, 238, 230), muted = RGB(148, 153, 174);
constexpr COLORREF faint = RGB(86, 93, 117), orange = RGB(255, 136, 28), gold = RGB(255, 204, 86);
constexpr COLORREF red = RGB(255, 112, 102), green = RGB(98, 214, 152);
constexpr COLORREF ember = RGB(255, 112, 24), sky = RGB(70, 110, 255);
constexpr COLORREF heroFrom = RGB(64, 24, 8), heroTo = RGB(15, 17, 31);

constexpr int sidebarWidth = 220, contentLeft = 252, margin = 32;
constexpr int heroTop = 124, heroBottom = 316, discTop = 332, discBottom = 420;
constexpr int tilesTop = 436, tilesBottom = 516, keysTop = 540;
constexpr int displayTop = 124, graphicsTop = 296, updatesTop = 468;

enum Id {
    PlayTab = 101,
    SavesTab,
    ModsTab,
    SettingsTab,
    ChooseIso = 110,
    VerifyIso,
    Play,
    ImportFolder = 120,
    ImportFile,
    Backup,
    Restore,
    OpenSaves,
    OpenBackups,
    BackupList,
    UnlockAll,
    DeleteSelected,
    Renderer = 140,
    Fps,
    Resolution,
    WindowMode,
    Antialiasing,
    Filtering,
    Upscaler,
    Vsync,
    AutoBackup,
    Aspect,
    Audio,
    UpdateChannel = 160,
    AutomaticUpdates,
    CheckUpdates,
    InstallUpdate,
    ReleasePage,
    ResetGraphics = 170,
    ClearImage = 173,
    DiscordActivity = 174,
    SaveListKind = 175,
    SelectSaves = 176,
    ModList = 180,
    ModToggle,
    ModUp,
    ModDown,
    ModRefresh,
    ModFolder,
    ModName,
    ModCreate,
    OpenImporter,
    ModContents,
    ModRename,
    ModDelete,
    ModRemove,
    SourceGame = 200,
    SourceIso = 201,
    SourceFiles = 202,
    EntryFilter = 204,
    TargetMod = 205,
    ImportSelected = 210,
    CloseImporter = 213,
    TaskCharacters = 214,
    TaskStages = 215,
    TaskMovesets = 216,
    TargetList = 217,
    SpotMode = 218,
    TaskCapsules = 219,
    SaveReport = 220,
    SourceLast = 221
};
constexpr int pageCount = 4, modsPage = 2, settingsPage = 3, importPage = 4;
constexpr std::array<unsigned, 4> audioVolumes{100u, 75u, 50u, 25u};
int audioChoice(const Settings &settings) {
    if (!settings.audio)
        return 0;
    const auto nearest = std::min_element(audioVolumes.begin(), audioVolumes.end(), [&](unsigned a, unsigned b) {
        return std::abs(int(a) - int(settings.volume)) < std::abs(int(b) - int(settings.volume));
    });
    return 1 + int(nearest - audioVolumes.begin());
}

std::wstring wide(const std::string &s) {
    if (s.empty())
        return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring r(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), r.data(), n);
    return r;
}
std::string utf8(const std::wstring &s) {
    if (s.empty())
        return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0, nullptr, nullptr);
    std::string r(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), r.data(), n, nullptr, nullptr);
    return r;
}
COLORREF mix(COLORREF a, COLORREF b, int t) {
    t = std::clamp(t, 0, 255);
    const auto channel = [t](int x, int y) { return x + (y - x) * t / 255; };
    return RGB(channel(GetRValue(a), GetRValue(b)), channel(GetGValue(a), GetGValue(b)),
               channel(GetBValue(a), GetBValue(b)));
}
void fill(HDC dc, RECT r, COLORREF color) {
    SetDCBrushColor(dc, color);
    FillRect(dc, &r, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
}
void line(HDC dc, int x, int y, int w, COLORREF color) { fill(dc, {x, y, x + w, y + 1}, color); }
void ellipse(HDC dc, int cx, int cy, int radius, COLORREF color) {
    SetDCBrushColor(dc, color);
    auto oldBrush = SelectObject(dc, GetStockObject(DC_BRUSH));
    auto oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
    Ellipse(dc, cx - radius, cy - radius, cx + radius + 1, cy + radius + 1);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
}
void lettering(HDC dc, const std::wstring &value, int x, int y, HFONT font, COLORREF color, COLORREF edge,
               int stroke) {
    auto oldFont = SelectObject(dc, font);
    const auto oldAlign = SetTextAlign(dc, TA_LEFT | TA_BASELINE);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, edge);
    for (int dy = -stroke; dy <= stroke; ++dy)
        for (int dx = -stroke; dx <= stroke; ++dx)
            if ((dx || dy) && dx * dx + dy * dy <= stroke * stroke + 1)
                TextOutW(dc, x + dx, y + dy, value.c_str(), int(value.size()));
    SetTextColor(dc, color);
    TextOutW(dc, x, y, value.c_str(), int(value.size()));
    SetTextAlign(dc, oldAlign);
    SelectObject(dc, oldFont);
}
std::array<POINT, 6> cutCorners(RECT r, int cut) {
    return {POINT{r.left + cut, r.top}, POINT{r.right - 1, r.top}, POINT{r.right - 1, r.bottom - 1 - cut},
            POINT{r.right - 1 - cut, r.bottom - 1}, POINT{r.left, r.bottom - 1}, POINT{r.left, r.top + cut}};
}
HRGN cutRegion(RECT r, int cut) {
    const std::array<POINT, 6> points{POINT{r.left + cut, r.top}, POINT{r.right, r.top}, POINT{r.right, r.bottom - cut},
                                      POINT{r.right - cut, r.bottom}, POINT{r.left, r.bottom}, POINT{r.left, r.top + cut}};
    return CreatePolygonRgn(points.data(), int(points.size()), WINDING);
}
COLORREF blend(COLORREF a, COLORREF b, double t) {
    t = std::clamp(t, 0.0, 1.0);
    const auto channel = [t](int x, int y) { return int(std::lround(x + (y - x) * t)); };
    return RGB(channel(GetRValue(a), GetRValue(b)), channel(GetGValue(a), GetGValue(b)), channel(GetBValue(a), GetBValue(b)));
}
struct Surface {
    HDC dc;
    uint32_t *pixels;
    int width, height;
};
std::vector<Surface> &surfaces() {
    static std::vector<Surface> list;
    return list;
}
template <class Shade>
bool cutShape(HDC dc, RECT r, double cut, Shade shade, std::optional<COLORREF> outline, bool filled = true) {
    const auto &list = surfaces();
    const auto found = std::find_if(list.begin(), list.end(), [dc](const Surface &s) { return s.dc == dc; });
    if (found == list.end() || !found->pixels)
        return false;
    GdiFlush();
    POINT origin{};
    GetViewportOrgEx(dc, &origin);
    OffsetRect(&r, origin.x, origin.y);
    auto *pixels = found->pixels;
    const int width = found->width, height = found->height;
    for (int y = std::max<int>(0, r.top); y < std::min<int>(height, r.bottom); ++y)
        for (int x = std::max<int>(0, r.left); x < std::min<int>(width, r.right); ++x) {
            const double px = x + 0.5, py = y + 0.5;
            const double edge = std::max({r.left - px, px - r.right, r.top - py, py - r.bottom,
                                          (cut - (px - r.left) - (py - r.top)) / 1.4142,
                                          (cut - (r.right - px) - (r.bottom - py)) / 1.4142});
            double cover = std::clamp(0.5 - edge, 0.0, 1.0);
            if (cover <= 0)
                continue;
            const double inner = std::clamp(-0.5 - edge, 0.0, 1.0);
            COLORREF color = filled ? shade(x - origin.x, y - origin.y) : *outline;
            if (outline && filled)
                color = blend(*outline, color, inner);
            else if (outline)
                cover *= 1.0 - inner;
            uint32_t &pixel = pixels[size_t(y) * size_t(width) + size_t(x)];
            const COLORREF under = RGB(pixel >> 16 & 255, pixel >> 8 & 255, pixel & 255);
            const COLORREF out = blend(under, color, cover);
            pixel = uint32_t(GetRValue(out)) << 16 | uint32_t(GetGValue(out)) << 8 | GetBValue(out);
        }
    return true;
}
struct Canvas {
    HDC target, dc{};
    RECT area;
    HBITMAP bitmap{};
    HGDIOBJ previous{};
    Canvas(HDC into, RECT r) : target(into), area(r) {
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(info.bmiHeader);
        info.bmiHeader.biWidth = std::max(1L, r.right - r.left);
        info.bmiHeader.biHeight = -std::max(1L, r.bottom - r.top);
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void *bits = nullptr;
        bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        dc = CreateCompatibleDC(into);
        if (bitmap) {
            previous = SelectObject(dc, bitmap);
            surfaces().push_back({dc, static_cast<uint32_t *>(bits), int(info.bmiHeader.biWidth), int(-info.bmiHeader.biHeight)});
        }
        SetViewportOrgEx(dc, -r.left, -r.top, nullptr);
    }
    ~Canvas() {
        auto &list = surfaces();
        list.erase(std::remove_if(list.begin(), list.end(), [this](const Surface &s) { return s.dc == dc; }), list.end());
        SetViewportOrgEx(dc, 0, 0, nullptr);
        if (bitmap) {
            BitBlt(target, area.left, area.top, area.right - area.left, area.bottom - area.top, dc, 0, 0, SRCCOPY);
            SelectObject(dc, previous);
            DeleteObject(bitmap);
        }
        DeleteDC(dc);
    }
};
struct Frame {
    uint32_t *pixels = nullptr;
    int width = 0, height = 0;
    void light(int x, int y, COLORREF color, int amount) const {
        uint32_t &pixel = pixels[size_t(y) * size_t(width) + size_t(x)];
        const auto screen = [amount](uint32_t base, int add) {
            add = add * amount / 255;
            return uint32_t(255 - (255 - int(base)) * (255 - add) / 255);
        };
        pixel = screen(pixel >> 16 & 255, GetRValue(color)) << 16 | screen(pixel >> 8 & 255, GetGValue(color)) << 8 |
                screen(pixel & 255, GetBValue(color));
    }
    static bool inside(RECT r, int cut, int x, int y) {
        return x >= r.left && x < r.right && y >= r.top && y < r.bottom && (x - r.left) + (y - r.top) >= cut &&
               (r.right - 1 - x) + (r.bottom - 1 - y) >= cut;
    }
    void radial(RECT clip, int cx, int cy, int radius, COLORREF color, int strength, int corner = 0) const {
        if (!pixels || radius <= 0)
            return;
        GdiFlush();
        const int left = std::max<int>({0, int(clip.left), cx - radius}),
                  right = std::min<int>({width, int(clip.right), cx + radius});
        const int top = std::max<int>({0, int(clip.top), cy - radius}),
                  bottom = std::min<int>({height, int(clip.bottom), cy + radius});
        for (int y = top; y < bottom; ++y)
            for (int x = left; x < right; ++x) {
                const double d = std::hypot(double(x - cx), double(y - cy)) / radius;
                if (d < 1.0 && (!corner || inside(clip, corner, x, y)))
                    light(x, y, color, int(strength * (1.0 - d) * (1.0 - d)));
            }
    }
    void ball(int cx, int cy, int radius, RECT clip) const {
        if (!pixels || radius < 4)
            return;
        GdiFlush();
        const double r = radius;
        double star[10][2];
        for (int i = 0; i < 10; ++i) {
            const double angle = -1.5707963 + i * 0.6283185, length = i % 2 ? 0.42 : 1.0;
            star[i][0] = std::cos(angle) * length;
            star[i][1] = std::sin(angle) * length;
        }
        const auto inStar = [&](double x, double y, double sx, double sy, double size) {
            x = (x - sx) / size;
            y = (y - sy) / size;
            bool in = false;
            for (int i = 0, j = 9; i < 10; j = i++)
                if ((star[i][1] > y) != (star[j][1] > y) &&
                    x < (star[j][0] - star[i][0]) * (y - star[i][1]) / (star[j][1] - star[i][1]) + star[i][0])
                    in = !in;
            return in;
        };
        const double lx = -0.42, ly = -0.55, lz = 0.72;
        for (int y = std::max<int>({0, int(clip.top), cy - radius - 1}); y < std::min<int>({height, int(clip.bottom), cy + radius + 1}); ++y)
            for (int x = std::max<int>({0, int(clip.left), cx - radius - 1}); x < std::min<int>({width, int(clip.right), cx + radius + 1}); ++x) {
                double red = 0, green = 0, blue = 0, cover = 0;
                for (int s = 0; s < 4; ++s) {
                    const double nx = (x + 0.25 + 0.5 * (s & 1) - cx) / r, ny = (y + 0.25 + 0.5 * (s >> 1) - cy) / r;
                    const double d2 = nx * nx + ny * ny;
                    if (d2 > 1.0)
                        continue;
                    const double nz = std::sqrt(1.0 - d2);
                    const double diffuse = std::max(0.0, nx * lx + ny * ly + nz * lz);
                    const double through = std::max(0.0, nx * 0.45 + ny * 0.5 + nz * 0.25);
                    double cr = 150 + 105 * diffuse + 40 * through, cg = 50 + 95 * diffuse + 60 * through,
                           cb = 6 + 20 * diffuse + 20 * through;
                    if (inStar(nx, ny, -0.2, 0.02, 0.28) || inStar(nx, ny, 0.25, 0.26, 0.28)) {
                        cr = 170 + 50 * diffuse;
                        cg = 24 + 16 * diffuse;
                        cb = 18 + 10 * diffuse;
                    }
                    const double rim = 1.0 - 0.35 * std::pow(d2, 3);
                    const double hx = nx + 0.5, hy = ny + 0.52;
                    const double shine = std::exp(-(hx * hx + hy * hy) / 0.02) * 0.8;
                    red += std::min(255.0, cr * rim + (255 - cr * rim) * shine);
                    green += std::min(255.0, cg * rim + (255 - cg * rim) * shine);
                    blue += std::min(255.0, cb * rim + (255 - cb * rim) * shine);
                    cover += 1;
                }
                if (!cover)
                    continue;
                uint32_t &pixel = pixels[size_t(y) * size_t(width) + size_t(x)];
                const double a = cover / 4;
                const auto blend = [&](int shift, double value) {
                    return uint32_t(std::lround((pixel >> shift & 255) * (1 - a) + value / cover * a)) << shift;
                };
                pixel = blend(16, red) | blend(8, green) | blend(0, blue);
            }
    }
    void rays(RECT clip, int corner, int cx, int cy, int inner, int outer, COLORREF color, int strength) const {
        if (!pixels || outer <= inner)
            return;
        GdiFlush();
        for (int y = std::max<int>(0, clip.top); y < std::min<int>(height, clip.bottom); ++y)
            for (int x = std::max<int>(0, clip.left); x < std::min<int>(width, clip.right); ++x) {
                const double d = std::hypot(double(x - cx), double(y - cy));
                if (d < inner || d > outer || !inside(clip, corner, x, y))
                    continue;
                const double a = std::atan2(double(y - cy), double(x - cx));
                const double streak = std::pow(0.5 + 0.5 * std::cos(a * 36), 6) * (0.55 + 0.45 * std::cos(a * 7 + 1.3));
                const double fade = (d - inner) / (outer - inner);
                light(x, y, color, int(strength * streak * (1.0 - fade) * std::min(1.0, fade * 4)));
            }
    }
    void halo(RECT r, int cut, int radius, COLORREF color, int strength) const {
        if (!pixels || radius <= 0)
            return;
        GdiFlush();
        for (int y = std::max<int>(0, r.top - radius); y < std::min<int>(height, r.bottom + radius); ++y)
            for (int x = std::max<int>(0, r.left - radius); x < std::min<int>(width, r.right + radius); ++x) {
                const double px = x + 0.5, py = y + 0.5;
                const double qx = std::max({0.0, r.left - px, px - r.right}), qy = std::max({0.0, r.top - py, py - r.bottom});
                const double d = std::max({std::hypot(qx, qy), (cut - (px - r.left) - (py - r.top)) / 1.4142,
                                           (cut - (r.right - px) - (r.bottom - py)) / 1.4142});
                if (d > 0 && d < radius)
                    light(x, y, color, int(strength * (1.0 - d / radius) * (1.0 - d / radius)));
            }
    }
};
struct Fonts {
    HFONT body{}, caption{}, label{}, heading{}, brand{}, strong{}, wordmark{}, title{}, mark{};
    void clear() {
        for (auto f : {body, caption, label, heading, brand, strong, wordmark, title, mark})
            if (f)
                DeleteObject(f);
        body = caption = label = heading = brand = strong = wordmark = title = mark = nullptr;
    }
    void make(unsigned dpi, const wchar_t *lettering) {
        clear();
        auto font = [&](int size, int weight, const wchar_t *face = L"Segoe UI") {
            return CreateFontW(-MulDiv(size, int(dpi), 96), 0, 0, 0, weight, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               CLEARTYPE_QUALITY, DEFAULT_PITCH, face);
        };
        body = font(14, FW_NORMAL);
        caption = font(12, FW_NORMAL);
        label = font(11, FW_BOLD);
        heading = font(26, FW_SEMIBOLD);
        brand = font(17, FW_SEMIBOLD);
        strong = font(14, FW_SEMIBOLD);
        wordmark = font(58, FW_NORMAL, lettering);
        title = font(40, FW_NORMAL, lettering);
        mark = font(25, FW_NORMAL, lettering);
    }
    ~Fonts() { clear(); }
};
struct Progress {
    std::mutex mutex;
    std::wstring text;
    uint64_t done = 0, total = 0;
};
enum class Job {
    verify, launch, backup, importSave, restore, afterExit, checkUpdate, downloadUpdate, unlock,
    deleteSaves, deleteBackups, readGame, readSource, importMod, saveReport
};
bool modJob(Job kind) { return kind >= Job::readGame; }
struct Result {
    Job kind;
    std::optional<DiscMount> disc;
    std::filesystem::path image;
    std::wstring message;
    std::optional<UpdateRelease> release;
    std::optional<StagedUpdate> staged;
    std::shared_ptr<mods::SourceDisc> loaded;
    std::optional<mods::DiscRoster> roster;
    mods::DiscIdentity identity;
    std::vector<std::optional<std::string>> stageProblems;
};
struct Grid {
    int column;
    explicit Grid(int contentWidth) : column((contentWidth - 40 - 32) / 3) {}
    int x(int index) const { return contentLeft + 20 + index * (column + 16); }
    static int label(int top, int row) { return top + 44 + row * 56; }
    static int field(int top, int row) { return label(top, row) + 18; }
};
LRESULT CALLBACK combo_procedure(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK button_procedure(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK edit_procedure(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK save_list_procedure(HWND, UINT, WPARAM, LPARAM);
struct Window {
    HWND hwnd{};
    Fonts fonts;
    const wchar_t *letteringFace = L"Bangers";
    unsigned dpi = 96;
    int page = 0, hotButton = 0;
    struct FrameBuffer {
        HDC dc{};
        HBITMAP bitmap{};
        HGDIOBJ previous{};
        Frame frame;
        void make(int width, int height) {
            if (dc && width == frame.width && height == frame.height)
                return;
            release();
            BITMAPINFO info{};
            info.bmiHeader.biSize = sizeof(info.bmiHeader);
            info.bmiHeader.biWidth = width;
            info.bmiHeader.biHeight = -height;
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = BI_RGB;
            void *bits = nullptr;
            bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
            if (!bitmap)
                throw std::runtime_error("Cannot draw the launcher window");
            dc = CreateCompatibleDC(nullptr);
            previous = SelectObject(dc, bitmap);
            frame = {static_cast<uint32_t *>(bits), width, height};
            surfaces().push_back({dc, frame.pixels, width, height});
        }
        void release() {
            if (dc) {
                auto &list = surfaces();
                list.erase(std::remove_if(list.begin(), list.end(), [this](const Surface &s) { return s.dc == dc; }), list.end());
                SelectObject(dc, previous);
                DeleteDC(dc);
            }
            if (bitmap)
                DeleteObject(bitmap);
            dc = nullptr;
            bitmap = nullptr;
            frame = {};
        }
        ~FrameBuffer() { release(); }
    } buffer;
    std::vector<uint32_t> ambient;
    int paintedPage = -1;
    std::vector<int> paintedGlow;
    std::pair<int, int> paintedSize;
    Settings settings;
    DiscordPresence presence;
    PresenceStatus lastPresenceStatus=PresenceStatus::off;
    std::filesystem::path candidateIso;
    Job activeJob = Job::verify, activeUpdateJob = Job::checkUpdate;
    std::optional<DiscMount> disc;
    std::optional<UpdateRelease> release;
    std::optional<StagedUpdate> staged;
    bool updatePending = true;
    std::wstring updateStatus = L"Checked automatically between game sessions.";
    std::map<int, HWND> controls;
    std::vector<unsigned> rates{30, 60, 90, 120, 144, 165, 240, 0};
    std::vector<std::pair<unsigned, unsigned>> sizes{
        {1280, 720}, {1600, 900}, {1920, 1080}, {2560, 1440}, {3840, 2160}};
    std::vector<std::filesystem::path> backups;
    std::vector<mods::ModInfo> modList;
    std::shared_ptr<mods::SourceDisc> source, game;
    std::optional<mods::DiscRoster> sourceRoster, gameRoster;
    std::optional<std::vector<mods::DiscStage>> sourceStages;
    bool movesView = false, stageView = false;
    std::optional<std::vector<mods::DiscStage>> gameStages;
    std::vector<std::optional<std::string>> sourceStageProblems;
    bool stageImport() const {
        return mods::stage_mods_enabled && stageView && sourceIdentity.game == mods::DragonBallGame::budokai3 &&
               gameStages && game;
    }
    enum class Task { characters, stages, movesets, capsules } importTask = Task::characters;
    mods::DiscIdentity sourceIdentity, gameIdentity;
    bool characterView = false;
    std::vector<size_t> sourceRows;
    std::vector<std::string> savedGames;
    HANDLE child{};
    std::future<Result> task, updateTask;
    std::shared_ptr<Progress> progress = std::make_shared<Progress>();
    std::shared_ptr<Progress> updateProgress = std::make_shared<Progress>();
    bool closing = false, error = false, updateError = false, externalGame = false, picking = false;
    bool installRequested = false;
    std::chrono::steady_clock::time_point lastInput = std::chrono::steady_clock::now();
    std::wstring status = L"Choose your Budokai 2 ISO to get started.";
    int p(int n) const { return MulDiv(n, int(dpi), 96); }
    int unscale(int n) const { return MulDiv(n, 96, int(dpi)); }
    bool busy() const { return task.valid(); }
    bool updating() const { return updateTask.valid(); }
    bool running() const { return child || externalGame; }
    bool dropdownOpen() const {
        for (auto [id, control] : controls)
            if ((id >= Renderer && id <= Audio) || id == UpdateChannel || id == AutomaticUpdates ||
                id == SaveListKind || id == DiscordActivity || id == TargetMod || id == SpotMode)
                if (SendMessageW(control, CB_GETDROPPEDSTATE, 0, 0)) return true;
        return false;
    }
    std::filesystem::path card() const {
        return disc ? disc->root / "mc0" : std::filesystem::path{};
    }
    std::filesystem::path backupPath() const {
        return default_cache_directory().parent_path() / "backups" /
               (disc ? disc->image_sha256 : "unselected");
    }
    int contentWidth() const {
        RECT rect{};
        GetClientRect(hwnd, &rect);
        return unscale(rect.right) - contentLeft - margin;
    }
    std::wstring progressText() const {
        std::lock_guard lock(progress->mutex);
        return progress->text;
    }
    void text(HDC dc, const std::wstring &value, int x, int y, int w, int h, HFONT font,
              COLORREF color = ink, UINT flags = DT_LEFT | DT_TOP | DT_NOPREFIX) {
        auto old = SelectObject(dc, font);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, color);
        RECT r{p(x), p(y), p(x + w), p(y + h)};
        DrawTextW(dc, value.c_str(), int(value.size()), &r, flags);
        SelectObject(dc, old);
    }
    void caps(HDC dc, const std::wstring &value, int x, int y, int w, COLORREF color = muted,
              UINT flags = DT_LEFT) {
        const int spacing = SetTextCharacterExtra(dc, p(1));
        text(dc, value, x, y, w, 16, fonts.label, color, flags | DT_SINGLELINE | DT_NOPREFIX);
        SetTextCharacterExtra(dc, spacing);
    }
    int measure(HDC dc, const std::wstring &value, HFONT font) {
        auto old = SelectObject(dc, font);
        SIZE size{};
        GetTextExtentPoint32W(dc, value.c_str(), int(value.size()), &size);
        SelectObject(dc, old);
        return unscale(size.cx);
    }
    int cut(int radius) const { return std::max(2, p(radius) * 3 / 5); }
    void box(HDC dc, RECT rect, COLORREF color, COLORREF outline = border, int radius = 10) {
        SetDCBrushColor(dc, color);
        SetDCPenColor(dc, outline);
        auto oldBrush = SelectObject(dc, GetStockObject(DC_BRUSH));
        auto oldPen = SelectObject(dc, GetStockObject(DC_PEN));
        if (!cutShape(dc, rect, cut(radius), [color](int, int) { return color; }, outline)) {
            const auto points = cutCorners(rect, cut(radius));
            Polygon(dc, points.data(), int(points.size()));
        }
        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
    }
    void panelBox(HDC dc, int left, int top, int right, int bottom) {
        box(dc, {p(left), p(top), p(right), p(bottom)}, panel, mix(panel, border, 150), 12);
        fill(dc, {p(left) + cut(12) + 1, p(top) + 1, p(right) - 2, p(top) + 2}, mix(panel, ink, 16));
    }
    std::optional<RECT> controlRect(int id) const {
        const auto found = controls.find(id);
        if (found == controls.end() || !(GetWindowLongPtrW(found->second, GWL_STYLE) & WS_VISIBLE))
            return std::nullopt;
        RECT r{};
        GetWindowRect(found->second, &r);
        MapWindowPoints(nullptr, hwnd, reinterpret_cast<POINT *>(&r), 2);
        return r;
    }
    void backdrop(HDC dc, HWND control, const RECT &r, COLORREF fallback) {
        RECT bounds{};
        GetWindowRect(control, &bounds);
        MapWindowPoints(nullptr, hwnd, reinterpret_cast<POINT *>(&bounds), 2);
        if (buffer.dc && bounds.right <= buffer.frame.width && bounds.bottom <= buffer.frame.height)
            BitBlt(dc, r.left, r.top, r.right - r.left, r.bottom - r.top, buffer.dc, bounds.left + r.left,
                   bounds.top + r.top, SRCCOPY);
        else
            fill(dc, r, fallback);
    }
    void emberFill(HDC dc, RECT r, int radius, bool pressed, bool hot) {
        const COLORREF from = pressed ? RGB(236, 120, 22) : hot ? RGB(255, 214, 110) : RGB(255, 186, 70);
        const COLORREF to = pressed ? RGB(214, 96, 14) : hot ? RGB(255, 150, 44) : RGB(250, 126, 22);
        const int height = std::max(1L, r.bottom - r.top);
        if (cutShape(dc, r, cut(radius), [&](int, int y) {
                return y == r.top ? mix(from, RGB(255, 245, 214), 150) : mix(from, to, (y - r.top) * 255 / height);
            }, std::nullopt))
            return;
        auto region = cutRegion(r, cut(radius));
        SelectClipRgn(dc, region);
        const COLORREF top = pressed ? RGB(236, 120, 22) : hot ? RGB(255, 214, 110) : RGB(255, 186, 70);
        const COLORREF bottom = pressed ? RGB(214, 96, 14) : hot ? RGB(255, 150, 44) : RGB(250, 126, 22);
        const int span = std::max(1L, r.bottom - r.top);
        for (int y = r.top; y < r.bottom; ++y)
            fill(dc, {r.left, y, r.right, y + 1}, mix(top, bottom, (y - r.top) * 255 / span));
        fill(dc, {r.left, r.top, r.right, r.top + 1}, mix(top, RGB(255, 245, 214), 150));
        SelectClipRgn(dc, nullptr);
        DeleteObject(region);
    }
    void button(int id, const wchar_t *label) {
        controls[id] =
            CreateWindowExW(0, L"BUTTON", label, WS_CHILD | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 0, 0,
                            hwnd, reinterpret_cast<HMENU>(intptr_t(id)), nullptr, nullptr);
        auto old = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(controls[id], GWLP_WNDPROC,
                    reinterpret_cast<LONG_PTR>(button_procedure)));
        SetPropW(controls[id], L"OpenKai2.ButtonProcedure", reinterpret_cast<HANDLE>(old));
    }
    void combo(int id, const std::vector<std::wstring> &items, int selected) {
        auto w = CreateWindowExW(0, L"COMBOBOX", L"",
                                 WS_CHILD | WS_TABSTOP | CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED |
                                     CBS_HASSTRINGS | CBS_NOINTEGRALHEIGHT | WS_VSCROLL,
                                 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(intptr_t(id)), nullptr,
                                 nullptr);
        controls[id] = w;
        auto old = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrW(w, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(combo_procedure)));
        SetPropW(w, L"OpenKai2.ComboProcedure", reinterpret_cast<HANDLE>(old));
        for (const auto &item : items)
            SendMessageW(w, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item.c_str()));
        SendMessageW(w, CB_SETCURSEL, selected, 0);
    }
    void list(int id, const wchar_t *name, int rowHeight, bool multiple) {
        controls[id] = CreateWindowExW(
            0, L"LISTBOX", name,
            WS_CHILD | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS |
                LBS_NOINTEGRALHEIGHT | (multiple ? LBS_EXTENDEDSEL : 0),
            0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(intptr_t(id)), nullptr, nullptr);
        SendMessageW(controls[id], LB_SETITEMHEIGHT, 0, p(rowHeight));
    }
    void edit(int id, const wchar_t *hint) {
        controls[id] = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd,
                                       reinterpret_cast<HMENU>(intptr_t(id)), nullptr, nullptr);
        auto old = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrW(controls[id], GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(edit_procedure)));
        SetPropW(controls[id], L"OpenKai2.EditProcedure", reinterpret_cast<HANDLE>(old));
        SetPropW(controls[id], L"OpenKai2.EditHint", reinterpret_cast<HANDLE>(const_cast<wchar_t *>(hint)));
    }
    std::wstring textOf(int id) const {
        const int n = GetWindowTextLengthW(controls.at(id));
        std::wstring value(size_t(n) + 1, 0);
        GetWindowTextW(controls.at(id), value.data(), n + 1);
        value.resize(size_t(n));
        return value;
    }
    int selected(int id) const { return int(SendMessageW(controls.at(id), CB_GETCURSEL, 0, 0)); }
    void place(int id, int x, int y, int w, int h, bool visible = true) {
        auto c = controls.at(id);
        wchar_t type[32]{};
        GetClassNameW(c, type, 32);
        int allocatedHeight = p(h);
        if (lstrcmpiW(type, L"ComboBox") == 0) {
            const auto rows = std::min<LRESULT>(8, SendMessageW(c, CB_GETCOUNT, 0, 0));
            allocatedHeight = std::max(allocatedHeight, p(36)) + int(rows) * p(34) + p(8);
            SendMessageW(c, CB_SETITEMHEIGHT, WPARAM(-1), p(h - 6));
            SendMessageW(c, CB_SETITEMHEIGHT, 0, p(34));
        }
        SetWindowPos(c, nullptr, p(x), p(y), p(w), allocatedHeight,
                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
        const bool shown = (GetWindowLongPtrW(c, GWL_STYLE) & WS_VISIBLE) != 0;
        if (visible != shown)
            ShowWindow(c, visible ? SW_SHOWNA : SW_HIDE);
    }
    void initialize() {
        presence.enable(settings.discordPresence);
        fonts.make(dpi, letteringFace);
        for (auto [id, label] :
             std::vector<std::pair<int, const wchar_t *>>{{PlayTab, L"Play"},
                                                          {SavesTab, L"Saves"},
                                                          {ModsTab, L"Mods"},
                                                          {ModToggle, L"Enable / disable"},
                                                          {ModUp, L"Move up"},
                                                          {ModDown, L"Move down"},
                                                          {ModRefresh, L"Refresh"},
                                                          {ModFolder, L"Open mods folder"},
                                                          {ModCreate, L"Create mod"},
                                                          {OpenImporter, L"Import tool…"},
                                                          {ModRename, L"Rename"},
                                                          {ModDelete, L"Delete mod"},
                                                          {ModRemove, L"Remove"},
                                                          {SourceGame, L"Game disc"},
                                                          {SourceIso, L"Choose ISO…"},
                                                          {SourceLast, L"Last disc"},
                                                          {ImportSelected, L"Import"},
                                                          {TaskCharacters, L"Characters"},
                                                          {TaskStages, L"Stages"},
                                                          {TaskMovesets, L"Movesets"},
                                                          {TaskCapsules, L"Capsules"},
                                                          {SaveReport, L"Disc report…"},
                                                          {CloseImporter, L"Back to mods"},
                                                          {SettingsTab, L"Settings"},
                                                          {CheckUpdates, L"Check now"},
                                                          {InstallUpdate, L"Install"},
                                                          {ReleasePage, L"Release notes"},
                                                          {ResetGraphics, L"Restore defaults"},
                                                          {ClearImage, L"Crisp preset"},
                                                          {ChooseIso, L"Choose ISO…"},
                                                          {VerifyIso, L"Verify"},
                                                          {Play, L"Play"},
                                                          {ImportFolder, L"Import folder"},
                                                          {ImportFile, L"Import file…"},
                                                          {Backup, L"Back up now"},
                                                          {UnlockAll,L"Unlock all"},
                                                          {DeleteSelected,L"Delete selected"},
                                                          {SelectSaves,L"Select all"},
                                                          {Restore, L"Restore backup"},
                                                          {OpenSaves, L"Open saves"},
                                                          {OpenBackups, L"Open folder"}})
            button(id, label);
        if (std::find(rates.begin(), rates.end(), settings.fps) == rates.end())
            rates.push_back(settings.fps);
        std::vector<std::wstring> fps;
        for (auto r : rates)
            fps.push_back(r ? std::to_wstring(r) + L" FPS" : L"Uncapped");
        const auto size = std::make_pair(settings.width, settings.height);
        if (std::find(sizes.begin(), sizes.end(), size) == sizes.end())
            sizes.push_back(size);
        std::vector<std::wstring> resolutions;
        for (auto [w, h] : sizes)
            resolutions.push_back(std::to_wstring(w) + L" × " + std::to_wstring(h));
        combo(Renderer, {L"Automatic", L"OpenGL (GPU)", L"Software (CPU)"},
              settings.renderer == "gpu"   ? 1
              : settings.renderer == "cpu" ? 2
                                           : 0);
        combo(Fps, fps, int(std::find(rates.begin(), rates.end(), settings.fps) - rates.begin()));
        combo(Resolution, resolutions,
              int(std::find(sizes.begin(), sizes.end(), size) - sizes.begin()));
        combo(WindowMode, {L"Windowed", L"Borderless fullscreen"}, int(settings.windowMode));
        combo(Antialiasing, {L"Off", L"FXAA"}, int(settings.antialiasing));
        combo(Filtering, {L"Game default", L"Nearest (sharp)", L"Bilinear (3D surfaces)"},
              int(settings.textureFilter));
        combo(Upscaler, {L"Nearest", L"Bilinear", L"Bicubic", L"AMD FSR 1"}, int(settings.upscaler));
        combo(Vsync, {L"Off", L"On"}, settings.vsync ? 1 : 0);
        combo(AutoBackup, {L"Off", L"On"}, settings.autoBackups ? 1 : 0);
        combo(DiscordActivity,{L"Off",L"On"},settings.discordPresence ? 1 : 0);
        combo(SaveListKind,{L"Backups",L"Game saves"},0);
        combo(Aspect, {L"4:3 (original)", L"16:9 (widescreen)", L"Stretch to window", L"Native pixels"},
              int(settings.aspect));
        combo(Audio, {L"Off", L"100% volume", L"75% volume", L"50% volume", L"25% volume"},
              audioChoice(settings));
        combo(UpdateChannel, {L"Stable", L"Stable and prerelease"}, int(settings.updateChannel));
        combo(AutomaticUpdates, {L"Off", L"On"}, settings.autoUpdates ? 1 : 0);
        combo(TargetMod, {}, 0);
        combo(SpotMode, {L"Share a spot (R2 switches)", L"Add a new spot"}, 0);
        controls[BackupList] = CreateWindowExW(
            0, L"LISTBOX", L"Save backups",
            WS_CHILD | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_EXTENDEDSEL,
            0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(intptr_t(BackupList)), nullptr, nullptr);
        SendMessageW(controls[BackupList], LB_SETITEMHEIGHT, 0, p(40));
        auto originalList = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(controls[BackupList], GWLP_WNDPROC,
            reinterpret_cast<LONG_PTR>(save_list_procedure)));
        SetPropW(controls[BackupList], L"OpenKai2.SaveListProcedure", reinterpret_cast<HANDLE>(originalList));
        list(ModList, L"Mods", 40, false);
        list(ModContents, L"Mod contents", 30, false);
        list(SourceFiles, L"Source files", 30, false);
        list(TargetList, L"Import target", 30, false);
        edit(ModName, L"New mod name");
        edit(EntryFilter, L"Search characters");
        for (auto [id, c] : controls)
            SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(fonts.body), TRUE);
        SetTimer(hwnd, 1, 75, nullptr);
        layout();
    }
    bool gameSaveList() const { return selected(SaveListKind) == 1; }
    bool emptySaveList() const { return gameSaveList() ? savedGames.empty() : backups.empty(); }
    std::vector<int> selectedSaveRows() const {
        const auto list = controls.at(BackupList);
        const auto count = SendMessageW(list, LB_GETSELCOUNT, 0, 0);
        if (count <= 0) return {};
        std::vector<int> rows(size_t(count), 0);
        if (SendMessageW(list, LB_GETSELITEMS, WPARAM(count), LPARAM(rows.data())) != count) return {};
        return rows;
    }
    void refreshEnabled() {
        const bool active = disc.has_value() && !busy() && !running();
        for (int id : {ChooseIso, VerifyIso})
            EnableWindow(controls[id], !busy() && !running() &&
                         (id != VerifyIso || !candidateIso.empty()));
        for (int id : {Play, ImportFolder, ImportFile, Backup, OpenSaves, OpenBackups, UnlockAll})
            EnableWindow(controls[id], active);
        const auto selections = selectedSaveRows().size();
        EnableWindow(controls[Restore], active && !gameSaveList() && selections == 1u);
        EnableWindow(controls[DeleteSelected], active && selections != 0u);
        const auto deleteLabel = selections ? L"Delete (" + std::to_wstring(selections) + L")" : L"Delete selected";
        SetWindowTextW(controls[DeleteSelected], deleteLabel.c_str());
        EnableWindow(controls[SaveListKind], !busy());
        EnableWindow(controls[SelectSaves], !busy() && !emptySaveList());
        const auto rowCount = SendMessageW(controls[BackupList], LB_GETCOUNT, 0, 0);
        SetWindowTextW(controls[SelectSaves], rowCount > 0 && selections == size_t(rowCount) ? L"Clear all" : L"Select all");
        EnableWindow(controls[BackupList], !busy() && !child);
        const bool modding = !busy() && !running();
        const auto chosenMod = SendMessageW(controls[ModList], LB_GETCURSEL, 0, 0);
        const bool modChosen = chosenMod >= 0 && size_t(chosenMod) < modList.size();
        for (int id : {ModRename, ModDelete})
            EnableWindow(controls[id], modding && modChosen);
        const auto contentRow = SendMessageW(controls[ModContents], LB_GETCURSEL, 0, 0);
        EnableWindow(controls[ModRemove], modding && modChosen && contentRow >= 0 && size_t(contentRow) < contentSlots.size() &&
                                              (contentSlots[size_t(contentRow)] || contentStages[size_t(contentRow)]));
        EnableWindow(controls[ModContents], !busy());
        for (int id : {ModToggle, ModUp, ModDown})
            EnableWindow(controls[id], modding && chosenMod >= 0 && size_t(chosenMod) < modList.size() &&
                                           (id != ModUp || chosenMod > 0) &&
                                           (id != ModDown || size_t(chosenMod) + 1 < modList.size()));
        for (int id : {ModCreate, OpenImporter, SourceGame, SourceIso})
            EnableWindow(controls[id], modding && (id != SourceGame || !settings.iso.empty()));
        std::error_code missing;
        EnableWindow(controls[SourceLast], modding && !settings.importIso.empty() &&
                                               std::filesystem::is_regular_file(settings.importIso, missing) &&
                                               (!source || source->image() != settings.importIso));
        for (int id : {SourceFiles, EntryFilter, TargetList, TargetMod, SpotMode, TaskCharacters, TaskStages, TaskMovesets,
                       TaskCapsules})
            EnableWindow(controls[id], !busy());
        EnableWindow(controls[SaveReport], !busy() && source);
        const auto spot = SendMessageW(controls[TargetList], LB_GETCURSEL, 0, 0);
        const bool picked = (characterView && sourceCharacter() && spot >= 0 && gameRoster && game && fits(size_t(spot))) ||
                            (stageImport() && spot >= 0 && SendMessageW(controls[SourceFiles], LB_GETCURSEL, 0, 0) >= 0 &&
                             !stageProblem(size_t(SendMessageW(controls[SourceFiles], LB_GETCURSEL, 0, 0))));
        EnableWindow(controls[ImportSelected], modding && picked);
        EnableWindow(controls[Resolution], settings.windowMode == 0);
        for (int id : {UpdateChannel, AutomaticUpdates, CheckUpdates, InstallUpdate})
            EnableWindow(controls[id], !updating() &&
                         (id != InstallUpdate || (!busy() && !running() &&
                          (release.has_value() || staged.has_value()))));
        InvalidateRect(hwnd, nullptr, FALSE);
    }
    void layout() {
        RECT rect{};
        GetClientRect(hwnd, &rect);
        const int w = unscale(rect.right), h = unscale(rect.bottom);
        const int content = w - contentLeft - margin, right = contentLeft + content;
        std::vector<int> order{PlayTab, SavesTab, ModsTab, SettingsTab};
        auto position = [&](int id, int x, int y, int width, int height) {
            place(id, x, y, width, height);
            order.push_back(id);
        };
        for (int i = 0; i < pageCount; ++i)
            place(PlayTab + i, 12, 104 + i * 58, sidebarWidth - 24, 50);
        position(DiscordActivity, 24, h - 130, sidebarWidth - 48, 32);
        if (page == 0) {
            position(Play, contentLeft + 28, heroBottom - 70, 168, 46);
            position(VerifyIso, right - 20 - 96, discTop + 26, 96, 36);
            position(ChooseIso, right - 20 - 96 - 12 - 132, discTop + 26, 132, 36);
        } else if (page == 1) {
            const int panelBottom = h - 64;
            position(AutoBackup, right - 20 - 160, 142, 160, 34);
            const int gap = 12, action = (content - 40 - 3 * gap) / 4;
            int x = contentLeft + 20;
            for (int id : {ImportFile, ImportFolder, Backup, UnlockAll}) {
                position(id, x, 246, action, 36);
                x += action + gap;
            }
            position(BackupList, contentLeft + 20, 382, content - 40,
                     std::max(60, panelBottom - 84 - 382));
            if (emptySaveList())
                ShowWindow(controls[BackupList], SW_HIDE);
            position(SaveListKind, right - 180, 342, 160, 32);
            if (!gameSaveList())
                position(Restore, contentLeft + 20, panelBottom - 52, 130, 36);
            position(OpenBackups, contentLeft + (gameSaveList() ? 20 : 162), panelBottom - 52, 120, 36);
            position(SelectSaves, right - 292, panelBottom - 52, 100, 36);
            position(DeleteSelected, right - 180, panelBottom - 52, 160, 36);
        } else if (page == modsPage) {
            const int panelBottom = h - 64;
            position(OpenImporter, right - 20 - 140, 142, 140, 36);
            position(ModFolder, right - 20 - 140 - 12 - 160, 142, 160, 36);
            const int half = (content - 52) / 2, contentsLeft = contentLeft + 32 + half;
            position(ModList, contentLeft + 20, 252, half, std::max(80, panelBottom - 64 - 252));
            position(ModContents, contentsLeft, 252, half, std::max(60, panelBottom - 112 - 252));
            const int third = (half - 20) / 3;
            position(ModRename, contentsLeft, panelBottom - 100, third, 36);
            position(ModDelete, contentsLeft + third + 10, panelBottom - 100, third, 36);
            position(ModRemove, contentsLeft + 2 * (third + 10), panelBottom - 100, half - 2 * (third + 10), 36);
            if (modList.empty())
                for (int id : {ModList, ModContents, ModRename, ModDelete, ModRemove})
                    ShowWindow(controls[id], SW_HIDE);
            int x = contentLeft + 20;
            for (auto [id, width] : {std::pair{ModToggle, 150}, {ModUp, 100}, {ModDown, 110}, {ModRefresh, 90}}) {
                position(id, x, panelBottom - 52, width, 36);
                x += width + 10;
            }
            const int name = std::max(120, right - 20 - 120 - 10 - (x + 6));
            position(ModName, right - 20 - 120 - 10 - name + 10, panelBottom - 44, name - 20, 22);
            position(ModCreate, right - 20 - 120, panelBottom - 52, 120, 36);
        } else if (page == importPage) {
            const int listsBottom = h - 210, targetTop = listsBottom + 12;
            int x = contentLeft + 20;
            for (int id : {TaskCharacters, TaskStages, TaskMovesets, TaskCapsules}) {
                position(id, x, 152, 120, 34);
                x += 130;
            }
            position(CloseImporter, right - 20 - 130, 152, 130, 34);
            position(SourceIso, right - 20 - 130, 214, 130, 34);
            position(SourceLast, right - 20 - 130 - 10 - 110, 214, 110, 34);
            position(SourceGame, right - 20 - 130 - 10 - 110 - 10 - 116, 214, 116, 34);
            position(SaveReport, right - 20 - 130 - 10 - 110 - 10 - 116 - 10 - 130, 214, 130, 34);
            if (characterView) {
                const int listTop = 304, listHeight = listsBottom - 14 - listTop, half = (content - 52) / 2;
                position(EntryFilter, contentLeft + 20 + half - 190, 276, 180, 20);
                position(SourceFiles, contentLeft + 20, listTop, half, listHeight);
                position(TargetList, contentLeft + 32 + half, listTop, content - 52 - half, listHeight);
                position(TargetMod, contentLeft + 20, targetTop + 52, 220, 34);
                position(SpotMode, contentLeft + 252, targetTop + 52, 260, 34);
                position(ImportSelected, right - 20 - 160, targetTop + 52, 160, 36);
            } else if (stageView) {
                const int listTop = 304, listHeight = listsBottom - 14 - listTop, half = (content - 52) / 2;
                position(SourceFiles, contentLeft + 20, listTop, half, listHeight);
                if (stageImport()) {
                    position(TargetList, contentLeft + 32 + half, listTop, content - 52 - half, listHeight);
                    position(TargetMod, contentLeft + 20, targetTop + 52, 220, 34);
                    position(ImportSelected, right - 20 - 160, targetTop + 52, 160, 36);
                }
            }
        } else {
            const Grid grid(content);
            const int half = (grid.column - 10) / 2;
            position(WindowMode, grid.x(0), Grid::field(displayTop, 0), grid.column, 34);
            position(Resolution, grid.x(1), Grid::field(displayTop, 0), grid.column, 34);
            position(Aspect, grid.x(2), Grid::field(displayTop, 0), grid.column, 34);
            position(Fps, grid.x(0), Grid::field(displayTop, 1), grid.column, 34);
            position(Vsync, grid.x(1), Grid::field(displayTop, 1), grid.column, 34);
            position(Audio, grid.x(2), Grid::field(displayTop, 1), grid.column, 34);
            position(Renderer, grid.x(0), Grid::field(graphicsTop, 0), grid.column, 34);
            position(Antialiasing, grid.x(1), Grid::field(graphicsTop, 0), grid.column, 34);
            position(Filtering, grid.x(2), Grid::field(graphicsTop, 0), grid.column, 34);
            position(Upscaler, grid.x(0), Grid::field(graphicsTop, 1), grid.column, 34);
            position(ClearImage, grid.x(1), Grid::field(graphicsTop, 1), grid.column, 34);
            position(ResetGraphics, grid.x(2), Grid::field(graphicsTop, 1), grid.column, 34);
            position(UpdateChannel, grid.x(0), Grid::field(updatesTop, 0), grid.column, 34);
            position(AutomaticUpdates, grid.x(1), Grid::field(updatesTop, 0), grid.column, 34);
            position(CheckUpdates, grid.x(2), Grid::field(updatesTop, 0), half, 34);
            position(InstallUpdate, grid.x(2) + half + 10, Grid::field(updatesTop, 0),
                     grid.column - half - 10, 34);
            position(ReleasePage, right - 20 - 116, updatesTop + 10, 116, 28);
        }
        HWND previous = HWND_TOP;
        for (auto id : order) {
            auto control = controls.at(id);
            SetWindowPos(control, previous, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            previous = control;
        }
        for (auto [id, control] : controls)
            if (std::find(order.begin(), order.end(), id) == order.end() &&
                (GetWindowLongPtrW(control, GWL_STYLE) & WS_VISIBLE))
                ShowWindow(control, SW_HIDE);
        refreshEnabled();
    }
    COLORREF stateColor() const {
        return error ? red : running() ? gold : busy() ? gold : disc ? green : muted;
    }
    std::wstring stateLabel() const {
        if (error) return L"Needs attention";
        if (running()) return L"Game running";
        if (busy()) return activeJob == Job::verify || activeJob == Job::launch ? L"Checking disc" : L"Working";
        return disc ? L"Ready" : L"No disc";
    }
    void paintSidebar(HDC dc, int h) {
        fill(dc, {p(sidebarWidth) - 1, 0, p(sidebarWidth), p(h)}, mix(sidebar, border, 170));
        buffer.frame.radial({0, 0, p(sidebarWidth) - 1, p(h)}, p(38), p(48), p(40), ember, 90);
        buffer.frame.ball(p(38), p(48), p(15), {0, 0, p(sidebarWidth), p(h)});
        lettering(dc, L"OpenKai2", p(64), p(46), fonts.mark, ink, RGB(6, 7, 12), p(1));
        text(dc, L"Budokai 2 for PC", 64, 52, 150, 18, fonts.caption, muted);
        const int chosen = page == importPage ? modsPage : page;
        const RECT row{p(12), p(104 + chosen * 58), p(sidebarWidth - 12), p(154 + chosen * 58)};
        const int span = std::max(1L, row.right - row.left);
        cutShape(dc, row, cut(12), [&](int x, int) {
            return mix(mix(sidebar, orange, 58), mix(sidebar, orange, 14), (x - row.left) * 255 / span);
        }, mix(sidebar, orange, 110));
        const RECT accent{row.left + p(6), row.top + p(15), row.left + p(9), row.bottom - p(15)};
        fill(dc, accent, orange);
        buffer.frame.halo(accent, 0, p(10), ember, 110);

        const int top = h - 162;
        line(dc, p(24), p(top - 10), p(sidebarWidth - 48), border);
        caps(dc, L"DISCORD ACTIVITY", 24, top + 6, sidebarWidth - 48);
        const auto state = presence.status();
        text(dc, !settings.discordPresence ? L"Activity sharing is off"
                  : state == PresenceStatus::connected ? L"Connected to Discord"
                  : L"Discord isn't open", 24, top + 74, sidebarWidth - 48, 18,
             fonts.caption, state == PresenceStatus::connected ? green : muted,
             DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        text(dc, L"v" + wide(application_version), 24, h - 38, sidebarWidth - 48, 18, fonts.caption, faint);
    }
    void paintHeader(HDC dc, int w, int content) {
        const wchar_t *titles[] = {L"Play", L"Saves", L"Mods", L"Settings", L"Import tool"};
        const wchar_t *subtitles[] = {
            L"",
            L"Import, back up and restore your memory card.",
            L"Characters, movesets and stages layered over your disc. Your ISO is never changed.",
            L"Saved automatically. Game settings apply at the next launch.",
            L"Add characters, stages and movesets from other Dragon Ball Z games. Nothing in Budokai 2 is removed."};
        lettering(dc, titles[page], p(contentLeft), p(60), fonts.title, ink, RGB(6, 7, 12), p(2));
        text(dc, subtitles[page], contentLeft, 72, content, 20, fonts.caption, muted,
             DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        const auto label = stateLabel();
        const int labelWidth = measure(dc, label, fonts.caption), left = w - margin - labelWidth;
        ellipse(dc, p(left - 12), p(47), p(3), stateColor());
        buffer.frame.halo({p(left - 12), p(47), p(left - 11), p(48)}, 0, p(9), stateColor(), 90);
        text(dc, label, left, 38, labelWidth + 4, 20, fonts.caption, muted, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
        rule(dc, contentLeft, 104, content);
    }
    void rule(HDC dc, int x, int y, int width) {
        const int steps = 24;
        for (int i = 0; i < steps; ++i)
            fill(dc, {p(x + width * i / steps), p(y), p(x + width * (i + 1) / steps), p(y) + 1},
                 mix(mix(border, orange, 60), background, i * 255 / steps));
    }
    int keycap(HDC dc, int x, int y, const std::wstring &key, const std::wstring &action) {
        const int width = std::max(24, measure(dc, key, fonts.strong) + 16);
        box(dc, {p(x), p(y), p(x + width), p(y + 26)}, raised, hover, 8);
        fill(dc, {p(x + 3), p(y + 24), p(x + width - 3), p(y + 25)}, border);
        text(dc, key, x, y, width, 25, fonts.strong, ink, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        const int actionWidth = measure(dc, action, fonts.caption);
        text(dc, action, x + width + 8, y, actionWidth + 4, 26, fonts.caption, muted,
             DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        return x + width + 8 + actionWidth + 22;
    }
    void paintPlay(HDC dc, int content) {
        const int right = contentLeft + content;
        const RECT hero{p(contentLeft), p(heroTop), p(right), p(heroBottom)};
        const int span = std::max(1L, hero.right - hero.left);
        cutShape(dc, hero, p(18), [&](int x, int y) {
            return y == hero.top + 1 && x > hero.left + p(18) ? mix(heroFrom, gold, 60)
                                                             : mix(heroFrom, heroTo, (x - hero.left) * 255 / span);
        }, std::nullopt);
        const int cx = right - 112, cy = heroTop + 96;
        buffer.frame.rays(hero, p(18), p(cx), p(cy), p(50), p(300), gold, 46);
        buffer.frame.radial(hero, p(cx), p(cy), p(190), ember, 110, p(18));
        buffer.frame.radial(hero, p(contentLeft), p(heroTop), p(260), ember, 34, p(18));
        if (content > 620)
            buffer.frame.ball(p(cx), p(cy), p(58), hero);

        caps(dc, L"DRAGON BALL Z", contentLeft + 30, heroTop + 22, 300, gold);
        lettering(dc, L"Budokai 2", p(contentLeft + 28), p(heroTop + 82), fonts.wordmark, RGB(255, 240, 222),
                  RGB(28, 12, 4), p(3));
        int spec = contentLeft + 30;
        for (const wchar_t *part : {L"PLAYSTATION 2", L"NTSC-U", L"SLUS-20779"}) {
            caps(dc, part, spec, heroTop + 93, 160, RGB(206, 176, 150));
            spec += measure(dc, part, fonts.label) + int(std::wcslen(part)) + 22;
        }

        std::wstring headline, detail;
        if (running()) {
            headline = L"Game running";
            detail = L"Close the game to change your disc or saves.";
        } else if (busy()) {
            headline = L"Checking your disc";
            detail = progressText();
        } else if (error && !disc) {
            headline = L"This disc can't be used";
            detail = L"Details are shown at the bottom of the window.";
        } else if (disc) {
            headline = L"Ready to play";
            detail.clear();
        } else {
            headline = L"No disc selected";
            detail = L"Choose your USA ISO below to get started.";
        }
        text(dc, headline, contentLeft + 214, heroBottom - (detail.empty() ? 59 : 69), content - 400, 22, fonts.strong);
        text(dc, detail, contentLeft + 214, heroBottom - 47, content - 400, 20, fonts.caption,
             RGB(214, 196, 180), DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

        panelBox(dc, contentLeft, discTop, right, discBottom);
        caps(dc, L"GAME DISC", contentLeft + 20, discTop + 16, 200);
        const int nameWidth = content - 40 - 96 - 12 - 132 - 20;
        text(dc, candidateIso.empty() ? L"No ISO selected" : candidateIso.filename().wstring(),
             contentLeft + 20, discTop + 34, nameWidth, 24, fonts.strong, ink,
             DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        text(dc, candidateIso.empty() ? L"Budokai 2 USA (SLUS-20779). Your choice is remembered."
                                      : std::filesystem::path(candidateIso).make_preferred().parent_path().wstring(),
             contentLeft + 20, discTop + 60, nameWidth, 18, fonts.caption, muted,
             DT_SINGLELINE | DT_PATH_ELLIPSIS | DT_NOPREFIX);

        const int tile = (content - 24) / 3;
        const wchar_t *scaling[] = {L"Nearest", L"Bilinear", L"Bicubic", L"FSR 1"};
        const wchar_t *aspects[] = {L"4:3", L"16:9", L"Stretched", L"Native pixels"};
        const std::wstring captions[] = {L"VIDEO", L"DISPLAY", L"SAVES"};
        const std::wstring values[] = {
            settings.renderer == "cpu" ? L"Software" : settings.renderer == "gpu" ? L"OpenGL" : L"Automatic",
            settings.windowMode ? L"Borderless" : std::to_wstring(settings.width) + L" × " + std::to_wstring(settings.height),
            settings.autoBackups ? L"Auto backup" : L"Manual backup"};
        const std::wstring details[] = {
            std::wstring(scaling[std::min(3u, settings.upscaler)]) + L" upscaling" +
                (settings.antialiasing ? L", FXAA" : L""),
            std::wstring(aspects[std::min(3u, settings.aspect)]) + L", " +
                (settings.fps ? std::to_wstring(settings.fps) + L" FPS cap" : L"uncapped") +
                (settings.vsync ? L", VSync" : L""),
            disc ? std::to_wstring(backups.size()) + (backups.size() == 1 ? L" backup stored" : L" backups stored")
                 : L"Available once your disc is verified"};
        for (int i = 0; i < 3; ++i) {
            const int x = contentLeft + i * (tile + 12);
            panelBox(dc, x, tilesTop, x + tile, tilesBottom);
            caps(dc, captions[i], x + 16, tilesTop + 16, tile - 32);
            text(dc, values[i], x + 16, tilesTop + 36, tile - 32, 22, fonts.brand, ink,
                 DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            text(dc, details[i], x + 16, tilesTop + 58, tile - 32, 16, fonts.caption, muted,
                 DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        }
        caps(dc, L"KEYBOARD", contentLeft, keysTop + 6, 90);
        int x = contentLeft + 84;
        for (auto [key, action] : std::vector<std::pair<std::wstring, std::wstring>>{
                 {L"WASD", L"Move"}, {L"X", L"Confirm"}, {L"C", L"Back"}, {L"Enter", L"Start"}})
            if (x < right - 60)
                x = keycap(dc, x, keysTop, key, action);
    }
    void paintSaves(HDC dc, int h, int content) {
        const int right = contentLeft + content, panelBottom = h - 64;
        panelBox(dc, contentLeft, 124, right, 194);
        text(dc, L"Automatic backups", contentLeft + 20, 138, content - 220, 22, fonts.strong);
        text(dc, L"Copies your memory card before and after every game session.", contentLeft + 20, 160,
             content - 220, 18, fonts.caption, muted, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

        panelBox(dc, contentLeft, 206, right, 322);
        text(dc, L"Save tools", contentLeft + 20, 218, content - 40, 22, fonts.strong);
        text(dc, L"Manage your memory card and unlocks. Every change is backed up first.",
             contentLeft + 20, 290, content - 40, 18, fonts.caption, muted,
             DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

        panelBox(dc, contentLeft, 334, right, panelBottom);
        text(dc, gameSaveList() ? L"Memory card" : L"Backup history", contentLeft + 20, 348, content - 390, 22, fonts.strong);
        if (disc)
            text(dc, std::to_wstring(gameSaveList() ? savedGames.size() : backups.size()) +
                 (gameSaveList() ? L" saves" : L" backups"),
                 right - 340, 350, 140, 20, fonts.caption, muted, DT_RIGHT | DT_SINGLELINE | DT_NOPREFIX);
        if (!emptySaveList())
            text(dc, gameSaveList() ? L"Select saves to delete. A recovery backup is created first."
                                   : L"Select one to restore, or several to delete. Ctrl / Shift selects more.",
                 contentLeft + 20, panelBottom - 78, content - 40, 18, fonts.caption, muted,
                 DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        if(emptySaveList()) {
            const int bottom=std::max(430,panelBottom-84),middle=(382+bottom)/2;
            box(dc,{p(contentLeft+20),p(382),p(right-20),p(bottom)},field,border,8);
            text(dc,disc ? (gameSaveList() ? L"No game saves yet" : L"No backups yet") : L"Choose your game disc",contentLeft+36,middle-28,
                 content-72,24,fonts.strong,ink,DT_CENTER|DT_SINGLELINE|DT_NOPREFIX);
            text(dc,disc ? (gameSaveList() ? L"Create a save in the game, or import an existing save above." : L"Back up your save before importing or unlocking content.")
                         : L"Verify your ISO on the Play page to manage saves.",contentLeft+36,middle+4,
                 content-72,38,fonts.caption,muted,DT_CENTER|DT_WORDBREAK|DT_NOPREFIX);
        }
    }
    void editFrame(HDC dc, int id) {
        auto control = controls.at(id);
        if (!(GetWindowLongPtrW(control, GWL_STYLE) & WS_VISIBLE))
            return;
        RECT r{};
        GetWindowRect(control, &r);
        MapWindowPoints(nullptr, hwnd, reinterpret_cast<POINT *>(&r), 2);
        InflateRect(&r, p(10), p(7));
        box(dc, r, field, GetFocus() == control ? orange : border, 8);
    }
    void paintMods(HDC dc, int h, int content) {
        const int right = contentLeft + content, panelBottom = h - 64;
        panelBox(dc, contentLeft, 124, right, 196);
        text(dc, L"Mods folder", contentLeft + 20, 136, content - 360, 22, fonts.strong);
        text(dc, mods::default_mods_directory().make_preferred().wstring(), contentLeft + 20, 160, content - 360, 18,
             fonts.caption, muted, DT_LEFT | DT_SINGLELINE | DT_PATH_ELLIPSIS | DT_NOPREFIX);
        panelBox(dc, contentLeft, 208, right, panelBottom);
        const int half = (content - 52) / 2, contentsLeft = contentLeft + 32 + half;
        text(dc, L"Load order", contentLeft + 20, 220, 120, 22, fonts.strong);
        text(dc, L"Top to bottom. Later mods win.", contentLeft + 130, 223, half - 110,
             18, fonts.caption, muted, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        if (!modList.empty()) {
            text(dc, L"What it adds", contentsLeft, 220, 140, 22, fonts.strong);
            text(dc, L"Choose a character or stage to remove it.", contentsLeft + 130, 223, half - 130, 18, fonts.caption, muted,
                 DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        }
        if (modList.empty()) {
            const int bottom = std::max(320, panelBottom - 64), middle = (252 + bottom) / 2;
            box(dc, {p(contentLeft + 20), p(252), p(right - 20), p(bottom)}, field, border, 8);
            text(dc, L"No mods yet", contentLeft + 36, middle - 28, content - 72, 24, fonts.strong, ink,
                 DT_CENTER | DT_SINGLELINE | DT_NOPREFIX);
            text(dc, L"Create one below, open the import tool, or drop mod folders into the mods folder.",
                 contentLeft + 36, middle + 4, content - 72, 38, fonts.caption, muted,
                 DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
        }
        editFrame(dc, ModName);
    }
    void paintImport(HDC dc, int h, int content) {
        const int right = contentLeft + content, listsBottom = h - 210, targetTop = listsBottom + 12;
        panelBox(dc, contentLeft, 124, right, 196);
        caps(dc, L"WHAT DO YOU WANT TO ADD TO BUDOKAI 2?", contentLeft + 20, 132, 400);
        panelBox(dc, contentLeft, 204, right, 256);
        caps(dc, L"FROM", contentLeft + 20, 212, 100);
        if (source) {
            const auto file = source->image().filename().wstring();
            const int fileWidth = std::min(measure(dc, file, fonts.strong), content - 640);
            text(dc, file, contentLeft + 20, 228, fileWidth, 22, fonts.strong, ink, DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            auto name = wide(mods::game_name(sourceIdentity.game));
            if (name.rfind(L"Dragon Ball Z: ", 0) == 0)
                name.erase(0, 15);
            text(dc, name, contentLeft + 34 + fileWidth, 229,
                 content - 614 - fileWidth, 22, fonts.caption, muted, DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        } else
            text(dc, game || !disc ? L"Choose a Budokai, Budokai 2, Budokai 3 or Infinite World disc" : L"Reading your game disc…",
                 contentLeft + 20, 228, content - 600, 22, fonts.strong, ink, DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        panelBox(dc, contentLeft, 264, right, listsBottom);
        const auto placeholder = [&](const std::wstring &message) {
            box(dc, {p(contentLeft + 20), p(304), p(right - 20), p(listsBottom - 14)}, field, border, 8);
            text(dc, message, contentLeft + 40, (304 + listsBottom) / 2 - 30, content - 80, 60, fonts.caption, muted,
                 DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
        };
        const wchar_t *contents[] = {L"Characters", L"Stages", L"Movesets", L"Capsules"};
        const std::wstring content_name = contents[int(importTask)];
        const auto mapping = L" Disc report… lists the disc's tables and file formats, not its data, so they can be mapped.";
        if (characterView) {
            const int half = (content - 52) / 2;
            const bool models = sourceRoster->models;
            caps(dc, models ? L"1   CHOOSE A MODEL" : movesView ? L"1   CHOOSE WHOSE MOVES" : L"1   CHOOSE A CHARACTER",
                 contentLeft + 20, 278, half - 200, orange);
            caps(dc, models && selected(SpotMode) == forceModel ? L"2   CHOOSE WHOSE MOVES IT USES"
                 : movesView ? L"2   CHOOSE WHOSE BODY"
                 : models ? L"2   CHOOSE WHO WEARS IT"
                 : selected(SpotMode) == 1 ? L"2   CHOOSE WHOSE PORTRAIT THE NEW SPOT USES"
                                           : L"2   CHOOSE A SPOT TO SHARE",
                 contentLeft + 32 + half, 278, content - 52 - half, orange);
            editFrame(dc, EntryFilter);
        } else {
            auto heading = content_name;
            std::transform(heading.begin(), heading.end(), heading.begin(), ::towupper);
            if (!stageImport())
                caps(dc, heading, contentLeft + 20, 278, 300, orange);
            if (!disc)
                placeholder(L"Verify your Budokai 2 ISO on the Play page first. The import tool needs it.");
            else if (!source)
                placeholder(L"Reading the disc…");
            else if (importTask == Task::characters)
                placeholder(L"Characters from " + wide(mods::game_name(sourceIdentity.game)) +
                            L" can't be imported yet: its character tables aren't mapped." + mapping);
            else if (stageImport()) {
                const int half = (content - 52) / 2;
                caps(dc, L"1   CHOOSE A STAGE (EXPERIMENTAL)", contentLeft + 20, 278, half, orange);
                caps(dc, L"2   CHOOSE WHICH STAGE IT REPLACES", contentLeft + 32 + half, 278, content - 52 - half, orange);
            } else if (stageView) {
                const int half = (content - 52) / 2;
                text(dc,
                     L"This disc has " + std::to_wstring(sourceStages->size()) + L" stages. " +
                         (sourceIdentity.game == mods::DragonBallGame::budokai3 && !mods::stage_mods_enabled
                              ? std::wstring(L"Budokai 3 stages can't be imported in this version.")
                          : sourceIdentity.game == mods::DragonBallGame::budokai3
                              ? std::wstring(L"Stages can be added once your Budokai 2 disc has been read: verify it "
                                             L"on the Play page, then open the import tool again.")
                              : std::wstring(L"These are Budokai 2's own stages, already in the game. To add one, "
                                             L"choose your Budokai 3 disc with Choose ISO…")),
                     contentLeft + 32 + half + 8, 310, content - 52 - half - 36, 200, fonts.caption, muted,
                     DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
            } else if (importTask == Task::movesets)
                placeholder(L"Movesets from " + wide(mods::game_name(sourceIdentity.game)) +
                            L" can't be imported: its move files are a newer version than Budokai 2 reads. Choose "
                            L"your Budokai 2 disc (Game disc) to put one character's moves on another's body.");
            else
                placeholder(content_name + L" aren't mapped yet, in Budokai 2 or the other games." + mapping);
        }
        panelBox(dc, contentLeft, targetTop, right, h - 64);
        text(dc, L"3   Save", contentLeft + 20, targetTop + 10, 160, 22, fonts.strong);
        uint64_t done = 0, total = 0;
        if (busy() && modJob(activeJob)) {
            std::lock_guard lock(progress->mutex);
            done = progress->done;
            total = progress->total;
        }
        if (busy() && modJob(activeJob)) {
            const int left = contentLeft + 130, width = content - 150 - 52, top = targetTop + 17;
            box(dc, {p(left), p(top), p(left + width), p(top + 8)}, field, border, 8);
            int from = 0, to = 0;
            if (total) {
                to = int(uint64_t(width) * std::min(done, total) / total);
            } else {
                const auto now = std::chrono::steady_clock::now().time_since_epoch();
                from = int(std::chrono::duration_cast<std::chrono::milliseconds>(now).count() / 4 % (width + 80)) - 80;
                to = std::min(width, from + 80);
                from = std::max(0, from);
            }
            if (to > from)
                box(dc, {p(left + from), p(top), p(left + to), p(top + 8)}, orange, orange, 8);
            text(dc, total ? std::to_wstring(std::min<uint64_t>(100, done * 100 / total)) + L"%" : L"…", left + width + 8,
                 targetTop + 10, 44, 22, fonts.strong, ink, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
        }
        if (characterView) {
            const auto free = freeIds;
            if (!(busy() && modJob(activeJob))) {
                text(dc,
                     free == 0 ? std::wstring(L"No room for more characters. Remove one on the Mods page to make room.")
                     : L"Room for " + std::to_wstring(free) + (free == 1 ? L" more character." : L" more characters."),
                     contentLeft + 120, targetTop + 13, content - 120 - 200, 18, fonts.caption, free ? muted : red,
                     DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            }
            caps(dc, L"SAVE TO", contentLeft + 20, targetTop + 34, 220);
            caps(dc, L"ON THE SELECT SCREEN", contentLeft + 252, targetTop + 34, 260);
            text(dc, plan(), contentLeft + 20, targetTop + 94, content - 40, 36, fonts.caption, ink,
                 DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS | DT_NOPREFIX);
        } else if (stageImport()) {
            caps(dc, L"SAVE TO", contentLeft + 20, targetTop + 34, 220);
            text(dc, stagePlan(), contentLeft + 20, targetTop + 94, content - 40, 36, fonts.caption, ink,
                 DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS | DT_NOPREFIX);
        } else
            text(dc, L"Imports add to Budokai 2; every character already in it stays on the select screen.",
                 contentLeft + 20, targetTop + 48, content - 40, 20, fonts.caption, muted,
                 DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
    static constexpr uint32_t stageSlots = mods::stage_count - mods::stage_first_variant;
    std::wstring stageName(uint32_t record) const {
        static const char *const names[] = {"ISLAND", "TOWN", "ROOM TIME MIND", "BUDO", "R&R BASE", "SNOW", "GARDEN",
                                            "NAMEC", "KAIOH SHIN", "ISLAND B", "TOWN B", "R&R BASE B", "SNOW B"};
        if (gameStages && record < gameStages->size())
            return wide(mods::stage_display_name((*gameStages)[record].internal));
        return record < std::size(names) ? wide(mods::stage_display_name(names[record])) : L"stage " + std::to_wstring(record);
    }
    std::optional<std::string> stageProblem(size_t row) const {
        if (row < sourceStageProblems.size() && sourceStageProblems[row])
            return sourceStageProblems[row];
        if (sourceStages && row < sourceStages->size() && (*sourceStages)[row].files.size() != 2)
            return std::string("it has no second pack");
        return std::nullopt;
    }
    std::wstring stagePlan() const {
        const auto row = SendMessageW(controls.at(SourceFiles), LB_GETCURSEL, 0, 0);
        const auto target = SendMessageW(controls.at(TargetList), LB_GETCURSEL, 0, 0);
        if (!stageImport() || row < 0 || size_t(row) >= sourceStages->size() || target < 0 || target >= LRESULT(stageSlots))
            return L"Choose a stage, then the stage it replaces.";
        const auto &stage = (*sourceStages)[size_t(row)];
        const uint32_t record = mods::stage_first_variant + uint32_t(target);
        if (const auto problem = stageProblem(size_t(row)))
            return wide(mods::stage_display_name(stage.internal)) + L" can't be imported: " + wide(*problem) + L".";
        const int choice = selected(TargetMod);
        const auto into = choice > 0 && size_t(choice) <= modList.size() ? L"Saved into " + wide(modList[size_t(choice - 1)].name) + L"."
                                                                         : std::wstring(L"Saved as a new mod.");
        return wide(mods::stage_display_name(stage.internal)) + L" from Budokai 3 replaces " + stageName(record) +
               L", a copy of another stage, so no stage is lost. Choose " + stageName(record) +
               L" on the stage select screen to play it. Stage imports are experimental: its particle effects "
               L"are left out, and if the fight doesn't load, remove the stage on the Mods page. " + into;
    }
    std::wstring plan() const {
        const auto *character = sourceCharacter();
        const auto row = SendMessageW(controls.at(TargetList), LB_GETCURSEL, 0, 0);
        if (!character || !gameRoster || row < 0)
            return L"Choose a character and where it goes.";
        const int choice = selected(TargetMod);
        const auto into = choice > 0 && size_t(choice) <= modList.size() ? L"Saved into " + wide(modList[size_t(choice - 1)].name) + L"."
                                                                         : std::wstring(L"Saved as a new mod.");
        const auto *occupant = gameRoster->find(gameRoster->grid[size_t(row)]);
        if (movesView) {
            if (!occupant)
                return L"That spot is empty. Choose a spot with a character whose body the moves go on.";
            const auto moves = gameRoster->shapes.find(character->id), body = gameRoster->shapes.find(occupant->id);
            const bool differ = moves == gameRoster->shapes.end() || body == gameRoster->shapes.end() ||
                                !(moves->second == body->second);
            return L"A new character: " + wide(occupant->name) + L"'s body with " + wide(character->name) +
                   L"'s moves, " +
                   (selected(SpotMode) == 1 ? std::wstring(L"with a new spot of its own before Random. ")
                                            : L"sharing spot " + std::to_wstring(row + 1) + L"; R2 switches. ") +
                   (differ ? L"Their skeletons differ, so the body may move oddly. " : L"") + into;
        }
        if (sourceRoster->models) {
            if (!occupant)
                return L"That spot is empty. Choose a spot with a character to wear the model.";
            if (selected(SpotMode) == forceModel)
                return L"A new character with its own spot: the whole Budokai 3 " + wide(character->name) +
                       L" model with " + wide(occupant->name) + L"'s moves. Budokai 3 bodies still show black in "
                       L"Budokai 2, and the moves may not suit the body. " + into;
            if (const auto why = modelMismatch(*character, *occupant))
                return *why;
            const auto wearer = L"A new " + wide(occupant->name) + L" wearing the Budokai 3 " + wide(character->name) +
                                L" textures, with " + wide(occupant->name) + L"'s body and moves,";
            if (selected(SpotMode) == compareVersions)
                return L"Adds four versions of " + wide(character->name) + L" worn by " + wide(occupant->name) +
                       L", sharing spot " + std::to_wstring(row + 1) +
                       L". R2 goes through them in order: D " + wide(occupant->name) +
                       L"'s body with the Budokai 3 textures (the normal import), H the same keeping " +
                       wide(occupant->name) + L"'s shading textures, J the whole Budokai 3 model with " +
                       wide(occupant->name) + L"'s shading textures, A the whole model as forced. Uses four "
                       L"character IDs. " + into;
            return wearer + (selected(SpotMode) == 1
                                 ? L" with its own spot before Random. "
                                 : L" sharing spot " + std::to_wstring(row + 1) + L"; R2 switches. ") +
                   L"Only the textures come over. " + into;
        }
        if (selected(SpotMode) == 1)
            return wide(character->name) + L" gets its own spot before Random, showing " +
                   (occupant ? wide(occupant->name) + L"'s" : L"spot " + std::to_wstring(row + 1) + L"'s") +
                   L" portrait. " + into;
        return wide(character->name) + L" shares spot " + std::to_wstring(row + 1) +
               (occupant ? L" with " + wide(occupant->name) : L"") +
               L"; press R2 (Right Shift) there to switch between them. " + into;
    }
    void paintSettings(HDC dc, int content) {
        const int right = contentLeft + content;
        const Grid grid(content);
        auto section = [&](int top, int rows, const wchar_t *title, const std::wstring &note, COLORREF tone) {
            panelBox(dc, contentLeft, top, right, top + 44 + rows * 56 + 4);
            text(dc, title, contentLeft + 20, top + 12, 200, 22, fonts.strong);
            text(dc, note, contentLeft + 120, top + 14, content - 140 - (top == updatesTop ? 130 : 0), 18,
                 fonts.caption, tone, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        };
        auto label = [&](int top, int row, int column, const wchar_t *value) {
            caps(dc, value, grid.x(column), Grid::label(top, row), grid.column);
        };
        section(displayTop, 2, L"Display",
                settings.windowMode ? L"Borderless uses your desktop resolution. The game draws up to 60 frames a second."
                                    : L"The game draws up to 60 frames a second; a higher cap only lowers latency.",
                muted);
        label(displayTop, 0, 0, L"WINDOW MODE");
        label(displayTop, 0, 1, L"RESOLUTION");
        label(displayTop, 0, 2, L"ASPECT RATIO");
        label(displayTop, 1, 0, L"FRAME RATE CAP");
        label(displayTop, 1, 1, L"VSYNC");
        label(displayTop, 1, 2, L"GAME AUDIO");

        section(graphicsTop, 2, L"Graphics", L"Renderer, filtering and upscaling", muted);
        label(graphicsTop, 0, 0, L"RENDERER");
        label(graphicsTop, 0, 1, L"ANTI-ALIASING");
        label(graphicsTop, 0, 2, L"TEXTURE FILTERING");
        label(graphicsTop, 1, 0, L"UPSCALING");
        label(graphicsTop, 1, 1, L"PRESETS");

        std::wstring message = updateStatus;
        if (updating()) {
            std::lock_guard lock(updateProgress->mutex);
            if (!updateProgress->text.empty())
                message = updateProgress->text;
        }
        section(updatesTop, 1, L"Updates", message, updateError ? red : muted);
        label(updatesTop, 0, 0, L"CHANNEL");
        label(updatesTop, 0, 1, L"AUTOMATIC UPDATES");
        label(updatesTop, 0, 2, L"MANUAL CHECK");
    }
    void paint() {
        PAINTSTRUCT ps{};
        auto dc = BeginPaint(hwnd, &ps);
        RECT rect{};
        GetClientRect(hwnd, &rect);
        if (rect.right <= 0 || rect.bottom <= 0) {
            EndPaint(hwnd, &ps);
            return;
        }
        buffer.make(rect.right, rect.bottom);
        auto memory = buffer.dc;
        const int w = unscale(rect.right), h = unscale(rect.bottom);
        const int content = w - contentLeft - margin;
        const size_t pixels = size_t(rect.right) * size_t(rect.bottom);
        if (ambient.size() != pixels) {
            fill(memory, rect, background);
            fill(memory, {0, 0, p(sidebarWidth), rect.bottom}, sidebar);
            buffer.frame.radial(rect, rect.right * 3 / 4, -p(160), p(620), ember, 30);
            buffer.frame.radial(rect, 0, rect.bottom + p(60), p(440), sky, 22);
            ambient.assign(buffer.frame.pixels, buffer.frame.pixels + pixels);
        } else {
            GdiFlush();
            std::copy(ambient.begin(), ambient.end(), buffer.frame.pixels);
        }
        paintSidebar(memory, h);
        paintHeader(memory, w, content);
        if (page == 0)
            paintPlay(memory, content);
        else if (page == 1)
            paintSaves(memory, h, content);
        else if (page == modsPage)
            paintMods(memory, h, content);
        else if (page == importPage)
            paintImport(memory, h, content);
        else
            paintSettings(memory, content);
        const int footer = h - 34;
        rule(memory, contentLeft, footer - 12, content);
        std::wstring message = status;
        if (busy()) {
            const auto current = progressText();
            if (!current.empty())
                message = current;
        }
        if (error)
            ellipse(memory, p(contentLeft + 4), p(footer + 9), p(3), red);
        text(memory, message, contentLeft + (error ? 14 : 0), footer, content - (error ? 14 : 0), 20,
             fonts.caption, error ? red : muted, DT_LEFT | DT_END_ELLIPSIS | DT_SINGLELINE | DT_NOPREFIX);
        const int task[] = {TaskCharacters, TaskStages, TaskMovesets, TaskCapsules};
        std::vector<int> glowing;
        for (int id : {int(Play), int(ImportSelected), task[int(importTask)]})
            if (const auto r = controlRect(id); r && IsWindowEnabled(controls.at(id))) {
                buffer.frame.halo(*r, cut(id == Play ? 12 : 10), p(id == Play ? 22 : 14), ember, id == Play ? 120 : 80);
                glowing.push_back(id);
            }
        BitBlt(dc, 0, 0, rect.right, rect.bottom, memory, 0, 0, SRCCOPY);
        EndPaint(hwnd, &ps);
        if (paintedPage != page || paintedSize != std::pair<int, int>{rect.right, rect.bottom} || paintedGlow != glowing) {
            paintedPage = page;
            paintedSize = {rect.right, rect.bottom};
            paintedGlow = glowing;
            for (auto [id, control] : controls)
                InvalidateRect(control, nullptr, FALSE);
        }
    }
    COLORREF surface(int id, const RECT &r) const {
        if (id >= PlayTab && id <= SettingsTab)
            return sidebar;
        if (id == Play) {
            const int width = std::max(1, p(contentWidth()));
            RECT bounds{};
            GetWindowRect(controls.at(id), &bounds);
            MapWindowPoints(nullptr, hwnd, reinterpret_cast<POINT *>(&bounds), 2);
            return mix(heroFrom, heroTo, (bounds.left + (r.right - r.left) / 2 - p(contentLeft)) * 255 / width);
        }
        return panel;
    }
    void navigationIcon(HDC dc, int id, RECT r, COLORREF color) {
        const int left = r.left + p(18), top = r.top + p(17), size = p(16);
        auto pen = CreatePen(PS_SOLID, p(2), color);
        auto previous = SelectObject(dc, pen);
        auto hollow = SelectObject(dc, GetStockObject(NULL_BRUSH));
        if (id == PlayTab) {
            POINT points[] = {{left + p(2), top}, {left + size, top + size / 2}, {left + p(2), top + size}};
            SetDCBrushColor(dc, color);
            SelectObject(dc, GetStockObject(DC_BRUSH));
            Polygon(dc, points, 3);
        } else if (id == SavesTab) {
            RoundRect(dc, left, top, left + size, top + size, p(4), p(4));
            MoveToEx(dc, left + p(4), top + p(1), nullptr);
            LineTo(dc, left + p(4), top + p(6));
            LineTo(dc, left + size - p(4), top + p(6));
            LineTo(dc, left + size - p(4), top);
        } else if (id == ModsTab) {
            const int half = size / 2 - p(1);
            for (int i = 0; i < 4; ++i) {
                const int x = left + (i % 2) * (half + p(2)), y = top + (i / 2) * (half + p(2));
                RoundRect(dc, x, y, x + half, y + half, p(3), p(3));
            }
        } else {
            for (int i = 0; i < 3; ++i) {
                const int y = top + p(2) + i * p(6), knob = left + p(i == 1 ? 10 : 4);
                MoveToEx(dc, left, y, nullptr);
                LineTo(dc, left + size, y);
                ellipse(dc, knob, y, p(2), color);
            }
        }
        SelectObject(dc, hollow);
        SelectObject(dc, previous);
        DeleteObject(pen);
    }
    void drawItem(const DRAWITEMSTRUCT &item) {
        auto dc = item.hDC;
        RECT r = item.rcItem;
        const bool enabled = (item.itemState & ODS_DISABLED) == 0;
        if (item.CtlType == ODT_BUTTON) {
            Canvas canvas(item.hDC, r);
            dc = canvas.dc;
            const int id = int(item.CtlID);
            const bool navigation = id >= PlayTab && id <= SettingsTab;
            const bool chosen = navigation && id == PlayTab + (page == importPage ? modsPage : page);
            const bool primary = id == Play;
            const bool taskChoice = (id == TaskCharacters && importTask == Task::characters) ||
                                    (id == TaskStages && importTask == Task::stages) ||
                                    (id == TaskMovesets && importTask == Task::movesets) ||
                                    (id == TaskCapsules && importTask == Task::capsules);
            const bool pressed = (item.itemState & ODS_SELECTED) != 0;
            const bool hot = hotButton == id && enabled;
            backdrop(dc, item.hwndItem, r, surface(id, r));
            COLORREF label;
            const bool lit = (primary || taskChoice || id == ImportSelected) && enabled;
            if (navigation) {
                if (hot && !chosen)
                    box(dc, r, mix(sidebar, ink, 10), mix(sidebar, ink, 10), 10);
                label = chosen || hot ? ink : muted;
            } else if (lit) {
                emberFill(dc, r, primary ? 12 : 10, pressed, hot);
                label = RGB(36, 16, 4);
            } else {
                const COLORREF color = !enabled ? panel : pressed ? field : hot ? hover : raised;
                const COLORREF outline = !enabled ? mix(panel, border, 140) : hot ? mix(border, orange, 110) : border;
                box(dc, r, color, outline, 10);
                if (enabled && !pressed)
                    fill(dc, {r.left + cut(10) + 1, r.top + 1, r.right - 1, r.top + 2}, mix(color, ink, 18));
                label = enabled ? ink : faint;
                if (id == DeleteSelected && enabled) label = red;
            }
            wchar_t text[100]{};
            GetWindowTextW(item.hwndItem, text, 100);
            auto old = SelectObject(dc, primary ? fonts.brand : navigation || taskChoice || id == ImportSelected ? fonts.strong
                                                                                                                 : fonts.body);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, label);
            RECT textRect = r;
            if (navigation) {
                navigationIcon(dc, id, r, chosen ? gold : hot ? muted : faint);
                textRect.left = r.left + p(48);
                DrawTextW(dc, text, -1, &textRect, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX);
            } else {
                if (primary && enabled) {
                    const int cx = r.left + p(30), cy = (r.top + r.bottom) / 2, s = p(7);
                    POINT play[] = {{cx - s / 2, cy - s}, {cx + s, cy}, {cx - s / 2, cy + s}};
                    SetDCBrushColor(dc, label);
                    auto brush = SelectObject(dc, GetStockObject(DC_BRUSH));
                    auto pen = SelectObject(dc, GetStockObject(NULL_PEN));
                    Polygon(dc, play, 3);
                    SelectObject(dc, brush);
                    SelectObject(dc, pen);
                    textRect.left += p(18);
                }
                DrawTextW(dc, text, -1, &textRect, DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
            }
            SelectObject(dc, old);
            if ((item.itemState & ODS_FOCUS) && !(item.itemState & ODS_NOFOCUSRECT) && !chosen) {
                RECT focus = r;
                InflateRect(&focus, -p(3), -p(3));
                SetDCPenColor(dc, lit ? RGB(36, 16, 4) : navigation ? mix(sidebar, orange, 70) : orange);
                auto pen = SelectObject(dc, GetStockObject(DC_PEN));
                auto brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
                if (!cutShape(dc, focus, cut(8), [](int, int) { return COLORREF(0); },
                              lit ? RGB(36, 16, 4) : navigation ? mix(sidebar, orange, 70) : orange, false)) {
                    const auto points = cutCorners(focus, cut(8));
                    Polygon(dc, points.data(), int(points.size()));
                }
                SelectObject(dc, pen);
                SelectObject(dc, brush);
            }
            return;
        }
        const bool chosen = (item.itemState & ODS_SELECTED) != 0;
        const bool list = item.CtlType == ODT_LISTBOX;
        fill(dc, r, list ? field : panel);
        if (chosen) {
            fill(dc, r, mix(list ? field : panel, orange, 26));
            fill(dc, {r.left, r.top + p(list ? 8 : 6), r.left + p(3), r.bottom - p(list ? 8 : 6)}, orange);
        }
        std::wstring value;
        if (item.itemID != UINT(-1)) {
            const UINT getLength = list ? LB_GETTEXTLEN : CB_GETLBTEXTLEN, getText = list ? LB_GETTEXT : CB_GETLBTEXT;
            const auto n = SendMessageW(item.hwndItem, getLength, item.itemID, 0);
            if (n >= 0 && n < 32768) {
                value.resize(size_t(n) + 1);
                SendMessageW(item.hwndItem, getText, item.itemID, reinterpret_cast<LPARAM>(value.data()));
                value.resize(size_t(n));
            }
        }
        auto old = SelectObject(dc, fonts.body);
        SetBkMode(dc, TRANSPARENT);
        r.left += p(14);
        r.right -= p(10);
        std::vector<std::wstring> parts;
        for (size_t start = 0;;) {
            const size_t end = value.find(L'\u00b7', start);
            auto part = value.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
            part.erase(0, part.find_first_not_of(L' '));
            part.erase(part.find_last_not_of(L' ') + 1);
            if (!part.empty())
                parts.push_back(part);
            if (end == std::wstring::npos)
                break;
            start = end + 1;
        }
        if (parts.size() > 1 && (parts[0] == L"On" || parts[0] == L"Off")) {
            const bool on = parts[0] == L"On";
            ellipse(dc, r.left + p(4), (r.top + r.bottom) / 2, p(4), on ? green : faint);
            if (!on)
                ellipse(dc, r.left + p(4), (r.top + r.bottom) / 2, p(2), chosen ? mix(field, orange, 26) : field);
            r.left += p(20);
            parts.erase(parts.begin());
        }
        for (size_t i = 0; i < parts.size() && r.left < r.right; ++i) {
            SetTextColor(dc, !enabled ? muted : i == 0 ? ink : muted);
            SIZE size{};
            GetTextExtentPoint32W(dc, parts[i].c_str(), int(parts[i].size()), &size);
            DrawTextW(dc, parts[i].c_str(), int(parts[i].size()), &r, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
            r.left += size.cx + p(i == 0 ? 22 : 18);
        }
        SelectObject(dc, old);
    }
    void refreshBackups() {
        savedGames = disc ? list_card_saves(card()) : std::vector<std::string>{};
        backups = disc ? list_backups(backupPath()) : std::vector<std::filesystem::path>{};
        SendMessageW(controls[BackupList], LB_RESETCONTENT, 0, 0);
        if (!gameSaveList()) for (const auto &p : backups) {
            const auto time = std::filesystem::last_write_time(p);
            const auto system = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                time - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
            const auto seconds = std::chrono::system_clock::to_time_t(system);
            tm local{};
            localtime_s(&local, &seconds);
            wchar_t stamp[80]{};
            wcsftime(stamp, 80, L"%b %d, %Y  at  %H:%M:%S", &local);
            const auto label = std::wstring(stamp) + L"     " +
                               std::to_wstring((std::filesystem::file_size(p) + 1023) / 1024) + L" KB";
            SendMessageW(controls[BackupList], LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        }
        if (gameSaveList()) {
            for (const auto &name : savedGames) {
                const auto label = wide(name) == L"BASLUS-20779DBZ2" ? std::wstring(L"Budokai 2 \u00b7 USA") : wide(name);
                SendMessageW(controls[BackupList], LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
            }
        } else if (!backups.empty())
            SendMessageW(controls[BackupList], LB_SETSEL, TRUE, 0);
        if (page == 1)
            ShowWindow(controls[BackupList], emptySaveList() ? SW_HIDE : SW_SHOWNA);
    }
    static std::wstring bytesText(uint64_t bytes) {
        if (bytes < 1024)
            return std::to_wstring(bytes) + L" B";
        if (bytes < 1024 * 1024)
            return std::to_wstring((bytes + 1023) / 1024) + L" KB";
        const auto tenths = (bytes * 10 + 1024 * 1024 / 2) / (1024 * 1024);
        return std::to_wstring(tenths / 10) + L"." + std::to_wstring(tenths % 10) + L" MB";
    }
    static std::wstring number(size_t value) {
        auto text = std::to_wstring(value);
        return std::wstring(text.size() < 4 ? 4 - text.size() : 0, L'0') + text;
    }
    std::vector<std::optional<uint32_t>> contentSlots;
    std::vector<std::optional<uint32_t>> contentStages;
    std::multimap<std::string, std::wstring> importedFrom;
    unsigned freeIds = 0;
    std::optional<std::wstring> importedInto(size_t entry) const {
        if (!source)
            return std::nullopt;
        const auto key = utf8(source->image().filename().wstring()) + ":" + mods::roster_archive + "#" + std::to_string(entry);
        const auto found = importedFrom.find(key);
        return found == importedFrom.end() ? std::nullopt : std::optional<std::wstring>(found->second);
    }
    void fillModContents() {
        auto list = controls[ModContents];
        SendMessageW(list, LB_RESETCONTENT, 0, 0);
        contentSlots.clear();
        contentStages.clear();
        const auto row = SendMessageW(controls[ModList], LB_GETCURSEL, 0, 0);
        if (row < 0 || size_t(row) >= modList.size()) {
            refreshEnabled();
            return;
        }
        const auto &mod = modList[size_t(row)];
        const auto add = [&](const std::wstring &label, std::optional<uint32_t> slot,
                             std::optional<uint32_t> stage = std::nullopt) {
            SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
            contentSlots.push_back(slot);
            contentStages.push_back(stage);
        };
        std::vector<mods::RosterSection> characters;
        try {
            characters = mods::read_roster_sections(mod.folder);
        } catch (const std::exception &) {
        }
        const auto nameOf = [&](uint32_t id) -> std::wstring {
            const auto *character = gameRoster ? gameRoster->find(id) : nullptr;
            return character ? wide(character->name) : L"ID " + std::to_wstring(id);
        };
        for (const auto &character : characters) {
            auto label = wide(character.name.empty() ? "Character" : mods::roster_display_name(character.name)) +
                         L"   ·   ID " + std::to_wstring(character.slot);
            if (character.base)
                label += L"   ·   moves of " + nameOf(*character.base);
            if (character.new_cell)
                label += L"   ·   own spot";
            else if (!character.shared_cells.empty())
                label += L"   ·   shares spot " + std::to_wstring(character.shared_cells.front() + 1);
            add(label, character.slot);
        }
        std::vector<mods::StageSection> stages;
        try {
            stages = mods::read_stage_sections(mod.folder);
        } catch (const std::exception &) {
        }
        for (const auto &stage : stages)
            add(L"Stage: " + wide(stage.name) + L"   ·   replaces " + stageName(stage.slot), std::nullopt, stage.slot);
        if (mod.archive_entries)
            add(std::to_wstring(mod.archive_entries) + (mod.archive_entries == 1 ? L" archive entry" : L" archive entries"),
                std::nullopt);
        if (mod.replaced_files)
            add(std::to_wstring(mod.replaced_files) + (mod.replaced_files == 1 ? L" replaced file" : L" replaced files"),
                std::nullopt);
        if (characters.empty() && stages.empty() && !mod.archive_entries && !mod.replaced_files)
            add(L"Nothing yet: import into it or add files to its folder", std::nullopt);
        refreshEnabled();
    }
    void refreshMods(int select = -1) {
        const auto keep = SendMessageW(controls[ModList], LB_GETCURSEL, 0, 0);
        modList = mods::scan_mods(mods::default_mods_directory());
        auto list = controls[ModList];
        SendMessageW(list, LB_RESETCONTENT, 0, 0);
        for (const auto &mod : modList) {
            std::wstring label = (mod.enabled ? L"On    ·   " : L"Off   ·   ") + wide(mod.name);
            if (!mod.author.empty())
                label += L"   ·   by " + wide(mod.author);
            size_t characters = 0;
            try {
                characters = mods::read_roster_sections(mod.folder).size();
            } catch (const std::exception &) {
            }
            const auto changes = mod.replaced_files + mod.archive_entries;
            if (characters)
                label += L"   ·   " + std::to_wstring(characters) + (characters == 1 ? L" character" : L" characters");
            if (changes || !characters)
                label += L"   ·   " + std::to_wstring(changes) + (changes == 1 ? L" file" : L" files");
            SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        }
        const int chosen = select >= 0 ? select : int(keep);
        if (chosen >= 0 && size_t(chosen) < modList.size())
            SendMessageW(list, LB_SETCURSEL, WPARAM(chosen), 0);
        if (page == modsPage)
            for (int id : {ModList, ModContents, ModRename, ModDelete, ModRemove})
                ShowWindow(controls[id], modList.empty() ? SW_HIDE : SW_SHOWNA);
        fillModContents();
        importedFrom.clear();
        std::vector<std::filesystem::path> folders;
        for (const auto &mod : modList) {
            folders.push_back(mod.folder);
            for (const auto &record : mods::read_imports(mod.folder))
                importedFrom.emplace(record.source, wide(mod.name));
        }
        freeIds = 0;
        const auto taken = mods::roster_slots_in_use(folders);
        for (uint32_t id = 0; id < mods::roster_count; ++id)
            if (mods::roster_slot_unused(id) && std::find(taken.begin(), taken.end(), id) == taken.end())
                ++freeIds;
        auto target = controls[TargetMod];
        const auto previous = SendMessageW(target, CB_GETCURSEL, 0, 0);
        SendMessageW(target, CB_RESETCONTENT, 0, 0);
        SendMessageW(target, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"New mod"));
        for (const auto &mod : modList)
            SendMessageW(target, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(wide(mod.name).c_str()));
        SendMessageW(target, CB_SETCURSEL, previous > 0 && size_t(previous) <= modList.size() ? previous : 0, 0);
        refreshEnabled();
    }
    void startModJob(Job kind, const std::wstring &label, std::function<Result(const std::shared_ptr<Progress> &)> work) {
        if (busy() || child)
            return;
        activeJob = kind;
        error = false;
        {
            std::lock_guard lock(progress->mutex);
            progress->text = label;
            progress->done = progress->total = 0;
        }
        std::cout << "[launcher] operation=" << int(kind) << " started" << std::endl;
        auto updates = progress;
        task = std::async(std::launch::async, [kind, work = std::move(work), updates] {
            auto result = work(updates);
            result.kind = kind;
            return result;
        });
        refreshEnabled();
    }
    static void report(const std::shared_ptr<Progress> &progress, const std::wstring &text, uint64_t done,
                       uint64_t total) {
        std::lock_guard lock(progress->mutex);
        progress->done = done;
        progress->total = total;
        const auto percent = total ? done * 100 / total : 100;
        progress->text = text + L"   " + std::to_wstring(percent) + L"%";
    }
    static Result readDisc(Job kind, const std::filesystem::path &image) {
        Result result{kind};
        result.loaded = std::make_shared<mods::SourceDisc>(image);
        result.identity = mods::identify_disc(*result.loaded);
        result.roster = mods::read_disc_roster(*result.loaded);
        if (result.roster)
            mods::read_model_shapes(*result.loaded, *result.roster);
        else
            result.roster = mods::read_budokai3_models(*result.loaded);
        if (result.identity.game == mods::DragonBallGame::budokai3)
            if (const auto stages = mods::read_disc_stages(*result.loaded))
                for (const auto &stage : *stages)
                    result.stageProblems.push_back(mods::budokai3_stage_problem(*result.loaded, stage));
        return result;
    }
    void dropped(HDROP drop) {
        std::wstring name(size_t(DragQueryFileW(drop, 0, nullptr, 0)) + 1, L'\0');
        DragQueryFileW(drop, 0, name.data(), UINT(name.size()));
        DragFinish(drop);
        name.resize(name.size() - 1);
        const std::filesystem::path image(name);
        auto extension = image.extension().wstring();
        std::transform(extension.begin(), extension.end(), extension.begin(), ::towlower);
        if (extension != L".iso") {
            status = L"Only .iso disc images can be dropped here.";
            InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }
        if (page == importPage) {
            if (!busy())
                loadSource(image);
            return;
        }
        if (busy() || child)
            return;
        if (page != 0)
            command(PlayTab, BN_CLICKED);
        disc.reset();
        backups.clear();
        savedGames.clear();
        start(Job::verify, image);
    }
    void rememberPlacement() {
        WINDOWPLACEMENT placement{sizeof(placement)};
        if (!GetWindowPlacement(hwnd, &placement))
            return;
        RECT r = placement.rcNormalPosition;
        if (!IsIconic(hwnd) && !IsZoomed(hwnd))
            GetWindowRect(hwnd, &r);
        settings.windowLeft = r.left;
        settings.windowTop = r.top;
        settings.windowWidth = std::clamp(int(r.right - r.left), 0, 32768);
        settings.windowHeight = std::clamp(int(r.bottom - r.top), 0, 32768);
        settings.windowMaximized = placement.showCmd == SW_SHOWMAXIMIZED;
        try {
            write_settings(settings_path(), settings);
        } catch (const std::exception &e) {
            std::cerr << "[launcher:error] " << e.what() << std::endl;
        }
    }
    void loadSource(const std::filesystem::path &image) {
        startModJob(Job::readSource, L"Reading " + image.filename().wstring() + L"…",
                    [image](const std::shared_ptr<Progress> &) {
                        auto result = readDisc(Job::readSource, image);
                        if (result.identity.game == mods::DragonBallGame::none)
                            throw std::runtime_error(utf8(image.filename().wstring()) +
                                                     " is not a Dragon Ball game. The import tool reads Budokai, "
                                                     "Budokai 2, Budokai 3 and Infinite World discs");
                        result.message = L"Read " + wide(mods::game_name(result.identity.game)) + L" (" +
                                         wide(result.identity.serial) + L").";
                        return result;
                    });
    }
    void showSource(std::shared_ptr<mods::SourceDisc> disc, std::optional<mods::DiscRoster> roster,
                    mods::DiscIdentity identity) {
        source = std::move(disc);
        sourceRoster = std::move(roster);
        sourceIdentity = std::move(identity);
        if (sourceIdentity.game != mods::DragonBallGame::budokai3)
            sourceStageProblems.clear();
        try {
            sourceStages = mods::read_disc_stages(*source);
        } catch (const std::exception &) {
            sourceStages.reset();
        }
        SetWindowTextW(controls[EntryFilter], L"");
        setTask(importTask);
    }
    void setTask(Task next) {
        importTask = next;
        movesView = importTask == Task::movesets && sourceRoster && !sourceRoster->models;
        stageView = importTask == Task::stages && sourceStages.has_value();
        characterView = (importTask == Task::characters && sourceRoster.has_value()) || movesView;
        for (int id : {TaskCharacters, TaskStages, TaskMovesets, TaskCapsules})
            InvalidateRect(controls[id], nullptr, FALSE);
        layout();
        fillSourceList();
        if (characterView && sourceRoster->models)
            showWearers();
        else
            fillTargetList();
        spotModes();
        refreshEnabled();
    }
    static constexpr int compareVersions = 2, forceModel = 3;
    void spotModes() {
        const bool models = characterView && sourceRoster->models;
        const auto list = controls[SpotMode];
        const auto count = SendMessageW(list, CB_GETCOUNT, 0, 0);
        if ((count == 4) == models)
            return;
        const auto keep = std::min<LRESULT>(SendMessageW(list, CB_GETCURSEL, 0, 0), 1);
        SendMessageW(list, CB_RESETCONTENT, 0, 0);
        std::vector<const wchar_t *> modes{L"Share a spot (R2 switches)", L"Add a new spot"};
        if (models) {
            modes.push_back(L"Compare 4 versions (test)");
            modes.push_back(L"Force a new character");
        }
        for (const auto *mode : modes)
            SendMessageW(list, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(mode));
        SendMessageW(list, CB_SETCURSEL, WPARAM(std::max<LRESULT>(keep, 0)), 0);
    }
    void fillSourceList() {
        const auto *kept = sourceCharacter();
        const auto keptId = kept ? int(kept->id) : -1;
        sourceRows.clear();
        auto list = controls[SourceFiles];
        const auto keepStage = SendMessageW(list, LB_GETCURSEL, 0, 0);
        SendMessageW(list, LB_RESETCONTENT, 0, 0);
        if (stageView) {
            for (const auto &stage : *sourceStages) {
                auto label = wide(mods::stage_display_name(stage.internal));
                const auto index = size_t(&stage - sourceStages->data());
                if (sourceIdentity.game == mods::DragonBallGame::budokai3 &&
                    (stage.files.size() != 2 || (index < sourceStageProblems.size() && sourceStageProblems[index])))
                    label += L"   ·   can't be imported";
                else if (sourceIdentity.game == mods::DragonBallGame::budokai3 && !stage.files.empty())
                    if (const auto mod = importedInto(stage.files.front().second))
                        label += L"   ·   already in " + *mod;
                SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
            }
            if (keepStage >= 0 && size_t(keepStage) < sourceStages->size())
                SendMessageW(list, LB_SETCURSEL, WPARAM(keepStage), 0);
            return;
        }
        if (!characterView)
            return;
        std::wstring filter = textOf(EntryFilter);
        std::transform(filter.begin(), filter.end(), filter.begin(), ::towlower);
        int first = -1;
        for (size_t i = 0; i < sourceRoster->characters.size(); ++i) {
            const auto &character = sourceRoster->characters[i];
            auto label = wide(character.name);
            if (sourceRoster->models && gameRoster) {
                const auto fit = mods::fitting_wearers(*sourceRoster, character.id, *gameRoster);
                label += fit.empty() ? L"   ·   no Budokai 2 match"
                                     : L"   ·   fits " + wide(gameRoster->find(fit.front())->name) +
                                           (fit.size() > 1 ? L" +" + std::to_wstring(fit.size() - 1) : L"");
            }
            if (!movesView && !character.files.empty())
                if (const auto mod = importedInto(character.files.front().second))
                    label += L"   ·   already in " + *mod;
            auto lowered = label;
            std::transform(lowered.begin(), lowered.end(), lowered.begin(), ::towlower);
            if (!filter.empty() && lowered.find(filter) == std::wstring::npos)
                continue;
            if (int(character.id) == keptId)
                first = int(sourceRows.size());
            sourceRows.push_back(i);
            SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        }
        SendMessageW(list, LB_SETCURSEL, WPARAM(std::max(0, first)), 0);
    }
    const mods::DiscCharacter *sourceCharacter() const {
        const auto row = SendMessageW(controls.at(SourceFiles), LB_GETCURSEL, 0, 0);
        return characterView && row >= 0 && size_t(row) < sourceRows.size()
                   ? &sourceRoster->characters[sourceRows[size_t(row)]]
                   : nullptr;
    }
    void fillTargetList() {
        auto list = controls[TargetList];
        const auto keep = SendMessageW(list, LB_GETCURSEL, 0, 0);
        SendMessageW(list, LB_RESETCONTENT, 0, 0);
        if (stageImport()) {
            std::map<uint32_t, std::wstring> filled;
            for (const auto &mod : modList)
                if (mod.enabled)
                    for (const auto &section : mods::read_stage_sections(mod.folder))
                        filled[section.slot] = wide(section.name) + L" from " + wide(mod.name);
            for (uint32_t i = 0; i < stageSlots; ++i) {
                const uint32_t record = mods::stage_first_variant + i;
                auto label = stageName(record);
                const auto &stages = *gameStages;
                for (const auto &other : stages)
                    if (record < stages.size() && !other.variant && other.files == stages[record].files)
                        label += L"   ·   a copy of " + wide(mods::stage_display_name(other.internal));
                if (const auto found = filled.find(record); found != filled.end())
                    label += L"   ·   now " + found->second;
                SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
            }
            SendMessageW(list, LB_SETCURSEL, WPARAM(keep >= 0 && keep < LRESULT(stageSlots) ? keep : 0), 0);
            return;
        }
        if (!characterView || !gameRoster)
            return;
        for (uint32_t cell = 0; cell < mods::roster_select_cells; ++cell) {
            const auto *occupant = gameRoster->find(gameRoster->grid[cell]);
            auto label = L"Spot " + std::to_wstring(cell + 1) + L"   ·   " +
                         (occupant ? wide(occupant->name) : std::wstring(L"empty"));
            if (sourceRoster->models && sourceCharacter() && fits(cell))
                label += L"   ·   fits";
            SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        }
        SendMessageW(list, LB_SETCURSEL, WPARAM(keep >= 0 && keep < LRESULT(mods::roster_select_cells) ? keep : 0), 0);
    }
    static std::vector<size_t> characterEntries(const mods::DiscCharacter &character) {
        std::vector<size_t> entries;
        for (const auto &file : character.files)
            if (std::find(entries.begin(), entries.end(), file.second) == entries.end())
                entries.push_back(file.second);
        return entries;
    }
    void openImporter() {
        page = importPage;
        refreshMods();
        layout();
        if (game || !disc || settings.iso.empty())
            return;
        const auto image = settings.iso;
        startModJob(Job::readGame, L"Reading your game disc…", [image](const std::shared_ptr<Progress> &) {
            auto result = readDisc(Job::readGame, image);
            result.message = L"Your game disc is ready. Choose what to add, or another disc with Choose ISO…";
            return result;
        });
    }
    void showGame(std::shared_ptr<mods::SourceDisc> disc, std::optional<mods::DiscRoster> roster,
                  mods::DiscIdentity identity) {
        game = std::move(disc);
        gameRoster = std::move(roster);
        gameIdentity = identity;
        try {
            gameStages = mods::read_disc_stages(*game);
        } catch (const std::exception &) {
            gameStages.reset();
        }
        if (!source)
            showSource(game, gameRoster, std::move(identity));
        else
            setTask(importTask);
    }
    std::filesystem::path importMod(const std::string &name) {
        const int choice = selected(TargetMod);
        if (choice > 0 && size_t(choice) <= modList.size())
            return modList[size_t(choice - 1)].folder;
        auto folder = mods::create_mod(mods::default_mods_directory(), name, "", "Made with the OpenKai2 import tool");
        refreshMods();
        for (size_t i = 0; i < modList.size(); ++i)
            if (modList[i].folder == folder)
                SendMessageW(controls[TargetMod], CB_SETCURSEL, i + 1, 0);
        return folder;
    }
    struct ImportItem {
        size_t entry;
        mods::ImportTarget target;
        std::wstring name;
    };
    std::optional<std::wstring> modelMismatch(const mods::DiscCharacter &model, const mods::DiscCharacter &wearer) const {
        const auto theirs = gameRoster->shapes.find(wearer.id);
        if (theirs == gameRoster->shapes.end())
            return wide(wearer.name) + L" has no costume model to replace.";
        const auto fit = mods::fitting_wearers(*sourceRoster, model.id, *gameRoster);
        if (std::find(fit.begin(), fit.end(), wearer.id) != fit.end())
            return std::nullopt;
        const auto ours = sourceRoster->shapes.find(model.id);
        std::wstring others;
        for (size_t i = 0; i < fit.size() && i < 3; ++i)
            others += (i ? L", " : L"") + wide(gameRoster->find(fit[i])->name);
        return wide(model.name) + L"'s textures (" + std::to_wstring(ours->second.textures.size()) +
               L") are laid out differently from " + wide(wearer.name) + L"'s (" +
               std::to_wstring(theirs->second.textures.size()) + L"), so they would land in the wrong places. " +
               (others.empty() ? L"No Budokai 2 character's textures match."
                               : L"It fits " + others + (fit.size() > 3 ? L" and others." : L"."));
    }
    void showWearers() {
        fillTargetList();
        const auto list = controls[TargetList];
        const auto spot = SendMessageW(list, LB_GETCURSEL, 0, 0);
        if (spot >= 0 && fits(size_t(spot)))
            return;
        for (uint32_t cell = 0; cell < mods::roster_select_cells; ++cell)
            if (fits(cell)) {
                SendMessageW(list, LB_SETCURSEL, cell, 0);
                return;
            }
    }
    bool fits(size_t spot) const {
        if (movesView)
            return gameRoster && gameRoster->find(gameRoster->grid[spot]);
        if (!sourceRoster || !sourceRoster->models)
            return true;
        const auto *model = sourceCharacter();
        const auto *wearer = gameRoster ? gameRoster->find(gameRoster->grid[spot]) : nullptr;
        if (model && wearer && selected(SpotMode) == forceModel)
            return gameRoster->shapes.count(wearer->id) != 0;
        return model && wearer && !modelMismatch(*model, *wearer);
    }
    void importModel(const mods::DiscCharacter &model, int pick) {
        const auto *wearer = gameRoster->find(gameRoster->grid[size_t(pick)]);
        if (!wearer)
            throw std::runtime_error("That spot is empty; choose a spot with a character to wear the model");
        const bool force = selected(SpotMode) == forceModel;
        if (const auto why = modelMismatch(model, *wearer); why && !force)
            throw std::runtime_error(utf8(*why));
        if (mods::costume_fields(*game, *wearer).empty())
            throw std::runtime_error(utf8(wide(wearer->name)) + " has no costume model to replace");
        const bool compare = selected(SpotMode) == compareVersions, newSpot = selected(SpotMode) == 1 || force;
        using Version = mods::ModelVersion;
        const std::vector<std::pair<Version, const char *>> versions =
            compare ? std::vector<std::pair<Version, const char *>>{{Version::budokai2_mesh, "D"},
                                                                     {Version::budokai2_lighting, "H"},
                                                                     {Version::converted_budokai2_lighting, "J"},
                                                                     {Version::converted, "A"}}
                    : std::vector<std::pair<Version, const char *>>{{force ? Version::converted : Version::budokai2_mesh, ""}};
        std::vector<std::filesystem::path> folders;
        for (const auto &mod : modList)
            folders.push_back(mod.folder);
        auto taken = mods::roster_slots_in_use(folders);
        std::vector<mods::RosterCharacter> added;
        for (const auto &[version, letter] : versions) {
            const auto slot = mods::free_roster_slot(taken);
            if (!slot)
                throw std::runtime_error(compare ? "Comparing versions needs four unused character IDs; fewer are free"
                                                 : "Every unused character ID is already filled by a mod");
            taken.push_back(*slot);
            mods::RosterCharacter character;
            character.slot = *slot;
            character.base = wearer->id;
            character.name = model.internal + (*letter ? std::string(" ") + letter : std::string());
            if (newSpot) {
                character.new_cell = true;
                character.icon_cell = uint32_t(pick);
            } else
                character.alternate_cells.push_back(uint32_t(pick));
            added.push_back(std::move(character));
        }
        std::string stem = model.name;
        for (auto &c : stem)
            c = std::isalnum(static_cast<unsigned char>(c)) ? char(std::tolower(static_cast<unsigned char>(c))) : '_';
        const auto entry = model.files.front().second;
        const auto costumes = mods::costume_fields(*game, *wearer);
        const auto own = costumes.front().second;
        std::vector<mods::ImportTarget> targets;
        for (const auto &[version, letter] : versions) {
            mods::ImportTarget target;
            target.mode = mods::ImportTarget::Mode::NewEntry;
            target.file = mods::roster_archive;
            target.kind = mods::ContentKind::Character;
            target.name = stem + "_" + std::to_string(entry) + (*letter ? std::string("_") + letter : std::string()) +
                          ".bin";
            target.source = utf8(source->image().filename().wstring()) + ":" + mods::roster_archive + "#" +
                            std::to_string(entry);
            targets.push_back(std::move(target));
        }
        std::wstring ids;
        for (size_t i = 0; i < added.size(); ++i)
            ids += (i ? L", " : L"") + (compare ? wide(versions[i].second) + L" = " : std::wstring()) +
                   std::to_wstring(added[i].slot);
        const auto summary = (force ? L"Added the whole " + wide(model.name) + L" model as a new character with " +
                                          wide(wearer->name) + L"'s moves"
                                    : L"Added " + wide(wearer->name) + L" with " + wide(model.name) + L"'s textures") +
                             (compare ? L" in four versions, sharing spot " + std::to_wstring(pick + 1) +
                                            L"; press R2 there to go through them"
                              : newSpot ? std::wstring(L", with a new spot of its own")
                                        : L", sharing spot " + std::to_wstring(pick + 1) + L"; press R2 there to switch") +
                             L" (character ID" + (compare ? L"s " : L" ") + ids + L").";
        const auto mod = importMod(model.name);
        startModJob(Job::importMod, L"Importing…",
                    [disc = source, game = game, entry, own, versions, targets, mod, added, costumes,
                     summary](const std::shared_ptr<Progress> &progress) {
                        for (size_t i = 0; i < versions.size(); ++i) {
                            report(progress, L"Importing the model", i, versions.size());
                            const auto bytes = mods::model_version(*disc, entry, *game, own, versions[i].first);
                            const auto written = mods::import_bytes(mod, targets[i], bytes).filename().string();
                            std::vector<std::pair<uint32_t, std::string>> fields;
                            for (const auto &costume : costumes)
                                fields.push_back({costume.first, "add:" + written});
                            mods::append_roster(mod, added[i], fields);
                        }
                        report(progress, L"Importing the model", versions.size(), versions.size());
                        Result result{Job::importMod};
                        result.message = summary + L" It loads the next time the game starts.";
                        return result;
                    });
    }
    void importMoveset(const mods::DiscCharacter &moves, int pick) {
        const auto *body = gameRoster->find(gameRoster->grid[size_t(pick)]);
        if (!body)
            throw std::runtime_error("That spot is empty; choose a spot with a character whose body the moves go on");
        const auto costumes = mods::costume_fields(*game, *body);
        if (costumes.empty())
            throw std::runtime_error(utf8(wide(body->name)) + " has no costume model to lend");
        std::vector<std::filesystem::path> folders;
        for (const auto &mod : modList)
            folders.push_back(mod.folder);
        const auto slot = mods::free_roster_slot(mods::roster_slots_in_use(folders));
        if (!slot)
            throw std::runtime_error("Every unused character ID is already filled by a mod");
        mods::RosterCharacter added;
        added.slot = *slot;
        added.base = moves.id;
        added.name = body->internal;
        const bool newSpot = selected(SpotMode) == 1;
        if (newSpot) {
            added.new_cell = true;
            added.icon_cell = uint32_t(pick);
        } else
            added.alternate_cells.push_back(uint32_t(pick));
        std::vector<std::pair<uint32_t, std::string>> fields;
        for (const auto &[offset, entry] : costumes)
            fields.push_back({offset, std::to_string(entry)});
        const auto mod = importMod(body->name + " " + moves.name);
        mods::append_roster(mod, added, fields);
        status = L"Added " + wide(body->name) + L" with " + wide(moves.name) + L"'s moves" +
                 (newSpot ? std::wstring(L", with a new spot of its own")
                          : L", sharing spot " + std::to_wstring(pick + 1) + L"; press R2 there to switch") +
                 L" (character ID " + std::to_wstring(added.slot) + L")." +
                 L" It loads the next time the game starts.";
        refreshMods();
        InvalidateRect(hwnd, nullptr, FALSE);
    }
    void importStage() {
        const auto row = SendMessageW(controls[SourceFiles], LB_GETCURSEL, 0, 0);
        const auto target = SendMessageW(controls[TargetList], LB_GETCURSEL, 0, 0);
        if (row < 0 || size_t(row) >= sourceStages->size() || target < 0 || target >= LRESULT(stageSlots))
            throw std::runtime_error("Choose a stage and the B stage it becomes");
        const auto stage = (*sourceStages)[size_t(row)];
        const uint32_t record = mods::stage_first_variant + uint32_t(target);
        const auto shown = mods::stage_display_name(stage.internal);
        if (const auto problem = stageProblem(size_t(row)))
            throw std::runtime_error(shown + " can't be imported: " + *problem);
        std::string stem = stage.internal;
        for (auto &c : stem)
            c = std::isalnum(static_cast<unsigned char>(c)) ? char(std::tolower(static_cast<unsigned char>(c))) : '_';
        std::string name = "B3 " + stage.internal;
        name.resize(std::min<size_t>(name.size(), 31));
        const auto mod = importMod(shown + " stage");
        const auto iso = utf8(source->image().filename().wstring());
        const auto summary = L"Added " + wide(shown) + L" from Budokai 3 in place of " + stageName(record) +
                             L". Choose " + stageName(record) + L" on the stage select screen to play it.";
        startModJob(Job::importMod, L"Importing…",
                    [disc = source, stage, record, stem, name, iso, mod, summary](const std::shared_ptr<Progress> &progress) {
                        report(progress, L"Unpacking the stage", 0, 9);
                        const auto files = mods::budokai3_stage_files(*disc, stage);
                        std::vector<std::pair<uint32_t, std::string>> fields;
                        for (size_t i = 0; i < files.size(); ++i) {
                            report(progress, L"Importing the stage's files", i + 1, 9);
                            mods::ImportTarget target;
                            target.mode = mods::ImportTarget::Mode::NewEntry;
                            target.file = mods::roster_archive;
                            target.kind = mods::ContentKind::Stage;
                            char suffix[16];
                            std::snprintf(suffix, sizeof suffix, "_%02x.bin", files[i].first);
                            target.name = "stage_" + stem + suffix;
                            target.source = iso + ":" + mods::roster_archive + "#" +
                                            std::to_string(stage.files[files[i].first < 0x54 ? 0 : 1].second);
                            const auto written = mods::import_bytes(mod, target, files[i].second).filename().string();
                            fields.push_back({files[i].first, "add:" + written});
                        }
                        mods::append_stage(mod, record, name, fields);
                        report(progress, L"Importing the stage's files", 9, 9);
                        Result result{Job::importMod};
                        result.message = summary + L" It loads the next time the game starts.";
                        return result;
                    });
    }
    void importCharacter() {
        const auto *character = sourceCharacter();
        if (!character)
            return;
        if (!game || !gameRoster)
            throw std::runtime_error("Verify your Budokai 2 ISO on the Play page before importing characters");
        const int pick = int(SendMessageW(controls[TargetList], LB_GETCURSEL, 0, 0));
        if (pick < 0)
            throw std::runtime_error("Choose a spot on the select screen");
        if (sourceRoster->models)
            return importModel(*character, pick);
        if (movesView)
            return importMoveset(*character, pick);
        std::vector<std::filesystem::path> folders;
        for (const auto &mod : modList)
            folders.push_back(mod.folder);
        const auto slot = mods::free_roster_slot(mods::roster_slots_in_use(folders));
        if (!slot)
            throw std::runtime_error("Every unused character ID is already filled by a mod");
        mods::RosterCharacter added;
        added.slot = *slot;
        added.base = character->id;
        added.name = character->internal;
        const bool newSpot = selected(SpotMode) == 1;
        if (newSpot) {
            added.new_cell = true;
            added.icon_cell = uint32_t(pick);
        } else
            added.alternate_cells.push_back(uint32_t(pick));
        const auto &names = source->entries(mods::roster_archive);
        const auto disc = utf8(source->image().filename().wstring());
        std::string stem = character->name;
        for (auto &c : stem)
            c = std::isalnum(static_cast<unsigned char>(c)) ? char(std::tolower(static_cast<unsigned char>(c))) : '_';
        std::vector<ImportItem> items;
        std::map<uint32_t, size_t> itemFor;
        uint64_t total = 0;
        for (const auto entry : characterEntries(*character)) {
            ImportItem item;
            item.entry = entry;
            item.target.mode = mods::ImportTarget::Mode::NewEntry;
            item.target.file = mods::roster_archive;
            item.target.kind = mods::ContentKind::Character;
            item.target.name = stem + "_" + std::to_string(entry) + ".bin";
            item.target.source = disc + ":" + mods::roster_archive + "#" + std::to_string(entry);
            item.name = wide(names[entry].name.empty() ? item.target.name : names[entry].name);
            total += source->range(mods::roster_archive, entry).size;
            itemFor[uint32_t(entry)] = items.size();
            items.push_back(std::move(item));
        }
        const auto *occupant = gameRoster->find(gameRoster->grid[size_t(pick)]);
        const auto summary =
            (newSpot ? L"Added " + wide(character->name) + L" with a new spot of its own (character ID " +
                          std::to_wstring(added.slot) + L")."
                    : L"Added " + wide(character->name) + L", sharing spot " + std::to_wstring(pick + 1) +
                          (occupant ? L" with " + wide(occupant->name) : std::wstring()) + L" (character ID " +
                          std::to_wstring(added.slot) + L"). Press R2 on that spot to switch.");
        const auto mod = importMod(character->name);
        const auto files = character->files;
        startModJob(Job::importMod, L"Importing…",
                    [disc = source, items, itemFor, total, mod, added, files, summary](const std::shared_ptr<Progress> &progress) {
                        uint64_t done = 0;
                        std::vector<std::string> written(items.size());
                        for (size_t i = 0; i < items.size(); ++i) {
                            const auto text = L"Importing " + items[i].name + L" (" + std::to_wstring(i + 1) +
                                              L" of " + std::to_wstring(items.size()) + L")";
                            report(progress, text, done, total);
                            written[i] = mods::import_from_disc(*disc, mods::roster_archive, items[i].entry, mod,
                                                                items[i].target,
                                                                [&](uint64_t bytes) { report(progress, text, done += bytes, total); })
                                             .filename()
                                             .string();
                        }
                        std::vector<std::pair<uint32_t, std::string>> fields;
                        for (const auto &[offset, entry] : files)
                            fields.push_back({offset, "add:" + written[itemFor.at(entry)]});
                        mods::append_roster(mod, added, fields);
                        Result result{Job::importMod};
                        result.message = summary + L" It loads the next time the game starts.";
                        return result;
                    });
    }
    void saveReport() {
        if (!source)
            return;
        const auto folder = mods::default_mods_directory() / mods::reports_folder;
        const auto name = sourceIdentity.serial.empty() ? utf8(source->image().stem().wstring()) : sourceIdentity.serial;
        std::vector<std::tuple<std::string, size_t, size_t>> compared;
        if (sourceRoster && sourceRoster->models && gameRoster && game)
            for (const char *wanted : {"KAIO-SHIN", "KULILIN"}) {
                const mods::DiscCharacter *theirs = nullptr, *ours = nullptr;
                for (const auto &character : sourceRoster->characters)
                    if (character.internal == wanted && !theirs)
                        theirs = &character;
                for (const auto &character : gameRoster->characters)
                    if (character.internal == wanted && !ours)
                        ours = &character;
                if (!theirs || !ours || theirs->files.empty())
                    continue;
                const auto costumes = mods::costume_fields(*game, *ours);
                if (!costumes.empty())
                    compared.emplace_back(ours->name, theirs->files.front().second, costumes.front().second);
            }
        startModJob(Job::saveReport, L"Writing the disc report…",
                    [disc = source, game = game, compared, folder, name](const std::shared_ptr<Progress> &) {
                        std::filesystem::create_directories(folder);
                        const auto path = folder / (wide(name) + L".txt");
                        std::ofstream out(path, std::ios::binary | std::ios::trunc);
                        out << mods::disc_report(*disc);
                        if (!compared.empty())
                            out << "\nBudokai 3 meshes compared with your Budokai 2 disc's:\n";
                        for (const auto &[who, entry, own] : compared)
                            out << "\n  " << who << ", Budokai 3 entry " << entry << ", Budokai 2 entry " << own << ":\n"
                                << mods::mesh_differences(*disc, entry, *game, own);
                        if (!out)
                            throw std::runtime_error("Cannot write " + utf8(path.wstring()));
                        Result result{Job::saveReport};
                        result.image = path;
                        result.message = L"Saved the disc report to " + path.wstring() + L". Send it to have this disc mapped.";
                        return result;
                    });
    }
    void start(Job kind, std::filesystem::path source = {}) {
        const bool background = kind == Job::checkUpdate || kind == Job::downloadUpdate;
        if (background ? updating() : (busy() || child)) return;
        if (background) activeUpdateJob = kind; else activeJob = kind;
        if (kind == Job::verify || kind == Job::launch)
            candidateIso = source.empty() ? settings.iso : source;
        const auto prefs = settings;
        const auto save = card(), archive = backupPath();
        const auto availableRelease = release;
        std::vector<std::string> selectedGames;
        std::vector<std::filesystem::path> selectedBackups;
        for (int row : selectedSaveRows()) {
            if (gameSaveList() && size_t(row) < savedGames.size()) selectedGames.push_back(savedGames[size_t(row)]);
            if (!gameSaveList() && size_t(row) < backups.size()) selectedBackups.push_back(backups[size_t(row)]);
        }
        auto updates = background ? updateProgress : progress;
        {
            std::lock_guard lock(updates->mutex);
            updates->text = kind == Job::verify || kind == Job::launch ? L"Verifying your disc…"
                            : background                             ? L"Checking for updates…"
                                                                     : L"Working on your saves…";
        }
        if (background) updateError = false; else error = false;
        std::cout << "[launcher] operation=" << int(kind) << " started" << std::endl;
        auto work = std::async(std::launch::async, [kind, source, prefs, save, archive, updates, availableRelease, selectedGames, selectedBackups]() {
            Result result{kind, {}, {}, L""};
            if (kind == Job::checkUpdate || kind == Job::downloadUpdate) {
                result.release = kind == Job::checkUpdate ? check_for_updates(prefs.updateChannel != 0)
                                                         : availableRelease;
                if (result.release) {
                    result.message = L"Version " + wide(result.release->version) + L" is available.";
                    if (prefs.autoUpdates || kind == Job::downloadUpdate) {
                        result.staged = stage_update(*result.release, [updates](const std::string &line) {
                            std::lock_guard lock(updates->mutex);
                            updates->text = wide(line);
                        });
                        result.message = L"Version " + wide(result.release->version) +
                                         L" is downloaded and verified. Ready to install.";
                    }
                } else result.message = L"You're on the latest version.";
            } else if (kind == Job::verify || kind == Job::launch) {
                result.image = source.empty() ? prefs.iso : source;
                DiscOptions options;
                options.progress = [updates](const std::string &line) {
                    std::lock_guard lock(updates->mutex);
                    updates->text = wide(line);
                    std::cout << "[launcher:verify] " << line << std::endl;
                };
                result.disc = prepare_disc(result.image, default_cache_directory(), options);
                if (kind == Job::launch && prefs.autoBackups)
                    backup_card(result.disc->root / "mc0",
                                default_cache_directory().parent_path() / "backups" / result.disc->image_sha256);
                result.message = L"Disc verified.";
            } else if (kind == Job::deleteSaves) {
                delete_card_saves(save, archive, selectedGames);
                result.message = std::to_wstring(selectedGames.size()) + (selectedGames.size() == 1 ? L" save deleted." : L" saves deleted.") +
                                 L" Your previous card is in Backups.";
            } else if (kind == Job::deleteBackups) {
                delete_backup_files(archive, selectedBackups);
                result.message = std::to_wstring(selectedBackups.size()) + (selectedBackups.size() == 1 ? L" backup deleted." : L" backups deleted.");
            } else if(kind==Job::unlock) {
                const bool changed=unlock_card_content(save,archive);
                result.message=changed ? L"Characters, stages and skill capsules unlocked. Load your save in the game. The previous card is backed up."
                                       : L"This save already has all characters, stages and skill capsules. Load it in the game.";
            } else if (kind == Job::backup || kind == Job::afterExit) {
                auto p = backup_card(save, archive);
                result.message = p.empty() ? L"There is no save data to back up yet." : L"Backup created.";
            } else {
                import_save(save, source, archive, kind == Job::restore);
                result.message = kind == Job::restore
                                     ? L"Backup restored. Your previous card was backed up first."
                                     : L"Save imported. Your previous card was backed up first.";
            }
            return result;
        });
        if (background) updateTask = std::move(work); else task = std::move(work);
        refreshEnabled();
    }
    void launch() {
        wchar_t exe[32768]{};
        const auto n = GetModuleFileNameW(nullptr, exe, 32768);
        if (!n || n >= 32768)
            throw std::runtime_error("Cannot locate the game executable");
        std::wstring command = quote_argument(exe) + L" --fps-limit=" +
                               std::to_wstring(settings.fps) + L" --renderer=" +
                               wide(settings.renderer) + L" " +
                               quote_argument(settings.iso.wstring());
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        if (!CreateProcessW(exe, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                            std::filesystem::path(exe).parent_path().c_str(), &startup, &process))
            throw std::runtime_error("Cannot start the game process");
        CloseHandle(process.hThread);
        child = process.hProcess;
        status = L"Game running. Save tools unlock when you close it.";
        ShowWindow(hwnd, SW_MINIMIZE);
    }
    void timer() {
        presence.enable(settings.discordPresence);
        const char* pages[]={"Choosing a game","Managing saves","Managing mods","Configuring settings","Importing mods"};
        presence.update(running()?"Playing Budokai 2":"In launcher",running()?"In game":pages[page]);
        if(presence.status()!=lastPresenceStatus && !dropdownOpen()) {
            lastPresenceStatus=presence.status();InvalidateRect(hwnd,nullptr,FALSE);
        }
        for (const bool background : {false, true}) {
            auto &work = background ? updateTask : task;
            if (!work.valid() || work.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
                continue;
            try {
                auto result = work.get();
                std::cout << "[launcher] operation=" << int(result.kind) << " completed";
                if (!result.message.empty())
                    std::cout << " message=\"" << utf8(result.message) << '"';
                std::cout << std::endl;
                if (background) {
                    release = std::move(result.release);
                    staged = std::move(result.staged);
                    updateStatus = staged && child ? L"Update ready. It installs after the game closes."
                                                  : result.message;
                    updateError = false;
                    if (result.kind == Job::downloadUpdate && staged) installRequested = true;
                } else {
                    status = result.message;
                    error = false;
                    if (result.disc) {
                        disc = std::move(result.disc);
                        settings.iso = result.image;
                        write_settings(settings_path(), settings);
                    }
                    refreshBackups();
                    if (result.kind == Job::readGame && result.loaded)
                        showGame(std::move(result.loaded), std::move(result.roster), std::move(result.identity));
                    if (result.kind == Job::readSource && result.loaded) {
                        if (result.loaded->image() != settings.iso && result.loaded->image() != settings.importIso) {
                            settings.importIso = result.loaded->image();
                            write_settings(settings_path(), settings);
                        }
                        sourceStageProblems = std::move(result.stageProblems);
                        showSource(std::move(result.loaded), std::move(result.roster), std::move(result.identity));
                    }
                    if (result.kind == Job::saveReport)
                        ShellExecuteW(hwnd, L"open", result.image.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                    if (result.kind == Job::importMod) {
                        refreshMods();
                        fillSourceList();
                        if (stageImport())
                            fillTargetList();
                    }
                    if (!closing && result.kind == Job::launch) launch();
                }
            } catch (const std::exception &e) {
                if (background) {
                    updateStatus = wide(e.what());
                    updateError = true;
                } else {
                    if (activeJob == Job::verify || activeJob == Job::launch) disc.reset();
                    status = wide(e.what());
                    error = true;
                    if (activeJob == Job::deleteSaves || activeJob == Job::deleteBackups) refreshBackups();
                }
                std::cerr << (background ? "[launcher:update-error] " : "[launcher:error] ")
                          << e.what() << std::endl;
                if (!background && modJob(activeJob)) refreshMods();
            } catch (...) {
                (background ? updateStatus : status) = L"Something went wrong. Details are in launcher.log.";
                (background ? updateError : error) = true;
                std::cerr << "[launcher:error] operation=" << int(background ? activeUpdateJob : activeJob)
                          << " failed with an unknown error" << std::endl;
            }
            refreshEnabled();
        }
        if (child && WaitForSingleObject(child, 0) == WAIT_OBJECT_0) {
            DWORD code = 0;
            GetExitCodeProcess(child, &code);
            CloseHandle(child);
            child = nullptr;
            status = code ? L"The game closed with an error. Check startup.log for details." : L"Game closed.";
            error = code != 0;
            if (!closing) { ShowWindow(hwnd, SW_RESTORE); SetForegroundWindow(hwnd); }
            if (settings.autoBackups && disc) start(Job::afterExit);
            refreshEnabled();
        }
        if (!child && !busy()) {
            const bool active = session_active();
            if (active != externalGame) { externalGame = active; refreshEnabled(); }
        }
        if (closing && !child && !busy() && !updating()) { DestroyWindow(hwnd); return; }
        if (!closing && !picking && !dropdownOpen()) {
            if (updatePending && !updating()) {
                updatePending = false;
                if (settings.autoUpdates) start(Job::checkUpdate);
            }
            const bool idle = std::chrono::steady_clock::now() - lastInput >= std::chrono::seconds(5);
            if (!busy() && !updating() && !running() && staged &&
                (installRequested || (settings.autoUpdates && idle && (page == 0 || page == settingsPage)))) {
                try { apply_update(*staged); }
                catch (const std::exception &e) {
                    updateStatus = wide(e.what());
                    updateError = true;
                    installRequested = false;
                    settings.autoUpdates = false;
                    SendMessageW(controls[AutomaticUpdates], CB_SETCURSEL, 0, 0);
                    refreshEnabled();
                    return;
                }
                staged.reset();
                DestroyWindow(hwnd);
                return;
            }
        }
        if (busy() || (updating() && page == settingsPage)) InvalidateRect(hwnd, nullptr, FALSE);
    }
    std::filesystem::path chooseFallback(bool folder, bool iso, const wchar_t *title) {
        if (folder) {
            BROWSEINFOW options{};
            options.hwndOwner = hwnd;
            options.lpszTitle = title ? title : L"Choose an extracted save folder";
            options.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
            auto item = SHBrowseForFolderW(&options);
            if (!item)
                return {};
            wchar_t path[MAX_PATH]{};
            const bool valid = SHGetPathFromIDListW(item, path) != FALSE;
            CoTaskMemFree(item);
            if (!valid)
                throw std::runtime_error("The selection is not a folder on disk");
            return path;
        }
        std::vector<wchar_t> path(32768, 0);
        if (iso && !settings.iso.empty()) {
            auto native = settings.iso;
            const auto saved = native.make_preferred().wstring();
            if (saved.size() < path.size())
                std::copy(saved.begin(), saved.end(), path.begin());
        }
        OPENFILENAMEW options{};
        options.lStructSize = sizeof(options);
        options.hwndOwner = hwnd;
        options.lpstrFile = path.data();
        options.nMaxFile = DWORD(path.size());
        options.lpstrFilter = iso     ? L"Disc images (*.iso)\0*.iso\0All files\0*.*\0\0"
                              : title ? L"All files\0*.*\0\0"
                                      : L"Saves and backups (*.psu, *.zip)\0*.psu;*.zip\0All files\0*.*\0\0";
        options.lpstrTitle = title ? title : iso ? L"Choose your Budokai 2 USA ISO" : L"Import a save or backup";
        options.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (GetOpenFileNameW(&options))
            return path.data();
        if (CommDlgExtendedError())
            throw std::runtime_error("The Windows file picker could not open");
        return {};
    }
    std::filesystem::path choose(bool folder, bool iso = false, const wchar_t *title = nullptr) {
        picking = true;
        struct Reset {
            bool &flag;
            ~Reset() { flag = false; }
        } reset{picking};
        IFileOpenDialog *dialog = nullptr;
        if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&dialog))))
            return chooseFallback(folder, iso, title);
        struct Release {
            IFileOpenDialog *dialog;
            ~Release() { dialog->Release(); }
        } release{dialog};
        DWORD flags = 0;
        dialog->GetOptions(&flags);
        flags |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR;
        if (folder)
            flags |= FOS_PICKFOLDERS;
        else
            flags |= FOS_FILEMUSTEXIST;
        if (FAILED(dialog->SetOptions(flags)))
            throw std::runtime_error("The Windows file picker could not be configured");
        if (!folder) {
            const COMDLG_FILTERSPEC images[] = {{L"Disc images (*.iso)", L"*.iso"},
                                                {L"All files", L"*.*"}};
            const COMDLG_FILTERSPEC saves[] = {{L"Saves and backups (*.psu, *.zip)", L"*.psu;*.zip"},
                                               {L"All files", L"*.*"}};
            if (iso || !title)
                dialog->SetFileTypes(2, iso ? images : saves);
            else
                dialog->SetFileTypes(1, images + 1);
            dialog->SetFileTypeIndex(1);
        }
        if (iso && !settings.iso.empty()) {
            auto nativeDirectory = settings.iso.parent_path();
            nativeDirectory.make_preferred();
            IShellItem *directory = nullptr;
            if (SUCCEEDED(SHCreateItemFromParsingName(nativeDirectory.c_str(), nullptr,
                                                      IID_PPV_ARGS(&directory)))) {
                dialog->SetFolder(directory);
                directory->Release();
            }
            dialog->SetFileName(settings.iso.filename().c_str());
        }
        dialog->SetTitle(title    ? title
                         : folder ? L"Choose an extracted save folder"
                         : iso    ? L"Choose your Budokai 2 USA ISO"
                                  : L"Import a save or backup");
        const auto shown = dialog->Show(hwnd);
        if (shown == HRESULT_FROM_WIN32(ERROR_CANCELLED))
            return {};
        if (FAILED(shown))
            throw std::runtime_error("The Windows file picker could not open");
        IShellItem *item = nullptr;
        if (FAILED(dialog->GetResult(&item)))
            throw std::runtime_error("Cannot read the selected path");
        PWSTR name = nullptr;
        const auto result = item->GetDisplayName(SIGDN_FILESYSPATH, &name);
        item->Release();
        if (FAILED(result))
            throw std::runtime_error("The selection is not a file on disk");
        std::filesystem::path path(name);
        CoTaskMemFree(name);
        return path;
    }
    void saveOptions() {
        Settings next = settings;
        next.renderer = selected(Renderer) == 1   ? "gpu"
                        : selected(Renderer) == 2 ? "cpu"
                                                  : "auto";
        next.fps = rates.at(size_t(selected(Fps)));
        const auto size = sizes.at(size_t(selected(Resolution)));
        next.width = size.first;
        next.height = size.second;
        next.windowMode = unsigned(selected(WindowMode));
        next.antialiasing = unsigned(selected(Antialiasing));
        next.textureFilter = unsigned(selected(Filtering));
        next.upscaler = unsigned(selected(Upscaler));
        next.vsync = selected(Vsync) != 0;
        next.autoBackups = selected(AutoBackup) != 0;
        next.discordPresence=selected(DiscordActivity)!=0;
        next.aspect = unsigned(selected(Aspect));
        if (const int choice = selected(Audio); choice > 0) {
            next.audio = true;
            next.volume = audioVolumes[size_t(choice - 1)];
        } else
            next.audio = false;
        write_settings(settings_path(), next);
        settings = std::move(next);
        status = child ? L"Settings saved. Restart the game to use them." : L"Settings saved.";
        std::cout << "[launcher:settings] saved fps=" << settings.fps << " size=" << settings.width << 'x'
                  << settings.height << " mode=" << settings.windowMode << " renderer=" << settings.renderer
                  << " aspect=" << settings.aspect << " aa=" << settings.antialiasing
                  << " filtering=" << settings.textureFilter << " scaling=" << settings.upscaler
                  << " vsync=" << settings.vsync << " audio=" << settings.audio
                  << " volume=" << settings.volume << std::endl;
        error = false;
    }
    void command(int id, int notification) {
        lastInput = std::chrono::steady_clock::now();
        if (picking)
            return;
        if (id >= PlayTab && id <= SettingsTab) {
            page = id - PlayTab;
            for (int tab = PlayTab; tab <= SettingsTab; ++tab)
                InvalidateRect(controls[tab], nullptr, FALSE);
            if (page == modsPage)
                refreshMods();
            layout();
            SetFocus(controls[id]);
            return;
        }
        if (id == SaveListKind && (notification == CBN_SELCHANGE || notification == CBN_CLOSEUP)) {
            if (!SendMessageW(controls[SaveListKind], CB_GETDROPPEDSTATE, 0, 0) || notification == CBN_CLOSEUP) {
                refreshBackups(); layout();
            }
            return;
        }
        if (id == SelectSaves && notification == BN_CLICKED) {
            const auto list = controls[BackupList];
            const auto count = SendMessageW(list, LB_GETCOUNT, 0, 0);
            if (count > 0) {
                SendMessageW(list, LB_SETSEL, selectedSaveRows().size() != size_t(count), -1);
                refreshEnabled();
            }
            return;
        }
        if (id == BackupList && notification == LBN_SELCHANGE) {
            if (!busy() && !error)
                status = std::to_wstring(selectedSaveRows().size()) +
                    L" selected. Ctrl+click or Shift+click selects several; Ctrl+A selects all.";
            refreshEnabled(); return;
        }
        if (((id >= Renderer && id <= Audio) || id == DiscordActivity) && notification == CBN_SELCHANGE) {
            saveOptions();
            if (id == DiscordActivity) {
                presence.enable(settings.discordPresence);
                status = L"Discord activity preference saved.";
            }
            refreshEnabled();
            return;
        }
        if ((id == UpdateChannel || id == AutomaticUpdates) && notification == CBN_SELCHANGE) {
            if (updating()) return;
            settings.updateChannel = unsigned(selected(UpdateChannel));
            settings.autoUpdates = selected(AutomaticUpdates) != 0;
            write_settings(settings_path(), settings);
            release.reset();
            staged.reset();
            updatePending = true;
            updateStatus = settings.autoUpdates ? L"Update preferences saved."
                                                : L"Automatic updates are off. Use Check now to look for a release.";
            updateError = false;
            refreshEnabled();
            return;
        }
        if (id == ClearImage) {
            auto next = settings;
            next.antialiasing = 0;
            next.textureFilter = 0;
            next.upscaler = 2;
            write_settings(settings_path(), next);
            settings = std::move(next);
            for (auto [control, value] : {std::pair<int, int>{Antialiasing, 0}, {Filtering, 0}, {Upscaler, 2}})
                SendMessageW(controls[control], CB_SETCURSEL, value, 0);
            status = L"Crisp preset applied: bicubic upscaling, game texture filtering and no FXAA.";
            error = false;
            refreshEnabled();
            return;
        }
        if (id == ResetGraphics) {
            Settings defaults;
            auto next = settings;
            next.fps = defaults.fps; next.width = defaults.width; next.height = defaults.height;
            next.windowMode = defaults.windowMode; next.antialiasing = defaults.antialiasing;
            next.textureFilter = defaults.textureFilter; next.upscaler = defaults.upscaler;
            next.aspect = defaults.aspect; next.vsync = defaults.vsync; next.renderer = defaults.renderer;
            write_settings(settings_path(), next);
            settings = std::move(next);
            const std::pair<int, int> choices[] = {{Renderer, 0}, {Fps, 6}, {Resolution, 0},
                {WindowMode, 0}, {Antialiasing, 0}, {Filtering, 0}, {Upscaler, 1}, {Vsync, 1}, {Aspect, 0}};
            for (auto [control, value] : choices) SendMessageW(controls[control], CB_SETCURSEL, value, 0);
            status = L"Display and graphics settings restored. Your disc, saves and update choices were kept.";
            error = false;
            refreshEnabled();
            return;
        }
        if (id == ReleasePage) {
            ShellExecuteW(hwnd, L"open", L"https://github.com/7v0biscuit/OpenKai2/releases", nullptr,
                          nullptr, SW_SHOWNORMAL);
            return;
        }
        if (id == CheckUpdates) {
            updatePending = false;
            start(Job::checkUpdate);
            return;
        }
        if ((id == ModName || id == EntryFilter) && (notification == EN_SETFOCUS || notification == EN_KILLFOCUS)) {
            InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }
        if (id == EntryFilter && notification == EN_CHANGE) {
            fillSourceList();
            if (characterView && sourceRoster->models)
                showWearers();
            refreshEnabled();
            return;
        }
        if ((id == SourceFiles || id == TargetList) && notification == LBN_SELCHANGE) {
            if (id == SourceFiles && sourceRoster && sourceRoster->models)
                showWearers();
            refreshEnabled();
            return;
        }
        if ((id == TaskCharacters || id == TaskStages || id == TaskMovesets || id == TaskCapsules) &&
            notification == BN_CLICKED) {
            if (!busy())
                setTask(id == TaskCharacters ? Task::characters
                        : id == TaskStages   ? Task::stages
                        : id == TaskMovesets ? Task::movesets
                                             : Task::capsules);
            return;
        }
        if ((id == TargetMod || id == SpotMode) && notification == CBN_SELCHANGE) {
            if (id == SpotMode && characterView && sourceRoster->models)
                showWearers();
            refreshEnabled();
            return;
        }
        if (id == ModList && notification == LBN_SELCHANGE) {
            fillModContents();
            return;
        }
        if (id == ModContents && notification == LBN_SELCHANGE) {
            refreshEnabled();
            return;
        }
        if (id == ModRefresh) {
            refreshMods();
            status = L"Mods reloaded from the mods folder.";
            error = false;
            return;
        }
        if (id == ModFolder) {
            const auto folder = mods::default_mods_directory();
            std::filesystem::create_directories(folder);
            ShellExecuteW(hwnd, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            return;
        }
        if (id == CloseImporter) {
            page = modsPage;
            refreshMods();
            layout();
            return;
        }
        if (id == SaveReport && notification == BN_CLICKED) {
            saveReport();
            return;
        }
        if (notification != BN_CLICKED && notification != LBN_DBLCLK)
            return;
        if (busy() || child)
            return;
        if (session_active()) {
            status = L"Close the running game before changing the disc or saves.";
            layout();
            return;
        }
        if (id == InstallUpdate) {
            if (staged) { installRequested = true; refreshEnabled(); }
            else if (release) start(Job::downloadUpdate);
        } else if (id == ChooseIso) {
            auto p = choose(false, true);
            if (!p.empty()) {
                disc.reset();
                backups.clear();
                savedGames.clear();
                start(Job::verify, p);
            }
        } else if (id == VerifyIso)
            start(Job::verify, candidateIso);
        else if (id == Play && disc)
            start(Job::launch, settings.iso);
        else if (id == ImportFolder && disc) {
            auto p = choose(true);
            if (!p.empty())
                start(Job::importSave, p);
        } else if (id == ImportFile && disc) {
            auto p = choose(false);
            if (!p.empty())
                start(Job::importSave, p);
        } else if (id == Backup && disc)
            start(Job::backup);
        else if(id==UnlockAll && disc)
            start(Job::unlock);
        else if ((id == Restore || (id == BackupList && notification == LBN_DBLCLK)) && disc) {
            const auto rows = selectedSaveRows();
            if (!gameSaveList() && rows.size() == 1u && size_t(rows.front()) < backups.size()) {
                const auto prompt = L"Restore the selected backup? This replaces the current memory card. "
                                    L"A recovery backup of the current card will be created first.";
                if (MessageBoxW(hwnd, prompt, L"Restore backup", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES)
                    start(Job::restore, backups[size_t(rows.front())]);
            }
        } else if (id == DeleteSelected && disc) {
            const auto count = selectedSaveRows().size();
            if (!count) return;
            const auto message = L"Delete " + std::to_wstring(count) +
                (gameSaveList() ? (count == 1 ? L" save? " : L" saves? ") + std::wstring(L"Your current card is backed up first.")
                                : (count == 1 ? L" backup? " : L" backups? ") + std::wstring(L"This can't be undone."));
            if (MessageBoxW(hwnd, message.c_str(), gameSaveList() ? L"Delete saves" : L"Delete backups",
                            MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES)
                start(gameSaveList() ? Job::deleteSaves : Job::deleteBackups);
        } else if (id == ModToggle || id == ModUp || id == ModDown || (id == ModList && notification == LBN_DBLCLK)) {
            const auto row = SendMessageW(controls[ModList], LB_GETCURSEL, 0, 0);
            if (row < 0 || size_t(row) >= modList.size())
                return;
            auto index = size_t(row);
            if (id == ModUp && index > 0) {
                std::swap(modList[index], modList[index - 1]);
                --index;
            } else if (id == ModDown && index + 1 < modList.size()) {
                std::swap(modList[index], modList[index + 1]);
                ++index;
            } else if (id == ModToggle || id == ModList)
                modList[index].enabled = !modList[index].enabled;
            mods::save_load_order(mods::default_mods_directory(), modList);
            refreshMods(int(index));
            status = wide(modList[index].name) + (id == ModUp || id == ModDown ? L" moved. "
                                                  : modList[index].enabled ? L" enabled. " : L" disabled. ") +
                     L"Changes apply the next time the game starts.";
            error = false;
        } else if (id == ModRename || id == ModDelete || id == ModRemove) {
            const auto row = SendMessageW(controls[ModList], LB_GETCURSEL, 0, 0);
            if (row < 0 || size_t(row) >= modList.size())
                return;
            const auto mod = modList[size_t(row)];
            try {
                if (id == ModRename) {
                    const auto name = utf8(textOf(ModName));
                    if (name.find_first_not_of(" \t") == std::string::npos) {
                        status = L"Type the new name in the box beside Create mod, then press Rename.";
                        error = true;
                        SetFocus(controls[ModName]);
                        refreshEnabled();
                        return;
                    }
                    mods::rename_mod(mod.folder, name);
                    SetWindowTextW(controls[ModName], L"");
                    refreshMods(int(row));
                    status = L"Renamed " + wide(mod.name) + L" to " + wide(name) + L".";
                } else if (id == ModDelete) {
                    const auto question = L"Delete " + wide(mod.name) + L"?\n\nIts folder and everything it adds are "
                                          L"removed. This can't be undone.";
                    if (MessageBoxW(hwnd, question.c_str(), L"Delete mod", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
                        return;
                    mods::delete_mod(mods::default_mods_directory(), mod.id);
                    refreshMods(std::min(int(row), int(modList.size()) - 2));
                    status = L"Deleted " + wide(mod.name) + L". The game starts without it next time.";
                } else {
                    const auto line = SendMessageW(controls[ModContents], LB_GETCURSEL, 0, 0);
                    if (line >= 0 && size_t(line) < contentStages.size() && contentStages[size_t(line)]) {
                        const auto record = *contentStages[size_t(line)];
                        const auto question = L"Remove the stage that replaces " + stageName(record) + L" from " +
                                              wide(mod.name) + L"?\n\nIts files are removed and " + stageName(record) +
                                              L" comes back.";
                        if (MessageBoxW(hwnd, question.c_str(), L"Remove stage", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
                            return;
                        const auto files = mods::remove_stage(mod.folder, record);
                        refreshMods(int(row));
                        status = L"Removed the stage from " + wide(mod.name) + L" (" + std::to_wstring(files) +
                                 (files == 1 ? L" file" : L" files") + L"); " + stageName(record) + L" is back.";
                        error = false;
                        return;
                    }
                    if (line < 0 || size_t(line) >= contentSlots.size() || !contentSlots[size_t(line)])
                        return;
                    const auto slot = *contentSlots[size_t(line)];
                    const auto question = L"Remove character ID " + std::to_wstring(slot) + L" from " + wide(mod.name) +
                                          L"?\n\nIts spot and the files only it uses are removed.";
                    if (MessageBoxW(hwnd, question.c_str(), L"Remove character", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
                        return;
                    const auto files = mods::remove_roster_character(mod.folder, slot);
                    refreshMods(int(row));
                    status = L"Removed character ID " + std::to_wstring(slot) + L" from " + wide(mod.name) + L" (" +
                             std::to_wstring(files) + (files == 1 ? L" file" : L" files") + L"). ID " +
                             std::to_wstring(slot) + L" is free for the next import.";
                }
                error = false;
            } catch (const std::exception &e) {
                status = wide(e.what());
                error = true;
            }
        } else if (id == ModCreate) {
            const auto name = utf8(textOf(ModName));
            if (name.find_first_not_of(" \t") == std::string::npos) {
                status = L"Type a name for the new mod.";
                error = true;
                SetFocus(controls[ModName]);
                refreshEnabled();
                return;
            }
            const auto folder = mods::create_mod(mods::default_mods_directory(), name, "", "");
            SetWindowTextW(controls[ModName], L"");
            refreshMods();
            for (size_t i = 0; i < modList.size(); ++i)
                if (modList[i].folder == folder)
                    refreshMods(int(i));
            status = L"Created " + wide(name) + L". Add to it with the import tool, or put files in its folder.";
            error = false;
        } else if (id == OpenImporter)
            openImporter();
        else if (id == SourceGame && game)
            showSource(game, gameRoster, gameIdentity);
        else if (id == SourceGame && !settings.iso.empty())
            loadSource(settings.iso);
        else if (id == SourceLast && !settings.importIso.empty())
            loadSource(settings.importIso);
        else if (id == SourceIso) {
            const auto image = choose(false, true, L"Choose a PS2 ISO to import from");
            if (!image.empty())
                loadSource(image);
        } else if (id == ImportSelected && stageImport())
            importStage();
        else if (id == ImportSelected)
            importCharacter();
        else if ((id == OpenSaves || id == OpenBackups) && disc) {
            auto p = id == OpenSaves || gameSaveList() ? card() : backupPath();
            std::filesystem::create_directories(p);
            ShellExecuteW(hwnd, L"open", p.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
    }
    ~Window() {
        if (task.valid()) task.wait();
        if (updateTask.valid()) updateTask.wait();
        if (child)
            CloseHandle(child);
    }
};
LRESULT CALLBACK save_list_procedure(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
    auto original = reinterpret_cast<WNDPROC>(GetPropW(hwnd, L"OpenKai2.SaveListProcedure"));
    auto *owner = reinterpret_cast<Window *>(GetWindowLongPtrW(GetParent(hwnd), GWLP_USERDATA));
    if (owner && message == WM_KEYDOWN) {
        if (wp == 'A' && (GetKeyState(VK_CONTROL) & 0x8000)) {
            const auto count = SendMessageW(hwnd, LB_GETCOUNT, 0, 0);
            if (count > 0) {
                SendMessageW(hwnd, LB_SELITEMRANGE, TRUE, MAKELPARAM(0, count - 1));
                SendMessageW(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(BackupList, LBN_SELCHANGE), reinterpret_cast<LPARAM>(hwnd));
            }
            return 0;
        }
        if (wp == VK_DELETE && IsWindowEnabled(owner->controls[DeleteSelected])) {
            SendMessageW(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(DeleteSelected, BN_CLICKED), reinterpret_cast<LPARAM>(owner->controls[DeleteSelected]));
            return 0;
        }
    }
    if (message == WM_NCDESTROY) RemovePropW(hwnd, L"OpenKai2.SaveListProcedure");
    return CallWindowProcW(original, hwnd, message, wp, lp);
}
LRESULT CALLBACK button_procedure(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
    auto original = reinterpret_cast<WNDPROC>(GetPropW(hwnd, L"OpenKai2.ButtonProcedure"));
    auto *owner = reinterpret_cast<Window *>(GetWindowLongPtrW(GetParent(hwnd), GWLP_USERDATA));
    if (owner && message == WM_MOUSEMOVE) {
        const int id = GetDlgCtrlID(hwnd);
        if (owner->hotButton != id) {
            owner->hotButton = id;
            TRACKMOUSEEVENT tracking{sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&tracking);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
    } else if (owner && message == WM_MOUSELEAVE) {
        owner->hotButton = 0;
        InvalidateRect(hwnd, nullptr, FALSE);
    } else if (message == WM_SETCURSOR && IsWindowEnabled(hwnd)) {
        SetCursor(LoadCursor(nullptr, IDC_HAND));
        return TRUE;
    }
    return original ? CallWindowProcW(original, hwnd, message, wp, lp)
                    : DefWindowProcW(hwnd, message, wp, lp);
}
LRESULT CALLBACK edit_procedure(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
    auto original = reinterpret_cast<WNDPROC>(GetPropW(hwnd, L"OpenKai2.EditProcedure"));
    auto *owner = reinterpret_cast<Window *>(GetWindowLongPtrW(GetParent(hwnd), GWLP_USERDATA));
    const auto result = original ? CallWindowProcW(original, hwnd, message, wp, lp)
                                 : DefWindowProcW(hwnd, message, wp, lp);
    if (message == WM_PAINT && owner && GetFocus() != hwnd && GetWindowTextLengthW(hwnd) == 0) {
        const auto *hint = reinterpret_cast<const wchar_t *>(GetPropW(hwnd, L"OpenKai2.EditHint"));
        auto dc = GetDC(hwnd);
        RECT rect{};
        GetClientRect(hwnd, &rect);
        rect.left += owner->p(2);
        auto old = SelectObject(dc, owner->fonts.body);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, faint);
        if (hint)
            DrawTextW(dc, hint, -1, &rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        SelectObject(dc, old);
        ReleaseDC(hwnd, dc);
    }
    if (message == WM_SETFOCUS || message == WM_KILLFOCUS)
        InvalidateRect(hwnd, nullptr, TRUE);
    return result;
}
LRESULT CALLBACK combo_procedure(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
    auto original = reinterpret_cast<WNDPROC>(GetPropW(hwnd, L"OpenKai2.ComboProcedure"));
    auto *owner = reinterpret_cast<Window *>(GetWindowLongPtrW(GetParent(hwnd), GWLP_USERDATA));
    if (message == WM_PAINT && owner) {
        PAINTSTRUCT ps;
        auto screen = BeginPaint(hwnd, &ps);
        RECT rect{};
        GetClientRect(hwnd, &rect);
        std::optional<Canvas> canvas(std::in_place, screen, rect);
        auto dc = canvas->dc;
        const bool enabled = IsWindowEnabled(hwnd) != FALSE, focused = GetFocus() == hwnd;
        owner->backdrop(dc, hwnd, rect, panel);
        owner->box(dc, rect, enabled ? field : panel, focused ? orange : enabled ? border : mix(panel, border, 140), 8);
        const auto index = SendMessageW(hwnd, CB_GETCURSEL, 0, 0);
        std::wstring label;
        if (index >= 0) {
            const auto n = SendMessageW(hwnd, CB_GETLBTEXTLEN, index, 0);
            if (n >= 0 && n < 32768) {
                label.resize(size_t(n) + 1);
                SendMessageW(hwnd, CB_GETLBTEXT, index, reinterpret_cast<LPARAM>(label.data()));
                label.resize(size_t(n));
            }
        }
        auto old = SelectObject(dc, owner->fonts.body);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, enabled ? ink : faint);
        RECT text = rect;
        text.left += owner->p(12);
        text.right -= owner->p(34);
        DrawTextW(dc, label.c_str(), int(label.size()), &text,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        SelectObject(dc, old);
        const int x = rect.right - owner->p(18), y = rect.bottom / 2;
        auto pen = CreatePen(PS_SOLID, owner->p(2), enabled ? (focused ? orange : muted) : faint);
        auto previous = SelectObject(dc, pen);
        MoveToEx(dc, x - owner->p(4), y - owner->p(2), nullptr);
        LineTo(dc, x, y + owner->p(2));
        LineTo(dc, x + owner->p(5), y - owner->p(3));
        SelectObject(dc, previous);
        DeleteObject(pen);
        canvas.reset();
        EndPaint(hwnd, &ps);
        return 0;
    }
    if (message == WM_ERASEBKGND)
        return 1;
    if (message == WM_SETCURSOR && IsWindowEnabled(hwnd)) {
        SetCursor(LoadCursor(nullptr, IDC_HAND));
        return TRUE;
    }
    const auto result = original ? CallWindowProcW(original, hwnd, message, wp, lp)
                                 : DefWindowProcW(hwnd, message, wp, lp);
    if (message == WM_SETFOCUS || message == WM_KILLFOCUS || message == WM_ENABLE ||
        message == CB_SETCURSEL)
        InvalidateRect(hwnd, nullptr, FALSE);
    return result;
}
LRESULT CALLBACK procedure(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
    auto *self = reinterpret_cast<Window *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<Window *>(reinterpret_cast<CREATESTRUCTW *>(lp)->lpCreateParams);
        self->hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self)
        return DefWindowProcW(hwnd, message, wp, lp);
    try {
        switch (message) {
        case WM_CREATE:
            self->initialize();
            return 0;
        case WM_PAINT:
            self->paint();
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_DRAWITEM:
            self->drawItem(*reinterpret_cast<DRAWITEMSTRUCT *>(lp));
            return TRUE;
        case WM_MEASUREITEM:
            reinterpret_cast<MEASUREITEMSTRUCT *>(lp)->itemHeight = self->p(34);
            return TRUE;
        case WM_COMMAND:
            self->command(LOWORD(wp), HIWORD(wp));
            return 0;
        case WM_TIMER:
            self->timer();
            return 0;
        case WM_SIZE:
            if (!self->controls.empty())
                self->layout();
            return 0;
        case WM_DROPFILES:
            self->dropped(reinterpret_cast<HDROP>(wp));
            return 0;
        case WM_GETMINMAXINFO: {
            auto *limits = reinterpret_cast<MINMAXINFO *>(lp);
            limits->ptMinTrackSize = {self->p(1000), self->p(680)};
            return 0;
        }
        case WM_DPICHANGED: {
            self->dpi = HIWORD(wp);
            self->ambient.clear();
            self->fonts.make(self->dpi, self->letteringFace);
            for (auto [id, c] : self->controls)
                SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(self->fonts.body), TRUE);
            auto *r = reinterpret_cast<RECT *>(lp);
            SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            self->layout();
            return 0;
        }
        case WM_CTLCOLOREDIT: {
            SetDCBrushColor(reinterpret_cast<HDC>(wp), field);
            SetTextColor(reinterpret_cast<HDC>(wp), ink);
            SetBkColor(reinterpret_cast<HDC>(wp), field);
            return reinterpret_cast<LRESULT>(GetStockObject(DC_BRUSH));
        }
        case WM_CTLCOLORLISTBOX: {
            const auto target = reinterpret_cast<HWND>(lp);
            const bool list = target == self->controls[BackupList] || target == self->controls[ModList] ||
                              target == self->controls[SourceFiles] || target == self->controls[TargetList];
            const COLORREF color = list ? field : panel;
            SetDCBrushColor(reinterpret_cast<HDC>(wp), color);
            SetTextColor(reinterpret_cast<HDC>(wp), ink);
            SetBkColor(reinterpret_cast<HDC>(wp), color);
            return reinterpret_cast<LRESULT>(GetStockObject(DC_BRUSH));
        }
        case WM_CLOSE:
            if (self->child) {
                self->closing = true;
                ShowWindow(hwnd, SW_HIDE);
                return 0;
            }
            if (self->busy() || self->updating()) {
                self->closing = true;
                self->status = L"Finishing up…";
                return 0;
            }
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            KillTimer(hwnd, 1);
            self->rememberPlacement();
            PostQuitMessage(0);
            return 0;
        }
    } catch (const std::exception &e) {
        self->status = wide(e.what());
        self->error = true;
        std::cerr << "[launcher:error] " << e.what() << std::endl;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    } catch (...) {
        self->status = L"Something went wrong. Details are in launcher.log.";
        self->error = true;
        std::cerr << "[launcher:error] message=" << message << " failed with an unknown error" << std::endl;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wp, lp);
}
}
int run_window(const std::filesystem::path &initialIso) {
    auto singleton = CreateMutexW(nullptr, TRUE, L"Local\\OpenKai2.Launcher");
    if (!singleton)
        throw std::runtime_error("Cannot open the launcher session");
    const bool alreadyRunning = GetLastError() == ERROR_ALREADY_EXISTS;
    struct Singleton {
        HANDLE handle;
        bool owned;
        ~Singleton() { if (owned) ReleaseMutex(handle); CloseHandle(handle); }
    } singletonLock{singleton, !alreadyRunning};
    if (alreadyRunning) {
        if (auto other = FindWindowW(L"OpenKai2.Launcher", nullptr)) {
            ShowWindow(other, SW_RESTORE);
            SetForegroundWindow(other);
        }
        return 0;
    }
    if (auto user = GetModuleHandleW(L"user32.dll"))
        if (auto fn = reinterpret_cast<BOOL(WINAPI *)(HANDLE)>(
                GetProcAddress(user, "SetProcessDpiAwarenessContext")))
            fn(reinterpret_cast<HANDLE>(-4));
    const auto com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    struct Com {
        HRESULT result;
        ~Com() {
            if (SUCCEEDED(result))
                CoUninitialize();
        }
    } cleanup{com};
    DWORD installed = 0;
    auto fontResource = AddFontMemResourceEx(const_cast<unsigned char *>(display_font), DWORD(display_font_size),
                                             nullptr, &installed);
    struct FontCleanup {
        HANDLE handle;
        ~FontCleanup() { if (handle) RemoveFontMemResourceEx(handle); }
    } fontCleanup{fontResource};
    Window state;
    if (!fontResource || !installed)
        state.letteringFace = L"Segoe UI";
    try {
        state.settings = read_settings(settings_path());
    } catch (const std::exception &e) {
        state.status = wide(e.what()) + L". Default settings are in use.";
        state.error = true;
    }
    if (!initialIso.empty())
        state.settings.iso = initialIso;
    if (!state.settings.autoUpdates)
        state.updateStatus = L"Automatic updates are off. Use Check now to look for a release.";
    WNDCLASSEXW type{};
    type.cbSize = sizeof(type);
    type.style = CS_HREDRAW | CS_VREDRAW;
    type.lpfnWndProc = procedure;
    type.hInstance = GetModuleHandleW(nullptr);
    type.lpszClassName = L"OpenKai2.Launcher";
    type.hCursor = LoadCursor(nullptr, IDC_ARROW);
    type.hIcon = static_cast<HICON>(LoadImageW(type.hInstance, MAKEINTRESOURCEW(1), IMAGE_ICON, 0, 0,
                                               LR_DEFAULTSIZE | LR_SHARED));
    type.hIconSm = static_cast<HICON>(LoadImageW(type.hInstance, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                                 GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
                                                 LR_SHARED));
    RegisterClassExW(&type);
    if (auto user = GetModuleHandleW(L"user32.dll"))
        if (auto fn = reinterpret_cast<UINT(WINAPI *)()>(GetProcAddress(user, "GetDpiForSystem")))
            state.dpi = fn();
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int width = std::min<int>(state.p(1120), int(work.right - work.left) - state.p(24));
    int height = std::min<int>(state.p(740), int(work.bottom - work.top) - state.p(24));
    int left = work.left + (work.right - work.left - width) / 2, top = work.top + (work.bottom - work.top - height) / 2;
    const auto &saved = state.settings;
    const RECT last{saved.windowLeft, saved.windowTop, saved.windowLeft + saved.windowWidth, saved.windowTop + saved.windowHeight};
    if (saved.windowWidth > 0 && saved.windowHeight > 0 && MonitorFromRect(&last, MONITOR_DEFAULTTONULL)) {
        left = saved.windowLeft;
        top = saved.windowTop;
        width = saved.windowWidth;
        height = saved.windowHeight;
    }
    auto hwnd = CreateWindowExW(0, type.lpszClassName, L"OpenKai2", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, left, top,
                                width, height, nullptr, nullptr, type.hInstance, &state);
    if (!hwnd)
        throw std::runtime_error("Cannot open the launcher window");
    if (auto module = LoadLibraryW(L"dwmapi.dll")) {
        if (auto fn = reinterpret_cast<HRESULT(WINAPI *)(HWND, DWORD, LPCVOID, DWORD)>(
                GetProcAddress(module, "DwmSetWindowAttribute"))) {
            BOOL dark = TRUE;
            fn(hwnd, 20, &dark, sizeof(dark));
            COLORREF caption = sidebar;
            fn(hwnd, 35, &caption, sizeof(caption));
        }
        FreeLibrary(module);
    }
    if (auto module = LoadLibraryW(L"uxtheme.dll")) {
        if (auto fn = reinterpret_cast<HRESULT(WINAPI *)(HWND, LPCWSTR, LPCWSTR)>(GetProcAddress(module, "SetWindowTheme")))
            for (auto [id, control] : state.controls)
                fn(control, L"DarkMode_Explorer", nullptr);
        FreeLibrary(module);
    }
    ShowWindow(hwnd, state.settings.windowMaximized ? SW_SHOWMAXIMIZED : SW_SHOW);
    DragAcceptFiles(hwnd, TRUE);
    UpdateWindow(hwnd);
    if (!state.settings.iso.empty())
        state.start(Job::verify, state.settings.iso);
    ACCEL shortcuts[] = {{FCONTROL | FVIRTKEY, '1', PlayTab},
                         {FCONTROL | FVIRTKEY, '2', SavesTab},
                         {FCONTROL | FVIRTKEY, '3', ModsTab},
                         {FCONTROL | FVIRTKEY, '4', SettingsTab},
                         {FVIRTKEY, VK_F5, Play}};
    const auto accelerators = CreateAcceleratorTableW(shortcuts, int(std::size(shortcuts)));
    struct AcceleratorCleanup {
        HACCEL value;
        ~AcceleratorCleanup() { if (value) DestroyAcceleratorTable(value); }
    } shortcutCleanup{accelerators};
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!(accelerators && TranslateAcceleratorW(hwnd, accelerators, &message)) &&
            !IsDialogMessageW(hwnd, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    std::cout << "[launcher] closed" << std::endl;
    return 0;
}
}
