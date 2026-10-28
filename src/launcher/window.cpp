// SPDX-License-Identifier: GPL-3.0-only
#include "launcher/window.h"
#include "disc_image.h"
#include "launcher/saves.h"
#include "launcher/session.h"
#include "launcher/settings.h"
#include "launcher/update.h"
#include "launcher/version.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <commdlg.h>
#include <functional>
#include <future>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <sstream>
#include <stdexcept>
#include <vector>
#include <windowsx.h>

namespace openkai2::launcher {
namespace {
constexpr COLORREF background = RGB(14, 20, 31), panel = RGB(23, 31, 45), raised = RGB(34, 46, 65);
constexpr COLORREF border = RGB(52, 67, 88), ink = RGB(242, 245, 250), muted = RGB(161, 174, 195);
constexpr COLORREF orange = RGB(244, 151, 56), gold = RGB(248, 196, 94), red = RGB(255, 139, 125);
enum Id {
    PlayTab = 101,
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
    UpdateChannel = 160,
    AutomaticUpdates,
    CheckUpdates,
    InstallUpdate,
    ReleasePage,
    ResetGraphics = 170, OpenGraphics, OpenSaveManager, ClearImage
};
std::wstring wide(const std::string &s) {
    if (s.empty())
        return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring r(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), r.data(), n);
    return r;
}
void fill(HDC dc, RECT r, COLORREF color) {
    auto b = CreateSolidBrush(color);
    FillRect(dc, &r, b);
    DeleteObject(b);
}
void line(HDC dc, int x, int y, int w, COLORREF color) { fill(dc, {x, y, x + w, y + 1}, color); }
struct Fonts {
    HFONT body{}, caption{}, heading{}, brand{};
    void clear() {
        for (auto f : {body, caption, heading, brand})
            if (f)
                DeleteObject(f);
        body = caption = heading = brand = nullptr;
    }
    void make(unsigned dpi) {
        clear();
        auto font = [&](int size, int weight) {
            return CreateFontW(-MulDiv(size, int(dpi), 96), 0, 0, 0, weight, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        };
        body = font(15, FW_NORMAL);
        caption = font(12, FW_NORMAL);
        heading = font(32, FW_SEMIBOLD);
        brand = font(16, FW_SEMIBOLD);
    }
    ~Fonts() { clear(); }
};
struct Progress {
    std::mutex mutex;
    std::wstring text;
};
enum class Job { verify, launch, backup, importSave, restore, afterExit, checkUpdate, downloadUpdate };
struct Result {
    Job kind;
    std::optional<DiscMount> disc;
    std::filesystem::path image;
    std::wstring message;
    std::optional<UpdateRelease> release;
    std::optional<StagedUpdate> staged;
};
LRESULT CALLBACK combo_procedure(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK button_procedure(HWND, UINT, WPARAM, LPARAM);
struct Window {
    HWND hwnd{};
    Fonts fonts;
    unsigned dpi = 96;
    int page = 0, hotButton = 0;
    Settings settings;
    std::filesystem::path candidateIso;
    Job activeJob = Job::verify, activeUpdateJob = Job::checkUpdate;
    std::optional<DiscMount> disc;
    std::optional<UpdateRelease> release;
    std::optional<StagedUpdate> staged;
    bool updatePending = true;
    std::wstring updateStatus = L"Automatic updates check GitHub between game sessions.";
    std::map<int, HWND> controls;
    std::vector<unsigned> rates{30, 60, 90, 120, 144, 165, 240, 0};
    std::vector<std::pair<unsigned, unsigned>> sizes{
        {1280, 720}, {1600, 900}, {1920, 1080}, {2560, 1440}, {3840, 2160}};
    std::vector<std::filesystem::path> backups;
    HANDLE child{};
    std::future<Result> task, updateTask;
    std::shared_ptr<Progress> progress = std::make_shared<Progress>();
    std::shared_ptr<Progress> updateProgress = std::make_shared<Progress>();
    bool closing = false, error = false, updateError = false, externalGame = false, picking = false;
    bool installRequested = false;
    std::chrono::steady_clock::time_point lastInput = std::chrono::steady_clock::now();
    std::wstring status = L"Choose a game ISO to get started.";
    int p(int n) const { return MulDiv(n, int(dpi), 96); }
    bool busy() const { return task.valid(); }
    bool updating() const { return updateTask.valid(); }
    bool dropdownOpen() const {
        for (auto [id, control] : controls)
            if ((id >= Renderer && id <= Aspect) || id == UpdateChannel || id == AutomaticUpdates)
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
    void text(HDC dc, std::wstring value, int x, int y, int w, int h, HFONT font,
              COLORREF color = ink, UINT flags = DT_LEFT | DT_TOP | DT_NOPREFIX) {
        auto old = SelectObject(dc, font);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, color);
        RECT r{p(x), p(y), p(x + w), p(y + h)};
        DrawTextW(dc, value.c_str(), int(value.size()), &r, flags);
        SelectObject(dc, old);
    }
    void box(HDC dc, RECT rect, COLORREF color, COLORREF outline = border) {
        fill(dc, rect, background);
        auto brush = CreateSolidBrush(color);
        auto pen = CreatePen(PS_SOLID, p(1), outline);
        auto oldBrush = SelectObject(dc, brush), oldPen = SelectObject(dc, pen);
        RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, p(6), p(6));
        SelectObject(dc, oldBrush); SelectObject(dc, oldPen);
        DeleteObject(brush); DeleteObject(pen);
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
        SendMessageW(w, CB_SETITEMHEIGHT, WPARAM(-1), p(32));
        SendMessageW(w, CB_SETITEMHEIGHT, 0, p(32));
    }
    int selected(int id) const { return int(SendMessageW(controls.at(id), CB_GETCURSEL, 0, 0)); }
    void place(int id, int x, int y, int w, int h, bool visible = true) {
        auto c = controls.at(id);
        wchar_t type[32]{};
        GetClassNameW(c, type, 32);
        int allocatedHeight = p(h);
        if (lstrcmpiW(type, L"ComboBox") == 0) {
            // A native combo's requested height includes its drop list, even
            // though GetWindowRect reports only the collapsed field afterwards.
            const auto rows = std::min<LRESULT>(8, SendMessageW(c, CB_GETCOUNT, 0, 0));
            // The collapsed selection and themed borders also consume height;
            // reserve them independently of the requested field height.
            allocatedHeight = std::max(allocatedHeight,p(36)) + int(rows) * p(32) + p(8);
            SendMessageW(c, CB_SETITEMHEIGHT, WPARAM(-1), p(30));
            SendMessageW(c, CB_SETITEMHEIGHT, 0, p(32));
        }
        SetWindowPos(c, nullptr, p(x), p(y), p(w), allocatedHeight, SWP_NOZORDER | SWP_NOACTIVATE);
        const bool shown = (GetWindowLongPtrW(c, GWL_STYLE) & WS_VISIBLE) != 0;
        if (visible != shown)
            ShowWindow(c, visible ? SW_SHOWNA : SW_HIDE);
    }
    void initialize() {
        fonts.make(dpi);
        for (auto pair :
             std::vector<std::pair<int, const wchar_t *>>{{PlayTab, L"Play"},
                                                          {SavesTab, L"Saves"},
                                                          {SettingsTab, L"Settings"},
                                                          {UpdatesTab, L"Updates"},
                                                          {CheckUpdates, L"Check now"},
                                                          {InstallUpdate, L"Install update"},
                                                          {ReleasePage, L"View releases"},
                                                          {ResetGraphics, L"Reset graphics"},
                                                          {OpenGraphics,L"Graphics settings"},
                                                          {OpenSaveManager,L"Manage saves"},
                                                          {ClearImage,L"Clear image preset"},
                                                          {ChooseIso, L"Choose ISO…"},
                                                          {VerifyIso, L"Verify again"},
                                                          {Play, L"Launch game"},
                                                          {ImportFolder, L"Import folder"},
                                                          {ImportFile, L"Import save"},
                                                          {Backup, L"Back up now"},
                                                          {Restore, L"Restore selected"},
                                                          {OpenSaves, L"Open save folder"},
                                                          {OpenBackups, L"Open backups"}})
            button(pair.first, pair.second);
        if (std::find(rates.begin(), rates.end(), settings.fps) == rates.end())
            rates.push_back(settings.fps);
        std::vector<std::wstring> fps;
        for (auto r : rates)
            fps.push_back(r ? std::to_wstring(r) + L" FPS" : L"Unlimited");
        const auto size = std::make_pair(settings.width, settings.height);
        if (std::find(sizes.begin(), sizes.end(), size) == sizes.end())
            sizes.push_back(size);
        std::vector<std::wstring> resolutions;
        for (auto [w, h] : sizes)
            resolutions.push_back(std::to_wstring(w) + L" × " + std::to_wstring(h));
        combo(Renderer, {L"Automatic", L"GPU · OpenGL", L"Software"},
              settings.renderer == "gpu"   ? 1
              : settings.renderer == "cpu" ? 2
                                           : 0);
        combo(Fps, fps, int(std::find(rates.begin(), rates.end(), settings.fps) - rates.begin()));
        combo(Resolution, resolutions,
              int(std::find(sizes.begin(), sizes.end(), size) - sizes.begin()));
        combo(WindowMode, {L"Windowed", L"Borderless fullscreen"}, int(settings.windowMode));
        combo(Antialiasing, {L"Off", L"FXAA"}, int(settings.antialiasing));
        combo(Filtering, {L"Game default", L"Nearest", L"Bilinear"}, int(settings.textureFilter));
        combo(Upscaler, {L"Nearest", L"Bilinear", L"Bicubic", L"AMD FSR 1 · Spatial"}, int(settings.upscaler));
        combo(Vsync, {L"Off", L"On"}, settings.vsync ? 1 : 0);
        combo(AutoBackup, {L"Off", L"On"}, settings.autoBackups ? 1 : 0);
        combo(Aspect, {L"4:3 · Original", L"16:9 · Widescreen", L"Stretch to window", L"Native pixels"}, int(settings.aspect));
        combo(UpdateChannel, {L"Stable releases", L"Stable + prereleases"}, int(settings.updateChannel));
        combo(AutomaticUpdates, {L"Off", L"On"}, settings.autoUpdates ? 1 : 0);
        controls[BackupList] = CreateWindowExW(
            0, L"LISTBOX", L"Save backups",
            WS_CHILD | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS,
            0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(intptr_t(BackupList)), nullptr, nullptr);
        SendMessageW(controls[BackupList], LB_SETITEMHEIGHT, 0, p(40));
        for (auto [id, c] : controls)
            SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(fonts.body), TRUE);
        for (int id = Renderer; id <= Aspect; ++id) {
            SendMessageW(controls[id], CB_SETITEMHEIGHT, WPARAM(-1), p(36));
            SendMessageW(controls[id], CB_SETITEMHEIGHT, 0, p(36));
        }
        SetTimer(hwnd, 1, 75, nullptr);
        layout();
    }
    void refreshEnabled() {
        const bool active = disc.has_value() && !busy() && !child && !externalGame;
        for (int id : {ChooseIso, VerifyIso})
            EnableWindow(controls[id], !busy() && !child && !externalGame &&
                         (id != VerifyIso || !candidateIso.empty()));
        for (int id : {Play, ImportFolder, ImportFile, Backup, OpenSaves, OpenBackups})
            EnableWindow(controls[id], active);
        EnableWindow(controls[Restore], active && !backups.empty());
        EnableWindow(controls[BackupList], !busy() && !child);
        // Preferences can be edited while verification or another session runs.
        // They are read by the next game process, not applied to a running one.
        EnableWindow(controls[Resolution], settings.windowMode == 0);
        for (int id : {UpdateChannel, AutomaticUpdates, CheckUpdates, InstallUpdate})
            EnableWindow(controls[id], !updating() &&
                         (id != InstallUpdate || (!busy() && !child && !externalGame &&
                          (release.has_value() || staged.has_value()))));
        InvalidateRect(hwnd, nullptr, FALSE);
    }
    void layout() {
        RECT rect{}; GetClientRect(hwnd, &rect);
        const int w = MulDiv(rect.right, 96, int(dpi)), content = w - 240;
        const int col = (content - 24) / 2, right = 216 + col + 24;
        std::vector<int> order{PlayTab, SavesTab, SettingsTab, UpdatesTab};
        auto position = [&](int id, int x, int y, int width, int height) {
            place(id, x, y, width, height); order.push_back(id);
        };
        place(PlayTab, 12, 132, 160, 56);
        place(SavesTab, 12, 192, 160, 56);
        place(SettingsTab, 12, 252, 160, 56);
        place(UpdatesTab, 12, 312, 160, 56);
        if (page == 0) {
            position(OpenGraphics,w-240,176,192,36);
            position(OpenSaveManager,w-240,222,192,36);
            position(ChooseIso, w - 340, 350, 144, 36);
            position(VerifyIso, w - 184, 350, 136, 36);
            position(Play, w - 200, 526, 176, 48);
        } else if (page == 1) {
            position(AutoBackup, w - 212, 146, 168, 36);
            position(ImportFolder, 216, 218, 148, 36);
            position(ImportFile, 376, 218, 136, 36);
            position(Backup, 524, 218, 136, 36);
            position(OpenSaves, 672, 218, 168, 36);
            position(BackupList, 236, 334, content - 40, 174);
            if (backups.empty()) ShowWindow(controls[BackupList], SW_HIDE);
            position(Restore, 216, 536, 180, 36);
            position(OpenBackups, 408, 536, 152, 36);
        } else if (page == 2) {
            const int field = (content - 64) / 2, second = 236 + field + 24;
            position(WindowMode, 236, 194, field, 32);
            position(Resolution, second, 194, field, 32);
            position(Aspect, 236, 250, field, 32);
            position(Fps, second, 250, field, 32);
            position(Vsync, 236, 306, field, 32);
            position(Renderer, 236, 422, field, 32);
            position(Antialiasing, second, 422, field, 32);
            position(Filtering, 236, 478, field, 32);
            position(Upscaler, second, 478, field, 32);
            position(ResetGraphics, 216, 548, 144, 36);
            position(ClearImage, 376, 548, 164, 36);
        } else {
            const int field=(content-64)/2,second=236+field+24;
            position(UpdateChannel, 236, 194, field, 36);
            position(AutomaticUpdates, second, 194, field, 36);
            position(CheckUpdates, 236, 384, 132, 36);
            position(InstallUpdate, 380, 384, 152, 36);
            position(ReleasePage, 544, 384, 144, 36);
        }
        HWND previous = HWND_TOP;
        for (auto id : order) {
            auto control = controls.at(id);
            SetWindowPos(control, previous, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            previous = control;
        }
        for (auto [id, control] : controls)
            if (std::find(order.begin(), order.end(), id) == order.end() &&
                (GetWindowLongPtrW(control, GWL_STYLE) & WS_VISIBLE))
                ShowWindow(control, SW_HIDE);
        refreshEnabled();
    }
    void paint() {
        PAINTSTRUCT ps{}; auto dc = BeginPaint(hwnd, &ps);
        RECT rect{}; GetClientRect(hwnd, &rect);
        if (rect.right <= 0 || rect.bottom <= 0) { EndPaint(hwnd, &ps); return; }
        auto memory = CreateCompatibleDC(dc);
        auto bitmap = CreateCompatibleBitmap(dc, rect.right, rect.bottom);
        auto old = SelectObject(memory, bitmap);
        const int w = MulDiv(rect.right, 96, int(dpi)), h = MulDiv(rect.bottom, 96, int(dpi));
        const int content = w - 240, col = (content - 24) / 2, right = 216 + col + 24;
        fill(memory, rect, background);
        fill(memory, {0, 0, p(184), rect.bottom}, panel);
        fill(memory, {p(183), 0, p(184), rect.bottom}, border);
        fill(memory, {p(16), p(32), p(19), p(50)}, orange);
        fill(memory, {p(16), p(95), p(168), p(96)}, border);
        text(memory, L"OpenKai2", 28, 30, 144, 30, fonts.brand);
        text(memory, L"YOUR LIBRARY", 24, 108, 144, 18, fonts.caption, muted);
        text(memory, L"Budokai 2", 28, 60, 144, 20, fonts.caption, muted);
        box(memory,{p(12),p(h-188),p(172),p(h-96)},background);
        text(memory,L"SESSION",24,h-176,136,18,fonts.caption,muted);
        const COLORREF sessionColor=error ? red : disc ? RGB(133,211,174) : gold;
        fill(memory,{p(24),p(h-145),p(30),p(h-139)},sessionColor);
        text(memory,child || externalGame ? L"Game running" : busy() ? L"Checking disc…" : disc ? L"Ready to launch" : L"Choose a disc",
             38,h-150,124,22,fonts.caption,sessionColor);
        text(memory,busy() ? L"Checking game files. You can still browse settings." : disc ? (settings.autoBackups ? L"Save backups on" : L"Manual save backups") : L"USA game ISO required",
             24,h-122,136,18,fonts.caption,muted);
        text(memory,L"Ctrl + 1–4  ·  Navigate",24,h-72,144,18,fonts.caption,muted);
        text(memory,L"OpenKai2  " + wide(application_version),24,h-44,144,20,fonts.caption,muted);
        const wchar_t *titles[] = {L"Your game", L"Save manager", L"Game settings", L"Updates"};
        const wchar_t *subtitles[] = {L"Choose your disc, review your setup and launch.",
            L"Import saves, create backups, or restore a previous copy.",
            L"Saved automatically. Changes apply the next time you start the game.",
            L"Check for releases and choose your update channel."};
        text(memory, titles[page], 216, 28, content, 36, fonts.heading);
        text(memory, subtitles[page], 216, 76, content, 24, fonts.caption, muted);
        const bool attention=error || (page==3 && updateError);
        const wchar_t* state=attention ? L"ATTENTION" : child || externalGame ? L"IN GAME" : busy() || (page==3 && updating()) ? L"WORKING" : disc ? L"READY" : L"SELECT ISO";
        box(memory,{p(w-160),p(32),p(w-24),p(62)},raised);
        text(memory,state,w-152,36,120,22,fonts.caption,attention ? red : disc ? gold : muted,DT_CENTER|DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX);
        line(memory, p(216), p(112), p(content), border);
        if (page == 0) {
            const RECT hero{p(216),p(140),p(w-24),p(296)};
            for(int y=hero.top;y<hero.bottom;++y){
                const int t=(y-hero.top)*255/std::max(1L,hero.bottom-hero.top);
                fill(memory,{hero.left,y,hero.right,y+1},RGB(31-t*9/255,48-t*17/255,75-t*29/255));
            }
            fill(memory,{p(216),p(140),p(220),p(296)},orange);
            line(memory,p(240),p(236),p(std::max(80,content-300)),RGB(65,83,106));
            text(memory,L"DRAGON BALL Z",240,160,content-48,22,fonts.caption,gold);
            text(memory,L"Budokai 2",238,187,content-240,42,fonts.heading);
            text(memory,L"USA EDITION   /   SLUS-20779",240,250,content-260,20,fonts.caption,muted);
            text(memory,L"Enter · Start     X · Confirm     C · Back",240,274,content-260,18,fonts.caption,muted);
            box(memory,{p(216),p(316),p(w-24),p(408)},panel);
            text(memory,L"GAME DISC",236,328,content-368,18,fonts.caption,muted);
            text(memory,candidateIso.empty() ? L"Choose your ISO to get started" : candidateIso.filename().wstring(),
                 236,350,content-368,24,fonts.body,ink,DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
            text(memory,candidateIso.empty() ? L"The selected path is remembered automatically." : candidateIso.wstring(),
                 236,386,content-40,16,fonts.caption,muted,DT_SINGLELINE|DT_PATH_ELLIPSIS|DT_NOPREFIX);
            const int card=(content-24)/3;
            const wchar_t* captions[]={L"GRAPHICS PROFILE",L"DISPLAY",L"FRAME PACING"};
            const std::wstring values[]={settings.renderer=="cpu" ? L"Software" : settings.renderer=="gpu" ? L"OpenGL GPU" : L"Automatic",
                settings.windowMode ? L"Borderless" : std::to_wstring(settings.width)+L" × "+std::to_wstring(settings.height),
                settings.fps ? std::to_wstring(settings.fps)+L" FPS limit" : L"Unlimited"};
            const wchar_t* scaling[]={L"Nearest scaling",L"Bilinear scaling",L"Bicubic scaling",L"AMD FSR 1 spatial"};
            const std::wstring details[]={std::wstring(scaling[settings.upscaler])+(settings.antialiasing ? L" / FXAA" : L""),
                settings.aspect==0 ? L"Original 4:3 proportions" : settings.aspect==1 ? L"16:9 widescreen projection" : L"Custom aspect ratio",
                settings.vsync ? L"VSync on · Original game timing" : L"VSync off · Original game timing"};
            for(int i=0;i<3;++i){const int x=216+i*(card+12);
                box(memory,{p(x),p(428),p(x+card),p(504)},panel);
                text(memory,captions[i],x+14,438,card-28,16,fonts.caption,muted);
                text(memory,values[i],x+14,458,card-28,22,fonts.brand);
                text(memory,details[i],x+14,484,card-28,16,fonts.caption,muted,DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
            }
            text(memory,busy() ? L"Preparing your game" : disc ? L"Ready to launch" : L"Add your game disc",216,526,content-208,24,fonts.brand);
            text(memory,busy() ? L"Checking game files. You can still browse settings." : disc ? (settings.autoBackups ? L"Disc checked at launch · Automatic save backups on" : L"Disc checked at launch · Manual save backups")
                             : L"Choose an ISO above. Your selection is remembered.",216,555,content-208,22,fonts.caption,muted);
        } else if (page == 1) {
            box(memory,{p(216),p(132),p(w-24),p(202)},panel);
            box(memory,{p(216),p(288),p(w-24),p(524)},panel);
            text(memory, L"Automatic session backups", 236, 146, content - 200, 24, fonts.brand);
            text(memory, L"Keep a copy before and after each game session.", 236, 176, content - 200, 20,
                 fonts.caption, muted);
            text(memory,L"Recovery history",236,302,content-200,24,fonts.brand);
            text(memory,std::to_wstring(backups.size())+L" backups",w-180,304,136,20,fonts.caption,muted,
                 DT_RIGHT|DT_SINGLELINE|DT_NOPREFIX);
            if (backups.empty())
                text(memory, disc ? L"No backups yet. Back up your memory card before importing a save." : L"Choose and verify your game ISO on the Play page to manage saves.",
                     236, 392, content - 40, 48, fonts.caption, muted, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
            text(memory, L"Imports: PSU, extracted save folders and OpenKai2 ZIP backups.",
                 216, 264, content, 20, fonts.caption, muted);
        } else if (page == 2) {
            const int field=(content-64)/2,second=236+field+24;
            box(memory,{p(216),p(132),p(w-24),p(350)},panel);
            box(memory,{p(216),p(364),p(w-24),p(528)},panel);
            text(memory,L"Display",236,146,field,24,fonts.brand);
            text(memory,L"Window, proportions & pacing",second,148,field,20,fonts.caption,muted);
            line(memory,p(236),p(174),p(content-40),border);
            const wchar_t *labels[]={L"Display mode",L"Window resolution",L"Aspect ratio",L"Presentation limit",L"VSync"};
            for(int i=0;i<5;++i)
                text(memory,labels[i],i%2 ? second : 236,176+(i/2)*56,field,18,fonts.caption,muted);
            text(memory,L"Start with 60 FPS and VSync for consistent pacing.",second,300,field,38,
                 fonts.caption,muted,DT_LEFT|DT_WORDBREAK|DT_NOPREFIX);
            text(memory,L"Rendering",236,378,field,24,fonts.brand);
            text(memory,L"Image quality & filtering",second,380,field,20,fonts.caption,muted);
            line(memory,p(236),p(402),p(content-40),border);
            const wchar_t *renderLabels[]={L"Renderer",L"Anti-aliasing",L"Texture filtering",L"Image scaling"};
            for(int i=0;i<4;++i)
                text(memory,renderLabels[i],i%2 ? second : 236,404+(i/2)*56,field,18,fonts.caption,muted);
            text(memory,L"Clear image uses original texture filtering, no FXAA and bicubic scaling.",556,548,
                 content-340,44,fonts.caption,muted,DT_LEFT|DT_WORDBREAK|DT_NOPREFIX);
        } else {
            const int field=(content-64)/2,second=236+field+24;
            box(memory,{p(216),p(132),p(w-24),p(244)},panel);
            text(memory,L"Update preferences",236,146,content-40,24,fonts.brand);
            text(memory,L"Release channel",236,176,field,18,fonts.caption,muted);
            text(memory,L"Automatic updates",second,176,field,18,fonts.caption,muted);
            box(memory,{p(216),p(258),p(w-24),p(438)},panel);
            text(memory,L"OpenKai2  " + wide(application_version),236,274,content-40,26,fonts.brand);
            std::wstring updateMessage=updateStatus;
            if(updating()) {
                std::lock_guard lock(updateProgress->mutex);
                if(!updateProgress->text.empty())updateMessage=updateProgress->text;
            }
            text(memory,updateMessage,236,316,content-40,48,fonts.body,updateError ? red : ink,
                 DT_LEFT|DT_WORDBREAK|DT_NOPREFIX);
            text(memory,L"Settings and saves",216,464,content,24,fonts.brand);
            text(memory,L"Updates preserve your ISO preferences, saves and backups. Installation waits until"
                        L" game sessions and save operations finish, then restarts the launcher.",
                 216,498,content,56,fonts.body,muted,DT_LEFT|DT_WORDBREAK|DT_NOPREFIX);
            text(memory,L"Release downloads are verified before installation.",216,570,content,20,fonts.caption,muted);
        }
        const int footer = h - 38;
        line(memory, p(216), p(footer - 12), p(content), border);
        std::wstring message = status;
        if (busy()) {
            std::lock_guard lock(progress->mutex);
            if (!progress->text.empty()) message = progress->text;
        }
        text(memory, message, 216, footer, content, 30, fonts.caption, error ? red : muted,
             DT_LEFT | DT_END_ELLIPSIS | DT_SINGLELINE | DT_NOPREFIX);
        BitBlt(dc, 0, 0, rect.right, rect.bottom, memory, 0, 0, SRCCOPY);
        SelectObject(memory, old); DeleteObject(bitmap); DeleteDC(memory);
        EndPaint(hwnd, &ps);
    }
    void drawItem(const DRAWITEMSTRUCT &item) {
        auto dc = item.hDC;
        RECT r = item.rcItem;
        const bool enabled = (item.itemState & ODS_DISABLED) == 0;
        if (item.CtlType == ODT_BUTTON) {
            const int id = int(item.CtlID);
            const bool navigation = id >= PlayTab && id <= UpdatesTab;
            const bool chosen = navigation && id == PlayTab + page;
            const bool primary = id == Play;
            auto color = primary ? (enabled ? orange : raised) : (chosen ? raised : panel);
            if (item.itemState & ODS_SELECTED)
                color = primary ? gold : raised;
            const bool hover = hotButton == id && enabled;
            if (hover && !(item.itemState & ODS_SELECTED)) color = primary ? gold : raised;
            box(dc, r, color, primary && enabled ? color : chosen || hover || !navigation ? border : panel);
            if (chosen)
                fill(dc, {r.left, r.top, r.left + p(3), r.bottom}, orange);
            wchar_t label[100]{};
            GetWindowTextW(item.hwndItem, label, 100);
            auto old = SelectObject(dc, primary ? fonts.brand : fonts.body);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, primary && enabled ? background : enabled ? ink : muted);
            RECT textRect = r;
            textRect.left += p(navigation ? 16 : 12);
            textRect.right -= p(12);
            if(navigation){
                const int left=r.left+p(16),top=r.top+p(13),size=p(14);
                const COLORREF accent=chosen ? gold : muted;
                auto pen=CreatePen(PS_SOLID,p(2),accent);auto previous=SelectObject(dc,pen);
                const auto stroke=[&](int x,int y,int ex,int ey){MoveToEx(dc,x,y,nullptr);LineTo(dc,ex,ey);};
                if(id==PlayTab){stroke(left,top,left,top+size);stroke(left,top,left+size,top+size/2);stroke(left+size,top+size/2,left,top+size);}
                else if(id==SavesTab){stroke(left,top,left+size,top);stroke(left,top,left,top+size);stroke(left+size,top,left+size,top+size);stroke(left,top+size,left+size,top+size);stroke(left+p(4),top+p(5),left+p(10),top+p(5));}
                else if(id==SettingsTab){for(int i=0;i<3;++i){const int y=top+i*p(6);stroke(left,y,left+size,y);fill(dc,{left+p(i==1?8:3),y-p(2),left+p(i==1?11:6),y+p(3)},accent);}}
                else{stroke(left,top+size/2,left+size,top+size/2);stroke(left+p(9),top+p(2),left+size,top+size/2);stroke(left+size,top+size/2,left+p(9),top+size-p(2));}
                SelectObject(dc,previous);DeleteObject(pen);
                textRect.left=r.left+p(44);
                textRect.top+=p(8);textRect.bottom=textRect.top+p(23);
                DrawTextW(dc,label,-1,&textRect,DT_SINGLELINE|DT_LEFT|DT_NOPREFIX);
                SelectObject(dc,fonts.caption);SetTextColor(dc,chosen ? gold : muted);
                const wchar_t* descriptions[]={L"ISO & launch",L"Imports & backups",L"Video & graphics",L"Release channels"};
                textRect.left=r.left+p(44);
                textRect.top+=p(23);textRect.bottom=textRect.top+p(18);
                DrawTextW(dc,descriptions[id-PlayTab],-1,&textRect,DT_SINGLELINE|DT_LEFT|DT_END_ELLIPSIS|DT_NOPREFIX);
            }else DrawTextW(dc,label,-1,&textRect,DT_SINGLELINE|DT_VCENTER|DT_CENTER|DT_NOPREFIX);
            SelectObject(dc, old);
            if (item.itemState & ODS_FOCUS) {
                RECT focus = r;
                InflateRect(&focus, -p(4), -p(4));
                DrawFocusRect(dc, &focus);
            }
        } else {
            fill(dc, r, (item.itemState & ODS_SELECTED) ? raised : panel);
            std::wstring value;
            if (item.itemID != UINT(-1)) {
                const UINT getLength =
                               item.CtlType == ODT_LISTBOX ? LB_GETTEXTLEN : CB_GETLBTEXTLEN,
                           getText = item.CtlType == ODT_LISTBOX ? LB_GETTEXT : CB_GETLBTEXT;
                const auto n = SendMessageW(item.hwndItem, getLength, item.itemID, 0);
                if (n >= 0 && n < 32768) {
                    value.resize(size_t(n) + 1);
                    SendMessageW(item.hwndItem, getText, item.itemID,
                                 reinterpret_cast<LPARAM>(value.data()));
                    value.resize(size_t(n));
                }
            }
            auto old = SelectObject(dc, fonts.body);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, enabled ? ink : muted);
            r.left += p(12);
            r.right -= p(10);
            DrawTextW(dc, value.c_str(), int(value.size()), &r,
                      DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
            SelectObject(dc, old);
        }
    }
    void refreshBackups() {
        backups = disc ? list_backups(backupPath()) : std::vector<std::filesystem::path>{};
        SendMessageW(controls[BackupList], LB_RESETCONTENT, 0, 0);
        for (const auto &p : backups) {
            auto label = p.filename().wstring();
            const auto time = std::filesystem::last_write_time(p);
            // Display local modification time; the filename remains a stable unique ID.
            const auto system = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                time - std::filesystem::file_time_type::clock::now() +
                std::chrono::system_clock::now());
            const auto seconds = std::chrono::system_clock::to_time_t(system);
            tm local{};
            localtime_s(&local, &seconds);
            wchar_t stamp[80]{};
            wcsftime(stamp, 80, L"%b %d, %Y   %H:%M", &local);
            label = std::wstring(stamp) + L"   ·   " +
                    std::to_wstring((std::filesystem::file_size(p) + 1023) / 1024) + L" KB";
            SendMessageW(controls[BackupList], LB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(label.c_str()));
        }
        if (!backups.empty())
            SendMessageW(controls[BackupList], LB_SETCURSEL, 0, 0);
        if (page == 1) ShowWindow(controls[BackupList], backups.empty() ? SW_HIDE : SW_SHOWNA);
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
            updates->text = kind == Job::verify || kind == Job::launch
                                ? L"Verifying your ISO…"
                                : kind == Job::checkUpdate || kind == Job::downloadUpdate
                                      ? L"Checking GitHub for updates…" : L"Working with your saves…";
        }
        if (background) updateError = false; else error = false;
        std::cout << "[launcher] operation=" << int(kind) << " started" << std::endl;
        auto work = std::async(std::launch::async, [kind, source, prefs, save, archive, updates, availableRelease]() {
            Result result{kind, {}, {}, L""};
            if (kind == Job::checkUpdate || kind == Job::downloadUpdate) {
                result.release = kind == Job::checkUpdate ? check_for_updates(prefs.updateChannel != 0)
                                                         : availableRelease;
                if (result.release) {
                    result.message = L"OpenKai2 " + wide(result.release->version) + L" is available.";
                    if (prefs.autoUpdates || kind == Job::downloadUpdate) {
                        result.staged = stage_update(*result.release, [updates](const std::string &line) {
                            std::lock_guard lock(updates->mutex);
                            updates->text = wide(line);
                        });
                        result.message = L"Update verified. Ready to install.";
                    }
                } else result.message = L"You are up to date on this channel.";
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
                    backup_card(result.disc->root / "mc0", default_cache_directory().parent_path() /
                                                               "backups" /
                                                               result.disc->image_sha256);
                result.message = L"ISO verified. Ready to play.";
            } else if (kind == Job::backup || kind == Job::afterExit) {
                auto p = backup_card(save, archive);
                result.message =
                    p.empty() ? L"No save files to back up yet." : L"Save backup created.";
            } else {
                import_save(save, source, archive, kind == Job::restore);
                result.message = kind == Job::restore
                                     ? L"Backup restored. Your previous save was backed up first."
                                     : L"Save imported. Your previous save was backed up first.";
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
        status = L"Game running. Save tools unlock when it closes.";
        ShowWindow(hwnd, SW_MINIMIZE);
    }
    void timer() {
        for (const bool background : {false, true}) {
            auto &work = background ? updateTask : task;
            if (!work.valid() || work.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
                continue;
            try {
                auto result = work.get();
                std::cout << "[launcher] operation=" << int(result.kind) << " completed" << std::endl;
                if (background) {
                    release = std::move(result.release);
                    staged = std::move(result.staged);
                    updateStatus = staged && child ? L"Update downloaded. Installation waits for the game to close."
                                                  : result.message;
                    updateError = false;
                    if (result.kind == Job::downloadUpdate && staged) installRequested = true;
                } else {
                    status = result.message; error = false;
                    if (result.disc) {
                        disc = std::move(result.disc);
                        settings.iso = result.image;
                        write_settings(settings_path(), settings);
                    }
                    refreshBackups();
                    if (!closing && result.kind == Job::launch) launch();
                }
            } catch (const std::exception &e) {
                if (background) {
                    updateStatus = wide(e.what()); updateError = true;
                } else {
                    if (activeJob == Job::verify || activeJob == Job::launch) disc.reset();
                    status = wide(e.what()); error = true;
                }
                std::cerr << (background ? "[launcher:update-error] " : "[launcher:error] ")
                          << e.what() << std::endl;
            }
            refreshEnabled();
        }
        if (child && WaitForSingleObject(child, 0) == WAIT_OBJECT_0) {
            DWORD code = 0; GetExitCodeProcess(child, &code);
            CloseHandle(child); child = nullptr;
            status = code ? L"The game stopped with an error. See the startup log." : L"Session ended.";
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
            if (!busy() && !updating() && !child && !externalGame && staged &&
                (installRequested || (settings.autoUpdates && idle && (page == 0 || page == 3)))) {
                try { apply_update(*staged); }
                catch (const std::exception &e) {
                    updateStatus = wide(e.what()); updateError = true;
                    installRequested = false;
                    settings.autoUpdates = false;
                    SendMessageW(controls[AutomaticUpdates], CB_SETCURSEL, 0, 0);
                    refreshEnabled(); return;
                }
                staged.reset(); DestroyWindow(hwnd); return;
            }
        }
        if (busy() || (updating() && page == 3)) InvalidateRect(hwnd, nullptr, FALSE);
    }
    std::filesystem::path chooseFallback(bool folder, bool iso) {
        if (folder) {
            BROWSEINFOW options{};
            options.hwndOwner = hwnd;
            options.lpszTitle = L"Select an extracted save folder";
            options.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
            auto item = SHBrowseForFolderW(&options);
            if (!item)
                return {};
            wchar_t path[MAX_PATH]{};
            const bool valid = SHGetPathFromIDListW(item, path) != FALSE;
            CoTaskMemFree(item);
            if (!valid)
                throw std::runtime_error("The selection is not a filesystem path");
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
        options.lpstrFilter = iso ? L"Game ISO (*.iso)\0*.iso\0All files\0*.*\0\0"
                                  : L"PSU saves and OpenKai2 backups\0*.psu;*.zip\0All files\0*.*\0\0";
        options.lpstrTitle = iso ? L"Select Budokai 2 USA ISO" : L"Import a save or backup";
        options.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (GetOpenFileNameW(&options))
            return path.data();
        if (CommDlgExtendedError())
            throw std::runtime_error("Windows file selection failed");
        return {};
    }
    std::filesystem::path choose(bool folder, bool iso = false) {
        picking = true;
        struct Reset {
            bool &flag;
            ~Reset() { flag = false; }
        } reset{picking};
        IFileOpenDialog *dialog = nullptr;
        if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&dialog))))
            return chooseFallback(folder, iso);
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
            throw std::runtime_error("Cannot configure file selection");
        if (!folder) {
            const COMDLG_FILTERSPEC images[] = {{L"Game ISO (*.iso)", L"*.iso"},
                                                {L"All files", L"*.*"}};
            const COMDLG_FILTERSPEC saves[] = {{L"PSU saves and OpenKai2 backups", L"*.psu;*.zip"},
                                               {L"All files", L"*.*"}};
            dialog->SetFileTypes(2, iso ? images : saves);
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
        dialog->SetTitle(folder ? L"Select an extracted save folder"
                         : iso  ? L"Select Budokai 2 USA ISO"
                                : L"Import a save or backup");
        const auto shown = dialog->Show(hwnd);
        if (shown == HRESULT_FROM_WIN32(ERROR_CANCELLED))
            return {};
        if (FAILED(shown))
            throw std::runtime_error("Windows file selection failed");
        IShellItem *item = nullptr;
        if (FAILED(dialog->GetResult(&item)))
            throw std::runtime_error("Cannot read the selected path");
        PWSTR name = nullptr;
        const auto result = item->GetDisplayName(SIGDN_FILESYSPATH, &name);
        item->Release();
        if (FAILED(result))
            throw std::runtime_error("The selection is not a filesystem path");
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
        next.aspect = unsigned(selected(Aspect));
        write_settings(settings_path(), next);
        settings = std::move(next);
        status = child ? L"Settings saved. Restart the game to apply them." : L"Settings saved.";
        std::cout << "[launcher:settings] saved fps=" << settings.fps << " size=" << settings.width << 'x'
                  << settings.height << " mode=" << settings.windowMode << " renderer=" << settings.renderer
                  << " aspect=" << settings.aspect << " aa=" << settings.antialiasing
                  << " filtering=" << settings.textureFilter << " scaling=" << settings.upscaler
                  << " vsync=" << settings.vsync << std::endl;
        error = false;
    }
    void command(int id, int notification) {
        lastInput = std::chrono::steady_clock::now();
        if (picking)
            return;
        if(id==OpenGraphics || id==OpenSaveManager)
            id=id==OpenGraphics ? SettingsTab : SavesTab;
        if (id >= PlayTab && id <= UpdatesTab) {
            page = id - PlayTab;
            for (int tab = PlayTab; tab <= UpdatesTab; ++tab)
                InvalidateRect(controls[tab], nullptr, FALSE);
            layout();
            SetFocus(controls[id]);
            return;
        }
        if (id >= Renderer && id <= Aspect && notification == CBN_SELCHANGE) {
            saveOptions();
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
            updateStatus = L"Update preferences saved.";
            updateError = false;
            refreshEnabled();
            return;
        }
        if (id == ClearImage) {
            auto next=settings;next.antialiasing=0;next.textureFilter=0;next.upscaler=2;
            write_settings(settings_path(),next);settings=std::move(next);
            for(auto choice : {std::pair<int,int>{Antialiasing,0},{Filtering,0},{Upscaler,2}})
                SendMessageW(controls[choice.first],CB_SETCURSEL,choice.second,0);
            status=L"Clear image preset saved: bicubic scaling, original texture filtering, FXAA off.";
            error=false;refreshEnabled();return;
        }
        if (id == ResetGraphics) {
            Settings defaults;
            auto next = settings;
            next.fps = defaults.fps; next.width = defaults.width; next.height = defaults.height;
            next.windowMode = defaults.windowMode; next.antialiasing = defaults.antialiasing;
            next.textureFilter = defaults.textureFilter; next.upscaler = defaults.upscaler;
            next.aspect = defaults.aspect; next.vsync = defaults.vsync; next.renderer = defaults.renderer;
            write_settings(settings_path(), next); settings = std::move(next);
            const std::pair<int, int> choices[] = {{Renderer, 0}, {Fps, 1}, {Resolution, 0},
                {WindowMode, 0}, {Antialiasing, 0}, {Filtering, 0}, {Upscaler, 1}, {Vsync, 1}, {Aspect, 0}};
            for (auto [control, value] : choices) SendMessageW(controls[control], CB_SETCURSEL, value, 0);
            status = L"Graphics defaults restored. ISO and save preferences were kept.";
            error = false; refreshEnabled(); return;
        }
        if (id == ReleasePage) {
            ShellExecuteW(hwnd, L"open", L"https://github.com/7v0biscuit/OpenKai2/releases", nullptr,
                          nullptr, SW_SHOWNORMAL);
            return;
        }
        if (id == CheckUpdates) {
            updatePending = false; start(Job::checkUpdate); return;
        }
        if (busy() || child)
            return;
        if (session_active()) {
            status = L"Close the running game before changing its ISO or saves.";
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
        else if (id == Restore && disc) {
            auto i = SendMessageW(controls[BackupList], LB_GETCURSEL, 0, 0);
            if (i >= 0 && size_t(i) < backups.size())
                start(Job::restore, backups[size_t(i)]);
        } else if ((id == OpenSaves || id == OpenBackups) && disc) {
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
            TrackMouseEvent(&tracking); InvalidateRect(hwnd, nullptr, FALSE);
        }
    } else if (owner && message == WM_MOUSELEAVE) {
        owner->hotButton = 0; InvalidateRect(hwnd, nullptr, FALSE);
    }
    return original ? CallWindowProcW(original, hwnd, message, wp, lp)
                    : DefWindowProcW(hwnd, message, wp, lp);
}
LRESULT CALLBACK combo_procedure(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
    auto original = reinterpret_cast<WNDPROC>(GetPropW(hwnd, L"OpenKai2.ComboProcedure"));
    auto *owner = reinterpret_cast<Window *>(GetWindowLongPtrW(GetParent(hwnd), GWLP_USERDATA));
    if (message == WM_PAINT && owner) {
        PAINTSTRUCT ps;
        auto dc = BeginPaint(hwnd, &ps);
        RECT rect{};
        GetClientRect(hwnd, &rect);
        fill(dc, rect, GetFocus() == hwnd ? orange : border);
        RECT inner = rect;
        InflateRect(&inner, -1, -1);
        fill(dc, inner, panel);
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
        SetTextColor(dc, IsWindowEnabled(hwnd) ? ink : muted);
        RECT text = rect;
        text.left += owner->p(12);
        text.right -= owner->p(34);
        DrawTextW(dc, label.c_str(), int(label.size()), &text,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        SelectObject(dc, old);
        const int x = rect.right - owner->p(18), y = rect.bottom / 2;
        auto pen = CreatePen(PS_SOLID, owner->p(1), muted);
        auto previous = SelectObject(dc, pen);
        MoveToEx(dc, x - owner->p(4), y - owner->p(2), nullptr);
        LineTo(dc, x, y + owner->p(2));
        LineTo(dc, x + owner->p(4), y - owner->p(2));
        SelectObject(dc, previous);
        DeleteObject(pen);
        EndPaint(hwnd, &ps);
        return 0;
    }
    if (message == WM_ERASEBKGND)
        return 1;
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
            reinterpret_cast<MEASUREITEMSTRUCT *>(lp)->itemHeight = self->p(36);
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
            self->fonts.make(self->dpi);
            for (auto [id, c] : self->controls)
                SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(self->fonts.body), TRUE);
            auto *r = reinterpret_cast<RECT *>(lp);
            SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            self->layout();
            return 0;
        }
        case WM_CTLCOLORLISTBOX:
            SetDCBrushColor(reinterpret_cast<HDC>(wp), panel);
            SetTextColor(reinterpret_cast<HDC>(wp), ink);
            SetBkColor(reinterpret_cast<HDC>(wp), panel);
            return reinterpret_cast<LRESULT>(GetStockObject(DC_BRUSH));
        case WM_CLOSE:
            if (self->child) {
                self->closing = true;
                ShowWindow(hwnd, SW_HIDE);
                return 0;
            }
            if (self->busy() || self->updating()) {
                self->closing = true;
                self->status = L"Finishing the current operation…";
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
    Window state;
    try {
        state.settings = read_settings(settings_path());
    } catch (const std::exception &e) {
        state.status = wide(e.what()) + L". Default settings are in use.";
        state.error = true;
    }
    if (!initialIso.empty())
        state.settings.iso = initialIso;
    if (!state.settings.autoUpdates)
        state.updateStatus=L"Automatic updates are off. Use Check now to find a release.";
    WNDCLASSW type{};
    type.lpfnWndProc = procedure;
    type.hInstance = GetModuleHandleW(nullptr);
    type.lpszClassName = L"OpenKai2.Launcher";
    type.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&type);
    if (auto user = GetModuleHandleW(L"user32.dll"))
        if (auto fn = reinterpret_cast<UINT(WINAPI *)()>(GetProcAddress(user, "GetDpiForSystem")))
            state.dpi = fn();
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int width = std::min<int>(state.p(1120), int(work.right - work.left) - state.p(24));
    const int height = std::min<int>(state.p(760), int(work.bottom - work.top) - state.p(24));
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
        }
        FreeLibrary(module);
    }
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    if (!state.settings.iso.empty())
        state.start(Job::verify, state.settings.iso);
    ACCEL shortcuts[]={{FCONTROL|FVIRTKEY,'1',PlayTab},{FCONTROL|FVIRTKEY,'2',SavesTab},
        {FCONTROL|FVIRTKEY,'3',SettingsTab},{FCONTROL|FVIRTKEY,'4',UpdatesTab}};
    const auto accelerators=CreateAcceleratorTableW(shortcuts,4);
    struct AcceleratorCleanup {HACCEL value;~AcceleratorCleanup(){if(value)DestroyAcceleratorTable(value);}} shortcutCleanup{accelerators};
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!(accelerators && TranslateAcceleratorW(hwnd,accelerators,&message)) &&
            !IsDialogMessageW(hwnd, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    std::cout << "[launcher] closed" << std::endl;
    return 0;
}
} // namespace openkai2::launcher
