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
constexpr COLORREF background = RGB(15, 22, 32), panel = RGB(22, 32, 45), raised = RGB(30, 43, 59);
constexpr COLORREF border = RGB(44, 60, 78), ink = RGB(245, 242, 233), muted = RGB(153, 170, 189);
constexpr COLORREF orange = RGB(249, 151, 52), gold = RGB(233, 187, 91), red = RGB(255, 139, 125);
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
    ReleasePage
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
    HFONT body{}, small{}, heading{}, brand{};
    void clear() {
        for (auto f : {body, small, heading, brand})
            if (f)
                DeleteObject(f);
        body = small = heading = brand = nullptr;
    }
    void make(unsigned dpi) {
        clear();
        auto font = [&](int size, int weight) {
            return CreateFontW(-MulDiv(size, int(dpi), 96), 0, 0, 0, weight, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        };
        body = font(15, FW_NORMAL);
        small = font(12, FW_NORMAL);
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
struct Window {
    HWND hwnd{};
    Fonts fonts;
    unsigned dpi = 96;
    int page = 0;
    Settings settings;
    std::filesystem::path candidateIso;
    Job activeJob = Job::verify;
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
    std::future<Result> task;
    std::shared_ptr<Progress> progress = std::make_shared<Progress>();
    bool closing = false, error = false, externalGame = false, picking = false;
    std::wstring status = L"Choose your game ISO to get started.";
    int p(int n) const { return MulDiv(n, int(dpi), 96); }
    bool busy() const { return task.valid(); }
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
    void button(int id, const wchar_t *label) {
        controls[id] =
            CreateWindowExW(0, L"BUTTON", label, WS_CHILD | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 0, 0,
                            hwnd, reinterpret_cast<HMENU>(intptr_t(id)), nullptr, nullptr);
    }
    void combo(int id, const std::vector<std::wstring> &items, int selected) {
        auto w = CreateWindowExW(0, L"COMBOBOX", L"",
                                 WS_CHILD | WS_TABSTOP | CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED |
                                     CBS_HASSTRINGS | WS_VSCROLL,
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
        SetWindowPos(c, nullptr, p(x), p(y), p(w), p(h), SWP_NOZORDER | SWP_NOACTIVATE);
        ShowWindow(c, visible ? SW_SHOW : SW_HIDE);
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
                                                          {ChooseIso, L"Choose ISO"},
                                                          {VerifyIso, L"Verify again"},
                                                          {Play, L"PLAY"},
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
        combo(Upscaler, {L"Nearest", L"Bilinear", L"Bicubic"}, int(settings.upscaler));
        combo(Vsync, {L"Off", L"On"}, settings.vsync ? 1 : 0);
        combo(AutoBackup, {L"Off", L"On"}, settings.autoBackups ? 1 : 0);
        combo(Aspect, {L"4:3", L"16:9", L"Stretch", L"Native pixels"}, int(settings.aspect));
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
    void layout() {
        RECT rect;
        GetClientRect(hwnd, &rect);
        const int w = MulDiv(rect.right, 96, int(dpi)), content = w - 264, col = (content - 24) / 2;
        for (auto [id, c] : controls)
            ShowWindow(c, SW_HIDE);
        place(PlayTab, 16, 152, 160, 44);
        place(SavesTab, 16, 206, 160, 44);
        place(SettingsTab, 16, 260, 160, 44);
        place(UpdatesTab, 16, 314, 160, 44);
        if (page == 0) {
            place(ChooseIso, 240, 320, 152, 42);
            place(VerifyIso, 408, 320, 144, 42);
            place(Play, 224, 466, content + 16, 60);
        }
        if (page == 1) {
            place(ImportFolder, 224, 240, 148, 42);
            place(ImportFile, 384, 240, 136, 42);
            place(Backup, 532, 240, 152, 42);
            place(OpenSaves, 696, 240, 168, 42);
            place(BackupList, 224, 344, content + 16, 188, !backups.empty());
            place(Restore, 224, 552, 180, 42);
            place(OpenBackups, 420, 552, 164, 42);
        }
        if (page == 2) {
            const int right = 224 + col + 24;
            const int ids[] = {Renderer,  Fps,      Resolution, WindowMode, Antialiasing,
                               Filtering, Upscaler, Vsync,      AutoBackup, Aspect};
            for (int i = 0; i < 10; ++i)
                place(ids[i], i % 2 ? right : 224, 188 + (i / 2) * 76, col, 40);
        }
        if (page == 3) {
            place(UpdateChannel, 224, 188, col, 40);
            place(AutomaticUpdates, 224 + col + 24, 188, col, 40);
            place(CheckUpdates, 224, 408, 144, 42);
            place(InstallUpdate, 384, 408, 168, 42);
            place(ReleasePage, 568, 408, 148, 42);
        }
        for (int id : {UpdateChannel, AutomaticUpdates, CheckUpdates, InstallUpdate})
            EnableWindow(controls[id], !busy() && !child && !externalGame &&
                         (id != InstallUpdate || release.has_value() || staged.has_value()));
        const bool active = disc.has_value() && !busy() && !child && !externalGame;
        EnableWindow(controls[ChooseIso], !busy() && !child && !externalGame);
        EnableWindow(controls[VerifyIso],
                     !candidateIso.empty() && !busy() && !child && !externalGame);
        EnableWindow(controls[Play], active);
        for (int id : {ImportFolder, ImportFile, Backup, OpenSaves, OpenBackups})
            EnableWindow(controls[id], active);
        EnableWindow(controls[Restore], active && !backups.empty());
        EnableWindow(controls[BackupList], !busy() && !child);
        for (int id = Renderer; id <= Aspect; ++id)
            EnableWindow(controls[id], !busy());
        EnableWindow(controls[Resolution], !busy() && settings.windowMode == 0);
        InvalidateRect(hwnd, nullptr, FALSE);
    }
    void paint() {
        PAINTSTRUCT ps;
        auto dc = BeginPaint(hwnd, &ps);
        RECT rect;
        GetClientRect(hwnd, &rect);
        auto memory = CreateCompatibleDC(dc);
        auto bitmap = CreateCompatibleBitmap(dc, rect.right, rect.bottom);
        auto old = SelectObject(memory, bitmap);
        fill(memory, rect, background);
        fill(memory, {0, 0, p(192), rect.bottom}, panel);
        fill(memory, {p(24), p(48), p(28), p(76)}, orange);
        text(memory, L"OPENKAI2", 40, 50, 144, 28, fonts.brand);
        text(memory, L"DESKTOP", 40, 82, 132, 22, fonts.small, muted);
        text(memory,
             page == 0   ? L"Play"
             : page == 1 ? L"Saves"
                         : page == 2 ? L"Settings" : L"Updates",
             224, 44, 720, 48, fonts.heading);
        text(memory,
             page == 0   ? L"Budokai 2 · USA"
             : page == 1 ? L"Memory card saves and recoverable backups"
                         : page == 2 ? L"Display and graphics preferences" : L"Updates from the project GitHub repository",
             224, 100, 740, 26, fonts.body, muted);
        const int w = MulDiv(rect.right, 96, int(dpi)), content = w - 264;
        if (page == 0) {
            fill(memory, {p(224), p(152), p(w - 24), p(400)}, panel);
            fill(memory, {p(224), p(152), p(228), p(400)}, orange);
            text(memory, L"DRAGON BALL Z", 248, 178, 520, 24, fonts.small, gold);
            text(memory, L"Budokai 2", 248, 212, 600, 48, fonts.heading);
            text(memory,
                 candidateIso.empty() ? L"No ISO selected" : candidateIso.filename().wstring(), 248,
                 276, content - 16, 28, fonts.body, ink,
                 DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            const auto state = child    ? L"PLAYING"
                               : busy() ? L"WORKING"
                               : disc   ? L"VERIFIED"
                                        : L"SELECT ISO";
            text(memory, state, w - 184, 178, 136, 24, fonts.small, disc ? gold : muted,
                 DT_RIGHT | DT_SINGLELINE);
            text(memory, L"Your ISO is verified before every launch.", 224, 420, content, 28,
                 fonts.small, muted);
        } else if (page == 1) {
            fill(memory, {p(224), p(152), p(w - 24), p(220)}, panel);
            text(memory, disc ? L"Memory card · Slot 1" : L"Verify an ISO to manage its saves", 244,
                 174, content - 16, 30, fonts.body);
            text(memory, L"Import a PSU save, an extracted save folder, or an OpenKai2 ZIP backup.",
                 224, 300, content + 16, 28, fonts.small, muted);
            if (backups.empty())
                text(memory, L"No backups yet. Create one before your next session.", 244, 404,
                     content - 16, 40, fonts.body, muted);
        } else if (page == 2) {
            const int col = (content - 24) / 2;
            const wchar_t *labels[] = {L"Renderer",
                                       L"Frame rate limit",
                                       L"Window resolution",
                                       L"Display mode",
                                       L"Anti-aliasing",
                                       L"Texture filtering",
                                       L"Upscaling",
                                       L"VSync",
                                       L"Automatic session backups",
                                       L"Aspect ratio"};
            for (int i = 0; i < 10; ++i)
                text(memory, labels[i], i % 2 ? 224 + col + 24 : 224, 160 + (i / 2) * 76, col, 24,
                     fonts.small, muted);
            text(memory, L"Changes apply next launch. Game speed stays at its original rate.", 224,
                 566, content + 16, 28, fonts.small, muted);
        } else {
            const int col = (content - 24) / 2;
            text(memory, L"Release channel", 224, 160, col, 24, fonts.small, muted);
            text(memory, L"Automatic updates", 224 + col + 24, 160, col, 24, fonts.small, muted);
            text(memory, L"Installed version  " + wide(application_version), 224, 264, content, 30,
                 fonts.body, gold);
            text(memory, updateStatus, 224, 310, content, 64, fonts.body, ink,
                 DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
            text(memory, L"Downloads are checked against the release SHA-256 checksums. Updates apply"
                         L" after the game and save backups finish, then restart the launcher.",
                 224, 486, content, 64, fonts.small, muted, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
            text(memory, L"ISO preferences, saves and backups stay in your data folder.",
                 224, 558, content, 28, fonts.small, muted);
        }
        const int footer = std::max(608, MulDiv(rect.bottom, 96, int(dpi)) - 52);
        line(memory, p(224), p(footer - 12), p(content + 16), border);
        std::wstring message = status;
        if (busy()) {
            std::lock_guard lock(progress->mutex);
            if (!progress->text.empty())
                message = progress->text;
        }
        text(memory, message, 224, footer, content + 16, 36, fonts.small, error ? red : muted,
             DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
        BitBlt(dc, 0, 0, rect.right, rect.bottom, memory, 0, 0, SRCCOPY);
        SelectObject(memory, old);
        DeleteObject(bitmap);
        DeleteDC(memory);
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
            fill(dc, r, color);
            if (chosen)
                fill(dc, {r.left, r.top, r.left + p(3), r.bottom}, orange);
            wchar_t label[100]{};
            GetWindowTextW(item.hwndItem, label, 100);
            auto old = SelectObject(dc, primary ? fonts.brand : fonts.body);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, primary && enabled ? background : enabled ? ink : muted);
            RECT textRect = r;
            textRect.left += p(16);
            DrawTextW(dc, label, -1, &textRect,
                      DT_SINGLELINE | DT_VCENTER | (navigation ? DT_LEFT : DT_CENTER) |
                          DT_NOPREFIX);
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
    }
    void start(Job kind, std::filesystem::path source = {}) {
        if (busy() || child)
            return;
        activeJob = kind;
        if (kind == Job::verify || kind == Job::launch)
            candidateIso = source.empty() ? settings.iso : source;
        const auto prefs = settings;
        const auto save = card(), archive = backupPath();
        const auto availableRelease = release;
        auto updates = progress;
        {
            std::lock_guard lock(updates->mutex);
            updates->text = kind == Job::verify || kind == Job::launch
                                ? L"Verifying your ISO…"
                                : kind == Job::checkUpdate || kind == Job::downloadUpdate
                                      ? L"Checking GitHub for updates…" : L"Working with your saves…";
        }
        error = false;
        std::cout << "[launcher] operation=" << int(kind) << " started" << std::endl;
        task = std::async(std::launch::async, [kind, source, prefs, save, archive, updates, availableRelease]() {
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
        layout();
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
        if (busy() && task.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
            try {
                auto result = task.get();
                status = result.message;
                std::cout << "[launcher] operation=" << int(result.kind) << " completed"
                          << std::endl;
                error = false;
                if (result.disc) {
                    disc = std::move(result.disc);
                    settings.iso = result.image;
                    write_settings(settings_path(), settings);
                }
                if (result.kind == Job::checkUpdate || result.kind == Job::downloadUpdate) {
                    release = std::move(result.release);
                    staged = std::move(result.staged);
                    updateStatus = result.message;
                }
                refreshBackups();
                if (!closing && result.kind == Job::launch)
                    launch();
                if (!closing && result.kind == Job::downloadUpdate && staged) {
                    apply_update(*staged);
                    staged.reset();
                    DestroyWindow(hwnd);
                    return;
                }
            } catch (const std::exception &e) {
                if (activeJob == Job::verify || activeJob == Job::launch)
                    disc.reset();
                status = wide(e.what());
                if (activeJob == Job::checkUpdate || activeJob == Job::downloadUpdate)
                    updateStatus = status;
                std::cerr << "[launcher:error] " << e.what() << std::endl;
                error = true;
            }
            if (closing) {
                DestroyWindow(hwnd);
                return;
            }
            layout();
        }
        if (child && WaitForSingleObject(child, 0) == WAIT_OBJECT_0) {
            DWORD code = 0;
            GetExitCodeProcess(child, &code);
            CloseHandle(child);
            child = nullptr;
            status =
                code ? L"The game stopped with an error. See the startup log." : L"Session ended.";
            error = code != 0;
            if (!closing) {
                ShowWindow(hwnd, SW_RESTORE);
                SetForegroundWindow(hwnd);
            }
            if (settings.autoBackups && disc)
                start(Job::afterExit);
            else if (closing) {
                DestroyWindow(hwnd);
                return;
            } else
                layout();
        }
        if (!child && !busy()) {
            const bool active = session_active();
            if (active != externalGame) {
                externalGame = active;
                layout();
            }
        }
        if (!closing && !busy() && !child && !externalGame) {
            if (staged && settings.autoUpdates) {
                // The worker waits for this process (and a portable wrapper) to exit.
                try { apply_update(*staged); }
                catch (const std::exception &e) {
                    updateStatus = status = wide(e.what());
                    error = true;
                    settings.autoUpdates = false;
                    SendMessageW(controls[AutomaticUpdates], CB_SETCURSEL, 0, 0);
                    layout();
                    return;
                }
                staged.reset();
                DestroyWindow(hwnd);
                return;
            }
            if (updatePending) {
                updatePending = false;
                if (settings.autoUpdates)
                    start(Job::checkUpdate);
            }
        }
        if (busy())
            InvalidateRect(hwnd, nullptr, FALSE);
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
            const auto saved = settings.iso.wstring();
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
            IShellItem *directory = nullptr;
            if (SUCCEEDED(SHCreateItemFromParsingName(settings.iso.parent_path().c_str(), nullptr,
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
        settings.renderer = selected(Renderer) == 1   ? "gpu"
                            : selected(Renderer) == 2 ? "cpu"
                                                      : "auto";
        settings.fps = rates.at(size_t(selected(Fps)));
        const auto size = sizes.at(size_t(selected(Resolution)));
        settings.width = size.first;
        settings.height = size.second;
        settings.windowMode = unsigned(selected(WindowMode));
        settings.antialiasing = unsigned(selected(Antialiasing));
        settings.textureFilter = unsigned(selected(Filtering));
        settings.upscaler = unsigned(selected(Upscaler));
        settings.vsync = selected(Vsync) != 0;
        settings.autoBackups = selected(AutoBackup) != 0;
        settings.aspect = unsigned(selected(Aspect));
        write_settings(settings_path(), settings);
        status = L"Settings saved. Changes apply next launch.";
        error = false;
    }
    void command(int id, int notification) {
        if (picking)
            return;
        if (id >= PlayTab && id <= UpdatesTab) {
            page = id - PlayTab;
            layout();
            return;
        }
        if (id >= Renderer && id <= Aspect && notification == CBN_SELCHANGE) {
            saveOptions();
            layout();
            return;
        }
        if ((id == UpdateChannel || id == AutomaticUpdates) && notification == CBN_SELCHANGE) {
            settings.updateChannel = unsigned(selected(UpdateChannel));
            settings.autoUpdates = selected(AutomaticUpdates) != 0;
            write_settings(settings_path(), settings);
            release.reset();
            staged.reset();
            updatePending = true;
            updateStatus = L"Update preferences saved.";
            layout();
            return;
        }
        if (id == ReleasePage) {
            ShellExecuteW(hwnd, L"open", L"https://github.com/7v0biscuit/OpenKai2/releases", nullptr,
                          nullptr, SW_SHOWNORMAL);
            return;
        }
        if (busy() || child)
            return;
        if (session_active()) {
            status = L"Close the running game before changing its ISO or saves.";
            layout();
            return;
        }
        if (id == CheckUpdates) {
            updatePending = false;
            start(Job::checkUpdate);
        } else if (id == InstallUpdate) {
            if (staged) {
                apply_update(*staged);
                staged.reset();
                DestroyWindow(hwnd);
            } else if (release)
                start(Job::downloadUpdate);
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
        if (task.valid())
            task.wait();
        if (child)
            CloseHandle(child);
    }
};
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
            if (self->busy()) {
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
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    std::cout << "[launcher] closed" << std::endl;
    return 0;
}
} // namespace openkai2::launcher
