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
#include "mods/disc_identity.h"
#include "mods/importer.h"
#include "mods/mod_library.h"
#include "mods/roster.h"
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
#include <vector>
#include <windowsx.h>

namespace openkai2::launcher {
namespace {
constexpr COLORREF background = RGB(11, 14, 21), sidebar = RGB(15, 19, 29), panel = RGB(20, 25, 37);
constexpr COLORREF field = RGB(26, 32, 47), raised = RGB(33, 40, 58), hover = RGB(42, 51, 72);
constexpr COLORREF border = RGB(42, 51, 70), ink = RGB(241, 244, 249), muted = RGB(146, 157, 179);
constexpr COLORREF faint = RGB(92, 103, 126), orange = RGB(255, 140, 38), gold = RGB(255, 201, 82);
constexpr COLORREF red = RGB(255, 112, 102), green = RGB(98, 214, 152);
constexpr COLORREF heroFrom = RGB(52, 27, 13), heroTo = RGB(18, 24, 39);

// Layout is expressed in 96-DPI units and scaled with p().
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
    ModList = 180,
    ModToggle,
    ModUp,
    ModDown,
    ModRefresh,
    ModFolder,
    ModName,
    ModCreate,
    OpenImporter,
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
    SaveReport = 220
};
// Sidebar pages, then the import tool, which opens from the Mods page.
constexpr int pageCount = 4, modsPage = 2, settingsPage = 3, importPage = 4;
constexpr std::array<unsigned, 4> audioVolumes{100u, 75u, 50u, 25u};
// Off, then the volume steps; a hand-edited volume selects the nearest step.
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
void star(HDC dc, int cx, int cy, int radius, COLORREF color) {
    POINT points[10];
    for (int i = 0; i < 10; ++i) {
        const double angle = -1.5707963 + i * 0.6283185;
        const double length = i % 2 ? radius * 0.42 : radius;
        points[i] = {cx + int(std::lround(std::cos(angle) * length)),
                     cy + int(std::lround(std::sin(angle) * length))};
    }
    SetDCBrushColor(dc, color);
    auto oldBrush = SelectObject(dc, GetStockObject(DC_BRUSH));
    auto oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
    Polygon(dc, points, 10);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
}
// A shaded orange sphere with red stars, drawn in device pixels.
void orb(HDC dc, int cx, int cy, int radius, int stars) {
    ellipse(dc, cx, cy, radius + std::max(2, radius / 10), RGB(120, 52, 14));
    const int steps = std::max(6, radius / 2);
    for (int i = 0; i < steps; ++i) {
        const int r = radius - radius * i / steps;
        const int shift = radius * i / (steps * 3);
        ellipse(dc, cx - shift, cy - shift, r, mix(RGB(214, 92, 18), RGB(255, 196, 92), i * 255 / steps));
    }
    const int size = std::max(4, radius / 3);
    if (stars == 1)
        star(dc, cx, cy, size, RGB(204, 38, 30));
    else
        for (int i = 0; i < stars; ++i)
            star(dc, cx + (i ? size : -size) * 2 / 3, cy + (i ? size / 2 : -size / 3), size * 3 / 4,
                 RGB(204, 38, 30));
    ellipse(dc, cx - radius * 2 / 5, cy - radius * 2 / 5, std::max(2, radius / 7), RGB(255, 241, 214));
}
// Title lettering: the display face with a dark edge traced around it. y is the baseline.
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
struct Fonts {
    HFONT body{}, caption{}, label{}, heading{}, brand{}, strong{}, wordmark{};
    void clear() {
        for (auto f : {body, caption, label, heading, brand, strong, wordmark})
            if (f)
                DeleteObject(f);
        body = caption = label = heading = brand = strong = wordmark = nullptr;
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
    }
    ~Fonts() { clear(); }
};
struct Progress {
    std::mutex mutex;
    std::wstring text;
    uint64_t done = 0, total = 0; // bytes, for the import tool's progress bar
};
enum class Job {
    verify, launch, backup, importSave, restore, afterExit, checkUpdate, downloadUpdate, unlock,
    readGame, readSource, importMod, saveReport
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
struct Window {
    HWND hwnd{};
    Fonts fonts;
    const wchar_t *letteringFace = L"Bangers";
    unsigned dpi = 96;
    int page = 0, hotButton = 0;
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
    // The import tool reads a source disc and writes into a mod; the game
    // disc supplies the archives and entries an import can target.
    std::shared_ptr<mods::SourceDisc> source, game;
    // Budokai 2 discs list their characters, each with its files grouped.
    std::optional<mods::DiscRoster> sourceRoster, gameRoster;
    // What the import tool brings into Budokai 2. Only characters are mapped
    // so far, and only from Budokai 2 discs; imports only ever add.
    enum class Task { characters, stages, movesets, capsules } importTask = Task::characters;
    mods::DiscIdentity sourceIdentity, gameIdentity;
    bool characterView = false; // characters from a disc whose tables are mapped
    std::vector<size_t> sourceRows;
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
                id == DiscordActivity || id == TargetMod || id == SpotMode)
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
    // Small spaced capitals used for section and field labels.
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
    void box(HDC dc, RECT rect, COLORREF color, COLORREF outline = border, int radius = 10) {
        SetDCBrushColor(dc, color);
        SetDCPenColor(dc, outline);
        auto oldBrush = SelectObject(dc, GetStockObject(DC_BRUSH));
        auto oldPen = SelectObject(dc, GetStockObject(DC_PEN));
        RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, p(radius), p(radius));
        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
    }
    void panelBox(HDC dc, int left, int top, int right, int bottom) {
        box(dc, {p(left), p(top), p(right), p(bottom)}, panel);
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
            // The height given to a native combo includes its drop list, while
            // GetWindowRect later reports only the collapsed field.
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
                                                          {SourceGame, L"Game disc"},
                                                          {SourceIso, L"Choose ISO…"},
                                                          {ImportSelected, L"Import"},
                                                          {TaskCharacters, L"Characters"},
                                                          {TaskStages, L"Stages"},
                                                          {TaskMovesets, L"Movesets"},
                                                          {TaskCapsules, L"Capsules"},
                                                          {SaveReport, L"Save disc report…"},
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
                                                          {Restore, L"Restore backup"},
                                                          {OpenSaves, L"Open saves"},
                                                          {OpenBackups, L"Open backup folder"}})
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
        combo(Aspect, {L"4:3 (original)", L"16:9 (widescreen)", L"Stretch to window", L"Native pixels"},
              int(settings.aspect));
        combo(Audio, {L"Off", L"On · 100% volume", L"On · 75% volume", L"On · 50% volume", L"On · 25% volume"},
              audioChoice(settings));
        combo(UpdateChannel, {L"Stable", L"Stable and prerelease"}, int(settings.updateChannel));
        combo(AutomaticUpdates, {L"Off", L"On"}, settings.autoUpdates ? 1 : 0);
        combo(TargetMod, {}, 0);
        combo(SpotMode, {L"Share a spot (R2 switches)", L"Add a new spot (experimental)"}, 0);
        list(BackupList, L"Save backups", 40, false);
        list(ModList, L"Mods", 40, false);
        list(SourceFiles, L"Source files", 30, false);
        list(TargetList, L"Import target", 30, false);
        edit(ModName, L"New mod name");
        edit(EntryFilter, L"Search characters");
        for (auto [id, c] : controls)
            SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(fonts.body), TRUE);
        SetTimer(hwnd, 1, 75, nullptr);
        layout();
    }
    void refreshEnabled() {
        const bool active = disc.has_value() && !busy() && !running();
        for (int id : {ChooseIso, VerifyIso})
            EnableWindow(controls[id], !busy() && !running() &&
                         (id != VerifyIso || !candidateIso.empty()));
        for (int id : {Play, ImportFolder, ImportFile, Backup, OpenSaves, OpenBackups, UnlockAll})
            EnableWindow(controls[id], active);
        EnableWindow(controls[Restore], active && !backups.empty());
        EnableWindow(controls[BackupList], !busy() && !child);
        // Mods are read when the game starts, so they change only while it is closed.
        const bool modding = !busy() && !running();
        const auto chosenMod = SendMessageW(controls[ModList], LB_GETCURSEL, 0, 0);
        for (int id : {ModToggle, ModUp, ModDown})
            EnableWindow(controls[id], modding && chosenMod >= 0 && size_t(chosenMod) < modList.size() &&
                                           (id != ModUp || chosenMod > 0) &&
                                           (id != ModDown || size_t(chosenMod) + 1 < modList.size()));
        for (int id : {ModCreate, OpenImporter, SourceGame, SourceIso})
            EnableWindow(controls[id], modding && (id != SourceGame || !settings.iso.empty()));
        for (int id : {SourceFiles, EntryFilter, TargetList, TargetMod, SpotMode, TaskCharacters, TaskStages, TaskMovesets,
                       TaskCapsules})
            EnableWindow(controls[id], !busy());
        EnableWindow(controls[SaveReport], !busy() && source);
        const bool picked = characterView && sourceCharacter() &&
                            SendMessageW(controls[TargetList], LB_GETCURSEL, 0, 0) >= 0 && gameRoster;
        EnableWindow(controls[ImportSelected], modding && picked && game);
        // Preferences stay editable while a disc check or game session runs;
        // the next game process reads them.
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
            const int gap = 12, action = (content - 40 - 4 * gap) / 5;
            int x = contentLeft + 20;
            for (int id : {ImportFile, ImportFolder, Backup, OpenSaves, UnlockAll}) {
                position(id, x, 246, action, 36);
                x += action + gap;
            }
            position(BackupList, contentLeft + 20, 382, content - 40,
                     std::max(80, panelBottom - 56 - 382 - 8));
            if (backups.empty())
                ShowWindow(controls[BackupList], SW_HIDE);
            position(Restore, contentLeft + 20, panelBottom - 52, 160, 36);
            position(OpenBackups, contentLeft + 192, panelBottom - 52, 172, 36);
        } else if (page == modsPage) {
            const int panelBottom = h - 64;
            position(OpenImporter, right - 20 - 140, 142, 140, 36);
            position(ModFolder, right - 20 - 140 - 12 - 160, 142, 160, 36);
            position(ModList, contentLeft + 20, 252, content - 40, std::max(80, panelBottom - 64 - 252));
            if (modList.empty())
                ShowWindow(controls[ModList], SW_HIDE);
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
            position(SourceGame, right - 20 - 130 - 10 - 116, 214, 116, 34);
            position(SaveReport, right - 20 - 130 - 10 - 116 - 10 - 160, 214, 160, 34);
            // Lists appear only for characters from a disc whose tables are mapped.
            if (characterView) {
                const int listTop = 304, listHeight = listsBottom - 14 - listTop, half = (content - 52) / 2;
                position(EntryFilter, contentLeft + 20 + half - 190, 276, 180, 20);
                position(SourceFiles, contentLeft + 20, listTop, half, listHeight);
                position(TargetList, contentLeft + 32 + half, listTop, content - 52 - half, listHeight);
                position(TargetMod, contentLeft + 20, targetTop + 52, 220, 34);
                position(SpotMode, contentLeft + 252, targetTop + 52, 260, 34);
                position(ImportSelected, right - 20 - 160, targetTop + 52, 160, 36);
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
        fill(dc, {0, 0, p(sidebarWidth), p(h)}, sidebar);
        fill(dc, {p(sidebarWidth) - 1, 0, p(sidebarWidth), p(h)}, border);
        orb(dc, p(38), p(48), p(15), 2);
        text(dc, L"OpenKai2", 64, 28, 150, 24, fonts.brand);
        text(dc, L"Budokai 2 for PC", 64, 51, 150, 18, fonts.caption, muted);

        const int top = h - 162;
        line(dc, p(24), p(top - 10), p(sidebarWidth - 48), border);
        caps(dc, L"DISCORD ACTIVITY", 24, top + 6, sidebarWidth - 48);
        const auto state = presence.status();
        text(dc, !settings.discordPresence ? L"Activity sharing is off"
                  : state == PresenceStatus::connected ? L"Connected to Discord"
                  : L"Waiting for desktop app", 24, top + 74, sidebarWidth - 48, 18,
             fonts.caption, state == PresenceStatus::connected ? green : muted,
             DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        text(dc, L"v" + wide(application_version), 24, h - 38, sidebarWidth - 48, 18, fonts.caption, faint);
    }
    void paintHeader(HDC dc, int w, int content) {
        const wchar_t *titles[] = {L"Play", L"Saves", L"Mods", L"Settings", L"Import tool"};
        const wchar_t *subtitles[] = {
            L"Your game, ready when you are.",
            L"Import, back up and restore your memory card.",
            L"Characters, movesets and stages layered over your disc. Your ISO is never changed.",
            L"Saved automatically. Game settings apply at the next launch.",
            L"Add characters from other Dragon Ball Z games to Budokai 2, without removing anyone."};
        text(dc, titles[page], contentLeft, 28, content - 180, 34, fonts.heading);
        text(dc, subtitles[page], contentLeft, 66, content - 180, 20, fonts.caption, muted,
             DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        const auto label = stateLabel();
        const int pill = measure(dc, label, fonts.strong) + 40, left = w - margin - pill;
        box(dc, {p(left), p(32), p(w - margin), p(62)}, panel, border, 30);
        ellipse(dc, p(left + 16), p(47), p(4), stateColor());
        text(dc, label, left + 26, 32, pill - 30, 30, fonts.strong, stateColor(),
             DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        line(dc, p(contentLeft), p(104), p(content), border);
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
        auto region = CreateRoundRectRgn(hero.left, hero.top, hero.right + 1, hero.bottom + 1, p(14), p(14));
        SelectClipRgn(dc, region);
        const int span = std::max(1L, hero.right - hero.left);
        for (int x = hero.left; x < hero.right; x += 2)
            fill(dc, {x, hero.top, x + 2, hero.bottom}, mix(heroFrom, heroTo, (x - hero.left) * 255 / span));
        const int cx = right - 100, cy = heroTop + 96;
        // A restrained outline gives the title room without additional artwork.
        auto pen = CreatePen(PS_SOLID, p(1), mix(heroTo, orange, 46));
        auto oldPen = SelectObject(dc, pen);
        auto oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        for (int radius : {74, 90, 106})
            Ellipse(dc, p(cx - radius), p(cy - radius), p(cx + radius), p(cy + radius));
        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
        DeleteObject(pen);
        SelectClipRgn(dc, nullptr);
        DeleteObject(region);

        caps(dc, L"DRAGON BALL Z", contentLeft + 30, heroTop + 22, 300, gold);
        lettering(dc, L"Budokai 2", p(contentLeft + 28), p(heroTop + 82), fonts.wordmark, RGB(255, 240, 222),
                  RGB(28, 12, 4), p(3));
        text(dc, L"PlayStation 2  ·  NTSC-U  ·  SLUS-20779", contentLeft + 30, heroTop + 92,
             content - 260, 18, fonts.caption, RGB(214, 196, 180));

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
            detail = L"Your disc was verified.";
        } else {
            headline = L"No disc selected";
            detail = L"Choose your USA ISO below to get started.";
        }
        text(dc, headline, contentLeft + 214, heroBottom - 69, content - 400, 22, fonts.strong);
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
                (settings.antialiasing ? L" · FXAA" : L""),
            std::wstring(aspects[std::min(3u, settings.aspect)]) + L" · " +
                (settings.fps ? std::to_wstring(settings.fps) + L" FPS cap" : L"uncapped") +
                (settings.vsync ? L" · VSync" : L""),
            disc ? std::to_wstring(backups.size()) + (backups.size() == 1 ? L" backup stored" : L" backups stored")
                 : L"Available once your disc is verified"};
        for (int i = 0; i < 3; ++i) {
            const int x = contentLeft + i * (tile + 12);
            panelBox(dc, x, tilesTop, x + tile, tilesBottom);
            fill(dc, {p(x + 16), p(tilesTop + 18), p(x + 19), p(tilesTop + 30)}, i == 2 ? green : orange);
            caps(dc, captions[i], x + 26, tilesTop + 16, tile - 40);
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
        text(dc, L"Import saves or unlock every character and stage. A backup is made before changes.",
             contentLeft + 20, 290, content - 40, 18, fonts.caption, muted,
             DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

        panelBox(dc, contentLeft, 334, right, panelBottom);
        text(dc, L"Backup history", contentLeft + 20, 348, content - 200, 22, fonts.strong);
        if (disc)
            text(dc, std::to_wstring(backups.size()) + (backups.size() == 1 ? L" backup" : L" backups"),
                 right - 160, 350, 140, 20, fonts.caption, muted, DT_RIGHT | DT_SINGLELINE | DT_NOPREFIX);
        if(backups.empty()) {
            const int bottom=std::max(440,panelBottom-64),middle=(382+bottom)/2;
            box(dc,{p(contentLeft+20),p(382),p(right-20),p(bottom)},field,border,8);
            text(dc,disc ? L"No backups yet" : L"Choose your game disc",contentLeft+36,middle-28,
                 content-72,24,fonts.strong,ink,DT_CENTER|DT_SINGLELINE|DT_NOPREFIX);
            text(dc,disc ? L"Back up your save before importing or unlocking content."
                         : L"Verify your ISO on the Play page to manage saves.",contentLeft+36,middle+4,
                 content-72,38,fonts.caption,muted,DT_CENTER|DT_WORDBREAK|DT_NOPREFIX);
        }
    }
    // Edit controls have no frame of their own; this draws the field around one.
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
        text(dc, L"Load order", contentLeft + 20, 220, 200, 22, fonts.strong);
        text(dc, L"Mods load top to bottom and later ones win. Changes apply the next time the game starts.",
             contentLeft + 130, 223, content - 150, 18, fonts.caption, muted,
             DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
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
        text(dc, source ? source->image().filename().wstring() + L"  ·  " + wide(mods::game_name(sourceIdentity.game))
                        : game || !disc ? L"Choose a Budokai, Budokai 2, Budokai 3 or Infinite World disc"
                                        : L"Reading your game disc…",
             contentLeft + 20, 228, content - 480, 22, fonts.strong, ink, DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        panelBox(dc, contentLeft, 264, right, listsBottom);
        const auto placeholder = [&](const std::wstring &message) {
            box(dc, {p(contentLeft + 20), p(304), p(right - 20), p(listsBottom - 14)}, field, border, 8);
            text(dc, message, contentLeft + 40, (304 + listsBottom) / 2 - 30, content - 80, 60, fonts.caption, muted,
                 DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
        };
        const wchar_t *contents[] = {L"Characters", L"Stages", L"Movesets", L"Capsules"};
        const std::wstring content_name = contents[int(importTask)];
        const auto mapping = L" Save a disc report and send it to have them mapped; it lists the disc's tables and file "
                             L"formats, not its data. Nothing in Budokai 2 is changed.";
        if (characterView) {
            const int half = (content - 52) / 2;
            caps(dc, L"1   CHOOSE A CHARACTER", contentLeft + 20, 278, half - 200, orange);
            caps(dc, selected(SpotMode) == 1 ? L"2   CHOOSE WHOSE PORTRAIT THE NEW SPOT USES"
                                              : L"2   CHOOSE A SPOT TO SHARE",
                 contentLeft + 32 + half, 278, content - 52 - half, orange);
            editFrame(dc, EntryFilter);
        } else {
            auto heading = content_name;
            std::transform(heading.begin(), heading.end(), heading.begin(), ::towupper);
            caps(dc, heading, contentLeft + 20, 278, 300, orange);
            if (!disc)
                placeholder(L"Verify your Budokai 2 ISO on the Play page first. The import tool needs it.");
            else if (!source)
                placeholder(L"Reading the disc…");
            else if (importTask == Task::characters)
                placeholder(L"Characters from " + wide(mods::game_name(sourceIdentity.game)) +
                            L" can't be imported yet: its character tables aren't mapped." + mapping);
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
            // A bar fills with the bytes copied; reading a disc has no total, so
            // a short segment sweeps across instead.
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
            caps(dc, L"SAVE TO", contentLeft + 20, targetTop + 34, 220);
            caps(dc, L"ON THE SELECT SCREEN", contentLeft + 252, targetTop + 34, 260);
            // One plain sentence says what pressing Import will do.
            text(dc, plan(), contentLeft + 20, targetTop + 94, content - 40, 36, fonts.caption, ink,
                 DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS | DT_NOPREFIX);
        } else
            text(dc, L"Imports add to Budokai 2; every character already in it stays on the select screen.",
                 contentLeft + 20, targetTop + 48, content - 40, 20, fonts.caption, muted,
                 DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
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
        if (selected(SpotMode) == 1)
            return wide(character->name) + L" gets a new character slot and a new spot of its own before Random, showing " +
                   (occupant ? wide(occupant->name) + L"'s" : L"spot " + std::to_wstring(row + 1) + L"'s") +
                   L" portrait. This changes the select screen's code; report anything odd. " + into;
        return wide(character->name) + L" gets a new character slot and shares spot " + std::to_wstring(row + 1) +
               (occupant ? L" with " + wide(occupant->name) : L"") +
               L"; press R2 (Right Shift) on that spot to switch between them. " + into;
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
        auto memory = CreateCompatibleDC(dc);
        auto bitmap = CreateCompatibleBitmap(dc, rect.right, rect.bottom);
        auto old = SelectObject(memory, bitmap);
        const int w = unscale(rect.right), h = unscale(rect.bottom);
        const int content = w - contentLeft - margin;
        fill(memory, rect, background);
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
        line(memory, p(contentLeft), p(footer - 12), p(content), border);
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
        BitBlt(dc, 0, 0, rect.right, rect.bottom, memory, 0, 0, SRCCOPY);
        SelectObject(memory, old);
        DeleteObject(bitmap);
        DeleteDC(memory);
        EndPaint(hwnd, &ps);
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
            fill(dc, r, surface(id, r));
            COLORREF color, outline, label;
            if (navigation) {
                color = chosen ? raised : hot ? RGB(24, 30, 44) : sidebar;
                outline = color;
                label = chosen ? ink : hot ? ink : muted;
            } else if (primary || taskChoice || id == ImportSelected) {
                // The chosen task and the Import action stand out like Play.
                color = !enabled ? raised : pressed ? RGB(230, 118, 24) : hot ? gold : orange;
                outline = color;
                label = enabled ? RGB(28, 16, 6) : faint;
            } else {
                color = !enabled ? panel : pressed ? raised : hot ? hover : raised;
                outline = enabled ? (hot ? RGB(66, 78, 104) : border) : RGB(34, 41, 57);
                label = enabled ? ink : faint;
            }
            box(dc, r, color, outline, primary ? 12 : 10);
            if (chosen)
                box(dc, {r.left + p(4), r.top + p(14), r.left + p(7), r.bottom - p(14)}, orange, orange, 3);
            wchar_t text[100]{};
            GetWindowTextW(item.hwndItem, text, 100);
            auto old = SelectObject(dc, primary ? fonts.brand : navigation || taskChoice || id == ImportSelected ? fonts.strong
                                                                                                                 : fonts.body);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, label);
            RECT textRect = r;
            if (navigation) {
                navigationIcon(dc, id, r, chosen ? gold : hot ? ink : faint);
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
            if ((item.itemState & ODS_FOCUS) && !(item.itemState & ODS_NOFOCUSRECT)) {
                RECT focus = r;
                InflateRect(&focus, -p(3), -p(3));
                SetDCPenColor(dc, primary ? RGB(28, 16, 6) : navigation ? RGB(66, 78, 104) : orange);
                auto pen = SelectObject(dc, GetStockObject(DC_PEN));
                auto brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
                RoundRect(dc, focus.left, focus.top, focus.right, focus.bottom, p(8), p(8));
                SelectObject(dc, pen);
                SelectObject(dc, brush);
            }
            return;
        }
        const bool chosen = (item.itemState & ODS_SELECTED) != 0;
        const bool list = item.CtlType == ODT_LISTBOX;
        fill(dc, r, list ? field : panel);
        if (chosen) {
            fill(dc, r, raised);
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
        SetTextColor(dc, enabled ? ink : muted);
        r.left += p(14);
        r.right -= p(10);
        DrawTextW(dc, value.c_str(), int(value.size()), &r,
                  DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
        SelectObject(dc, old);
    }
    void refreshBackups() {
        backups = disc ? list_backups(backupPath()) : std::vector<std::filesystem::path>{};
        SendMessageW(controls[BackupList], LB_RESETCONTENT, 0, 0);
        for (const auto &p : backups) {
            // Show the local modification time; the filename stays the stable ID.
            const auto time = std::filesystem::last_write_time(p);
            const auto system = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                time - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
            const auto seconds = std::chrono::system_clock::to_time_t(system);
            tm local{};
            localtime_s(&local, &seconds);
            wchar_t stamp[80]{};
            wcsftime(stamp, 80, L"%b %d, %Y  at  %H:%M", &local);
            const auto label = std::wstring(stamp) + L"     " +
                               std::to_wstring((std::filesystem::file_size(p) + 1023) / 1024) + L" KB";
            SendMessageW(controls[BackupList], LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        }
        if (!backups.empty())
            SendMessageW(controls[BackupList], LB_SETCURSEL, 0, 0);
        if (page == 1)
            ShowWindow(controls[BackupList], backups.empty() ? SW_HIDE : SW_SHOWNA);
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
    void refreshMods(int select = -1) {
        const auto keep = SendMessageW(controls[ModList], LB_GETCURSEL, 0, 0);
        modList = mods::scan_mods(mods::default_mods_directory());
        auto list = controls[ModList];
        SendMessageW(list, LB_RESETCONTENT, 0, 0);
        for (const auto &mod : modList) {
            std::wstring label = (mod.enabled ? L"On    ·   " : L"Off   ·   ") + wide(mod.name);
            if (!mod.author.empty())
                label += L"   ·   by " + wide(mod.author);
            const auto changes = mod.replaced_files + mod.archive_entries;
            label += L"   ·   " + std::to_wstring(changes) + (changes == 1 ? L" change" : L" changes");
            SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        }
        const int chosen = select >= 0 ? select : int(keep);
        if (chosen >= 0 && size_t(chosen) < modList.size())
            SendMessageW(list, LB_SETCURSEL, WPARAM(chosen), 0);
        if (page == modsPage)
            ShowWindow(list, modList.empty() ? SW_HIDE : SW_SHOWNA);
        auto target = controls[TargetMod];
        const auto previous = SendMessageW(target, CB_GETCURSEL, 0, 0);
        SendMessageW(target, CB_RESETCONTENT, 0, 0);
        SendMessageW(target, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"New mod"));
        for (const auto &mod : modList)
            SendMessageW(target, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(wide(mod.name).c_str()));
        SendMessageW(target, CB_SETCURSEL, previous > 0 && size_t(previous) <= modList.size() ? previous : 0, 0);
        refreshEnabled();
    }
    // Import tool work runs on a worker thread so the window keeps
    // responding; it reports through `progress` and finishes in timer().
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
    // Reads a disc, its game and, for Budokai 2, its characters.
    static Result readDisc(Job kind, const std::filesystem::path &image) {
        Result result{kind};
        result.loaded = std::make_shared<mods::SourceDisc>(image);
        result.identity = mods::identify_disc(*result.loaded);
        result.roster = mods::read_disc_roster(*result.loaded);
        return result;
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
        SetWindowTextW(controls[EntryFilter], L"");
        setTask(importTask);
    }
    // Switches what the import tool shows for the chosen content.
    void setTask(Task next) {
        importTask = next;
        characterView = importTask == Task::characters && sourceRoster.has_value();
        for (int id : {TaskCharacters, TaskStages, TaskMovesets, TaskCapsules})
            InvalidateRect(controls[id], nullptr, FALSE);
        layout();
        fillSourceList();
        fillTargetList();
        refreshEnabled();
    }
    // The left list: characters by name, filtered by the search box.
    void fillSourceList() {
        const auto *kept = sourceCharacter();
        const auto keptId = kept ? int(kept->id) : -1;
        sourceRows.clear();
        auto list = controls[SourceFiles];
        SendMessageW(list, LB_RESETCONTENT, 0, 0);
        if (!characterView)
            return;
        std::wstring filter = textOf(EntryFilter);
        std::transform(filter.begin(), filter.end(), filter.begin(), ::towlower);
        int first = -1;
        for (size_t i = 0; i < sourceRoster->characters.size(); ++i) {
            const auto &character = sourceRoster->characters[i];
            auto label = wide(character.name);
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
    // The right list: the spots on the game's select screen.
    void fillTargetList() {
        auto list = controls[TargetList];
        const auto keep = SendMessageW(list, LB_GETCURSEL, 0, 0);
        SendMessageW(list, LB_RESETCONTENT, 0, 0);
        if (!characterView || !gameRoster)
            return;
        for (uint32_t cell = 0; cell < mods::roster_select_cells; ++cell) {
            const auto *occupant = gameRoster->find(gameRoster->grid[cell]);
            const auto label = L"Spot " + std::to_wstring(cell + 1) + L"   ·   " +
                               (occupant ? wide(occupant->name) : std::wstring(L"empty"));
            SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        }
        SendMessageW(list, LB_SETCURSEL, WPARAM(keep >= 0 && keep < LRESULT(mods::roster_select_cells) ? keep : 0), 0);
    }
    // Every DATA_CMN entry a character's record names, once each.
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
            result.message = L"Your game disc is ready. Choose a character, or another disc with Choose ISO.";
            return result;
        });
    }
    void showGame(std::shared_ptr<mods::SourceDisc> disc, std::optional<mods::DiscRoster> roster,
                  mods::DiscIdentity identity) {
        game = std::move(disc);
        gameRoster = std::move(roster);
        gameIdentity = identity;
        // The game disc is the source until another disc is chosen.
        if (!source)
            showSource(game, gameRoster, std::move(identity));
        else
            setTask(importTask);
    }
    // The mod chosen under Save to, or a new one named after what is imported.
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
    // Adds the character's files as new DATA_CMN entries and a roster.ini
    // section that fills an unused character ID with them. The character
    // shares a spot or gets a new one, so nobody leaves the select screen.
    void importCharacter() {
        const auto *character = sourceCharacter();
        if (!character)
            return;
        if (!game || !gameRoster)
            throw std::runtime_error("Verify your Budokai 2 ISO on the Play page before importing characters");
        const int pick = int(SendMessageW(controls[TargetList], LB_GETCURSEL, 0, 0));
        if (pick < 0)
            throw std::runtime_error("Choose a spot on the select screen");
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
        std::map<uint32_t, size_t> itemFor; // source entry -> item
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
            newSpot ? L"Added " + wide(character->name) + L" with a new spot of its own (character ID " +
                          std::to_wstring(added.slot) + L")."
                    : L"Added " + wide(character->name) + L", sharing spot " + std::to_wstring(pick + 1) +
                          (occupant ? L" with " + wide(occupant->name) : std::wstring()) + L" (character ID " +
                          std::to_wstring(added.slot) + L"). Press R2 on that spot to switch.";
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
    // Writes a report of the source disc to mods/reports, for mapping games
    // whose tables are not known yet, and shows it.
    void saveReport() {
        if (!source)
            return;
        const auto folder = mods::default_mods_directory() / "reports";
        const auto name = sourceIdentity.serial.empty() ? utf8(source->image().stem().wstring()) : sourceIdentity.serial;
        startModJob(Job::saveReport, L"Writing the disc report…",
                    [disc = source, folder, name](const std::shared_ptr<Progress> &) {
                        std::filesystem::create_directories(folder);
                        const auto path = folder / (wide(name) + L".txt");
                        std::ofstream out(path, std::ios::binary | std::ios::trunc);
                        out << mods::disc_report(*disc);
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
        auto updates = background ? updateProgress : progress;
        {
            std::lock_guard lock(updates->mutex);
            updates->text = kind == Job::verify || kind == Job::launch ? L"Verifying your disc…"
                            : background                             ? L"Checking for updates…"
                                                                     : L"Working on your saves…";
        }
        if (background) updateError = false; else error = false;
        std::cout << "[launcher] operation=" << int(kind) << " started" << std::endl;
        auto work = std::async(std::launch::async, [kind, source, prefs, save, archive, updates, availableRelease]() {
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
                result.message = L"Disc verified. You're ready to play.";
            } else if(kind==Job::unlock) {
                const bool changed=unlock_card_content(save,archive);
                result.message=changed ? L"All characters and stages unlocked. Your previous card is in Backup history."
                                       : L"All characters and stages are already unlocked.";
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
                    if (result.kind == Job::readSource && result.loaded)
                        showSource(std::move(result.loaded), std::move(result.roster), std::move(result.identity));
                    if (result.kind == Job::saveReport)
                        ShellExecuteW(hwnd, L"open", result.image.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                    if (result.kind == Job::importMod) refreshMods();
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
                }
                std::cerr << (background ? "[launcher:update-error] " : "[launcher:error] ")
                          << e.what() << std::endl;
                if (!background && modJob(activeJob)) refreshMods();
            } catch (...) {
                (background ? updateStatus : status) = L"The operation failed unexpectedly. Details are in launcher.log.";
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
    // A title marks a mod-tool pick, which accepts any file.
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
            refreshEnabled();
            return;
        }
        if ((id == SourceFiles || id == TargetList) && notification == LBN_SELCHANGE) {
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
            refreshEnabled();
            return;
        }
        if (id == ModList && notification == LBN_SELCHANGE) {
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
        else if (id == Restore && disc) {
            auto i = SendMessageW(controls[BackupList], LB_GETCURSEL, 0, 0);
            if (i >= 0 && size_t(i) < backups.size())
                start(Job::restore, backups[size_t(i)]);
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
            status = L"Created " + folder.filename().wstring() + L". Add files with the import tool or by hand.";
            error = false;
        } else if (id == OpenImporter)
            openImporter();
        else if (id == SourceGame && game)
            showSource(game, gameRoster, gameIdentity);
        else if (id == SourceGame && !settings.iso.empty())
            loadSource(settings.iso);
        else if (id == SourceIso) {
            const auto image = choose(false, true, L"Choose a PS2 ISO to import from");
            if (!image.empty())
                loadSource(image);
        } else if (id == ImportSelected)
            importCharacter();
        else if ((id == OpenSaves || id == OpenBackups) && disc) {
            auto p = id == OpenSaves ? card() : backupPath();
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
// Draws the hint of an empty, unfocused field; cue banners need a newer
// common controls library than the launcher loads.
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
        auto dc = BeginPaint(hwnd, &ps);
        RECT rect{};
        GetClientRect(hwnd, &rect);
        const bool enabled = IsWindowEnabled(hwnd) != FALSE, focused = GetFocus() == hwnd;
        fill(dc, rect, panel);
        owner->box(dc, rect, enabled ? field : panel, focused ? orange : enabled ? border : RGB(34, 41, 57), 8);
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
        case WM_GETMINMAXINFO: {
            auto *limits = reinterpret_cast<MINMAXINFO *>(lp);
            limits->ptMinTrackSize = {self->p(1000), self->p(680)};
            return 0;
        }
        case WM_DPICHANGED: {
            self->dpi = HIWORD(wp);
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
        // Nothing may escape into Windows' message dispatch, which would end the process.
        self->status = L"The action failed unexpectedly. Details are in launcher.log.";
        self->error = true;
        std::cerr << "[launcher:error] message=" << message << " failed with an unknown error" << std::endl;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wp, lp);
}
} // namespace
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
    const int width = std::min<int>(state.p(1120), int(work.right - work.left) - state.p(24));
    const int height = std::min<int>(state.p(740), int(work.bottom - work.top) - state.p(24));
    auto hwnd =
        CreateWindowExW(0, type.lpszClassName, L"OpenKai2", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                        work.left + (work.right - work.left - width) / 2,
                        work.top + (work.bottom - work.top - height) / 2, width, height, nullptr,
                        nullptr, type.hInstance, &state);
    if (!hwnd)
        throw std::runtime_error("Cannot open the launcher window");
    if (auto module = LoadLibraryW(L"dwmapi.dll")) {
        if (auto fn = reinterpret_cast<HRESULT(WINAPI *)(HWND, DWORD, LPCVOID, DWORD)>(
                GetProcAddress(module, "DwmSetWindowAttribute"))) {
            BOOL dark = TRUE;
            fn(hwnd, 20, &dark, sizeof(dark));
            // Windows 11 caption colour; older versions ignore the attribute.
            COLORREF caption = sidebar;
            fn(hwnd, 35, &caption, sizeof(caption));
        }
        FreeLibrary(module);
    }
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    if (!state.settings.iso.empty())
        state.start(Job::verify, state.settings.iso);
    ACCEL shortcuts[] = {{FCONTROL | FVIRTKEY, '1', PlayTab},
                         {FCONTROL | FVIRTKEY, '2', SavesTab},
                         {FCONTROL | FVIRTKEY, '3', ModsTab},
                         {FCONTROL | FVIRTKEY, '4', SettingsTab}};
    const auto accelerators = CreateAcceleratorTableW(shortcuts, pageCount);
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
} // namespace openkai2::launcher
