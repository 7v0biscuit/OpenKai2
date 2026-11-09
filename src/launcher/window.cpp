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
#include "speedrun.h"
#include "dev_identity.h"
#include "netplay.h"
#include "room.h"
#include "online.h"
#include <deque>
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
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <commctrl.h>
#include <commdlg.h>
#include <fstream>
#include <functional>
#include <future>
#include <iostream>
#include <map>
#include <utility>
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
COLORREF background = RGB(9, 11, 19), sidebar = RGB(12, 14, 24), panel = RGB(17, 20, 33);
COLORREF field = RGB(22, 26, 42), raised = RGB(29, 34, 53), hover = RGB(39, 45, 69);
COLORREF border = RGB(35, 41, 61), ink = RGB(242, 238, 230), muted = RGB(148, 153, 174);
COLORREF faint = RGB(86, 93, 117), orange = RGB(255, 136, 28), gold = RGB(255, 204, 86);
COLORREF red = RGB(255, 112, 102), green = RGB(98, 214, 152);
COLORREF ember = RGB(255, 112, 24);
COLORREF sky = RGB(70, 110, 255);
COLORREF bannerTitle = RGB(255, 240, 222), bannerText = RGB(206, 176, 150), titleShadow = RGB(6, 7, 12);
COLORREF ballColor = RGB(255, 145, 26), ballStars = RGB(220, 40, 28), bannerLabel = RGB(255, 204, 86);
COLORREF heroFrom = RGB(64, 24, 8), heroTo = RGB(15, 17, 31);
COLORREF buttonTop = RGB(255, 186, 70), buttonBottom = RGB(250, 126, 22), buttonHotTop = RGB(255, 214, 110),
         buttonHotBottom = RGB(255, 150, 44), buttonPressedTop = RGB(236, 120, 22), buttonPressedBottom = RGB(214, 96, 14),
         buttonShine = RGB(255, 245, 214), buttonInk = RGB(36, 16, 4);
COLORREF colorOf(Rgb c) { return RGB(c.r, c.g, c.b); }
void apply_theme(unsigned choice, unsigned accent, const CustomColors &custom) {
    const auto t = make_theme(choice, accent, custom, GetTickCount64());
    background = colorOf(t.background); sidebar = colorOf(t.sidebar); panel = colorOf(t.panel);
    field = colorOf(t.field); raised = colorOf(t.raised); hover = colorOf(t.hover); border = colorOf(t.border);
    ink = colorOf(t.ink); muted = colorOf(t.muted); faint = colorOf(t.faint);
    orange = colorOf(t.accent); gold = colorOf(t.highlight); ember = colorOf(t.glow);
    heroFrom = colorOf(t.heroFrom); heroTo = colorOf(t.heroTo);
    buttonTop = colorOf(t.buttonTop); buttonBottom = colorOf(t.buttonBottom);
    buttonHotTop = colorOf(t.buttonHotTop); buttonHotBottom = colorOf(t.buttonHotBottom);
    buttonPressedTop = colorOf(t.buttonPressedTop); buttonPressedBottom = colorOf(t.buttonPressedBottom);
    buttonShine = colorOf(t.buttonShine); buttonInk = colorOf(t.buttonInk);
    red = colorOf(t.error); green = colorOf(t.success); sky = colorOf(t.info);
    bannerTitle = colorOf(t.bannerTitle); bannerText = colorOf(t.bannerText); titleShadow = colorOf(t.shadow);
    ballColor = colorOf(t.ball); ballStars = colorOf(t.ballStars); bannerLabel = colorOf(t.bannerLabel);
}

constexpr int sidebarWidth = 220, contentLeft = 252, margin = 32;
constexpr int heroTop = 124, heroBottom = 316, discTop = 332, discBottom = 420;
constexpr int tilesTop = 436, tilesBottom = 516, keysTop = 540;
constexpr int speedrunTop = 112, splitsTop = 284, speedrunRulesTop = 526;
constexpr int onlineProfileTop = 112, onlineHostTop = 228, onlineListsTop = 400;
constexpr UINT onlineChanged = WM_APP + 30, onlineNotice = WM_APP + 31;

enum Id {
    PlayTab = 101,
    OnlineTab,
    SpeedrunTab,
    ModsTab,
    SavesTab,
    SettingsTab,
    UpdatesTab,
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
    CpuDifficulty,
    InternalScale,
    HdTextures,
    Music,
    UpdateChannel = 160,
    AutomaticUpdates,
    CheckUpdates,
    InstallUpdate,
    ReleasePage,
    ResetGraphics = 170,
    TextureFolder = 171,
    ClearImage = 173,
    DiscordActivity = 174,
    SaveListKind = 175,
    SelectSaves = 176,
    SpeedrunMode = 177,
    Permadeath = 178,
    FpsCounter = 179,
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
    SourceLast = 221,
    OpenOverlay = 222,
    CloseOverlay,
    OverlayItems,
    OverlayToggle,
    OverlayCorner,
    OverlaySize,
    OverlayBackground,
    OverlaySplits,
    OverlayDefaults,
    OverlayText,
    OverlayAhead,
    OverlayBehind,
    OverlayGold,
    OverlayDecimals,
    SpeedrunCpu,
    OnlineName = 240,
    OnlineDelay,
    OnlinePort,
    OnlineAddress,
    HostGame,
    JoinGame,
    OnlineProfile,
    ProfileAdd,
    ProfileRemove,
    OnlineRoom,
    OnlinePassword,
    OnlineListed,
    OnlineLobby,
    RefreshRooms,
    RoomList,
    JoinPassword,
    JoinRoom,
    PlayerList,
    InvitePlayer,
    AddFriend,
    NoticeAccept,
    NoticeDecline,
    FriendList,
    SplitList,
    SplitTime,
    SplitSet,
    SplitAdd,
    SplitRemove,
    SplitReset,
    TimingRules = 290,
    OverlayMargin,
    OverlayShape,
    OverlayTitle,
    OverlayNames,
    TabList,
    TabUp,
    TabDown,
    TabToggle,
    TabDefaults,
    CloseTabs,
    ArrangeTabs,
    LauncherTheme,
    LauncherAccent,
    StartPage,
    ShortcutFirst,
    ShortcutLast = ShortcutFirst + 6,
    ColorList,
    ColorPick,
    ColorReset,
    ColorResetAll,
    CloseColors,
    OpenColors,
    RandomizerMode,
    SpeedrunRandomizer,
    ColorCopy,
    ColorPaste,
    ColorSurprise,
    OpenLogs
};
constexpr int tabStep = 52, tabHeight = 46;
constexpr int pageCount = 7, onlinePage = 1, speedrunPage = 2, modsPage = 3, savesPage = 4, settingsPage = 5, updatesPage = 6,
              importPage = 7, overlayPage = 8, tabsPage = 9, colorsPage = 10;
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
                    const double br = GetRValue(ballColor), bg = GetGValue(ballColor), bb = GetBValue(ballColor);
                    double cr = br * (150 + 105 * diffuse) / 255 + 40 * through * br / 255,
                           cg = bg * (50 + 95 * diffuse) / 145 + 60 * through * bg / 145,
                           cb = bb * (6 + 20 * diffuse) / 26 + 20 * through * bb / 26;
                    if (inStar(nx, ny, -0.2, 0.02, 0.28) || inStar(nx, ny, 0.25, 0.26, 0.28)) {
                        cr = GetRValue(ballStars) * (170 + 50 * diffuse) / 220;
                        cg = GetGValue(ballStars) * (24 + 16 * diffuse) / 40;
                        cb = GetBValue(ballStars) * (18 + 10 * diffuse) / 28;
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
        wordmark = font(44, FW_NORMAL, lettering);
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
struct SettingsGrid : Grid {
    int header, rowHeight, gap, controlHeight, fieldGap;
    int displayTop = 112, graphicsTop, gameTop, updatesTop;
    SettingsGrid(int width, int height) : Grid(width) {
        const bool compact = height < 778, tight = height < 700;
        header = tight ? 34 : compact ? 40 : 44;
        rowHeight = tight ? 44 : compact ? 48 : 56;
        gap = tight ? 6 : compact ? 8 : 10;
        controlHeight = tight ? 28 : compact ? 30 : 34;
        fieldGap = tight ? 15 : compact ? 16 : 18;
        gameTop = bottom(displayTop, 2) + gap;
        graphicsTop = bottom(gameTop, 2) + gap;
        updatesTop = bottom(graphicsTop, 2) + gap;
    }
    int bottom(int top, int rows) const { return top + header + rows * rowHeight + 4; }
    int label(int top, int row) const { return top + header + row * rowHeight; }
    int field(int top, int row) const { return label(top, row) + fieldGap; }
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
    bool cycling = false;
    unsigned long long ambientAt = 0;
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
    std::wstring onlineArguments;
    std::unique_ptr<netplay::Room> room;
    bool roomHost = false;
    bool directJoin = false;
    std::string joinAddress, joinRoomId;
    uint16_t roomPort = 0;
    struct Notice {
        bool invite = true;
        online::Invite inviteMessage;
        online::FriendMessage friendMessage;
    };
    struct OnlineShared {
        std::mutex mutex;
        std::deque<Notice> notices;
        std::map<std::string, int> pings;
        std::set<std::string> pinging;
        HWND window{};
    };
    std::shared_ptr<OnlineShared> onlineShared = std::make_shared<OnlineShared>();
    std::unique_ptr<online::Directory> directory;
    std::future<std::pair<std::unique_ptr<online::Directory>, std::string>> connecting;
    std::chrono::steady_clock::time_point lastConnect{};
    std::string connectedProfile, onlineProblem;
    std::vector<online::Room> roomRows;
    std::vector<std::pair<std::string, std::string>> playerRows, friendRows;
    std::set<std::string> requestedFriends;
    std::string hostingRoom, hostingToken, pendingInvitee;
    std::chrono::steady_clock::time_point inviteDeadline{};
    bool trayAdded = false, wasRunning = false;
    int p(int n) const { return MulDiv(n, int(dpi), 96); }
    int unscale(int n) const { return MulDiv(n, 96, int(dpi)); }
    int navigationPage() const {
        return page == importPage ? modsPage : page == overlayPage ? speedrunPage : page == tabsPage || page == colorsPage ? settingsPage : page;
    }
    std::vector<int> visibleTabs() const {
        std::vector<int> shown;
        for (unsigned tab : settings.tabOrder)
            if (!((settings.hiddenTabs >> tab) & 1u))
                shown.push_back(int(tab));
        return shown;
    }
    int friendsTop() const { return 104 + int(visibleTabs().size()) * tabStep + 22; }
    int tabSlot(int tab) const {
        const auto shown = visibleTabs();
        const auto found = std::find(shown.begin(), shown.end(), tab);
        return found == shown.end() ? -1 : int(found - shown.begin());
    }
    bool busy() const { return task.valid(); }
    bool updating() const { return updateTask.valid(); }
    bool running() const { return child || externalGame; }
    bool dropdownOpen() const {
        for (auto [id, control] : controls)
            if ((id >= Renderer && id <= Music) || id == UpdateChannel || id == AutomaticUpdates ||
                id == SaveListKind || id == TargetMod || id == SpotMode ||
                (id >= SpeedrunMode && id <= FpsCounter) || (id >= OverlayCorner && id <= OverlaySplits) || (id >= OverlayText && id <= SpeedrunCpu) || id == OnlineDelay || id == OnlineProfile || id == OnlineListed)
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
        const COLORREF from = pressed ? buttonPressedTop : hot ? buttonHotTop : buttonTop;
        const COLORREF to = pressed ? buttonPressedBottom : hot ? buttonHotBottom : buttonBottom;
        const int height = std::max(1L, r.bottom - r.top);
        if (cutShape(dc, r, cut(radius), [&](int, int y) {
                return y == r.top ? mix(from, buttonShine, 150) : mix(from, to, (y - r.top) * 255 / height);
            }, std::nullopt))
            return;
        auto region = cutRegion(r, cut(radius));
        SelectClipRgn(dc, region);
        const COLORREF top = pressed ? buttonPressedTop : hot ? buttonHotTop : buttonTop;
        const COLORREF bottom = pressed ? buttonPressedBottom : hot ? buttonHotBottom : buttonBottom;
        const int span = std::max(1L, r.bottom - r.top);
        for (int y = r.top; y < r.bottom; ++y)
            fill(dc, {r.left, y, r.right, y + 1}, mix(top, bottom, (y - r.top) * 255 / span));
        fill(dc, {r.left, r.top, r.right, r.top + 1}, mix(top, buttonShine, 150));
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
    speedrun::SplitBook splitBook;
    std::wstring textOf(int id) const {
        const int n = GetWindowTextLengthW(controls.at(id));
        std::wstring value(size_t(n) + 1, 0);
        GetWindowTextW(controls.at(id), value.data(), n + 1);
        value.resize(size_t(n));
        return value;
    }
    int selected(int id) const { return int(SendMessageW(controls.at(id), CB_GETCURSEL, 0, 0)); }
    struct Placement {
        int x, y, w, h;
        bool visible;
    };
    std::map<int, Placement> *planning = nullptr;
    void conceal(int id) {
        if (planning) {
            if (auto found = planning->find(id); found != planning->end())
                found->second.visible = false;
        } else
            ShowWindow(controls.at(id), SW_HIDE);
    }
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
        if (planning) {
            (*planning)[id] = Placement{p(x), p(y), p(w), allocatedHeight, visible};
            return;
        }
        SetWindowPos(c, nullptr, p(x), p(y), p(w), allocatedHeight,
                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
        const bool shown = (GetWindowLongPtrW(c, GWL_STYLE) & WS_VISIBLE) != 0;
        if (visible != shown)
            ShowWindow(c, visible ? SW_SHOWNA : SW_HIDE);
    }
    void initialize() {
        apply_theme(settings.launcherTheme, settings.launcherAccent, settings.launcherColors);
        if (settings.startPage && settings.lastPage < tab_count && !((settings.hiddenTabs >> settings.lastPage) & 1u))
            page = int(settings.lastPage);
        if (!settings.discordPresence) {
            settings.discordPresence = true;
            try {
                write_settings(settings_path(), settings);
            } catch (const std::exception &) {
            }
        }
        presence.enable(true);
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
                                                          {UpdatesTab, L"Updates"},
                                                          {OnlineTab, L"Online"},
                                                          {SpeedrunTab, L"Speedrun"},
                                                          {HostGame, L"Host room"},
                                                          {JoinGame, L"Join address"},
                                                          {ProfileAdd, L"Add"},
                                                          {ProfileRemove, L"Remove"},
                                                          {RefreshRooms, L"Refresh"},
                                                          {JoinRoom, L"Join room"},
                                                          {InvitePlayer, L"Invite"},
                                                          {AddFriend, L"Add friend"},
                                                          {NoticeAccept, L"Accept"},
                                                          {NoticeDecline, L"Decline"},
                                                          {CheckUpdates, L"Check now"},
                                                          {InstallUpdate, L"Install"},
                                                          {ReleasePage, L"Release notes"},
                                                          {ResetGraphics, L"Defaults"},
                                                          {TextureFolder, L"Open texture folder"},
                                                          {OpenOverlay, L"Customize overlay…"},
                                                          {CloseOverlay, L"Back to speedrun"},
                                                          {CloseTabs, L"Back to settings"},
                                                          {TabUp, L"Move up"},
                                                          {TabDown, L"Move down"},
                                                          {TabToggle, L"Show / hide"},
                                                          {TabDefaults, L"Restore order"},
                                                          {ArrangeTabs, L"Arrange tabs…"},
                                                          {OpenColors, L"Colors…"},
                                                          {ColorPick, L"Change color…"},
                                                          {ColorReset, L"Use theme color"},
                                                          {ColorResetAll, L"Reset all colors"},
                                                          {CloseColors, L"Back to settings"},
                                                          {ColorCopy, L"Copy colors"},
                                                          {ColorPaste, L"Paste colors"},
                                                          {ColorSurprise, L"Surprise me"},
                                                          {OpenLogs, L"Open logs folder"},
                                                          {OverlayToggle, L"Show / hide"},
                                                          {OverlayDefaults, L"Restore defaults"},
                                                          {ClearImage, L"Crisp"},
                                                          {ChooseIso, L"Choose ISO…"},
                                                          {VerifyIso, L"Verify"},
                                                          {Play, L"Play"},
                                                          {ImportFolder, L"Import folder…"},
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
        combo(Renderer, {L"Automatic", L"OpenGL (GPU)", L"Vulkan (GPU)", L"Software (CPU)"},
              settings.renderer == "gpu"      ? 1
              : settings.renderer == "vulkan" ? 2
              : settings.renderer == "cpu"    ? 3
                                              : 0);
        combo(Fps, fps, int(std::find(rates.begin(), rates.end(), settings.fps) - rates.begin()));
        combo(Resolution, resolutions,
              int(std::find(sizes.begin(), sizes.end(), size) - sizes.begin()));
        combo(WindowMode, {L"Windowed", L"Borderless fullscreen"}, int(settings.windowMode));
        combo(Antialiasing, {L"Off", L"FXAA", L"FXAA High", L"FXAA Ultra"}, int(std::min(settings.antialiasing, 3u)));
        combo(Filtering, {L"Game default", L"Nearest (sharp)", L"Bilinear (3D surfaces)"},
              int(settings.textureFilter));
        combo(Upscaler, {L"Nearest", L"Bilinear", L"Bicubic", L"AMD FSR 1", L"Lanczos", L"Sharp pixels"}, int(settings.upscaler));
        combo(InternalScale, {L"Native (1x)", L"2x native", L"3x native", L"4x native"},
              int(std::clamp(settings.internalScale, 1u, 4u) - 1u));
        combo(HdTextures, {L"Off", L"Upscaled from your disc", L"Texture packs", L"Texture packs, upscale the rest", L"Dump textures for pack makers"},
              int(std::min(settings.hdTextures, 4u)));
        combo(Music, {L"On", L"Off (voices and sound effects stay on)"}, settings.music ? 0 : 1);
        combo(Vsync, {L"Off", L"On"}, settings.vsync ? 1 : 0);
        combo(AutoBackup, {L"Off", L"On"}, settings.autoBackups ? 1 : 0);
        combo(SaveListKind,{L"Backups",L"Game saves"},0);
        {
            std::vector<std::wstring> categories;
            for (unsigned i = 0; i < speedrun::category_count; ++i)
                categories.push_back(wide(speedrun::category_name(speedrun::Category(i))));
            combo(SpeedrunMode, categories, int(settings.speedrun));
            SendMessageW(controls[SpeedrunMode], CB_SETDROPPEDWIDTH, p(270), 0);
        }
        combo(Permadeath, {L"Off", L"On"}, settings.permadeath && !speedrun::tournament(speedrun::Category(settings.speedrun)) ? 1 : 0);
        combo(FpsCounter, {L"Off", L"On"}, settings.fpsCounter ? 1 : 0);
        {
            std::vector<std::wstring> delays{L"Auto (from ping)"};
            for (unsigned d = 1; d <= 8; ++d)
                delays.push_back(std::to_wstring(d) + (d == 1 ? L" frame" : L" frames"));
            combo(OnlineDelay, delays, int(settings.onlineDelay));
        }
        combo(OnlineListed, {L"Public", L"Invite only", L"Not listed"}, int(settings.onlineVisibility));
        combo(OnlineProfile, {}, 0);
        fillProfiles();
        combo(OverlayCorner, {L"Top left", L"Top right", L"Bottom left", L"Bottom right"}, int(settings.overlayCorner));
        combo(OverlaySize, {L"Small", L"Medium", L"Large", L"Extra large"}, int(std::min(settings.overlaySize, 3u)));
        combo(OverlayBackground, {L"None", L"Light", L"Dark", L"Faint", L"Solid"}, int(std::min(settings.overlayBackground, 4u)));
        combo(OverlayMargin, {L"Tight", L"Normal", L"Wide"}, int(std::min(settings.overlayMargin, 2u)));
        combo(OverlayShape, {L"Rounded", L"Square"}, int(std::min(settings.overlayShape, 1u)));
        edit(OverlayTitle, L"Title, like My Any% run");
        SetWindowTextW(controls[OverlayTitle], wide(settings.overlayTitle).c_str());
        SendMessageW(controls[OverlayTitle], EM_LIMITTEXT, 48, 0);
        edit(OverlayNames, L"Split names, separated by commas");
        SetWindowTextW(controls[OverlayNames], wide(settings.overlaySplitNames).c_str());
        SendMessageW(controls[OverlayNames], EM_LIMITTEXT, 600, 0);
        combo(OverlaySplits, {L"3 splits", L"5 splits", L"8 splits", L"12 splits"}, int(settings.overlaySplits));
        {
            std::vector<std::wstring> colors;
            for (const auto &color : speedrun::overlay_colors)
                colors.push_back(color.name);
            const int roles[] = {OverlayText, OverlayAhead, OverlayBehind, OverlayGold};
            for (unsigned role = 0; role < speedrun::overlay_color_roles; ++role)
                combo(roles[role], colors, int(std::min(settings.overlayColors[role], speedrun::overlay_color_count - 1u)));
        }
        combo(OverlayDecimals, {speedrun::overlay_decimal_names[0], speedrun::overlay_decimal_names[1],
                                speedrun::overlay_decimal_names[2], speedrun::overlay_decimal_names[3]},
              int(std::min(settings.overlayDecimals, 3u)));
        combo(SpeedrunCpu, {L"Game setting", L"Z1", L"Z2", L"Z3", L"Z4", L"Z5", L"Z6"}, int(std::min(settings.cpuDifficulty, 6u)));
        list(SplitList, L"Saved splits", 30, false);
        edit(SplitTime, L"Time, like 12:34.56");
        button(SplitSet, L"Set time");
        button(SplitAdd, L"Add split");
        button(SplitRemove, L"Remove");
        button(SplitReset, L"Reset best");
        refreshSplits();
        timingRulesBase =
            L"• The timer starts when Dragon World starts and stops on the last hit on Kid Buu.\r\n\r\n"
            L"• Real time is the leaderboard time. PS2 time counts the console's frames, so slowdown is not added.\r\n\r\n"
            L"• Any% starts with default equipment. NG+ allows skills set up before Dragon World.\r\n\r\n"
            L"• Permadeath: being knocked out in a Dragon World fight, or losing one on time, ends the run.\r\n\r\n"
            L"• Z1 to Z6 make the CPU stronger and are locked for the whole run; F6 is off in speedrun mode.\r\n\r\n"
            L"• Each battle won is a split. Best runs and best segments are saved after every attempt.\r\n\r\n"
            L"• Runs are flagged if the disc differs from the US retail disc, the game runs fast, or a memory editor is open.\r\n\r\n"
            L"• Each session writes a checksummed log to logs\\speedruns for speedrun.com moderators.";
        controls[TimingRules] = CreateWindowExW(0, L"EDIT", L"",
            WS_CHILD | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
            0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(intptr_t(TimingRules)), nullptr, nullptr);
        SendMessageW(controls[TimingRules], EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);
        refreshTimingRules();
        combo(Aspect, {L"4:3 (original)", L"16:9 (widescreen)", L"Stretch to window", L"Native pixels"},
              int(settings.aspect));
        combo(Audio, {L"Off", L"100% volume", L"75% volume", L"50% volume", L"25% volume"},
              audioChoice(settings));
        combo(CpuDifficulty, {L"Game setting", L"Z1", L"Z2", L"Z3", L"Z4", L"Z5", L"Z6"}, int(std::min(settings.cpuDifficulty, 6u)));
        combo(UpdateChannel, {L"Stable", L"Stable and prerelease"}, int(settings.updateChannel));
        {
            std::vector<std::wstring> names, colors;
            for (const auto &theme : themes)
                names.push_back(theme.name);
            for (const auto &accent : accents)
                colors.push_back(accent.name);
            combo(LauncherTheme, names, int(std::min(settings.launcherTheme, theme_count - 1u)));
            combo(LauncherAccent, colors, int(std::min(settings.launcherAccent, accent_count - 1u)));
        }
        combo(StartPage, {L"Play", L"Last page used"}, int(std::min(settings.startPage, 1u)));
        combo(RandomizerMode, {L"Off", L"On"}, settings.randomizer ? 1 : 0);
        combo(SpeedrunRandomizer, {L"Off", L"On"}, settings.randomizer ? 1 : 0);
        list(TabList, L"Tabs", 40, false);
        list(ColorList, L"Colors", 36, false);
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
        list(OverlayItems, L"Overlay items", 36, false);
        refreshOverlayItems();
        list(ModContents, L"Mod contents", 30, false);
        list(SourceFiles, L"Source files", 30, false);
        list(TargetList, L"Import target", 30, false);
        edit(ModName, L"New mod name");
        edit(EntryFilter, L"Search characters");
        edit(OnlineName, L"Name for a new profile");
        edit(OnlinePort, L"7350");
        edit(OnlineAddress, L"Or type an address (IP or IP:port)");
        edit(OnlineRoom, L"Room name");
        edit(OnlinePassword, L"No password");
        edit(OnlineLobby, L"Server address");
        edit(JoinPassword, L"Only for locked rooms");
        list(RoomList, L"Rooms", 34, false);
        list(PlayerList, L"Players", 34, false);
        list(FriendList, L"Friends", 32, false);
        onlineShared->window = hwnd;
        SetWindowTextW(controls[OnlinePort], std::to_wstring(settings.onlinePort).c_str());
        SetWindowTextW(controls[OnlineAddress], wide(settings.onlineAddress).c_str());
        SetWindowTextW(controls[OnlineRoom], wide(settings.onlineRoom).c_str());
        SetWindowTextW(controls[OnlinePassword], wide(settings.onlinePassword).c_str());
        SetWindowTextW(controls[OnlineLobby], wide(settings.onlineLobby).c_str());
        for (auto [id, limit] : {std::pair{OnlineName, 24}, {OnlinePort, 5}, {OnlineAddress, 255}, {OnlineRoom, 40},
                                 {OnlinePassword, 64}, {OnlineLobby, 255}, {JoinPassword, 64}})
            SendMessageW(controls[id], EM_SETLIMITTEXT, WPARAM(limit), 0);
        for (auto [id, c] : controls)
            SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(fonts.body), TRUE);
        SendMessageW(controls[TimingRules], WM_SETFONT, reinterpret_cast<WPARAM>(fonts.caption), TRUE);
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
        for (int id : {Play, ImportFolder, ImportFile, Backup, OpenSaves, OpenBackups, UnlockAll, HostGame, JoinGame, JoinRoom})
            EnableWindow(controls[id], active && (!room || id == (roomHost ? HostGame : directJoin ? JoinGame : JoinRoom)));
        SetWindowTextW(controls[HostGame], room && roomHost ? L"Close room" : L"Host room");
        SetWindowTextW(controls[JoinRoom], room && !roomHost && !directJoin ? L"Cancel" : L"Join room");
        SetWindowTextW(controls[JoinGame], room && !roomHost && directJoin ? L"Cancel" : L"Join address");
        EnableWindow(controls[ProfileRemove], !settings.onlineProfiles.empty());
        const bool online = directory && directory->connected();
        EnableWindow(controls[InvitePlayer], online && disc.has_value() && !busy());
        EnableWindow(controls[AddFriend], online);
        EnableWindow(controls[RefreshRooms], true);
        const auto selections = selectedSaveRows().size();
        EnableWindow(controls[Restore], active && !gameSaveList() && selections == 1u);
        EnableWindow(controls[DeleteSelected], active && selections != 0u);
        const auto deleteLabel = selections ? L"Delete (" + std::to_wstring(selections) + L")" : L"Delete selected";
        SetWindowTextW(controls[DeleteSelected], deleteLabel.c_str());
        EnableWindow(controls[SaveListKind], !busy());
        EnableWindow(controls[SelectSaves], !busy() && !emptySaveList());
        const auto rowCount = SendMessageW(controls[BackupList], LB_GETCOUNT, 0, 0);
        SetWindowTextW(controls[SelectSaves], rowCount > 0 && selections == size_t(rowCount) ? L"Deselect all" : L"Select all");
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
        EnableWindow(controls[Permadeath], settings.speedrun != 0 && !speedrun::tournament(speedrun::Category(settings.speedrun)));
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
        std::vector<int> order{PlayTab, OnlineTab, SpeedrunTab, ModsTab, SavesTab, SettingsTab, UpdatesTab};
        std::map<int, Placement> plan;
        planning = &plan;
        auto position = [&](int id, int x, int y, int width, int height) {
            place(id, x, y, width, height);
            order.push_back(id);
        };
        for (int i = 0; i < pageCount; ++i) {
            const int slot = tabSlot(i);
            place(PlayTab + i, 12, 104 + std::max(slot, 0) * tabStep, sidebarWidth - 24, tabHeight, slot >= 0);
        }
        if (!friendRows.empty())
            position(FriendList, 12, friendsTop() + 30, sidebarWidth - 24, std::max(32, h - 56 - friendsTop() - 30));
        if (hasNotice()) {
            position(NoticeDecline, contentLeft + content - 96, 64, 96, 30);
            position(NoticeAccept, contentLeft + content - 96 - 10 - 96, 64, 96, 30);
        }
        if (page == 0) {
            position(Play, contentLeft + 28, heroBottom - 70, 168, 46);
            position(VerifyIso, right - 20 - 96, discTop + 26, 96, 36);
            position(ChooseIso, right - 20 - 96 - 12 - 132, discTop + 26, 132, 36);
        } else if (page == savesPage) {
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
                conceal(BackupList);
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
                    conceal(id);
            int x = contentLeft + 20;
            for (auto [id, width] : {std::pair{ModToggle, 150}, {ModUp, 100}, {ModDown, 110}, {ModRefresh, 90}}) {
                position(id, x, panelBottom - 52, width, 36);
                x += width + 10;
            }
            const int name = std::max(120, right - 20 - 120 - 10 - (x + 6));
            position(ModName, right - 20 - 120 - 10 - name + 10, panelBottom - 44, name - 20, 22);
            position(ModCreate, right - 20 - 120, panelBottom - 52, 120, 36);
        } else if (page == speedrunPage) {
            const Grid grid(content);
            const bool compact = h < 700;
            const int rulesTop = compact ? 496 : speedrunRulesTop;
            const int buttonHeight = compact ? 30 : 34;
            position(SpeedrunMode, grid.x(0), Grid::field(speedrunTop, 0), grid.column, 34);
            position(Permadeath, grid.x(1), Grid::field(speedrunTop, 0), grid.column, 34);
            position(SpeedrunCpu, grid.x(0), Grid::field(speedrunTop, 1), grid.column, 34);
            position(SpeedrunRandomizer, grid.x(2), Grid::field(speedrunTop, 0), grid.column, 34);
            position(OpenOverlay, right - 20 - 170, speedrunTop + 10, 170, 28);
            const int listWidth = (content - 40) * 58 / 100, side = contentLeft + 20 + listWidth + 16;
            const int sideWidth = right - 20 - side, half = (sideWidth - 10) / 2;
            position(SplitList, contentLeft + 20, splitsTop + 66, listWidth, rulesTop - 12 - 40 - splitsTop - 66);
            if (splitBook.best().empty()) conceal(SplitList);
            position(SplitTime, side, splitsTop + (compact ? 78 : 92), sideWidth, 22);
            position(SplitSet, side, splitsTop + (compact ? 108 : 124), half, buttonHeight);
            position(SplitAdd, side + half + 10, splitsTop + (compact ? 108 : 124), sideWidth - half - 10, buttonHeight);
            position(SplitRemove, side, splitsTop + (compact ? 146 : 166), half, buttonHeight);
            position(SplitReset, side + half + 10, splitsTop + (compact ? 146 : 166), sideWidth - half - 10, buttonHeight);
            position(TimingRules, contentLeft + 20, rulesTop + 42, content - 40,
                     std::max(28, h - 58 - rulesTop - 54));
        } else if (page == onlinePage) {
            const Grid grid(content);
            const int half = (grid.column - 10) / 2, bottom = h - 64;
            position(OnlineProfile, grid.x(0), Grid::field(onlineProfileTop, 0), grid.column, 34);
            position(OnlineName, grid.x(1), Grid::field(onlineProfileTop, 0) + 6, grid.column, 22);
            position(ProfileAdd, grid.x(2), Grid::field(onlineProfileTop, 0), half, 34);
            position(ProfileRemove, grid.x(2) + half + 10, Grid::field(onlineProfileTop, 0), half, 34);
            position(OnlineRoom, grid.x(0), Grid::field(onlineHostTop, 0) + 6, grid.column, 22);
            position(OnlinePassword, grid.x(1), Grid::field(onlineHostTop, 0) + 6, grid.column, 22);
            position(OnlineDelay, grid.x(0), Grid::field(onlineHostTop, 1), grid.column, 34);
            position(OnlineListed, grid.x(1), Grid::field(onlineHostTop, 1), grid.column, 34);
            position(HostGame, grid.x(2), Grid::field(onlineHostTop, 1), grid.column, 36);
            const int split = contentLeft + (content * 62) / 100, roomsRight = split - 6, playersLeft = split + 6;
            const int listTop = onlineListsTop + 44;
            position(RefreshRooms, roomsRight - 20 - 100, onlineListsTop + 8, 100, 30);
            position(RoomList, contentLeft + 20, listTop, roomsRight - contentLeft - 40, std::max(34, bottom - 100 - listTop));
            const int fieldWidth = (roomsRight - contentLeft - 40 - 10) / 2;
            position(JoinPassword, contentLeft + 20, bottom - 88, fieldWidth, 22);
            position(JoinRoom, contentLeft + 30 + fieldWidth, bottom - 94, fieldWidth, 34);
            position(OnlineAddress, contentLeft + 20, bottom - 42, fieldWidth, 22);
            position(JoinGame, contentLeft + 30 + fieldWidth, bottom - 48, fieldWidth, 34);
            const int playersWidth = contentLeft + content - playersLeft - 40;
            position(PlayerList, playersLeft + 20, listTop, playersWidth, std::max(34, bottom - 54 - listTop));
            position(InvitePlayer, playersLeft + 20, bottom - 48, (playersWidth - 10) / 2, 34);
            position(AddFriend, playersLeft + 30 + (playersWidth - 10) / 2, bottom - 48, (playersWidth - 10) / 2, 34);
            if (roomRows.empty())
                conceal(RoomList);
            if (playerRows.empty())
                conceal(PlayerList);
        } else if (page == overlayPage) {
            const int panelBottom = h - 64, half = (content - 52) / 2, side = contentLeft + 32 + half;
            position(CloseOverlay, right - 20 - 150, 136, 150, 34);
            position(OverlayItems, contentLeft + 20, 228, half, std::max(120, panelBottom - 68 - 228));
            position(OverlayToggle, contentLeft + 20, panelBottom - 52, 130, 36);
            position(OverlayDefaults, contentLeft + 160, panelBottom - 52, 150, 36);
            const int column = (content - 52 - half - 12) / 2, step = overlayStep(h), height = step < 64 ? 30 : 34;
            const auto row = [&](int index) { return 246 + index * step; };
            position(OverlayCorner, side, row(0), column, height);
            position(OverlaySize, side + column + 12, row(0), column, height);
            position(OverlayBackground, side, row(1), column, height);
            position(OverlaySplits, side + column + 12, row(1), column, height);
            position(OverlayText, side, row(2), column, height);
            position(OverlayAhead, side + column + 12, row(2), column, height);
            position(OverlayBehind, side, row(3), column, height);
            position(OverlayGold, side + column + 12, row(3), column, height);
            position(OverlayDecimals, side, row(4), column, height);
            position(OverlayMargin, side + column + 12, row(4), column, height);
            position(OverlayShape, side, row(5), column, height);
            position(OverlayTitle, side + column + 12, row(5), column, height);
            position(OverlayNames, side, row(6), column * 2 + 12, height);
        } else if (page == updatesPage) {
            const Grid grid(content);
            const int half = (grid.column - 10) / 2;
            position(UpdateChannel, grid.x(0), Grid::field(236, 0), grid.column, 34);
            position(AutomaticUpdates, grid.x(1), Grid::field(236, 0), grid.column, 34);
            position(CheckUpdates, grid.x(2), Grid::field(236, 0), half, 34);
            position(InstallUpdate, grid.x(2) + half + 10, Grid::field(236, 0), grid.column - half - 10, 34);
            position(ReleasePage, right - 20 - 130, 136, 130, 34);
            position(OpenLogs, contentLeft + 20, 470, 170, 34);
        } else if (page == colorsPage) {
            const int panelBottom = h - 64, listWidth = std::min(560, content - 40 - 200);
            position(CloseColors, right - 20 - 150, 136, 150, 34);
            position(ColorList, contentLeft + 20, 228, listWidth, std::max(120, panelBottom - 20 - 228));
            const int side = contentLeft + 20 + listWidth + 20, buttonWidth = std::min(170, right - 20 - side);
            position(ColorPick, side, 228, buttonWidth, 34);
            position(ColorReset, side, 272, buttonWidth, 34);
            position(ColorResetAll, side, 316, buttonWidth, 34);
            position(ColorSurprise, side, 380, buttonWidth, 34);
            position(ColorCopy, side, 424, buttonWidth, 34);
            position(ColorPaste, side, 468, buttonWidth, 34);
        } else if (page == tabsPage) {
            const int panelBottom = h - 64, listWidth = std::min(420, content - 40 - 180);
            position(CloseTabs, right - 20 - 150, 136, 150, 34);
            position(TabList, contentLeft + 20, 228, listWidth, std::min(int(tab_count) * 40 + 4, panelBottom - 20 - 228));
            const int side = contentLeft + 20 + listWidth + 20;
            position(TabUp, side, 228, 150, 34);
            position(TabDown, side, 272, 150, 34);
            position(TabToggle, side, 316, 150, 34);
            position(TabDefaults, side, 380, 150, 34);
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
            const SettingsGrid grid(content, h);
            const int displayTop = grid.displayTop, graphicsTop = grid.graphicsTop,
                      gameTop = grid.gameTop, updatesTop = grid.updatesTop;
            const int half = (grid.column - 10) / 2;
            position(WindowMode, grid.x(0), grid.field(displayTop, 0), grid.column, grid.controlHeight);
            position(Resolution, grid.x(1), grid.field(displayTop, 0), grid.column, grid.controlHeight);
            position(Aspect, grid.x(2), grid.field(displayTop, 0), grid.column, grid.controlHeight);
            position(Fps, grid.x(0), grid.field(displayTop, 1), grid.column, grid.controlHeight);
            position(Vsync, grid.x(1), grid.field(displayTop, 1), grid.column, grid.controlHeight);
            position(Audio, grid.x(2), grid.field(displayTop, 1), grid.column, grid.controlHeight);
            position(Renderer, grid.x(0), grid.field(graphicsTop, 0), grid.column, grid.controlHeight);
            position(Antialiasing, grid.x(1), grid.field(graphicsTop, 0), grid.column, grid.controlHeight);
            position(Filtering, grid.x(2), grid.field(graphicsTop, 0), grid.column, grid.controlHeight);
            position(Upscaler, grid.x(0), grid.field(graphicsTop, 1), grid.column, grid.controlHeight);
            position(InternalScale, grid.x(1), grid.field(graphicsTop, 1), grid.column, grid.controlHeight);
            position(ClearImage, grid.x(2), grid.field(graphicsTop, 1), half, grid.controlHeight);
            position(ResetGraphics, grid.x(2) + half + 10, grid.field(graphicsTop, 1), grid.column - half - 10, grid.controlHeight);
            position(CpuDifficulty, grid.x(0), grid.field(gameTop, 0), grid.column, grid.controlHeight);
            position(HdTextures, grid.x(1), grid.field(gameTop, 0), grid.column, grid.controlHeight);
            position(Music, grid.x(2), grid.field(gameTop, 0), grid.column, grid.controlHeight);
            position(FpsCounter, grid.x(0), grid.field(gameTop, 1), grid.column, grid.controlHeight);
            position(RandomizerMode, grid.x(1), grid.field(gameTop, 1), grid.column, grid.controlHeight);
            position(TextureFolder, right - 20 - 170, gameTop + (grid.header - 28) / 2, 170, 28);
            position(LauncherTheme, grid.x(0), grid.field(updatesTop, 0), grid.column, grid.controlHeight);
            position(LauncherAccent, grid.x(1), grid.field(updatesTop, 0), grid.column, grid.controlHeight);
            position(StartPage, grid.x(2), grid.field(updatesTop, 0), grid.column, grid.controlHeight);
            position(ArrangeTabs, right - 20 - 140, updatesTop + (grid.header - 28) / 2, 140, 28);
            position(OpenColors, right - 20 - 140 - 10 - 100, updatesTop + (grid.header - 28) / 2, 100, 28);

        }
        planning = nullptr;
        if (auto batch = BeginDeferWindowPos(int(controls.size()))) {
            HWND previous = HWND_TOP;
            for (auto id : order) {
                auto control = controls.at(id);
                const auto found = plan.find(id);
                const bool shown = (GetWindowLongPtrW(control, GWL_STYLE) & WS_VISIBLE) != 0;
                UINT flags = SWP_NOACTIVATE | SWP_NOCOPYBITS;
                if (found == plan.end())
                    flags |= SWP_NOMOVE | SWP_NOSIZE;
                else if (found->second.visible != shown)
                    flags |= found->second.visible ? SWP_SHOWWINDOW : SWP_HIDEWINDOW;
                const auto &where = found == plan.end() ? Placement{} : found->second;
                if (batch)
                    batch = DeferWindowPos(batch, control, previous, where.x, where.y, where.w, where.h, flags);
                previous = control;
            }
            for (auto [id, control] : controls)
                if (batch && std::find(order.begin(), order.end(), id) == order.end() &&
                    (GetWindowLongPtrW(control, GWL_STYLE) & WS_VISIBLE))
                    batch = DeferWindowPos(batch, control, nullptr, 0, 0, 0, 0,
                                           SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_HIDEWINDOW);
            if (batch)
                EndDeferWindowPos(batch);
        }
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
        lettering(dc, L"OpenKai2", p(64), p(46), fonts.mark, ink, titleShadow, p(1));
        text(dc, L"Budokai 2 for PC", 64, 52, 150, 18, fonts.caption, muted);
        const int chosen = std::max(tabSlot(navigationPage()), 0);
        const RECT row{p(12), p(104 + chosen * tabStep), p(sidebarWidth - 12), p(104 + tabHeight + chosen * tabStep)};
        const int span = std::max(1L, row.right - row.left);
        cutShape(dc, row, cut(12), [&](int x, int) {
            return mix(mix(sidebar, orange, 58), mix(sidebar, orange, 14), (x - row.left) * 255 / span);
        }, mix(sidebar, orange, 110));
        const RECT accent{row.left + p(6), row.top + p(15), row.left + p(9), row.bottom - p(15)};
        fill(dc, accent, orange);
        buffer.frame.halo(accent, 0, p(10), ember, 110);

        line(dc, p(24), p(friendsTop() - 6), p(sidebarWidth - 48), border);
        caps(dc, L"FRIENDS", 24, friendsTop() + 6, sidebarWidth - 48);
        const size_t onlineFriends = std::count_if(friendRows.begin(), friendRows.end(), [&](const auto &row) {
            return directory && directory->player(row.first).has_value();
        });
        if (!friendRows.empty()) {
            const auto count = std::to_wstring(onlineFriends) + L" online";
            const int width = measure(dc, count, fonts.caption);
            text(dc, count, sidebarWidth - 24 - width, friendsTop() + 4, width, 18, fonts.caption, onlineFriends ? green : faint,
                 DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
        } else
            text(dc, activeProfile() ? L"Add friends from the Players list on the Online page." : L"Add a profile on the Online page to make friends.",
                 24, friendsTop() + 32, sidebarWidth - 48, 40, fonts.caption, muted, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
        text(dc, L"v" + wide(application_version), 24, h - 38, sidebarWidth - 48, 18, fonts.caption, faint);
    }
    void paintHeader(HDC dc, int w, int content) {
        const wchar_t *titles[] = {L"Play", L"Online", L"Speedrun", L"Mods", L"Saves", L"Settings", L"Updates", L"Import tool", L"Speedrun overlay", L"Arrange tabs", L"Colors"};
        const wchar_t *subtitles[] = {
            L"",
            L"Play versus with friends and other players over the internet.",
            L"Dragon World and World Tournament timing, with separate splits and run logs.",
            L"Characters, movesets and stages layered over your disc. Your ISO is never changed.",
            L"Import, back up and restore your memory card.",
            L"Saved automatically. Game settings apply at the next launch.",
            L"New versions of OpenKai2, how they are found and when they install.",
            L"Add characters, stages and movesets from other Dragon Ball Z games. Nothing in Budokai 2 is removed.",
            L"Pick what the in-game overlay shows. Splits, deltas and best times are tracked for you.",
            L"Choose the order of the tabs in the sidebar and hide the ones you do not use.",
            L"Every color in the launcher."};
        lettering(dc, titles[page], p(contentLeft), p(60), fonts.title, ink, titleShadow, p(2));
        if (hasNotice()) {
            const auto message = noticeText();
            const int width = std::min(content - 220, measure(dc, message, fonts.caption) + 40);
            const int left = contentLeft + content - 96 - 10 - 96 - 14 - width;
            panelBox(dc, left, 60, contentLeft + content + 6, 98);
            ellipse(dc, p(left + 14), p(79), p(4), orange);
            text(dc, message, left + 26, 70, width - 30, 18, fonts.caption, ink,
                 DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        } else
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
        buffer.frame.radial(hero, p(contentLeft), p(heroTop), p(260), ember, 34, p(18));

        caps(dc, L"DRAGON BALL Z", contentLeft + 30, heroTop + 22, 300, bannerLabel);
        lettering(dc, L"Budokai 2", p(contentLeft + 28), p(heroTop + 76), fonts.wordmark, bannerTitle,
                  mix(heroFrom, RGB(0, 0, 0), 180), p(2));
        int spec = contentLeft + 30;
        for (const wchar_t *part : {L"PLAYSTATION 2", L"NTSC-U", L"SLUS-20779"}) {
            caps(dc, part, spec, heroTop + 93, 160, bannerText);
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
        text(dc, headline, contentLeft + 214, heroBottom - (detail.empty() ? 59 : 69), content - 400, 22, fonts.strong, bannerTitle);
        text(dc, detail, contentLeft + 214, heroBottom - 47, content - 400, 20, fonts.caption,
             mix(bannerText, RGB(255, 255, 255), 50), DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

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
        const wchar_t *scaling[] = {L"Nearest", L"Bilinear", L"Bicubic", L"FSR 1", L"Lanczos", L"Sharp pixel"};
        const wchar_t *aspects[] = {L"4:3", L"16:9", L"Stretched", L"Native pixels"};
        const std::wstring captions[] = {L"VIDEO", L"DISPLAY", L"SAVES"};
        const std::wstring values[] = {
            settings.renderer == "cpu"      ? L"Software"
            : settings.renderer == "gpu"    ? L"OpenGL"
            : settings.renderer == "vulkan" ? L"Vulkan"
                                            : L"Automatic",
            settings.windowMode ? L"Borderless" : std::to_wstring(settings.width) + L" × " + std::to_wstring(settings.height),
            settings.autoBackups ? L"Auto backup" : L"Manual backup"};
        const std::wstring details[] = {
            std::wstring(scaling[std::min(5u, settings.upscaler)]) + L" upscaling" +
                (settings.antialiasing == 3 ? L", FXAA Ultra" : settings.antialiasing == 2 ? L", FXAA High" : settings.antialiasing ? L", FXAA" : L""),
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
                 {L"WASD", L"Move"}, {L"X", L"Confirm"}, {L"V", L"Back"}, {L"Enter", L"Start"}})
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
    std::wstring timingRulesBase;
    void refreshTimingRules() {
        if (!controls.count(TimingRules))
            return;
        const auto category = speedrun::Category(settings.speedrun);
        const auto intro = wide(speedrun::category_rules(category));
        const auto details = speedrun::tournament(category)
            ? std::wstring(L"F9 starts or restarts the run; F10 records a round split; F11 finishes. Tournament timing is manual. Permadeath is not used. Times show milliseconds.\r\n\r\n"
                           L"Use F10 for intermediate rounds only. Use F11 at the final finish frame. F8 resets without completing the attempt. Category goals and timing require review of your recording.\r\n\r\n"
                           L"Mods and save states are disabled. Each category and Z difficulty keeps separate records. Each session writes a checksummed log to logs") + wchar_t(92) + L"speedruns for moderators."
            : timingRulesBase + L"\r\n\r\nSave and difficulty requirements are applied automatically. Runs use a temporary memory card; your original save is unchanged. Quitting ends the attempt. Category objectives and equipment still require review.";
        SetWindowTextW(controls[TimingRules], (intro + L"\r\n\r\n" + details).c_str());
        SendMessageW(controls[TimingRules], EM_SETSEL, 0, 0);
        SendMessageW(controls[TimingRules], EM_SCROLLCARET, 0, 0);
    }
    std::filesystem::path splitsFile() const {
        return settings_path().parent_path() / "splits" /
               speedrun::splits_name(speedrun::Category(std::min(settings.speedrun, speedrun::category_count - 1)),
                                     settings.permadeath && !speedrun::tournament(speedrun::Category(settings.speedrun)),
                                     std::min(settings.cpuDifficulty, 6u), settings.randomizer);
    }
    std::string splitTime(double seconds) const {
        return speedrun::format_time(seconds, speedrun::tournament(speedrun::Category(settings.speedrun)) ? 3u : 2u);
    }
    std::wstring splitName(size_t i) const {
        if (speedrun::tournament(speedrun::Category(settings.speedrun)))
            return i + 1 == splitBook.best().size() ? std::wstring(L"Final") : L"Round " + std::to_wstring(i + 1);
        return i + 1 == splitBook.best().size() ? std::wstring(L"Kid Buu") : L"Battle " + std::to_wstring(i + 1);
    }
    static constexpr int splitNameColumn = 26;
    static constexpr int splitColumns[3] = {50, 74, 98};
    std::vector<bool> splitGold;
    void refreshSplits() {
        const auto list = controls[SplitList];
        const auto chosen = SendMessageW(list, LB_GETCURSEL, 0, 0);
        SendMessageW(list, LB_RESETCONTENT, 0, 0);
        splitBook = settings.speedrun ? speedrun::SplitBook::load(splitsFile()) : speedrun::SplitBook{};
        const auto &best = splitBook.best();
        const auto &golds = splitBook.golds();
        splitGold.assign(best.size(), false);
        for (size_t i = 0; i < best.size(); ++i) {
            const double segment = best[i] - (i ? best[i - 1] : 0.0);
            const bool hasGold = i < golds.size() && golds[i] > 0;
            splitGold[i] = hasGold && segment <= golds[i] + 0.005;
            const std::wstring row = splitName(i) + L" \u00b7 " + wide(splitTime(best[i])) + L" \u00b7 " +
                                     wide(splitTime(segment)) + L" \u00b7 " +
                                     (hasGold ? wide(splitTime(golds[i])) : std::wstring(L"\u2013"));
            SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(row.c_str()));
        }
        if (chosen >= 0 && size_t(chosen) < best.size())
            SendMessageW(list, LB_SETCURSEL, chosen, 0);
        ShowWindow(list, page == speedrunPage && !best.empty() ? SW_SHOWNA : SW_HIDE);
    }
    void refreshOverlayItems() {
        const auto list = controls[OverlayItems];
        const auto chosen = SendMessageW(list, LB_GETCURSEL, 0, 0);
        SendMessageW(list, LB_RESETCONTENT, 0, 0);
        for (unsigned i = 0; i < speedrun::overlay_item_count; ++i) {
            const auto &item = speedrun::overlay_item_text[i];
            const bool locked = (speedrun::overlay_required >> i) & 1u;
            const std::wstring row = std::wstring((settings.overlayItems >> i) & 1u ? L"On" : L"Off") + L" \u00b7 " +
                                     item.name + L" \u00b7 " + (locked ? L"Always shown. " : L"") + item.about;
            SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(row.c_str()));
        }
        SendMessageW(list, LB_SETCURSEL, chosen, 0);
    }
    static int overlayStep(int h) { return std::clamp((h - 64 - 246 - 48) / 6, 52, 68); }
    void saveOverlay(const std::wstring &message) {
        auto next = settings;
        next.overlayCorner = unsigned(selected(OverlayCorner));
        next.overlaySize = unsigned(selected(OverlaySize));
        next.overlayBackground = unsigned(selected(OverlayBackground));
        next.overlaySplits = unsigned(selected(OverlaySplits));
        const int roles[] = {OverlayText, OverlayAhead, OverlayBehind, OverlayGold};
        for (unsigned role = 0; role < speedrun::overlay_color_roles; ++role)
            next.overlayColors[role] = unsigned(std::max(0, selected(roles[role])));
        next.overlayDecimals = unsigned(std::max(0, selected(OverlayDecimals)));
        next.overlayMargin = unsigned(std::max(0, selected(OverlayMargin)));
        next.overlayShape = unsigned(std::max(0, selected(OverlayShape)));
        next.overlayTitle = utf8(trimmed(textOf(OverlayTitle)).substr(0, 48));
        next.overlaySplitNames = utf8(trimmed(textOf(OverlayNames)).substr(0, 600));
        write_settings(settings_path(), next);
        settings = std::move(next);
        status = message;
        error = false;
        InvalidateRect(hwnd, nullptr, FALSE);
    }
    void refreshTabs() {
        const auto list = controls[TabList];
        const auto chosen = SendMessageW(list, LB_GETCURSEL, 0, 0);
        SendMessageW(list, LB_RESETCONTENT, 0, 0);
        for (unsigned tab : settings.tabOrder) {
            const bool hidden = (settings.hiddenTabs >> tab) & 1u, fixed = (fixed_tabs >> tab) & 1u;
            const int slot = tabSlot(int(tab));
            const std::wstring row = std::wstring(hidden ? L"Off" : L"On") + L" \u00b7 " + tab_names[tab] + L" \u00b7 " +
                                     (hidden ? std::wstring(L"Hidden from the sidebar") : L"Shown, Ctrl+" + std::to_wstring(slot + 1)) +
                                     (fixed ? L". Always shown." : L"");
            SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(row.c_str()));
        }
        SendMessageW(list, LB_SETCURSEL, chosen, 0);
    }
    void saveLauncherLook(const std::wstring &message) {
        try {
            write_settings(settings_path(), settings);
            status = message;
            error = false;
        } catch (const std::exception &failure) {
            status = wide(failure.what());
            error = true;
        }
    }
    void chrome() {
        if (!hwnd)
            return;
        const bool light = themes[std::min(settings.launcherTheme, theme_count - 1u)].light;
        if (auto module = LoadLibraryW(L"dwmapi.dll")) {
            if (auto fn = reinterpret_cast<HRESULT(WINAPI *)(HWND, DWORD, LPCVOID, DWORD)>(
                    GetProcAddress(module, "DwmSetWindowAttribute"))) {
                BOOL dark = light ? FALSE : TRUE;
                fn(hwnd, 20, &dark, sizeof(dark));
                COLORREF caption = sidebar, captionText = ink, edge = border;
                fn(hwnd, 35, &caption, sizeof(caption));
                fn(hwnd, 36, &captionText, sizeof(captionText));
                fn(hwnd, 34, &edge, sizeof(edge));
            }
            FreeLibrary(module);
        }
        if (auto module = LoadLibraryW(L"uxtheme.dll")) {
            if (auto fn = reinterpret_cast<HRESULT(WINAPI *)(HWND, LPCWSTR, LPCWSTR)>(GetProcAddress(module, "SetWindowTheme")))
                for (auto [id, control] : controls)
                    fn(control, light ? L"Explorer" : L"DarkMode_Explorer", nullptr);
            FreeLibrary(module);
        }
    }
    void restyle() {
        apply_theme(settings.launcherTheme, settings.launcherAccent, settings.launcherColors);
        chrome();
        renderFrame();
        layout();
        RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME);
    }
    void paintUpdates(HDC dc, int h, int content) {
        const int right = contentLeft + content;
        std::wstring message = updateStatus;
        if (updating()) {
            std::lock_guard lock(updateProgress->mutex);
            if (!updateProgress->text.empty())
                message = updateProgress->text;
        }
        panelBox(dc, contentLeft, 124, right, 212);
        text(dc, L"OpenKai2 v" + wide(application_version), contentLeft + 20, 136, content - 200, 24, fonts.strong);
        text(dc, release ? L"Version " + wide(release->version) + L" is available." : std::wstring(L"You have this version installed."),
             contentLeft + 20, 162, content - 200, 18, fonts.caption, release ? gold : muted, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
        text(dc, message, contentLeft + 20, 184, content - 200, 18, fonts.caption, updateError ? red : muted,
             DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        const Grid grid(content);
        panelBox(dc, contentLeft, 224, right, 372);
        text(dc, L"Preferences", contentLeft + 20, 236, content - 40, 22, fonts.strong);
        caps(dc, L"UPDATE CHANNEL", grid.x(0), Grid::label(236, 0), grid.column);
        caps(dc, L"AUTOMATIC UPDATES", grid.x(1), Grid::label(236, 0), grid.column);
        caps(dc, L"MANUAL CHECK", grid.x(2), Grid::label(236, 0), grid.column);
        text(dc, settings.updateChannel ? L"Stable and prerelease: every published build, including test versions."
                                        : L"Stable: only versions marked stable.",
             grid.x(0), 344, content - 40, 18, fonts.caption, muted, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        panelBox(dc, contentLeft, 384, right, std::min(h - 64, 518));
        text(dc, L"How updates install", contentLeft + 20, 396, content - 40, 22, fonts.strong);
        text(dc, L"With automatic updates on, the launcher looks for a new version between game sessions. A found update downloads, "
                 L"is checked against its published SHA-256, and installs after the game closes; your settings, saves and mods are kept. "
                 L"If GitHub is busy, the launcher tries again a few times.",
             contentLeft + 20, 418, content - 40, 46, fonts.caption, muted, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
        text(dc, L"Reporting a problem? Attach startup.log or launcher.log from the logs folder.", contentLeft + 200, 478,
             content - 220, 18, fonts.caption, muted, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
    void refreshColors() {
        const auto list = controls[ColorList];
        const auto chosen = SendMessageW(list, LB_GETCURSEL, 0, 0);
        const auto top = SendMessageW(list, LB_GETTOPINDEX, 0, 0);
        SendMessageW(list, LB_RESETCONTENT, 0, 0);
        const auto look = make_theme(settings.launcherTheme, settings.launcherAccent, settings.launcherColors);
        for (unsigned role = 0; role < color_role_count; ++role) {
            const Rgb color = look.*(color_roles[role].member);
            wchar_t hex[8]{};
            swprintf(hex, 8, L"#%02X%02X%02X", color.r, color.g, color.b);
            const std::wstring row = std::wstring(hex) + L" \u00b7 " + color_roles[role].name +
                                     (settings.launcherColors[role] >= 0 ? L" (changed)" : L"") + L" \u00b7 " + color_roles[role].about;
            SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(row.c_str()));
        }
        SendMessageW(list, LB_SETTOPINDEX, WPARAM(std::max<LRESULT>(top, 0)), 0);
        SendMessageW(list, LB_SETCURSEL, chosen, 0);
    }
    void paintColors(HDC dc, int h, int content) {
        const int right = contentLeft + content, panelBottom = h - 64;
        panelBox(dc, contentLeft, 124, right, 196);
        text(dc, std::wstring(L"Theme ") + themes[std::min(settings.launcherTheme, theme_count - 1u)].name,
             contentLeft + 20, 136, content - 200, 22, fonts.strong);
        text(dc, L"Double-click a part to recolor it.", contentLeft + 20, 160, content - 200, 18, fonts.caption, muted,
             DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
        panelBox(dc, contentLeft, 208, right, panelBottom);
        caps(dc, L"COLORS", contentLeft + 20, 212, 200);
        const unsigned custom = unsigned(std::count_if(settings.launcherColors.begin(), settings.launcherColors.end(),
                                                       [](int32_t value) { return value >= 0; }));
        const int listWidth = std::min(560, content - 40 - 200), side = contentLeft + 20 + listWidth + 20;
        text(dc, std::to_wstring(custom) + (custom == 1 ? L" color changed" : L" colors changed"), side, 516,
             std::max(40, right - 20 - side), 18, fonts.caption, muted, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
    }
    void paintTabs(HDC dc, int h, int content) {
        const int right = contentLeft + content, panelBottom = h - 64;
        panelBox(dc, contentLeft, 124, right, 196);
        text(dc, L"Your sidebar", contentLeft + 20, 136, content - 200, 22, fonts.strong);
        text(dc, L"Pick a tab, then move it up or down or hide it. Ctrl+1 to Ctrl+7 follow the order shown. "
                 L"Play and Settings are always shown.",
             contentLeft + 20, 160, content - 200, 32, fonts.caption, muted, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
        panelBox(dc, contentLeft, 208, right, panelBottom);
        caps(dc, L"TABS", contentLeft + 20, 212, 200);
    }
    void paintOverlay(HDC dc, int h, int content) {
        const int right = contentLeft + content, panelBottom = h - 64, half = (content - 52) / 2;
        const int side = contentLeft + 32 + half, column = (content - 52 - half - 12) / 2;
        panelBox(dc, contentLeft, 124, right, 196);
        text(dc, L"Tracked automatically", contentLeft + 20, 136, content - 200, 22, fonts.strong);
        text(dc, L"Splits happen on each battle won. Your best run, best segments and every attempt are saved after each run. "
                 L"In game: F7 hides the overlay, F8 resets.",
             contentLeft + 20, 160, content - 200, 32, fonts.caption, muted, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
        panelBox(dc, contentLeft, 208, right, panelBottom);
        caps(dc, L"SHOWN IN GAME", contentLeft + 20, 212, half);
        const int step = overlayStep(h);
        const auto heading = [&](int index) { return 228 + index * step; };
        caps(dc, L"POSITION", side, heading(0), 200);
        caps(dc, L"SIZE", side + column + 12, heading(0), 200);
        caps(dc, L"BACKGROUND", side, heading(1), 200);
        caps(dc, L"SPLIT LIST", side + column + 12, heading(1), 200);
        caps(dc, L"TEXT COLOR", side, heading(2), 200);
        caps(dc, L"AHEAD COLOR", side + column + 12, heading(2), 200);
        caps(dc, L"BEHIND COLOR", side, heading(3), 200);
        caps(dc, L"BEST SEGMENT COLOR", side + column + 12, heading(3), 200);
        caps(dc, L"TIMES SHOWN TO", side, heading(4), 200);
        caps(dc, L"DISTANCE FROM EDGE", side + column + 12, heading(4), 200);
        caps(dc, L"CORNERS", side, heading(5), 200);
        caps(dc, L"TITLE", side + column + 12, heading(5), 200);
        caps(dc, L"SPLIT NAMES", side, heading(6), 200);
        const int previewTop = 246 + 7 * step + 2;
        if (panelBottom - 20 - previewTop < 80)
            return;
        caps(dc, L"PREVIEW", side, previewTop - 20, 200);

        const int previewWidth = right - 20 - side;
        const int previewHeight = std::max(80, std::min(previewWidth * 9 / 16, panelBottom - 20 - previewTop));
        const RECT screen{p(side), p(previewTop), p(side + previewWidth), p(previewTop + previewHeight)};
        const int span = std::max(1L, screen.bottom - screen.top);
        for (int y = screen.top; y < screen.bottom; ++y)
            fill(dc, {screen.left, y, screen.right, y + 1},
                 mix(RGB(70, 120, 190), RGB(46, 80, 40), (y - screen.top) * 255 / span));
        const unsigned items = settings.overlayItems;
        std::vector<std::pair<std::wstring, std::wstring>> rows;
        if (!settings.overlayTitle.empty())
            rows.push_back({wide(settings.overlayTitle), L""});
        if (items & speedrun::overlay_bit(speedrun::show_status))
            rows.push_back({wide(speedrun::category_name(speedrun::Category(settings.speedrun))), speedrun::tournament(speedrun::Category(settings.speedrun)) ? L"Manual timing" : L"Verified"});
        if (items & speedrun::overlay_bit(speedrun::show_splits)) {
            const unsigned shown = speedrun::overlay_split_rows[std::min(settings.overlaySplits, 3u)];
            for (unsigned i = 0; i < std::min(shown, 4u); ++i)
            {
                std::vector<std::wstring> names;
                std::wstring name;
                for (const wchar_t c : wide(settings.overlaySplitNames) + L",") {
                    if (c != L',') { name += c; continue; }
                    names.push_back(trimmed(name));
                    name.clear();
                }
                const std::wstring label = i < names.size() && !names[i].empty() ? names[i] : L"Battle " + std::to_wstring(i + 1);
                rows.push_back({label, i < 2 ? (i ? L"+0.84" : L"-1.20") : L""});
            }
        }
        const bool big = items & speedrun::overlay_bit(speedrun::show_timer);
        const wchar_t *names[] = {L"", L"", L"Segment", L"Previous segment", L"Possible time save", L"Best possible time",
                                  L"Sum of best", L"Personal best", L"PS2 time", L"Attempts", L""};
        std::vector<std::wstring> info;
        for (unsigned i = speedrun::show_segment; i <= speedrun::show_attempts; ++i)
            if (items & (1u << i))
                info.push_back(names[i]);
        const int scale[] = {85, 100, 125, 150};
        const int unit = std::max(1, previewHeight * scale[std::min(settings.overlaySize, 3u)] / 100);
        const int row = std::max(5, unit / 22), timer = big ? row * 2 : 0;
        const int boxWidth = std::max(40, previewWidth * 23 / 100 * scale[std::min(settings.overlaySize, 3u)] / 100);
        const int boxHeight = int(rows.size() + info.size()) * row + timer + 8;
        if (!rows.empty() || !info.empty() || big) {
            const int corner = int(settings.overlayCorner);
            const int edges[] = {3, 8, 20}, edge = edges[std::min(settings.overlayMargin, 2u)];
            const int left = corner % 2 ? screen.right - p(edge) - p(boxWidth) : screen.left + p(edge);
            const int top = corner < 2 ? screen.top + p(edge) : screen.bottom - p(edge) - p(boxHeight);
            RECT frame{left, top, left + p(boxWidth), top + p(boxHeight)};
            const int shades[] = {0, 120, 210, 60, 250};
            if (settings.overlayBackground)
                box(dc, frame, mix(RGB(60, 90, 120), RGB(8, 9, 14), shades[std::min(settings.overlayBackground, 4u)]),
                    mix(RGB(60, 90, 120), RGB(8, 9, 14), 90), settings.overlayShape ? 0 : 6);
            int y = top + p(4);
            auto bar = [&](int width, COLORREF color) {
                fill(dc, {frame.left + p(5), y + p(row) / 3, frame.left + p(5) + p(width), y + p(row) * 2 / 3}, color);
            };
            for (const auto &[name, delta] : rows) {
                bar(boxWidth * 2 / 5, mix(ink, RGB(20, 20, 20), 80));
                if (!delta.empty())
                    fill(dc, {frame.right - p(boxWidth / 2), y + p(row) / 3, frame.right - p(boxWidth / 3), y + p(row) * 2 / 3},
                         delta[0] == L'-' ? green : red);
                fill(dc, {frame.right - p(boxWidth / 4), y + p(row) / 3, frame.right - p(5), y + p(row) * 2 / 3},
                     mix(ink, RGB(20, 20, 20), 120));
                y += p(row);
            }
            if (big) {
                fill(dc, {frame.right - p(boxWidth * 3 / 5), y + p(timer) / 5, frame.right - p(5), y + p(timer) * 4 / 5}, green);
                y += p(timer);
            }
            for (size_t i = 0; i < info.size(); ++i) {
                bar(boxWidth / 3, mix(muted, RGB(20, 20, 20), 60));
                fill(dc, {frame.right - p(boxWidth / 4), y + p(row) / 3, frame.right - p(5), y + p(row) * 2 / 3},
                     mix(ink, RGB(20, 20, 20), 120));
                y += p(row);
            }
        }
        if (settings.fpsCounter) {
            const int corner = int(settings.overlayCorner);
            const int width = std::max(12, previewWidth / 14);
            const int left = corner % 2 ? screen.left + p(8) : screen.right - p(8) - p(width);
            fill(dc, {left, screen.top + p(8), left + p(width), screen.top + p(8) + p(std::max(3, row * 2 / 3))},
                 mix(ink, RGB(20, 20, 20), 100));
        }
        text(dc, settings.speedrun ? L"Speedrun mode is on." : L"Speedrun mode is off. Choose a category on the Speedrun page to use the overlay.",
             side, 386 + previewHeight + 8, previewWidth, 18, fonts.caption, settings.speedrun ? muted : gold,
             DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
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
            caps(dc, models && forcing() ? L"2   CHOOSE WHOSE MOVES IT USES"
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
                       L" model with " + wide(occupant->name) + L"'s moves. The moves may not suit the body. " + into;
            if (selected(SpotMode) == forceWithMoves)
                return L"A new character with its own spot: the whole Budokai 3 " + wide(character->name) +
                       L" model with its Budokai 3 animations and moves, converted for Budokai 2, on " +
                       wide(occupant->name) + L"'s record. Experimental: some buttons may do a different move. " + into;
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
    void paintOnline(HDC dc, int h, int content) {
        const int right = contentLeft + content;
        const Grid grid(content);
        auto section = [&](int left, int top, int edge, int bottom, const wchar_t *title, const std::wstring &note,
                           int noteRight = 0) {
            panelBox(dc, left, top, edge, bottom);
            text(dc, title, left + 20, top + 12, 200, 22, fonts.strong);
            text(dc, note, left + 140, top + 14, (noteRight ? noteRight : edge - 20) - left - 140, 18, fonts.caption, muted,
                 DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        };
        auto label = [&](int top, int row, int column, const wchar_t *value) {
            caps(dc, value, grid.x(column), Grid::label(top, row), grid.column);
        };
        const bool taken = directory && directory->connected() && directory->name_taken();
        const bool online = directory && directory->connected() && !taken;
        const std::wstring service = taken ? std::wstring(L"Name taken")
                                     : online ? L"Online \u00b7 " + std::to_wstring(playerCount()) + L" players"
                                     : connecting.valid() ? std::wstring(L"Connecting\u2026")
                                     : settings.onlineProfiles.empty() ? std::wstring(L"Add a profile to go online")
                                     : L"Offline" + (onlineProblem.empty() ? std::wstring() : L": " + wide(onlineProblem));
        section(contentLeft, onlineProfileTop, right, onlineProfileTop + 104, L"Profile",
                settings.onlineProfiles.empty() ? L"Names are unique across OpenKai2. Yours is shown to other players."
                : directory && directory->name_taken() ? L"This name belongs to another profile. Add a new profile with a unique name."
                                                : L"Names are unique across OpenKai2. Yours is shown to other players.");
        const int dotX = right - 20 - measure(dc, service, fonts.caption);
        ellipse(dc, p(dotX - 12), p(onlineProfileTop + 23), p(4),
                taken ? red : online ? green : connecting.valid() ? gold : faint);
        text(dc, service, dotX, onlineProfileTop + 14, right - dotX - 16, 18, fonts.caption, taken ? red : online ? ink : muted,
             DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        label(onlineProfileTop, 0, 0, L"PROFILE");
        label(onlineProfileTop, 0, 1, L"NEW PROFILE");
        section(contentLeft, onlineHostTop, right, onlineHostTop + 160, L"Host a room",
                L"You are player 1. Your router's port is opened automatically when it allows it.");
        label(onlineHostTop, 0, 0, L"ROOM NAME (OPTIONAL)");
        label(onlineHostTop, 0, 1, L"PASSWORD (OPTIONAL)");
        label(onlineHostTop, 0, 2, L"CONNECTION");
        text(dc, L"Port and router are set up for you.", grid.x(2), Grid::field(onlineHostTop, 0) + 8, grid.column, 18,
             fonts.caption, muted, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        label(onlineHostTop, 1, 0, L"INPUT DELAY");
        label(onlineHostTop, 1, 1, L"WHO CAN SEE IT");
        const int split = contentLeft + (content * 62) / 100, bottom = h - 64 + 6;
        const std::wstring roomNote = roomRows.empty() ? std::wstring(L"Updates live. Same version and disc needed.")
                                      : std::to_wstring(roomRows.size()) + (roomRows.size() == 1 ? L" open room" : L" open rooms") +
                                            L". Double-click one to join.";
        section(contentLeft, onlineListsTop, split - 6, bottom, L"Rooms", roomNote,
                split - 6 - 20 - 100 - 12);
        section(split + 6, onlineListsTop, right, bottom, L"Players",
                playerRows.empty() ? std::wstring(L"Friends are in the sidebar")
                                   : std::to_wstring(playerRows.size()) + L" online \u00b7 friends are in the sidebar");
        const int listTop = onlineListsTop + 44;
        if (roomRows.empty())
            text(dc, !online ? L"Rooms appear here when you are online." : L"No open rooms right now. Host one, or invite a player.",
                 contentLeft + 20, listTop + 8, split - 6 - contentLeft - 40, 18, fonts.caption, muted,
                 DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        if (playerRows.empty())
            text(dc, !online ? L"Players appear here when you are online." : L"Nobody else is online yet.", split + 26,
                 listTop + 8, right - split - 46, 18, fonts.caption, muted,
                 DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
    void paintSettings(HDC dc, int content) {
        const int right = contentLeft + content;
        RECT client{};
        GetClientRect(hwnd, &client);
        const SettingsGrid grid(content, unscale(client.bottom));
        const int displayTop = grid.displayTop, graphicsTop = grid.graphicsTop,
                  gameTop = grid.gameTop, updatesTop = grid.updatesTop;
        auto section = [&](int top, int rows, const wchar_t *title, const std::wstring &note, COLORREF tone) {
            panelBox(dc, contentLeft, top, right, grid.bottom(top, rows));
            text(dc, title, contentLeft + 20, top + 12, 200, 22, fonts.strong);
            const int actions = top == updatesTop ? 270 : top == gameTop ? 190 : 0;
            text(dc, note, contentLeft + 120, top + 14, content - 140 - actions, 18,
                 fonts.caption, tone, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        };
        auto label = [&](int top, int row, int column, const wchar_t *value) {
            caps(dc, value, grid.x(column), grid.label(top, row), grid.column);
        };
        section(displayTop, 2, L"Display",
                settings.windowMode ? L"Borderless uses your desktop resolution. The game draws up to 60 frames a second."
                                    : L"Game timing stays at 60 FPS. A higher presentation cap lowers input latency.",
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
        label(graphicsTop, 1, 1, L"INTERNAL RESOLUTION");
        label(graphicsTop, 1, 2, L"PRESETS");

        const wchar_t *const cpuNotes[] = {
            L"The CPU plays at the Game Difficulty and COM Level set in the game's Options.",
            L"Z1: the strongest CPU behaviour in every fight, and its hits do 10% more damage.",
            L"Z2: the strongest CPU, 20% stronger hits, 10% less damage taken, faster Ki and a small Ki head start.",
            L"Z3: the strongest CPU, 30% stronger hits, 20% less damage taken, much faster Ki and a Ki head start.",
            L"Z4: 40% stronger hits, 30% less damage taken, faster Ki, slow health recovery and 40% starting Ki.",
            L"Z5: 55% stronger hits, 40% less damage taken, very fast Ki, health recovery and 60% starting Ki.",
            L"Z6: 70% stronger hits, half damage taken, fastest Ki, quick health recovery and full Ki at the start."};
        section(gameTop, 2, L"Game", cpuNotes[std::min(settings.cpuDifficulty, 6u)], muted);
        label(gameTop, 0, 0, L"CPU DIFFICULTY");
        label(gameTop, 0, 1, L"HD TEXTURES");
        label(gameTop, 0, 2, L"MUSIC");
        label(gameTop, 1, 0, L"FPS COUNTER");
        label(gameTop, 1, 1, L"RANDOMIZER");

        section(updatesTop, 1, L"Launcher", L"Colors, tab order and the page the launcher opens on", muted);
        label(updatesTop, 0, 0, L"THEME");
        label(updatesTop, 0, 1, L"ACCENT COLOR");
        label(updatesTop, 0, 2, L"START ON");
    }
    void paintSpeedrun(HDC dc, int h, int content) {
        const int right = contentLeft + content;
        const Grid grid(content);
        const bool compact = h < 700;
        const int rulesTop = compact ? 496 : speedrunRulesTop;
        const int splitBottom = rulesTop - 12;
        panelBox(dc, contentLeft, speedrunTop, right, speedrunTop + 160);
        text(dc, L"Run", contentLeft + 20, speedrunTop + 12, 200, 22, fonts.strong);
        text(dc, settings.speedrun ? (speedrun::tournament(speedrun::Category(settings.speedrun)) ? L"F9 starts; F10 splits; F11 finishes. F8 resets." : L"Mods are off while a category is chosen. F7 hides the overlay; F8 resets.")
                                   : L"Choose a category to time runs.",
             contentLeft + 120, speedrunTop + 14, content - 324, 28, fonts.caption, muted,
             DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
        caps(dc, L"CATEGORY", grid.x(0), Grid::label(speedrunTop, 0), grid.column);
        caps(dc, L"PERMADEATH", grid.x(1), Grid::label(speedrunTop, 0), grid.column);
        caps(dc, L"RANDOMIZER", grid.x(2), Grid::label(speedrunTop, 0), grid.column);
        caps(dc, L"CPU DIFFICULTY", grid.x(0), Grid::label(speedrunTop, 1), grid.column);
        text(dc, L"Z1 to Z6 runs are timed as their own categories, with their own splits.", grid.x(1),
             Grid::field(speedrunTop, 1) + 8, grid.column * 2 + 16, 28, fonts.caption, muted,
             DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
        panelBox(dc, contentLeft, splitsTop, right, splitBottom);
        const std::wstring splitTitle = settings.speedrun ? L"Splits · " +
            wide(speedrun::category_label(speedrun::Category(std::min(settings.speedrun, speedrun::category_count - 1)),
                                          std::min(settings.cpuDifficulty, 6u), settings.randomizer)) +
            (settings.permadeath && !speedrun::tournament(speedrun::Category(settings.speedrun)) ? L" permadeath" : L"") : std::wstring(L"Splits");
        const std::wstring attempts = settings.speedrun && splitBook.attempts() ?
            std::to_wstring(splitBook.attempts()) + (splitBook.attempts() == 1 ? L" attempt" : L" attempts") +
            L" · " + std::to_wstring(splitBook.completed()) + L" finished" : std::wstring();
        const int attemptsWidth = attempts.empty() ? 0 : std::min(content / 2 - 20, measure(dc, attempts, fonts.caption));
        text(dc, splitTitle, contentLeft + 20, splitsTop + 12,
             content - 40 - attemptsWidth - (attempts.empty() ? 0 : 16), 22, fonts.strong, ink,
             DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        text(dc, attempts, right - 20 - attemptsWidth, splitsTop + 15, attemptsWidth, 18, fonts.caption, muted,
             DT_RIGHT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        const int listWidth = (content - 40) * 58 / 100, side = contentLeft + 20 + listWidth + 16;
        RECT listClient{};
        GetClientRect(controls[SplitList], &listClient);
        const int columnsWidth = listClient.right > 0 ? unscale(listClient.right) : listWidth;
        caps(dc, L"SPLIT", contentLeft + 34, splitsTop + 46, columnsWidth * splitNameColumn / 100 - 22);
        const wchar_t *headings[] = {L"TIME", L"SEGMENT", L"BEST SEGMENT"};
        for (int i = 0; i < 3; ++i) {
            const int begin = contentLeft + 20 + columnsWidth * (i ? splitColumns[i - 1] : splitNameColumn) / 100 + 8;
            const int edge = contentLeft + 20 + columnsWidth * splitColumns[i] / 100 - 8;
            // DrawText's right alignment can clip the last glyph when character
            // spacing is enabled. Measure and draw these column labels without
            // tracking, leaving a little room for glyph overhang at the edge.
            const int headingWidth = measure(dc, headings[i], fonts.label) + 2;
            if (headingWidth <= edge - begin)
                text(dc, headings[i], begin, splitsTop + 46, edge - begin, 16, fonts.label, muted,
                     DT_RIGHT | DT_SINGLELINE | DT_NOPREFIX);
            else
                text(dc, headings[i], begin, splitsTop + 38, edge - begin, 28, fonts.label, muted,
                     DT_RIGHT | DT_WORDBREAK | DT_NOPREFIX);
        }
        const auto chosenSplit = SendMessageW(controls[SplitList], LB_GETCURSEL, 0, 0);
        const bool picked = chosenSplit >= 0 && size_t(chosenSplit) < splitBook.best().size();
        const int sideWidth = right - 20 - side, selectedTop = splitsTop + (compact ? 54 : 70);
        const int labelWidth = std::min(130, sideWidth / 2);
        caps(dc, L"SELECTED SPLIT", side, selectedTop, labelWidth);
        text(dc, picked ? splitName(size_t(chosenSplit)) : std::wstring(L"None"), side + labelWidth + 8,
             selectedTop, sideWidth - labelWidth - 8, 18, fonts.caption, picked ? ink : muted,
             DT_RIGHT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        text(dc, L"Pick a split, type a time, then press Set time.", side, splitsTop + (compact ? 182 : 208), sideWidth, 18,
             fonts.caption, muted, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        if (splitBook.best().empty()) {
            text(dc, settings.speedrun ? L"No best run saved yet. Finish a run, or add split times here."
                                       : L"Choose a category to see and edit its splits.",
                 contentLeft + 28, splitsTop + 78, listWidth - 24, splitBottom - 40 - splitsTop - 86,
                 fonts.caption, muted, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
        } else {
            const auto &golds = splitBook.golds();
            double sum = 0;
            bool complete = golds.size() >= splitBook.best().size();
            for (size_t i = 0; i < splitBook.best().size() && complete; ++i) {
                if (golds[i] <= 0) complete = false;
                sum += golds[i];
            }
            std::vector<std::pair<std::wstring, std::wstring>> totals = {
                {L"BEST RUN", wide(splitTime(splitBook.best_time()))}};
            if (complete) {
                totals.push_back({L"SUM OF BEST", wide(splitTime(sum))});
                totals.push_back({L"POSSIBLE SAVE", wide(splitTime(std::max(0.0, splitBook.best_time() - sum)))});
            }
            const int cell = (listWidth - 20) / int(totals.size());
            for (size_t i = 0; i < totals.size(); ++i) {
                const int x = contentLeft + 30 + int(i) * cell;
                caps(dc, totals[i].first, x, splitBottom - 38, cell - 8, muted, DT_LEFT | DT_END_ELLIPSIS);
                text(dc, totals[i].second, x, splitBottom - 21, cell - 8, 18, fonts.caption, ink,
                     DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            }
        }
        panelBox(dc, contentLeft, rulesTop, right, h - 58);
        text(dc, L"How runs are timed", contentLeft + 20, rulesTop + 12, content - 40, 22, fonts.strong);
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
        const auto glowing = renderFrame();
        BitBlt(dc, 0, 0, rect.right, rect.bottom, buffer.dc, 0, 0, SRCCOPY);
        EndPaint(hwnd, &ps);
        if (paintedPage != page || paintedSize != std::pair<int, int>{rect.right, rect.bottom} || paintedGlow != glowing) {
            paintedPage = page;
            paintedSize = {rect.right, rect.bottom};
            paintedGlow = glowing;
            for (auto [id, control] : controls)
                InvalidateRect(control, nullptr, FALSE);
        }
    }
    std::vector<int> renderFrame() {
        RECT rect{};
        GetClientRect(hwnd, &rect);
        if (rect.right <= 0 || rect.bottom <= 0)
            return {};
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
        else if (page == savesPage)
            paintSaves(memory, h, content);
        else if (page == modsPage)
            paintMods(memory, h, content);
        else if (page == importPage)
            paintImport(memory, h, content);
        else if (page == overlayPage)
            paintOverlay(memory, h, content);
        else if (page == tabsPage)
            paintTabs(memory, h, content);
        else if (page == colorsPage)
            paintColors(memory, h, content);
        else if (page == updatesPage)
            paintUpdates(memory, h, content);
        else if (page == onlinePage)
            paintOnline(memory, h, content);
        else if (page == speedrunPage)
            paintSpeedrun(memory, h, content);
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
        for (int id : {int(ImportSelected), task[int(importTask)]})
            if (const auto r = controlRect(id); r && IsWindowEnabled(controls.at(id))) {
                buffer.frame.halo(*r, cut(10), p(14), ember, 80);
                glowing.push_back(id);
            }
        return glowing;
    }
    COLORREF surface(int id, const RECT &r) const {
        if (id >= PlayTab && id <= UpdatesTab)
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
        const int size = p(16), left = r.left + p(18), top = r.top + (r.bottom - r.top - size) / 2;
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
        } else if (id == OnlineTab) {
            Ellipse(dc, left, top, left + size, top + size);
            Ellipse(dc, left + size / 4, top, left + size - size / 4, top + size);
            MoveToEx(dc, left, top + size / 2, nullptr);
            LineTo(dc, left + size, top + size / 2);
        } else if (id == SpeedrunTab) {
            Ellipse(dc, left + p(1), top + p(2), left + size - p(1), top + size);
            MoveToEx(dc, left + size / 2, top + p(2) + size / 2 - p(1), nullptr);
            LineTo(dc, left + size / 2, top + p(6));
            MoveToEx(dc, left + size / 2 - p(3), top, nullptr);
            LineTo(dc, left + size / 2 + p(3), top);
        } else if (id == UpdatesTab) {
            MoveToEx(dc, left + size / 2, top, nullptr);
            LineTo(dc, left + size / 2, top + size - p(5));
            MoveToEx(dc, left + size / 2 - p(5), top + size - p(10), nullptr);
            LineTo(dc, left + size / 2, top + size - p(5));
            LineTo(dc, left + size / 2 + p(5) + 1, top + size - p(10) - 1);
            MoveToEx(dc, left, top + size - p(4), nullptr);
            LineTo(dc, left, top + size);
            LineTo(dc, left + size, top + size);
            LineTo(dc, left + size, top + size - p(5));
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
            const bool navigation = id >= PlayTab && id <= UpdatesTab;
            const bool chosen = navigation && id == PlayTab + navigationPage();
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
                label = buttonInk;
            } else {
                const bool onBanner = id == Play;
                const COLORREF idle = onBanner ? mix(heroTo, bannerTitle, 26) : panel;
                const COLORREF color = !enabled ? idle : pressed ? field : hot ? hover : raised;
                const COLORREF outline = !enabled ? mix(idle, onBanner ? bannerTitle : border, onBanner ? 50 : 140) : hot ? mix(border, orange, 110) : border;
                box(dc, r, color, outline, 10);
                if (enabled && !pressed)
                    fill(dc, {r.left + cut(10) + 1, r.top + 1, r.right - 1, r.top + 2}, mix(color, ink, 18));
                label = enabled ? ink : onBanner ? mix(bannerTitle, heroTo, 120) : faint;
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
                SetDCPenColor(dc, lit ? buttonInk : navigation ? mix(sidebar, orange, 70) : orange);
                auto pen = SelectObject(dc, GetStockObject(DC_PEN));
                auto brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
                if (!cutShape(dc, focus, cut(8), [](int, int) { return COLORREF(0); },
                              lit ? buttonInk : navigation ? mix(sidebar, orange, 70) : orange, false)) {
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
        const bool friends = item.hwndItem == controls[FriendList];
        const COLORREF base = friends ? sidebar : list ? field : panel;
        fill(dc, r, base);
        if (chosen) {
            fill(dc, r, mix(base, orange, 26));
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
        if (item.hwndItem == controls[SplitList] && parts.size() == 4u) {
            const int width = r.right - r.left + p(24);
            SetTextColor(dc, enabled ? ink : muted);
            RECT nameCell = r;
            nameCell.right = r.left - p(14) + width * splitNameColumn / 100 - p(8);
            DrawTextW(dc, parts[0].c_str(), int(parts[0].size()), &nameCell,
                      DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
            const bool golden = item.itemID < splitGold.size() && splitGold[item.itemID];
            for (size_t i = 1; i < 4u; ++i) {
                RECT cell = r;
                cell.left = r.left - p(14) + width * (i == 1 ? splitNameColumn : splitColumns[i - 2]) / 100 + p(8);
                cell.right = r.left - p(14) + width * splitColumns[i - 1] / 100 - p(8);
                SetTextColor(dc, !enabled ? muted : i == 1 ? ink : i == 2 && golden ? gold : muted);
                DrawTextW(dc, parts[i].c_str(), int(parts[i].size()), &cell,
                          DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_END_ELLIPSIS | DT_NOPREFIX);
            }
            SelectObject(dc, old);
            return;
        }
        if (item.hwndItem == controls[ColorList] && parts.size() > 1 && parts[0].size() == 7 && parts[0][0] == L'#') {
            const auto value = std::wcstol(parts[0].c_str() + 1, nullptr, 16);
            const COLORREF swatch = RGB(value >> 16 & 255, value >> 8 & 255, value & 255);
            const RECT box{r.left, r.top + p(6), r.left + p(56), r.bottom - p(6)};
            fill(dc, box, mix(border, ink, 60));
            fill(dc, {box.left + 1, box.top + 1, box.right - 1, box.bottom - 1}, swatch);
            r.left += p(70);
            parts.erase(parts.begin());
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
        if (page == savesPage)
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
    static constexpr int compareVersions = 2, forceModel = 3, forceWithMoves = 4;
    bool forcing() const { return selected(SpotMode) == forceModel || selected(SpotMode) == forceWithMoves; }
    void spotModes() {
        const bool models = characterView && sourceRoster->models;
        const auto list = controls[SpotMode];
        const auto count = SendMessageW(list, CB_GETCOUNT, 0, 0);
        if ((count == 5) == models)
            return;
        const auto keep = std::min<LRESULT>(SendMessageW(list, CB_GETCURSEL, 0, 0), 1);
        SendMessageW(list, CB_RESETCONTENT, 0, 0);
        std::vector<const wchar_t *> modes{L"Share a spot (R2 switches)", L"Add a new spot"};
        if (models) {
            modes.push_back(L"Compare 4 versions (test)");
            modes.push_back(L"Force a new character");
            modes.push_back(L"Force a new character with its Budokai 3 moves (experimental)");
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
        if (model && wearer && forcing())
            return gameRoster->shapes.count(wearer->id) != 0;
        return model && wearer && !modelMismatch(*model, *wearer);
    }
    void importModel(const mods::DiscCharacter &model, int pick) {
        const auto *wearer = gameRoster->find(gameRoster->grid[size_t(pick)]);
        if (!wearer)
            throw std::runtime_error("That spot is empty; choose a spot with a character to wear the model");
        const bool force = forcing(), ownMoves = selected(SpotMode) == forceWithMoves;
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
        size_t wearerMotions = 0;
        for (const auto &[offset, file] : wearer->files)
            if (offset == mods::motions_field)
                wearerMotions = file;
        if (ownMoves && !wearerMotions)
            throw std::runtime_error(utf8(wide(wearer->name)) + " has no motions file to take the Budokai 3 moves' place");
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
                                          (ownMoves ? std::wstring(L"its own Budokai 3 moves (experimental)")
                                                    : wide(wearer->name) + L"'s moves")
                                    : L"Added " + wide(wearer->name) + L" with " + wide(model.name) + L"'s textures") +
                             (compare ? L" in four versions, sharing spot " + std::to_wstring(pick + 1) +
                                            L"; press R2 there to go through them"
                              : newSpot ? std::wstring(L", with a new spot of its own")
                                        : L", sharing spot " + std::to_wstring(pick + 1) + L"; press R2 there to switch") +
                             L" (character ID" + (compare ? L"s " : L" ") + ids + L").";
        const auto mod = importMod(model.name);
        startModJob(Job::importMod, L"Importing…",
                    [disc = source, game = game, entry, own, versions, targets, mod, added, costumes, summary, ownMoves,
                     wearerMotions, stem](const std::shared_ptr<Progress> &progress) {
                        std::vector<std::pair<uint32_t, std::string>> moveFields;
                        if (ownMoves) {
                            report(progress, L"Converting the Budokai 3 moves", 0, 1);
                            const auto moveset = mods::budokai3_moveset(*disc, entry, *game, wearerMotions);
                            if (!moveset)
                                throw std::runtime_error("This Budokai 3 model has no move script next to it to convert");
                            for (const auto &[field, bytes, label] :
                                 {std::tuple{mods::moves_field, &moveset->moves, "moves"},
                                  std::tuple{mods::motions_field, &moveset->motions, "motions"}}) {
                                mods::ImportTarget target = targets.front();
                                target.name = stem + "_" + std::to_string(entry) + "_" + label + ".bin";
                                moveFields.push_back(
                                    {field, "add:" + mods::import_bytes(mod, target, *bytes).filename().string()});
                            }
                        }
                        for (size_t i = 0; i < versions.size(); ++i) {
                            report(progress, L"Importing the model", i, versions.size());
                            const auto bytes = mods::model_version(*disc, entry, *game, own, versions[i].first);
                            const auto written = mods::import_bytes(mod, targets[i], bytes).filename().string();
                            std::vector<std::pair<uint32_t, std::string>> fields;
                            for (const auto &costume : costumes)
                                fields.push_back({costume.first, "add:" + written});
                            fields.insert(fields.end(), moveFields.begin(), moveFields.end());
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
                               wide(settings.renderer) + onlineArguments + L" " +
                               quote_argument(settings.iso.wstring());
        const bool online = !onlineArguments.empty();
        onlineArguments.clear();
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        if (!CreateProcessW(exe, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                            std::filesystem::path(exe).parent_path().c_str(), &startup, &process))
            throw std::runtime_error("Cannot start the game process");
        CloseHandle(process.hThread);
        child = process.hProcess;
        status = online ? L"Online game running. Save tools unlock when you close it."
                        : L"Game running. Save tools unlock when you close it.";
        ShowWindow(hwnd, SW_MINIMIZE);
    }
    void cycleTimer() {
        const bool want = settings.launcherAccent == rgb_accent;
        if (want != cycling) {
            cycling = want;
            if (want)
                SetTimer(hwnd, 2, rgb_frame_ms, nullptr);
            else
                KillTimer(hwnd, 2);
        }
    }
    void cycleFrame() {
        if (!cycling || dropdownOpen() || IsIconic(hwnd))
            return;
        apply_theme(settings.launcherTheme, settings.launcherAccent, settings.launcherColors);
        const auto now = GetTickCount64();
        if (now - ambientAt >= 500u) {
            ambientAt = now;
            ambient.clear();
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        for (auto [id, control] : controls)
            if ((GetWindowLongPtrW(control, GWL_STYLE) & BS_TYPEMASK) == BS_OWNERDRAW && IsWindowVisible(control)) {
                wchar_t kind[16]{};
                if (GetClassNameW(control, kind, 16) && _wcsicmp(kind, L"Button") == 0)
                    InvalidateRect(control, nullptr, FALSE);
            }
    }
    void timer() {
        presence.enable(true);
        const char* pages[]={"Choosing a game","Setting up online play","Setting up a speedrun","Managing mods","Managing saves","Configuring settings","Checking for updates"};
        const int shown=std::clamp(navigationPage(),0,int(std::size(pages))-1);
        presence.update(running()?"Playing Budokai 2":"In launcher",running()?"In game":page==importPage?"Importing mods":pages[shown]);
        if(presence.status()!=lastPresenceStatus && !dropdownOpen()) {
            lastPresenceStatus=presence.status();InvalidateRect(hwnd,nullptr,FALSE);
        }
        onlineTick();
        cycleTimer();
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
                (installRequested || (settings.autoUpdates && idle && (page == 0 || page == updatesPage)))) {
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
        if (busy() || (updating() && page == updatesPage)) InvalidateRect(hwnd, nullptr, FALSE);
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
    static std::wstring trimmed(std::wstring value) {
        const auto first = value.find_first_not_of(L" \t");
        const auto last = value.find_last_not_of(L" \t");
        return first == std::wstring::npos ? std::wstring() : value.substr(first, last - first + 1);
    }
    std::optional<online::Profile> activeProfile() const {
        for (size_t i = 0; i < settings.onlineProfiles.size() && i < settings.onlineKeys.size(); ++i)
            if (settings.onlineProfiles[i] == settings.onlineName) {
                crypto::Seed seed{};
                if (!crypto::from_hex(settings.onlineKeys[i], seed.data(), seed.size()))
                    return std::nullopt;
                return online::make_profile(seed, settings.onlineName);
            }
        return std::nullopt;
    }
    void tidyProfiles() {
        Settings next = settings;
        const bool developer = identity::developer_seed().has_value();
        bool changed = false;
        std::vector<std::string> names, keys;
        for (size_t i = 0; i < next.onlineProfiles.size(); ++i) {
            const auto name = identity::allowed_name(next.onlineProfiles[i], developer);
            if (std::find(names.begin(), names.end(), name) != names.end()) {
                changed = true;
                continue;
            }
            std::string key = i < next.onlineKeys.size() ? next.onlineKeys[i] : std::string();
            crypto::Seed seed{};
            if (!crypto::from_hex(key, seed.data(), seed.size())) {
                const auto fresh = online::new_seed();
                key = crypto::to_hex(fresh.data(), fresh.size());
            }
            changed = changed || name != next.onlineProfiles[i] || i >= next.onlineKeys.size() || key != next.onlineKeys[i];
            if (next.onlineName == next.onlineProfiles[i])
                next.onlineName = name;
            names.push_back(name);
            keys.push_back(key);
        }
        const auto name = identity::allowed_name(next.onlineName, developer);
        changed = changed || name != next.onlineName || keys.size() != next.onlineKeys.size();
        next.onlineName = name;
        next.onlineProfiles = names;
        next.onlineKeys = keys;
        if (!changed)
            return;
        try {
            write_settings(settings_path(), next);
            settings = std::move(next);
        } catch (const std::exception &e) {
            std::cerr << "[launcher:online] " << e.what() << std::endl;
        }
    }
    void fillProfiles() {
        tidyProfiles();
        const auto box = controls[OnlineProfile];
        SendMessageW(box, CB_RESETCONTENT, 0, 0);
        int chosen = -1;
        for (size_t i = 0; i < settings.onlineProfiles.size(); ++i) {
            SendMessageW(box, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(wide(settings.onlineProfiles[i]).c_str()));
            if (settings.onlineProfiles[i] == settings.onlineName)
                chosen = int(i);
        }
        if (settings.onlineProfiles.empty())
            SendMessageW(box, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"No profile yet"));
        SendMessageW(box, CB_SETCURSEL, WPARAM(chosen < 0 ? 0 : chosen), 0);
        EnableWindow(box, !settings.onlineProfiles.empty());
    }
    bool saveOnline() {
        Settings next = settings;
        next.onlineAddress = utf8(trimmed(textOf(OnlineAddress)));
        next.onlineRoom = utf8(trimmed(textOf(OnlineRoom)));
        next.onlinePassword = utf8(textOf(OnlinePassword));
        next.onlineVisibility = unsigned(std::clamp(selected(OnlineListed), 0, 2));
        next.onlineListed = next.onlineVisibility != 2;
        next.onlineDelay = unsigned(std::clamp(selected(OnlineDelay), 0, 8));
        if (!settings.onlineProfiles.empty()) {
            const int chosen = std::clamp(selected(OnlineProfile), 0, int(settings.onlineProfiles.size()) - 1);
            next.onlineName = settings.onlineProfiles[size_t(chosen)];
        }
        try {
            write_settings(settings_path(), next);
        } catch (const std::exception &e) {
            status = wide(e.what());
            error = true;
            return false;
        }
        settings = std::move(next);
        error = false;
        return true;
    }
    bool profileReady() {
        if (activeProfile())
            return true;
        status = L"Add a profile before playing online.";
        error = true;
        SetFocus(controls[OnlineName]);
        return false;
    }
    void addProfile() {
        auto name = utf8(trimmed(textOf(OnlineName)));
        if (name.empty() || name.size() > 24) {
            status = L"Type a profile name of up to 24 characters.";
            error = true;
            return;
        }
        if (identity::claims_developer(name)) {
            if (!identity::developer_seed()) {
                const auto key = choose(false, false, L"[DEV] needs the developer key. Choose dev.key");
                if (key.empty() || !identity::install_developer_key(key)) {
                    status = key.empty() ? L"[DEV] is reserved for the OpenKai2 developer."
                                         : L"That file is not the OpenKai2 developer key.";
                    error = true;
                    return;
                }
            }
            name = identity::developer_name;
        }
        if (settings.onlineProfiles.size() >= 32) {
            status = L"Remove a profile before adding another.";
            error = true;
            return;
        }
        Settings next = settings;
        if (std::find(next.onlineProfiles.begin(), next.onlineProfiles.end(), name) == next.onlineProfiles.end()) {
            const auto key = online::name_key(name);
            if (std::any_of(next.onlineProfiles.begin(), next.onlineProfiles.end(),
                            [&](const std::string &existing) { return online::name_key(existing) == key; })) {
                status = L"You already have a profile with that name.";
                error = true;
                return;
            }
            const auto seed = online::new_seed();
            const auto owner = online::make_profile(seed, name).id;
            status = L"Checking that " + wide(name) + L" is free\u2026";
            error = false;
            InvalidateRect(hwnd, nullptr, FALSE);
            UpdateWindow(hwnd);
            const auto previous = SetCursor(LoadCursor(nullptr, IDC_WAIT));
            std::string problem;
            const auto state = online::lookup_name(settings.onlineLobby, name, owner, problem);
            SetCursor(previous);
            if (!state) {
                status = L"Could not check the name. Profile names are unique, so connect to the internet and try again.";
                error = true;
                return;
            }
            if (*state == online::NameState::taken) {
                status = wide(name) + L" is already used by another OpenKai2 profile. Profile names must be unique.";
                error = true;
                return;
            }
            next.onlineProfiles.push_back(name);
            next.onlineKeys.resize(next.onlineProfiles.size() - 1);
            next.onlineKeys.push_back(crypto::to_hex(seed.data(), seed.size()));
        }
        next.onlineName = name;
        write_settings(settings_path(), next);
        settings = std::move(next);
        SetWindowTextW(controls[OnlineName], L"");
        fillProfiles();
        reconnect();
        status = L"Profile " + wide(name) + L" is ready for online play.";
        error = false;
        refreshEnabled();
    }
    void removeProfile() {
        if (settings.onlineProfiles.empty())
            return;
        const int chosen = std::clamp(selected(OnlineProfile), 0, int(settings.onlineProfiles.size()) - 1);
        Settings next = settings;
        const auto name = next.onlineProfiles[size_t(chosen)];
        std::string owner;
        if (size_t(chosen) < next.onlineKeys.size()) {
            crypto::Seed seed{};
            if (crypto::from_hex(next.onlineKeys[size_t(chosen)], seed.data(), seed.size()))
                owner = online::make_profile(seed, name).id;
            next.onlineKeys.erase(next.onlineKeys.begin() + chosen);
        }
        next.onlineFriends.erase(std::remove_if(next.onlineFriends.begin(), next.onlineFriends.end(),
                                                [&](const Settings::Friend &entry) { return entry.owner == owner; }),
                                 next.onlineFriends.end());
        next.onlineProfiles.erase(next.onlineProfiles.begin() + chosen);
        next.onlineName = next.onlineProfiles.empty() ? std::string("Player") : next.onlineProfiles.front();
        write_settings(settings_path(), next);
        settings = std::move(next);
        fillProfiles();
        reconnect();
        status = L"Profile " + wide(name) + L" removed.";
        error = false;
        refreshEnabled();
    }
    void reconnect() {
        if (directory)
            directory->close();
        directory.reset();
        connectedProfile.clear();
        roomRows.clear();
        playerRows.clear();
        lastConnect = {};
        refreshOnlineLists();
    }
    void ensureOnline() {
        if (connecting.valid()) {
            if (connecting.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
                return;
            auto [opened, problem] = connecting.get();
            onlineProblem = problem;
            if (opened) {
                directory = std::move(opened);
                std::cout << "[launcher:online] connected as " << settings.onlineName
                          << (directory->name_taken() ? " (name taken by another profile)" : "") << std::endl;
            } else
                std::cout << "[launcher:online] could not connect: " << problem << std::endl;
            refreshOnlineLists();
            refreshEnabled();
            InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }
        const auto profile = activeProfile();
        if (!profile)
            return;
        if (directory && directory->connected() && connectedProfile == profile->id)
            return;
        if (directory && connectedProfile != profile->id)
            reconnect();
        const auto now = std::chrono::steady_clock::now();
        if (now - lastConnect < std::chrono::seconds(directory ? 5 : 15) && lastConnect != std::chrono::steady_clock::time_point{})
            return;
        lastConnect = now;
        directory.reset();
        connectedProfile = profile->id;
        auto shared = onlineShared;
        connecting = std::async(std::launch::async, [shared, profile = *profile, server = settings.onlineLobby]() {
            auto opened = std::make_unique<online::Directory>();
            opened->on_change([shared] {
                if (shared->window)
                    PostMessageW(shared->window, onlineChanged, 0, 0);
            });
            opened->on_invite([shared](const online::Invite &invite) {
                std::lock_guard lock(shared->mutex);
                if (shared->notices.size() < 20)
                    shared->notices.push_back({true, invite, {}});
                if (shared->window)
                    PostMessageW(shared->window, onlineNotice, 0, 0);
            });
            opened->on_friend([shared](const online::FriendMessage &message) {
                std::lock_guard lock(shared->mutex);
                if (shared->notices.size() < 40)
                    shared->notices.push_back({false, {}, message});
                if (shared->window)
                    PostMessageW(shared->window, onlineNotice, 0, 0);
            });
            std::string problem;
            if (!opened->open(server, profile, application_version, problem))
                opened.reset();
            return std::make_pair(std::move(opened), problem);
        });
        InvalidateRect(hwnd, nullptr, FALSE);
    }
    size_t playerCount() const { return directory ? directory->players().size() + 1 : 0; }
    bool isFriend(const std::string &id) const {
        const auto profile = activeProfile();
        return profile && std::any_of(settings.onlineFriends.begin(), settings.onlineFriends.end(), [&](const auto &entry) {
                   return entry.owner == profile->id && entry.id == id;
               });
    }
    void setFriend(const std::string &id, const std::string &name, bool add) {
        const auto profile = activeProfile();
        if (!profile)
            return;
        Settings next = settings;
        next.onlineFriends.erase(std::remove_if(next.onlineFriends.begin(), next.onlineFriends.end(),
                                                [&](const Settings::Friend &entry) {
                                                    return entry.owner == profile->id && entry.id == id;
                                                }),
                                 next.onlineFriends.end());
        if (add)
            next.onlineFriends.push_back({profile->id, id, name});
        write_settings(settings_path(), next);
        settings = std::move(next);
    }
    void refreshOnlineLists() {
        std::string selectedRoom, selectedPlayer;
        if (const auto row = SendMessageW(controls[RoomList], LB_GETCURSEL, 0, 0); row >= 0 && size_t(row) < roomRows.size())
            selectedRoom = roomRows[size_t(row)].id;
        if (const auto row = SendMessageW(controls[PlayerList], LB_GETCURSEL, 0, 0); row >= 0 && size_t(row) < playerRows.size())
            selectedPlayer = playerRows[size_t(row)].first;
        roomRows = directory ? directory->rooms() : std::vector<online::Room>{};
        std::stable_sort(roomRows.begin(), roomRows.end(), [](const online::Room &a, const online::Room &b) {
            return (a.version == application_version) > (b.version == application_version);
        });
        std::map<std::string, int> pings;
        {
            std::lock_guard lock(onlineShared->mutex);
            pings = onlineShared->pings;
            for (const auto &room : roomRows) {
                if (onlineShared->pings.count(room.id) || onlineShared->pinging.count(room.id) || room.version != application_version)
                    continue;
                onlineShared->pinging.insert(room.id);
                std::thread([shared = onlineShared, room] {
                    const auto ping = netplay::probe_room(room.address, room.port, 2000);
                    std::lock_guard lock(shared->mutex);
                    shared->pings[room.id] = ping ? int(*ping) : -1;
                    shared->pinging.erase(room.id);
                    if (shared->window)
                        PostMessageW(shared->window, onlineChanged, 0, 0);
                }).detach();
            }
        }
        SendMessageW(controls[RoomList], WM_SETREDRAW, FALSE, 0);
        SendMessageW(controls[RoomList], LB_RESETCONTENT, 0, 0);
        int roomSelection = -1;
        for (const auto &room : roomRows) {
            std::wstring ping = L"…";
            if (const auto found = pings.find(room.id); found != pings.end())
                ping = found->second < 0 ? L"via relay" : std::to_wstring(found->second) + L" ms";
            std::wstring row = wide(room.name.empty() ? room.host + "'s room" : room.name) + L" · " + wide(room.host) +
                               L" · " + (room.version == application_version ? ping : L"version " + wide(room.version)) +
                               L" · " + (room.locked ? L"Password" : L"Open");
            if (room.id == selectedRoom)
                roomSelection = int(SendMessageW(controls[RoomList], LB_GETCOUNT, 0, 0));
            SendMessageW(controls[RoomList], LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(row.c_str()));
        }
        if (roomSelection >= 0)
            SendMessageW(controls[RoomList], LB_SETCURSEL, WPARAM(roomSelection), 0);
        SendMessageW(controls[RoomList], WM_SETREDRAW, TRUE, 0);
        playerRows.clear();
        std::vector<std::wstring> labels;
        const auto profile = activeProfile();
        const auto players = directory ? directory->players() : std::vector<online::Player>{};
        std::string selectedFriend;
        if (const auto row = SendMessageW(controls[FriendList], LB_GETCURSEL, 0, 0); row >= 0 && size_t(row) < friendRows.size())
            selectedFriend = friendRows[size_t(row)].first;
        friendRows.clear();
        std::vector<std::pair<bool, std::wstring>> friendLabels;
        if (profile)
            for (const auto &entry : settings.onlineFriends)
                if (entry.owner == profile->id) {
                    const auto found = std::find_if(players.begin(), players.end(), [&](const auto &player) { return player.id == entry.id; });
                    friendRows.emplace_back(entry.id, found == players.end() ? entry.name : found->name);
                    friendLabels.emplace_back(found != players.end(), wide(friendRows.back().second) + L" · " +
                                                  (found == players.end() ? std::wstring(L"offline") : wide(found->status)));
                }
        std::vector<size_t> order(friendRows.size());
        for (size_t i = 0; i < order.size(); ++i)
            order[i] = i;
        std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return friendLabels[a].first > friendLabels[b].first; });
        {
            auto rows = friendRows;
            auto texts = friendLabels;
            for (size_t i = 0; i < order.size(); ++i) {
                friendRows[i] = rows[order[i]];
                friendLabels[i] = texts[order[i]];
            }
        }
        SendMessageW(controls[FriendList], WM_SETREDRAW, FALSE, 0);
        SendMessageW(controls[FriendList], LB_RESETCONTENT, 0, 0);
        for (size_t i = 0; i < friendRows.size(); ++i) {
            const auto label = (friendLabels[i].first ? L"On · " : L"Off · ") + friendLabels[i].second;
            SendMessageW(controls[FriendList], LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
            if (friendRows[i].first == selectedFriend)
                SendMessageW(controls[FriendList], LB_SETCURSEL, WPARAM(i), 0);
        }
        SendMessageW(controls[FriendList], WM_SETREDRAW, TRUE, 0);
        for (const auto &player : players) {
            if (isFriend(player.id))
                continue;
            playerRows.emplace_back(player.id, player.name);
            labels.push_back(wide(player.name) + (player.impostor ? L" (name not verified)" : L"") + L" · " + wide(player.status) +
                             (player.version == application_version ? L"" : L" · version " + wide(player.version)));
        }
        SendMessageW(controls[PlayerList], WM_SETREDRAW, FALSE, 0);
        SendMessageW(controls[PlayerList], LB_RESETCONTENT, 0, 0);
        int playerSelection = -1;
        for (size_t i = 0; i < labels.size(); ++i) {
            if (playerRows[i].first == selectedPlayer)
                playerSelection = int(i);
            SendMessageW(controls[PlayerList], LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(labels[i].c_str()));
        }
        if (playerSelection >= 0)
            SendMessageW(controls[PlayerList], LB_SETCURSEL, WPARAM(playerSelection), 0);
        SendMessageW(controls[PlayerList], WM_SETREDRAW, TRUE, 0);
        if (selectedPlayerId().empty() || !isFriend(selectedPlayerId()))
            SetWindowTextW(controls[AddFriend], L"Add friend");
        else
            SetWindowTextW(controls[AddFriend], L"Remove friend");
        layout();
        InvalidateRect(hwnd, nullptr, FALSE);
    }
    std::string selectedPlayerId() const {
        const auto row = SendMessageW(controls.at(PlayerList), LB_GETCURSEL, 0, 0);
        if (row >= 0 && size_t(row) < playerRows.size())
            return playerRows[size_t(row)].first;
        const auto chosen = SendMessageW(controls.at(FriendList), LB_GETCURSEL, 0, 0);
        return chosen >= 0 && size_t(chosen) < friendRows.size() ? friendRows[size_t(chosen)].first : std::string();
    }
    bool hasNotice() const {
        std::lock_guard lock(onlineShared->mutex);
        return !onlineShared->notices.empty();
    }
    std::wstring noticeText() const {
        std::lock_guard lock(onlineShared->mutex);
        if (onlineShared->notices.empty())
            return {};
        const auto &notice = onlineShared->notices.front();
        auto who = [&](const std::string &id, const std::string &name) {
            const bool verified = directory && directory->name_state(name, id) == online::NameState::mine;
            return wide(name) + (verified ? L"" : L" (name not verified)");
        };
        if (notice.invite)
            return who(notice.inviteMessage.from_id, notice.inviteMessage.from_name) + L" invited you to play";
        if (notice.friendMessage.kind == online::FriendKind::request)
            return who(notice.friendMessage.from_id, notice.friendMessage.from_name) + L" sent you a friend request";
        return {};
    }
    void notify(const std::wstring &title, const std::wstring &message) {
        NOTIFYICONDATAW data{};
        data.cbSize = sizeof data;
        data.hWnd = hwnd;
        data.uID = 1;
        data.uFlags = NIF_ICON | NIF_TIP | NIF_INFO;
        data.hIcon = reinterpret_cast<HICON>(SendMessageW(hwnd, WM_GETICON, ICON_SMALL, 0));
        if (!data.hIcon)
            data.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
        wcsncpy_s(data.szTip, L"OpenKai2", _TRUNCATE);
        wcsncpy_s(data.szInfoTitle, title.c_str(), _TRUNCATE);
        wcsncpy_s(data.szInfo, message.c_str(), _TRUNCATE);
        data.dwInfoFlags = NIIF_INFO;
        Shell_NotifyIconW(trayAdded ? NIM_MODIFY : NIM_ADD, &data);
        trayAdded = true;
        FLASHWINFO flash{sizeof flash, hwnd, FLASHW_TRAY | FLASHW_TIMERNOFG, 0, 0};
        FlashWindowEx(&flash);
        MessageBeep(MB_ICONASTERISK);
    }
    void removeTray() {
        if (!trayAdded)
            return;
        NOTIFYICONDATAW data{};
        data.cbSize = sizeof data;
        data.hWnd = hwnd;
        data.uID = 1;
        Shell_NotifyIconW(NIM_DELETE, &data);
        trayAdded = false;
    }
    void noticesArrived() {
        std::vector<std::pair<std::wstring, std::wstring>> alerts;
        {
            std::lock_guard lock(onlineShared->mutex);
            auto &queue = onlineShared->notices;
            for (auto it = queue.begin(); it != queue.end();) {
                if (!it->invite && it->friendMessage.kind != online::FriendKind::request) {
                    const auto message = it->friendMessage;
                    it = queue.erase(it);
                    if (directory)
                        directory->clear_friend(message.from_id);
                    if (message.kind == online::FriendKind::accept && requestedFriends.count(message.from_id)) {
                        setFriend(message.from_id, message.from_name, true);
                        requestedFriends.erase(message.from_id);
                        alerts.emplace_back(L"Friend added", wide(message.from_name) + L" accepted your friend request.");
                    } else if (message.kind == online::FriendKind::remove)
                        setFriend(message.from_id, message.from_name, false);
                    continue;
                }
                if (!it->invite && isFriend(it->friendMessage.from_id)) {
                    if (directory) {
                        directory->clear_friend(it->friendMessage.from_id);
                        directory->send_friend(it->friendMessage.from_id, online::FriendKind::accept);
                    }
                    it = queue.erase(it);
                    continue;
                }
                ++it;
            }
            if (!queue.empty()) {
                const auto &latest = queue.back();
                if (latest.invite)
                    alerts.emplace_back(L"Invite", wide(latest.inviteMessage.from_name) + L" invited you to play Budokai 2.");
                else
                    alerts.emplace_back(L"Friend request", wide(latest.friendMessage.from_name) + L" wants to be friends.");
            }
        }
        if (!alerts.empty())
            notify(alerts.back().first, alerts.back().second);
        refreshOnlineLists();
        layout();
    }
    void answerNotice(bool accept) {
        Notice notice;
        {
            std::lock_guard lock(onlineShared->mutex);
            if (onlineShared->notices.empty())
                return;
            notice = onlineShared->notices.front();
            onlineShared->notices.pop_front();
        }
        if (notice.invite) {
            if (accept) {
                const auto room = directory ? directory->room(notice.inviteMessage.room_id) : std::nullopt;
                if (!room)
                    status = L"That room has closed.", error = true;
                else if (room->version != application_version)
                    status = L"That room uses another OpenKai2 version.", error = true;
                else if (!disc)
                    status = L"Choose your Budokai 2 ISO first.", error = true;
                else {
                    directJoin = false;
                    joinRoomId = room->id;
                    launchOnline(false, wide(room->address) + L":" + std::to_wstring(room->port), wide(notice.inviteMessage.token));
                }
            } else
                status = L"Invite declined.";
        } else {
            const auto &message = notice.friendMessage;
            if (directory)
                directory->clear_friend(message.from_id);
            if (accept) {
                setFriend(message.from_id, message.from_name, true);
                if (directory)
                    directory->send_friend(message.from_id, online::FriendKind::accept);
                status = wide(message.from_name) + L" is now your friend.";
            } else
                status = L"Friend request declined.";
        }
        refreshOnlineLists();
        layout();
    }
    void toggleFriend() {
        const auto id = selectedPlayerId();
        if (id.empty() || !directory) {
            status = L"Choose a player.";
            error = true;
            return;
        }
        const auto found = std::find_if(playerRows.begin(), playerRows.end(), [&](const auto &row) { return row.first == id; });
        const std::string name = found == playerRows.end() ? std::string() : found->second;
        if (isFriend(id)) {
            setFriend(id, name, false);
            directory->send_friend(id, online::FriendKind::remove);
            status = wide(name) + L" removed from friends.";
        } else {
            requestedFriends.insert(id);
            directory->send_friend(id, online::FriendKind::request);
            status = L"Friend request sent to " + wide(name) + L".";
        }
        error = false;
        refreshOnlineLists();
    }
    void invitePlayer() {
        const auto id = selectedPlayerId();
        const auto player = directory ? directory->player(id) : std::nullopt;
        if (!player) {
            status = L"Choose a player who is online.";
            error = true;
            return;
        }
        if (player->version != application_version) {
            status = wide(player->name) + L" uses another OpenKai2 version.";
            error = true;
            return;
        }
        if (running() || room) {
            if (!hostingRoom.empty() && directory->room(hostingRoom)) {
                directory->invite(id, hostingRoom, hostingToken);
                status = L"Invite sent to " + wide(player->name) + L".";
            } else {
                status = room ? L"Your room is still opening. Try again in a moment."
                              : L"Close the running game first, or host a room to invite players.";
                error = true;
            }
            return;
        }
        pendingInvitee = id;
        inviteDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(90);
        startOnline(true);
        status = L"Starting your room. " + wide(player->name) + L" is invited as soon as it opens.";
    }
    void onlineTick() {
        ensureOnline();
        roomTick();
        const bool runningNow = running();
        if (directory && directory->connected() && runningNow != wasRunning)
            directory->set_status(runningNow ? "in a match" : room ? (roomHost ? "hosting" : "joining") : "online");
        if (!runningNow && wasRunning) {
            hostingRoom.clear();
            pendingInvitee.clear();
            if (room && room->status().phase == netplay::RoomPhase::ready)
                room.reset();
            refreshEnabled();
        }
        wasRunning = runningNow;
        if (!pendingInvitee.empty() && directory) {
            if (!hostingRoom.empty() && directory->room(hostingRoom)) {
                directory->invite(pendingInvitee, hostingRoom, hostingToken);
                const auto player = directory->player(pendingInvitee);
                status = L"Invite sent to " + (player ? wide(player->name) : std::wstring(L"the player")) + L".";
                pendingInvitee.clear();
                InvalidateRect(hwnd, nullptr, FALSE);
            } else if (std::chrono::steady_clock::now() > inviteDeadline) {
                pendingInvitee.clear();
                status = L"Your room did not open in time, so the invite was not sent.";
                error = true;
            }
        }
    }
    void joinRoom() {
        const auto row = SendMessageW(controls[RoomList], LB_GETCURSEL, 0, 0);
        if (row < 0 || size_t(row) >= roomRows.size()) {
            status = L"Choose a room to join.";
            error = true;
            return;
        }
        const auto room = roomRows[size_t(row)];
        if (room.version != application_version) {
            status = L"This room uses OpenKai2 " + wide(room.version) + L". Both players need the same version.";
            error = true;
            return;
        }
        const auto password = textOf(JoinPassword);
        if (room.locked && password.empty()) {
            status = L"This room has a password. Type it in the box below the list.";
            error = true;
            SetFocus(controls[JoinPassword]);
            return;
        }
        directJoin = false;
        joinRoomId = room.id;
        launchOnline(false, wide(room.address) + L":" + std::to_wstring(room.port), password);
    }
    void launchOnline(bool host, const std::wstring &target, const std::wstring &password) {
        if (!saveOnline() || !profileReady() || room)
            return;
        netplay::RoomOptions options;
        options.host = host;
        options.name = settings.onlineName;
        options.version = application_version;
        options.delay = settings.onlineDelay;
        options.lobby = settings.onlineLobby;
        options.port = netplay::default_port;
        if (host) {
            hostingRoom = online::new_id();
            hostingToken = online::new_id();
            const unsigned visibility = pendingInvitee.empty() ? settings.onlineVisibility
                                                               : settings.onlineVisibility == 0 ? 0u : 1u;
            options.password = utf8(password);
            options.invite_token = hostingToken;
            options.room = settings.onlineRoom;
            options.room_id = hostingRoom;
            options.listed = visibility != 2;
            options.hidden = visibility == 1;
        } else {
            hostingRoom.clear();
            auto address = utf8(target);
            uint16_t port = netplay::default_port;
            if (const auto colon = address.rfind(':'); colon != std::string::npos && address.find(':') == colon) {
                port = uint16_t(std::clamp(std::atoi(address.substr(colon + 1).c_str()), 1, 65535));
                address.resize(colon);
            }
            options.address = address;
            options.port = port;
            options.password = utf8(password);
            options.room_id = std::exchange(joinRoomId, std::string());
            joinAddress = address;
            roomPort = port;
        }
        roomHost = host;
        room = std::make_unique<netplay::Room>(options);
        room->start();
        if (directory && directory->connected())
            directory->set_status(host ? "hosting" : "joining");
        status = host ? L"Opening your room\u2026" : L"Joining " + target + L"\u2026";
        error = false;
        refreshEnabled();
        InvalidateRect(hwnd, nullptr, FALSE);
    }
    void closeRoom(const std::wstring &message) {
        room.reset();
        hostingRoom.clear();
        pendingInvitee.clear();
        if (directory && directory->connected())
            directory->set_status("online");
        status = message;
        error = false;
        refreshEnabled();
        InvalidateRect(hwnd, nullptr, FALSE);
    }
    void roomTick() {
        if (!room || running())
            return;
        const auto state = room->status();
        std::wstring next = wide(state.message);
        if (state.phase == netplay::RoomPhase::failed) {
            std::cout << "[launcher:online] room " << (roomHost ? "hosting" : "joining") << " failed: " << state.message
                      << std::endl;
            room.reset();
            hostingRoom.clear();
            status = next.empty() ? L"Could not connect." : next;
            error = true;
            refreshEnabled();
            InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }
        if (state.phase == netplay::RoomPhase::ready) {
            std::wstring arguments = L" --netplay=" +
                                     (roomHost ? L"host:" + std::to_wstring(state.port)
                                      : state.relayed ? L"join:127.0.0.1:" + std::to_wstring(state.local_port)
                                                      : L"join:" + wide(joinAddress) + L":" + std::to_wstring(roomPort)) +
                                     L" --netplay-delay=" + std::to_wstring(state.delay) + L" " +
                                     quote_argument(L"--netplay-name=" + wide(settings.onlineName)) + L" " +
                                     quote_argument(L"--netplay-password=" + wide(state.key));
            onlineArguments = arguments;
            std::cout << "[launcher:online] room ready with " << state.opponent << " ping " << state.ping_ms
                      << " ms delay " << state.delay << (state.relayed ? " via relay" : " direct") << std::endl;
            start(Job::launch, settings.iso);
            status = L"Connected to " + wide(state.opponent) + L" (" + std::to_wstring(state.ping_ms) +
                     L" ms). Starting the match\u2026";
            error = false;
            refreshEnabled();
            InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }
        if (!next.empty() && next != status) {
            std::cout << "[launcher:online] " << state.message << std::endl;
            status = next;
            error = false;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
    }
    void startOnline(bool host) {
        if (!saveOnline())
            return;
        if (host) {
            launchOnline(true, L"", wide(settings.onlinePassword));
            return;
        }
        if (settings.onlineAddress.empty()) {
            status = L"Type the host's address to join directly.";
            error = true;
            return;
        }
        std::wstring target = wide(settings.onlineAddress);
        if (std::count(settings.onlineAddress.begin(), settings.onlineAddress.end(), ':') != 1)
            target += L":" + std::to_wstring(settings.onlinePort);
        directJoin = true;
        launchOnline(false, target, textOf(JoinPassword));
    }
    void saveOptions() {
        Settings next = settings;
        next.renderer = selected(Renderer) == 1   ? "gpu"
                        : selected(Renderer) == 2 ? "vulkan"
                        : selected(Renderer) == 3 ? "cpu"
                                                  : "auto";
        next.fps = rates.at(size_t(selected(Fps)));
        const auto size = sizes.at(size_t(selected(Resolution)));
        next.width = size.first;
        next.height = size.second;
        next.windowMode = unsigned(selected(WindowMode));
        next.antialiasing = unsigned(selected(Antialiasing));
        next.textureFilter = unsigned(selected(Filtering));
        next.upscaler = unsigned(selected(Upscaler));
        next.internalScale = unsigned(std::max(0, selected(InternalScale))) + 1u;
        next.hdTextures = unsigned(std::max(0, selected(HdTextures)));
        next.music = selected(Music) != 1;
        next.vsync = selected(Vsync) != 0;
        next.cpuDifficulty = unsigned(std::max(0, selected(CpuDifficulty)));
        next.autoBackups = selected(AutoBackup) != 0;
        next.discordPresence = true;
        next.speedrun = unsigned(selected(SpeedrunMode));
        next.permadeath = !speedrun::tournament(speedrun::Category(next.speedrun)) && selected(Permadeath) != 0;
        if (speedrun::tournament(speedrun::Category(next.speedrun)))
            SendMessageW(controls[Permadeath], CB_SETCURSEL, 0, 0);
        next.fpsCounter = selected(FpsCounter) != 0;
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
    bool keyboardShortcut(int id) {
        const HWND focus = GetFocus();
        auto focused = [&](int control) { return controls.count(control) && controls.at(control) == focus; };
        if (id == IDOK) {
            if (focused(OnlineName))
                addProfile();
            else if (focused(JoinPassword) && disc && !room)
                joinRoom();
            else if (focused(OnlineAddress) && disc && !room) {
                directJoin = true;
                startOnline(false);
            } else
                return false;
            InvalidateRect(hwnd, nullptr, FALSE);
            return true;
        }
        if (id == IDCANCEL && room) {
            closeRoom(roomHost ? L"Room closed." : L"Stopped joining.");
            return true;
        }
        return false;
    }
    void command(int id, int notification) {
        lastInput = std::chrono::steady_clock::now();
        if ((id == IDOK || id == IDCANCEL) && keyboardShortcut(id))
            return;
        if (const auto found = controls.find(id); found != controls.end() &&
            (notification == CBN_DROPDOWN || notification == CBN_CLOSEUP) &&
            (GetWindowLongPtrW(found->second, GWL_STYLE) & CBS_DROPDOWNLIST) == CBS_DROPDOWNLIST) {
            InvalidateRect(found->second, nullptr, FALSE);
        }
        if (picking)
            return;
        if (id >= PlayTab && id <= UpdatesTab) {
            if (page == id - PlayTab) {
                SetFocus(controls[id]);
                return;
            }
            page = id - PlayTab;
            if (settings.startPage && settings.lastPage != unsigned(page)) {
                settings.lastPage = unsigned(page);
                try {
                    write_settings(settings_path(), settings);
                } catch (const std::exception &) {
                }
            }
            if (page == modsPage)
                refreshMods();
            renderFrame();
            layout();
            InvalidateRect(hwnd, nullptr, FALSE);
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
        if (((id >= Renderer && id <= Music) || (id >= SpeedrunMode && id <= FpsCounter) || id == SpeedrunCpu) &&
            notification == CBN_SELCHANGE) {
            if (id == SpeedrunCpu)
                SendMessageW(controls[CpuDifficulty], CB_SETCURSEL, selected(SpeedrunCpu), 0);
            const bool previousHd = settings.hdTextures != 0;
            const bool raisedScale = id == HdTextures && selected(HdTextures) > 0 && selected(InternalScale) <= 0;
            if (raisedScale)
                SendMessageW(controls[InternalScale], CB_SETCURSEL, 1, 0);
            if (id == InternalScale && selected(InternalScale) <= 0 && selected(HdTextures) > 0)
                SendMessageW(controls[HdTextures], CB_SETCURSEL, 0, 0);
            saveOptions();
            SendMessageW(controls[SpeedrunCpu], CB_SETCURSEL, std::min(settings.cpuDifficulty, 6u), 0);
            if (id == SpeedrunMode || id == Permadeath || id == SpeedrunCpu || id == CpuDifficulty) {
                refreshSplits();
                refreshTimingRules();
            }
            if (id == SpeedrunCpu)
                status = settings.cpuDifficulty ? L"CPU difficulty Z" + std::to_wstring(settings.cpuDifficulty) +
                                                      L". Runs at this difficulty are timed as their own category."
                                                : std::wstring(L"CPU difficulty follows the game's own setting.");
            if (id == SpeedrunMode || id == Permadeath)
                status = settings.speedrun
                             ? std::wstring(L"Speedrun mode: ") + wide(speedrun::category_name(speedrun::Category(settings.speedrun))) +
                                   (settings.permadeath && !speedrun::tournament(speedrun::Category(settings.speedrun)) ? L" with permadeath" : L"") +
                                   L". Mods stay off while it is on; logs go to the logs\\speedruns folder."
                             : L"Speedrun mode is off.";
            if (id == Music)
                status = settings.music ? std::wstring(L"Music is on.")
                                        : std::wstring(L"Music is off. Voices, sound effects and videos still play. Applies the next time the game starts.");
            else if (id == HdTextures || (id == InternalScale && settings.hdTextures && settings.internalScale == 1))
                status = settings.hdTextures == 0 ? std::wstring(L"HD textures are off.")
                         : settings.internalScale == 1
                             ? std::wstring(L"HD textures need an internal resolution of 2x or higher and the GPU or Vulkan renderer.")
                         : settings.hdTextures == 1 ? std::wstring(L"Textures are upscaled from your own disc while you play. Nothing is downloaded.")
                         : settings.hdTextures == 4 ? std::wstring(L"Each texture the game uses is saved once to textures\\dump as a PNG named by its hash. "
                                                                   L"Edit a copy at any size and put it in the textures folder.")
                                                    : std::wstring(L"PNG files named by texture hash in the textures folder replace the game's textures.") +
                                                          (settings.hdTextures == 3 ? L" Textures without a replacement are upscaled." : L"");
            else if (id == InternalScale)
                status = settings.internalScale > 1
                             ? L"Internal resolution " + std::to_wstring(settings.internalScale) +
                                   L"x: 3D scenes render at a higher resolution with the GPU and Vulkan renderers. "
                                   L"It needs more graphics power and applies the next time the game starts."
                             : std::wstring(L"Internal resolution: native, as on a PS2.");
            if (id == HdTextures && raisedScale)
                status += L" Internal resolution is now 2x, which HD textures need.";
            if (id == InternalScale && settings.internalScale == 1 && previousHd)
                status = L"Internal resolution: native, as on a PS2. HD textures are off because they need 2x or higher.";
            refreshEnabled();
            return;
        }
        if ((id == OnlineDelay || id == OnlineListed || id == OnlineProfile) && notification == CBN_SELCHANGE) {
            const std::string before = settings.onlineName;
            if (saveOnline())
                status = id == OnlineDelay    ? (settings.onlineDelay ? L"Input delay saved. It is used when you host."
                                                                      : L"Input delay is picked from the ping when someone joins.")
                         : id == OnlineListed ? (settings.onlineVisibility == 0   ? L"Your rooms are shown to everyone."
                                                 : settings.onlineVisibility == 1 ? L"Only players you invite can find your rooms."
                                                                                  : L"Your rooms are not listed. Share your address instead.")
                                              : L"Profile " + wide(settings.onlineName) + L" selected.";
            if (settings.onlineName != before)
                reconnect();
            return;
        }
        if ((id == OnlineAddress || id == OnlineRoom || id == OnlinePassword) &&
            notification == EN_KILLFOCUS) {
            saveOnline();
            return;
        }
        if (id == ProfileAdd && notification == BN_CLICKED) {
            addProfile();
            return;
        }
        if (id == ProfileRemove && notification == BN_CLICKED) {
            removeProfile();
            return;
        }
        if (id == RefreshRooms && (notification == BN_CLICKED || notification == 1)) {
            {
                std::lock_guard lock(onlineShared->mutex);
                onlineShared->pings.clear();
            }
            if (!directory || !directory->connected())
                lastConnect = {};
            refreshOnlineLists();
            status = directory && directory->connected() ? L"Room list refreshed." : L"Connecting to the online service\u2026";
            return;
        }
        if ((id == NoticeAccept || id == NoticeDecline) && notification == BN_CLICKED) {
            answerNotice(id == NoticeAccept);
            return;
        }
        if (id == AddFriend && notification == BN_CLICKED) {
            toggleFriend();
            return;
        }
        if ((id == PlayerList || id == FriendList) && notification == LBN_SELCHANGE) {
            SendMessageW(controls[id == PlayerList ? FriendList : PlayerList], LB_SETCURSEL, WPARAM(-1), 0);
            SetWindowTextW(controls[AddFriend], isFriend(selectedPlayerId()) ? L"Remove friend" : L"Add friend");
            return;
        }
        if (id == FriendList && notification == LBN_DBLCLK) {
            if (disc)
                invitePlayer();
            else
                status = L"Choose your Budokai 2 ISO first.", error = true;
            InvalidateRect(hwnd, nullptr, FALSE);
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
            next.internalScale = defaults.internalScale; next.hdTextures = defaults.hdTextures;
            write_settings(settings_path(), next);
            settings = std::move(next);
            const std::pair<int, int> choices[] = {{Renderer, 0}, {Fps, 6}, {Resolution, 0},
                {WindowMode, 0}, {Antialiasing, 0}, {Filtering, 0}, {Upscaler, 1}, {Vsync, 1}, {Aspect, 0}, {InternalScale, 0}, {HdTextures, 0}};
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
        if (id == TextureFolder) {
            const auto folder = textures_directory();
            std::error_code ignored;
            std::filesystem::create_directories(folder / "dump", ignored);
            ShellExecuteW(hwnd, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            return;
        }
        if (id == ModFolder) {
            const auto folder = mods::default_mods_directory();
            std::filesystem::create_directories(folder);
            ShellExecuteW(hwnd, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            return;
        }
        if (id >= ShortcutFirst && id <= ShortcutLast) {
            const auto shown = visibleTabs();
            const size_t slot = size_t(id - ShortcutFirst);
            if (slot < shown.size())
                command(PlayTab + shown[slot], BN_CLICKED);
            return;
        }
        if ((id == RandomizerMode || id == SpeedrunRandomizer) && notification == CBN_SELCHANGE) {
            settings.randomizer = selected(id) == 1;
            SendMessageW(controls[id == RandomizerMode ? SpeedrunRandomizer : RandomizerMode], CB_SETCURSEL, settings.randomizer ? 1 : 0, 0);
            saveLauncherLook(settings.randomizer
                                 ? L"Randomizer on: your character changes every Dragon World and World Tournament fight. The CPU is not changed."
                                 : L"Randomizer off.");
            refreshSplits();
            refreshTimingRules();
            InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }
        if (id == OpenLogs && notification == BN_CLICKED) {
            const auto folder = default_cache_directory().parent_path() / "logs";
            std::error_code ignored;
            std::filesystem::create_directories(folder, ignored);
            ShellExecuteW(hwnd, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            return;
        }
        if ((id == ColorCopy || id == ColorPaste || id == ColorSurprise) && notification == BN_CLICKED) {
            std::wstring message;
            if (id == ColorCopy) {
                const auto code = wide(look_code(settings.launcherTheme, settings.launcherAccent, settings.launcherColors));
                bool copied = false;
                if (OpenClipboard(hwnd)) {
                    EmptyClipboard();
                    if (HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, (code.size() + 1) * sizeof(wchar_t))) {
                        std::memcpy(GlobalLock(memory), code.c_str(), (code.size() + 1) * sizeof(wchar_t));
                        GlobalUnlock(memory);
                        copied = SetClipboardData(CF_UNICODETEXT, memory) != nullptr;
                        if (!copied)
                            GlobalFree(memory);
                    }
                    CloseClipboard();
                }
                status = copied ? L"Colors copied. Paste them into a message to share this look." : L"Could not copy to the clipboard.";
                error = !copied;
                InvalidateRect(hwnd, nullptr, FALSE);
                return;
            }
            if (id == ColorPaste) {
                std::wstring text;
                if (OpenClipboard(hwnd)) {
                    if (HANDLE data = GetClipboardData(CF_UNICODETEXT))
                        if (const auto *chars = static_cast<const wchar_t *>(GlobalLock(data))) {
                            text = chars;
                            GlobalUnlock(data);
                        }
                    CloseClipboard();
                }
                unsigned theme = 0, accent = 0;
                CustomColors colors{};
                if (!parse_look_code(utf8(trimmed(text)), theme, accent, colors)) {
                    status = L"The clipboard does not hold OpenKai2 colors. Copy them with Copy colors first.";
                    error = true;
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return;
                }
                settings.launcherTheme = theme;
                settings.launcherAccent = accent;
                settings.launcherColors = colors;
                message = std::wstring(L"Colors pasted: ") + themes[theme].name + L".";
            } else {
                const uint64_t tick = GetTickCount64();
                unsigned theme = unsigned(tick % theme_count);
                if (theme == settings.launcherTheme)
                    theme = (theme + 1u) % theme_count;
                settings.launcherTheme = theme;
                settings.launcherAccent = unsigned((tick / theme_count) % accent_count);
                settings.launcherColors = no_custom_colors();
                message = std::wstring(L"Surprise: ") + themes[theme].name +
                          (settings.launcherAccent ? std::wstring(L" with ") + accents[settings.launcherAccent].name : std::wstring()) + L".";
            }
            SendMessageW(controls[LauncherTheme], CB_SETCURSEL, settings.launcherTheme, 0);
            SendMessageW(controls[LauncherAccent], CB_SETCURSEL, settings.launcherAccent, 0);
            saveLauncherLook(message);
            refreshColors();
            restyle();
            return;
        }
        if (id == OpenColors && notification == BN_CLICKED) {
            page = colorsPage;
            refreshColors();
            layout();
            InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }
        if (id == CloseColors && notification == BN_CLICKED) {
            page = settingsPage;
            layout();
            InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }
        if (((id == ColorPick || id == ColorReset || id == ColorResetAll) && notification == BN_CLICKED) ||
            (id == ColorList && notification == LBN_DBLCLK)) {
            const auto row = SendMessageW(controls[ColorList], LB_GETCURSEL, 0, 0);
            std::wstring message;
            if (id == ColorResetAll) {
                if (MessageBoxW(hwnd, L"Return every color to the theme?", L"Reset colors", MB_YESNO | MB_ICONQUESTION) != IDYES)
                    return;
                settings.launcherColors = no_custom_colors();
                message = L"All colors follow the theme again.";
            } else if (row < 0 || row >= LRESULT(color_role_count)) {
                status = L"Pick a color first.";
                error = true;
                InvalidateRect(hwnd, nullptr, FALSE);
                return;
            } else if (id == ColorReset) {
                settings.launcherColors[size_t(row)] = -1;
                message = std::wstring(color_roles[row].name) + L" follows the theme again.";
            } else {
                static COLORREF saved[16]{};
                const auto look = make_theme(settings.launcherTheme, settings.launcherAccent, settings.launcherColors);
                for (unsigned i = 0; i < 16 && i < color_role_count; ++i)
                    if (!saved[i])
                        saved[i] = colorOf(look.*(color_roles[i + 6u < color_role_count ? i + 6u : i].member));
                const Rgb current = look.*(color_roles[row].member);
                CHOOSECOLORW choice{};
                choice.lStructSize = sizeof(choice);
                choice.hwndOwner = hwnd;
                choice.rgbResult = colorOf(current);
                choice.lpCustColors = saved;
                choice.Flags = CC_FULLOPEN | CC_RGBINIT | CC_ANYCOLOR;
                if (!ChooseColorW(&choice))
                    return;
                settings.launcherColors[size_t(row)] = int32_t(GetRValue(choice.rgbResult)) << 16 |
                                                       int32_t(GetGValue(choice.rgbResult)) << 8 | int32_t(GetBValue(choice.rgbResult));
                message = std::wstring(color_roles[row].name) + L" changed.";
            }
            saveLauncherLook(message);
            refreshColors();
            restyle();
            return;
        }
        if (id == ArrangeTabs && notification == BN_CLICKED) {
            page = tabsPage;
            refreshTabs();
            layout();
            InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }
        if (id == CloseTabs && notification == BN_CLICKED) {
            page = settingsPage;
            layout();
            InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }
        if ((id == TabUp || id == TabDown || id == TabToggle || id == TabDefaults) && notification == BN_CLICKED) {
            const auto row = SendMessageW(controls[TabList], LB_GETCURSEL, 0, 0);
            std::wstring message;
            if (id == TabDefaults) {
                settings.tabOrder = {0, 1, 2, 3, 4, 5, 6};
                settings.hiddenTabs = 0;
                message = L"Tabs restored to their original order.";
            } else if (row < 0 || row >= LRESULT(tab_count)) {
                status = L"Pick a tab first.";
                error = true;
                InvalidateRect(hwnd, nullptr, FALSE);
                return;
            } else {
                const unsigned tab = settings.tabOrder[size_t(row)];
                if (id == TabToggle) {
                    if ((fixed_tabs >> tab) & 1u) {
                        status = std::wstring(tab_names[tab]) + L" is always shown.";
                        error = true;
                        InvalidateRect(hwnd, nullptr, FALSE);
                        return;
                    }
                    settings.hiddenTabs ^= 1u << tab;
                    message = std::wstring(tab_names[tab]) + ((settings.hiddenTabs >> tab) & 1u ? L" is hidden." : L" is shown.");
                } else {
                    const LRESULT target = id == TabUp ? row - 1 : row + 1;
                    if (target < 0 || target >= LRESULT(tab_count))
                        return;
                    std::swap(settings.tabOrder[size_t(row)], settings.tabOrder[size_t(target)]);
                    SendMessageW(controls[TabList], LB_SETCURSEL, WPARAM(target), 0);
                    message = std::wstring(tab_names[tab]) + L" moved " + (id == TabUp ? L"up." : L"down.");
                }
            }
            saveLauncherLook(message);
            refreshTabs();
            layout();
            InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }
        if ((id == LauncherTheme || id == LauncherAccent || id == StartPage) && notification == CBN_SELCHANGE) {
            settings.launcherTheme = unsigned(std::max(0, selected(LauncherTheme)));
            settings.launcherAccent = unsigned(std::max(0, selected(LauncherAccent)));
            settings.startPage = unsigned(std::max(0, selected(StartPage)));
            settings.lastPage = unsigned(std::clamp(navigationPage(), 0, int(tab_count) - 1));
            saveLauncherLook(id == StartPage ? (settings.startPage ? L"The launcher opens on the last page you used." : L"The launcher opens on Play.")
                                             : L"Theme " + std::wstring(themes[settings.launcherTheme].name) + L" with " +
                                                   (settings.launcherAccent ? std::wstring(accents[settings.launcherAccent].name) + L" accents." : L"its own accent color."));
            if (id != StartPage)
                restyle();
            else
                InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }
        if (id == OpenOverlay && notification == BN_CLICKED) {
            page = overlayPage;
            refreshOverlayItems();
            layout();
            return;
        }
        if (id == CloseOverlay && notification == BN_CLICKED) {
            page = speedrunPage;
            layout();
            return;
        }
        if ((id == OverlayToggle && notification == BN_CLICKED) || (id == OverlayItems && notification == LBN_DBLCLK)) {
            const auto row = SendMessageW(controls[OverlayItems], LB_GETCURSEL, 0, 0);
            if (row < 0 || row >= LRESULT(speedrun::overlay_item_count)) {
                status = L"Choose an item to show or hide.";
                return;
            }
            if ((speedrun::overlay_required >> unsigned(row)) & 1u) {
                status = std::wstring(speedrun::overlay_item_text[row].name) +
                         L" are always shown so moderators can verify every run.";
                error = false;
                InvalidateRect(hwnd, nullptr, FALSE);
                return;
            }
            settings.overlayItems ^= 1u << unsigned(row);
            const bool on = (settings.overlayItems >> unsigned(row)) & 1u;
            refreshOverlayItems();
            saveOverlay(std::wstring(speedrun::overlay_item_text[row].name) + (on ? L" is shown." : L" is hidden."));
            return;
        }
        if (((id >= OverlayCorner && id <= OverlaySplits) || (id >= OverlayText && id <= OverlayDecimals) || id == OverlayMargin || id == OverlayShape) &&
            notification == CBN_SELCHANGE) {
            saveOverlay(L"Overlay layout saved.");
            return;
        }
        if ((id == OverlayTitle || id == OverlayNames) && notification == EN_KILLFOCUS) {
            saveOverlay(id == OverlayTitle ? L"Overlay title saved." : L"Split names saved. They replace the names in order: the first name is the first split.");
            return;
        }
        if (id == SplitList && notification == LBN_SELCHANGE) {
            const auto row = SendMessageW(controls[SplitList], LB_GETCURSEL, 0, 0);
            if (row >= 0 && size_t(row) < splitBook.best().size())
                SetWindowTextW(controls[SplitTime], wide(splitTime(splitBook.best()[size_t(row)])).c_str());
            InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }
        if ((id == SplitSet || id == SplitAdd || id == SplitRemove || id == SplitReset) && notification == BN_CLICKED) {
            if (!settings.speedrun) {
                status = L"Choose a speedrun category first; each category keeps its own splits.";
                error = true;
                InvalidateRect(hwnd, nullptr, FALSE);
                return;
            }
            const auto row = SendMessageW(controls[SplitList], LB_GETCURSEL, 0, 0);
            double time = 0;
            const bool typed = speedrun::parse_time(utf8(trimmed(textOf(SplitTime))), time);
            std::wstring message;
            bool changed = false;
            if (id == SplitReset) {
                if (MessageBoxW(hwnd, L"Clear the best run and best segments for this category? Attempt history is kept.",
                                L"Reset best", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                    splitBook.clear_best();
                    changed = true;
                    message = L"Best run cleared for this category.";
                }
            } else if (id == SplitRemove) {
                changed = row >= 0 && splitBook.remove_split(size_t(row));
                message = changed ? L"Split removed." : L"Pick a split to remove.";
            } else if (!typed) {
                message = L"Type a time like 12:34.56 or 1:02:03.";
            } else if (id == SplitSet) {
                changed = row >= 0 && splitBook.set_split(size_t(row), time);
                message = changed ? L"Split time changed." : L"Pick a split to change.";
            } else {
                splitBook.add_split(time);
                changed = true;
                message = L"Split added.";
            }
            if (changed) {
                try {
                    splitBook.save(splitsFile());
                } catch (const std::exception &failure) {
                    message = wide(failure.what());
                    changed = false;
                }
            }
            status = message;
            error = !changed;
            refreshSplits();
            InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }
        if (id == OverlayDefaults && notification == BN_CLICKED) {
            const Settings defaults;
            settings.overlayItems = defaults.overlayItems;
            for (auto [control, value] : {std::pair<int, unsigned>{OverlayCorner, defaults.overlayCorner},
                                          {OverlaySize, defaults.overlaySize},
                                          {OverlayBackground, defaults.overlayBackground},
                                          {OverlaySplits, defaults.overlaySplits},
                                          {OverlayText, defaults.overlayColors[0]},
                                          {OverlayAhead, defaults.overlayColors[1]},
                                          {OverlayBehind, defaults.overlayColors[2]},
                                          {OverlayGold, defaults.overlayColors[3]},
                                          {OverlayDecimals, defaults.overlayDecimals},
                                          {OverlayMargin, defaults.overlayMargin},
                                          {OverlayShape, defaults.overlayShape}})
                SendMessageW(controls[control], CB_SETCURSEL, value, 0);
            SetWindowTextW(controls[OverlayTitle], L"");
            SetWindowTextW(controls[OverlayNames], L"");
            refreshOverlayItems();
            saveOverlay(L"Overlay restored to its defaults.");
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
        else if ((id == HostGame || id == JoinGame || id == JoinRoom) && notification == BN_CLICKED && room)
            closeRoom(L"Room closed.");
        else if ((id == HostGame || id == JoinGame) && notification == BN_CLICKED && disc)
            startOnline(id == HostGame);
        else if (((id == JoinRoom && notification == BN_CLICKED) || (id == RoomList && notification == LBN_DBLCLK)) && disc && !room)
            joinRoom();
        else if (((id == InvitePlayer && notification == BN_CLICKED) || (id == PlayerList && notification == LBN_DBLCLK)) && disc)
            invitePlayer();
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
    } else if (message == WM_ERASEBKGND && (GetWindowLongPtrW(hwnd, GWL_STYLE) & BS_TYPEMASK) == BS_OWNERDRAW) {
        return 1;
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
        const bool dropped = SendMessageW(hwnd, CB_GETDROPPEDSTATE, 0, 0) != 0;
        const bool hot = enabled && GetPropW(hwnd, L"OpenKai2.Hot") != nullptr;
        const COLORREF face = !enabled ? panel : dropped ? mix(field, orange, 14) : hot ? mix(field, ink, 10) : field;
        owner->backdrop(dc, hwnd, rect, panel);
        owner->box(dc, rect, face,
                   focused || dropped ? orange : hot ? mix(border, orange, 110) : enabled ? border : mix(panel, border, 140), 8);
        if (enabled)
            fill(dc, {rect.left + owner->p(8), rect.top + 1, rect.right - owner->p(8), rect.top + 2}, mix(face, ink, 16));
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
        const int x = rect.right - owner->p(18), y = rect.bottom / 2, flip = dropped ? -1 : 1;
        fill(dc, {x - owner->p(16), rect.top + owner->p(8), x - owner->p(15), rect.bottom - owner->p(8)},
                    mix(face, border, 200));
        auto pen = CreatePen(PS_SOLID, owner->p(2), enabled ? (focused || dropped || hot ? orange : muted) : faint);
        auto previous = SelectObject(dc, pen);
        MoveToEx(dc, x - owner->p(4), y - flip * owner->p(2), nullptr);
        LineTo(dc, x, y + flip * owner->p(2));
        LineTo(dc, x + owner->p(4) + (flip > 0 ? 1 : 0), y - flip * owner->p(2) - (flip > 0 ? 1 : 0));
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
    if (message == WM_MOUSEMOVE && !GetPropW(hwnd, L"OpenKai2.Hot")) {
        SetPropW(hwnd, L"OpenKai2.Hot", reinterpret_cast<HANDLE>(1));
        TRACKMOUSEEVENT track{sizeof track, TME_LEAVE, hwnd, 0};
        TrackMouseEvent(&track);
        InvalidateRect(hwnd, nullptr, FALSE);
    }
    if (message == WM_MOUSELEAVE) {
        RemovePropW(hwnd, L"OpenKai2.Hot");
        InvalidateRect(hwnd, nullptr, FALSE);
    }
    if (message == WM_NCDESTROY)
        RemovePropW(hwnd, L"OpenKai2.Hot");
    const auto result = original ? CallWindowProcW(original, hwnd, message, wp, lp)
                                 : DefWindowProcW(hwnd, message, wp, lp);
    if (message == WM_SETFOCUS || message == WM_KILLFOCUS || message == WM_ENABLE ||
        message == CB_SETCURSEL || message == CB_SHOWDROPDOWN || message == WM_LBUTTONDOWN)
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
            if (wp == 2)
                self->cycleFrame();
            else
                self->timer();
            return 0;
        case onlineChanged:
            self->refreshOnlineLists();
            self->refreshEnabled();
            return 0;
        case onlineNotice:
            self->noticesArrived();
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
            limits->ptMinTrackSize = {self->p(1000), self->p(720)};
            return 0;
        }
        case WM_DPICHANGED: {
            self->dpi = HIWORD(wp);
            self->ambient.clear();
            self->fonts.make(self->dpi, self->letteringFace);
            for (auto [id, c] : self->controls)
                SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(self->fonts.body), TRUE);
            SendMessageW(self->controls[TimingRules], WM_SETFONT, reinterpret_cast<WPARAM>(self->fonts.caption), TRUE);
            auto *r = reinterpret_cast<RECT *>(lp);
            SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            self->layout();
            return 0;
        }
        case WM_CTLCOLORBTN:
        case WM_CTLCOLORSTATIC: {
            const auto target = reinterpret_cast<HWND>(lp);
            RECT bounds{};
            GetWindowRect(target, &bounds);
            MapWindowPoints(nullptr, hwnd, reinterpret_cast<POINT *>(&bounds), 2);
            const COLORREF color = GetDlgCtrlID(target) == TimingRules ? panel
                                   : bounds.left < self->p(sidebarWidth) ? sidebar
                                   : message == WM_CTLCOLORSTATIC ? field : panel;
            SetDCBrushColor(reinterpret_cast<HDC>(wp), color);
            SetTextColor(reinterpret_cast<HDC>(wp), message == WM_CTLCOLORSTATIC ? muted : ink);
            SetBkColor(reinterpret_cast<HDC>(wp), color);
            return reinterpret_cast<LRESULT>(GetStockObject(DC_BRUSH));
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
                              target == self->controls[SourceFiles] || target == self->controls[TargetList] ||
                              target == self->controls[RoomList] || target == self->controls[PlayerList];
            const COLORREF color = target == self->controls[FriendList] ? sidebar : list ? field : panel;
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
            KillTimer(hwnd, 2);
            self->removeTray();
            {
                std::lock_guard lock(self->onlineShared->mutex);
                self->onlineShared->window = nullptr;
            }
            if (self->directory)
                self->directory->close();
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
    int height = std::min<int>(state.p(760), int(work.bottom - work.top) - state.p(24));
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
    state.chrome();
    ShowWindow(hwnd, state.settings.windowMaximized ? SW_SHOWMAXIMIZED : SW_SHOW);
    DragAcceptFiles(hwnd, TRUE);
    UpdateWindow(hwnd);
    if (!state.settings.iso.empty())
        state.start(Job::verify, state.settings.iso);
    ACCEL shortcuts[] = {{FCONTROL | FVIRTKEY, '1', ShortcutFirst},
                         {FCONTROL | FVIRTKEY, '2', ShortcutFirst + 1},
                         {FCONTROL | FVIRTKEY, '3', ShortcutFirst + 2},
                         {FCONTROL | FVIRTKEY, '4', ShortcutFirst + 3},
                         {FCONTROL | FVIRTKEY, '5', ShortcutFirst + 4},
                         {FCONTROL | FVIRTKEY, '6', ShortcutFirst + 5},
                         {FCONTROL | FVIRTKEY, '7', ShortcutFirst + 6},
                         {FCONTROL | FVIRTKEY, 'R', RefreshRooms},
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
