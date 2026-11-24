// SPDX-License-Identifier: GPL-3.0-only
#include "launcher/window.h"
#include "discord_presence.h"
#include "launcher/display_font.h"
#include "launcher/layout.h"
#include "disc_image.h"
#include "launcher/saves.h"
#include "launcher/session.h"
#include "launcher/settings.h"
#include "launcher/online_browser.h"
#include "launcher/update.h"
#include "launcher/version.h"
#include "speedrun.h"
#include "speedrun_images.h"
#include "dev_identity.h"
#include "name_style.h"
#include "achievements.h"
#include "retroachievements.h"
#include "netplay.h"
#include "room.h"
#include "online.h"
#include "profile_choices.h"
#include <deque>
#include "mods/budokai3.h"
#include "mods/disc_identity.h"
#include "mods/importer.h"
#include "mods/mod_library.h"
#include "mods/mod_package.h"
#include "mods/roster.h"
#include "mods/stages.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#include <windowsx.h>
#include <objidl.h>
#include <gdiplus.h>

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
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <future>
#include <iostream>
#include <map>
#include <unordered_map>
#include <utility>
#include <memory>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <thread>
#include <optional>
#include <set>
#include <ctime>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <vector>

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
COLORREF styleColor(name_style::Rgb c){return RGB(std::clamp(c.r,0,255),std::clamp(c.g,0,255),std::clamp(c.b,0,255));}
name_style::Rgb styleRgb(COLORREF c){return {GetRValue(c),GetGValue(c),GetBValue(c)};}
COLORREF nameColorAt(int index){return styleColor(name_style::name_colors[size_t(index)].base);}
constexpr int name_color_custom=int(name_style::name_color_count)+1;
ULONGLONG motionTicks() {
    static const LONGLONG frequency=[]{LARGE_INTEGER f{};QueryPerformanceFrequency(&f);return f.QuadPart>0 ? f.QuadPart : 1;}();
    LARGE_INTEGER now{};QueryPerformanceCounter(&now);
    return ULONGLONG(now.QuadPart/frequency)*1000ull+ULONGLONG((now.QuadPart%frequency)*1000/frequency);
}
uint32_t storedColor(COLORREF color){return 0x1000000u|(uint32_t(GetRValue(color))<<16u)|(uint32_t(GetGValue(color))<<8u)|uint32_t(GetBValue(color));}
COLORREF shownColor(uint32_t color){return RGB((color>>16u)&255u,(color>>8u)&255u,color&255u);}
double luminance(COLORREF color){
    const auto channel=[](int value){const double c=value/255.0;return c<=0.03928 ? c/12.92 : std::pow((c+0.055)/1.055,2.4);};
    return 0.2126*channel(GetRValue(color))+0.7152*channel(GetGValue(color))+0.0722*channel(GetBValue(color));
}
bool wavePalette = false;
COLORREF waveTint = RGB(10, 12, 34);
void apply_theme(unsigned choice, unsigned accent, const CustomColors &custom) {
    const auto t = make_theme(choice, accent, custom, motionTicks());
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
    if (wavePalette) {
        const auto lift = [](COLORREF base, int amount) { return RGB(GetRValue(base) + (255 - GetRValue(base)) * amount / 255, GetGValue(base) + (255 - GetGValue(base)) * amount / 255, GetBValue(base) + (255 - GetBValue(base)) * amount / 255); };
        background = lift(waveTint, 12); sidebar = lift(waveTint, 6); panel = lift(waveTint, 26);
        field = lift(waveTint, 16); raised = lift(waveTint, 44); hover = lift(waveTint, 62); border = lift(waveTint, 104);
        ink = RGB(252, 248, 242); muted = RGB(204, 194, 214); faint = RGB(146, 136, 162);
        heroFrom = RGB(36, 28, 62); heroTo = RGB(120, 62, 40);
        COLORREF* const lifted[] = {&background, nullptr, &panel, &field, &raised, &hover, &border, &ink, &muted, &faint};
        for (unsigned role = 0; role < sizeof(lifted) / sizeof(lifted[0]); ++role)
            if (lifted[role] && custom[role] >= 0)
                *lifted[role] = RGB((custom[role] >> 16) & 255, (custom[role] >> 8) & 255, custom[role] & 255);
    }
}
bool wave_color_role(unsigned role) { return role != 1 && (role < 15 || role > 18); }
COLORREF shown_role_color(unsigned role, COLORREF themed) {
    if (!wavePalette) return themed;
    COLORREF* const lifted[] = {&background, nullptr, &panel, &field, &raised, &hover, &border, &ink, &muted, &faint};
    return role < sizeof(lifted) / sizeof(lifted[0]) && lifted[role] ? *lifted[role] : themed;
}

constexpr int contentLeft = 36, margin = 36;
constexpr int shellHeader = 76, shellDockReserve = 116, waveDockReserve = 56;
constexpr int speedrunTop = 112, splitsTop = 284, speedrunRulesTop = 526, classicSplitsTop = splitsTop;
constexpr int onlineProfileTop = 112, onlineHostTop = 248, onlineHostBottom = 352, onlineListsTop = 364;
constexpr int waveOnlinePlayersTop = 252, waveOnlineRoomsTop = 680, waveOnlineHostTop = 1092, waveOptionRow = 56;
struct LoungeLayout {
    bool compact;
    int left=contentLeft, friendsLeft, friendsWidth, friendsBottom, centre, centreRight, details, detailsTop,
        right, bottom, gameBottom, roomsTop, notesTop, notesBottom;
    LoungeLayout(int width,int height) : compact(width<1100),
        friendsWidth(compact ? std::clamp(width*28/100,240,292) : std::clamp(width*19/100,200,300)),
        friendsBottom(compact ? 424 : height-128),centre(compact ? left : left+friendsWidth+20),
        centreRight(left+width-(compact ? friendsWidth : std::clamp(width*23/100,232,294))-20),
        details(centreRight+20),detailsTop(compact ? 440 : 112),right(left+width),bottom(height-128),
        gameBottom(348),roomsTop(gameBottom+92),notesTop(height-112),notesBottom(height-32) {
        friendsLeft=compact ? details : left;
    }
    int centreWidth() const{return centreRight-centre;}
    int detailsWidth() const{return right-details;}
};
struct HostRow {
    int column;
    explicit HostRow(int contentWidth) : column((contentWidth - 40 - 4 * 12) / 5) {}
    int x(int index) const { return contentLeft + 20 + index * (column + 12); }
    static constexpr int label = onlineHostTop + 44, field = onlineHostTop + 62;
};
constexpr UINT onlineChanged = WM_APP + 30, onlineNotice = WM_APP + 31;

enum Id {
    PlayTab = 101,
    OnlineTab,
    SpeedrunTab,
    ModsTab,
    SavesTab,
    SettingsTab,
    UpdatesTab,
    ControlsTab,
    ChooseIso = 110,
    VerifyIso,
    Play,
    LoadState,
    ReleaseNotes,
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
    ImageEffect = 172,
    Brightness = 165,
    ModInstall = 166,
    ModExport = 167,
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
    OverlayCorner = 226,
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
    SplitNameField,
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
    ShortcutLast = ShortcutFirst + tab_count - 1,
    ColorList,
    ColorPick,
    ColorReset,
    ColorResetAll,
    CloseColors,
    OpenColors,
    RandomizerMode,
    LoadTimes,
    RunAhead,
    SpeedrunRandomizer,
    ColorCopy,
    ColorPaste,
    ColorSurprise,
    OpenLogs,
    ControlsReset = 400,
    ControlKey = 410,
    ControlAlternate = 430,
    HomeProfile = 1000, HomeControls = 1003, HomeSaves, HomeSettings, HomeArrange,
    ProfileInitials, ProfileTagline, ProfileAccent, ProfileSave, ProfileReset, ProfileCopyId, NavigateBack,
    SplitStyleList = 1020, SplitStyleAdd, SplitImageChoose, SplitImageRemove, SplitStyleReset, SplitNameSave,
    ProfileImageChoose = 1040, ProfileImageRemove, OnlineSearch, RoomFilter, PlayerFilter, ClearOnlineSearch, FocusOnlineSearch,
    HomeFriendScope = 1060, HomeFindPlayers, HomeInvite, HomeCreateRoom,
    ControlPad = 450,
    FindAction = 1100, ActionSearch, ActionList, ActionOpen, ActionClose,
    BackToPlay, VerifySelectedIso, SaveSearch, SaveSort, ClearSaveSearch,
    SettingsSearch, ClearSettingsSearch, ResetDisplay, ResetGame, UndoSettings,
    SettingsAll=1200, SettingsDisplay, SettingsGraphics, SettingsGame, SettingsLauncher,
    OpenNotifications=1210,
    ProfileFit=1220, ProfileNotifications, ProfileCopyFrom, ProfileCopyStyle, ProfileRecover, ProfileManage, LauncherMotion, LauncherHints, ProfileAvailability, ProfileNameColor, ProfileFighter, ProfileSharing, ProfileFriendAlerts,
    OpenHelp=1240,NextDockTab,PreviousDockTab,PageTop,PageBottom,SaveCurrent,UndoCurrent,ToggleLauncher,CopySetup,OpenIsoFolder,
    ProfileAbout=1300, ProfileRegion, ProfilePlaystyle, ProfilePreview, PlayerAction, PlayerFriend, PlayerBlock, PlayerCopyId, PlayerProfile, ProfileRename, ProfileLink, PlayerChannel, LauncherInterface, OpenWaveHome, WaveSky, WaveEmblem, LauncherClock, WaveRibbons,
    OpenAchievements, RaUser, RaPassword, RaSignIn, RaSignOut, RaHardcore, RaCopyHash, RaOpenSet, RaRefresh, RaOpenSite,
    ProfileFrame, ProfileBackdrop, ProfileTitle, ProfileFrameColor, ProfileFeatured
};
constexpr int pageCount = 8, onlinePage = 1, speedrunPage = 2, modsPage = 3, savesPage = 4, settingsPage = 5, updatesPage = 6,
              controlsPage = 7, importPage = 8, overlayPage = 9, tabsPage = 10, colorsPage = 11,
              profilePage = 12, finderPage = 13, notificationsPage=14, helpPage=15, playerPage=16, wavePage=17, achievementsPage=18;
constexpr UINT controlKeyMessage = WM_APP + 40, controlClearMessage = WM_APP + 41,
               pageClipMessage = WM_APP + 42, restoreUiMessage = WM_APP + 43,
               deferredUiMessage = WM_APP + 44, glideMessage = WM_APP + 45, retroMessage = WM_APP + 46, revealMessage = WM_APP + 47;
constexpr std::array<int, 17> padChoices{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 15, 16, 17};
constexpr int controlsTop = 112, controlRowTop = 196;
constexpr std::array<unsigned, 4> audioVolumes{100u, 75u, 50u, 25u};
int audioChoice(const Settings &settings) {
    if (!settings.audio)
        return 0;
    const auto nearest = std::min_element(audioVolumes.begin(), audioVolumes.end(), [&](unsigned a, unsigned b) {
        return std::abs(int(a) - int(settings.volume)) < std::abs(int(b) - int(settings.volume));
    });
    return 1 + int(nearest - audioVolumes.begin());
}

RECT fitWindowToMonitor(RECT target) {
    MONITORINFO monitor{sizeof(monitor)};
    if(!GetMonitorInfoW(MonitorFromRect(&target,MONITOR_DEFAULTTONEAREST),&monitor))return target;
    const auto& work=monitor.rcWork;
    const LONG width=std::clamp(target.right-target.left,1L,work.right-work.left);
    const LONG height=std::clamp(target.bottom-target.top,1L,work.bottom-work.top);
    target.left=std::clamp(target.left,work.left,work.right-width);
    target.top=std::clamp(target.top,work.top,work.bottom-height);
    target.right=target.left+width;target.bottom=target.top+height;
    return target;
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
Gdiplus::Color drawingColor(COLORREF color) {return Gdiplus::Color(255,GetRValue(color),GetGValue(color),GetBValue(color));}
void smoothDrawing(Gdiplus::Graphics& graphics) {
    GdiFlush();
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    graphics.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);
}
template<class Distance>
bool contour(HDC dc, RECT r, Distance distance, COLORREF face, COLORREF outline, bool filled, RECT interior={}) {
    const auto& list=surfaces();
    const auto found=std::find_if(list.begin(),list.end(),[dc](const Surface& s){return s.dc==dc;});
    if(found==list.end() || !found->pixels)return false;
    if(filled && interior.right>interior.left && interior.bottom>interior.top)fill(dc,interior,face);
    GdiFlush();POINT origin{};GetViewportOrgEx(dc,&origin);
    const uint32_t solid=uint32_t(GetRValue(face))<<16|uint32_t(GetGValue(face))<<8|GetBValue(face);
    for(int y=std::max<int>(r.top,-origin.y);y<std::min<int>(r.bottom,found->height-origin.y);++y)
        for(int x=std::max<int>(r.left,-origin.x);x<std::min<int>(r.right,found->width-origin.x);++x) {
            if(filled && y>=interior.top && y<interior.bottom && x>=interior.left && x<interior.right){x=interior.right-1;continue;}
            const double d=distance(x+.5,y+.5);
            const double outer=std::clamp(.5-d,0.0,1.0),inner=std::clamp(-.5-d,0.0,1.0);
            const double cover=filled ? outer : outer-inner;
            if(cover<=0)continue;
            auto& pixel=found->pixels[size_t(y+origin.y)*found->width+x+origin.x];
            if(filled && inner>=1){pixel=solid;continue;}
            const auto color=filled ? blend(outline,face,inner/outer) : outline;
            const auto under=RGB(pixel>>16&255,pixel>>8&255,pixel&255);
            const auto out=blend(under,color,cover);
            pixel=uint32_t(GetRValue(out))<<16|uint32_t(GetGValue(out))<<8|GetBValue(out);
        }
    return true;
}
template<class Draw>
void sampledVector(HDC dc,RECT area,Draw draw) {
    const int width=area.right-area.left,height=area.bottom-area.top,samples=4;
    if(width<=0 || height<=0)return;
    Gdiplus::Bitmap source(width*samples,height*samples,PixelFormat32bppPARGB);
    {
        Gdiplus::Graphics graphics(&source);smoothDrawing(graphics);
        graphics.Clear(Gdiplus::Color(0,0,0,0));
        graphics.ScaleTransform(float(samples),float(samples));
        graphics.TranslateTransform(float(-area.left),float(-area.top));
        draw(graphics);
    }
    Gdiplus::Rect bounds(0,0,width*samples,height*samples);Gdiplus::BitmapData data{};
    if(source.LockBits(&bounds,Gdiplus::ImageLockModeRead,PixelFormat32bppPARGB,&data)!=Gdiplus::Ok)return;
    std::vector<unsigned char> pixels(size_t(width)*height*4);
    for(int y=0;y<height;++y)for(int x=0;x<width;++x)for(int c=0;c<4;++c) {
        unsigned sum=0;
        for(int sy=0;sy<samples;++sy)for(int sx=0;sx<samples;++sx)
            sum+=static_cast<const unsigned char*>(data.Scan0)[(y*samples+sy)*data.Stride+(x*samples+sx)*4+c];
        pixels[(size_t(y)*width+x)*4+c]=static_cast<unsigned char>((sum+8)/16);
    }
    source.UnlockBits(&data);
    Gdiplus::Bitmap result(width,height,width*4,PixelFormat32bppPARGB,pixels.data());
    Gdiplus::Graphics target(dc);
    target.DrawImage(&result,area.left,area.top,width,height);
}
void smoothBox(HDC dc,RECT r,COLORREF face,COLORREF outline,float radius,bool filled=true) {
    if(r.right<=r.left || r.bottom<=r.top)return;
    const double halfWidth=(r.right-r.left)/2.0,halfHeight=(r.bottom-r.top)/2.0;
    const double corner=std::clamp(double(radius),0.0,std::min(halfWidth,halfHeight));
    const LONG inset=LONG(std::ceil(corner))+1;
    const RECT interior{r.left+inset,r.top+1,r.right-inset,r.bottom-1};
    if(contour(dc,r,[&](double x,double y){
        const double qx=std::abs(x-r.left-halfWidth)-halfWidth+corner;
        const double qy=std::abs(y-r.top-halfHeight)-halfHeight+corner;
        return (qx>0 && qy>0 ? std::hypot(qx,qy) : std::max(qx,qy))-corner;
    },face,outline,filled,interior))return;
    sampledVector(dc,r,[&](Gdiplus::Graphics& graphics){
        const float x=float(r.left)+.5f,y=float(r.top)+.5f,w=float(r.right-r.left)-1.f,h=float(r.bottom-r.top)-1.f;
        const float d=float(corner)*2;Gdiplus::GraphicsPath path;
        if(d<1)path.AddRectangle(Gdiplus::RectF(x,y,w,h));
        else {path.AddArc(x,y,d,d,180,90);path.AddArc(x+w-d,y,d,d,270,90);
            path.AddArc(x+w-d,y+h-d,d,d,0,90);path.AddArc(x,y+h-d,d,d,90,90);path.CloseFigure();}
        if(filled){Gdiplus::SolidBrush brush(drawingColor(face));graphics.FillPath(&brush,&path);}
        Gdiplus::Pen pen(drawingColor(outline),1.f);graphics.DrawPath(&pen,&path);
    });
}
void smoothLines(HDC dc,std::initializer_list<Gdiplus::PointF> points,COLORREF color,float width) {
    if(points.size()<2)return;
    float left=points.begin()->X,right=left,top=points.begin()->Y,bottom=top;
    for(const auto& p:points){left=std::min(left,p.X);right=std::max(right,p.X);top=std::min(top,p.Y);bottom=std::max(bottom,p.Y);}
    const int pad=int(std::ceil(width/2))+1;
    sampledVector(dc,{LONG(std::floor(left))-pad,LONG(std::floor(top))-pad,LONG(std::ceil(right))+pad,LONG(std::ceil(bottom))+pad},[&](Gdiplus::Graphics& graphics){
        Gdiplus::Pen pen(drawingColor(color),width);pen.SetLineJoin(Gdiplus::LineJoinRound);
        pen.SetStartCap(Gdiplus::LineCapRound);pen.SetEndCap(Gdiplus::LineCapRound);
        graphics.DrawLines(&pen,points.begin(),INT(points.size()));
    });
}
void smoothPolygon(HDC dc,const Gdiplus::PointF* points,int count,COLORREF color) {
    if(count<3)return;
    float left=points[0].X,right=left,top=points[0].Y,bottom=top;
    for(int i=1;i<count;++i){left=std::min(left,points[i].X);right=std::max(right,points[i].X);top=std::min(top,points[i].Y);bottom=std::max(bottom,points[i].Y);}
    sampledVector(dc,{LONG(std::floor(left))-1,LONG(std::floor(top))-1,LONG(std::ceil(right))+1,LONG(std::ceil(bottom))+1},[&](Gdiplus::Graphics& graphics){
        Gdiplus::SolidBrush brush(drawingColor(color));graphics.FillPolygon(&brush,points,count);
    });
}
void ellipse(HDC dc,int cx,int cy,int radius,COLORREF color) {
    if(radius<1)return;
    const RECT bounds{cx-radius,cy-radius,cx+radius,cy+radius};
    if(contour(dc,bounds,[&](double x,double y){return std::hypot(x-cx,y-cy)-radius;},color,color,true))return;
    sampledVector(dc,bounds,[&](Gdiplus::Graphics& graphics){Gdiplus::SolidBrush brush(drawingColor(color));
        graphics.FillEllipse(&brush,float(cx-radius),float(cy-radius),float(radius*2),float(radius*2));});
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
struct SavedDc {
    HDC dc;
    int state;
    explicit SavedDc(HDC value) : dc(value), state(SaveDC(value)) {}
    ~SavedDc() { if (state) RestoreDC(dc, state); }
};
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
    const wchar_t *bodyFace = L"Inter", *strongFace = L"Inter SemiBold", *textFace = L"Inter";
    HFONT body{}, caption{}, label{}, heading{}, brand{}, strong{}, gameName{}, title{}, mark{};
    void clear() {
        for (auto f : {body, caption, label, heading, brand, strong, gameName, title, mark})
            if (f)
                DeleteObject(f);
        body = caption = label = heading = brand = strong = gameName = title = mark = nullptr;
    }
    void make(unsigned dpi) {
        clear();
        auto font = [&](int size, int weight, const wchar_t *face = nullptr, BOOL italic = FALSE) {
            return CreateFontW(-MulDiv(size, int(dpi), 96), 0, 0, 0, weight, italic, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               ANTIALIASED_QUALITY, DEFAULT_PITCH, face ? face : weight>=FW_SEMIBOLD ? strongFace : bodyFace);
        };
        body = font(15, FW_NORMAL);
        caption = font(13, FW_NORMAL, textFace);
        label = font(13, FW_NORMAL);
        heading = font(28, FW_NORMAL);
        brand = font(20, FW_NORMAL);
        strong = font(16, FW_NORMAL);
        gameName = font(62, FW_NORMAL);
        title = font(30, FW_NORMAL);
        mark = font(24, FW_SEMIBOLD);
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
struct ControlsLayout {
    bool stacked;
    int column, pad, key;
    explicit ControlsLayout(int width) : stacked(width < 1180),
        column(stacked ? width - 40 : (width - 64) / 2),
        pad(std::clamp(column * 36 / 100, 190, 282)), key((column - 104 - pad - 20) / 2) {}
    int x(unsigned index) const { return contentLeft + 20 + (stacked ? 0 : int(index / 8) * (column + 24)); }
    int y(unsigned index) const { return controlRowTop + int(index % 8) * 44 + (stacked ? int(index / 8) * 392 : 0); }
    int bottom() const { return controlRowTop + 8 * 44 + (stacked ? 392 : 0) + 8; }
};
LRESULT CALLBACK combo_procedure(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK button_procedure(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK edit_procedure(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK save_list_procedure(HWND, UINT, WPARAM, LPARAM);
struct Window {
    HWND hwnd{}, hoverHints{};
    std::map<int, std::wstring> hintText;
    std::map<int, LRESULT> dropdownOriginal;
    std::map<int,int> comboTextWidths;
    std::set<int> comboValueHints;
    Fonts fonts;
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
    std::map<std::string, speedrun::SplitImage> splitImages, profileImages;
    struct RemoteCard {uint64_t revision=0;online::Card card;speedrun::SplitImage image;};
    std::map<std::string,RemoteCard> remoteCards;
    std::map<std::string,std::string> cardThumbnails;
    uint32_t customNameColor=0x1ffcc40u;int nameColorSelection=0;
    std::set<std::string> friendsOnline;std::string friendWatchProfile;std::chrono::steady_clock::time_point friendWatchSince{};
    int styleSelection = 0;
    bool imageAnimationTicking = false;
    UINT motionInterval = 16;
    bool cycling = false;
    unsigned long long ambientAt = 0;
    int paintedPage = -1, paintedScroll = -1;
    unsigned displayedMinute=~0u;
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
    std::string lastRoomMessage;
    bool roomLaunched = false;
    bool directJoin = false;
    std::string joinAddress, joinRoomId;
    uint16_t roomPort = 0;
    struct Notice {
        bool invite = true;
        online::Invite inviteMessage;
        online::FriendMessage friendMessage;
        std::chrono::steady_clock::time_point at = std::chrono::steady_clock::now();
        const std::string &sender() const { return invite ? inviteMessage.from_id : friendMessage.from_id; }
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
    std::vector<online::Room> roomRows, availableRooms;
    std::vector<std::pair<std::string, std::string>> playerRows, friendRows;
    std::vector<std::wstring> shownFriendTexts, shownPlayerTexts;
    std::map<std::string, int> roomPings;
    std::vector<online::Player> playerDetails;
    std::vector<std::string> friendStatus;
    std::set<std::string> requestedFriends;
    struct NameCheck {
        std::mutex mutex;
        bool done = false;
        std::optional<online::NameState> state;
        std::string name, renameFrom;
        crypto::Seed seed{};
        std::chrono::steady_clock::time_point started;
    };
    std::shared_ptr<NameCheck> nameCheck;
    int capturing = -1;
    LPARAM pendingCaptureAlt=0;
    std::string hostingRoom, hostingToken, pendingInvitee;
    std::chrono::steady_clock::time_point inviteDeadline{};
    bool trayAdded = false, wasRunning = false;
    unsigned launchState = 0;
    int pageScroll=0, laidOutPage=-1, wheelRemainder=0;
    double dockPosition=0, dockFrom=0;
    int dockTarget=-1;
    ULONGLONG dockStarted=0;
    int homeScope=-1;
    bool layoutPending=false, forwardingWheel=false, layingOut=false, layoutAgain=false;
    bool painting=false, repaintPending=false, dispatchingUi=false;
    struct DeferredMessage {
        UINT message;
        WPARAM wp;
        LPARAM lp;
        RECT rectangle{};
    };
    std::deque<DeferredMessage> deferredMessages;
    bool deferDuringPaint(UINT message, WPARAM wp, LPARAM lp) {
        if (!painting) return false;
        switch (message) {
        case WM_COMMAND: case WM_TIMER: case WM_SIZE: case WM_DPICHANGED:
        case WM_CLOSE: case WM_DROPFILES: case WM_MOUSEWHEEL: case WM_VSCROLL:
        case pageClipMessage: case restoreUiMessage: case onlineChanged: case onlineNotice:
        case controlKeyMessage: case controlClearMessage:
            break;
        default: return false;
        }
        if (message==WM_COMMAND && closingDropdowns &&
            (HIWORD(wp)==CBN_SELCHANGE || HIWORD(wp)==CBN_SELENDOK)) return true;
        DeferredMessage pending{message,wp,lp};
        if (message==WM_DPICHANGED) pending.rectangle=*reinterpret_cast<const RECT*>(lp);
        if (message==WM_TIMER && std::any_of(deferredMessages.begin(),deferredMessages.end(),
            [wp](const auto& item){return item.message==WM_TIMER && item.wp==wp;})) return true;
        deferredMessages.push_back(pending);
        SetPropW(hwnd,L"OpenKai2.PendingUi",reinterpret_cast<HANDLE>(1));
        return true;
    }
    void dispatchDeferredUi() {
        if (painting || dispatchingUi) return;
        dispatchingUi=true;
        struct Dispatch {
            Window& window;
            ~Dispatch() {
                window.dispatchingUi=false;
                if (window.deferredMessages.empty()) RemovePropW(window.hwnd,L"OpenKai2.PendingUi");
                else PostMessageW(window.hwnd,deferredUiMessage,0,0);
            }
        } dispatch{*this};
        auto pending=std::move(deferredMessages);
        deferredMessages.clear();
        for (auto& item:pending) {
            if (!IsWindow(hwnd)) break;
            SendMessageW(hwnd,item.message,item.wp,item.message==WM_DPICHANGED ?
                reinterpret_cast<LPARAM>(&item.rectangle) : item.lp);
        }
    }
    bool appActive=true, wasMinimized=false, restoreQueued=false, closingDropdowns=false;
    HWND lastFocusedControl{};
    void queueUiRestore() {
        if (!restoreQueued && hwnd && !closing) {
            restoreQueued=true;
            PostMessageW(hwnd,restoreUiMessage,0,0);
        }
    }
    void suspendUi() {
        if (const auto focus=GetFocus(); focus && IsChild(hwnd,focus)) lastFocusedControl=focus;
        hotButton=0;
        closingDropdowns=true;
        const auto originals=dropdownOriginal;
        for (const auto& [id,control] : controls) {
            RemovePropW(control,L"OpenKai2.Hot");
            wchar_t kind[32]{};GetClassNameW(control,kind,32);
            if (_wcsicmp(kind,L"ComboBox")!=0) continue;
            if (const auto choice=originals.find(id); choice!=originals.end())
                SendMessageW(control,CB_SETCURSEL,choice->second,0);
            if (SendMessageW(control,CB_GETDROPPEDSTATE,0,0)) SendMessageW(control,CB_SHOWDROPDOWN,FALSE,0);
        }
        dropdownOriginal.clear(); closingDropdowns=false;
        if (hoverHints) SendMessageW(hoverHints,TTM_POP,0,0);
        pendingCaptureAlt=0;
        if (capturing>=0) {capturing=-1;refreshControls();}
        if (dockStarted) {dockPosition=double(dockTarget);dockStarted=0;endFrames(4);}
    }
    void restoreUi() {
        restoreQueued=false;
        if (!appActive || IsIconic(hwnd) || !IsWindowVisible(hwnd) || !IsWindowEnabled(hwnd) || picking || controls.empty()) return;
        ambient.clear(); paintedPage=-1; paintedScroll=-1; paintedSize={};
        layout();
        const auto focus=GetFocus();
        if ((!focus || focus==hwnd) && lastFocusedControl && IsWindow(lastFocusedControl) &&
            IsChild(hwnd,lastFocusedControl) && IsWindowVisible(lastFocusedControl) && IsWindowEnabled(lastFocusedControl))
            SetFocus(lastFocusedControl);
        (pageDirty=true),RedrawWindow(hwnd,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_FRAME|RDW_ALLCHILDREN);
    }
    int finderFrom = 0, finderReturnPage = 0, profileReturnPage=0,helpReturnPage=0,helpOrigin=0,playerReturnPage=0,playerNavigation=0;
    std::string viewedPlayer,viewedName;
    struct Action { int id; const wchar_t *name; const wchar_t *keywords; };
    const std::vector<Action> actions{
        {Play, L"Play game", L"start offline launch"},
        {ChooseIso, L"Choose a game ISO", L"disc browse file change"},
        {VerifyIso, L"Verify selected ISO", L"disc check files"},
        {HomeSaves, L"Saves & backups", L"memory card import restore delete"},
        {HomeControls, L"Controls", L"keyboard controller bindings"},
        {OnlineTab, L"Online", L"friends rooms multiplayer"},
        {HomeProfile, L"Customize profile", L"picture avatar gif name color fighter card"},
        {SpeedrunTab, L"Speedrun", L"timer categories splits"},
        {OpenOverlay, L"Customize speedrun overlay", L"split images png jpeg gif"},
        {ModsTab, L"Mods", L"characters stages import"},
        {SettingsTab, L"Settings", L"display graphics audio theme"},
        {SettingsDisplay,L"Display settings",L"window resolution aspect ratio frame rate fps cap vsync volume audio fullscreen"},
        {SettingsGraphics,L"Graphics settings",L"renderer anti aliasing texture filtering upscaling fsr internal resolution image effect sharpen vibrant colors crt scanlines retro glow bloom brightness gamma"},
        {SettingsGame,L"Game settings",L"cpu difficulty music textures randomizer load times fps counter run ahead input lag latency"},
        {SettingsLauncher,L"Launcher appearance",L"theme accent colors start page discord activity"},
        {OpenColors,L"Theme colors",L"custom colours palette background text panel accent"},
        {ArrangeTabs, L"Arrange tabs", L"dock navigation order hide"},
        {UpdatesTab, L"Updates", L"release patch notes download"},
        {OpenLogs, L"Open logs folder", L"diagnostics startup launcher"},
        {OpenHelp,L"Help & shortcuts",L"guide keyboard getting started support setup"},
        {CopySetup,L"Copy setup summary",L"support settings report clipboard"},
        {OpenIsoFolder,L"Open ISO folder",L"disc location explorer"},
        {OpenNotifications,L"Notifications",L"friend requests invitations online"},
        {OpenAchievements,L"Achievements",L"trophies retroachievements points unlocks hardcore"},
        {OpenWaveHome,L"Wave home",L"home menu ps3 wave bar categories"}
    };
    std::vector<size_t> actionRows;
    std::vector<std::pair<std::string,std::string>> finderPlayers;
    std::optional<Settings> previousSettings;
    static auto preferences(Settings &s) {
        return std::tie(s.renderer, s.fps, s.width, s.height, s.windowMode, s.aspect, s.antialiasing,
            s.textureFilter, s.upscaler, s.internalScale, s.hdTextures, s.music, s.vsync, s.cpuDifficulty,
            s.audio, s.volume, s.fpsCounter, s.randomizer, s.ps2LoadTimes, s.runAhead, s.imageEffect, s.brightness, s.launcherTheme, s.launcherAccent,
            s.launcherColors, s.startPage, s.discordPresence, s.reducedMotion, s.helpHints, s.launcherInterface, s.waveSky, s.waveEmblem);
    }
    static std::wstring searchText(std::wstring value) {
        std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) { return wchar_t(std::towlower(c)); });
        return value;
    }
    static bool matchesSearch(const std::wstring& text,const std::wstring& query) {
        return ui::matchesTerms(text,query);
    }
    void refreshActions() {
        const auto query = searchText(trimmed(textOf(ActionSearch)));
        SendMessageW(controls[ActionList], LB_RESETCONTENT, 0, 0);
        actionRows.clear();
        for (size_t i = 0; i < actions.size(); ++i) {
            const auto haystack = searchText(std::wstring(actions[i].name) + L" " + actions[i].keywords);
            if (!matchesSearch(haystack,query)) continue;
            actionRows.push_back(i);
            SendMessageW(controls[ActionList], LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(actions[i].name));
        }
        finderPlayers.clear();
        if (const auto profile = activeProfile(); profile && !query.empty()) {
            std::vector<std::pair<std::string,std::string>> known;
            if (directory)
                for (const auto &player : directory->players()) known.emplace_back(player.id, player.name);
            for (const auto *list : {&settings.onlineFriends, &settings.recentPlayers, &settings.blockedPlayers})
                for (const auto &entry : *list)
                    if (entry.owner == profile->id) known.emplace_back(entry.id, entry.name);
            const auto lowered = utf8(query);
            for (const auto &[id, name] : known) {
                if (finderPlayers.size() >= 8) break;
                if (id == profile->id || std::any_of(finderPlayers.begin(), finderPlayers.end(), [&](const auto &row) { return row.first == id; })) continue;
                if (!matchesSearch(searchText(wide(name)), query) && lowered != id) continue;
                finderPlayers.emplace_back(id, name);
                actionRows.push_back(actions.size() + finderPlayers.size() - 1);
                const bool online = directory && directory->player(id);
                const auto label = L"Player profile \u00b7 " + wide(name) + (online ? L" (online)" : isFriend(id) ? L" (friend)" : L"");
                SendMessageW(controls[ActionList], LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
            }
            if (finderPlayers.empty() && lowered.size() == 64 && online::valid_id(lowered) && lowered != profile->id) {
                finderPlayers.emplace_back(lowered, std::string());
                actionRows.push_back(actions.size());
                const std::wstring label = L"Player profile \u00b7 ID " + query.substr(0, 12) + L"\u2026";
                SendMessageW(controls[ActionList], LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
            }
        }
        if (!actionRows.empty()) SendMessageW(controls[ActionList], LB_SETCURSEL, 0, 0);
        EnableWindow(controls[ActionOpen], !actionRows.empty());
        (pageDirty=true),InvalidateRect(hwnd, nullptr, FALSE);
    }
    void openActionFinder() {
        if(page==finderPage) {SetFocus(controls[ActionSearch]);SendMessageW(controls[ActionSearch],EM_SETSEL,0,-1);return;}
        if (page != finderPage) { finderReturnPage = page; finderFrom = navigationPage(); }
        page = finderPage;
        SetWindowTextW(controls[ActionSearch], L"");
        refreshActions(); layout(); SetFocus(controls[ActionSearch]);
    }
    void openSelectedAction() {
        const auto row = SendMessageW(controls[ActionList], LB_GETCURSEL, 0, 0);
        if (row < 0 || size_t(row) >= actionRows.size()) return;
        const auto index = actionRows[size_t(row)];
        if (index >= actions.size()) {
            const auto chosen = finderPlayers.at(index - actions.size());
            page = finderReturnPage;
            layout();
            openPlayer(chosen.first, chosen.second);
            return;
        }
        const int id = actions[index].id;
        page = finderReturnPage;
        layout();
        command(id, BN_CLICKED);
    }
    int pageVisibleHeight() const {
        RECT r{};GetClientRect(hwnd,&r);
        return std::max(1,unscale(r.bottom)-shellHeader-dockReserve());
    }
    int pageCanvasHeight() const {
        RECT bounds{}; GetClientRect(hwnd, &bounds);
        const int width = unscale(bounds.right) - contentLeft - margin;
        const int controlsHeight = width < 1180 ? 1120 : 760;
        int settingsHeight = 970;
        if (page == settingsPage && controls.count(SettingsSearch)) {
            ui::SettingsLayout grid(width,contentLeft,waveStyle(),settingsTop());
            grid.filter(searchText(trimmed(textOf(SettingsSearch))));
            settingsHeight=grid.height();
        }
        if (page == wavePage) return pageVisibleHeight();
        return std::max(page == achievementsPage ? achievementsHeight() : page == helpPage ? 940 : page == overlayPage ? (waveStyle() ? 1820 : 880) : page == onlinePage ? (waveStyle() ? 1420 : 740) : page == modsPage && waveStyle() ? 960 : page == speedrunPage && waveStyle() ? 900 :page == colorsPage && waveStyle() ? 680 : page == controlsPage ? controlsHeight :
                        page == settingsPage ? settingsHeight : page == profilePage ? (waveStyle() ? profileWave(width,heldTag().has_value()).actionsBottom+24 : 1420) : page == playerPage ? 820 : page == 0 ? (width<1100 ? 1040 : 740) : page == savesPage ? 780 : 680, pageVisibleHeight());
    }
    void scrollPage(int value) {
        scrollGliding=false;
        glideActive=false;
        RemovePropW(hwnd,L"OpenKai2.Gliding");
        scrollPageNow(value);
    }
    int scrollReported() const {
        if (scrollGliding) SetPropW(hwnd,L"OpenKai2.Gliding",reinterpret_cast<HANDLE>(1));
        else RemovePropW(hwnd,L"OpenKai2.Gliding");
        return scrollGliding ? scrollGoal : pageScroll;
    }
    void glideTo(int value) {
        const int target=std::clamp(value,0,std::max(0,pageCanvasHeight()-pageVisibleHeight()));
        if (!allowMotion() || dropdownOpen() || IsIconic(hwnd)) {
            scrollPage(target);
            return;
        }
        if (!scrollGliding) {
            scrollAt=pageScroll;
            scrollTickAt=std::chrono::steady_clock::now();
        }
        scrollGoal=target;
        scrollGliding=target!=pageScroll || std::abs(scrollAt-target)>=0.5;
        SetPropW(hwnd,L"OpenKai2.ScrollPos",reinterpret_cast<HANDLE>(intptr_t(scrollReported())));
        if (!scrollGliding) return;
        {
            std::lock_guard lock(glideMutex);
            glideActive=true;
        }
        if (!glideThread.joinable()) glideThread=std::thread([this]{glideLoop();});
        glideWake.notify_one();
    }
    void glideStep() {
        glideTickPending=false;
        if (!scrollGliding) {glideActive=false;return;}
        if (dropdownOpen()) {scrollPage(scrollGoal);return;}
        const auto now=std::chrono::steady_clock::now();
        const double elapsed=std::clamp(std::chrono::duration<double>(now-scrollTickAt).count(),0.0,0.1);
        scrollTickAt=now;
        scrollAt+=(scrollGoal-scrollAt)*(1.0-std::exp(-elapsed/0.055));
        const bool settled=std::abs(scrollGoal-scrollAt)<0.5;
        if (settled) {scrollAt=scrollGoal;scrollGliding=false;glideActive=false;}
        scrollPageNow(int(std::lround(scrollAt)));
        if (settled) RedrawWindow(hwnd,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_UPDATENOW);
        if (pageScroll!=int(std::lround(scrollAt))) {scrollGliding=false;glideActive=false;}
        SetPropW(hwnd,L"OpenKai2.ScrollPos",reinterpret_cast<HANDLE>(intptr_t(scrollReported())));
    }
    unsigned fastTimers=0;
    bool fineClock=false;
    void updateClockPrecision() {
        const bool want=fastTimers!=0;
        if(want==fineClock)return;
        fineClock=want;
        if(want)timeBeginPeriod(1);else timeEndPeriod(1);
    }
    void frames(UINT id) {if(!hwnd)return;SetTimer(hwnd,id,16,nullptr);fastTimers|=1u<<id;updateClockPrecision();}
    void endFrames(UINT id) {if(hwnd)KillTimer(hwnd,id);fastTimers&=~(1u<<id);updateClockPrecision();}
    void glideLoop() {
        using Flush=HRESULT(WINAPI*)();
        const auto module=LoadLibraryW(L"dwmapi.dll");
        const auto flush=module?reinterpret_cast<Flush>(reinterpret_cast<void*>(GetProcAddress(module,"DwmFlush"))):nullptr;
        std::unique_lock lock(glideMutex);
        while (!glideStop) {
            glideWake.wait(lock,[this]{return glideStop || glideActive;});
            if (glideStop) break;
            lock.unlock();
            const auto began=std::chrono::steady_clock::now();
            if (!flush || FAILED(flush()) || std::chrono::steady_clock::now()-began<std::chrono::microseconds(500))
                std::this_thread::sleep_until(began+std::chrono::microseconds(4000));
            if (glideActive && !glideTickPending.exchange(true)) PostMessageW(hwnd,glideMessage,0,0);
            lock.lock();
        }
        lock.unlock();
        if (module) FreeLibrary(module);
    }
    void stopGlideThread() {
        {
            std::lock_guard lock(glideMutex);
            glideStop=true;
        }
        glideWake.notify_one();
        if (glideThread.joinable()) glideThread.join();
    }
    void scrollPageNow(int value) {
        if (dropdownOpen())
            return;
        const int next=std::clamp(value,0,pageCanvasHeight()-pageVisibleHeight());
        if(next==pageScroll)return;
        pageScroll=next;
        if(!scrollControls())layout();
        dropStaleHover();
        RedrawWindow(hwnd,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW|RDW_ALLCHILDREN);
    }
    void dropStaleHover() {
        POINT cursor{};
        if (!GetCursorPos(&cursor)) return;
        const auto under=[&](HWND control){RECT box{};GetWindowRect(control,&box);return PtInRect(&box,cursor)!=FALSE;};
        if (hotButton) {
            const auto found=controls.find(hotButton);
            if (found==controls.end() || !under(found->second)) hotButton=0;
        }
        for (const auto& [id,control] : controls)
            if (GetPropW(control,L"OpenKai2.Hot") && !under(control)) RemovePropW(control,L"OpenKai2.Hot");
    }
    void focusControl(int id) {
        if(dropdownOpen())suspendUi();
        const auto control=controls.at(id);
        RECT bounds{};GetWindowRect(control,&bounds);
        MapWindowPoints(nullptr,hwnd,reinterpret_cast<POINT*>(&bounds),2);
        if(bounds.top<p(shellHeader))scrollPage(pageScroll+unscale(bounds.top)-shellHeader-12);
        else if(bounds.bottom>p(shellHeader+pageVisibleHeight()))
            scrollPage(pageScroll+unscale(bounds.bottom)-shellHeader-pageVisibleHeight()+12);
        SetFocus(control);
    }
    bool routeWheelToControl(WPARAM keys, LPARAM position) {
        const POINT screen{GET_X_LPARAM(position), GET_Y_LPARAM(position)};
        POINT client=screen;ScreenToClient(hwnd,&client);
        if(client.y<p(shellHeader) || client.y>=p(shellHeader+pageVisibleHeight()))return false;
        for(auto child=WindowFromPoint(screen);child && child!=hwnd && IsChild(hwnd,child);child=GetParent(child)) {
            wchar_t type[32]{};GetClassNameW(child,type,32);
            const auto style=GetWindowLongPtrW(child,GWL_STYLE);
            const bool list=_wcsicmp(type,L"LISTBOX")==0;
            const bool multiline=_wcsicmp(type,L"EDIT")==0 && (style&ES_MULTILINE);
            bool overflowing=false;
            if(list) {
                RECT area{};GetClientRect(child,&area);
                const auto count=SendMessageW(child,LB_GETCOUNT,0,0),row=SendMessageW(child,LB_GETITEMHEIGHT,0,0);
                overflowing=count>0 && row>0 && count*row>area.bottom-area.top;
            } else if(multiline) {
                SCROLLINFO info{sizeof(info),SIF_RANGE|SIF_PAGE};
                overflowing=GetScrollInfo(child,SB_VERT,&info) && info.nMax-info.nMin+1>int(info.nPage);
            }
            if((list || multiline) && overflowing && (style&WS_VSCROLL) && IsWindowEnabled(child)) {
                forwardingWheel=true;
                SendMessageW(child,WM_MOUSEWHEEL,keys,position);
                forwardingWheel=false;
                return true;
            }
        }
        return false;
    }
    int p(int n) const { return MulDiv(n, int(dpi), 96); }
    int unscale(int n) const { return MulDiv(n, 96, int(dpi)); }
    int navigationPage() const {
        return page == wavePage || page == achievementsPage ? 0 : page == helpPage ? helpOrigin : page == notificationsPage ? onlinePage : page == finderPage ? finderFrom : page == savesPage || page == controlsPage ? 0 :
               page == profilePage ? profileReturnPage : page == playerPage ? playerNavigation : page == importPage ? modsPage : page == overlayPage ? speedrunPage : page == tabsPage || page == colorsPage ? settingsPage : page;
    }
    std::vector<int> visibleTabs() const {
        std::vector<int> shown;
        for (unsigned tab : settings.tabOrder)
            if (dock_tab(tab) && !((settings.hiddenTabs >> tab) & 1u))
                shown.push_back(int(tab));
        return shown;
    }
    int tabSlot(int tab) const {
        const auto shown = visibleTabs();
        const auto found = std::find(shown.begin(), shown.end(), tab);
        return found == shown.end() ? -1 : int(found - shown.begin());
    }
    double pinnedClock=-1.0;
    ULONGLONG pinnedTicks=0;
    ULONGLONG imageTicks() const {return pinnedTicks ? pinnedTicks : motionTicks();}
    double profileClock() const {
        if(pinnedClock>=0.0)return pinnedClock;
        const double now=double(motionTicks());
        return settings.reducedMotion ? now*0.3 : now;
    }
    bool allowMotion() const {
        if(settings.reducedMotion)return false;
        BOOL enabled=TRUE;
        return !SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&enabled,0) || enabled;
    }
    void updateDockMotion() {
        const int target=tabSlot(navigationPage());
        if(target<0)return;
        if(dockTarget<0 || !allowMotion() || IsIconic(hwnd)) {
            dockPosition=double(target);dockTarget=target;dockStarted=0;endFrames(4);return;
        }
        if(target!=dockTarget) {
            dockFrom=dockPosition;dockTarget=target;dockStarted=motionTicks();frames(4);
        }
    }
    void animateDock() {
        if(!dockStarted)return;
        const double t=std::min(1.0,double(motionTicks()-dockStarted)/240.0);
        const double eased=1.0-std::pow(1.0-t,3.0);
        dockPosition=dockFrom+(double(dockTarget)-dockFrom)*eased;
        if(t>=1.0 || !allowMotion() || IsIconic(hwnd)) {
            dockPosition=double(dockTarget);dockStarted=0;endFrames(4);
        }
        if(dropdownOpen() || IsIconic(hwnd))return;
        RECT r{};GetClientRect(hwnd,&r);r.top=std::max(0L,r.bottom-p(108));r.bottom-=p(30);
        (pageDirty=true),RedrawWindow(hwnd,&r,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN);
    }
    struct WaveItem { int action; const wchar_t* title; const wchar_t* detail; int icon; bool opensPage; };
    struct WaveCategory { int icon; const wchar_t* name; std::vector<WaveItem> items; };
    std::vector<WaveCategory> waveCategories() const {
        auto all=waveCategoryList();
        std::vector<WaveCategory> ordered;
        for(const int tab:visibleTabs())
            for(auto& category:all)
                if(category.icon==PlayTab+tab && !category.items.empty()){ordered.push_back(std::move(category));category.items.clear();}
        for(const int keep:{PlayTab,SettingsTab})
            if(std::none_of(ordered.begin(),ordered.end(),[&](const WaveCategory& c){return c.icon==keep;}))
                for(auto& category:all)if(category.icon==keep && !category.items.empty()){ordered.insert(keep==PlayTab ? ordered.begin() : ordered.end(),std::move(category));category.items.clear();}
        return ordered;
    }
    std::vector<WaveCategory> waveCategoryList() const {
        return {
            {PlayTab,L"Game",{{Play,L"Play Budokai 2",L"Start the game from your USA disc.",PlayTab,false},
                              {LoadState,L"Load state",L"Continue from your last save state.",PlayTab,false},
                              {HomeSaves,L"Saves & backups",L"Memory card, imports and recovery copies.",SavesTab,true},
                              {HomeControls,L"Controls",L"Keyboard and controller bindings.",ControlsTab,true},
                              {OpenAchievements,L"Achievements",L"Built-in trophies and RetroAchievements.",OpenAchievements,true},
                              {ChooseIso,L"Choose ISO",L"Pick the Budokai 2 disc image.",UpdatesTab,false},
                              {VerifyIso,L"Verify ISO",L"Check the disc against the USA retail release.",FindAction,false}}},
            {OnlineTab,L"Network",{{OnlineTab,L"Online lobby",L"Rooms, players and friends.",OnlineTab,true},
                                   {HomeProfile,L"Profile",L"Picture, name color, effects and player card.",HomeProfile,true},
                                   {OpenNotifications,L"Notifications",L"Friend requests and match invitations.",OpenNotifications,true}}},
            {SpeedrunTab,L"Speedrun",{{SpeedrunTab,L"Speedrun mode",L"Categories, splits and verified timing.",SpeedrunTab,true},
                                      {OpenOverlay,L"Overlay",L"Split names, images and layout.",SpeedrunTab,true}}},
            {ModsTab,L"Mods",{{ModsTab,L"Mod manager",L"Load order and what each mod adds.",ModsTab,true},
                              {OpenImporter,L"Import tool",L"Add characters and stages from another disc.",ModsTab,true}}},
            {SettingsTab,L"Settings",{{SettingsDisplay,L"Display",L"Window, resolution, frame rate and sound.",SettingsTab,true},
                                      {SettingsGraphics,L"Graphics",L"Renderer, upscaling and image effects.",SettingsTab,true},
                                      {SettingsGame,L"Game",L"Difficulty, textures and music.",SettingsTab,true},
                                      {SettingsLauncher,L"Launcher",L"Theme, accent and interface style.",SettingsTab,true},
                                      {ArrangeTabs,L"Arrange tabs",L"Order and hide home categories.",SettingsTab,true},
                                      {OpenHelp,L"Help & shortcuts",L"Controls, setup and support tools.",OpenHelp,true}}},
            {UpdatesTab,L"Updates",{{UpdatesTab,L"System update",L"Channel, checks and installs.",UpdatesTab,true},
                                    {ReleaseNotes,L"Release notes",L"Open this version's notes in your browser.",UpdatesTab,false},
                                    {OpenLogs,L"Open logs folder",L"Startup and launcher logs for support.",FindAction,false}}}};
    }
    int waveCategory=0;
    std::array<int,6> waveItem{};
    double waveCategoryAt=0.0,waveEnter=0.0;
    std::array<double,6> waveItemAt{};
    ULONGLONG waveTick=0,waveFlashAt=0;
    bool waveTicking=false;
    UINT waveInterval=16;
    bool waveStyle() const {return settings.launcherInterface==1;}
    static constexpr const wchar_t* pageTitles[]={L"Play",L"Online",L"Speedrun",L"Mods",L"Saves & backups",L"Settings",
        L"Updates",L"Controls",L"Import tool",L"Speedrun overlay",L"Arrange tabs",L"Theme colors",L"Profile",L"Find an action",L"Notifications",L"Help & shortcuts",L"Player profile",L"Home",L"Achievements"};
    static const wchar_t* pageTitle(int index) {return index>=0 && index<int(std::size(pageTitles)) ? pageTitles[index] : L"";}
    std::wstring clockText(const SYSTEMTIME& now) const {
        if(settings.launcherClock==3)return {};
        wchar_t clock[40]{};
        const wchar_t* format=settings.launcherClock==1 ? L"h:mm tt" : settings.launcherClock==2 ? L"HH:mm" : nullptr;
        GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT,format ? 0 : TIME_NOSECONDS,&now,format,clock,40);
        return clock;
    }
    std::wstring dateClock(const SYSTEMTIME& now) const {
        wchar_t date[40]{};
        GetDateFormatEx(LOCALE_NAME_USER_DEFAULT,0,&now,L"d/M",date,40,nullptr);
        const auto time=clockText(now);
        return time.empty() ? std::wstring(date) : std::wstring(date)+L"   "+time;
    }
    int settingsTop() const {return waveStyle() ? 172 : ui::SettingsLayout::top;}
    std::wstring waveTrail;
    int waveTrailPage=-1;
    bool waveTrailPending=false;
    bool waveRedundant(int id) const {
        if(!waveStyle())return false;
        switch(id) {
        case BackToPlay:case CloseImporter:case CloseOverlay:case CloseTabs:return true;
        case NavigateBack:return page!=playerPage;
        case HomeProfile:return page==onlinePage;
        case OpenOverlay:return page==speedrunPage;
        case OpenImporter:return page==modsPage;
        case ArrangeTabs:return page==settingsPage;
        case SettingsSearch:case ClearSettingsSearch:case SettingsAll:case SettingsDisplay:case SettingsGraphics:case SettingsGame:case SettingsLauncher:return page==settingsPage;
        default:return false;
        }
    }
    void waveBack() {
        if(page==playerPage)command(NavigateBack,BN_CLICKED);
        else if(page==colorsPage)command(CloseColors,BN_CLICKED);
        else openWaveHome();
    }
    int64_t restoredBefore=0;
    bool deletedBudokai=false;
    struct RetroReply { int kind=0; retro::LoginResult login; retro::Lookup lookup; retro::SetInfo set; std::string hash; };
    achievements::Progress achievementProgress;
    retro::SetInfo retroSet;
    std::string retroHash;
    std::wstring retroStatus;
    unsigned retroGame=0;
    bool retroBusy=false,retroChecked=false,retroLookupFailed=false;
    static constexpr int achievementRow=58,achievementsTop=464;
    int retroTop() const {return achievementsTop+64+int(achievements::definition_count)*achievementRow+16;}
    int achievementsHeight() const {
        const size_t extra=retroSet.entries.size();
        return extra ? retroTop()+64+int(extra)*achievementRow+40 : retroTop()+20;
    }
    void openAchievements() {
        if(page!=achievementsPage)page=achievementsPage;
        achievementProgress=achievements::load(achievements::progress_path());
        if(const auto cached=retro::load_cache(retro::cache_path());cached.ok && retroSet.entries.empty())retroSet=cached;
        SetWindowTextW(controls[RaUser],wide(settings.raUser).c_str());
        SendMessageW(controls[RaHardcore],CB_SETCURSEL,settings.raHardcore ? 1 : 0,0);
        layout();refreshEnabled();
        if(!retroChecked)checkRetro();
        (pageDirty=true),InvalidateRect(hwnd,nullptr,FALSE);
    }
    void checkRetro() {
        if(retroBusy)return;
        retroBusy=true;retroStatus=L"Checking RetroAchievements\u2026";
        const auto iso=settings.iso;const auto user=settings.raUser,token=settings.raToken;const HWND target=hwnd;
        std::thread([iso,user,token,target] {
            auto* reply=new RetroReply;reply->kind=1;
            std::error_code missing;
            reply->hash=!iso.empty() && std::filesystem::is_regular_file(iso,missing) ? retro::disc_hash(iso) : std::string();
            if(!reply->hash.empty()) {
                reply->lookup=retro::resolve(reply->hash);
                if(reply->lookup.ok && reply->lookup.game && !token.empty())reply->set=retro::fetch_set(user,token,reply->lookup.game);
            }
            if(!PostMessageW(target,retroMessage,0,reinterpret_cast<LPARAM>(reply)))delete reply;
        }).detach();
        if(page==achievementsPage)(pageDirty=true),InvalidateRect(hwnd,nullptr,FALSE);
    }
    void signInRetro() {
        const auto user=utf8(trimmed(textOf(RaUser))),password=utf8(textOf(RaPassword));
        if(user.empty() || password.empty()) {
            status=L"Type your RetroAchievements username and password.";error=true;
            focusControl(user.empty() ? RaUser : RaPassword);(pageDirty=true),InvalidateRect(hwnd,nullptr,FALSE);return;
        }
        if(retroBusy)return;
        retroBusy=true;retroStatus=L"Signing in\u2026";
        SetWindowTextW(controls[RaPassword],L"");
        const HWND target=hwnd;
        std::thread([user,password,target] {
            auto* reply=new RetroReply;reply->kind=2;
            reply->login=retro::login(user,password);
            if(!PostMessageW(target,retroMessage,0,reinterpret_cast<LPARAM>(reply)))delete reply;
        }).detach();
        refreshEnabled();(pageDirty=true),InvalidateRect(hwnd,nullptr,FALSE);
    }
    void retroArrived(RetroReply* raw) {
        std::unique_ptr<RetroReply> reply(raw);
        retroBusy=false;
        if(reply->kind==2) {
            if(reply->login.ok) {
                settings.raUser=reply->login.user;settings.raToken=reply->login.token;
                saveAchievementState();
                SetWindowTextW(controls[RaUser],wide(settings.raUser).c_str());
                status=L"Signed in to RetroAchievements as "+wide(settings.raUser)+L".";error=false;
                retroChecked=false;checkRetro();
            } else {
                retroStatus=wide(reply->login.error);status=retroStatus;error=true;
            }
        } else {
            retroChecked=true;retroHash=reply->hash;retroLookupFailed=false;
            if(retroHash.empty())retroStatus=L"Verify your Budokai 2 ISO on Play to check RetroAchievements.";
            else if(!reply->lookup.ok){retroStatus=wide(reply->lookup.error);retroLookupFailed=true;}
            else {
                retroGame=reply->lookup.game;
                if(!retroGame)retroStatus=L"This disc is not linked on RetroAchievements yet. Built-in achievements work now; the set loads once RetroAchievements links this disc's hash.";
                else if(settings.raToken.empty())retroStatus=L"This disc is linked on RetroAchievements. Sign in to load its achievements.";
                else if(reply->set.ok) {
                    const auto cached=retro::load_cache(retro::cache_path());
                    for(auto& entry:reply->set.entries)
                        for(const auto& known:cached.entries)
                            if(known.id==entry.id){entry.unlocked=known.unlocked;entry.hardcore=known.hardcore;entry.when=known.when;}
                    reply->set.hash=retroHash;
                    retroSet=std::move(reply->set);
                    retroStatus=wide(retroSet.title)+L": "+std::to_wstring(retroSet.entries.size())+L" achievements, "+std::to_wstring(retroSet.earned())+L" of "+std::to_wstring(retroSet.points())+L" points earned.";
                } else retroStatus=wide(reply->set.error);
            }
        }
        if(page==achievementsPage){layout();refreshEnabled();(pageDirty=true),InvalidateRect(hwnd,nullptr,FALSE);}
    }
    void saveAchievementState() {
        try{write_settings(settings_path(),settings);}
        catch(const std::exception& problem){std::cout<<"[launcher:achievements] "<<problem.what()<<std::endl;}
        if(page==achievementsPage)(pageDirty=true),InvalidateRect(hwnd,nullptr,FALSE);
    }
    ULONGLONG pageEnterAt=0,transitionAt=0;
    int shownPage=-1,transitionDirection=1;
    bool navigatingBack=false;
    std::vector<uint32_t> transitionPixels;
    int transitionWidth=0,transitionHeight=0;
    double transitionProgress() const {
        if(!transitionAt)return 1.0;
        return std::clamp(double(motionTicks()-transitionAt)/260.0,0.0,1.0);
    }
    void captureTransition(const RECT& client) {
        if(!allowMotion() || !buffer.frame.pixels || buffer.frame.width!=client.right || buffer.frame.height<client.bottom || client.bottom<=0) {
            transitionAt=0;
            if(controlsDeferred && hwnd)frames(9);
            return;
        }
        const size_t count=size_t(client.right)*size_t(client.bottom);
        GdiFlush();
        transitionPixels.assign(buffer.frame.pixels,buffer.frame.pixels+count);
        transitionWidth=client.right;transitionHeight=client.bottom;
        transitionAt=motionTicks();
        controlsDeferred=controlsDeferred || (waveStyle() && page!=wavePage && !dropdownOpen());
        if(controlsDeferred)for(const auto& [id,control]:controls)
            if(pageControlId(id) && IsWindowVisible(control))ShowWindow(control,SW_HIDE);
        if(hwnd)frames(9);
    }
    bool controlsDeferred=false;
    static bool pageControlId(int id) {return !(id>=PlayTab && id<=ControlsTab) && id!=FindAction && id!=OpenNotifications;}
    void revealControls() {
        revealPosted=false;
        if(!controlsDeferred)return;
        controlsDeferred=false;
        if(page!=wavePage && !dropdownOpen())layout();
    }
    bool revealPosted=false;
    void blendTransition() {
        const double t=transitionProgress();
        if(controlsDeferred && t>=0.6 && hwnd && !revealPosted){revealPosted=true;PostMessageW(hwnd,revealMessage,0,0);}
        if(t>=1.0 || transitionPixels.empty()){transitionAt=0;transitionPixels.clear();return;}
        const double eased=1.0-std::pow(1.0-t,3.0);
        const uint32_t alpha=uint32_t((1.0-eased)*235.0),keep=256u-alpha;
        const int shift=int(std::lround(double(transitionDirection)*eased*double(p(70))));
        GdiFlush();
        const int width=std::min(transitionWidth,buffer.frame.width),height=std::min(transitionHeight,buffer.frame.height);
        for(int y=0;y<height;++y) {
            uint32_t* row=buffer.frame.pixels+size_t(y)*size_t(buffer.frame.width);
            const uint32_t* old=transitionPixels.data()+size_t(y)*size_t(transitionWidth);
            const int from=std::max(0,shift),to=std::min(width,transitionWidth+shift);
            for(int x=from;x<to;++x) {
                const uint32_t a=old[x-shift],b=row[x];
                const uint32_t rb=(((a&0xff00ffu)*alpha+(b&0xff00ffu)*keep)>>8)&0xff00ffu;
                const uint32_t g=(((a&0x00ff00u)*alpha+(b&0x00ff00u)*keep)>>8)&0x00ff00u;
                row[x]=rb|g;
            }
        }
    }
    void animateTransition() {
        if(transitionProgress()>=0.6)revealControls();
        if(transitionProgress()>=1.0){endFrames(9);transitionAt=0;transitionPixels.clear();}
        if(IsIconic(hwnd) || dropdownOpen())return;
        InvalidateRect(hwnd,nullptr,FALSE);
    }
    double pageEnter(double delay=0.0) const {
        if(!pageEnterAt)return 1.0;
        const double t=std::clamp((double(motionTicks()-pageEnterAt)-delay)/340.0,0.0,1.0);
        return 1.0-std::pow(1.0-t,3.0);
    }
    void animatePageEnter() {
        const bool done=!pageEnterAt || motionTicks()-pageEnterAt>=460 || page==wavePage || !waveStyle();
        if(done){pageEnterAt=0;endFrames(8);}
        if(IsIconic(hwnd) || dropdownOpen())return;
        (pageDirty=true),InvalidateRect(hwnd,nullptr,FALSE);
    }
    int dockReserve() const {return waveStyle() ? waveDockReserve : shellDockReserve;}
    void startWave() {
        if(!waveTicking && hwnd){waveTicking=true;waveTick=motionTicks();waveInterval=16;SetTimer(hwnd,6,16,nullptr);}
    }
    void openWaveHome() {
        if(page==wavePage)return;
        navigatingBack=true;
        page=wavePage;waveEnter=0.0;
        layout();SetFocus(hwnd);startWave();
        (pageDirty=true),InvalidateRect(hwnd,nullptr,FALSE);
    }
    void animateWave() {
        if(page!=wavePage){KillTimer(hwnd,6);endFrames(6);waveTicking=false;return;}
        const ULONGLONG now=motionTicks();
        const double dt=std::clamp(double(now-waveTick)/1000.0,0.0,0.1);waveTick=now;
        const double ease=1.0-std::exp(-dt*(allowMotion() ? 14.0 : 40.0));
        waveCategoryAt+=(double(waveCategory)-waveCategoryAt)*ease;
        for(size_t i=0;i<waveItemAt.size();++i)waveItemAt[i]+=(double(waveItem[i])-waveItemAt[i])*ease;
        waveEnter=std::min(1.0,waveEnter+dt*(allowMotion() ? 1.8 : 10.0));
        if(waveFlashAt && now-waveFlashAt>=170){waveFlashAt=0;openWaveItem();if(page!=wavePage)return;}
        bool moving=std::fabs(waveCategoryAt-waveCategory)>0.002 || waveEnter<1.0 || waveFlashAt;
        for(size_t i=0;i<waveItemAt.size();++i)moving=moving || std::fabs(waveItemAt[i]-double(waveItem[i]))>0.002;
        if(moving && allowMotion()) {
            if(waveInterval){waveInterval=0;KillTimer(hwnd,6);frames(6);}
        } else if(const UINT wanted=appActive && allowMotion() ? 16u : 50u;wanted!=waveInterval){
            waveInterval=wanted;
            if(wanted==16u)frames(6);
            else{endFrames(6);SetTimer(hwnd,6,wanted,nullptr);}
        }
        if(IsIconic(hwnd) || dropdownOpen())return;
        InvalidateRect(hwnd,nullptr,FALSE);
    }
    void waveMove(int dx,int dy) {
        const auto categories=waveCategories();
        if(dx)waveCategory=std::clamp(waveCategory+dx,0,int(categories.size())-1);
        if(dy) {
            auto& item=waveItem[size_t(waveCategory)];
            item=std::clamp(item+dy,0,int(categories[size_t(waveCategory)].items.size())-1);
        }
        startWave();
    }
    void waveActivate() {
        if(waveFlashAt)return;
        if(!allowMotion()){openWaveItem();return;}
        waveFlashAt=motionTicks();startWave();
    }
    void openWaveItem() {
        const auto categories=waveCategories();
        const auto& category=categories[size_t(waveCategory)];
        const auto& item=category.items[size_t(std::clamp(waveItem[size_t(waveCategory)],0,int(category.items.size())-1))];
        waveTrail=std::wstring(category.name)+L"  \u203a  "+item.title;waveTrailPending=true;
        if(item.opensPage) {
            page=0;
            if(item.action==SettingsDisplay || item.action==SettingsGraphics || item.action==SettingsGame || item.action==SettingsLauncher)command(SettingsTab,BN_CLICKED);
            command(item.action,BN_CLICKED);
        } else command(item.action,BN_CLICKED);
        waveTrailPage=page;waveTrailPending=false;
        (pageDirty=true),InvalidateRect(hwnd,nullptr,FALSE);
    }
    struct WaveGeometry {double rowY,baseX,spacing,itemGap,itemTop;
        double itemY(double offset,double above) const {
            if(offset>=0)return itemTop+offset*itemGap;
            if(offset>=-1)return itemTop+(above-itemTop)*(-offset);
            return above+(offset+1)*itemGap*0.85;
        }
    };
    WaveGeometry waveGeometry(int width,int height) const {
        return {height*0.29,width*0.27,double(p(156)),double(p(70)),height*0.29+double(p(96))};
    }
    bool waveClick(int x,int y) {
        RECT client{};GetClientRect(hwnd,&client);
        const auto g=waveGeometry(client.right,client.bottom);
        const auto categories=waveCategories();
        if(std::fabs(y-g.rowY)<p(56)) {
            for(int i=0;i<int(categories.size());++i) {
                const double cx=g.baseX+(i-waveCategoryAt)*g.spacing;
                if(std::fabs(x-cx)<g.spacing/2){if(i==waveCategory)waveActivate();else{waveCategory=i;startWave();}return true;}
            }
        }
        const auto& items=categories[size_t(waveCategory)].items;
        const double at=waveItemAt[size_t(waveCategory)];
        for(int j=0;j<int(items.size());++j) {
            const double offset=j-at;
            const double cy=g.itemY(offset,g.rowY-p(66));
            if(std::fabs(y-cy)<g.itemGap/2 && x>g.baseX-p(60) && x<client.right-p(40)) {
                if(j==waveItem[size_t(waveCategory)])waveActivate();else{waveItem[size_t(waveCategory)]=j;startWave();}
                return true;
            }
        }
        return false;
    }
    std::vector<uint32_t> waveBackdrop;
    int waveBackdropWidth=0,waveBackdropHeight=0;
    unsigned waveBackdropTone=0;
    unsigned waveSkyChoice() const {
        if(settings.waveSky)return settings.waveSky;
        SYSTEMTIME now{};GetLocalTime(&now);
        return now.wHour>=6 && now.wHour<10 ? 6u : now.wHour>=10 && now.wHour<17 ? 4u : now.wHour>=17 && now.wHour<21 ? 1u : 5u;
    }
    std::array<COLORREF,3> waveSkyColors() const {
        switch(waveSkyChoice()) {
        case 2:return {RGB(6,22,26),RGB(38,104,78),RGB(150,196,92)};
        case 3:return {RGB(24,4,10),RGB(116,20,32),RGB(236,92,52)};
        case 4:return {RGB(16,40,96),RGB(70,128,206),RGB(198,224,248)};
        case 5:return {RGB(8,6,20),RGB(58,30,108),RGB(146,90,196)};
        case 6:return {RGB(4,10,30),RGB(20,70,150),RGB(92,178,236)};
        case 7:return {RGB(70,74,86),RGB(170,176,190),RGB(236,238,244)};
        case 8:return {RGB(20,4,6),RGB(120,24,20),RGB(230,110,60)};
        case 9:return {RGB(14,20,48),RGB(40,120,140),RGB(250,170,90)};
        case 10:return {RGB(30,6,26),RGB(150,50,120),RGB(255,170,210)};
        case 11:return {RGB(1,12,26),RGB(3,22,42),RGB(8,36,64)};
        default:return {RGB(10,12,34),mix(RGB(70,30,78),orange,70),mix(orange,RGB(150,52,12),96)};
        }
    }
    COLORREF waveSkyTop() const {return waveSkyColors()[0];}
    COLORREF waveSkyMid() const {return waveSkyColors()[1];}
    COLORREF waveSkyLow() const {return waveSkyColors()[2];}
    unsigned waveBackdropKey() const {return (waveSkyChoice()*4u+(settings.waveEmblem ? 1u : 0u)+unsigned(orange)*32u+galaxyTemplate()*977u)^(unsigned(ballColor)*2654435761u)^(unsigned(ballStars)*40503u);}
    void buildWaveBackdrop(HDC dc,int width,int height) {
        Gdiplus::Graphics graphics(dc);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        const int split=int(height*0.58);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeNone);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        Gdiplus::LinearGradientBrush upper(Gdiplus::Point(0,-2),Gdiplus::Point(0,split+3),drawingColor(waveSkyTop()),drawingColor(waveSkyMid()));
        upper.SetWrapMode(Gdiplus::WrapModeTileFlipXY);
        graphics.FillRectangle(&upper,0,0,width,split+1);
        Gdiplus::LinearGradientBrush lower(Gdiplus::Point(0,split-2),Gdiplus::Point(0,height+3),drawingColor(waveSkyMid()),drawingColor(waveSkyLow()));
        lower.SetWrapMode(Gdiplus::WrapModeTileFlipXY);
        graphics.FillRectangle(&lower,0,split,width,height-split+1);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeDefault);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        int edges=1;
        if(galaxyTemplate()==2u) {
            Gdiplus::GraphicsPath sun;
            sun.AddEllipse(Gdiplus::REAL(width*0.30),Gdiplus::REAL(height*0.42),Gdiplus::REAL(width*0.9),Gdiplus::REAL(height*0.9));
            Gdiplus::PathGradientBrush sunGlow(&sun);
            sunGlow.SetCenterColor(Gdiplus::Color(90,255,196,110));
            Gdiplus::Color sunEdge(0,255,150,60);
            sunGlow.SetSurroundColors(&sunEdge,&edges);
            graphics.FillPath(&sunGlow,&sun);
        }
        for(int star=0;star<90;++star) {
            const double a=std::sin(star*12.9898)*43758.5453,b=std::sin(star*78.233)*24634.6345;
            const double fx=a-std::floor(a),fy=b-std::floor(b);
            const double size=0.6+1.2*fx*fy*double(p(1));
            Gdiplus::SolidBrush light(Gdiplus::Color(BYTE(60+120*fx*(1.0-fy)),255,244,230));
            graphics.FillEllipse(&light,Gdiplus::REAL(width*fx),Gdiplus::REAL(height*fy*0.5),Gdiplus::REAL(size),Gdiplus::REAL(size));
        }
        if(!settings.waveEmblem)return;
        const double ballR=std::min(width,height)*0.30,ballX=width*0.84,ballY=height*0.80;
        Gdiplus::GraphicsPath ball;
        ball.AddEllipse(Gdiplus::REAL(ballX-ballR),Gdiplus::REAL(ballY-ballR),Gdiplus::REAL(ballR*2),Gdiplus::REAL(ballR*2));
        Gdiplus::PathGradientBrush ballFill(&ball);
        ballFill.SetCenterPoint(Gdiplus::PointF(Gdiplus::REAL(ballX-ballR*0.38),Gdiplus::REAL(ballY-ballR*0.42)));
        const COLORREF ballLight=mix(ballColor,RGB(255,240,190),150);
        ballFill.SetCenterColor(Gdiplus::Color(70,GetRValue(ballLight),GetGValue(ballLight),GetBValue(ballLight)));
        Gdiplus::Color ballEdge(26,GetRValue(ballColor),GetGValue(ballColor),GetBValue(ballColor));
        ballFill.SetSurroundColors(&ballEdge,&edges);
        graphics.FillPath(&ballFill,&ball);
        Gdiplus::Pen rim(Gdiplus::Color(40,255,210,140),float(std::max(1,p(2))));
        graphics.DrawEllipse(&rim,Gdiplus::REAL(ballX-ballR),Gdiplus::REAL(ballY-ballR),Gdiplus::REAL(ballR*2),Gdiplus::REAL(ballR*2));
        for(int star=0;star<4;++star) {
            const double sx=ballX+std::cos(star*1.5708+0.78)*ballR*0.30,sy=ballY+std::sin(star*1.5708+0.78)*ballR*0.30,sr=ballR*0.12;
            Gdiplus::PointF points[10];
            for(int k=0;k<10;++k){const double angle=-1.5708+k*0.6283,r=k%2 ? sr*0.42 : sr;points[k]={Gdiplus::REAL(sx+std::cos(angle)*r),Gdiplus::REAL(sy+std::sin(angle)*r)};}
            Gdiplus::SolidBrush starBrush(Gdiplus::Color(70,GetRValue(ballStars),GetGValue(ballStars),GetBValue(ballStars)));
            graphics.FillPolygon(&starBrush,points,10);
        }
        Gdiplus::GraphicsPath shine;
        shine.AddEllipse(Gdiplus::REAL(ballX-ballR*0.62),Gdiplus::REAL(ballY-ballR*0.78),Gdiplus::REAL(ballR*0.55),Gdiplus::REAL(ballR*0.32));
        Gdiplus::PathGradientBrush shineFill(&shine);
        shineFill.SetCenterColor(Gdiplus::Color(60,255,255,240));
        Gdiplus::Color shineEdge(0,255,255,240);
        shineFill.SetSurroundColors(&shineEdge,&edges);
        graphics.FillPath(&shineFill,&shine);
    }
    void paintWave(HDC dc,int width,int height) {
        const double now=double(allowMotion() ? motionTicks() : 0)/1000.0;
        const auto g=waveGeometry(width,height);
        const COLORREF deep=waveSkyTop(),warm=waveSkyMid();
        const size_t pixels=size_t(width)*size_t(height);
        if(waveBackdropWidth!=width || waveBackdropHeight!=height || waveBackdropTone!=waveBackdropKey() || waveBackdrop.size()!=pixels) {
            buildWaveBackdrop(dc,width,height);
            GdiFlush();
            waveBackdrop.assign(buffer.frame.pixels,buffer.frame.pixels+pixels);
            waveBackdropWidth=width;waveBackdropHeight=height;waveBackdropTone=waveBackdropKey();
        } else if(galaxyTemplate()==2u || galaxyTemplate()==4u) {
            GdiFlush();
            std::copy(waveBackdrop.begin(),waveBackdrop.end(),buffer.frame.pixels);
        }
        GdiFlush();
        if(const unsigned kind=galaxyTemplate();kind!=2u && kind!=4u && waveBackdrop.size()==pixels) {
            const unsigned key=waveBackdropKey();
            if(galaxyFrameWidth!=width || galaxyFrameHeight!=height || galaxyFrameKey!=key || galaxyFrame.size()!=pixels) {
                galaxyFrame.assign(pixels,0u);galaxyFrameWidth=width;galaxyFrameHeight=height;galaxyFrameKey=key;galaxySlice=0;
                if(computeGalaxyLayer(width,height,now))blendGalaxy(waveBackdrop.data(),width,galaxyFrame.data(),width,width,height,0,1);
                else std::copy(waveBackdrop.begin(),waveBackdrop.end(),galaxyFrame.begin());
            } else {
                computeGalaxyLayer(width,height,now,galaxySlice,4);
                blendGalaxy(waveBackdrop.data(),width,galaxyFrame.data(),width,width,height,galaxySlice,4);
                galaxySlice=(galaxySlice+1)%4;
            }
            if(buffer.frame.width==width)std::copy(galaxyFrame.begin(),galaxyFrame.end(),buffer.frame.pixels);
            else for(int y=0;y<height;++y)std::copy(galaxyFrame.begin()+ptrdiff_t(size_t(y)*size_t(width)),galaxyFrame.begin()+ptrdiff_t(size_t(y+1)*size_t(width)),buffer.frame.pixels+size_t(y)*size_t(buffer.frame.width));
        } else paintGalaxy(buffer.frame.pixels,buffer.frame.width,width,height,now,true);
        drawSky(buffer.frame.pixels,buffer.frame.width,buffer.frame.height,0,height,[](int,int){return true;});
        const double enter=1.0-std::pow(1.0-waveEnter,3.0);
        const auto backdropAt=[&](double x,double y){
            const int px=std::clamp(int(x),0,width-1),py=std::clamp(int(y),0,height-1);
            if(waveBackdrop.size()!=pixels)return mix(deep,warm,110);
            const uint32_t value=waveBackdrop[size_t(py)*size_t(width)+size_t(px)];
            return RGB(value>>16&255,value>>8&255,value&255);
        };
        const auto fade=[&](COLORREF color,double amount){return mix(mix(deep,warm,110),color,int(std::clamp(amount,0.0,1.0)*255.0));};
        const auto fadeAt=[&](COLORREF color,double amount,double x,double y){
            const COLORREF under=backdropAt(x,y);
            const int light=(GetRValue(under)*3+GetGValue(under)*6+GetBValue(under))/10;
            const COLORREF base=light>150 ? mix(under,RGB(30,14,10),150) : under;
            return mix(base,light>150 ? RGB(40,18,12) : color,int(std::clamp(light>150 ? 0.35+0.65*amount : amount,0.0,1.0)*255.0));
        };
        SYSTEMTIME clock{};GetLocalTime(&clock);
        const int rightEdge=unscale(width)-48;
        text(dc,dateClock(clock),rightEdge-420,24,420,40,fonts.title,fade(RGB(250,246,236),enter),DT_RIGHT|DT_SINGLELINE|DT_NOPREFIX);
        if(const auto profile=activeProfile()) {
            avatar(dc,p(rightEdge-14),p(78),p(14),wide(profile->name),profile->id,mix(deep,warm,60),directory && directory->connected() ? 1 : 0);
            nameText(dc,wide(profile->name),rightEdge-360,68,320,22,fonts.body,fade(RGB(240,232,214),enter),DT_RIGHT|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX,profile->id);
        }
        buffer.frame.ball(p(48+14),p(44),p(14),{0,0,width,height});
        text(dc,L"OpenKai2",48+38,30,220,30,fonts.brand,fade(RGB(250,246,236),enter*0.9));
        const auto categories=waveCategories();
        const double slide=(1.0-enter)*p(120);
        for(int i=0;i<int(categories.size());++i) {
            const double distance=std::fabs(i-waveCategoryAt);
            const double focus=std::max(0.0,1.0-distance);
            const double cx=g.baseX+(i-waveCategoryAt)*g.spacing+slide*(1+i*0.25);
            if(cx<-g.spacing || cx>width+g.spacing)continue;
            const int size=int(p(42)+p(26)*focus);
            const double edge=std::clamp(std::min(cx+g.spacing*0.5,double(width)-cx)/g.spacing,0.0,1.0);
            const double alpha=enter*(0.5+0.5*focus)*edge;
            if(focus>0.05) {
                const int glow=int(p(46)*focus);
                buffer.frame.halo({LONG(cx),LONG(g.rowY),LONG(cx+1),LONG(g.rowY+1)},0,glow+glow/4,RGB(255,214,140),int(70*focus*enter));
            }
            navigationIcon(dc,categories[size_t(i)].icon,{LONG(cx-size/2),LONG(g.rowY-size/2),LONG(cx+size/2),LONG(g.rowY+size/2)},fade(RGB(255,250,240),alpha),size);
            if(categories[size_t(i)].icon==OnlineTab && hasNotice() && alpha>0.2) {
                const int count=noticeCount(),bx=int(cx+size*0.42),by=int(g.rowY-size*0.42),radius=p(9);
                const double beat=0.5+0.5*std::sin(now*3.0);
                buffer.frame.halo({LONG(bx),LONG(by),LONG(bx+1),LONG(by+1)},0,radius+p(8),orange,int(50+40*beat));
                ellipse(dc,bx,by,radius,orange);
                const std::wstring label=count>9 ? L"9+" : std::to_wstring(count);
                auto previous=SelectObject(dc,fonts.label);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(30,14,6));
                RECT digits{bx-radius,by-radius,bx+radius+1,by+radius+1};
                DrawTextW(dc,label.c_str(),-1,&digits,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
                SelectObject(dc,previous);
            }
            if(focus>0.02)
                text(dc,categories[size_t(i)].name,unscale(int(cx))-100,unscale(int(g.rowY))+40,200,24,fonts.body,fade(RGB(255,248,232),alpha*focus),DT_CENTER|DT_SINGLELINE|DT_NOPREFIX);
        }
        const int shown=std::clamp(int(std::lround(waveCategoryAt)),0,int(categories.size())-1);
        const double columnAlpha=enter*std::max(0.0,1.0-std::fabs(shown-waveCategoryAt)*2.2);
        const auto& items=categories[size_t(shown)].items;
        const double at=waveItemAt[size_t(shown)];
        const double columnX=g.baseX+(shown-waveCategoryAt)*g.spacing+slide*0.6;
        for(int j=0;j<int(items.size());++j) {
            const double offset=j-at;
            const double focus=std::max(0.0,1.0-std::fabs(offset));
            const double cy=g.itemY(offset,g.rowY-p(66))+(offset>=0 ? focus*p(6) : 0.0);
            if(cy<p(96) || cy>height-p(60))continue;
            const double crossing=offset<0 && offset>-1 ? 1.0-std::sin(-offset*3.1416)*0.8 : 1.0;
            const double alpha=columnAlpha*crossing*(offset<-0.5 ? 0.45 : 0.55+0.45*focus)*std::clamp(1.0-(std::fabs(offset)-3.5)*0.5,0.0,1.0);
            if(alpha<0.02)continue;
            const int tile=int(p(40)+p(14)*focus);
            const RECT box{LONG(columnX-tile/2),LONG(cy-tile/2),LONG(columnX+tile/2),LONG(cy+tile/2)};
            if(focus>0.5 && offset>=-0.5) {
                const double flash=waveFlashAt ? std::sin(std::min(1.0,double(motionTicks()-waveFlashAt)/170.0)*3.1416) : 0.0;
                const double pulse=0.5+0.5*std::sin(now*2.4)+flash*2.2;
                Gdiplus::Graphics graphics(dc);
                graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
                const Gdiplus::REAL barLeft=Gdiplus::REAL(columnX-tile/2-p(10)),barTop=Gdiplus::REAL(cy-tile/2-p(8));
                const Gdiplus::REAL barWidth=Gdiplus::REAL(std::min(double(width)*0.62,double(p(560)))),barHeight=Gdiplus::REAL(tile+p(16));
                Gdiplus::LinearGradientBrush bar(Gdiplus::PointF(barLeft,0),Gdiplus::PointF(barLeft+barWidth,0),
                    Gdiplus::Color(BYTE(std::min(255.0,(50+30*pulse)*focus*columnAlpha)),255,226,170),Gdiplus::Color(0,255,226,170));
                graphics.FillRectangle(&bar,barLeft,barTop,barWidth,barHeight);
                Gdiplus::LinearGradientBrush edge(Gdiplus::PointF(barLeft,0),Gdiplus::PointF(barLeft+barWidth,0),
                    Gdiplus::Color(BYTE(std::min(255.0,(110+60*pulse)*focus*columnAlpha)),255,240,210),Gdiplus::Color(0,255,240,210));
                graphics.FillRectangle(&edge,barLeft,barTop,barWidth,Gdiplus::REAL(std::max(1,p(1))));
                graphics.FillRectangle(&edge,barLeft,barTop+barHeight-Gdiplus::REAL(std::max(1,p(1))),barWidth,Gdiplus::REAL(std::max(1,p(1))));
            }
            if(focus>0.5)
                buffer.frame.halo({LONG(columnX),LONG(cy),LONG(columnX+1),LONG(cy+1)},0,int(tile*0.9),RGB(255,226,170),int(60*focus*alpha));
            navigationIcon(dc,items[size_t(j)].icon,{box.left,box.top,box.right,box.bottom},fade(RGB(255,250,242),std::min(1.0,alpha*1.25)),int(tile*(0.62+0.08*focus)));
            const int textX=unscale(int(columnX))+unscale(tile)/2+18;
            const COLORREF titleTone=focus>0.5 ? fade(RGB(255,252,244),alpha) : fadeAt(RGB(255,248,236),alpha,columnX+tile,cy);
            const int textWidth=std::max(160,unscale(width)-textX-48);
            if(focus>0.5) {
                const int titleHeight=fontHeight(dc,fonts.heading),detailHeight=fontHeight(dc,fonts.caption),gap=2;
                const int top=unscale(int(cy))-(titleHeight+gap+detailHeight)/2;
                text(dc,items[size_t(j)].title,textX,top,textWidth,titleHeight+2,fonts.heading,titleTone,DT_LEFT|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
                text(dc,items[size_t(j)].detail,textX,top+titleHeight+gap,textWidth,detailHeight+2,fonts.caption,fade(RGB(240,228,204),alpha*focus),DT_LEFT|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
            } else {
                const int lineHeight=fontHeight(dc,fonts.body);
                text(dc,items[size_t(j)].title,textX,unscale(int(cy))-lineHeight/2,textWidth,lineHeight+2,fonts.body,titleTone,DT_LEFT|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
            }
        }
        const int bottom=unscale(height);
        const std::wstring legend=L"Enter  Select      Arrows  Move      Ctrl+K  Find      F1  Help";
        text(dc,legend,unscale(width)-48-620,bottom-44,620,20,fonts.caption,fadeAt(RGB(250,240,220),enter,width-p(300),height-p(34)),DT_RIGHT|DT_SINGLELINE|DT_NOPREFIX);
        std::wstring message=status;
        if(busy()){const auto current=progressText();if(!current.empty())message=current;}
        text(dc,message,48,bottom-44,unscale(width)-48-680,20,fonts.caption,error ? RGB(200,40,30) : fadeAt(RGB(250,240,220),enter,p(120),height-p(34)),DT_LEFT|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
    }
    static uint64_t achievementMask() {
        const auto progress=achievements::load(achievements::progress_path());
        uint64_t mask=0;
        for(size_t i=0;i<achievements::definition_count && i<64;++i)
            if(progress.has(achievements::definitions[i].id))mask|=1u<<i;
        return mask;
    }
    static bool featuredUnlocked(unsigned featured,uint64_t mask,bool developer=false) {
        return featured==0 || (featured<=achievements::definition_count && featured<=63 && (developer || ((mask>>(featured-1))&1u)));
    }
    static std::wstring featuredTitle(unsigned featured,uint64_t mask,bool developer=false) {
        if(featured==0 || featured>achievements::definition_count || !featuredUnlocked(featured,mask,developer))return {};
        return wide(achievements::definitions[featured-1].title);
    }
    static COLORREF frameTint(unsigned color) {
        if(color==0 || color>=frame_color_count)return CLR_INVALID;
        return RGB(frame_colors[color].r,frame_colors[color].g,frame_colors[color].b);
    }
    static bool titleUnlocked(unsigned title,uint32_t matches,uint64_t mask,bool developer=false) {
        if(title>=title_count)return false;
        if(developer)return true;
        const auto& choice=title_choices[title];
        if(choice.achievement==developer_title)return false;
        if(choice.achievement>=0)return (mask>>unsigned(choice.achievement))&1u;
        return profileLevel(matches)>=choice.level;
    }
    void labelTitles(uint32_t matches) {
        const auto list=controls[ProfileTitle];
        const auto chosen=SendMessageW(list,CB_GETCURSEL,0,0);
        const uint64_t mask=achievementMask();
        SendMessageW(list,CB_RESETCONTENT,0,0);
        for(unsigned i=0;i<title_count;++i) {
            const auto& choice=title_choices[i];
            std::wstring label=wide(choice.name);
            if(!titleUnlocked(i,matches,mask,holdsDevKey()))
                label+=choice.achievement==developer_title ? std::wstring(L"  (developer key)") : choice.achievement>=0 ? L"  (needs "+wide(achievements::definitions[size_t(choice.achievement)].title)+L")" : L"  (level "+std::to_wstring(choice.level)+L")";
            SendMessageW(list,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));
        }
        SendMessageW(list,CB_SETCURSEL,std::max<LRESULT>(0,chosen),0);
    }
    int fontHeight(HDC dc,HFONT font) const {
        const auto old=SelectObject(dc,font);TEXTMETRICW metrics{};GetTextMetricsW(dc,&metrics);SelectObject(dc,old);
        return std::max(1,unscale(int(metrics.tmHeight)));
    }
    int friendCount(bool onlineOnly=false) const {
        const auto profile=activeProfile();if(!profile)return 0;
        return int(std::count_if(settings.onlineFriends.begin(),settings.onlineFriends.end(),[&](const auto& row){
            return row.owner==profile->id && (!onlineOnly || (directory && directory->player(row.id).has_value()));
        }));
    }
    int onlineFriendCount() const {return friendCount(true);}
    std::filesystem::path stateSlot() const { return default_cache_directory().parent_path() / "states" / "slot1.okstate"; }
    std::optional<std::wstring> stateAge() const {
        std::error_code failed;
        const auto time = std::filesystem::last_write_time(stateSlot(), failed);
        if (failed)
            return std::nullopt;
        const auto minutes = std::chrono::duration_cast<std::chrono::minutes>(std::filesystem::file_time_type::clock::now() - time).count();
        if (minutes < 1)
            return std::wstring(L"just now");
        if (minutes < 60)
            return std::to_wstring(minutes) + (minutes == 1 ? L" minute ago" : L" minutes ago");
        if (minutes < 48 * 60)
            return std::to_wstring(minutes / 60) + (minutes / 60 == 1 ? L" hour ago" : L" hours ago");
        return std::to_wstring(minutes / 1440) + L" days ago";
    }
    static void summarizeGameLog(DWORD code) {
        std::ifstream log(default_cache_directory().parent_path()/"logs"/"startup.log");
        if(!log)return;
        const auto starts=[](const std::string& line,const char* prefix){return line.rfind(prefix,0)==0;};
        std::vector<std::string> first,problems;
        std::map<std::string,std::string> latest;
        std::string worst;double worstFps=1e9;size_t samples=0,lateSamples=0;
        for(std::string line;std::getline(log,line);) {
            if(line.size()>400)line.resize(400);
            for(const char* prefix:{"[build]","[host]","[display]","[display:settings]","[renderer] selected","[audio:output] enabled","[vector:engine]"})
                if(starts(line,prefix) && std::none_of(first.begin(),first.end(),[&](const std::string& seen){return starts(seen,prefix);})){first.push_back(line);break;}
            for(const char* prefix:{"[performance:frames]","[renderer:gpu]","[renderer:hires]","[audio:device]","[audio:output] stopped","[display] internal_scale","[startup:presentation]"})
                if(starts(line,prefix))latest[prefix]=line;
            if(starts(line,"[renderer:wait]")||starts(line,"[audio:core]"))latest[line.substr(0,std::min<size_t>(line.size(),line.find(' ',line.find(' ')+1)))]=line;
            if(starts(line,"[runtime:work]")){const auto stage=line.substr(0,line.find(' ',15));latest[stage]=line;}
            if(starts(line,"[performance:sample]")) {
                ++samples;
                const auto at=line.find("measured_fps=");
                if(at!=std::string::npos){const double fps=std::atof(line.c_str()+at+13);if(fps<worstFps){worstFps=fps;worst=line;}}
                const auto late=line.find("game_late_fields=");
                if(late!=std::string::npos && std::atoi(line.c_str()+late+17)>0)++lateSamples;
            }
            std::string lower=line;std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c){return char(std::tolower(c));});
            if(problems.size()<12 && (lower.find("error")!=std::string::npos || lower.find("failed")!=std::string::npos || lower.find("exception")!=std::string::npos)
               && lower.find("error=0")==std::string::npos)problems.push_back(line);
        }
        std::cout<<"[game] exit_code="<<code<<" samples="<<samples<<" samples_with_late_frames="<<lateSamples<<'\n';
        for(const auto& line:first)std::cout<<"[game] "<<line<<'\n';
        for(const auto& [key,line]:latest)std::cout<<"[game] "<<line<<'\n';
        if(!worst.empty())std::cout<<"[game] worst "<<worst<<'\n';
        for(const auto& line:problems)std::cout<<"[game] problem "<<line<<'\n';
        std::cout.flush();
    }
    bool busy() const { return task.valid(); }
    bool updating() const { return updateTask.valid(); }
    bool running() const { return child || externalGame; }
    bool dropdownOpen() const {
        for (const auto& [id,control] : controls) {
            wchar_t type[32]{};
            if (IsWindowVisible(control) && GetClassNameW(control,type,32) &&
                _wcsicmp(type,L"ComboBox")==0 && SendMessageW(control,CB_GETDROPPEDSTATE,0,0))
                return true;
        }
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
    const Settings::ProfileStyle* profileStyle(const std::string& id) const {
        const auto found=std::find_if(settings.profileStyles.begin(),settings.profileStyles.end(),[&](const auto& s){return s.id==id;});
        return found==settings.profileStyles.end() ? nullptr : &*found;
    }
    void openProfile() {
        const auto profile=activeProfile();
        if(page!=profilePage)profileReturnPage=navigationPage();
        if(!profile){page=profilePage;layout();focusControl(OnlineName);(pageDirty=true),InvalidateRect(hwnd,nullptr,FALSE);return;}
        const auto style=profileStyle(profile->id);
        SetWindowTextW(controls[ProfileInitials],style ? wide(style->initials).c_str() : L"");
        SetWindowTextW(controls[ProfileTagline],style ? wide(style->tagline).c_str() : L"");
        SendMessageW(controls[ProfileAccent],CB_SETCURSEL,style ? style->accent : 0,0);
        SendMessageW(controls[ProfileFit],CB_SETCURSEL,style ? style->pictureFit : 0,0);
        SendMessageW(controls[ProfileAvailability],CB_SETCURSEL,style ? style->availability : 0,0);
        SendMessageW(controls[ProfileNotifications],CB_SETCURSEL,style && !style->notifications ? 1 : 0,0);
        SendMessageW(controls[ProfileFighter],CB_SETCURSEL,style ? style->fighter : 0,0);
        SendMessageW(controls[ProfileSharing],CB_SETCURSEL,style ? style->sharing : 0,0);
        SendMessageW(controls[ProfileFriendAlerts],CB_SETCURSEL,style && !style->friendAlerts ? 1 : 0,0);
        SendMessageW(controls[ProfileRegion],CB_SETCURSEL,style ? style->region : 0,0);
        SendMessageW(controls[ProfilePlaystyle],CB_SETCURSEL,style ? style->playstyle : 0,0);
        SetWindowTextW(controls[ProfileAbout],style ? wide(style->about).c_str() : L"");
        SetWindowTextW(controls[ProfileLink],style ? wide(style->link).c_str() : L"");
        selectNameColor(style ? style->nameColor : 0);
        labelTitles(style ? style->matches : 0u);
        SendMessageW(controls[ProfileFrame],CB_SETCURSEL,style ? style->frame : 0u,0);
        SendMessageW(controls[ProfileBackdrop],CB_SETCURSEL,style ? style->backdrop : 0u,0);
        SendMessageW(controls[ProfileTitle],CB_SETCURSEL,style ? style->title : 0u,0);
        SendMessageW(controls[ProfileFrameColor],CB_SETCURSEL,style ? style->frameColor : 0u,0);
        SendMessageW(controls[ProfileFeatured],CB_SETCURSEL,style ? style->featured : 0u,0);
        refreshEffectChoices(style);
        page=profilePage;layout();(pageDirty=true),InvalidateRect(hwnd,nullptr,FALSE);
    }
    void applyCardLook(Settings::ProfileStyle& style) {
        style.frame=unsigned(std::clamp(selected(ProfileFrame),0,int(frame_count)-1));
        style.backdrop=unsigned(std::clamp(selected(ProfileBackdrop),0,int(backdrop_count)-1));
        style.frameColor=unsigned(std::clamp(selected(ProfileFrameColor),0,int(frame_color_count)-1));
        const unsigned title=unsigned(std::clamp(selected(ProfileTitle),0,int(title_count)-1));
        if(titleUnlocked(title,style.matches,achievementMask(),holdsDevKey()))style.title=title;
        const unsigned featured=unsigned(std::max(0,selected(ProfileFeatured)));
        style.featured=featuredUnlocked(featured,achievementMask(),holdsDevKey()) ? featured : 0u;
    }
    std::filesystem::path profileImageDirectory() const {return settings_path().parent_path()/"profiles"/"images";}
    void saveProfileImage(const std::string& file,const std::string& name) {
        const auto profile=activeProfile();if(!profile)return;
        Settings next=settings;
        auto found=std::find_if(next.profileStyles.begin(),next.profileStyles.end(),[&](const auto& style){return style.id==profile->id;});
        if(found==next.profileStyles.end()) {
            next.profileStyles.push_back({profile->id,{},{},0,{},{}});found=std::prev(next.profileStyles.end());
        }
        found->initials=utf8(trimmed(textOf(ProfileInitials)));
        found->tagline=utf8(trimmed(textOf(ProfileTagline)));
        found->accent=unsigned(std::max(0,selected(ProfileAccent)));
        found->image=file;found->imageName=name;
        found->pictureFit=unsigned(std::max(0,selected(ProfileFit)));
        found->availability=unsigned(std::max(0,selected(ProfileAvailability)));
        found->notifications=selected(ProfileNotifications)!=1;
        found->nameColor=chosenNameColor();found->fighter=unsigned(std::max(0,selected(ProfileFighter)));found->sharing=unsigned(std::max(0,selected(ProfileSharing)));
        found->friendAlerts=selected(ProfileFriendAlerts)!=1;found->nameEffect=chosenEffect();
        applyCardLook(*found);
        found->about=utf8(trimmed(textOf(ProfileAbout)));if(heldTag())found->link=utf8(trimmed(textOf(ProfileLink)));found->region=unsigned(std::max(0,selected(ProfileRegion)));found->playstyle=unsigned(std::max(0,selected(ProfilePlaystyle)));
        write_settings(settings_path(),next);settings=std::move(next);cycleTimer();openProfile();publishCard();
        status=file.empty() ? L"Profile picture removed. Your original file was kept." : found->sharing==0 ? L"Profile picture saved. Players online see it on your card." : L"Profile picture saved on this PC.";
        error=false;
    }
    void attachProfileImage(const std::filesystem::path& source) {
        if(source.empty() || !activeProfile())return;
        auto imported=speedrun::import_split_image(source,profileImageDirectory());
        profileImages[imported.file]=std::move(imported.image);
        saveProfileImage(imported.file,imported.name);
    }
    const speedrun::SplitImage* profileImage(const std::string& file) {
        if(file.empty())return nullptr;
        auto [found,inserted]=profileImages.try_emplace(file);
        if(inserted) {
            try{found->second=speedrun::load_split_image(profileImageDirectory()/file);}
            catch(const std::exception& error){std::cout<<"[launcher:profile-image] "<<error.what()<<std::endl;}
        }
        if(found->second.frames.size()>1 && allowMotion() && !imageAnimationTicking) {
            imageAnimationTicking=true;SetTimer(hwnd,3,motionInterval,nullptr);
        }
        return found->second.frames.empty() ? nullptr : &found->second;
    }
    const RemoteCard* remoteCard(const std::string& id) {
        if(id.empty() || !directory)return nullptr;
        const auto revision=directory->card_revision(id);
        if(!revision){remoteCards.erase(id);return nullptr;}
        auto& entry=remoteCards[id];
        if(entry.revision!=revision) {
            entry=RemoteCard{};
            if(const auto card=directory->card(id)) {
                entry.card=*card;entry.revision=card->revision;
                if(!card->picture.empty()) {
                    try{entry.image=speedrun::decode_image_bytes(std::vector<uint8_t>(card->picture.begin(),card->picture.end()));}
                    catch(const std::exception&){entry.image={};}
                    if(entry.image.frames.size()>1)entry.image.frames.resize(1);
                }
            }
        }
        return entry.revision ? &entry : nullptr;
    }
    std::string playerNamed(const std::string& name) const {
        if(!directory || name.empty())return {};
        for(const auto& player:directory->players())if(player.name==name)return player.id;
        return {};
    }
    uint32_t nameColorOf(const std::string& id) {
        if(const auto style=profileStyle(id))return style->nameColor;
        if(const auto remote=remoteCard(id))return remote->card.name_color;
        return 0;
    }
    COLORREF readableName(uint32_t stored,COLORREF fallback) const {
        if(!(stored>>24u))return fallback;
        COLORREF color=shownColor(name_style::refresh_name_color(stored));
        for(int step=0;step<4;++step) {
            const double a=luminance(color),b=luminance(panel),ratio=(std::max(a,b)+0.05)/(std::min(a,b)+0.05);
            if(ratio>=3.0)break;
            color=mix(color,ink,110);
        }
        return color;
    }
    COLORREF nameInk(const std::string& id,COLORREF fallback) {return readableName(nameColorOf(id),fallback);}
    static constexpr const wchar_t* name_effect_names[]={L"",L"Shenron wish",L"Fusion dance",L"Super Saiyan",L"Ki blast",L"Frost demon",L"Galick Gun",L"Majin Buu",L"Death Ball",L"Kaio-ken",L"Kaio-ken x20",L"Red Ribbon",L"Namek inferno",L"Kamehameha",L"Perfect Cell",L"Super Saiyan 2",L"Dragon Balls",L"Spirit Bomb",L"Majin magic",L"Candy beam",L"Instant Transmission",L"Champion belt",L"Cell Games",L"Dragon Fist",L"Planet Vegeta",L"Big Bang Attack",L"Piccolo",L"Powering up",L"Ginyu Force",L"Dynamite Kick",L"Kid Buu rampage"};
    bool effectPainted=false,pageEffectPainted=false,drawingRow=false,recordingMotion=false;
    struct Motion {RECT rect;std::vector<uint32_t> under;std::function<void(HDC)> draw;double clock=0.0;ULONGLONG ticks=0;std::vector<uint32_t> overlay;std::vector<uint8_t> mask;};
    void applyOverlay(const Motion& motion,LONG shift) {
        if(motion.mask.empty())return;
        GdiFlush();
        const size_t width=size_t(motion.rect.right-motion.rect.left);
        for(LONG y=motion.rect.top;y<motion.rect.bottom;++y) {
            const LONG row=y+shift;
            if(row<0 || row>=buffer.frame.height)continue;
            uint32_t* target=buffer.frame.pixels+size_t(row)*size_t(buffer.frame.width)+size_t(motion.rect.left);
            const size_t base=size_t(y-motion.rect.top)*width;
            for(size_t x=0;x<width;++x)if(motion.mask[base+x])target[x]=motion.overlay[base+x];
        }
    }
    void sealMotions() {
        if(motions.empty() || !buffer.frame.pixels)return;
        for(auto& motion:motions) {
            GdiFlush();
            const size_t width=size_t(motion.rect.right-motion.rect.left),count=width*size_t(motion.rect.bottom-motion.rect.top);
            std::vector<uint32_t> finished;finished.reserve(count);
            for(LONG y=motion.rect.top;y<motion.rect.bottom;++y){const auto row=buffer.frame.pixels+size_t(y)*size_t(buffer.frame.width)+size_t(motion.rect.left);finished.insert(finished.end(),row,row+width);}
            for(LONG y=motion.rect.top;y<motion.rect.bottom;++y)
                std::copy_n(motion.under.begin()+ptrdiff_t(size_t(y-motion.rect.top)*width),width,buffer.frame.pixels+size_t(y)*size_t(buffer.frame.width)+size_t(motion.rect.left));
            pinnedClock=motion.clock;pinnedTicks=motion.ticks;
            const int saved=SaveDC(buffer.dc);
            IntersectClipRect(buffer.dc,motion.rect.left,motion.rect.top,motion.rect.right,motion.rect.bottom);
            motion.draw(buffer.dc);
            RestoreDC(buffer.dc,saved);
            pinnedClock=-1.0;pinnedTicks=0;
            GdiFlush();
            motion.mask.assign(count,0);
            bool any=false;
            for(LONG y=motion.rect.top;y<motion.rect.bottom;++y) {
                const auto row=buffer.frame.pixels+size_t(y)*size_t(buffer.frame.width)+size_t(motion.rect.left);
                const size_t base=size_t(y-motion.rect.top)*width;
                for(size_t x=0;x<width;++x)if(row[x]!=finished[base+x]){motion.mask[base+x]=1;any=true;}
                std::copy_n(finished.begin()+ptrdiff_t(base),width,row);
            }
            if(any)motion.overlay=std::move(finished);else motion.mask.clear();
        }
    }
    std::vector<Motion> motions;
    bool motionTick=false;
    bool paintMotionsInPlace(const RECT& update,const RECT& client) {
        if(!std::exchange(motionTick,false) || motions.empty() || page==wavePage || pageDirty || painting==false || paintedPage!=page || paintedScroll!=pageScroll ||
           paintedSize!=std::pair<int,int>{client.right,client.bottom} || !buffer.dc || !buffer.frame.pixels)return false;
        const int visibleHeight=std::max(1,unscale(client.bottom)-shellHeader-dockReserve());
        const LONG top=p(shellHeader),bottom=p(shellHeader+visibleHeight),shift=top-p(pageScroll);
        RECT bounds{};bool any=false;
        for(const auto& motion:motions) {
            const RECT shown{motion.rect.left,motion.rect.top+shift,motion.rect.right,motion.rect.bottom+shift};
            if(shown.bottom<=top || shown.top>=bottom)continue;
            if(shown.top<top || shown.bottom>bottom || shown.bottom>buffer.frame.height)return false;
            if(!any)bounds=shown;else UnionRect(&bounds,&bounds,&shown);
            any=true;
        }
        if(!any || update.left<bounds.left || update.top<bounds.top || update.right>bounds.right || update.bottom>bounds.bottom)return false;
        GdiFlush();
        for(const auto& motion:motions) {
            const RECT shown{motion.rect.left,motion.rect.top+shift,motion.rect.right,motion.rect.bottom+shift};
            if(shown.bottom<=top || shown.top>=bottom)continue;
            const size_t width=size_t(motion.rect.right-motion.rect.left);
            for(LONG y=shown.top;y<shown.bottom;++y)
                std::copy_n(motion.under.begin()+ptrdiff_t(size_t(y-shown.top)*width),width,
                            buffer.frame.pixels+size_t(y)*size_t(buffer.frame.width)+size_t(shown.left));
        }
        for(const auto& motion:motions) {
            if(motion.rect.bottom+shift<=top || motion.rect.top+shift>=bottom)continue;
            const int saved=SaveDC(buffer.dc);
            SetViewportOrgEx(buffer.dc,0,shift,nullptr);
            IntersectClipRect(buffer.dc,motion.rect.left,motion.rect.top,motion.rect.right,motion.rect.bottom);
            motion.draw(buffer.dc);
            RestoreDC(buffer.dc,saved);
        }
        for(const auto& motion:motions)
            if(!(motion.rect.bottom+shift<=top || motion.rect.top+shift>=bottom))applyOverlay(motion,shift);
        return true;
    }
    void recordMotion(HDC dc,RECT r,std::function<void(HDC)> draw) {
        if(!recordingMotion || dc!=buffer.dc || !buffer.frame.pixels)return;
        r.left=std::max(0L,r.left);r.top=std::max(0L,r.top);
        r.right=std::min(LONG(buffer.frame.width),r.right);r.bottom=std::min(LONG(buffer.frame.height),r.bottom);
        if(r.right<=r.left || r.bottom<=r.top)return;
        GdiFlush();
        Motion motion{r,{},std::move(draw),profileClock(),motionTicks(),{},{}};
        const size_t width=size_t(r.right-r.left);
        motion.under.reserve(width*size_t(r.bottom-r.top));
        for(LONG y=r.top;y<r.bottom;++y) {
            const auto row=buffer.frame.pixels+size_t(y)*size_t(buffer.frame.width)+size_t(r.left);
            motion.under.insert(motion.under.end(),row,row+width);
        }
        motions.push_back(std::move(motion));
    }
    void replayMotions() {
        if(motions.empty())return;
        GdiFlush();
        for(const auto& motion:motions) {
            const size_t width=size_t(motion.rect.right-motion.rect.left);
            for(LONG y=motion.rect.top;y<motion.rect.bottom;++y)
                std::copy_n(motion.under.begin()+ptrdiff_t(size_t(y-motion.rect.top)*width),width,
                            buffer.frame.pixels+size_t(y)*size_t(buffer.frame.width)+size_t(motion.rect.left));
        }
        for(const auto& motion:motions) {
            const int saved=SaveDC(buffer.dc);
            IntersectClipRect(buffer.dc,motion.rect.left,motion.rect.top,motion.rect.right,motion.rect.bottom);
            motion.draw(buffer.dc);
            RestoreDC(buffer.dc,saved);
        }
        GdiFlush();
        for(const auto& motion:motions)applyOverlay(motion,0);
    }
    std::optional<identity::Tag> heldTag() const {
        const auto profile=activeProfile();if(!profile)return std::nullopt;
        const auto now=motionTicks();
        if(heldTagChecked && heldTagId==profile->id && now-heldTagTime<3000)return heldTagValue;
        heldTagChecked=true;heldTagId=profile->id;heldTagTime=now;
        heldTagValue=online::can_use_flair(*profile) ? identity::claimed_tag(profile->name) : std::nullopt;
        return heldTagValue;
    }
    bool developerProfile() const {return heldTag().has_value();}
    bool holdsDevKey() const {const auto tag=heldTag();return tag && *tag==identity::Tag::developer;}
    static COLORREF tagColor(identity::Tag tag) {return styleColor(name_style::plain_tag_color(tag));}
    std::optional<identity::Tag> verifiedTag(const std::string& id,const std::string& name) {
        if(id.empty())return std::nullopt;
        const auto profile=activeProfile();
        if(profile && profile->id==id)return heldTag();
        if(!directory || !directory->tagged(id))return std::nullopt;
        return identity::claimed_tag(name.empty() ? knownName(id) : name);
    }
    std::wstring netplayStyle() {
        const auto profile=activeProfile();if(!profile)return {};
        const auto style=profileStyle(profile->id);
        const uint32_t color=style ? name_style::refresh_name_color(style->nameColor) : 0u;
        const unsigned effect=effectOf(profile->id);
        wchar_t value[48];
        if(color>>24u)swprintf(value,48,L" --netplay-style=%06X,%u",unsigned(color&0xffffffu),effect);
        else swprintf(value,48,L" --netplay-style=,%u",effect);
        return value;
    }
    bool movingTag(const std::string& id) const {
        if(settings.reducedMotion)return false;
        const auto tag=identity::claimed_tag(knownName(id));
        return tag && name_style::tag_animates(*tag);
    }
    unsigned effectOf(const std::string& id) {
        if(id.empty())return 0;
        const auto profile=activeProfile();
        if(profile && profile->id==id) {
            const auto tag=heldTag();if(!tag)return 0;
            const auto style=profileStyle(id);
            return style && online::tag_allows(*tag,style->nameEffect) ? style->nameEffect : 0u;
        }
        return directory ? directory->flair(id) : 0u;
    }
    std::string channelOf(const std::string& id) {
        const auto profile=activeProfile();
        if(profile && profile->id==id) {
            const auto style=profileStyle(id);
            return heldTag() && style && online::valid_channel_link(style->link) ? style->link : std::string();
        }
        return directory ? directory->link(id) : std::string();
    }
    void keepProfileMotion() {
        effectPainted=true;if(!drawingRow)pageEffectPainted=true;
        if(!imageAnimationTicking && hwnd){imageAnimationTicking=true;SetTimer(hwnd,3,motionInterval,nullptr);}
    }
    static COLORREF effectColor(unsigned effect,size_t index,size_t count,double now) {
        return styleColor(name_style::effect_color(effect,index,count,now,styleRgb(ink)));
    }
    static double effectLift(unsigned effect,size_t index,double now) {return name_style::effect_lift(effect,index,now);}
    void drawName(HDC dc,const std::wstring& value,RECT r,UINT flags,const std::string& id,COLORREF color,unsigned effect=~0u) {
        if(effect==~0u)effect=effectOf(id);
        SetBkMode(dc,TRANSPARENT);
        const auto tag=value.empty() ? std::nullopt : identity::claimed_tag(utf8(value));
        size_t tagEnd=0;
        if(tag && value.front()==L'[') {
            const auto label=wide(identity::tag_label(*tag));
            if(value.size()>=label.size() && _wcsnicmp(value.c_str(),label.c_str(),label.size())==0)tagEnd=label.size();
        }
        const bool tagMoves=tagEnd && name_style::tag_animates(*tag) && !settings.reducedMotion;
        const uint32_t storedName=id.empty() ? 0u : nameColorOf(id);
        const bool shaded=!effect && (storedName>>24u) && readableName(storedName,ink)==color;
        if(!effect && !tagEnd && !shaded) {SetTextColor(dc,color);DrawTextW(dc,value.c_str(),int(value.size()),&r,flags);return;}
        if(effect || tagMoves) {
            keepProfileMotion();
            if(recordingMotion) {
                const auto font=static_cast<HFONT>(GetCurrentObject(dc,OBJ_FONT));
                recordMotion(dc,{r.left-p(4),r.top-p(5),r.right+p(8),r.bottom+p(5)},[this,value,r,flags,id,color,effect,font](HDC target) {
                    const auto old=SelectObject(target,font);
                    const bool recording=std::exchange(recordingMotion,false);
                    drawName(target,value,r,flags,id,color,effect);
                    recordingMotion=recording;
                    SelectObject(target,old);
                });
            }
        }
        const double now=effect || tagMoves ? profileClock() : 0.0;
        std::wstring shown=value;
        const auto extent=[&](const std::wstring& text,size_t count){SIZE size{};GetTextExtentPoint32W(dc,text.c_str(),int(count),&size);return int(size.cx);};
        const int room=r.right-r.left;
        if(extent(shown,shown.size())>room && room>0) {
            while(shown.size()>1 && extent(shown+L"\u2026",shown.size()+1)>room)shown.pop_back();
            shown+=L"\u2026";
        }
        const int total=extent(shown,shown.size());
        int x=(flags&DT_CENTER) ? (r.left+r.right-total)/2 : (flags&DT_RIGHT) ? r.right-total : r.left;
        x=std::max<int>(x,r.left);
        const UINT single=(flags&~UINT(DT_CENTER|DT_RIGHT|DT_END_ELLIPSIS|DT_WORDBREAK))|DT_LEFT|DT_NOCLIP|DT_SINGLELINE;
        const COLORREF labelTone=tag ? readableName(storedColor(tagColor(*tag)),ink) : color;
        const size_t nameCount=shown.size()>tagEnd ? shown.size()-tagEnd : 0;
        for(size_t i=0;i<shown.size();++i) {
            const int left=x+extent(shown,i),right=x+extent(shown,i+1);
            const int lift=effect && i>=tagEnd ? int(std::lround(effectLift(effect,i,now)*double(p(100))/100.0)) : 0;
            RECT cell{left,r.top+lift,right+p(6),r.bottom+lift};
            COLORREF tone=color;
            if(i<tagEnd)tone=tagMoves ? readableName(storedColor(styleColor(name_style::tag_color(*tag,i,tagEnd,now))),ink) : labelTone;
            else if(effect)tone=readableName(storedColor(effectColor(effect,i,shown.size(),now)),ink);
            else if(shaded)tone=readableName(storedColor(styleColor(name_style::plain_color(storedName,i-tagEnd,nameCount))),ink);
            SetTextColor(dc,tone);
            DrawTextW(dc,&shown[i],1,&cell,single);
        }
    }
    void nameText(HDC dc,const std::wstring& value,int x,int y,int w,int h,HFONT font,COLORREF color,UINT flags,const std::string& id,unsigned effect=~0u) {
        auto old=SelectObject(dc,font);
        drawName(dc,value,{p(x),p(y),p(x+w),p(y+h)},flags,id,color,effect);
        SelectObject(dc,old);
    }
    unsigned chosenEffect() const {
        const int index=selected(ProfileNameColor);const auto tag=heldTag();
        if(!tag || index<=name_color_custom)return 0u;
        const auto effects=online::tag_effects(*tag);
        const size_t slot=size_t(index-name_color_custom-1);
        return slot<effects.size() ? effects[slot] : 0u;
    }
    void refreshEffectChoices(const Settings::ProfileStyle* style) {
        const auto list=controls[ProfileNameColor];
        while(SendMessageW(list,CB_GETCOUNT,0,0)>name_color_custom+1)SendMessageW(list,CB_DELETESTRING,name_color_custom+1,0);
        const auto tag=heldTag();if(!tag)return;
        const auto effects=online::tag_effects(*tag);
        for(const unsigned effect:effects)
            SendMessageW(list,CB_ADDSTRING,0,reinterpret_cast<LPARAM>((wide(identity::tag_label(online::effect_tag(effect).value_or(*tag)))+L" "+name_effect_names[effect]).c_str()));
        if(const auto found=style ? std::find(effects.begin(),effects.end(),style->nameEffect) : effects.end();found!=effects.end()) {
            const int index=name_color_custom+1+int(found-effects.begin());
            SendMessageW(list,CB_SETCURSEL,index,0);nameColorSelection=index;
        }
    }
    std::wstring cardLine(const std::string& id) {
        const auto remote=remoteCard(id);if(!remote)return {};
        std::wstring line;
        if(remote->card.fighter && remote->card.fighter<=fighter_count)line=L"Mains "+wide(fighter_names[remote->card.fighter-1u]);
        if(!remote->card.tagline.empty())line+=(line.empty() ? L"\u201c" : L" \u00b7 \u201c")+wide(remote->card.tagline)+L"\u201d";
        return line;
    }
    void labelCustomColor(int index) {
        wchar_t label[48];
        if(index==name_color_custom)swprintf(label,48,L"Custom #%06X",unsigned(customNameColor&0xffffffu));
        else wcscpy(label,L"Custom color\u2026");
        SendMessageW(controls[ProfileNameColor],CB_DELETESTRING,name_color_custom,0);
        SendMessageW(controls[ProfileNameColor],CB_INSERTSTRING,name_color_custom,LPARAM(label));
    }
    void selectNameColor(uint32_t stored) {
        int index=0;
        if(stored>>24u) {
            index=name_color_custom;
            for(int i=0;i+1<name_color_custom;++i)if(storedColor(nameColorAt(i))==name_style::refresh_name_color(stored))index=i+1;
        }
        if(index==name_color_custom)customNameColor=stored;
        labelCustomColor(index);SendMessageW(controls[ProfileNameColor],CB_SETCURSEL,index,0);nameColorSelection=index;
    }
    uint32_t chosenNameColor() const {
        const int index=selected(ProfileNameColor);
        if(index<=0)return 0;
        if(index>name_color_custom) {
            const auto profile=activeProfile();const auto style=profile ? profileStyle(profile->id) : nullptr;
            return style ? style->nameColor : 0u;
        }
        return index<name_color_custom ? storedColor(nameColorAt(index-1)) : customNameColor;
    }
    static std::wstring seenText(int64_t seen) {
        const int64_t now=int64_t(std::time(nullptr)),ago=std::max<int64_t>(0,now-seen);
        if(seen<=0)return L"Offline";
        if(ago<60)return L"Last seen just now";
        if(ago<3600)return L"Last seen "+std::to_wstring(ago/60)+L" min ago";
        if(ago<86400)return L"Last seen "+std::to_wstring(ago/3600)+L" h ago";
        if(ago<2*86400)return L"Last seen yesterday";
        if(ago<30*86400)return L"Last seen "+std::to_wstring(ago/86400)+L" days ago";
        return L"Last seen over a month ago";
    }
    void watchFriends() {
        const auto profile=activeProfile();
        if(!profile || !directory || !directory->connected()) {friendsOnline.clear();friendWatchProfile.clear();return;}
        if(friendWatchProfile!=profile->id) {friendsOnline.clear();friendWatchProfile=profile->id;friendWatchSince=std::chrono::steady_clock::now();}
        std::set<std::string> now;std::map<std::string,std::string> names;
        for(const auto& player:directory->players())if(isFriend(player.id)){now.insert(player.id);names[player.id]=player.name;}
        std::vector<std::string> arrived,changed;
        for(const auto& id:now)if(!friendsOnline.count(id)){arrived.push_back(id);changed.push_back(id);}
        for(const auto& id:friendsOnline)if(!now.count(id))changed.push_back(id);
        friendsOnline=std::move(now);
        if(!changed.empty()) {
            Settings next=settings;const auto stamp=int64_t(std::time(nullptr));
            for(auto& entry:next.onlineFriends)
                if(entry.owner==profile->id && std::find(changed.begin(),changed.end(),entry.id)!=changed.end())entry.seen=stamp;
            try{write_settings(settings_path(),next);settings=std::move(next);}
            catch(const std::exception& error){std::cout<<"[launcher:friends] "<<error.what()<<std::endl;}
        }
        const auto style=profileStyle(profile->id);
        if(arrived.empty() || (style && !style->friendAlerts) || running() ||
           std::chrono::steady_clock::now()-friendWatchSince<std::chrono::seconds(8))return;
        std::cout<<"[launcher:friends] "<<arrived.size()<<(arrived.size()==1 ? " friend came online" : " friends came online")<<std::endl;
        const auto first=wide(names[arrived.front()]);
        const auto detail=cardLine(arrived.front());
        if(arrived.size()==1)notify(first+L" is online",detail.empty() ? std::wstring(L"Invite them from Friends or Online.") : detail);
        else notify(L"Friends are online",first+L" and "+std::to_wstring(arrived.size()-1)+(arrived.size()==2 ? L" other friend are online." : L" other friends are online."));
    }
    static std::wstring dateText(int64_t seconds) {
        std::tm local{};const std::time_t value=std::time_t(seconds);
        if(localtime_s(&local,&value)!=0)return L"Unknown";
        wchar_t out[32]{};wcsftime(out,32,L"%b %Y",&local);return out;
    }
    static std::wstring playedText(int64_t seen) {
        auto text=seenText(seen);
        return text.rfind(L"Last seen ",0)==0 ? L"Played "+text.substr(10) : text;
    }
    int recentCount() const {
        const auto profile=activeProfile();if(!profile)return 0;
        return int(std::count_if(settings.recentPlayers.begin(),settings.recentPlayers.end(),[&](const auto& entry){return entry.owner==profile->id;}));
    }
    int64_t recentSeen(const std::string& id) const {
        const auto profile=activeProfile();if(!profile)return 0;
        for(const auto& entry:settings.recentPlayers)if(entry.owner==profile->id && entry.id==id)return entry.seen;
        return 0;
    }
    bool isBlocked(const std::string& id) const {
        const auto profile=activeProfile();
        return profile && std::any_of(settings.blockedPlayers.begin(),settings.blockedPlayers.end(),[&](const auto& entry){return entry.owner==profile->id && entry.id==id;});
    }
    std::string knownName(const std::string& id) const {
        if(directory)if(const auto player=directory->player(id))return player->name;
        const auto profile=activeProfile();
        if(profile && profile->id==id)return profile->name;
        for(const auto* list:{&settings.onlineFriends,&settings.recentPlayers,&settings.blockedPlayers})
            for(const auto& entry:*list)if(profile && entry.owner==profile->id && entry.id==id)return entry.name;
        return {};
    }
    void setBlocked(const std::string& id,const std::string& name,bool block) {
        const auto profile=activeProfile();if(!profile || id.empty() || id==profile->id)return;
        if(block && isFriend(id)) {setFriend(id,name,false);if(directory)directory->send_friend(id,online::FriendKind::remove);}
        Settings next=settings;
        next.blockedPlayers.erase(std::remove_if(next.blockedPlayers.begin(),next.blockedPlayers.end(),[&](const auto& entry){return entry.owner==profile->id && entry.id==id;}),next.blockedPlayers.end());
        if(block)next.blockedPlayers.push_back({profile->id,id,name});
        write_settings(settings_path(),next);settings=std::move(next);
        if(block){std::lock_guard lock(onlineShared->mutex);auto& queue=onlineShared->notices;
            queue.erase(std::remove_if(queue.begin(),queue.end(),[&](const Notice& notice){return notice.sender()==id;}),queue.end());}
        status=block ? wide(name)+L" is blocked. Their invites and friend requests are ignored." : wide(name)+L" is unblocked.";error=false;
        refreshOnlineLists();refreshPlayerButtons();(pageDirty=true),InvalidateRect(hwnd,nullptr,FALSE);
    }
    void recordOpponent(const std::string& name) {
        const auto profile=activeProfile();if(!profile)return;
        auto id=playerNamed(name);
        if(id.empty() && !pendingInvitee.empty() && knownName(pendingInvitee)==name)id=pendingInvitee;
        Settings next=settings;const auto stamp=int64_t(std::time(nullptr));
        auto found=std::find_if(next.profileStyles.begin(),next.profileStyles.end(),[&](const auto& style){return style.id==profile->id;});
        if(found==next.profileStyles.end()){next.profileStyles.push_back({profile->id,{},{},0,{},{}});found=std::prev(next.profileStyles.end());}
        ++found->matches;if(!found->since)found->since=stamp;
        if(!id.empty() && id!=profile->id) {
            auto& recent=next.recentPlayers;
            recent.erase(std::remove_if(recent.begin(),recent.end(),[&](const auto& entry){return entry.owner==profile->id && entry.id==id;}),recent.end());
            recent.insert(recent.begin(),{profile->id,id,name,stamp});
            int kept=0;
            recent.erase(std::remove_if(recent.begin(),recent.end(),[&](const auto& entry){return entry.owner==profile->id && ++kept>20;}),recent.end());
        }
        try{write_settings(settings_path(),next);settings=std::move(next);publishCard();}
        catch(const std::exception& problem){std::cout<<"[launcher:online] "<<problem.what()<<std::endl;}
    }
    struct ShownCard {bool shared=false;online::Card card;const speedrun::SplitImage* image=nullptr;unsigned fit=0;};
    std::vector<std::pair<std::wstring,COLORREF>> profileBadges(const ShownCard& shown,const std::string& id,const std::wstring& name) {
        std::vector<std::pair<std::wstring,COLORREF>> badges;
        if(const auto keyTag=verifiedTag(id,utf8(name)))
            badges.emplace_back(wide(identity::tag_label(*keyTag)),readableName(storedColor(tagColor(*keyTag)),ink));
        if(!shown.shared)return badges;
        const auto& card=shown.card;
        if(card.matches>=200)badges.emplace_back(L"LEGEND",gold);
        else if(card.matches>=50)badges.emplace_back(L"Z WARRIOR",orange);
        else if(card.matches>=10)badges.emplace_back(L"FIGHTER",green);
        else if(card.matches>=1)badges.emplace_back(L"ROOKIE",sky);
        const int64_t now=int64_t(std::time(nullptr));
        if(card.since>0 && now-card.since>=365*86400)badges.emplace_back(L"VETERAN",RGB(196,150,255));
        else if(card.since>0 && card.since<1767225600)badges.emplace_back(L"EARLY FIGHTER",RGB(120,210,255));
        if(!card.tagline.empty() && !card.about.empty() && card.fighter && (!card.picture.empty() || shown.image))badges.emplace_back(L"COMPLETE CARD",RGB(140,230,170));
        const auto earned=[&](const char* idText){for(size_t i=0;i<achievements::definition_count && i<64;++i)if(std::string(achievements::definitions[i].id)==idText)return ((card.achievements>>i)&1u)!=0;return false;};
        if(earned("champion"))badges.emplace_back(L"WORLD CHAMPION",RGB(255,206,72));
        if(earned("credits"))badges.emplace_back(L"SAGA HERO",RGB(255,150,80));
        if(earned("flawless"))badges.emplace_back(L"UNTOUCHABLE",RGB(150,220,255));
        if(earned("online_win"))badges.emplace_back(L"ONLINE WARRIOR",RGB(120,200,255));
        if(earned("marathon"))badges.emplace_back(L"TIME CHAMBER",RGB(230,200,140));
        if(earned("speed_record"))badges.emplace_back(L"RECORD HOLDER",RGB(255,120,120));
        else if(earned("speed_finish"))badges.emplace_back(L"SPEEDRUNNER",RGB(255,170,110));
        if(earned("speed_permadeath"))badges.emplace_back(L"IRON WILL",RGB(200,200,220));
        if(earned("wins_500"))badges.emplace_back(L"UNSTOPPABLE",RGB(255,96,64));
        if(earned("streak"))badges.emplace_back(L"ON FIRE",RGB(255,140,40));
        if(earned("z6_flawless"))badges.emplace_back(L"ULTRA INSTINCT",RGB(214,224,255));
        else if(earned("z6_win"))badges.emplace_back(L"LIMIT BREAKER",RGB(255,110,90));
        if(earned("flawless_3"))badges.emplace_back(L"PERFECT FORM",RGB(160,240,255));
        if(earned("online_streak"))badges.emplace_back(L"RAMPAGE",RGB(255,90,140));
        int count=0;for(uint64_t bits=card.achievements;bits;bits&=bits-1)++count;
        if(count>=int(achievements::definition_count))badges.emplace_back(L"COMPLETIONIST",RGB(255,226,120));
        else if(count>=7)badges.emplace_back(L"ACHIEVER",RGB(210,180,255));
        if(!channelOf(id).empty())badges.emplace_back(L"STREAMER",RGB(190,140,255));
        return badges;
    }
    struct ProfileWave {
        int left,right,half,rightLeft,pictureLeft,hero=246,heroBottom=566,look=586,lookBottom=1054,privacy=1074,privacyBottom=1290,actions=1310,actionsBottom;
        int label(int width) const {return std::clamp(width*2/5,130,220);}
        int rowY(int top,int index) const {return top+64+index*waveOptionRow;}
    };
    ProfileWave profileWave(int content,bool tagged) const {
        ProfileWave g{};
        g.left=contentLeft;g.right=contentLeft+content;g.half=(content-20)/2;g.rightLeft=g.left+g.half+20;g.pictureLeft=g.right-24-260;
        g.actionsBottom=g.actions+(tagged ? 196 : 140);
        return g;
    }
    void waveRow(HDC dc,int left,int right,int y,int labelWidth,const std::wstring& label,COLORREF tone) {
        Gdiplus::Graphics graphics(dc);
        Gdiplus::LinearGradientBrush divider(Gdiplus::PointF(Gdiplus::REAL(p(left)),0),Gdiplus::PointF(Gdiplus::REAL(p(right)),0),Gdiplus::Color(70,255,236,210),Gdiplus::Color(8,255,236,210));
        graphics.FillRectangle(&divider,Gdiplus::REAL(p(left)),Gdiplus::REAL(p(y)),Gdiplus::REAL(p(right-left)),Gdiplus::REAL(std::max(1,p(1))));
        text(dc,label,left,y+14,labelWidth-12,28,fonts.body,tone,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
    }
    static unsigned profileLevel(uint32_t matches) {return std::min(99u,1u+matches/5u);}
    void paintBadges(HDC dc,const std::vector<std::pair<std::wstring,COLORREF>>& badges,int centre,int y,int width=280) {
        size_t start=0;
        while(start<badges.size()) {
            size_t end=start;int total=0;
            while(end<badges.size()) {
                const int next=unscale(tagWidth(dc,badges[end].first))+6;
                if(end>start && total+next>width)break;
                total+=next;++end;
            }
            int x=centre+total/2;
            for(size_t i=start;i<end;++i){tag(dc,p(x),p(y),badges[i].first,badges[i].second,false);x-=unscale(tagWidth(dc,badges[i].first))+6;}
            start=end;y+=24;
        }
    }
    void paintLevel(HDC dc,const ShownCard& shown,int left,int y,int width) {
        if(!shown.shared)return;
        const uint32_t matches=shown.card.matches;
        const unsigned level=profileLevel(matches);
        const double progress=level>=99 ? 1.0 : double(matches%5u)/5.0;
        text(dc,L"LEVEL "+std::to_wstring(level),left,y,120,22,fonts.strong,ink,DT_LEFT|DT_SINGLELINE|DT_NOPREFIX);
        text(dc,level>=99 ? std::wstring(L"Max level") : std::to_wstring(5u-matches%5u)+L" matches to next level",left+120,y+2,width-120,20,fonts.caption,muted,DT_RIGHT|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
        const RECT track{p(left),p(y+28),p(left+width),p(y+34)};
        box(dc,track,mix(panel,RGB(0,0,0),60),mix(panel,border,120),3);
        if(progress>0)box(dc,{track.left,track.top,track.left+LONG((track.right-track.left)*progress),track.bottom},orange,orange,3);
    }
    ShownCard shownCard(const std::string& id) {
        ShownCard out;
        const auto profile=activeProfile();
        if(profile && profile->id==id) {
            const auto style=profileStyle(id);
            if(style && style->sharing==2)return out;
            out.shared=true;
            if(style) {
                out.card.accent=style->accent;out.card.name_color=style->nameColor;out.card.initials=style->initials;out.card.tagline=style->tagline;
                out.card.fighter=style->fighter;out.card.region=style->region;out.card.playstyle=style->playstyle;out.card.about=style->about;
                out.card.matches=style->matches;out.card.since=style->since;
                out.card.frame=style->frame;out.card.backdrop=style->backdrop;out.card.title=style->title;out.card.achievements=achievementMask();
                out.card.frame_color=style->frameColor;out.card.featured=style->featured;
                if(style->sharing==0){out.image=profileImage(style->image);out.fit=style->pictureFit;}
            }
            return out;
        }
        if(const auto remote=remoteCard(id)) {
            out.shared=true;out.card=remote->card;
            if(!remote->image.frames.empty())out.image=&remote->image;
        }
        return out;
    }
    void openPlayer(const std::string& id,const std::string& name={}) {
        if(id.empty())return;
        if(page!=playerPage){playerReturnPage=page==finderPage ? finderReturnPage : page;playerNavigation=navigationPage();}
        viewedPlayer=id;viewedName=name.empty() ? knownName(id) : name;
        if(viewedName.empty())viewedName="Player "+id.substr(0,8);
        page=playerPage;layout();refreshPlayerButtons();refreshEnabled();(pageDirty=true),InvalidateRect(hwnd,nullptr,FALSE);
    }
    void refreshPlayerButtons() {
        if(viewedPlayer.empty() || controls.empty())return;
        const auto profile=activeProfile();const bool own=profile && profile->id==viewedPlayer;
        const auto player=directory ? directory->player(viewedPlayer) : std::nullopt;
        const auto hosted=player ? roomHostedBy(player->name) : std::nullopt;
        SetWindowTextW(controls[PlayerAction],own ? L"Edit my profile" : hosted ? L"Join their room" : L"Invite to play");
        SetWindowTextW(controls[PlayerFriend],isFriend(viewedPlayer) ? L"Remove friend" : requestedFriends.count(viewedPlayer) ? L"Friend request sent" : L"Add friend");
        SetWindowTextW(controls[PlayerBlock],isBlocked(viewedPlayer) ? L"Unblock" : L"Block");
        const bool online=directory && directory->connected();
        EnableWindow(controls[PlayerAction],own || (online && player && player->version==application_version && disc.has_value() && !busy() && !isBlocked(viewedPlayer)));
        EnableWindow(controls[PlayerFriend],online && !own && !isBlocked(viewedPlayer) && (isFriend(viewedPlayer) || !requestedFriends.count(viewedPlayer)));
        EnableWindow(controls[PlayerBlock],!own);
    }
    void paintPlayer(HDC dc,int h,int content) {
        const int right=contentLeft+content,split=contentLeft+std::clamp(content/3,260,340),side=split+44,width=right-side-24;
        panelBox(dc,contentLeft,124,split,764);panelBox(dc,split+20,124,right,764);
        if(viewedPlayer.empty())return;
        const auto profile=activeProfile();const bool own=profile && profile->id==viewedPlayer;
        const auto shown=shownCard(viewedPlayer);
        const auto player=directory ? directory->player(viewedPlayer) : std::nullopt;
        const auto name=wide(player ? player->name : viewedName);
        const auto cardTag=verifiedTag(viewedPlayer,utf8(name));
        if(cardTag)paintTagTheme(dc,*cardTag,contentLeft,124,split,764);
        if(shown.shared)paintBackdrop(dc,{p(contentLeft),p(124),p(split),p(764)},shown.card.backdrop,orange);
        const int cx=(contentLeft+split)/2,cy=214,radius=62;
        const unsigned accentIndex=shown.shared && shown.card.accent<accent_count ? shown.card.accent : 0u;
        if(shown.shared && accentIndex==rgb_accent)keepProfileMotion();
        const COLORREF tone=!shown.shared ? avatarTone(viewedPlayer) : accentIndex==rgb_accent ? colorOf(cycling_accent(uint64_t(profileClock()))) : accentIndex ? colorOf(accents[accentIndex].color) : orange;
        buffer.frame.halo({p(cx),p(cy),p(cx+1),p(cy+1)},0,p(84),tone,player ? 70 : 30);
        if(cardTag)ellipse(dc,p(cx),p(cy),p(radius+9),styleColor(name_style::tag_theme(*cardTag).edge));
        ellipse(dc,p(cx),p(cy),p(radius+6),panel);ellipse(dc,p(cx),p(cy),p(radius),tone);
        if(shown.shared)paintFrame(dc,p(cx),p(cy),p(radius+6),shown.card.frame,tone,frameTint(shown.card.frame_color));
        if(shown.image)paintRoundPhoto(dc,*shown.image,p(cx),p(cy),p(radius-2),shown.fit);
        else {
            const auto letters=shown.shared && !shown.card.initials.empty() ? wide(shown.card.initials) : initials(name);
            text(dc,letters,cx-radius,cy-24,radius*2,48,fonts.title,RGB(255,255,255),DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
        }
        const bool busyPlayer=player && (player->status=="in a match" || player->status=="hosting" || player->status=="joining" || player->status=="away" || player->status=="do not disturb");
        ellipse(dc,p(cx+44),p(cy+44),p(12),panel);ellipse(dc,p(cx+44),p(cy+44),p(9),player ? (busyPlayer ? gold : green) : faint);
        const int columnWidth=split-contentLeft-48;
        nameText(dc,name,contentLeft+24,292,columnWidth,30,fonts.brand,readableName(shown.shared ? shown.card.name_color : 0u,ink),DT_CENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX,viewedPlayer);
        int y=326;
        if(player && player->impostor){text(dc,L"NAME NOT VERIFIED",contentLeft+24,y,columnWidth,18,fonts.label,red,DT_CENTER|DT_SINGLELINE|DT_NOPREFIX);y+=22;}
        const bool developerCard=cardTag && *cardTag==identity::Tag::developer;
        if(shown.shared && shown.card.title>0 && shown.card.title<title_count && (title_choices[shown.card.title].achievement!=developer_title || developerCard)){text(dc,wide(title_choices[shown.card.title].name),contentLeft+24,y,columnWidth,18,fonts.label,gold,DT_CENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);y+=22;}
        if(shown.shared)if(const auto featured=featuredTitle(shown.card.featured,shown.card.achievements,developerCard);!featured.empty()){text(dc,L"\u2605 "+featured,contentLeft+24,y,columnWidth,18,fonts.label,RGB(255,214,120),DT_CENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);y+=22;}
        if(shown.shared && !shown.card.tagline.empty()){text(dc,L"\u201c"+wide(shown.card.tagline)+L"\u201d",contentLeft+24,y,columnWidth,22,fonts.caption,muted,DT_CENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);y+=24;}
        if(shown.shared && shown.card.fighter && shown.card.fighter<=fighter_count){text(dc,L"MAINS "+wide(fighter_names[shown.card.fighter-1u]),contentLeft+24,y,columnWidth,20,fonts.label,orange,DT_CENTER|DT_SINGLELINE|DT_NOPREFIX);y+=24;}
        std::wstring presence;
        if(own)presence=directory && directory->connected() ? L"This is you · as other players see you" : L"This is you · offline";
        else if(player) {
            presence=statusLabel(player->status);
            if(const auto hosted=hostingLine(player->name);!hosted.empty())presence=hosted;
        } else presence=isFriend(viewedPlayer) ? seenText(friendSeen(viewedPlayer)) : recentSeen(viewedPlayer) ? playedText(recentSeen(viewedPlayer)) : std::wstring(L"Offline");
        text(dc,presence,contentLeft+24,y+4,columnWidth,20,fonts.caption,player ? ink : muted,DT_CENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
        std::vector<std::pair<std::wstring,COLORREF>> tags;
        if(own)tags.emplace_back(L"YOU",orange);
        if(const auto keyTag=verifiedTag(viewedPlayer,viewedName))
            tags.emplace_back(L"VERIFIED "+wide(identity::tag_label(*keyTag)),readableName(storedColor(tagColor(*keyTag)),ink));
        if(isFriend(viewedPlayer))tags.emplace_back(L"FRIEND",green);
        if(isBlocked(viewedPlayer))tags.emplace_back(L"BLOCKED",red);
        if(recentSeen(viewedPlayer))tags.emplace_back(L"PLAYED BEFORE",sky);
        if(player && player->version!=application_version)tags.emplace_back(player->version.empty() ? std::wstring(L"OTHER VERSION") : L"V"+wide(player->version),red);
        if(!tags.empty()) {
            int total=0;for(const auto& tagValue:tags)total+=unscale(tagWidth(dc,tagValue.first))+6;
            int x=(contentLeft+split)/2+total/2;
            for(const auto& tagValue:tags){tag(dc,p(x),p(y+40),tagValue.first,tagValue.second);x-=unscale(tagWidth(dc,tagValue.first))+6;}
        }
        const int badgeY=y+(tags.empty() ? 40 : 66);
        paintBadges(dc,profileBadges(shown,viewedPlayer,name),(contentLeft+split)/2,badgeY,split-contentLeft-48);
        paintLevel(dc,shown,contentLeft+24,badgeY+20,columnWidth);
        heading(dc,L"About",side,144,width);
        if(!shown.shared)
            text(dc,own ? L"Your card is kept on this PC. Choose what to share on your Profile page." : L"This player has not shared a player card.",side,180,width,60,fonts.body,muted,DT_WORDBREAK|DT_NOPREFIX);
        else
            text(dc,shown.card.about.empty() ? std::wstring(own ? L"Add a few words about yourself on your Profile page." : L"No about text yet.") : wide(shown.card.about),
                 side,180,width,96,fonts.body,shown.card.about.empty() ? muted : ink,DT_WORDBREAK|DT_END_ELLIPSIS|DT_NOPREFIX|DT_EDITCONTROL);
        heading(dc,L"Details",side,300,width);
        const auto row=[&](int index,const std::wstring& label,const std::wstring& value){
            const int top=340+index*38;
            text(dc,label,side,top,width/2,24,fonts.caption,muted,DT_LEFT|DT_SINGLELINE|DT_NOPREFIX);
            text(dc,value,side+width/3,top,width-width/3-8,24,fonts.strong,ink,DT_RIGHT|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
            if(index<7)fill(dc,{p(side),p(top+30),p(side+width),p(top+31)},mix(panel,border,140));
        };
        const auto pick=[&](unsigned value,const auto& names){return value && value<=names.size() ? wide(names[value-1u]) : std::wstring(L"Not shown");};
        row(0,L"Region",shown.shared ? pick(shown.card.region,region_names) : L"Not shown");
        row(1,L"Playstyle",shown.shared ? pick(shown.card.playstyle,playstyle_names) : L"Not shown");
        row(2,L"Online matches",shown.shared ? std::to_wstring(shown.card.matches) : L"Not shown");
        row(3,L"Member since",shown.shared && shown.card.since>0 ? dateText(shown.card.since) : L"Not shown");
        row(4,L"Matches together",own ? std::wstring(L"\u2014") : recentSeen(viewedPlayer) ? playedText(recentSeen(viewedPlayer)) : std::wstring(L"Not yet"));
        row(5,L"OpenKai2 version",!player ? std::wstring(L"Offline") : player->version.empty() ? std::wstring(L"Unknown") : wide(player->version));
        row(6,L"Profile ID",wide(viewedPlayer.substr(0,16))+L"\u2026");
        text(dc,L"Right-click a name in Players, Friends or Rooms to open a profile. Ctrl+K finds players by name or profile ID.",side,630,width,40,fonts.caption,faint,DT_WORDBREAK|DT_NOPREFIX);
    }
    enum MenuKind {menuHeader,menuSeparator,menuProfile,menuInvite,menuJoin,menuFriend,menuUnfriend,menuCopy,menuId,menuBlock,menuChannel};
    struct MenuEntry {UINT command=0;std::wstring label,detail;MenuKind kind=menuProfile;bool danger=false,disabled=false;};
    std::vector<MenuEntry> menuEntries;std::string menuPlayer;
    void menuGlyph(HDC dc,MenuKind kind,int cx,int cy,COLORREF color) {
        const float size=float(p(16)),x=float(cx)-size/2,y=float(cy)-size/2;
        sampledVector(dc,{LONG(x-2),LONG(y-2),LONG(std::ceil(x+size))+2,LONG(std::ceil(y+size))+2},[&](Gdiplus::Graphics& g){
            g.TranslateTransform(x,y);g.ScaleTransform(size/24.f,size/24.f);
            Gdiplus::Pen pen(drawingColor(color),2.f);pen.SetLineJoin(Gdiplus::LineJoinRound);
            pen.SetStartCap(Gdiplus::LineCapRound);pen.SetEndCap(Gdiplus::LineCapRound);
            Gdiplus::SolidBrush brush(drawingColor(color));
            auto line=[&](float a,float b,float c,float d){g.DrawLine(&pen,a,b,c,d);};
            auto person=[&](float dx){g.DrawEllipse(&pen,dx+5.f,3.f,8.f,8.f);g.DrawArc(&pen,dx+1.f,13.f,16.f,16.f,180.f,180.f);};
            switch(kind) {
            case menuProfile:person(3.f);break;
            case menuInvite:{Gdiplus::PointF tri[]={{7,4},{20,12},{7,20}};g.FillPolygon(&brush,tri,3);break;}
            case menuJoin:line(3,12,15,12);line(11,8,15,12);line(15,12,11,16);g.DrawRectangle(&pen,15.f,4.f,6.f,16.f);break;
            case menuFriend:person(0.f);line(19,6,19,14);line(15,10,23,10);break;
            case menuUnfriend:person(0.f);line(15,10,23,10);break;
            case menuCopy:g.DrawRectangle(&pen,8.f,8.f,12.f,13.f);line(4,16,4,3);line(4,3,15,3);break;
            case menuId:line(9,3,7,21);line(17,3,15,21);line(4,9,21,9);line(3,15,20,15);break;
            case menuBlock:g.DrawEllipse(&pen,3.f,3.f,18.f,18.f);line(6,18,18,6);break;
            case menuChannel:{g.DrawRectangle(&pen,3.f,5.f,18.f,13.f);Gdiplus::PointF play[]={{10,8.5f},{15,11.5f},{10,14.5f}};g.FillPolygon(&brush,play,3);line(8,21,16,21);break;}
            default:break;
            }
        });
    }
    void measureMenu(MEASUREITEMSTRUCT& item) {
        const size_t index=size_t(item.itemData);
        if(index>=menuEntries.size()){item.itemHeight=p(32);item.itemWidth=p(180);return;}
        const auto& entry=menuEntries[index];
        if(entry.kind==menuSeparator){item.itemHeight=p(9);item.itemWidth=p(60);return;}
        HDC dc=GetDC(hwnd);auto old=SelectObject(dc,entry.kind==menuHeader ? fonts.strong : fonts.body);
        SIZE label{};GetTextExtentPoint32W(dc,entry.label.c_str(),int(entry.label.size()),&label);
        SIZE detail{};SelectObject(dc,fonts.caption);GetTextExtentPoint32W(dc,entry.detail.c_str(),int(entry.detail.size()),&detail);
        SelectObject(dc,old);ReleaseDC(hwnd,dc);
        if(entry.kind==menuHeader){item.itemHeight=p(54);item.itemWidth=UINT(std::max<LONG>(p(200),std::max(label.cx,detail.cx)+p(70)));return;}
        item.itemHeight=p(34);item.itemWidth=UINT(std::max<LONG>(p(200),label.cx+p(60)+(entry.detail.empty() ? 0 : detail.cx+p(18))));
    }
    void drawMenu(const DRAWITEMSTRUCT& item) {
        const size_t index=size_t(item.itemData);
        if(index>=menuEntries.size())return;
        const auto& entry=menuEntries[index];HDC dc=item.hDC;RECT r=item.rcItem;
        fill(dc,r,raised);
        if(entry.kind==menuSeparator){fill(dc,{r.left+p(10),(r.top+r.bottom)/2,r.right-p(10),(r.top+r.bottom)/2+std::max(1,p(1))},mix(raised,border,200));return;}
        SetBkMode(dc,TRANSPARENT);
        if(entry.kind==menuHeader) {
            const int radius=p(16),cx=r.left+p(10)+radius,cy=(r.top+r.bottom)/2;
            if(!menuPlayer.empty())avatar(dc,cx,cy,radius,entry.label,menuPlayer,raised);
            else {ellipse(dc,cx,cy,radius,mix(raised,orange,60));}
            auto old=SelectObject(dc,fonts.strong);
            RECT name{cx+radius+p(10),r.top+p(8),r.right-p(10),cy+p(2)};
            drawName(dc,entry.label,name,DT_LEFT|DT_BOTTOM|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX,menuPlayer,menuPlayer.empty() ? ink : nameInk(menuPlayer,ink));
            SelectObject(dc,fonts.caption);SetTextColor(dc,muted);
            RECT line{name.left,cy+p(3),name.right,r.bottom-p(6)};
            DrawTextW(dc,entry.detail.c_str(),-1,&line,DT_LEFT|DT_TOP|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
            SelectObject(dc,old);return;
        }
        const bool disabled=entry.disabled,selectedRow=(item.itemState&ODS_SELECTED)!=0 && !disabled;
        const COLORREF tone=entry.danger ? red : orange;
        if(selectedRow)box(dc,{r.left+p(5),r.top+p(2),r.right-p(5),r.bottom-p(2)},mix(raised,tone,52),mix(raised,tone,120),8);
        const COLORREF label=disabled ? faint : entry.danger ? red : selectedRow ? ink : mix(muted,ink,170);
        menuGlyph(dc,entry.kind,r.left+p(22),(r.top+r.bottom)/2,disabled ? faint : selectedRow || entry.danger ? tone : muted);
        auto old=SelectObject(dc,fonts.body);SetTextColor(dc,label);
        RECT text{r.left+p(40),r.top,r.right-p(12),r.bottom};
        DrawTextW(dc,entry.label.c_str(),-1,&text,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
        if(!entry.detail.empty()) {
            SelectObject(dc,fonts.caption);SetTextColor(dc,faint);
            DrawTextW(dc,entry.detail.c_str(),-1,&text,DT_RIGHT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
        }
        SelectObject(dc,old);
    }
    void contextMenu(HWND child,LPARAM lp) {
        const int id=GetDlgCtrlID(child);
        if(id!=PlayerList && id!=FriendList && id!=RoomList)return;
        POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
        int row=-1;
        if(point.x==-1 && point.y==-1) {
            row=int(SendMessageW(child,LB_GETCURSEL,0,0));RECT item{};
            if(row<0 || SendMessageW(child,LB_GETITEMRECT,WPARAM(row),reinterpret_cast<LPARAM>(&item))==LB_ERR)return;
            point={(item.left+item.right)/2,(item.top+item.bottom)/2};ClientToScreen(child,&point);
        } else {
            POINT local=point;ScreenToClient(child,&local);
            const auto hit=SendMessageW(child,LB_ITEMFROMPOINT,0,MAKELPARAM(local.x,local.y));
            if(HIWORD(hit))return;
            row=int(LOWORD(hit));
            SendMessageW(child,LB_SETCURSEL,WPARAM(row),0);command(id,LBN_SELCHANGE);
        }
        std::string playerId,name;std::optional<online::Room> roomChoice;
        if(id==PlayerList && size_t(row)<playerRows.size()){playerId=playerRows[size_t(row)].first;name=playerRows[size_t(row)].second;}
        else if(id==FriendList && size_t(row)<friendRows.size()){playerId=friendRows[size_t(row)].first;name=friendRows[size_t(row)].second;}
        else if(id==RoomList && size_t(row)<roomRows.size()){roomChoice=roomRows[size_t(row)];name=roomChoice->host;playerId=playerNamed(name);}
        else return;
        const auto profile=activeProfile();const bool own=profile && profile->id==playerId;
        const auto player=directory && !playerId.empty() ? directory->player(playerId) : std::nullopt;
        const auto hosted=player && !roomChoice ? roomHostedBy(player->name) : std::nullopt;
        enum {ViewProfile=1,InviteOrJoin,FriendToggle,CopyName,CopyId,BlockToggle,OpenChannel,CopyChannel};
        const auto channel=playerId.empty() ? std::string() : channelOf(playerId);
        menuEntries.clear();menuPlayer=playerId;
        std::wstring presence=L"Offline";
        if(own)presence=L"This is you";
        else if(player)presence=hosted ? L"Hosting a room" : statusLabel(player->status);
        else if(roomChoice)presence=L"Room host";
        else if(isFriend(playerId))presence=seenText(friendSeen(playerId));
        menuEntries.push_back({0,wide(name),presence,menuHeader});
        menuEntries.push_back({0,{},{},menuSeparator});
        const bool ready=disc.has_value() && !busy() && directory && directory->connected();
        if(!playerId.empty())menuEntries.push_back({ViewProfile,own ? L"View my profile" : L"View profile",{},menuProfile});
        if(roomChoice)menuEntries.push_back({InviteOrJoin,L"Join room",{},menuJoin,false,!(ready && !room && roomChoice->version==application_version)});
        else if(player && !own)menuEntries.push_back({InviteOrJoin,hosted ? L"Join their room" : L"Invite to play",{},hosted ? menuJoin : menuInvite,false,
                                                      !(ready && player->version==application_version && !isBlocked(playerId))});
        if(online::valid_channel_link(channel))menuEntries.push_back({OpenChannel,L"Open channel",{},menuChannel});
        if(!playerId.empty() && !own) {
            const bool friendly=isFriend(playerId),sent=requestedFriends.count(playerId)>0;
            menuEntries.push_back({FriendToggle,friendly ? L"Remove friend" : sent ? L"Friend request sent" : L"Add friend",{},friendly ? menuUnfriend : menuFriend,false,
                                   !(directory && directory->connected() && !isBlocked(playerId) && (friendly || !sent))});
        }
        menuEntries.push_back({0,{},{},menuSeparator});
        menuEntries.push_back({CopyName,L"Copy name",{},menuCopy});
        if(!playerId.empty())menuEntries.push_back({CopyId,L"Copy profile ID",{},menuId});
        if(online::valid_channel_link(channel))menuEntries.push_back({CopyChannel,L"Copy channel link",{},menuCopy});
        if(!playerId.empty() && !own) {
            menuEntries.push_back({0,{},{},menuSeparator});
            menuEntries.push_back({BlockToggle,isBlocked(playerId) ? L"Unblock" : L"Block",{},menuBlock,!isBlocked(playerId)});
        }
        HMENU menu=CreatePopupMenu();if(!menu)return;
        HBRUSH menuBrush=CreateSolidBrush(raised);
        MENUINFO info{};info.cbSize=sizeof(info);info.fMask=MIM_BACKGROUND;info.hbrBack=menuBrush;SetMenuInfo(menu,&info);
        for(size_t i=0;i<menuEntries.size();++i) {
            const auto& entry=menuEntries[i];
            UINT flags=MF_OWNERDRAW;
            if(entry.kind==menuSeparator)flags|=MF_SEPARATOR;
            if(entry.kind==menuHeader || entry.disabled)flags|=MF_DISABLED;
            AppendMenuW(menu,flags,entry.command ? entry.command : UINT(9000+i),reinterpret_cast<LPCWSTR>(i));
        }
        const int choice=int(TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON|TPM_NONOTIFY,point.x,point.y,0,hwnd,nullptr));
        DestroyMenu(menu);DeleteObject(menuBrush);menuEntries.clear();
        switch(choice) {
        case ViewProfile:openPlayer(playerId,name);break;
        case InviteOrJoin:
            if(roomChoice)joinRoom(*roomChoice);
            else if(hosted)joinRoom(*hosted);
            else invitePlayer();
            break;
        case FriendToggle:toggleFriend();break;
        case CopyName:status=copyText(wide(name)) ? L"Name copied." : L"Could not access the clipboard.";error=false;break;
        case CopyId:status=copyText(wide(playerId)) ? L"Profile ID copied." : L"Could not access the clipboard.";error=false;break;
        case BlockToggle:setBlocked(playerId,name,!isBlocked(playerId));break;
        case OpenChannel: {
            const auto result=reinterpret_cast<INT_PTR>(ShellExecuteW(hwnd,L"open",wide(channel).c_str(),nullptr,nullptr,SW_SHOWNORMAL));
            status=result>32 ? L"Opening "+wide(channel) : std::wstring(L"Could not open the channel link.");error=result<=32;break;
        }
        case CopyChannel:status=copyText(wide(channel)) ? L"Channel link copied." : L"Could not access the clipboard.";error=false;break;
        default:return;
        }
        refreshEnabled();(pageDirty=true),InvalidateRect(hwnd,nullptr,FALSE);
    }
    void publishCard() {
        if(!directory || !directory->connected())return;
        const auto profile=activeProfile();if(!profile || profile->id!=connectedProfile)return;
        if(const auto existing=profileStyle(profile->id);!existing || !existing->since) {
            Settings next=settings;
            auto found=std::find_if(next.profileStyles.begin(),next.profileStyles.end(),[&](const auto& style){return style.id==profile->id;});
            if(found==next.profileStyles.end()){next.profileStyles.push_back({profile->id,{},{},0,{},{}});found=std::prev(next.profileStyles.end());}
            found->since=int64_t(std::time(nullptr));
            try{write_settings(settings_path(),next);settings=std::move(next);}
            catch(const std::exception& problem){std::cout<<"[launcher:profile-card] "<<problem.what()<<std::endl;}
        }
        const auto style=profileStyle(profile->id);
        directory->set_flair(style ? style->nameEffect : 0u);
        directory->set_link(style && online::valid_channel_link(style->link) ? style->link : std::string());
        if(style && style->sharing==2){directory->set_card(std::nullopt);return;}
        online::Card card;
        if(style) {
            card.accent=style->accent;card.name_color=style->nameColor;card.initials=style->initials;card.tagline=style->tagline;card.fighter=style->fighter;
            card.about=style->about;card.region=style->region;card.playstyle=style->playstyle;card.matches=style->matches;card.since=style->since;
            card.frame=style->frame;card.backdrop=style->backdrop;card.title=style->title;card.achievements=achievementMask();
            card.frame_color=style->frameColor;card.featured=style->featured;
            if(style->sharing==0 && !style->image.empty()) {
                const COLORREF tone=style->accent && style->accent!=rgb_accent ? colorOf(accents[style->accent].color) : orange;
                const auto key=style->image+':'+std::to_string(style->pictureFit)+':'+std::to_string(tone);
                auto found=cardThumbnails.find(key);
                if(found==cardThumbnails.end()) {
                    std::string bytes;
                    if(const auto image=profileImage(style->image)) {
                        try{const auto jpeg=speedrun::encode_thumbnail(*image,style->pictureFit,storedColor(tone)&0xffffffu,online::card_picture_limit);bytes.assign(jpeg.begin(),jpeg.end());}
                        catch(const std::exception& error){std::cout<<"[launcher:profile-card] "<<error.what()<<std::endl;}
                    }
                    found=cardThumbnails.emplace(key,std::move(bytes)).first;
                }
                card.picture=found->second;
            }
        }
        directory->set_card(card);
    }
    static COLORREF avatarTone(const std::string &key) {
        static const COLORREF tones[] = {RGB(226, 118, 58), RGB(214, 170, 64), RGB(206, 86, 86),  RGB(84, 174, 124),
                                         RGB(74, 142, 214), RGB(150, 106, 214), RGB(212, 104, 160), RGB(64, 170, 180)};
        uint32_t hash = 2166136261u;
        for (unsigned char c : key)
            hash = (hash ^ c) * 16777619u;
        return tones[hash % 8u];
    }
    static std::wstring initials(const std::wstring &name) {
        std::wstring out;
        bool start = true;
        for (wchar_t c : name) {
            if (iswalnum(c)) {
                if (start && out.size() < 2)
                    out.push_back(wchar_t(towupper(c)));
                start = false;
            } else
                start = true;
        }
        return out.empty() ? std::wstring(L"?") : out;
    }
    void paintRoundPhoto(HDC dc,const speedrun::SplitImage& image,int cx,int cy,int radius,unsigned mode) {
        const int side=std::max(2,radius*2);
        const auto& frame=image.frames[image.frame_at(allowMotion() ? imageTicks() : 0)].rgba;
        const auto framing=speedrun::picture_framing(image.width,image.height,unsigned(side),mode);
        const bool fit=framing.fit;const double scale=framing.scale;
        std::vector<unsigned char> pixels(size_t(side)*side*4);
        for(int y=0;y<side;++y)for(int x=0;x<side;++x) {
            const double coverage=std::clamp(radius+.5-std::hypot(x+.5-radius,y+.5-radius),0.0,1.0);
            if(coverage<=0)continue;
            if(fit && (std::abs(x+.5-side/2.0)>image.width*scale/2 || std::abs(y+.5-side/2.0)>image.height*scale/2))continue;
            const double sx=std::clamp((x+.5-side/2.0)/scale+framing.centerX-.5,0.0,double(image.width-1));
            const double sy=std::clamp((y+.5-side/2.0)/scale+framing.centerY-.5,0.0,double(image.height-1));
            const unsigned x0=unsigned(sx),y0=unsigned(sy),x1=std::min(x0+1,image.width-1),y1=std::min(y0+1,image.height-1);
            const double fx=sx-x0,fy=sy-y0;
            const unsigned xx[]={x0,x1,x0,x1},yy[]={y0,y0,y1,y1};
            const double weights[]={(1-fx)*(1-fy),fx*(1-fy),(1-fx)*fy,fx*fy};
            double rgba[4]{};
            for(unsigned i=0;i<4;++i) {
                const size_t source=(size_t(yy[i])*image.width+xx[i])*4;
                const double alpha=frame[source+3]/255.0*weights[i];
                for(unsigned c=0;c<3;++c)rgba[c]+=frame[source+c]*alpha;
                rgba[3]+=alpha*255;
            }
            const size_t target=(size_t(y)*side+x)*4;
            pixels[target]=static_cast<unsigned char>(std::lround(rgba[2]*coverage));
            pixels[target+1]=static_cast<unsigned char>(std::lround(rgba[1]*coverage));
            pixels[target+2]=static_cast<unsigned char>(std::lround(rgba[0]*coverage));
            pixels[target+3]=static_cast<unsigned char>(std::lround(rgba[3]*coverage));
        }
        Gdiplus::Bitmap bitmap(side,side,side*4,PixelFormat32bppPARGB,pixels.data());
        Gdiplus::Graphics graphics(dc);smoothDrawing(graphics);
        graphics.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
        graphics.DrawImage(&bitmap,cx-radius,cy-radius,side,side);
    }
    void paintFrame(HDC dc,int cx,int cy,int radius,unsigned frame,COLORREF tone,COLORREF tint=CLR_INVALID) {
        if(!frame || frame>=frame_count || radius<6)return;
        Gdiplus::Graphics g(dc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        const auto recolor=[tint](COLORREF c) {
            if(tint==CLR_INVALID)return c;
            const double light=(GetRValue(c)*0.299+GetGValue(c)*0.587+GetBValue(c)*0.114)/255.0;
            const double base=std::min(1.0,light*1.35),white=std::max(0.0,(light-0.78)/0.22)*0.7;
            const auto channel=[&](int value){return BYTE(std::clamp(value*base*(1.0-white)+255.0*white,0.0,255.0));};
            return RGB(channel(GetRValue(tint)),channel(GetGValue(tint)),channel(GetBValue(tint)));
        };
        const auto color=[&](COLORREF c,int a){c=recolor(c);return Gdiplus::Color(BYTE(std::clamp(a,0,255)),GetRValue(c),GetGValue(c),GetBValue(c));};
        const float r=float(radius),x=float(cx),y=float(cy),w=std::max(1.5f,r*0.09f),inkWidth=std::max(1.2f,w*0.55f);
        const float pi=3.14159265f;
        const auto lighter=[](COLORREF c){return RGB((GetRValue(c)+255)/2,(GetGValue(c)+255)/2,(GetBValue(c)+255)/2);};
        const auto darker=[](COLORREF c){return RGB(GetRValue(c)*3/5,GetGValue(c)*3/5,GetBValue(c)*3/5);};
        const auto ring=[&](float at,COLORREF c,int a,float width){Gdiplus::Pen pen(color(c,a),width);g.DrawEllipse(&pen,x-at,y-at,at*2,at*2);};
        const auto arc=[&](float at,COLORREF c,int a,float width,float from,float sweep){Gdiplus::Pen pen(color(c,a),width);pen.SetStartCap(Gdiplus::LineCapRound);pen.SetEndCap(Gdiplus::LineCapRound);g.DrawArc(&pen,x-at,y-at,at*2,at*2,from,sweep);};
        const auto polar=[&](float angle,float at){return Gdiplus::PointF(x+std::cos(angle)*at,y+std::sin(angle)*at);};
        const auto glow=[&](float at,COLORREF c,int a,float width,float spread){for(int i=4;i>=1;--i)ring(at,c,a*(5-i)/14,width+spread*float(i));ring(at,c,a,width);};
        const auto glowArc=[&](float at,COLORREF c,int a,float width,float spread,float from,float sweep){for(int i=4;i>=1;--i)arc(at,c,a*(5-i)/14,width+spread*float(i),from,sweep);arc(at,c,a,width,from,sweep);};
        const auto gloss=[&](float at,float width,COLORREF top,COLORREF bottom) {
            Gdiplus::GraphicsPath band(Gdiplus::FillModeAlternate);
            const float outer=at+width/2,inner=at-width/2;
            band.AddEllipse(x-outer,y-outer,outer*2,outer*2);band.AddEllipse(x-inner,y-inner,inner*2,inner*2);
            Gdiplus::LinearGradientBrush fill(Gdiplus::PointF(x-outer*0.3f,y-outer),Gdiplus::PointF(x+outer*0.3f,y+outer),color(top,255),color(bottom,255));
            const COLORREF mid=RGB((GetRValue(top)+GetRValue(bottom))/2,(GetGValue(top)+GetGValue(bottom))/2,(GetBValue(top)+GetBValue(bottom))/2);
            Gdiplus::Color stops[]={color(lighter(top),255),color(top,255),color(mid,255),color(bottom,255),color(mid,255),color(lighter(mid),255)};
            Gdiplus::REAL positions[]={0.f,0.18f,0.42f,0.62f,0.86f,1.f};
            fill.SetInterpolationColors(stops,positions,6);
            g.FillPath(&fill,&band);
            Gdiplus::Pen edge(color(RGB(0,0,0),170),std::max(1.f,inkWidth*0.6f));g.DrawEllipse(&edge,x-outer,y-outer,outer*2,outer*2);g.DrawEllipse(&edge,x-inner,y-inner,inner*2,inner*2);
            arc(outer-std::max(1.f,inkWidth*0.5f),RGB(255,255,255),170,std::max(1.f,inkWidth*0.55f),195.f,110.f);
            arc(inner+std::max(1.f,inkWidth*0.5f),RGB(255,255,255),90,std::max(1.f,inkWidth*0.45f),15.f,80.f);
            arc(at+width*0.12f,RGB(255,255,255),110,width*0.2f,205.f,70.f);
        };
        const auto sparkle=[&](float sx,float sy,float size,COLORREF c,int a) {
            Gdiplus::PointF points[8];
            for(int i=0;i<8;++i){const float angle=-pi/2+i*pi/4,rr=i%2 ? size*0.22f : size;points[i]={sx+std::cos(angle)*rr,sy+std::sin(angle)*rr};}
            Gdiplus::GraphicsPath halo;halo.AddEllipse(sx-size*0.9f,sy-size*0.9f,size*1.8f,size*1.8f);
            Gdiplus::PathGradientBrush soft(&halo);soft.SetCenterColor(color(c,a/2));Gdiplus::Color clear=color(c,0);int one=1;soft.SetSurroundColors(&clear,&one);g.FillPath(&soft,&halo);
            Gdiplus::SolidBrush b(color(c,a));g.FillPolygon(&b,points,8);
        };
        const auto planet=[&](float px,float py,float size,COLORREF base) {
            Gdiplus::GraphicsPath body;body.AddEllipse(px-size,py-size,size*2,size*2);
            Gdiplus::PathGradientBrush shade(&body);shade.SetCenterPoint(Gdiplus::PointF(px-size*0.35f,py-size*0.4f));
            shade.SetCenterColor(color(lighter(base),255));Gdiplus::Color rim=color(darker(base),255);int one=1;shade.SetSurroundColors(&rim,&one);
            g.FillPath(&shade,&body);
        };
        const auto ellipseArc=[&](float rx,float ry,float tilt,COLORREF c,int a,float width,float from,float sweep) {
            const auto state=g.Save();g.TranslateTransform(x,y);g.RotateTransform(tilt);
            Gdiplus::Pen pen(color(c,a),width);g.DrawArc(&pen,-rx,-ry,rx*2,ry*2,from,sweep);g.Restore(state);
        };
        {
            const COLORREF black=RGB(0,0,0);
            for(int i=5;i>=1;--i){Gdiplus::Pen shade(Gdiplus::Color(BYTE(16+i*4),0,0,0),w*(2.6f+float(i)*0.7f));g.DrawEllipse(&shade,x-r-w*1.6f,y-r-w*1.6f+w*0.5f,(r+w*1.6f)*2,(r+w*1.6f)*2);}
            Gdiplus::Pen inner(color(black,120),w*0.7f);g.DrawEllipse(&inner,x-r,y-r,r*2,r*2);
        }
        switch(frame) {
        case 1:
            ring(r+w*3.4f,RGB(170,195,255),80,std::max(1.f,w*0.22f));
            ring(r+w*5.0f,RGB(170,195,255),45,std::max(1.f,w*0.18f));
            {const auto moon=polar(pi*0.15f,r+w*5.0f);planet(moon.X,moon.Y,w*0.55f,RGB(200,210,230));}
            glow(r+w*1.4f,RGB(120,170,255),220,w*0.55f,w*0.6f);
            {const auto at=polar(-pi/4,r+w*3.4f);sparkle(at.X,at.Y,w*2.2f,RGB(190,215,255),120);planet(at.X,at.Y,w*1.1f,RGB(90,150,255));}
            {const auto at=polar(pi*0.8f,r+w*3.4f);planet(at.X,at.Y,w*0.6f,RGB(230,200,170));}
            break;
        case 2:
            gloss(r+w*1.5f,w*2.4f,RGB(244,246,252),RGB(64,68,82));
            ring(r+w*0.25f,RGB(18,18,22),255,w*0.45f);
            ring(r+w*2.75f,RGB(26,26,32),220,w*0.3f);
            for(int i=0;i<9;++i){const float a=0.4f+float(i)*0.71f,size=w*(i%3 ? 0.32f : 0.5f);const auto c=polar(a,r+w*(1.1f+float(i%3)*0.4f));
                Gdiplus::SolidBrush pit(color(RGB(60,64,78),150));g.FillEllipse(&pit,c.X-size,c.Y-size,size*2,size*2);
                Gdiplus::Pen lip(color(RGB(255,255,255),110),std::max(1.f,size*0.35f));g.DrawArc(&lip,c.X-size,c.Y-size,size*2,size*2,20.f,140.f);}
            break;
        case 3:
            gloss(r+w*1.5f,w*2.2f,RGB(64,66,76),RGB(6,6,10));
            glowArc(r+w*1.5f,RGB(50,130,255),230,w*0.5f,w*0.7f,60.f,60.f);
            {const auto led=polar(pi/2,r+w*1.5f);sparkle(led.X,led.Y,w*1.6f,RGB(120,190,255),230);}
            break;
        case 4: {
            ellipseArc(r+w*4.4f,(r+w*4.4f)*0.42f,-25.f,RGB(190,170,255),70,std::max(1.f,w*0.25f),0.f,360.f);
            glow(r+w*1.3f,RGB(180,150,255),210,w*0.55f,w*0.55f);
            const float tilt=-25.f*pi/180.f,ox=(r+w*4.4f),oy=(r+w*4.4f)*0.42f;
            const auto onOrbit=[&](float a){const float px=std::cos(a)*ox,py=std::sin(a)*oy;return Gdiplus::PointF(x+px*std::cos(tilt)-py*std::sin(tilt),y+px*std::sin(tilt)+py*std::cos(tilt));};
            const auto gold=onOrbit(-0.5f),blue=onOrbit(-0.5f+pi);
            sparkle(gold.X,gold.Y,w*3.0f,RGB(255,214,140),200);planet(gold.X,gold.Y,w*1.25f,RGB(255,196,90));
            sparkle(blue.X,blue.Y,w*2.4f,RGB(160,200,255),200);planet(blue.X,blue.Y,w*0.95f,RGB(120,170,255));
            break;
        }
        case 5: {
            const float rx=r+w*6.2f,ry=(r+w*6.2f)*0.3f;
            ellipseArc(rx,ry,-18.f,RGB(230,200,140),90,w*1.6f,180.f,180.f);
            ring(r+w*1.2f,RGB(240,214,160),230,w*0.8f);
            ellipseArc(rx,ry,-18.f,RGB(250,220,160),230,w*1.6f,0.f,180.f);
            ellipseArc(rx-w*1.1f,ry-w*0.35f,-18.f,RGB(180,140,90),200,w*0.5f,0.f,180.f);
            ellipseArc(rx+w*0.9f,ry+w*0.3f,-18.f,RGB(255,236,200),150,w*0.35f,0.f,180.f);
            ellipseArc(rx-w*2.0f,ry-w*0.6f,-18.f,RGB(140,110,80),120,w*0.3f,0.f,180.f);
            break;
        }
        case 6:
            for(int i=9;i>=0;--i){const int k=i*255/9;ring(r+w*(1.2f+float(i)*0.5f),RGB(150+k/3,70+k/6,220-k/4),(10-i)*22,w*0.6f);}
            sparkle(polar(-pi*0.3f,r+w*4.4f).X,polar(-pi*0.3f,r+w*4.4f).Y,w*1.3f,RGB(255,220,250),220);
            for(int i=0;i<7;++i){const auto at=polar(0.6f+float(i)*0.83f,r+w*(2.2f+float(i%3)*1.2f));sparkle(at.X,at.Y,w*0.6f,RGB(255,235,255),190);}
            break;
        case 7: {
            glow(r+w*1.3f,RGB(225,232,255),220,w*0.55f,w*0.5f);
            const float angles[]={-1.57f,-1.1f,-0.5f,0.2f,0.9f,1.7f,2.4f,3.0f,3.7f,4.3f};
            for(int i=0;i<10;++i){const auto at=polar(angles[i],r+w*(2.6f+float(i%3)*1.0f));sparkle(at.X,at.Y,w*(i%2 ? 0.9f : 1.6f),RGB(235,240,255),230);}
            break;
        }
        case 8: {
            ring(r+w*1.2f,RGB(210,225,255),120,w*0.35f);
            for(int band=0;band<2;++band) {
                Gdiplus::PointF wave[97];
                for(int i=0;i<=96;++i){const float a=i*2*pi/96;wave[i]=polar(a,r+w*(2.6f+std::sin(a*5.f+band*1.4f)*(band ? 0.8f : 1.1f)));}
                for(int s=4;s>=0;--s){Gdiplus::Pen pen(color(band ? RGB(160,200,255) : RGB(240,246,255),s ? 40-s*6 : 230),w*(0.35f+float(s)*0.45f));pen.SetLineJoin(Gdiplus::LineJoinRound);g.DrawClosedCurve(&pen,wave,96);}
            }
            break;
        }
        case 9:
            glowArc(r+w*2.6f,RGB(255,140,40),220,w*0.9f,w*0.8f,-20.f,220.f);
            glowArc(r+w*2.6f,RGB(255,236,200),230,w*0.3f,w*0.2f,40.f,100.f);
            ring(r+w*1.2f,RGB(4,4,6),255,w*1.6f);
            ring(r+w*2.0f,RGB(255,180,90),160,std::max(1.f,w*0.25f));
            glowArc(r+w*2.05f,RGB(255,250,235),230,std::max(1.f,w*0.3f),w*0.25f,120.f,120.f);
            break;
        case 10:
            glowArc(r+w*1.6f,RGB(60,230,170),220,w*0.7f,w*0.7f,180.f,200.f);
            glowArc(r+w*3.0f,RGB(70,190,255),180,w*0.5f,w*0.6f,200.f,150.f);
            glowArc(r+w*4.2f,RGB(170,110,255),150,w*0.4f,w*0.5f,215.f,110.f);
            ring(r+w*1.1f,RGB(200,255,235),160,w*0.3f);
            for(int i=0;i<6;++i){const auto at=polar(-pi+0.35f+float(i)*0.5f,r+w*(5.2f+float(i%2)*0.8f));sparkle(at.X,at.Y,w*0.55f,RGB(220,255,240),200);}
            break;
        case 11: {
            ring(r+w*1.2f,RGB(200,225,255),200,w*0.5f);
            for(int i=0;i<24;++i){const float t=float(i)/23.f;arc(r+w*2.8f,RGB(150,210,255),int(20+200*t),w*(0.3f+1.4f*t),190.f+t*120.f,6.f);}
            const auto head=polar((190.f+126.f)*pi/180.f,r+w*2.8f);
            sparkle(head.X,head.Y,w*2.4f,RGB(230,245,255),255);
            for(int i=0;i<5;++i){const auto dust=polar((200.f+float(i)*22.f)*pi/180.f,r+w*(3.8f+float(i%2)*0.7f));sparkle(dust.X,dust.Y,w*0.45f,RGB(200,230,255),170);}
            break;
        }
        case 12: {
            ring(r+w*1.1f,RGB(190,210,255),110,w*0.3f);
            const float angles[]={-2.6f,-1.9f,-1.3f,-0.6f,0.1f,0.8f,1.6f,2.3f,3.0f};
            const float depth[]={2.4f,3.6f,2.6f,3.9f,2.8f,3.4f,2.5f,3.7f,2.9f};
            Gdiplus::PointF stars[9];
            for(int i=0;i<9;++i)stars[i]=polar(angles[i],r+w*depth[i]);
            Gdiplus::Pen link(color(RGB(170,200,255),150),std::max(1.f,w*0.22f));g.DrawLines(&link,stars,9);
            for(int i=0;i<9;++i)sparkle(stars[i].X,stars[i].Y,w*(i%3 ? 0.8f : 1.3f),RGB(235,242,255),240);
            break;
        }
        case 13:
            glow(r+w*1.5f,RGB(50,120,255),120,w*2.2f,w*0.5f);
            gloss(r+w*1.5f,w*2.2f,RGB(110,180,255),RGB(8,34,130));
            for(int i=0;i<12;++i){const float a=i*pi/6+0.13f;
                const Gdiplus::PointF facet[]={polar(a,r+w*0.45f),polar(a+0.26f,r+w*0.45f),polar(a+0.18f,r+w*2.55f),polar(a+0.05f,r+w*2.55f)};
                Gdiplus::SolidBrush face(color(i%2 ? RGB(255,255,255) : RGB(0,10,60),i%2 ? 40 : 55));g.FillPolygon(&face,facet,4);}
            {const auto glint=polar(-pi*0.72f,r+w*2.3f);sparkle(glint.X,glint.Y,w*1.4f,RGB(225,240,255),240);}
            break;
        case 14:
            for(int i=0;i<24;++i){const float a=i*pi/12+pi/24,len=i%2 ? 3.6f : 5.6f;
                const Gdiplus::PointF ray[]={polar(a-0.05f,r+w*1.6f),polar(a,r+w*len),polar(a+0.05f,r+w*1.6f)};
                Gdiplus::SolidBrush light(color(i%2 ? RGB(255,214,120) : RGB(255,244,210),i%2 ? 150 : 210));g.FillPolygon(&light,ray,3);}
            glow(r+w*1.3f,RGB(255,220,140),230,w*0.8f,w*0.6f);
            break;
        case 15:
            gloss(r+w*1.5f,w*2.0f,RGB(248,250,255),RGB(140,152,176));
            ring(r+w*0.35f,RGB(200,214,240),200,w*0.3f);
            {const auto top=polar(-pi/2,r+w*1.5f);sparkle(top.X,top.Y,w*2.8f,RGB(235,245,255),255);}
            {const auto side=polar(-pi/5,r+w*3.4f);sparkle(side.X,side.Y,w*1.1f,RGB(210,230,255),220);}
            {const auto low=polar(pi*0.85f,r+w*3.0f);sparkle(low.X,low.Y,w*0.8f,RGB(210,230,255),200);}
            break;
        default: break;
        }
        (void)tone;
    }
    static COLORREF lighterTone(COLORREF c) {return RGB((GetRValue(c)+255)/2,(GetGValue(c)+255)/2,(GetBValue(c)+255)/2);}
    void paintBackdrop(HDC dc,RECT area,unsigned backdrop,COLORREF tone) {
        if(!backdrop || backdrop>=backdrop_count)return;
        Gdiplus::Graphics g(dc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        const float r=float(p(waveStyle() ? 14 : 12)),x=float(area.left),y=float(area.top),w=float(area.right-area.left),h=float(area.bottom-area.top);
        Gdiplus::GraphicsPath rounded;
        rounded.AddArc(x,y,r*2,r*2,180,90);rounded.AddArc(x+w-r*2,y,r*2,r*2,270,90);
        rounded.AddArc(x+w-r*2,y+h-r*2,r*2,r*2,0,90);rounded.AddArc(x,y+h-r*2,r*2,r*2,90,90);rounded.CloseFigure();
        g.SetClip(&rounded);
        const auto color=[](COLORREF c,int a){return Gdiplus::Color(BYTE(std::clamp(a,0,255)),GetRValue(c),GetGValue(c),GetBValue(c));};
        const float unit=float(std::max(1,p(1)));
        const auto hash=[](uint32_t v){v^=v>>16;v*=0x7feb352du;v^=v>>15;v*=0x846ca68bu;v^=v>>16;return v;};
        const auto unitRandom=[&](uint32_t seed){return float(hash(seed)&0xffffu)/65535.f;};
        const auto sky=[&](COLORREF top,COLORREF bottom,int alpha){
            Gdiplus::LinearGradientBrush b(Gdiplus::PointF(0,y-1),Gdiplus::PointF(0,y+h+1),color(top,alpha),color(bottom,alpha));g.FillRectangle(&b,x,y,w,h);
        };
        const auto nebula=[&](float nx,float ny,float rx,float ry,COLORREF c,int alpha){
            Gdiplus::GraphicsPath cloud;cloud.AddEllipse(nx-rx,ny-ry,rx*2,ry*2);
            Gdiplus::PathGradientBrush b(&cloud);b.SetCenterColor(color(c,alpha));Gdiplus::Color edge=color(c,0);int n=1;b.SetSurroundColors(&edge,&n);
            g.FillPath(&b,&cloud);
        };
        const auto stars=[&](uint32_t seed,int count,COLORREF tint,float bigChance){
            for(int i=0;i<count;++i){
                const float sx=x+unitRandom(seed+uint32_t(i)*3u)*w,sy=y+unitRandom(seed+uint32_t(i)*3u+1u)*h,roll=unitRandom(seed+uint32_t(i)*3u+2u);
                const float size=unit*(roll>1.f-bigChance ? 2.4f : roll>0.6f ? 1.6f : 1.0f);
                Gdiplus::SolidBrush star(color(tint,roll>0.5f ? 230 : 150));g.FillEllipse(&star,sx-size/2,sy-size/2,size,size);
                if(roll>0.985f){Gdiplus::SolidBrush halo(color(tint,50));g.FillEllipse(&halo,sx-size*2,sy-size*2,size*4,size*4);}
            }
        };
        const auto shootingStar=[&](float sx,float sy,float length,float angle,COLORREF tint){
            const float ex=sx+std::cos(angle)*length,ey=sy+std::sin(angle)*length;
            Gdiplus::LinearGradientBrush tail(Gdiplus::PointF(sx,sy),Gdiplus::PointF(ex,ey),color(tint,0),color(tint,240));
            Gdiplus::Pen pen(&tail,unit*2.4f);pen.SetStartCap(Gdiplus::LineCapRound);pen.SetEndCap(Gdiplus::LineCapRound);
            g.DrawLine(&pen,sx,sy,ex,ey);
            Gdiplus::SolidBrush head(color(RGB(255,255,255),240));g.FillEllipse(&head,ex-unit*1.6f,ey-unit*1.6f,unit*3.2f,unit*3.2f);
        };
        const COLORREF ice=RGB(150,215,240);
        const int density=std::clamp(int(w*h/(unit*unit)/2600.f),40,420);
        switch(backdrop) {
        case 1:
            sky(RGB(4,18,36),RGB(2,12,26),235);
            stars(11u,density,ice,0.04f);
            shootingStar(x+w*0.16f,y+h*0.3f,w*0.16f,0.28f,ice);
            break;
        case 2:
            sky(RGB(12,10,40),RGB(4,4,20),235);
            nebula(x+w*0.62f,y+h*0.45f,w*0.34f,h*0.22f,RGB(120,90,255),110);
            nebula(x+w*0.62f,y+h*0.45f,w*0.12f,h*0.08f,RGB(230,220,255),150);
            stars(22u,density,RGB(210,200,255),0.04f);
            shootingStar(x+w*0.2f,y+h*0.2f,w*0.12f,0.35f,RGB(210,200,255));
            break;
        case 3:
            sky(RGB(30,6,30),RGB(10,2,16),235);
            nebula(x+w*0.7f,y+h*0.3f,w*0.3f,h*0.5f,RGB(255,80,170),110);
            nebula(x+w*0.35f,y+h*0.75f,w*0.25f,h*0.35f,RGB(255,150,90),80);
            stars(33u,density,RGB(255,210,230),0.04f);
            break;
        case 4:
            sky(RGB(2,24,22),RGB(2,10,14),235);
            nebula(x+w*0.55f,y+h*0.55f,w*0.38f,h*0.4f,RGB(40,220,150),100);
            nebula(x+w*0.8f,y+h*0.25f,w*0.18f,h*0.25f,RGB(120,255,220),80);
            stars(44u,density,RGB(190,255,230),0.04f);
            shootingStar(x+w*0.1f,y+h*0.55f,w*0.14f,-0.3f,RGB(190,255,230));
            break;
        case 5:
            sky(RGB(36,16,4),RGB(12,4,2),235);
            nebula(x+w*0.6f,y+h*0.5f,w*0.36f,h*0.45f,RGB(255,160,40),110);
            nebula(x+w*0.6f,y+h*0.5f,w*0.1f,h*0.14f,RGB(255,240,180),170);
            stars(55u,density,RGB(255,225,170),0.04f);
            break;
        case 6:
            sky(RGB(30,2,6),RGB(8,0,4),235);
            nebula(x+w*0.4f,y+h*0.4f,w*0.42f,h*0.5f,RGB(220,30,40),100);
            stars(66u,density,RGB(255,190,180),0.04f);
            shootingStar(x+w*0.55f,y+h*0.2f,w*0.18f,0.4f,RGB(255,200,190));
            break;
        case 7:
            sky(RGB(6,26,44),RGB(2,12,24),235);
            nebula(x+w*0.65f,y+h*0.4f,w*0.34f,h*0.4f,RGB(120,230,255),90);
            stars(77u,int(density*1.4f),RGB(220,248,255),0.05f);
            break;
        case 8:
            sky(RGB(20,6,40),RGB(6,2,16),235);
            nebula(x+w*0.3f,y+h*0.4f,w*0.3f,h*0.45f,RGB(170,80,255),110);
            nebula(x+w*0.75f,y+h*0.6f,w*0.28f,h*0.4f,RGB(80,120,255),100);
            stars(88u,density,RGB(225,205,255),0.04f);
            shootingStar(x+w*0.45f,y+h*0.15f,w*0.15f,0.25f,RGB(225,205,255));
            break;
        case 9: {
            sky(RGB(6,10,26),RGB(2,4,14),235);
            Gdiplus::Matrix tilt;tilt.RotateAt(-14.f,Gdiplus::PointF(x+w/2,y+h/2));g.SetTransform(&tilt);
            nebula(x+w*0.5f,y+h*0.5f,w*0.7f,h*0.16f,RGB(170,170,220),90);
            nebula(x+w*0.5f,y+h*0.5f,w*0.5f,h*0.07f,RGB(255,240,220),110);
            g.ResetTransform();
            stars(99u,int(density*1.6f),RGB(230,235,255),0.04f);
            break;
        }
        case 10: {
            sky(RGB(10,14,34),RGB(4,6,16),235);
            for(const auto [sx,sy,c]:{std::tuple{0.7f,0.35f,RGB(255,190,90)},{0.82f,0.55f,RGB(255,120,80)}}){
                nebula(x+w*sx,y+h*sy,w*0.14f,w*0.14f,c,120);
                Gdiplus::SolidBrush sun(color(RGB(255,250,230),240));const float sr=unit*(sx>0.75f ? 7.f : 10.f);g.FillEllipse(&sun,x+w*sx-sr,y+h*sy-sr,sr*2,sr*2);
            }
            stars(101u,density,ice,0.04f);
            break;
        }
        case 11:
            sky(RGB(4,30,40),RGB(40,24,10),235);
            nebula(x+w*0.5f,y+h*1.05f,w*0.6f,h*0.5f,RGB(255,180,80),110);
            stars(111u,density,RGB(220,240,255),0.04f);
            shootingStar(x+w*0.25f,y+h*0.25f,w*0.13f,0.2f,RGB(220,240,255));
            break;
        case 12: {
            sky(RGB(8,6,24),RGB(2,2,10),235);
            const float bx=x+w*0.66f,by=y+h*0.45f;
            nebula(bx,by,w*0.3f,w*0.3f,RGB(255,120,200),90);
            nebula(bx,by,w*0.12f,w*0.12f,RGB(160,220,255),130);
            for(int i=0;i<24;++i){const float a=i*3.14159f/12,len=w*(i%2 ? 0.1f : 0.18f);Gdiplus::Pen ray(color(RGB(255,240,250),i%2 ? 60 : 110),unit*(i%2 ? 1.f : 1.8f));g.DrawLine(&ray,bx,by,bx+std::cos(a)*len,by+std::sin(a)*len);}
            Gdiplus::SolidBrush core(color(RGB(255,255,255),250));g.FillEllipse(&core,bx-unit*5,by-unit*5,unit*10,unit*10);
            stars(121u,density,RGB(255,220,240),0.04f);
            break;
        }
        case 13: {
            sky(RGB(10,10,30),RGB(2,4,14),235);
            stars(131u,density,ice,0.04f);
            const float px=x+w*0.72f,py=y+h*0.5f,pr=std::min(w,h)*0.24f;
            Gdiplus::Pen backRing(color(RGB(230,200,150),150),unit*4);g.DrawArc(&backRing,px-pr*2,py-pr*0.5f,pr*4,pr,180.f,180.f);
            Gdiplus::GraphicsPath planet;planet.AddEllipse(px-pr,py-pr,pr*2,pr*2);
            Gdiplus::PathGradientBrush shade(&planet);shade.SetCenterPoint(Gdiplus::PointF(px-pr*0.4f,py-pr*0.4f));shade.SetCenterColor(color(RGB(250,200,130),255));
            Gdiplus::Color rim=color(RGB(120,60,40),255);int n=1;shade.SetSurroundColors(&rim,&n);g.FillPath(&shade,&planet);
            Gdiplus::Pen frontRing(color(RGB(250,225,180),220),unit*4);g.DrawArc(&frontRing,px-pr*2,py-pr*0.5f,pr*4,pr,0.f,180.f);
            break;
        }
        case 14:
            sky(RGB(4,14,34),RGB(2,6,18),235);
            stars(141u,density,ice,0.04f);
            for(int i=0;i<6;++i)shootingStar(x+w*(0.05f+0.15f*i),y+h*(0.1f+0.12f*float(i%3)),w*(0.08f+0.03f*float(i%2)),0.45f,ice);
            break;
        case 15: {
            sky(RGB(4,4,10),RGB(0,0,4),240);
            stars(151u,density,RGB(220,220,240),0.04f);
            const float hx=x+w*0.66f,hy=y+h*0.5f,hr=std::min(w,h)*0.14f;
            Gdiplus::Matrix tilt;tilt.RotateAt(-12.f,Gdiplus::PointF(hx,hy));g.SetTransform(&tilt);
            for(int i=0;i<5;++i){Gdiplus::Pen disk(color(i%2 ? RGB(255,170,60) : RGB(255,220,140),150-i*20),unit*(6-i));g.DrawEllipse(&disk,hx-hr*(2.6f+0.25f*i),hy-hr*(0.55f+0.06f*i),hr*(5.2f+0.5f*i),hr*(1.1f+0.12f*i));}
            g.ResetTransform();
            nebula(hx,hy,hr*1.8f,hr*1.8f,RGB(255,170,80),90);
            Gdiplus::SolidBrush hole(color(RGB(0,0,0),255));g.FillEllipse(&hole,hx-hr,hy-hr,hr*2,hr*2);
            Gdiplus::Pen photon(color(RGB(255,230,180),220),unit*1.5f);g.DrawEllipse(&photon,hx-hr,hy-hr,hr*2,hr*2);
            break;
        }
        default: break;
        }
        (void)tone;
        g.ResetClip();
    }
    void avatar(HDC dc, int cx, int cy, int radius, const std::wstring &name, const std::string &key, COLORREF ring,
                int presence = -1, bool dim = false, int frameOverride = -1) {
        const auto style=profileStyle(key);
        const auto own=style ? std::optional<online::Profile>{} : activeProfile();
        const auto remote=style || (own && own->id==key) ? nullptr : remoteCard(key);
        const unsigned accentIndex=style ? style->accent : remote && remote->card.accent<accent_count ? remote->card.accent : 0u;
        if((style || remote) && accentIndex==rgb_accent)keepProfileMotion();
        const auto* photo=style ? profileImage(style->image) : remote && !remote->image.frames.empty() ? &remote->image : nullptr;
        const bool animatedPhoto=photo && photo->frames.size()>1 && allowMotion() && radius>=p(16);
        if(animatedPhoto)keepProfileMotion();
        if(recordingMotion && dc==buffer.dc && (((style || remote) && accentIndex==rgb_accent) || effectOf(key) || animatedPhoto)) {
            const unsigned shownFrame=frameOverride>=0 ? unsigned(frameOverride) : style ? style->frame : remote ? remote->card.frame : 0u;
            const int outer=radius>=p(16) && shownFrame ? radius*9/5+p(4) : radius+std::max(2,radius/5)+std::max(1,radius/10)+p(3);
            recordMotion(dc,{cx-outer,cy-outer,cx+outer+1,cy+outer+1},[this,cx,cy,radius,name,key,ring,presence,dim,frameOverride](HDC target) {
                const bool recording=std::exchange(recordingMotion,false);
                avatar(target,cx,cy,radius,name,key,ring,presence,dim,frameOverride);
                recordingMotion=recording;
            });
        }
        const auto accent=style || remote ? (accentIndex==rgb_accent ? colorOf(cycling_accent(uint64_t(profileClock()))) : accentIndex ? colorOf(accents[accentIndex].color) : orange) : own && own->id==key ? orange : avatarTone(key);
        const COLORREF tone = dim ? mix(accent, panel, 150) : accent;
        if (const unsigned effect = effectOf(key)) {
            keepProfileMotion();
            const double now = profileClock();
            const int band = std::max(2, radius / 5);
            const auto glow = [&](double shift) {
                int red = 0, green = 0, blue = 0, samples = 0;
                for (size_t index = 0; index < 2; ++index)
                    for (const double back : {0.0, 45.0, 90.0, 135.0, 180.0}) {
                        const COLORREF sample = effectColor(effect, index, 8, now - back + shift);
                        red += GetRValue(sample); green += GetGValue(sample); blue += GetBValue(sample); ++samples;
                    }
                return RGB(red / samples, green / samples, blue / samples);
            };
            ellipse(dc, cx, cy, radius + band + std::max(1, radius / 10), mix(ring, glow(-300.0), 70));
            ellipse(dc, cx, cy, radius + band, readableName(storedColor(glow(0.0)), ink));
        } else
            ellipse(dc, cx, cy, radius + std::max(1, radius / 8), ring);
        ellipse(dc, cx, cy, radius, tone);
        const auto image=style ? profileImage(style->image) : remote && !remote->image.frames.empty() ? &remote->image : nullptr;
        if(image) {
            paintRoundPhoto(dc,*image,cx,cy,std::max(1,radius-std::max(1,p(2))),style ? style->pictureFit : 0u);
        } else {
        auto old = SelectObject(dc, radius >= p(18) ? fonts.strong : fonts.label);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, dim ? mix(RGB(255, 255, 255), panel, 110) : RGB(255, 255, 255));
        const auto letters = style && !style->initials.empty() ? wide(style->initials) : remote && !remote->card.initials.empty() ? wide(remote->card.initials) : initials(name);
        RECT r{cx - radius, cy - radius, cx + radius + 1, cy + radius + 1};
        DrawTextW(dc, letters.c_str(), int(letters.size()), &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(dc, old);
        }
        if (radius >= p(16)) {
            const unsigned frame = frameOverride >= 0 ? unsigned(frameOverride) : style ? style->frame : remote ? remote->card.frame : 0u;
            const unsigned frameColor = frameOverride >= 0 ? unsigned(std::max(0, selected(ProfileFrameColor))) : style ? style->frameColor : remote ? remote->card.frame_color : 0u;
            paintFrame(dc, cx, cy, radius + std::max(1, radius / 8), frame, tone, frameTint(frameColor));
        }
        if (presence >= 0) {
            const int dot = std::max(p(3), radius * 3 / 10), x = cx + radius * 7 / 10, y = cy + radius * 7 / 10;
            ellipse(dc, x, y, dot + std::max(1, p(2)), ring);
            ellipse(dc, x, y, dot, presence == 2 ? gold : presence == 1 ? green : faint);
        }
    }
    void avatarGlyph(HDC dc, RECT r, int kind, COLORREF color) {
        navigationIcon(dc, kind == 0 ? OnlineTab : HomeProfile, r, mix(color, ink, 90), std::max(8, int(std::min(r.right - r.left, r.bottom - r.top)) / 2));
    }
    void tag(HDC dc, int right, int cy, const std::wstring &value, COLORREF tone, bool solid = false) {
        auto old = SelectObject(dc, fonts.label);
        SIZE size{};
        GetTextExtentPoint32W(dc, value.c_str(), int(value.size()), &size);
        const RECT pill{right - size.cx - p(14), cy - p(9), right, cy + p(9)};
        box(dc, pill, solid ? tone : mix(panel, tone, 46), solid ? tone : mix(panel, tone, 90), 6);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, solid ? buttonInk : mix(tone, ink, 90));
        RECT text = pill;
        DrawTextW(dc, value.c_str(), int(value.size()), &text, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(dc, old);
    }
    int tagWidth(HDC dc, const std::wstring &value) {
        auto old = SelectObject(dc, fonts.label);
        SIZE size{};
        GetTextExtentPoint32W(dc, value.c_str(), int(value.size()), &size);
        SelectObject(dc, old);
        return size.cx + p(14);
    }
    void heading(HDC dc, const std::wstring &value, int x, int y, int w) {
        text(dc, value, x, y, w, 22, fonts.strong, ink, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
    int cut(int radius) const { return std::max(2, p(radius) * 3 / 5); }
    void box(HDC dc,RECT rect,COLORREF color,COLORREF outline=border,int radius=10) {
        smoothBox(dc,rect,color,outline,float(p(radius)));
    }
    void panelBox(HDC dc, int left, int top, int right, int bottom) {
        if (waveStyle()) {
            Gdiplus::Graphics graphics(dc);
            graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            const Gdiplus::REAL x=Gdiplus::REAL(p(left)),y=Gdiplus::REAL(p(top)),w=Gdiplus::REAL(p(right)-p(left)),h=Gdiplus::REAL(p(bottom)-p(top)),r=Gdiplus::REAL(p(14));
            Gdiplus::GraphicsPath rounded;
            rounded.AddArc(x,y,r*2,r*2,180,90);rounded.AddArc(x+w-r*2,y,r*2,r*2,270,90);
            rounded.AddArc(x+w-r*2,y+h-r*2,r*2,r*2,0,90);rounded.AddArc(x,y+h-r*2,r*2,r*2,90,90);rounded.CloseFigure();
            const bool tinted=settings.launcherColors[2]>=0;
            const COLORREF sky=waveSkyTop();
            const COLORREF tone=tinted ? panel : mix(sky,RGB(14,12,24),120),lower=tinted ? mix(panel,RGB(0,0,0),40) : mix(sky,RGB(4,4,10),170);
            const double rise=pageEnter(std::clamp(double(top-124)*0.25,0.0,160.0));
            const BYTE upper=BYTE(176*(0.45+0.55*rise)),bottomAlpha=BYTE(150*(0.45+0.55*rise));
            Gdiplus::LinearGradientBrush glass(Gdiplus::PointF(0,y),Gdiplus::PointF(0,y+h+1),Gdiplus::Color(upper,GetRValue(tone),GetGValue(tone),GetBValue(tone)),Gdiplus::Color(bottomAlpha,GetRValue(lower),GetGValue(lower),GetBValue(lower)));
            graphics.FillPath(&glass,&rounded);
            graphics.SetClip(&rounded);
            Gdiplus::LinearGradientBrush shine(Gdiplus::PointF(0,y-1),Gdiplus::PointF(0,y+Gdiplus::REAL(p(48))),Gdiplus::Color(BYTE(18+60*(1.0-rise)),255,240,220),Gdiplus::Color(0,255,240,220));
            graphics.FillRectangle(&shine,x,y,w,Gdiplus::REAL(p(48)));
            graphics.ResetClip();
            Gdiplus::Pen edge(Gdiplus::Color(64,255,232,200),float(std::max(1,p(1))));
            graphics.DrawPath(&edge,&rounded);
            if(rise<1.0) {
                const Gdiplus::REAL glint=x+w*Gdiplus::REAL(rise)*1.2f-w*0.1f,span=std::max<Gdiplus::REAL>(Gdiplus::REAL(p(60)),w*0.18f);
                Gdiplus::LinearGradientBrush sweep(Gdiplus::PointF(glint-span,0),Gdiplus::PointF(glint+span,0),Gdiplus::Color(0,255,236,200),Gdiplus::Color(0,255,236,200));
                Gdiplus::Color stops[]={Gdiplus::Color(0,255,236,200),Gdiplus::Color(BYTE(170*(1.0-rise)),255,240,214),Gdiplus::Color(0,255,236,200)};
                Gdiplus::REAL at[]={0.0f,0.5f,1.0f};
                sweep.SetInterpolationColors(stops,at,3);
                Gdiplus::Pen glow(&sweep,float(std::max(1,p(2))));
                graphics.SetClip(Gdiplus::RectF(glint-span,y-2,span*2,h+4));
                graphics.DrawPath(&glow,&rounded);
                graphics.ResetClip();
            }
            return;
        }
        const int darkness=(GetRValue(background)*299+GetGValue(background)*587+GetBValue(background)*114)/1000;
        fill(dc, {p(left) + cut(12), p(bottom), p(right) - cut(12), p(bottom) + p(1)}, mix(background, RGB(0, 0, 0), darkness>128 ? 24 : 100));
        box(dc, {p(left), p(top), p(right), p(bottom)}, panel, mix(panel, border, 150), 12);
    }
    void paintTagTheme(HDC dc,identity::Tag tag,int left,int top,int right,int bottom) {
        const auto theme=name_style::tag_theme(tag);
        Gdiplus::Graphics graphics(dc);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        const Gdiplus::REAL x=Gdiplus::REAL(p(left)),y=Gdiplus::REAL(p(top)),w=Gdiplus::REAL(p(right)-p(left)),h=Gdiplus::REAL(p(bottom)-p(top)),r=Gdiplus::REAL(p(waveStyle() ? 14 : 12));
        Gdiplus::GraphicsPath rounded;
        rounded.AddArc(x,y,r*2,r*2,180,90);rounded.AddArc(x+w-r*2,y,r*2,r*2,270,90);
        rounded.AddArc(x+w-r*2,y+h-r*2,r*2,r*2,0,90);rounded.AddArc(x,y+h-r*2,r*2,r*2,90,90);rounded.CloseFigure();
        const auto color=[](name_style::Rgb c,int alpha){return Gdiplus::Color(BYTE(alpha),BYTE(c.r),BYTE(c.g),BYTE(c.b));};
        Gdiplus::LinearGradientBrush wash(Gdiplus::PointF(0,y-1),Gdiplus::PointF(0,y+h+1),color(theme.top,150),color(theme.bottom,40));
        graphics.FillPath(&wash,&rounded);
        graphics.SetClip(&rounded);
        const Gdiplus::REAL glowHeight=Gdiplus::REAL(p(150));
        Gdiplus::LinearGradientBrush glow(Gdiplus::PointF(0,y-1),Gdiplus::PointF(0,y+glowHeight),color(theme.glow,46),color(theme.glow,0));
        graphics.FillRectangle(&glow,x,y,w,glowHeight);
        Gdiplus::LinearGradientBrush stripe(Gdiplus::PointF(x,0),Gdiplus::PointF(x+w,0),color(theme.edge,0),color(theme.edge,200));
        stripe.SetBlendTriangularShape(0.5f);
        graphics.FillRectangle(&stripe,x,y,w,Gdiplus::REAL(std::max(2,p(3))));
        graphics.ResetClip();
        Gdiplus::Pen edge(color(theme.edge,150),float(std::max(1,p(2))));
        graphics.DrawPath(&edge,&rounded);
        text(dc,theme.name,left+16,bottom-30,right-left-32,18,fonts.label,readableName(storedColor(styleColor(theme.label)),ink),DT_CENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
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
        const bool current = paintedScroll == pageScroll && paintedPage == page;
        if (current && buffer.dc && bounds.right <= buffer.frame.width && bounds.bottom <= buffer.frame.height)
            BitBlt(dc, r.left, r.top, r.right - r.left, r.bottom - r.top, buffer.dc, bounds.left + r.left,
                   bounds.top + r.top, SRCCOPY);
        else
            fill(dc, r, fallback);
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
        bool pageControl = false, wanted = false;
        int pageY = 0, pageH = 0, adjust = 0;
    };
    std::map<int, Placement> *planning = nullptr;
    std::map<int, Placement> scrollPlan;
    std::vector<int> scrollOrder;
    int scrollPlanPage = -1;
    std::pair<LONG, LONG> scrollPlanSize{};
    bool scrollPlanReady = false, pageDirty = true, scrollGliding = false;
    double scrollAt = 0;
    int scrollGoal = 0;
    std::chrono::steady_clock::time_point scrollTickAt{};
    std::thread glideThread;
    std::mutex glideMutex;
    std::condition_variable glideWake;
    std::atomic<bool> glideActive{false}, glideTickPending{false}, glideStop{false};
    std::vector<uint32_t> pageCache;
    int pageCachePage = -1, pageCacheWidth = 0, pageCacheHeight = 0;
    void conceal(int id) {
        if (planning) {
            if (auto found = planning->find(id); found != planning->end())
                found->second.visible = found->second.wanted = false;
        } else
            ShowWindow(controls.at(id), SW_HIDE);
    }
    void place(int id, int x, int y, int w, int h, bool visible = true) {
        auto c = controls.at(id);
        const bool pageControl = pageControlId(id);
        if (page == wavePage || waveRedundant(id) || (controlsDeferred && pageControl)) visible = false;
        const int pageY = y, pageH = h;
        const bool wanted = visible;
        if (pageControl) {
            y += shellHeader - pageScroll;
            const int viewportBottom = shellHeader + pageVisibleHeight();
            visible = visible && y + h > shellHeader && y < viewportBottom;
        }
        wchar_t type[32]{};
        GetClassNameW(c, type, 32);
        if (_wcsicmp(type,L"EDIT")==0 && !(GetWindowLongPtrW(c,GWL_STYLE)&ES_MULTILINE) && h>24) {
            y+=(h-22)/2;h=22;
        }
        if (_wcsicmp(type,L"LISTBOX")==0) h=std::min(h,std::max(100,pageVisibleHeight()-40));
        int allocatedHeight = p(h);
        if (lstrcmpiW(type, L"ComboBox") == 0) {
            const auto rows = std::min<LRESULT>(8, SendMessageW(c, CB_GETCOUNT, 0, 0));
            allocatedHeight = std::max(allocatedHeight, p(36)) + int(rows) * p(34) + p(8);
            SendMessageW(c, CB_SETITEMHEIGHT, WPARAM(-1), p(h - 6));
            SendMessageW(c, CB_SETITEMHEIGHT, 0, p(34));
            if(!comboTextWidths.count(id)) {
                auto dc=GetDC(c);auto previous=SelectObject(dc,fonts.body);int longest=0;
                const auto count=SendMessageW(c,CB_GETCOUNT,0,0);
                for(LRESULT row=0;row<count;++row) {
                    const auto length=SendMessageW(c,CB_GETLBTEXTLEN,row,0);
                    if(length<0 || length>32767)continue;
                    std::wstring value(size_t(length)+1,L'\0');SendMessageW(c,CB_GETLBTEXT,row,LPARAM(value.data()));
                    SIZE size{};GetTextExtentPoint32W(dc,value.data(),int(length),&size);longest=std::max(longest,int(size.cx));
                }
                SelectObject(dc,previous);ReleaseDC(c,dc);comboTextWidths[id]=longest+p(48);
            }
            MONITORINFO screen{sizeof(screen)};GetMonitorInfoW(MonitorFromWindow(c,MONITOR_DEFAULTTONEAREST),&screen);
            const int popupLimit=std::max(p(w),int(screen.rcWork.right-screen.rcWork.left)-p(24));
            SendMessageW(c,CB_SETDROPPEDWIDTH,std::min(popupLimit,std::max(p(w),comboTextWidths[id])),0);
            const int choice=selected(id);const auto length=SendMessageW(c,CB_GETLBTEXTLEN,choice,0);
            if(length>=0 && length<32768 && (!hintText.count(id) || comboValueHints.count(id))) {
                std::wstring value(size_t(length)+1,L'\0');SendMessageW(c,CB_GETLBTEXT,choice,LPARAM(value.data()));value.resize(size_t(length));
                if(comboTextWidths[id]>p(w) || comboValueHints.count(id)) {hint(id,value);comboValueHints.insert(id);}
            }
        }
        if (planning) {
            (*planning)[id] = Placement{p(x), p(y), p(w), allocatedHeight, visible, pageControl, wanted, pageY, pageH,
                                        pageControl ? y - (pageY + shellHeader - pageScroll) : 0};
            return;
        }
        SetWindowPos(c, nullptr, p(x), p(y), p(w), allocatedHeight,
                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
        const bool shown = (GetWindowLongPtrW(c, GWL_STYLE) & WS_VISIBLE) != 0;
        if (visible != shown)
            ShowWindow(c, visible ? SW_SHOWNA : SW_HIDE);
    }
    void hint(int id, std::wstring value) {
        if (!hoverHints)
            return;
        auto found = hintText.find(id);
        if (found != hintText.end() && found->second == value)
            return;
        const bool added = found != hintText.end();
        auto& text = hintText[id];
        text = std::move(value);
        TOOLINFOW tool{TTTOOLINFOW_V2_SIZE};
        tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
        tool.hwnd = hwnd;
        tool.uId = reinterpret_cast<UINT_PTR>(controls.at(id));
        tool.lpszText = text.data();
        const auto result = SendMessageW(hoverHints, added ? TTM_UPDATETIPTEXTW : TTM_ADDTOOLW,
                                        0, reinterpret_cast<LPARAM>(&tool));
        if (!added && !result) {
            hintText.erase(id);
            std::cerr << "[launcher] could not register hover hint for control " << id << '\n';
        }
    }
    void initializeHints() {
        hoverHints = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hwnd, nullptr, nullptr, nullptr);
        if (!hoverHints)
            return;
        SendMessageW(hoverHints, WM_SETFONT, reinterpret_cast<WPARAM>(fonts.caption), FALSE);
        SendMessageW(hoverHints, TTM_SETMAXTIPWIDTH, 0, p(360));
        SendMessageW(hoverHints, TTM_SETDELAYTIME, TTDT_INITIAL, 600);
        hint(OpenHelp,L"Keyboard shortcuts, game setup and support tools (F1).");
        hint(Play, L"Play game (F5). Verify your ISO first.");
        hint(ChooseIso, L"Choose your Budokai 2 USA ISO (Ctrl+O). You can also drop it onto the launcher.");
        hint(VerifyIso, L"Check the selected disc and required game files.");
        hint(HomeProfile, L"Customize your profile picture, name color, main fighter and tagline (Ctrl+P).");
        hint(OpenNotifications,L"Friend requests and match invitations (Ctrl+Shift+N).");
        hint(HomeFriendScope,L"Show your friends, friends who are online, everyone online, or players you recently played.");
        hint(HomeFindPlayers,L"Find players and manage friends on Online.");
        hint(HomeInvite,L"Invite the selected online friend. You can also double-click a friend to join or invite them.");
        hint(HomeCreateRoom,L"Choose a room name, visibility and password on Online before hosting.");
        hint(ProfileImageChoose,L"Choose your own PNG, JPEG or animated GIF, up to 16 MB. You can also drop a picture onto your Profile page.");
        hint(ProfileImageRemove,L"Return to initials. Your original picture is kept.");
        hint(ProfileNameColor,L"Color your name in player lists. Very dark or very light colors are adjusted so everyone can read them.");
        hint(ProfileFighter,L"Show your main fighter on your player card.");
        hint(ProfileSharing,L"Choose what players online see: your picture, accent, initials, tagline, name color and main fighter.");
        hint(ProfileRename,L"Rename the active profile to the name typed beside it. A [TTV], [YT] or [ToB] tag stays with the new name.");
        hint(PlayerChannel,L"Open this player's Twitch, YouTube or Kick channel in your browser.");
        hint(ProfileLink,L"Your Twitch, YouTube or Kick link. Only profiles with a [DEV], [TTV], [YT] or [ToB] key can share one.");
        hint(PlayerProfile,L"Open the selected player's profile. You can also right-click any name.");
        hint(ProfilePreview,L"See your player card the way other players see it.");
        hint(ProfileAbout,L"Up to 160 characters. Shown on your player profile.");
        hint(PlayerBlock,L"Blocked players cannot send you invites or friend requests, and are listed last.");
        hint(ProfileFriendAlerts,L"Show a desktop alert when a friend comes online. Quiet mode and Do not disturb silence it.");
        hint(RefreshRooms,L"Refresh rooms and ping, or reconnect to the service (Ctrl+R).");
        hint(OnlineSearch,L"Search room names, hosts and player names. Clear restores the full lists.");
        hint(RoomFilter,L"Show matching versions by default, all versions, or rooms without a password.");
        hint(PlayerFilter,L"Show everyone or only your friends who are online.");
        hint(HomeControls, L"Change keyboard and controller bindings.");
        hint(HomeSaves, L"Import saves and manage recovery backups.");
        hint(HomeSettings, L"Display, sound, graphics and launcher preferences.");
        hint(HomeArrange, L"Reorder or hide tabs in the bottom dock.");
        hint(OpenLogs, L"Open launcher and game logs (Ctrl+L).");
        hint(SplitImageChoose,L"Choose your own PNG, JPEG or GIF, up to 16 MB. Animated GIFs are supported. You can also drop an image onto this page.");
        hint(SplitNameSave,L"Save this split's name without editing recorded times.");
        hint(SplitStyleReset,L"Restore this row's default name and remove its image. Recorded times and original image files are kept.");
        hint(OverlayDefaults,L"Reset overlay layout, colors and heading. Custom split names and images are kept.");
        hint(TabDefaults,L"Restore the default dock order and show all tabs.");
        for (int id : {CloseImporter, CloseOverlay, CloseTabs, CloseColors})
            hint(id, L"Back (Alt+Left or Esc).");
    }
    bool navigateBack() {
        if (page < pageCount && page != savesPage && page != controlsPage)
            return false;
        navigatingBack = true;
        page = page == helpPage ? helpReturnPage : page == finderPage ? finderReturnPage : page == profilePage ? profileReturnPage : page == playerPage ? playerReturnPage : navigationPage();
        if (waveStyle() && page == 0) { page = wavePage; waveEnter = 0.0; }
        layout();
        if (page == wavePage) startWave();
        (pageDirty=true),InvalidateRect(hwnd, nullptr, FALSE);
        SetFocus(waveStyle() ? hwnd : controls.at(PlayTab + navigationPage()));
        return true;
    }
    void initialize() {
        settings.tabOrder = normalize_dock_order(settings.tabOrder);
        wavePalette = waveStyle();
        waveTint = waveSkyTop();
        apply_theme(settings.launcherTheme, settings.launcherAccent, settings.launcherColors);
        if (settings.startPage && settings.lastPage < tab_count && !((settings.hiddenTabs >> settings.lastPage) & 1u))
            page = int(settings.lastPage);
        if (waveStyle() && page == 0) page = wavePage;
        presence.enable(settings.discordPresence);
        fonts.make(dpi);
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
                                                          {ModInstall, L"Install mod…"},
                                                          {ModExport, L"Export mod…"},
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
                                                          {ControlsTab, L"Controls"},
                                                          {ControlsReset, L"Reset to defaults"},
                                                          {OnlineTab, L"Online"},
                                                          {SpeedrunTab, L"Speedrun"},
                                                          {HostGame, L"Host room"},
                                                          {JoinGame, L"Join address"},
                                                          {ProfileAdd, L"Add"},
                                                          {ProfileRename, L"Rename"},
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
                                                          {TabDefaults, L"Reset tabs"},
                                                          {ArrangeTabs, L"Arrange tabs…"},
                                                          {HomeProfile, L"Manage profile"},
                                                          {HomeFindPlayers,L"Players"},
                                                          {HomeInvite,L"Invite"},
                                                          {HomeCreateRoom,L"Create room"},
                                                          {HomeControls, L"Controls"},
                                                          {HomeSaves, L"Saves & backups"},
                                                          {HomeSettings, L"Graphics & audio"},
                                                          {HomeArrange, L"Arrange tabs"},
                                                          {ProfileRecover,L"Undo removal"},
                                                          {ProfileManage,L"Manage online profiles"},
                                                          {RaSignIn,L"Sign in"},{RaSignOut,L"Sign out"},{RaCopyHash,L"Copy disc hash"},
                                                          {RaOpenSet,L"Open set page"},{RaRefresh,L"Refresh"},{RaOpenSite,L"Create an account"},
                                                          {ProfileCopyStyle,L"Copy appearance"},
                                                          {ProfilePreview,L"Preview my card"},
                                                          {PlayerAction,L"Invite to play"},
                                                          {PlayerFriend,L"Add friend"},
                                                          {PlayerBlock,L"Block"},
                                                          {PlayerCopyId,L"Copy profile ID"},
                                                          {PlayerProfile,L"Profile"},
                                                          {PlayerChannel,L"Open channel"},
                                                          {ProfileSave, L"Save profile"},
                                                          {ProfileReset, L"Use defaults"},
                                                          {ProfileCopyId, L"Copy profile ID"},
                                                          {ProfileImageChoose, L"Choose picture…"},
                                                          {ProfileImageRemove, L"Remove picture"},
                                                          {ClearOnlineSearch, L"Clear"},
                                                          {OpenColors, L"Colors…"},
                                                          {ColorPick, L"Change color…"},
                                                          {ColorReset, L"Use theme color"},
                                                          {ColorResetAll, L"Reset all colors"},
                                                          {CloseColors, L"Back to settings"},
                                                          {ColorCopy, L"Copy colors"},
                                                          {ColorPaste, L"Paste colors"},
                                                          {ColorSurprise, L"Surprise me"},
                                                          {OpenHelp,L"Help & shortcuts"},{CopySetup,L"Copy setup summary"},{OpenIsoFolder,L"Open ISO folder"},
                                                          {OpenLogs, L"Open logs folder"},
                                                          {OverlayDefaults, L"Reset overlay"},
                                                          {SplitStyleAdd, L"Add split row"},
                                                          {SplitStyleReset, L"Use default"},
                                                          {SplitNameSave, L"Save name"},
                                                          {SplitImageChoose, L"Choose image…"},
                                                          {SplitImageRemove, L"Remove image"},
                                                          {ClearImage, L"Crisp"},
                                                          {ChooseIso, L"Choose ISO…"},
                                                          {FindAction, L"Find an action"},
                                                          {ActionOpen, L"Open"},
                                                          {ActionClose, L"Back"},
                                                          {BackToPlay, L"Back to Play"},
                                                          {ClearSaveSearch, L"Clear"},
                                                          {ClearSettingsSearch, L"Clear"},
                                                          {ResetDisplay, L"Reset display"},
                                                          {ResetGame, L"Reset game"},
                                                          {UndoSettings, L"Undo change"},
                                                          {SettingsAll,L"All settings"},
                                                          {SettingsDisplay,L"Display"},
                                                          {SettingsGraphics,L"Graphics"},
                                                          {SettingsGame,L"Game"},
                                                          {SettingsLauncher,L"Launcher"},
                                                          {OpenNotifications,L"Notifications"},
                                                          {NavigateBack,L"Back"},
                                                          {VerifyIso, L"Verify ISO"},
                                                          {Play, L"Play"},
                                                          {LoadState, L"Load state"},
                                                          {ReleaseNotes, L"All updates"},
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
        combo(Antialiasing, {L"Off", L"FXAA", L"FXAA High", L"FXAA Ultra", L"NFAA", L"DLAA (long edges)"}, int(std::min(settings.antialiasing, 5u)));
        combo(Filtering, {L"Game default", L"Nearest (sharp)", L"Bilinear (3D surfaces)"},
              int(settings.textureFilter));
        combo(Upscaler, {L"Nearest", L"Bilinear", L"Bicubic", L"AMD FSR 1", L"Lanczos", L"Sharp pixels", L"Mitchell (smooth)", L"Integer scaling"}, int(std::min(settings.upscaler, 7u)));
        combo(InternalScale, {L"Native (1x)", L"2x native", L"3x native", L"4x native", L"Auto (match display)"},
              settings.internalScale == 0 ? 4 : int(std::clamp(settings.internalScale, 1u, 4u) - 1u));
        combo(HdTextures, {L"Off", L"Upscaled from your disc", L"Texture packs", L"Texture packs, upscale the rest", L"Dump textures for pack makers"},
              int(std::min(settings.hdTextures, 4u)));
        combo(Music, {L"On", L"Off (voices and sound effects stay on)"}, settings.music ? 0 : 1);
        combo(Vsync, {L"Off", L"On"}, settings.vsync ? 1 : 0);
        combo(ImageEffect, {L"None", L"Sharpen", L"Vibrant colors", L"CRT scanlines", L"Sharpen + vibrant", L"Soft glow"}, int(std::min(settings.imageEffect, 5u)));
        combo(Brightness, {L"Darker", L"Normal", L"Brighter", L"Much brighter"}, int(std::min(settings.brightness, 3u)));
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
            std::vector<std::wstring> delays{L"Auto"};
            for (unsigned d = 1; d <= 8; ++d)
                delays.push_back(std::to_wstring(d) + (d == 1 ? L" frame" : L" frames"));
            combo(OnlineDelay, delays, int(settings.onlineDelay));
        }
        combo(OnlineListed, {L"Public", L"Invite only", L"Not listed"}, int(settings.onlineVisibility));
        combo(OnlineProfile, {}, 0);
        combo(HomeFriendScope,{L"All friends",L"Online friends",L"Everyone online",L"Recent opponents"},int(std::min(settings.homeFriendScope,3u)));
        homeScope=int(std::min(settings.homeFriendScope,3u));
        combo(RoomFilter,{L"Same version",L"All versions",L"No password"},int(settings.onlineRoomFilter));
        combo(PlayerFilter,{L"All players",L"Friends online"},int(settings.onlinePlayerFilter));
        edit(OnlineSearch,L"Search rooms or players");
        edit(ActionSearch, L"Search actions, settings and tools");
        edit(SaveSearch, L"Search saves or backup filenames");
        edit(SettingsSearch, L"Find a settings section");
        list(ActionList, L"Actions", 42, false);
        combo(SaveSort, {L"Newest first", L"Oldest first", L"Name"}, 0);
        combo(DiscordActivity,{L"Off",L"On"},settings.discordPresence ? 1 : 0);
        combo(LauncherMotion,{L"Smooth",L"Reduced motion"},settings.reducedMotion ? 1 : 0);
        combo(LauncherHints,{L"On",L"Off"},settings.helpHints ? 0 : 1);
        combo(LauncherInterface,{L"Classic",L"Wave home (PS3 inspired)"},int(settings.launcherInterface));
        combo(RaHardcore,{L"Softcore: save states allowed",L"Hardcore: no save states, mods or CPU changes"},settings.raHardcore ? 1 : 0);
        {std::vector<std::wstring> names;for(const char* name:frame_names)names.emplace_back(wide(name));combo(ProfileFrame,names,0);}
        {std::vector<std::wstring> names;for(const char* name:backdrop_names)names.emplace_back(wide(name));combo(ProfileBackdrop,names,0);}
        {std::vector<std::wstring> names;for(const auto& title:title_choices)names.emplace_back(wide(title.name));combo(ProfileTitle,names,0);}
        {std::vector<std::wstring> names;for(const auto& color:frame_colors)names.emplace_back(wide(color.name));combo(ProfileFrameColor,names,0);}
        {std::vector<std::wstring> names{L"No featured achievement"};for(const auto& definition:achievements::definitions)names.emplace_back(wide(definition.title));combo(ProfileFeatured,names,0);}
        combo(WaveSky,{L"Time of day",L"Saiyan sunset",L"Namek",L"Kaio-ken",L"Kami's lookout",L"Frieza night",L"Ki blue",
                       L"Time chamber",L"Planet Vegeta",L"Kame House dusk",L"Majin pink",L"Starry night"},int(settings.waveSky));
        combo(WaveEmblem,{L"Shown",L"Hidden"},settings.waveEmblem ? 0 : 1);
        combo(LauncherClock,{L"Windows format",L"12-hour",L"24-hour",L"Hidden"},int(settings.launcherClock));
        combo(WaveRibbons,{L"Spiral galaxy",L"Nebula drift",L"Milky Way",L"Star voyage",L"Still sky"},galaxyIndex(settings.waveRibbons));
        hint(LauncherClock,L"How the clock at the top of the launcher shows the time.");
        hint(WaveRibbons,L"The animated space scene behind the home screen. Other pages show it still. Still sky keeps only the stars.");
        hint(WaveSky,L"Colors of the wave home and the sky behind every page. Time of day changes through the day.");
        hint(WaveEmblem,L"Show the four-star Dragon Ball behind the wave home and pages.");
        hint(LauncherInterface,L"Wave home opens on an animated menu bar. Press Esc on any main page to return to it.");
        SendMessageW(controls[OnlineSearch],EM_SETLIMITTEXT,128,0);
        {std::vector<std::wstring> names;for(const auto& choice:accents)names.emplace_back(choice.name);combo(ProfileAccent,names,0);}
        combo(ProfileAvailability,{L"Available",L"Away",L"Do not disturb"},0);
        {std::vector<std::wstring> names{L"Theme default"};for(const auto& choice:name_style::name_colors)names.emplace_back(choice.name);names.emplace_back(L"Custom color\u2026");combo(ProfileNameColor,names,0);}
        {std::vector<std::wstring> names{L"No main fighter"};for(const auto* name:fighter_names)names.push_back(wide(name));combo(ProfileFighter,names,0);}
        combo(ProfileSharing,{L"Picture and card",L"Card without the picture",L"Keep on this PC"},0);
        combo(ProfileFriendAlerts,{L"Tell me",L"Don't tell me"},0);
        {std::vector<std::wstring> names{L"Not shown"};for(const auto* name:region_names)names.push_back(wide(name));combo(ProfileRegion,names,0);}
        {std::vector<std::wstring> names{L"Not shown"};for(const auto* name:playstyle_names)names.push_back(wide(name));combo(ProfilePlaystyle,names,0);}
        edit(ProfileAbout,L"A few words about you, your mains or when you play");
        edit(ProfileLink,L"https://www.twitch.tv/yourname or a YouTube or Kick link");
        SendMessageW(controls[ProfileLink],EM_SETLIMITTEXT,120,0);
        SendMessageW(controls[ProfileAbout],EM_SETLIMITTEXT,160,0);
        combo(ProfileFit,{L"Fill the circle",L"Fit the whole picture",L"Fill, keep the top",L"Zoom in"},0);
        combo(ProfileNotifications,{L"Sound & desktop alerts",L"Quiet — launcher inbox only"},0);
        combo(ProfileCopyFrom,{},0);
        edit(ProfileInitials,L"Up to two letters");edit(ProfileTagline,L"A short tagline");
        SendMessageW(controls[ProfileInitials],EM_SETLIMITTEXT,2,0);
        SendMessageW(controls[ProfileTagline],EM_SETLIMITTEXT,60,0);
        fillProfiles();
        combo(OverlayCorner, {L"Top left", L"Top right", L"Bottom left", L"Bottom right"}, int(settings.overlayCorner));
        combo(OverlaySize, {L"Small", L"Medium", L"Large", L"Extra large"}, int(std::min(settings.overlaySize, 3u)));
        combo(OverlayBackground, {L"None", L"Light", L"Dark", L"Faint", L"Solid"}, int(std::min(settings.overlayBackground, 4u)));
        combo(OverlayMargin, {L"Tight", L"Normal", L"Wide"}, int(std::min(settings.overlayMargin, 2u)));
        combo(OverlayShape, {L"Rounded", L"Square"}, int(std::min(settings.overlayShape, 1u)));
        edit(OverlayTitle, L"Title, like My Any% run");
        SetWindowTextW(controls[OverlayTitle], wide(settings.overlayTitle).c_str());
        SendMessageW(controls[OverlayTitle], EM_LIMITTEXT, 48, 0);
        edit(SplitNameField, L"Split name (blank uses the default)");
        SendMessageW(controls[SplitNameField], EM_LIMITTEXT, 48, 0);
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
        combo(StartPage, {L"Home", L"Last page used"}, int(std::min(settings.startPage, 1u)));
        combo(RandomizerMode, {L"Off", L"On"}, settings.randomizer ? 1 : 0);
        combo(LoadTimes, {L"Fastest (this PC)", L"Accurate (PS2)"}, settings.ps2LoadTimes ? 1 : 0);
        combo(RunAhead, {L"Off", L"1 frame", L"2 frames"}, int(std::min(settings.runAhead, 2u)));
        {
            std::vector<std::wstring> padNames;
            for (int code : padChoices)
                padNames.push_back(wide(input::pad_button_names[size_t(code)]));
            for (unsigned i = 0; i < input::button_count; ++i) {
                button(ControlKey + int(i), L"");
                button(ControlAlternate + int(i), L"");
                combo(ControlPad + int(i), padNames, 0);
            }
            refreshControls();
        }
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
        list(SplitStyleList, L"Split appearances", 40, false);
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
        edit(RaUser, L"RetroAchievements username");
        edit(RaPassword, L"Password");
        SetWindowLongPtrW(controls[RaPassword], GWL_STYLE, GetWindowLongPtrW(controls[RaPassword], GWL_STYLE) | ES_PASSWORD);
        SendMessageW(controls[RaPassword], EM_SETPASSWORDCHAR, WPARAM(L'\u2022'), 0);
        SetWindowTextW(controls[RaUser], wide(settings.raUser).c_str());
        edit(OnlineLobby, L"Server address");
        edit(JoinPassword, L"Only for locked rooms");
        list(RoomList, L"Rooms", 50, false);
        list(PlayerList, L"Players", 50, false);
        list(FriendList, L"Friends", 44, false);
        onlineShared->window = hwnd;
        SetWindowTextW(controls[OnlinePort], std::to_wstring(settings.onlinePort).c_str());
        SetWindowTextW(controls[OnlineAddress], wide(settings.onlineAddress).c_str());
        SetWindowTextW(controls[OnlineRoom], wide(settings.onlineRoom).c_str());
        SetWindowTextW(controls[OnlinePassword], wide(settings.onlinePassword).c_str());
        SetWindowTextW(controls[OnlineLobby], wide(settings.onlineLobby).c_str());
        for (auto [id, limit] : {std::pair{RaUser, 64}, {RaPassword, 128}, {OnlineName, 24}, {OnlinePort, 5}, {OnlineAddress, 255}, {OnlineRoom, 40},
                                 {OnlinePassword, 64}, {OnlineLobby, 255}, {JoinPassword, 64}})
            SendMessageW(controls[id], EM_SETLIMITTEXT, WPARAM(limit), 0);
        for (auto [id, c] : controls)
            SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(fonts.body), TRUE);
        SendMessageW(controls[TimingRules], WM_SETFONT, reinterpret_cast<WPARAM>(fonts.caption), TRUE);
        refreshStyleRows();
        initializeHints();
        SendMessageW(hoverHints,TTM_ACTIVATE,settings.helpHints,0);
        SetTimer(hwnd, 1, 75, nullptr);
        refreshMods();
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
        for (int id : {SaveSearch, ClearSaveSearch}) EnableWindow(controls[id], !busy() && !running());
        EnableWindow(controls[SaveSort], !busy() && !running() && !gameSaveList());
        EnableWindow(controls[UndoSettings], previousSettings.has_value());
        EnableWindow(controls[OpenIsoFolder],!settings.iso.empty());
        for (int id : {Play, ImportFolder, ImportFile, Backup, OpenSaves, OpenBackups, UnlockAll, HostGame, JoinGame, JoinRoom})
            EnableWindow(controls[id], active && (!room || id == (roomHost ? HostGame : directJoin ? JoinGame : JoinRoom)));
        EnableWindow(controls[LoadState], active && !room && settings.speedrun == 0 && stateAge().has_value());
        EnableWindow(controls[Play], !busy() && !running() && !room);
        SetWindowTextW(controls[ChooseIso], L"Choose ISO…");
        SetWindowTextW(controls[VerifyIso], L"Verify ISO");
        SetWindowTextW(controls[Play], disc ? L"Play game" : L"Choose ISO");
        SetWindowTextW(controls[HostGame], room && roomHost ? L"Close room" : L"Host room");
        SetWindowTextW(controls[JoinRoom], room && !roomHost && !directJoin ? L"Cancel" : L"Join room");
        SetWindowTextW(controls[JoinGame], room && !roomHost && directJoin ? L"Cancel" : L"Join address");
        const bool profileEditable=!nameCheck && !busy() && !running() && !room;
        EnableWindow(controls[ProfileRemove], !settings.onlineProfiles.empty() && profileEditable);
        EnableWindow(controls[ProfileAdd], profileEditable);
        EnableWindow(controls[ProfileRename], profileEditable && activeProfile().has_value());
        EnableWindow(controls[OnlineProfile],!settings.onlineProfiles.empty() && profileEditable);
        EnableWindow(controls[ProfileRecover],profileEditable && std::filesystem::exists(profileRecoveryPath()));
        EnableWindow(controls[ProfileCopyStyle],settings.onlineProfiles.size()>1 && profileEditable);
        EnableWindow(controls[ProfileCopyFrom],settings.onlineProfiles.size()>1 && profileEditable);
        EnableWindow(controls[OnlineName],profileEditable);
        EnableWindow(controls[NoticeAccept],hasNotice() && !busy() && !running());
        EnableWindow(controls[NoticeDecline],hasNotice());
        SetWindowTextW(controls[ProfileAdd], nameCheck ? L"Checking\u2026" : L"Add");
        const bool online = directory && directory->connected();
        const auto player=selectedPlayerId();
        const auto roomRow=SendMessageW(controls[RoomList],LB_GETCURSEL,0,0);
        const bool joinable=roomRow>=0 && size_t(roomRow)<roomRows.size() && roomRows[size_t(roomRow)].version==application_version && roomRows[size_t(roomRow)].id!=hostingRoom;
        if(!room)EnableWindow(controls[JoinRoom],active && joinable);
        EnableWindow(controls[InvitePlayer], online && disc.has_value() && !busy() && !player.empty());
        EnableWindow(controls[AddFriend], online && !player.empty());
        EnableWindow(controls[PlayerProfile], !player.empty());
        EnableWindow(controls[HomeInvite],online && disc.has_value() && !busy() && !selectedPlayerId().empty() &&
            directory->player(selectedPlayerId()).has_value());
        EnableWindow(controls[HomeFindPlayers],true);
        EnableWindow(controls[HomeCreateRoom],!busy());
        EnableWindow(controls[RefreshRooms], !connecting.valid());
        SetWindowTextW(controls[RefreshRooms],connecting.valid() ? L"Connecting…" : online ? L"Refresh" : L"Reconnect");
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
        for (int id : {ModCreate, OpenImporter, SourceGame, SourceIso, ModInstall})
            EnableWindow(controls[id], modding && (id != SourceGame || !settings.iso.empty()));
        EnableWindow(controls[ModExport], modding && SendMessageW(controls[ModList], LB_GETCURSEL, 0, 0) >= 0);
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
        EnableWindow(controls[LoadTimes], settings.speedrun == 0);
        SendMessageW(controls[LoadTimes], CB_SETCURSEL, settings.speedrun || settings.ps2LoadTimes ? 1 : 0, 0);
        EnableWindow(controls[RunAhead], settings.speedrun == 0);
        SendMessageW(controls[RunAhead], CB_SETCURSEL, settings.speedrun ? 0 : int(std::min(settings.runAhead, 2u)), 0);
        for (int id : {UpdateChannel, AutomaticUpdates, CheckUpdates, InstallUpdate})
            EnableWindow(controls[id], !updating() &&
                         (id != InstallUpdate || (!busy() && !running() &&
                          (release.has_value() || staged.has_value()))));
        const auto profile=activeProfile();
        const auto appearance=profile ? profileStyle(profile->id) : nullptr;
        EnableWindow(controls[ProfileImageChoose],profile.has_value());
        EnableWindow(controls[ProfileImageRemove],appearance && !appearance->image.empty());
        EnableWindow(controls[ProfileCopyId],profile.has_value());
        EnableWindow(controls[ClearOnlineSearch],!textOf(OnlineSearch).empty());
        const bool editingSplit = settings.speedrun != 0;
        for (int id : {SplitStyleList,SplitNameField,SplitNameSave,SplitStyleAdd,SplitImageChoose,SplitImageRemove,SplitStyleReset})
            if (controls.count(id)) EnableWindow(controls[id], (editingSplit || id == SplitStyleList) &&
                (id != SplitStyleAdd || styleRowCount() < 256) &&
                (id != SplitImageRemove || (splitStyle(size_t(styleSelection)) && !splitStyle(size_t(styleSelection))->image.empty())));
        (pageDirty=true),InvalidateRect(hwnd, nullptr, FALSE);
    }
    void layout() {
        if (layingOut) {layoutAgain=true;return;}
        if (IsIconic(hwnd)) return;
        layingOut=true;
        struct FinishLayout {
            Window& window;
            ~FinishLayout() {
                window.layingOut=false;
                if (window.layoutAgain) {
                    window.layoutAgain=false;window.layoutPending=true;
                    PostMessageW(window.hwnd,pageClipMessage,0,0);
                }
            }
        } finish{*this};
        if (dropdownOpen()) {
            if (laidOutPage == page) {
                layoutPending = true;
                return;
            }
            for (const auto& [id, control] : controls) {
                wchar_t type[32]{};
                if (GetClassNameW(control, type, 32) && _wcsicmp(type, L"ComboBox") == 0 &&
                    SendMessageW(control, CB_GETDROPPEDSTATE, 0, 0))
                    SendMessageW(control, CB_SHOWDROPDOWN, FALSE, 0);
            }
        }
        layoutPending = false;
        RECT rect{};
        GetClientRect(hwnd, &rect);
        if (rect.right<=0 || rect.bottom<=0) return;
        const bool pageChanged=laidOutPage!=page;
        if(pageChanged){
            pageScroll=0;laidOutPage=page;scrollGliding=false;glideActive=false;motions.clear();motionTick=false;pageDirty=true;
            pageEnterAt=0;
            if(waveStyle() && page!=wavePage && allowMotion() && shownPage>=0) {
                controlsDeferred=true;
                if(hwnd)frames(9);
            }
        }
        for (unsigned pass=0;pass<3;++pass) {
            const auto previous=rect;
            const int canvas=pageCanvasHeight();
            pageScroll=std::clamp(pageScroll,0,canvas-pageVisibleHeight());
            SetPropW(hwnd,L"OpenKai2.ScrollPos",reinterpret_cast<HANDLE>(intptr_t(scrollReported())));
            SetPropW(hwnd,L"OpenKai2.ScrollMax",reinterpret_cast<HANDLE>(intptr_t(canvas-1)));
            SetPropW(hwnd,L"OpenKai2.ScrollPage",reinterpret_cast<HANDLE>(intptr_t(pageVisibleHeight())));
            GetClientRect(hwnd,&rect);
            if (EqualRect(&previous,&rect)) break;
        }
        const int w = unscale(rect.right), clientHeight = unscale(rect.bottom), h = pageCanvasHeight();
        const int content = w - contentLeft - margin, right = contentLeft + content;
        std::vector<int> order{PlayTab, OnlineTab, SpeedrunTab, ModsTab, SavesTab, SettingsTab, UpdatesTab, ControlsTab};
        std::map<int, Placement> plan;
        planning = &plan;
        auto position = [&](int id, int x, int y, int width, int height) {
            place(id, x, y, width, height);
            order.push_back(id);
        };
        updateDockMotion();
        SetPropW(controls[OnlineSearch],L"OpenKai2.EditHint",reinterpret_cast<HANDLE>(const_cast<wchar_t*>(page==0 ? L"Search rooms or hosts" : L"Search rooms or players")));
        const auto dockTabs = visibleTabs();
        for (size_t slot = 0; slot < dockTabs.size(); ++slot)
            hint(PlayTab + dockTabs[slot], std::wstring(tab_names[dockTabs[slot]]) +
                L" (Ctrl+" + std::to_wstring(slot + 1) + L")");
        const auto selectedIso = candidateIso.empty() ? settings.iso : candidateIso;
        if (!selectedIso.empty())
            hint(ChooseIso, selectedIso.wstring() + L"\nChoose another ISO with Ctrl+O, or drop it onto the launcher.");
        const ui::DockLayout dock(w,clientHeight,int(dockTabs.size()));
        const int dockCell=dock.cell,dockLeft=dock.left;
        for (int i = 0; i < pageCount; ++i) {
            const auto tab = std::find(dockTabs.begin(), dockTabs.end(), i);
            place(PlayTab + i, dockLeft + int(tab - dockTabs.begin()) * dockCell,
                  clientHeight - 104, dockCell, 56, tab != dockTabs.end() && !waveStyle());
        }
        position(OpenNotifications,w-margin-40,22,40,34);
        if(page==notificationsPage && hasNotice()) {
            position(NoticeAccept,contentLeft+24,454,160,40);
            position(NoticeDecline,contentLeft+200,454,140,40);
        }
        if(page==helpPage) {
            SetWindowTextW(controls[NavigateBack],L"Back");position(NavigateBack,right-150,28,150,36);
            position(CopySetup,contentLeft+24,794,180,40);
            position(OpenLogs,contentLeft+218,794,164,40);
            position(OpenIsoFolder,contentLeft+396,794,172,40);
        } else if (page == finderPage) {
            position(ActionClose,right-166,28,166,36);
            position(ActionSearch, contentLeft + 30, 212, content - 60, 24);
            position(ActionList, contentLeft + 20, 258, content - 40, h - 366);
            if (actionRows.empty()) conceal(ActionList);
            position(ActionOpen, right - 160, h - 94, 140, 38);
        } else if(page==notificationsPage) {
            SetWindowTextW(controls[NavigateBack],L"Back to Online");
            position(NavigateBack,right-166,28,166,36);
        } else if(page==achievementsPage) {
            SetWindowTextW(controls[NavigateBack],waveStyle() ? L"Back to Home" : L"Back to Play");
            position(NavigateBack,right-166,28,166,36);
            const int row=324,gap=12;
            if(settings.raToken.empty()) {
                const int field=std::clamp((content-48-2*gap-160-190-2*gap)/2,160,320);
                position(RaUser,contentLeft+34,row,field-20,40);
                position(RaPassword,contentLeft+34+field+gap,row,field-20,40);
                position(RaSignIn,contentLeft+24+2*(field+gap),row,160,40);
                position(RaOpenSite,contentLeft+24+2*(field+gap)+160+gap,row,190,40);
            } else {
                const int combo=std::clamp(content-48-4*(150+gap),260,440);
                position(RaHardcore,contentLeft+24,row,combo,40);
                int x=contentLeft+24+combo+gap;
                for(const int id:{RaRefresh,RaCopyHash,RaOpenSet,RaSignOut}){position(id,x,row,150,40);x+=150+gap;}
            }
        } else if(page==playerPage) {
            SetWindowTextW(controls[NavigateBack],L"Back");
            position(NavigateBack,right-166,28,166,36);
            const int split=contentLeft+std::clamp(content/3,260,340),buttonWidth=split-contentLeft-48;
            const auto profile=activeProfile();const bool own=profile && profile->id==viewedPlayer;
            position(PlayerAction,contentLeft+24,536,buttonWidth,36);
            if(!own)position(PlayerFriend,contentLeft+24,580,buttonWidth,36);
            position(PlayerCopyId,contentLeft+24,own ? 580 : 624,buttonWidth,36);
            if(!own)position(PlayerBlock,contentLeft+24,668,buttonWidth,36);
            if(!channelOf(viewedPlayer).empty())position(PlayerChannel,contentLeft+24,own ? 624 : 712,buttonWidth,36);
        } else if (page == 0) {
            const LoungeLayout g(content,h);
            position(HomeFriendScope,g.friendsLeft+16,154,g.friendsWidth-32,32);
            if(g.compact) {
                position(FriendList,g.friendsLeft+12,202,g.friendsWidth-24,std::max(80,g.friendsBottom-258));
                const int third=(g.friendsWidth-48)/3;
                position(HomeFindPlayers,g.friendsLeft+16,g.friendsBottom-44,third,30);
                position(HomeInvite,g.friendsLeft+24+third,g.friendsBottom-44,third,30);
                SetWindowTextW(controls[HomeProfile],activeProfile() ? L"Profile" : L"Create");
                position(HomeProfile,g.friendsLeft+32+2*third,g.friendsBottom-44,third,30);
            } else {
                position(FriendList,g.friendsLeft+12,202,g.friendsWidth-24,std::max(80,g.friendsBottom-(g.friendsBottom-392>=160 ? 392 : 370)));
                const int friendAction=(g.friendsWidth-40)/2;
                position(HomeFindPlayers,g.friendsLeft+16,g.friendsBottom-144,friendAction,30);
                position(HomeInvite,g.friendsLeft+24+friendAction,g.friendsBottom-144,friendAction,30);
                SetWindowTextW(controls[HomeProfile],activeProfile() ? L"Customize profile" : L"Create profile");
                position(HomeProfile,g.friendsLeft+16,g.friendsBottom-40,g.friendsWidth-32,28);
            }
            if(friendRows.empty())conceal(FriendList);
            const int gameActionY=280;
            position(Play,g.centre+24,gameActionY,154,44);
            position(VerifyIso,g.centre+194,gameActionY+4,120,36);
            if(!settings.speedrun && stateAge())position(LoadState,g.centre+330,gameActionY+4,124,36);
            const int toolWidth=(g.centreWidth()-12)/2;
            position(HomeSaves,g.centre,g.gameBottom+16,toolWidth,60);
            position(HomeControls,g.centre+toolWidth+12,g.gameBottom+16,g.centreWidth()-toolWidth-12,60);
            position(HomeCreateRoom,g.centreRight-134,g.roomsTop+12,116,28);
            const int queryWidth=g.centreWidth()-250;
            position(OnlineSearch,g.centre+28,g.roomsTop+61,queryWidth-20,22);
            position(ClearOnlineSearch,g.centre+26+queryWidth,g.roomsTop+54,52,36);
            position(RoomFilter,g.centreRight-164,g.roomsTop+54,146,36);
            SendMessageW(controls[RoomList],LB_SETITEMHEIGHT,0,p(46));
            position(RoomList,g.centre+12,g.roomsTop+102,g.centreWidth()-24,std::max(48,g.bottom-g.roomsTop-116));
            if(roomRows.empty())conceal(RoomList);
            if(selectedRoomLocked())position(JoinPassword,g.details+20,g.bottom-104,g.detailsWidth()-40,30);
            position(JoinRoom,g.details+20,g.bottom-62,g.detailsWidth()-40,36);
            position(ReleaseNotes,g.right-136,g.notesTop+25,112,28);
        } else if (page == profilePage) {
            const auto back=waveStyle() && profileReturnPage==0 ? std::wstring(L"Back to Home") : L"Back to "+std::wstring(tab_names[profileReturnPage]);
            SetWindowTextW(controls[NavigateBack],back.c_str());
            position(NavigateBack,right-166,28,166,36);
            const int split=contentLeft+std::clamp(content/3,260,340),side=split+44,width=right-side-24;
            {
                const bool recover=std::filesystem::exists(profileRecoveryPath());
                const int buttons=recover ? 4 : 3,button=96,gap=10,group=buttons*button+(buttons-1)*gap;
                const int pick=std::clamp((content-48-group)/3,180,320);
                position(OnlineProfile,contentLeft+24,166,pick,40);
                const int nameLeft=contentLeft+24+pick+16,nameRight=right-24-group-gap;
                position(OnlineName,nameLeft+10,166,std::max(80,nameRight-nameLeft-20),40);
                int x=right-24-group;
                for(const int id:{ProfileAdd,ProfileRename,ProfileRemove}){position(id,x,166,button,40);x+=button+gap;}
                if(recover)position(ProfileRecover,x,166,button,40);
            }
            if(waveStyle()) {
                const auto g=profileWave(content,heldTag().has_value());
                const auto placeRow=[&](int id,int left,int edge,int labelWidth,int y,bool field=false) {
                    const int valueLeft=left+24+labelWidth,valueWidth=edge-24-valueLeft;
                    position(id,valueLeft+(field ? 10 : 0),y+8,valueWidth-(field ? 20 : 0),40);
                };
                const int lookRight=g.left+g.half,label=g.label(g.half);
                const int look[]={ProfileFrame,ProfileFrameColor,ProfileBackdrop,ProfileTitle,ProfileFeatured,ProfileAccent,ProfileNameColor};
                for(int i=0;i<7;++i)placeRow(look[i],g.left,lookRight,label,g.rowY(g.look,i));
                const int about[]={ProfileInitials,ProfileFighter,ProfileTagline,ProfileAbout,ProfileRegion,ProfilePlaystyle};
                for(int i=0;i<6;++i)placeRow(about[i],g.rightLeft,g.right,label,g.rowY(g.look,i),about[i]==ProfileInitials || about[i]==ProfileTagline || about[i]==ProfileAbout);
                placeRow(ProfileAvailability,g.left,lookRight,label,g.rowY(g.privacy,0));
                placeRow(ProfileNotifications,g.left,lookRight,label,g.rowY(g.privacy,1));
                placeRow(ProfileSharing,g.rightLeft,g.right,label,g.rowY(g.privacy,0));
                placeRow(ProfileFriendAlerts,g.rightLeft,g.right,label,g.rowY(g.privacy,1));
                position(ProfileSave,g.left+24,g.actions+20,170,40);
                position(ProfileReset,g.left+24+184,g.actions+20,140,40);
                position(ProfilePreview,g.left+24+338,g.actions+20,180,40);
                const int wideLabel=std::clamp(content/4,180,280),copyLeft=g.left+24+wideLabel,copyWidth=g.right-24-copyLeft;
                position(ProfileCopyFrom,copyLeft,g.actions+84,copyWidth-176,40);
                position(ProfileCopyStyle,g.right-24-160,g.actions+84,160,40);
                if(heldTag())position(ProfileLink,copyLeft+10,g.actions+140,copyWidth-20,40);
                position(ProfileCopyId,g.pictureLeft,g.hero+24,260,36);
                position(ProfileImageChoose,g.pictureLeft,g.hero+68,260,36);
                position(ProfileImageRemove,g.pictureLeft,g.hero+112,260,36);
                position(ProfileFit,g.pictureLeft,g.hero+182,260,40);
            } else {
            const int half=(width-16)/2,second=side+(width+16)/2;
            position(ProfileInitials,side,330,half,40);
            position(ProfileAccent,second,330,half,40);
            position(ProfileNameColor,side,410,half,40);
            position(ProfileFighter,second,410,half,40);
            position(ProfileTagline,side,490,width,40);
            position(ProfileAbout,side,570,width,40);
            position(ProfileRegion,side,650,half,40);
            position(ProfilePlaystyle,second,650,half,40);
            position(ProfileAvailability,side,730,half,40);
            position(ProfileNotifications,second,730,half,40);
            position(ProfileSharing,side,810,half,40);
            position(ProfileFriendAlerts,second,810,half,40);
            position(ProfileSave,side,872,160,40);
            position(ProfileReset,side+174,872,130,40);
            position(ProfilePreview,side+318,872,170,40);
            position(ProfileCopyId,contentLeft+24,480,split-contentLeft-48,36);
            position(ProfileImageChoose,contentLeft+24,532,split-contentLeft-48,36);
            position(ProfileImageRemove,contentLeft+24,580,split-contentLeft-48,36);
            position(ProfileFit,contentLeft+24,664,split-contentLeft-48,40);
            position(ProfileCopyFrom,side,990,width-176,40);
            position(ProfileCopyStyle,side+width-160,990,160,40);
            if(heldTag())position(ProfileLink,side,1124,width,40);
            }
            if(!waveStyle()) {
                const int third=(content-48-32)/3;
                position(ProfileFrame,contentLeft+24,1196+70,third,40);
                position(ProfileBackdrop,contentLeft+24+third+16,1196+70,third,40);
                position(ProfileTitle,contentLeft+24+2*(third+16),1196+70,third,40);
                position(ProfileFrameColor,contentLeft+24,1196+150,third,40);
                position(ProfileFeatured,contentLeft+24+third+16,1196+150,2*third+16,40);
            }
        } else if (page == savesPage) {
            SetWindowTextW(controls[BackToPlay], waveStyle() ? L"Back to Home" : L"Back to Play");
            position(BackToPlay, right - 150, 28, 150, 34);
            const int panelBottom = h - 64;
            position(AutoBackup, right - 20 - 160, 142, 160, 34);
            const int gap = 12, action = (content - 40 - 3 * gap) / 4;
            int x = contentLeft + 20;
            for (int id : {ImportFile, ImportFolder, Backup, UnlockAll}) {
                position(id, x, 246, action, 36);
                x += action + gap;
            }
            position(BackupList, contentLeft + 20, 430, content - 40, std::max(60, panelBottom - 84 - 430));
            if (emptySaveList())
                conceal(BackupList);
            position(SaveListKind, right - 180, 342, 160, 32);
            position(SaveSearch, contentLeft + 30, 390, content - 310, 24);
            position(ClearSaveSearch, right - 260, 382, 64, 38);
            position(SaveSort, right - 180, 382, 160, 38);
            if (!gameSaveList())
                position(Restore, contentLeft + 20, panelBottom - 52, 130, 36);
            position(OpenBackups, contentLeft + (gameSaveList() ? 20 : 162), panelBottom - 52, 120, 36);
            position(SelectSaves, right - 292, panelBottom - 52, 100, 36);
            position(DeleteSelected, right - 180, panelBottom - 52, 160, 36);
        } else if (page == modsPage) {
            const int panelBottom = h - 64;
            position(OpenImporter, right - 20 - 140, 142, 140, 36);
            position(ModFolder, right - 20 - 140 - 12 - 160, 142, 160, 36);
            position(ModExport, right - 20 - 140 - 12 - 160 - 12 - 130, 142, 130, 36);
            position(ModInstall, right - 20 - 140 - 12 - 160 - 12 - 130 - 12 - 130, 142, 130, 36);
            if (waveStyle()) {
                const int inner = content - 40;
                position(ModList, contentLeft + 20, 268, inner, 260);
                int x = contentLeft + 20;
                for (auto [id, width] : {std::pair{ModToggle, 150}, {ModUp, 100}, {ModDown, 110}, {ModRefresh, 90}}) {
                    position(id, x, 540, width, 36);
                    x += width + 10;
                }
                const int name = std::max(120, right - 20 - 120 - 10 - (x + 6));
                position(ModName, right - 20 - 120 - 10 - name + 10, 548, name - 20, 22);
                position(ModCreate, right - 20 - 120, 540, 120, 36);
                position(ModContents, contentLeft + 20, 656, inner, 200);
                position(ModRemove, right - 20 - 150, 868, 150, 36);
                position(ModDelete, right - 20 - 310, 868, 150, 36);
                position(ModRename, right - 20 - 470, 868, 150, 36);
                if (modList.empty())
                    for (int id : {ModList, ModContents, ModRename, ModDelete, ModRemove})
                        conceal(id);
            } else {
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
            }
        } else if (page == speedrunPage) {
            const Grid grid(content);
            const bool wave = waveStyle();
            const bool compact = h < 700 && !wave;
            const int splitsTop = wave ? 444 : classicSplitsTop;
            const int rulesTop = wave ? splitsTop + (speedrunRulesTop - classicSplitsTop) : compact ? 496 : speedrunRulesTop;
            const int buttonHeight = compact ? 30 : 34;
            if (wave) {
                const int labelWidth = std::clamp(content * 2 / 5, 180, 420);
                const int valueLeft = contentLeft + 24 + labelWidth, valueWidth = right - 24 - valueLeft, rows = speedrunTop + 64;
                position(SpeedrunMode, valueLeft, rows + 8, valueWidth, 34);
                position(Permadeath, valueLeft, rows + waveOptionRow + 8, valueWidth, 34);
                position(SpeedrunRandomizer, valueLeft, rows + 2 * waveOptionRow + 8, valueWidth, 34);
                position(SpeedrunCpu, valueLeft, rows + 3 * waveOptionRow + 8, valueWidth, 34);
                position(OpenOverlay, right - 20 - 190, speedrunTop + 10, 190, 34);
            } else {
            position(SpeedrunMode, grid.x(0), Grid::field(speedrunTop, 0), grid.column, 34);
            position(Permadeath, grid.x(1), Grid::field(speedrunTop, 0), grid.column, 34);
            position(SpeedrunCpu, grid.x(0), Grid::field(speedrunTop, 1), grid.column, 34);
            position(SpeedrunRandomizer, grid.x(2), Grid::field(speedrunTop, 0), grid.column, 34);
            position(OpenOverlay, right - 20 - 170, speedrunTop + 10, 170, 28);
            }
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
            SendMessageW(controls[RoomList],LB_SETITEMHEIGHT,0,p(50));
            const Grid grid(content);
            const int bottom = h - 64;
            const int profileRow = Grid::field(onlineProfileTop - 20, 0), action = 84;
            (void)action;
            SetWindowTextW(controls[HomeProfile],activeProfile() ? L"Customize profile" : L"Create profile");
            position(HomeProfile,grid.x(1),profileRow,grid.column,36);
            if (waveStyle()) position(ProfileCopyId,grid.x(1),profileRow,grid.x(2)+grid.column-grid.x(1),36);
            else position(ProfileCopyId,grid.x(2),profileRow,grid.column,36);
            if (waveStyle()) {
                const int inner = content - 40, labelWidth = std::clamp(content * 2 / 5, 180, 420);
                const int valueLeft = contentLeft + 24 + labelWidth, valueWidth = right - 24 - valueLeft;
                position(PlayerFilter, right - 20 - 240, waveOnlinePlayersTop + 10, 240, 32);
                position(PlayerList, contentLeft + 20, waveOnlinePlayersTop + 56, inner, 300);
                const int third = 160;
                position(PlayerProfile, right - 20 - third, waveOnlinePlayersTop + 368, third, 34);
                position(AddFriend, right - 20 - 2 * third - 10, waveOnlinePlayersTop + 368, third, 34);
                position(InvitePlayer, right - 20 - 3 * third - 20, waveOnlinePlayersTop + 368, third, 34);
                position(RefreshRooms, right - 20 - 110, waveOnlineRoomsTop + 10, 110, 32);
                const int searchWidth = inner - 200 - 64;
                position(OnlineSearch, contentLeft + 30, waveOnlineRoomsTop + 61, searchWidth - 20, 22);
                position(ClearOnlineSearch, contentLeft + 20 + searchWidth + 4, waveOnlineRoomsTop + 56, 56, 32);
                position(RoomFilter, right - 20 - 190, waveOnlineRoomsTop + 56, 190, 32);
                position(RoomList, contentLeft + 20, waveOnlineRoomsTop + 100, inner, 220);
                const int half = (inner - 12) / 2;
                if (selectedRoomLocked()) {
                    position(JoinPassword, contentLeft + 30, waveOnlineRoomsTop + 340, half / 2 - 36, 22);
                    position(JoinRoom, contentLeft + 20 + half / 2, waveOnlineRoomsTop + 334, half - half / 2, 34);
                } else {
                    position(JoinRoom, contentLeft + 20, waveOnlineRoomsTop + 334, half, 34);
                    conceal(JoinPassword);
                }
                position(OnlineAddress, contentLeft + 30 + half + 12, waveOnlineRoomsTop + 340, half / 2 - 36, 22);
                position(JoinGame, contentLeft + 20 + half + 12 + half / 2, waveOnlineRoomsTop + 334, half - half / 2, 34);
                position(HostGame, right - 20 - 160, waveOnlineHostTop + 10, 160, 34);
                const int rows = waveOnlineHostTop + 64;
                position(OnlineRoom, valueLeft + 10, rows + 15, valueWidth - 20, 22);
                position(OnlinePassword, valueLeft + 10, rows + waveOptionRow + 15, valueWidth - 20, 22);
                position(OnlineDelay, valueLeft, rows + 2 * waveOptionRow + 8, valueWidth, 34);
                position(OnlineListed, valueLeft, rows + 3 * waveOptionRow + 8, valueWidth, 34);
                if (roomRows.empty()) conceal(RoomList);
                if (playerRows.empty()) conceal(PlayerList);
            } else {
            const HostRow host(content);
            position(OnlineRoom,host.x(0)+10,HostRow::field+6,host.column-20,22);
            position(OnlinePassword,host.x(1)+10,HostRow::field+6,host.column-20,22);
            position(OnlineDelay, host.x(2), HostRow::field, host.column, 34);
            position(OnlineListed, host.x(3), HostRow::field, host.column, 34);
            position(HostGame, host.x(4), HostRow::field, host.column, 36);
            const int split = contentLeft + (content * 62) / 100, roomsRight = split - 6, playersLeft = split + 6;
            const int listTop = onlineListsTop + 90;
            const int roomWidth=roomsRight-contentLeft-40, playersWidth=right-playersLeft-40;
            const int searchWidth=roomWidth-190;
            position(OnlineSearch,contentLeft+30,onlineListsTop+49,searchWidth-80,22);
            position(ClearOnlineSearch,contentLeft+20+searchWidth-48,onlineListsTop+44,48,32);
            position(RoomFilter,contentLeft+20+searchWidth+10,onlineListsTop+44,180,32);
            position(PlayerFilter,playersLeft+20,onlineListsTop+44,playersWidth,32);
            position(RefreshRooms, roomsRight - 20 - 100, onlineListsTop + 8, 100, 30);
            position(RoomList, contentLeft + 20, listTop, roomsRight - contentLeft - 40, std::max(50, bottom - 110 - listTop));
            const int fieldWidth = (roomsRight - contentLeft - 40 - 10) / 2;
            if (selectedRoomLocked()) {
                position(JoinPassword,contentLeft+30,bottom-88,fieldWidth-20,22);
                position(JoinRoom, contentLeft + 30 + fieldWidth, bottom - 94, fieldWidth, 34);
            } else {
                position(JoinRoom, contentLeft + 20, bottom - 94, roomsRight - contentLeft - 40, 34);
                conceal(JoinPassword);
            }
            position(OnlineAddress,contentLeft+30,bottom-42,fieldWidth-20,22);
            position(JoinGame, contentLeft + 30 + fieldWidth, bottom - 48, fieldWidth, 34);

            position(PlayerList, playersLeft + 20, listTop, playersWidth, std::max(50, bottom - 60 - listTop));
            {
                const int third = (playersWidth - 16) / 3;
                position(InvitePlayer, playersLeft + 20, bottom - 48, third, 34);
                position(AddFriend, playersLeft + 28 + third, bottom - 48, third, 34);
                position(PlayerProfile, playersLeft + 36 + 2 * third, bottom - 48, third, 34);
            }
            if (roomRows.empty())
                conceal(RoomList);
            if (playerRows.empty())
                conceal(PlayerList);
            }
        } else if (page == overlayPage) {
            const int panelBottom = h - 64, half = (content - 52) / 2, side = contentLeft + 32 + half;
            position(CloseOverlay,right-166,28,166,36);
            if (waveStyle()) {
                const int labelWidth = std::clamp(content * 2 / 5, 180, 420);
                const int valueLeft = contentLeft + 24 + labelWidth, valueWidth = right - 24 - valueLeft;
                position(OverlayDefaults, right - 20 - 160, 142, 160, 34);
                position(SpeedrunMode, valueLeft, 276 + 8, valueWidth, 34);
                position(SplitStyleList, contentLeft + 20, 340, content - 40, 156);
                position(SplitStyleAdd, contentLeft + 20, 508, 140, 32);
                position(SplitStyleReset, contentLeft + 172, 508, 120, 32);
                position(SplitNameField, valueLeft, 556 + 8, valueWidth - 122, 34);
                position(SplitNameSave, right - 24 - 110, 556 + 8, 110, 34);
                position(SplitImageChoose, valueLeft, 612 + 8, 150, 34);
                position(SplitImageRemove, valueLeft + 162, 612 + 8, 130, 34);
                const int ids[] = {OverlayCorner, OverlaySize, OverlayBackground, OverlaySplits, OverlayText, OverlayAhead,
                                   OverlayBehind, OverlayGold, OverlayDecimals, OverlayMargin, OverlayShape, OverlayTitle};
                for (int i = 0; i < 12; ++i) position(ids[i], valueLeft, 756 + i * waveOptionRow + 8, valueWidth, 34);
            } else {
            position(OverlayDefaults, right - 20 - 150 - 10 - 160, 136, 160, 34);
            position(SpeedrunMode, contentLeft + 20, 238, half, 34);
            position(SplitStyleList, contentLeft + 20, 314, half, 156);
            position(SplitStyleAdd, contentLeft + 20, 484, 140, 32);
            position(SplitStyleReset, contentLeft + 172, 484, 120, 32);
            position(SplitNameField, contentLeft + 20, 555, half - 122, 34);
            position(SplitNameSave, contentLeft + 20 + half - 110, 555, 110, 34);
            position(SplitImageChoose, contentLeft + 20, 632, 150, 34);
            position(SplitImageRemove, contentLeft + 182, 632, 130, 34);
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
            }
        } else if (page == updatesPage) {
            const Grid grid(content);
            const int half = (grid.column - 10) / 2;
            position(ReleasePage, right - 20 - 130, 136, 130, 34);
            if (waveStyle()) {
                const int labelWidth = std::clamp(content * 2 / 5, 180, 420);
                const int valueLeft = contentLeft + 24 + labelWidth, valueWidth = right - 24 - valueLeft, rows = 224 + 64, split = (valueWidth - 10) / 2;
                position(UpdateChannel, valueLeft, rows + 8, valueWidth, 34);
                position(AutomaticUpdates, valueLeft, rows + waveOptionRow + 8, valueWidth, 34);
                position(CheckUpdates, valueLeft, rows + 2 * waveOptionRow + 8, split, 34);
                position(InstallUpdate, valueLeft + split + 10, rows + 2 * waveOptionRow + 8, valueWidth - split - 10, 34);
                position(OpenLogs, contentLeft + 24, 590, 170, 34);
            } else {
            position(UpdateChannel, grid.x(0), Grid::field(236, 0), grid.column, 34);
            position(AutomaticUpdates, grid.x(1), Grid::field(236, 0), grid.column, 34);
            position(CheckUpdates, grid.x(2), Grid::field(236, 0), half, 34);
            position(InstallUpdate, grid.x(2) + half + 10, Grid::field(236, 0), grid.column - half - 10, 34);
            position(OpenLogs, contentLeft + 20, 470, 170, 34);
            }
        } else if (page == colorsPage) {
            const int panelBottom = h - 64, listWidth = std::min(560, content - 40 - 200);
            position(CloseColors,right-166,28,166,36);
            if (waveStyle()) {
                position(ColorList, contentLeft + 20, 268, content - 40, 300);
                const int gap = 10, button = (content - 40 - 5 * gap) / 6;
                int x = contentLeft + 20;
                for (int id : {ColorPick, ColorReset, ColorResetAll, ColorSurprise, ColorCopy, ColorPaste}) {
                    position(id, x, 584, button, 34);
                    x += button + gap;
                }
            } else {
            position(ColorList, contentLeft + 20, 228, listWidth, std::max(120, panelBottom - 20 - 228));
            const int side = contentLeft + 20 + listWidth + 20, buttonWidth = std::min(170, right - 20 - side);
            position(ColorPick, side, 228, buttonWidth, 34);
            position(ColorReset, side, 272, buttonWidth, 34);
            position(ColorResetAll, side, 316, buttonWidth, 34);
            position(ColorSurprise, side, 380, buttonWidth, 34);
            position(ColorCopy, side, 424, buttonWidth, 34);
            position(ColorPaste, side, 468, buttonWidth, 34);
            }
        } else if (page == controlsPage) {
            const ControlsLayout grid(content);
            SetWindowTextW(controls[BackToPlay], waveStyle() ? L"Back to Home" : L"Back to Play");
            position(BackToPlay, right - 150, 28, 150, 34);
            position(ControlsReset, right - 20 - 160, controlsTop + 10, 160, 30);
            for (unsigned i = 0; i < input::button_count; ++i) {
                const int x = grid.x(i) + 104, y = grid.y(i);
                position(ControlKey + int(i), x, y, grid.key, 34);
                position(ControlAlternate + int(i), x + grid.key + 10, y, grid.key, 34);
                position(ControlPad + int(i), x + 2 * grid.key + 20, y - 2, grid.pad, 38);
            }
        } else if (page == tabsPage) {
            const int panelBottom = h - 64, listWidth = std::min(420, content - 40 - 180);
            position(CloseTabs,right-166,28,166,36);
            if (waveStyle()) {
                position(TabList, contentLeft + 20, 268, content - 40, int(dock_tab_count) * 40 + 4);
                const int y = 268 + int(dock_tab_count) * 40 + 20;
                position(TabUp, contentLeft + 20, y, 150, 34);
                position(TabDown, contentLeft + 180, y, 150, 34);
                position(TabToggle, contentLeft + 340, y, 150, 34);
                position(TabDefaults, right - 20 - 150, y, 150, 34);
            } else {
            position(TabList, contentLeft + 20, 228, listWidth, std::min(int(dock_tab_count) * 40 + 4, panelBottom - 20 - 228));
            const int side = contentLeft + 20 + listWidth + 20;
            position(TabUp, side, 228, 150, 34);
            position(TabDown, side, 272, 150, 34);
            position(TabToggle, side, 316, 150, 34);
            position(TabDefaults, side, 380, 150, 34);
            }
        } else if (page == importPage) {
            const int listsBottom = h - 210, targetTop = listsBottom + 12;
            int x = contentLeft + 20;
            for (int id : {TaskCharacters, TaskStages, TaskMovesets, TaskCapsules}) {
                position(id, x, 152, 120, 34);
                x += 130;
            }
            position(CloseImporter,right-166,28,166,36);
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
            ui::SettingsLayout grid(content,contentLeft,waveStyle(),settingsTop());
            grid.filter(searchText(trimmed(textOf(SettingsSearch))));
            position(SettingsSearch,contentLeft+10,123,content-276,24);
            position(ClearSettingsSearch,right-244,116,64,38);
            position(UndoSettings,right-164,116,164,38);
            int filterX=contentLeft;
            for(int id : {SettingsAll,SettingsDisplay,SettingsGraphics,SettingsGame,SettingsLauncher}) {
                const int width=id==SettingsAll ? 124 : 112;
                position(id,filterX,174,width,34);filterX+=width+8;
            }
            const std::array<std::vector<int>,4> fields{{
                {WindowMode,Resolution,Aspect,Fps,Vsync,Audio},
                {Renderer,Antialiasing,Filtering,Upscaler,InternalScale,ImageEffect,Brightness,ClearImage},
                {CpuDifficulty,HdTextures,Music,FpsCounter,RandomizerMode,LoadTimes,RunAhead},
                {LauncherTheme,LauncherAccent,StartPage,DiscordActivity,LauncherMotion,LauncherHints,LauncherInterface,WaveSky,WaveEmblem,LauncherClock,WaveRibbons}}};
            for(size_t group=0;group<fields.size();++group) {
                const int top=grid.sections[group];if(top<0)continue;
                for(size_t index=0;index<fields[group].size();++index) {
                    const auto cell=grid.field(top,int(index));
                    if(fields[group][index]==ClearImage) {
                        const int half=(cell.width-10)/2;
                        position(ClearImage,cell.x,cell.y,half,cell.height);
                        position(ResetGraphics,cell.x+half+10,cell.y,cell.width-half-10,cell.height);
                    } else position(fields[group][index],cell.x,cell.y,cell.width,cell.height);
                }
            }
            if(grid.sections[0]>=0) position(ResetDisplay,right-168,grid.sections[0]+12,144,32);
            if(grid.sections[2]>=0) {
                position(ResetGame,right-168,grid.sections[2]+12,144,32);
                position(TextureFolder,right-350,grid.sections[2]+12,170,32);
            }
            if(grid.sections[3]>=0) {
                position(ArrangeTabs,right-168,grid.sections[3]+12,144,32);
                position(OpenColors,right-280,grid.sections[3]+12,100,32);
                position(OpenHelp,right-460,grid.sections[3]+12,168,32);
            }
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
        scrollPlan=plan;scrollOrder=order;scrollPlanPage=page;scrollPlanSize={rect.right,rect.bottom};scrollPlanReady=true;
        clipPageControls();
        refreshEnabled();
        if(pageChanged)(pageDirty=true),RedrawWindow(hwnd,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW|RDW_ALLCHILDREN);
    }
    bool scrollControls() {
        RECT rect{};
        GetClientRect(hwnd,&rect);
        if (!scrollPlanReady || layoutPending || layingOut || scrollPlanPage!=page ||
            scrollPlanSize!=std::pair<LONG,LONG>{rect.right,rect.bottom} || dropdownOpen())
            return false;
        const int viewportBottom=shellHeader+pageVisibleHeight();
        auto batch=BeginDeferWindowPos(int(scrollOrder.size()));
        for (const auto id : scrollOrder) {
            const auto found=scrollPlan.find(id);
            if (found==scrollPlan.end() || !found->second.pageControl) continue;
            auto& where=found->second;
            const auto control=controls.at(id);
            const bool shown=(GetWindowLongPtrW(control,GWL_STYLE)&WS_VISIBLE)!=0;
            if (shown && !where.wanted) where.wanted=true;
            else if (!shown && where.wanted && where.visible) where.wanted=false;
            const int y=where.pageY+shellHeader-pageScroll;
            where.visible=where.wanted && y+where.pageH>shellHeader && y<viewportBottom;
            where.y=p(y+where.adjust);
            if (!where.visible && !shown) continue;
            UINT flags=SWP_NOACTIVATE|SWP_NOCOPYBITS|SWP_NOZORDER|SWP_NOSIZE;
            if (where.visible!=shown) flags|=where.visible?SWP_SHOWWINDOW:SWP_HIDEWINDOW;
            if (batch) batch=DeferWindowPos(batch,control,nullptr,where.x,where.y,0,0,flags);
        }
        if (batch) EndDeferWindowPos(batch);
        SetPropW(hwnd,L"OpenKai2.ScrollPos",reinterpret_cast<HANDLE>(intptr_t(scrollReported())));
        clipPageControls();
        return true;
    }
    void clipPageControls() {
        if (dropdownOpen())
            return;
        const int viewportTop = p(shellHeader), viewportBottom = p(shellHeader + pageVisibleHeight());
        for (const auto& [id, control] : controls) {
            if ((id >= PlayTab && id <= ControlsTab) || id == FindAction || id == OpenNotifications)
                continue;
            RECT bounds{};
            GetWindowRect(control, &bounds);
            MapWindowPoints(nullptr, hwnd, reinterpret_cast<POINT*>(&bounds), 2);
            const bool clipped = IsWindowVisible(control) &&
                (bounds.top < viewportTop || bounds.bottom > viewportBottom);
            auto region = clipped ? CreateRectRgn(0, std::max(0L, viewportTop - bounds.top),
                bounds.right - bounds.left, std::min(bounds.bottom, LONG(viewportBottom)) - bounds.top) : nullptr;
            auto previous = CreateRectRgn(0, 0, 0, 0);
            const bool hadRegion = GetWindowRgn(control, previous) != ERROR;
            const bool changed = region ? !hadRegion || !EqualRgn(previous, region) : hadRegion;
            DeleteObject(previous);
            if (!changed || !SetWindowRgn(control, region, TRUE)) {
                if (region)
                    DeleteObject(region);
            }
        }
    }
    void clipControlPaint(HDC dc, HWND control) const {
        const int id = GetDlgCtrlID(control);
        if ((id >= PlayTab && id <= ControlsTab) || id == FindAction || id == OpenNotifications)
            return;
        RECT bounds{};
        GetWindowRect(control, &bounds);
        MapWindowPoints(nullptr, hwnd, reinterpret_cast<POINT*>(&bounds), 2);
        IntersectClipRect(dc, 0, std::max(0L, p(shellHeader) - bounds.top), bounds.right - bounds.left,
                          std::min(bounds.bottom, LONG(p(shellHeader + pageVisibleHeight()))) - bounds.top);
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
    void paintShell(HDC dc, int h) {
        RECT client{};GetClientRect(hwnd,&client);const int w = unscale(client.right), right = w - margin;
        if (waveStyle()) {
            const size_t width = size_t(buffer.frame.width);
            if (ambient.size() == width * size_t(buffer.frame.height)) {
                GdiFlush();
                const auto copyRows = [&](int from, int to) {
                    from = std::clamp(from, 0, buffer.frame.height); to = std::clamp(to, 0, buffer.frame.height);
                    if (from < to) std::copy(ambient.begin() + ptrdiff_t(size_t(from) * width), ambient.begin() + ptrdiff_t(size_t(to) * width), buffer.frame.pixels + size_t(from) * width);
                };
                copyRows(0, p(shellHeader));
                copyRows(p(h - dockReserve()), int(client.bottom));
            }
            {
                Gdiplus::Graphics graphics(dc);
                Gdiplus::LinearGradientBrush rule(Gdiplus::PointF(Gdiplus::REAL(p(margin)),0),Gdiplus::PointF(Gdiplus::REAL(p(right)),0),Gdiplus::Color(90,255,236,210),Gdiplus::Color(0,255,236,210));
                graphics.FillRectangle(&rule,Gdiplus::REAL(p(margin)),Gdiplus::REAL(p(shellHeader-1)),Gdiplus::REAL(p(right-margin)),Gdiplus::REAL(std::max(1,p(1))));
                const Gdiplus::REAL barTop=Gdiplus::REAL(p(h-dockReserve()));
                Gdiplus::LinearGradientBrush bar(Gdiplus::PointF(0,barTop),Gdiplus::PointF(0,Gdiplus::REAL(client.bottom)+1),Gdiplus::Color(200,16,12,30),Gdiplus::Color(230,10,8,22));
                graphics.FillRectangle(&bar,Gdiplus::REAL(0),barTop,Gdiplus::REAL(client.right),Gdiplus::REAL(client.bottom)-barTop);
                Gdiplus::LinearGradientBrush barRule(Gdiplus::PointF(0,0),Gdiplus::PointF(Gdiplus::REAL(client.right),0),Gdiplus::Color(0,255,226,180),Gdiplus::Color(110,255,226,180));
                barRule.SetBlendBellShape(0.5f);
                graphics.FillRectangle(&barRule,Gdiplus::REAL(0),barTop,Gdiplus::REAL(client.right),Gdiplus::REAL(std::max(1,p(1))));
            }
            buffer.frame.ball(p(margin+16),p(36),p(14),{0,0,p(w),p(shellHeader)});
            text(dc,L"OpenKai2",margin+42,19,180,28,fonts.brand,ink);
            const std::wstring trail=L"Home  \u203a  "+((waveTrailPage==page || waveTrailPending) && !waveTrail.empty() ? waveTrail : std::wstring(pageTitle(page)));
            text(dc,trail,margin+42,44,std::max(230,w/2-margin-80),18,fonts.caption,muted,DT_LEFT|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
            SYSTEMTIME now{};GetLocalTime(&now);
            text(dc,dateClock(now),w-margin-480,16,416,44,fonts.title,ink,DT_RIGHT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
            text(dc,L"Esc  Back        F1  Help        Ctrl+K  Find",w/2-260,h-26,520,20,fonts.caption,muted,DT_CENTER|DT_SINGLELINE|DT_NOPREFIX);
            return;
        }
        fill(dc,{0,0,p(w),p(shellHeader)},sidebar);
        fill(dc,{p(margin),p(shellHeader-1),p(right),p(shellHeader)},border);
        buffer.frame.ball(p(margin+16),p(36),p(14),{0,0,p(w),p(shellHeader)});
        text(dc,L"OpenKai2",margin+42,19,180,28,fonts.brand,ink);
        text(dc,L"Budokai 2 for Windows",margin+42,44,210,18,fonts.caption,muted);
        SYSTEMTIME now{};GetLocalTime(&now);
        if(const auto clock=clockText(now);!clock.empty())
            text(dc,clock,w-margin-172,27,116,26,fonts.caption,muted,DT_RIGHT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
        const auto tabs = visibleTabs();
        const ui::DockLayout dockLayout(w,h,int(tabs.size()));
        const int cell=dockLayout.cell,left=dockLayout.left;
        const RECT dock{p(left-8),p(h-112),p(left+cell*int(tabs.size())+8),p(h-40)};
        box(dc,dock,sidebar,border,18);
        const int active = tabSlot(navigationPage());
        if(active>=0) {
            const int itemLeft=left+int(std::lround(dockPosition*cell));
            const RECT item{p(itemLeft+3),p(h-104),p(itemLeft+cell-3),p(h-48)};
            box(dc,item,mix(sidebar,orange,24),mix(border,orange,80),15);

        }
    }
    void paintHeader(HDC dc,int w,int content) {
        const auto titles=pageTitles;
        const wchar_t* subtitles[]={
            L"Start the game, manage your saves, or find an online match.",
            L"Find a room, invite friends, or host a versus match.",
            L"Categories, split records and automatic timing for Dragon World and World Tournament.",
            L"Manage what your mods add. Your original disc stays unchanged.",
            L"Import saves, keep recovery copies and manage your memory card.",
            L"Saved automatically. Game settings apply at the next launch.",
            L"Choose your update channel and manage new releases.",
            L"Keyboard and controller bindings. Changes apply when the game starts.",
            L"Choose content from another PS2 disc, then add it to a mod.",
            L"Customize split names, images and layout. PS2 time and verification stay protected.",
            L"Reorder the dock or hide tabs you do not use.",
            L"Personalize your theme. Unchanged colors follow the selected palette.",
            L"Your player card: picture, name color, main fighter and tagline. Choose what players online see.",
            L"Search pages, tools and players. Use the arrow keys and Enter to open a result.",
            L"Review friend requests and match invitations.",
            L"Quick controls, setup guidance and tools for reporting a problem.",
            L"Their player card, status and what you can do together.",
            L"",
            L"Built-in trophies and RetroAchievements. Unlock all pauses both so it cannot earn them for you."};
        const RECT tile{p(contentLeft),p(28),p(contentLeft+40),p(68)};
        const int icon=page==achievementsPage ? OpenAchievements : page==helpPage ? OpenHelp : page==profilePage || page==playerPage ? HomeProfile : page==finderPage ? FindAction : page==notificationsPage ? OpenNotifications :
            PlayTab+(page==controlsPage || page==savesPage ? page : navigationPage());
        const double enter=waveStyle() ? pageEnter() : 1.0,later=waveStyle() ? pageEnter(90.0) : 1.0;
        if(waveStyle()) {
            buffer.frame.halo({(tile.left+tile.right)/2,(tile.top+tile.bottom)/2,(tile.left+tile.right)/2+1,(tile.top+tile.bottom)/2+1},0,p(int(36+26*(1.0-enter))),RGB(255,214,150),int(60+90*(1.0-enter)));
            navigationIcon(dc,icon,tile,mix(background,RGB(255,250,240),int(255*(0.35+0.65*enter))),p(34));
        } else {
            box(dc,tile,mix(panel,orange,22),mix(border,orange,70),12);
            navigationIcon(dc,icon,{tile.left-p(7),tile.top,tile.right,tile.bottom},orange);
        }
        const int side=transitionDirection>0 ? -1 : 1;
        const int slide=int(std::lround(28.0*(1.0-enter)))*side,slideLater=int(std::lround(36.0*(1.0-later)))*side;
        text(dc,titles[page],contentLeft+56+slide,27,content-264,42,fonts.title,mix(background,ink,int(255*enter)),DT_LEFT|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
        text(dc,subtitles[page],contentLeft+56+slideLater,74,content-56,32,fonts.caption,mix(background,muted,int(255*later)),DT_LEFT|DT_WORDBREAK|DT_NOPREFIX);
        if(page<pageCount && page!=controlsPage && page!=savesPage) {
            const auto label=stateLabel();const int width=measure(dc,label,fonts.caption);
            const int left=w-margin-width-16;
            box(dc,{p(left-22),p(34),p(w-margin),p(62)},mix(background,stateColor(),18),mix(border,stateColor(),60),9);
            ellipse(dc,p(left-10),p(48),p(3),stateColor());
            text(dc,label,left,39,width+6,20,fonts.caption,muted,DT_LEFT|DT_SINGLELINE|DT_NOPREFIX);
        }
    }
    void paintHelp(HDC dc,int h,int content) {
        const int right=contentLeft+content;
        panelBox(dc,contentLeft,124,right,610);
        heading(dc,L"Keyboard shortcuts",contentLeft+24,142,content-48);
        const std::pair<const wchar_t*,const wchar_t*> shortcuts[]={
            {L"F5",L"Play the game"},{L"Ctrl + O",L"Choose an ISO"},{L"Ctrl + Page Up / Page Down",L"Previous / next dock tab"},
            {L"Ctrl + 1–6",L"Open a dock tab"},{L"Alt + Left",L"Go back"},{L"Ctrl + Home / End",L"Top / bottom of the page"},
            {L"Ctrl + S",L"Save profile or split name"},{L"Ctrl + Z",L"Undo the last settings change"},{L"Ctrl + F",L"Search the current page"},
            {L"Ctrl + K",L"Find a tool or setting"},{L"Ctrl + Shift + N",L"Open notifications"},{L"Ctrl + P",L"Customize your profile"},{L"F11",L"Fullscreen / back to a window"}};
        const int keyWidth=std::clamp(content/3,230,360);
        for(size_t i=0;i<std::size(shortcuts);++i) {
            const int y=188+int(i)*32;
            text(dc,shortcuts[i].first,contentLeft+24,y,keyWidth,28,fonts.strong,orange);
            text(dc,shortcuts[i].second,contentLeft+40+keyWidth,y,content-keyWidth-64,28,fonts.body,ink,DT_LEFT|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
        }
        panelBox(dc,contentLeft,630,right,750);
        heading(dc,L"Ready to play",contentLeft+24,646,content-48);
        text(dc,L"Choose your original USA ISO on Play, verify it, then choose Play game. Controls and Saves & backups are below the game panel. Your ISO selection is remembered.",
             contentLeft+24,684,content-48,52,fonts.caption,muted,DT_WORDBREAK|DT_NOPREFIX);
        panelBox(dc,contentLeft,770,right,h-32);
        text(dc,L"Copy version and launcher settings for a support report, or open the relevant files. Profile names, keys and file paths are excluded from the summary.",
             contentLeft+24,858,content-48,48,fonts.caption,muted,DT_WORDBREAK|DT_NOPREFIX);
    }
    bool copyText(const std::wstring& value) {
        if(!OpenClipboard(hwnd))return false;
        bool copied=false;const size_t bytes=(value.size()+1)*sizeof(wchar_t);
        if(auto memory=GlobalAlloc(GMEM_MOVEABLE,bytes)) {
            if(auto target=GlobalLock(memory)) {
                std::memcpy(target,value.c_str(),bytes);GlobalUnlock(memory);EmptyClipboard();
                copied=SetClipboardData(CF_UNICODETEXT,memory)!=nullptr;
            }
            if(!copied)GlobalFree(memory);
        }
        CloseClipboard();return copied;
    }
    void copySetup() {
        std::wostringstream summary;summary<<L"OpenKai2 v"<<wide(application_version)<<L"\r\n";
        const std::pair<const wchar_t*,int> fields[]={{L"Renderer",Renderer},{L"Presentation limit",Fps},{L"Window mode",WindowMode},
            {L"Aspect ratio",Aspect},{L"Internal resolution",InternalScale},{L"Anti-aliasing",Antialiasing},{L"Filtering",Filtering},
            {L"Upscaling",Upscaler},{L"Audio",Audio},{L"Music",Music},{L"Theme",LauncherTheme}};
        for(const auto& [label,id]:fields)summary<<label<<L": "<<textOf(id)<<L"\r\n";
        summary<<L"Window resolution: "<<settings.width<<L" x "<<settings.height<<L"\r\nGame simulation: 60 FPS\r\nDisc verified: "<<(disc?L"Yes":L"No")<<L"\r\n";
        const bool copied=copyText(summary.str());status=copied?L"Setup summary copied.":L"Could not access the clipboard.";error=!copied;
    }
    void openHelp() {
        if(page!=helpPage) {helpReturnPage=page;helpOrigin=navigationPage();}
        page=helpPage;layout();
    }
    void paintNotifications(HDC dc,int h,int content) {
        const int right=contentLeft+content;
        panelBox(dc,contentLeft,112,right,h-56);
        const auto notice=noticeView();
        if(!notice) {
            heading(dc,L"You're all caught up",contentLeft+24,144,content-48);
            text(dc,L"Friend requests and invitations appear here. You can keep playing while new requests arrive.",
                contentLeft+24,188,content-48,60,fonts.body,muted,DT_LEFT|DT_WORDBREAK|DT_NOPREFIX);
            return;
        }
        caps(dc,notice->invite ? L"MATCH INVITATION" : L"FRIEND REQUEST",contentLeft+24,140,content-48,orange);
        avatar(dc,p(contentLeft+52),p(206),p(28),notice->name,notice->id,panel,directory && directory->player(notice->id) ? 1 : 0);
        nameText(dc,notice->name,contentLeft+96,184,content-120,40,fonts.title,nameInk(notice->id,ink),DT_LEFT|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX,notice->id);
        if(const auto card=cardLine(notice->id);!card.empty())
            text(dc,card,contentLeft+96,226,content-120,22,fonts.caption,muted,DT_LEFT|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
        text(dc,notice->detail,contentLeft+24,262,content-48,76,fonts.body,muted,DT_LEFT|DT_WORDBREAK|DT_NOPREFIX);
        const auto identity=notice->verified ? L"Verified identity · "+notice->age : L"Identity not verified · "+notice->age;
        text(dc,identity,contentLeft+24,350,content-48,24,fonts.caption,notice->verified ? green : muted);
        if(notice->invite && !disc)
            text(dc,L"Verify your ISO on Play before joining this match.",contentLeft+24,394,content-48,36,fonts.caption,muted,DT_WORDBREAK|DT_NOPREFIX);
        if(notice->waiting)
            text(dc,std::to_wstring(notice->waiting)+L" more waiting",contentLeft+24,520,content-48,24,fonts.caption,muted);
    }
    void achievementRowPaint(HDC dc,int top,int content,bool unlocked,const std::wstring& title,const std::wstring& detail,unsigned points,const std::wstring& state,COLORREF tone) {
        const int right=contentLeft+content,cy=top+achievementRow/2;
        if(unlocked) {
            buffer.frame.halo({p(contentLeft+44),p(cy),p(contentLeft+45),p(cy+1)},0,p(26),tone,60);
            ellipse(dc,p(contentLeft+44),p(cy),p(16),tone);
            navigationIcon(dc,OpenAchievements,{p(contentLeft+32),p(cy-12),p(contentLeft+56),p(cy+12)},RGB(30,20,10),p(18));
        } else {
            if(!waveStyle()) {
                ellipse(dc,p(contentLeft+44),p(cy),p(16),mix(panel,border,140));
                ellipse(dc,p(contentLeft+44),p(cy),p(14),mix(panel,RGB(0,0,0),40));
            }
            navigationIcon(dc,OpenAchievements,{p(contentLeft+32),p(cy-12),p(contentLeft+56),p(cy+12)},faint,p(18));
        }
        text(dc,title,contentLeft+76,top+8,content-300,24,fonts.strong,unlocked ? ink : mix(ink,muted,140),DT_LEFT|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
        text(dc,detail,contentLeft+76,top+32,content-300,20,fonts.caption,muted,DT_LEFT|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
        if(points)text(dc,std::to_wstring(points)+(points==1 ? L" pt" : L" pts"),right-210,top+8,186,24,fonts.strong,unlocked ? tone : muted,DT_RIGHT|DT_SINGLELINE|DT_NOPREFIX);
        text(dc,state,right-210,top+32,186,20,fonts.caption,unlocked ? green : faint,DT_RIGHT|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
        fill(dc,{p(contentLeft+24),p(top+achievementRow-1),p(right-24),p(top+achievementRow)},mix(panel,border,70));
    }
    void paintAchievements(HDC dc,int h,int content) {
        const int right=contentLeft+content;
        const unsigned earned=unsigned(achievementProgress.unlocked.size()),points=achievementProgress.points(),total=achievements::total_points();
        panelBox(dc,contentLeft,124,right,232);
        heading(dc,L"OpenKai2 achievements",contentLeft+24,140,content-48);
        text(dc,std::to_wstring(earned)+L" of "+std::to_wstring(achievements::definition_count)+L" unlocked \u00b7 "+std::to_wstring(points)+L" of "+std::to_wstring(total)+L" points \u00b7 "+
                 std::to_wstring(achievementProgress.wins)+L" battles won",contentLeft+24,168,content-48,20,fonts.caption,muted,DT_LEFT|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
        const RECT track{p(contentLeft+24),p(198),p(right-24),p(206)};
        box(dc,track,mix(panel,RGB(0,0,0),60),mix(panel,border,120),4);
        if(total)box(dc,{track.left,track.top,track.left+LONG((track.right-track.left)*double(points)/double(total)),track.bottom},gold,gold,4);
        if(settings.achievementsPaused)
            text(dc,L"Paused: Unlock all changed your memory card. Restore a backup from before Unlock all, or delete the Budokai 2 save, to earn achievements again.",
                 contentLeft+24,212,content-48,18,fonts.caption,red,DT_LEFT|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
        panelBox(dc,contentLeft,248,right,448);
        heading(dc,L"RetroAchievements",contentLeft+24,264,content-48);
        const bool signedIn=!settings.raToken.empty();
        text(dc,signedIn ? L"Signed in as "+wide(settings.raUser) : std::wstring(L"Sign in with your free retroachievements.org account to earn their achievements too."),
             contentLeft+24,292,content-48,20,fonts.caption,signedIn ? green : muted,DT_LEFT|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
        text(dc,retroStatus,contentLeft+24,394,content-48,20,fonts.caption,retroLookupFailed ? red : muted,DT_LEFT|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
        if(!retroHash.empty())
            text(dc,L"Disc hash "+wide(retroHash)+(retroGame ? L" \u00b7 game "+std::to_wstring(retroGame) : std::wstring(L" \u00b7 set "+std::to_wstring(retro::budokai2_game))),
                 contentLeft+24,416,content-48,20,fonts.caption,faint,DT_LEFT|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
        panelBox(dc,contentLeft,achievementsTop,right,retroTop()-16);
        heading(dc,L"Built-in",contentLeft+24,achievementsTop+16,content-48);
        text(dc,L"Tracked in the game on this PC. They pause while Unlock all is in effect.",contentLeft+140,achievementsTop+19,content-164,20,fonts.caption,muted,DT_LEFT|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
        int y=achievementsTop+56;
        for(const auto& definition:achievements::definitions) {
            const auto found=achievementProgress.unlocked.find(definition.id);
            std::wstring state;
            if(found!=achievementProgress.unlocked.end())state=L"Unlocked "+dateText(found->second);
            else if(definition.goal>1)state=std::to_wstring(achievementProgress.count(definition))+L" / "+std::to_wstring(definition.goal);
            else state=L"Locked";
            achievementRowPaint(dc,y,content,found!=achievementProgress.unlocked.end(),wide(definition.title),wide(definition.description),definition.points,state,gold);
            y+=achievementRow;
        }
        if(!retroSet.entries.empty()) {
            const int top=retroTop();
            panelBox(dc,contentLeft,top,right,top+56+int(retroSet.entries.size())*achievementRow+8);
            heading(dc,wide(retroSet.title.empty() ? std::string("RetroAchievements set") : retroSet.title),contentLeft+24,top+16,content-48);
            text(dc,std::to_wstring(retroSet.earned())+L" of "+std::to_wstring(retroSet.points())+L" points",contentLeft+24,top+19,content-48,20,fonts.caption,muted,DT_RIGHT|DT_SINGLELINE|DT_NOPREFIX);
            int row=top+56;
            for(const auto& entry:retroSet.entries) {
                achievementRowPaint(dc,row,content,entry.unlocked,wide(entry.title),wide(entry.description),entry.points,
                    entry.unlocked ? (entry.hardcore ? L"Hardcore " : L"Unlocked ")+(entry.when ? dateText(entry.when) : std::wstring()) : std::wstring(L"Locked"),sky);
                row+=achievementRow;
            }
        }
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
    void paintGamePanel(const LoungeLayout& g) {
        const int inset=p(1),radius=p(11);
        const RECT area{p(g.centre)+inset,p(112)+inset,p(g.centreRight)-inset,p(g.gameBottom)-inset};
        GdiFlush();
        for(int x=area.left;x<area.right;++x) {
            const auto tone=mix(heroFrom,heroTo,(x-area.left)*255/std::max(1,int(area.right-area.left)-1));
            for(int y=area.top;y<area.bottom;++y) {
                const double cx=std::clamp(x+.5,double(area.left+radius),double(area.right-radius));
                const double cy=std::clamp(y+.5,double(area.top+radius),double(area.bottom-radius));
                const double coverage=std::clamp(radius+.5-std::hypot(x+.5-cx,y+.5-cy),0.0,1.0);
                if(coverage<=0)continue;
                auto& pixel=buffer.frame.pixels[size_t(y)*size_t(buffer.frame.width)+size_t(x)];
                const auto under=RGB((pixel>>16)&255,(pixel>>8)&255,pixel&255);
                const auto color=blend(under,tone,coverage);
                pixel=uint32_t(GetRValue(color))<<16|uint32_t(GetGValue(color))<<8|GetBValue(color);
            }
        }
    }
    void paintPlay(HDC dc,int content) {
        const LoungeLayout g(content,pageCanvasHeight());
        panelBox(dc,g.friendsLeft,112,g.friendsLeft+g.friendsWidth,g.friendsBottom);
        panelBox(dc,g.centre,112,g.centreRight,g.gameBottom);
        panelBox(dc,g.centre,g.roomsTop,g.centreRight,g.bottom);
        panelBox(dc,g.details,g.detailsTop,g.right,g.bottom);
        panelBox(dc,g.left,g.notesTop,g.right,g.notesBottom);
        paintGamePanel(g);
        {
            const int scope=std::clamp(homeScope,0,3);
            const wchar_t* headings[]={L"Friends",L"Friends",L"Players",L"Players"};
            heading(dc,headings[scope],g.friendsLeft+18,126,g.friendsWidth-80);
            if(activeProfile()) {
                const auto count=scope==0 ? std::to_wstring(friendCount(true))+L"/"+std::to_wstring(friendCount()) : std::to_wstring(friendRows.size());
                text(dc,count,g.friendsLeft+g.friendsWidth-80,128,62,20,fonts.label,muted,DT_RIGHT|DT_SINGLELINE|DT_NOPREFIX);
            }
            if(friendRows.empty()) {
                const int emptyTop=std::max(206,(202+g.friendsBottom-160)/2-26);
                const wchar_t* empty[]={L"Your friends appear here.",L"No friends online",L"Nobody else is online",L"No recent opponents yet"};
                const wchar_t* hints[]={L"Find players on Online to add a friend.",L"Friends show up here as soon as they come online.",
                                        L"Players appear here when they open OpenKai2.",L"Players you face online are remembered here."};
                text(dc,empty[scope],g.friendsLeft+20,emptyTop,g.friendsWidth-40,32,fonts.body,muted,DT_CENTER|DT_WORDBREAK|DT_NOPREFIX);
                text(dc,hints[scope],g.friendsLeft+20,emptyTop+34,g.friendsWidth-40,36,fonts.caption,faint,DT_CENTER|DT_WORDBREAK|DT_NOPREFIX);
            } else if(!g.compact && g.friendsBottom-392>=160)
                text(dc,L"Right-click a name for more",g.friendsLeft+16,g.friendsBottom-178,g.friendsWidth-32,18,fonts.label,faint,DT_CENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
        }
        const auto profile=activeProfile();
        if(!g.compact) {
        fill(dc,{p(g.friendsLeft+16),p(g.friendsBottom-100),p(g.friendsLeft+g.friendsWidth-16),p(g.friendsBottom-99)},border);
        const auto name=profile ? wide(profile->name) : std::wstring(L"Local player");
        if(profile)avatar(dc,p(g.friendsLeft+33),p(g.friendsBottom-70),p(16),name,profile->id,panel);
        else {ellipse(dc,p(g.friendsLeft+33),p(g.friendsBottom-70),p(16),mix(panel,orange,25));text(dc,L"P",g.friendsLeft+17,g.friendsBottom-82,32,24,fonts.strong,orange,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);}
        nameText(dc,name,g.friendsLeft+58,g.friendsBottom-89,g.friendsWidth-76,22,fonts.strong,profile ? nameInk(profile->id,ink) : ink,DT_LEFT|DT_END_ELLIPSIS|DT_SINGLELINE|DT_NOPREFIX,profile ? profile->id : std::string());
        const auto style=profile ? profileStyle(profile->id) : nullptr;
        const auto activity=directory && directory->connected() ? statusLabel(profile_status(style ? style->availability : 0,running(),room!=nullptr,roomHost)) : profile ? std::wstring(L"Offline · local profile") : std::wstring(L"Create an online profile");
        text(dc,activity,g.friendsLeft+58,g.friendsBottom-65,g.friendsWidth-76,18,fonts.label,muted,DT_LEFT|DT_END_ELLIPSIS|DT_SINGLELINE|DT_NOPREFIX);
        }
        caps(dc,L"DRAGON BALL Z",g.centre+24,133,g.centreWidth()-40,muted);
        text(dc,L"Budokai 2",g.centre+24,164,g.centreWidth()-48,70,fonts.gameName,bannerTitle,DT_LEFT|DT_END_ELLIPSIS|DT_SINGLELINE|DT_NOPREFIX);
        const int statusLeft=g.centre+24;
        const auto mode=settings.speedrun ? L"Speedrun · "+wide(speedrun::category_label(speedrun::Category(settings.speedrun),settings.cpuDifficulty,settings.randomizer)) : std::wstring(L"Standard play");
        const auto detail=busy() ? progressText() : disc ? L"USA disc verified · "+mode : std::wstring(L"Choose the original USA ISO to play.");
        text(dc,detail,statusLeft,244,g.centreRight-statusLeft-24,22,fonts.caption,disc ? green : muted,DT_LEFT|DT_END_ELLIPSIS|DT_SINGLELINE|DT_NOPREFIX);
        heading(dc,L"Rooms"+(roomRows.empty() ? std::wstring() : L" · "+std::to_wstring(roomRows.size())),g.centre+18,g.roomsTop+16,g.centreWidth()-170);
        if(roomRows.empty()) {
            const bool connected=directory && directory->connected() && !directory->name_taken();
            const int available=g.bottom-g.roomsTop-118;
            const int mid=g.roomsTop+102+std::max(0,(available-62)/2);
            text(dc,connected ? L"No matching rooms" : L"You are offline",g.centre+25,mid,g.centreWidth()-50,24,fonts.strong,muted,DT_CENTER|DT_SINGLELINE|DT_NOPREFIX);
            text(dc,connected ? L"Try another filter, or create a room." : L"Open Online to connect and find players.",g.centre+28,mid+28,g.centreWidth()-56,available<62 ? 20 : 36,fonts.caption,faint,
                 DT_CENTER|DT_NOPREFIX|(available<62 ? DT_SINGLELINE|DT_END_ELLIPSIS : DT_WORDBREAK));
        }
        caps(dc,L"ROOM DETAILS",g.details+20,(g.detailsTop+17),g.detailsWidth()-40,muted);
        const auto row=SendMessageW(controls[RoomList],LB_GETCURSEL,0,0);
        const auto chosen=row>=0 && size_t(row)<roomRows.size() ? &roomRows[size_t(row)] : nullptr;
        if(chosen) {
            {const auto hostId=playerNamed(chosen->host);avatar(dc,p(g.details+42),p((g.detailsTop+81)),p(22),wide(chosen->host),hostId.empty() ? chosen->host : hostId,panel);}
            text(dc,wide(chosen->name.empty() ? chosen->host+"'s room" : chosen->name),g.details+20,(g.detailsTop+118),g.detailsWidth()-40,64,fonts.title,ink,DT_LEFT|DT_WORDBREAK|DT_END_ELLIPSIS|DT_NOPREFIX);
            text(dc,L"Hosted by "+wide(chosen->host),g.details+20,(g.detailsTop+187),g.detailsWidth()-40,22,fonts.caption,muted,DT_LEFT|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
            fill(dc,{p(g.details+20),p((g.detailsTop+220)),p(g.right-20),p((g.detailsTop+221))},border);
            const auto ping=roomPings.find(chosen->id);
            const auto latency=ping==roomPings.end() ? std::wstring(L"Checking…") : ping->second<0 ? std::wstring(L"Via relay") : std::to_wstring(ping->second)+L" ms";
            const std::wstring labels[]={L"Connection",L"Access",L"Mode",L"Version"};
            const std::wstring values[]={latency,chosen->locked ? L"Password" : L"Open room",L"Versus",chosen->version==application_version ? L"Compatible" : wide(chosen->version)};
            for(int i=0;i<4;++i) {
                text(dc,labels[i],g.details+20,(g.detailsTop+238)+i*28,g.detailsWidth()/2-20,20,fonts.label,muted,DT_LEFT|DT_SINGLELINE|DT_NOPREFIX);
                text(dc,values[i],g.details+g.detailsWidth()/2,(g.detailsTop+238)+i*28,g.detailsWidth()/2-20,20,fonts.label,i==0 ? green : ink,DT_RIGHT|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
            }
        } else {
            text(dc,L"Choose a room",g.details+20,(g.detailsTop+74),g.detailsWidth()-40,74,fonts.title,ink,DT_LEFT|DT_WORDBREAK|DT_NOPREFIX);
            text(dc,L"Select a room to see its host, connection and access before joining.",g.details+20,(g.detailsTop+170),g.detailsWidth()-40,84,fonts.body,muted,DT_LEFT|DT_WORDBREAK|DT_NOPREFIX);
        }
        text(dc,chosen && chosen->locked ? L"Enter the room password above." : L"Both players must be ready to start.",g.details+20,g.bottom-20,g.detailsWidth()-40,17,fonts.label,faint,DT_CENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
        caps(dc,L"LATEST PATCH NOTES",g.left+22,g.notesTop+12,content-184,orange);
        text(dc,application_highlights,g.left+22,g.notesTop+36,content-200,34,fonts.caption,muted,DT_LEFT|DT_WORDBREAK|DT_END_ELLIPSIS|DT_NOPREFIX);
    }
    struct CardFace {
        std::wstring name,title,featured,tagline,fighter,since;
        std::string id;
        COLORREF nameColor=0,accent=0;
        unsigned effect=0,matches=0,friends=0,backdrop=0;
        int frame=-1;
        std::vector<std::pair<std::wstring,COLORREF>> badges;
        std::optional<identity::Tag> tag;
    };
    void outlinedText(HDC dc,const std::wstring& value,int x,int y,int width,int height,HFONT font,COLORREF fill,UINT flags) {
        const COLORREF inkTone=RGB(14,8,6);
        for(const auto [dx,dy]:{std::pair{-1,0},{1,0},{0,-1},{0,1},{1,1},{2,2}})text(dc,value,x+dx,y+dy,width,height,font,inkTone,flags);
        text(dc,value,x,y,width,height,font,fill,flags);
    }
    void paintCardFace(HDC dc,RECT card,const CardFace& face) {
        const int cl=card.left,ct=card.top,cr=card.right,cb=card.bottom,cw=cr-cl,ch=cb-ct;
        Gdiplus::Graphics g(dc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        const auto P=[&](int v){return Gdiplus::REAL(p(v));};
        const auto rgba=[](COLORREF c,int a){return Gdiplus::Color(BYTE(std::clamp(a,0,255)),GetRValue(c),GetGValue(c),GetBValue(c));};
        const Gdiplus::Color inkColor(255,14,8,6);
        const auto rounded=[&](Gdiplus::GraphicsPath& path,float x,float y,float w,float h,float r){
            path.AddArc(x,y,r*2,r*2,180,90);path.AddArc(x+w-r*2,y,r*2,r*2,270,90);path.AddArc(x+w-r*2,y+h-r*2,r*2,r*2,0,90);path.AddArc(x,y+h-r*2,r*2,r*2,90,90);path.CloseFigure();
        };
        Gdiplus::GraphicsPath body;rounded(body,P(cl),P(ct),P(cw),P(ch),P(12));
        Gdiplus::LinearGradientBrush base(Gdiplus::PointF(0,P(ct)),Gdiplus::PointF(0,P(cb)),rgba(mix(waveSkyTop(),RGB(10,8,20),120),235),rgba(RGB(8,6,14),235));
        g.FillPath(&base,&body);
        g.SetClip(&body);
        if(face.tag)paintTagTheme(dc,*face.tag,cl,ct,cr,cb);
        paintBackdrop(dc,{p(cl),p(ct),p(cr),p(cb)},face.backdrop,face.accent);
        {
            uint32_t seed=0x51f15eedu;
            const auto next=[&]{seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return float(seed&0xffffu)/65535.f;};
            for(int i=0;i<70;++i){const float sx=P(cl)+next()*P(cw),sy=P(ct)+next()*P(ch),size=P(1)*(0.5f+next()*1.1f);
                Gdiplus::SolidBrush dot(rgba(RGB(220,230,255),int(60+next()*150)));g.FillEllipse(&dot,sx-size,sy-size,size*2,size*2);}
            const float gx=P(cr-160),gy=P(ct+ch/2);
            for(int i=6;i>=1;--i){const float rx=P(150)*float(i)/6.f,ry=P(46)*float(i)/6.f;
                const auto state=g.Save();g.TranslateTransform(gx,gy);g.RotateTransform(-24.f);
                Gdiplus::SolidBrush band(rgba(mix(face.accent,RGB(210,200,255),150),6+(6-i)*3));g.FillEllipse(&band,-rx,-ry,rx*2,ry*2);g.Restore(state);}
            {const auto state=g.Save();g.TranslateTransform(gx,gy);g.RotateTransform(-24.f);
                Gdiplus::GraphicsPath core;core.AddEllipse(-P(40),-P(14),P(80),P(28));
                Gdiplus::PathGradientBrush glow(&core);glow.SetCenterColor(rgba(RGB(255,240,220),90));Gdiplus::Color edge=rgba(face.accent,0);int one=1;glow.SetSurroundColors(&edge,&one);
                g.FillPath(&glow,&core);g.Restore(state);}
            const float acx=P(cl+118),acy=P(ct+ch/2-12);
            Gdiplus::GraphicsPath nebula;nebula.AddEllipse(acx-P(150),acy-P(120),P(300),P(240));
            Gdiplus::PathGradientBrush cloud(&nebula);cloud.SetCenterColor(rgba(face.accent,80));Gdiplus::Color clear=rgba(face.accent,0);int one=1;cloud.SetSurroundColors(&clear,&one);
            g.FillPath(&cloud,&nebula);
            Gdiplus::GraphicsPath inner;inner.AddEllipse(acx-P(90),acy-P(80),P(180),P(160));
            Gdiplus::PathGradientBrush core(&inner);core.SetCenterColor(rgba(mix(face.accent,RGB(255,255,255),140),70));Gdiplus::Color none=rgba(RGB(255,255,255),0);core.SetSurroundColors(&none,&one);
            g.FillPath(&core,&inner);
        }
        const int strip=34;
        Gdiplus::SolidBrush stripFill(rgba(RGB(8,6,12),200));g.FillRectangle(&stripFill,P(cl),P(cb-strip),P(cw),P(strip));
        Gdiplus::Pen stripLine(rgba(face.accent,170),P(1));g.DrawLine(&stripLine,P(cl),P(cb-strip),P(cr),P(cb-strip));
        const int sl=cl+232,sr=cr-18,st=ct+16,sb=ct+66,skew=18;
        const Gdiplus::PointF shadow[]={{P(sl+skew+6),P(st+6)},{P(sr+6),P(st+6)},{P(sr-skew+6),P(sb+6)},{P(sl+6),P(sb+6)}};
        Gdiplus::SolidBrush shadowFill(rgba(RGB(0,0,0),120));g.FillPolygon(&shadowFill,shadow,4);
        const Gdiplus::PointF slash[]={{P(sl+skew),P(st)},{P(sr),P(st)},{P(sr-skew),P(sb)},{P(sl),P(sb)}};
        Gdiplus::LinearGradientBrush slashFill(Gdiplus::PointF(0,P(st)),Gdiplus::PointF(0,P(sb)),rgba(mix(face.accent,RGB(255,255,255),70),240),rgba(mix(face.accent,RGB(0,0,0),60),240));
        g.FillPolygon(&slashFill,slash,4);
        Gdiplus::SolidBrush gloss(rgba(RGB(255,255,255),50));
        const Gdiplus::PointF shine[]={{P(sl+skew),P(st)},{P(sr),P(st)},{P(sr-skew*0.4f),P(st+12)},{P(sl+skew*0.6f),P(st+12)}};g.FillPolygon(&gloss,shine,4);
        {
            Gdiplus::GraphicsPath slashPath;slashPath.AddPolygon(slash,4);
            Gdiplus::Region outer;g.GetClip(&outer);g.SetClip(&slashPath,Gdiplus::CombineModeIntersect);
            Gdiplus::SolidBrush lowerBand(rgba(RGB(0,0,0),40));g.FillRectangle(&lowerBand,P(sl),P(sb-12),P(sr-sl),P(12));
            g.SetClip(&outer);
            const Gdiplus::PointF innerLine[]={{P(sl+skew+6),P(st+5)},{P(sr-7),P(st+5)},{P(sr-skew-3),P(sb-5)},{P(sl+3),P(sb-5)}};
            Gdiplus::Pen innerPen(rgba(RGB(255,255,255),110),P(1));g.DrawPolygon(&innerPen,innerLine,4);
        }
        Gdiplus::Pen slashInk(rgba(RGB(255,255,255),70),P(1));slashInk.SetLineJoin(Gdiplus::LineJoinMiter);g.DrawPolygon(&slashInk,slash,4);
        g.ResetClip();
        Gdiplus::Pen border(rgba(RGB(4,4,10),230),P(2));g.DrawPath(&border,&body);
        Gdiplus::GraphicsPath inner;rounded(inner,P(cl+4),P(ct+4),P(cw-8),P(ch-8),P(9));
        Gdiplus::Pen bevel(rgba(face.accent,150),P(1));g.DrawPath(&bevel,&inner);
        Gdiplus::Pen bracket(rgba(face.accent,255),P(3));bracket.SetStartCap(Gdiplus::LineCapSquare);bracket.SetEndCap(Gdiplus::LineCapSquare);
        const int bl=18;
        for(const auto [bx,by,dx,dy]:{std::array<int,4>{cl+8,ct+8,1,1},{cr-8,ct+8,-1,1},{cl+8,cb-8,1,-1},{cr-8,cb-8,-1,-1}}){
            const Gdiplus::PointF corner[]={{P(bx),P(by+dy*bl)},{P(bx),P(by)},{P(bx+dx*bl),P(by)}};g.DrawLines(&bracket,corner,3);}
        avatar(dc,p(cl+116),p(ct+ch/2-12),p(52),face.name,face.id,panel,-1,false,face.frame);
        const unsigned level=profileLevel(face.matches);
        const int tx=sl+skew+10,tw=std::max(120,sr-skew-tx-10);
        const COLORREF inkTone=RGB(14,8,6);
        text(dc,face.name,tx+1,st+6,tw,44,fonts.title,inkTone,DT_LEFT|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
        nameText(dc,face.name,tx,st+4,tw,44,fonts.title,face.nameColor,DT_LEFT|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX,face.id,face.effect);
        int y=sb+10;
        if(!face.title.empty()) {
            const int width=measure(dc,face.title,fonts.label)+36;
            const Gdiplus::PointF ribbon[]={{P(sl+10),P(y)},{P(sl+10+width),P(y)},{P(sl+width),P(y+22)},{P(sl),P(y+22)}};
            Gdiplus::SolidBrush ribbonFill(rgba(RGB(24,14,8),235));g.FillPolygon(&ribbonFill,ribbon,4);
            Gdiplus::Pen ribbonEdge(rgba(RGB(255,206,72),255),P(2));g.DrawPolygon(&ribbonEdge,ribbon,4);
            text(dc,face.title,sl+16,y+2,width-24,18,fonts.label,RGB(255,214,100),DT_LEFT|DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX);
            y+=30;
        }
        if(!face.featured.empty()) {
            const float mx=P(sl+12),my=P(y+10),mr=P(9);
            Gdiplus::SolidBrush medalInk(inkColor);g.FillEllipse(&medalInk,mx-mr-P(2),my-mr-P(2),(mr+P(2))*2,(mr+P(2))*2);
            Gdiplus::SolidBrush medal(rgba(RGB(255,200,60),255));g.FillEllipse(&medal,mx-mr,my-mr,mr*2,mr*2);
            Gdiplus::PointF starPoints[10];for(int i=0;i<10;++i){const float a=-3.14159f/2+i*3.14159f/5,rr=i%2 ? mr*0.4f : mr*0.8f;starPoints[i]={mx+std::cos(a)*rr,my+std::sin(a)*rr};}
            Gdiplus::SolidBrush starFill(rgba(RGB(150,70,10),255));g.FillPolygon(&starFill,starPoints,10);
            outlinedText(dc,face.featured,sl+28,y,tw,20,fonts.label,RGB(255,226,140),DT_LEFT|DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
            y+=24;
        }
        outlinedText(dc,face.tagline,sl,y,tw+skew,20,fonts.caption,RGB(240,236,226),DT_LEFT|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
        y+=24;
        if(!face.fighter.empty()) {
            const std::wstring mains=L"MAIN  "+face.fighter;
            const int width=measure(dc,mains,fonts.label)+24;
            Gdiplus::GraphicsPath plate;rounded(plate,P(sl),P(y),P(width),P(20),P(5));
            Gdiplus::SolidBrush plateFill(rgba(face.accent,230));g.FillPath(&plateFill,&plate);
            Gdiplus::Pen plateInk(inkColor,P(2));g.DrawPath(&plateInk,&plate);
            text(dc,mains,sl+12,y+1,width-16,18,fonts.label,luminance(face.accent)>0.35 ? RGB(20,10,6) : RGB(255,255,255),DT_LEFT|DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX);
            y+=28;
        }
        const int barTop=std::max(y+4,cb-strip-30),barLeft=sl,barWidth=std::min(320,cr-90-barLeft),segments=10;
        const double progress=level>=99 ? 1.0 : double(face.matches%5u)/5.0;
        Gdiplus::SolidBrush barInk(inkColor);g.FillRectangle(&barInk,P(barLeft-3),P(barTop-3),P(barWidth+6),P(18));
        for(int i=0;i<segments;++i){
            const float sx=P(barLeft+i*barWidth/segments+1),sw=P(barWidth/segments-2);
            const bool lit=(i+1)<=int(std::lround(progress*segments)) || (progress>0 && i==0);
            Gdiplus::LinearGradientBrush seg(Gdiplus::PointF(0,P(barTop)),Gdiplus::PointF(0,P(barTop+12)),rgba(lit ? RGB(255,240,120) : RGB(60,50,40),255),rgba(lit ? RGB(240,150,20) : RGB(30,24,20),255));
            g.FillRectangle(&seg,sx,P(barTop),sw,P(12));
        }
        const int badgeX=barLeft+barWidth+30,badgeY=barTop+6;
        const float lx=P(badgeX),ly=P(badgeY),lr=P(18);
        for(int i=4;i>=1;--i){Gdiplus::SolidBrush halo(rgba(RGB(255,214,120),14*(5-i)));const float hr=lr+P(3*i);g.FillEllipse(&halo,lx-hr,ly-hr,hr*2,hr*2);}
        Gdiplus::SolidBrush levelInk(inkColor);g.FillEllipse(&levelInk,lx-lr-P(3),ly-lr-P(3),(lr+P(3))*2,(lr+P(3))*2);
        Gdiplus::LinearGradientBrush levelFill(Gdiplus::PointF(0,ly-lr),Gdiplus::PointF(0,ly+lr),rgba(RGB(255,236,140),255),rgba(RGB(230,150,20),255));
        g.FillEllipse(&levelFill,lx-lr,ly-lr,lr*2,lr*2);
        text(dc,L"LV",badgeX-20,badgeY-15,40,12,fonts.label,RGB(70,30,6),DT_CENTER|DT_SINGLELINE|DT_NOPREFIX);
        text(dc,std::to_wstring(level),badgeX-20,badgeY-5,40,20,fonts.strong,RGB(40,16,4),DT_CENTER|DT_SINGLELINE|DT_NOPREFIX);
        text(dc,level>=99 ? std::wstring(L"MAX LEVEL") : std::to_wstring(5u-face.matches%5u)+L" matches to level "+std::to_wstring(level+1),barLeft+barWidth+58,barTop-3,std::max(40,cr-barLeft-barWidth-82),18,fonts.caption,RGB(230,224,210),DT_LEFT|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
        const std::wstring stats=std::to_wstring(face.matches)+L" MATCHES   \u00b7   "+std::to_wstring(face.friends)+L" FRIENDS   \u00b7   "+face.since;
        text(dc,stats,cl+20,cb-strip+8,cw/2,20,fonts.label,RGB(236,230,214),DT_LEFT|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
        if(!face.badges.empty()) {
            int x=cr-16;
            for(size_t i=0;i<face.badges.size() && i<4;++i){const int width=unscale(tagWidth(dc,face.badges[i].first));if(x-width<cl+cw/2)break;tag(dc,p(x),p(cb-strip/2),face.badges[i].first,face.badges[i].second,false);x-=width+6;}
        }
    }
    static constexpr std::array<unsigned,5> galaxyOrder{0u,1u,3u,4u,2u};
    static int galaxyIndex(unsigned value) {
        for(size_t i=0;i<galaxyOrder.size();++i)if(galaxyOrder[i]==value)return int(i);
        return 0;
    }
    static unsigned galaxyValue(int index) {return galaxyOrder[size_t(std::clamp(index,0,int(galaxyOrder.size())-1))];}
    unsigned galaxyTemplate() const {return settings.waveRibbons;}
    struct VoyageStar { float x,y,z; };
    std::vector<float> galaxyNoise,galaxyGeo;
    std::vector<uint8_t> galaxyLayer;
    std::vector<VoyageStar> voyage;
    int galaxyWidth=0,galaxyHeight=0,galaxyKind=-1;
    double voyageAt=-1.0;
    uint32_t voyageSeed=0x2545f491u;
    static float smoothStep(float a,float b,float x) {const float t=std::clamp((x-a)/(b-a),0.f,1.f);return t*t*(3.f-2.f*t);}
    void ensureGalaxyNoise() {
        if(!galaxyNoise.empty())return;
        constexpr int n=256;
        std::vector<float> base(size_t(n)*n);
        uint32_t seed=0x9e3779b9u;
        for(auto& value:base){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;value=float(seed&0xffffu)/65535.f;}
        galaxyNoise.assign(size_t(n)*n,0.f);
        float total=0.f,amplitude=0.5f;
        for(int octave=0;octave<3;++octave) {
            const int cell=64>>octave;
            const auto at=[&](int x,int y){return base[size_t(y&(n-1))*n+size_t(x&(n-1))];};
            for(int y=0;y<n;++y)for(int x=0;x<n;++x) {
                const int x0=x/cell*cell,y0=y/cell*cell;
                const float fx=float(x-x0)/float(cell),fy=float(y-y0)/float(cell);
                const float sx=fx*fx*(3.f-2.f*fx),sy=fy*fy*(3.f-2.f*fy);
                const float a=at(x0,y0),b=at(x0+cell,y0),c=at(x0,y0+cell),d=at(x0+cell,y0+cell);
                galaxyNoise[size_t(y)*n+size_t(x)]+=amplitude*((a+(b-a)*sx)*(1.f-sy)+(c+(d-c)*sx)*sy);
            }
            total+=amplitude;amplitude*=0.5f;
        }
        for(auto& value:galaxyNoise)value/=total;
    }
    float galaxyNoiseAt(float u,float v) const {
        const float x=u*64.f,y=v*64.f,fx=std::floor(x),fy=std::floor(y);
        const int ix=int(fx)&255,iy=int(fy)&255,jx=(ix+1)&255,jy=(iy+1)&255;
        const float tx=x-fx,ty=y-fy;
        const float* table=galaxyNoise.data();
        const float a=table[iy*256+ix],b=table[iy*256+jx],c=table[jy*256+ix],d=table[jy*256+jx];
        return (a+(b-a)*tx)*(1.f-ty)+(c+(d-c)*tx)*ty;
    }
    void paintGalaxy(uint32_t* pixels,int stride,int width,int height,double now,bool advance) {
        const unsigned kind=galaxyTemplate();
        if(!pixels || width<16 || height<16 || stride<width || kind==2u)return;
        if(kind==4u){paintVoyage(pixels,stride,width,height,now,advance);return;}
        if(computeGalaxyLayer(width,height,now))blendGalaxy(pixels,stride,pixels,stride,width,height,0,1);
    }
    std::vector<int> galaxyColumns,galaxyWeights;
    std::vector<uint32_t> galaxyFrame;
    std::vector<uint8_t> galaxyNext;
    int galaxyFrameWidth=0,galaxyFrameHeight=0,galaxySlice=0;
    unsigned galaxyFrameKey=0;
    bool galaxyAny=false;
    bool computeGalaxyLayer(int width,int height,double now,int part=0,int parts=1) {
        const unsigned kind=galaxyTemplate();
        ensureGalaxyNoise();
        const int lw=width/4+2,lh=height/4+2;
        if(galaxyWidth!=width || galaxyHeight!=height || galaxyKind!=int(kind)) {
            galaxyWidth=width;galaxyHeight=height;galaxyKind=int(kind);
            galaxyGeo.assign(size_t(lw)*size_t(lh)*4u,0.f);
            const float cx=float(width)*0.74f/4.f,cy=float(height)*0.36f/4.f,radius=float(height)*0.60f/4.f;
            const float ca=std::cos(-0.30f),sa=std::sin(-0.30f);
            const float bx=float(lw),by=-0.85f*float(lh),length=std::sqrt(bx*bx+by*by),dx=bx/length,dy=by/length;
            for(int y=0;y<lh;++y)for(int x=0;x<lw;++x) {
                float* g=&galaxyGeo[(size_t(y)*size_t(lw)+size_t(x))*4u];
                if(kind==0u) {
                    const float ux=(float(x)-cx)/radius,uy=(float(y)-cy)/radius;
                    const float px=ux*ca-uy*sa,py=(ux*sa+uy*ca)/0.62f;
                    const float r=std::sqrt(px*px+py*py);
                    g[0]=r;g[1]=2.f*std::atan2(py,px)-4.5f*std::log(r+0.04f);g[2]=px;g[3]=py;
                } else if(kind==3u) {
                    const float ox=float(x),oy=float(y)-0.95f*float(lh);
                    g[0]=(ox*dx+oy*dy)/length;g[1]=(ox*-dy+oy*dx)/(0.16f*float(lh));
                }
            }
        }
        auto& layer=parts>1 ? galaxyNext : galaxyLayer;
        if(layer.size()!=size_t(lw)*size_t(lh)*3u || parts==1)layer.assign(size_t(lw)*size_t(lh)*3u,0);
        const int rowBegin=lh*part/parts,rowEnd=lh*(part+1)/parts;
        const float t=float(now),spin=t*0.025f,cs=std::cos(spin),sn=std::sin(spin);
        bool any=false;
        for(int y=rowBegin;y<rowEnd;++y)for(int x=0;x<lw;++x) {
            const size_t index=size_t(y)*size_t(lw)+size_t(x);
            const float* g=&galaxyGeo[index*4u];
            float r=0.f,gr=0.f,b=0.f;
            if(kind==0u) {
                const float distance=g[0];
                if(distance<1.5f) {
                    const float arm=0.5f+0.5f*std::cos(g[1]-2.f*spin);
                    const float nx=g[2]*cs-g[3]*sn,ny=g[2]*sn+g[3]*cs;
                    const float cloud=galaxyNoiseAt(nx*1.6f+7.f,ny*1.6f+3.f);
                    const float fall=std::exp(-distance*1.5f)*std::clamp((1.5f-distance)*2.f,0.f,1.f);
                    const float arms=arm*arm*fall*(0.3f+1.2f*cloud),core=std::exp(-distance*distance*14.f);
                    const float pink=std::max(0.f,cloud-0.58f)*arm*fall*2.5f,halo=std::exp(-distance*distance*1.8f);
                    r=arms*110.f+core*230.f+halo*28.f+pink*230.f;gr=arms*130.f+core*200.f+halo*24.f+pink*80.f;b=arms*235.f+core*170.f+halo*60.f+pink*160.f;
                }
            } else if(kind==1u) {
                const float u=float(x)/float(lw)*2.4f,v=float(y)/float(lh)*1.5f;
                const float n1=galaxyNoiseAt(u+t*0.012f,v+t*0.005f),n2=galaxyNoiseAt(u*1.8f-t*0.008f+5.3f,v*1.8f+t*0.004f+2.1f);
                const float n3=galaxyNoiseAt(u*3.5f+t*0.02f+1.7f,v*3.5f-1.2f);
                const float density=smoothStep(0.42f,0.80f,n1)*(0.55f+0.8f*n2),tint=smoothStep(0.35f,0.7f,n2);
                const float bright=smoothStep(0.62f,0.9f,n3)*density;
                const float soft=0.55f;
                r=soft*(density*(210.f*(1.f-tint)+60.f*tint)+bright*90.f);gr=soft*(density*(60.f*(1.f-tint)+170.f*tint)+bright*80.f);b=soft*(density*(200.f*(1.f-tint)+230.f*tint)+bright*110.f);
            } else {
                const float along=g[0],across=g[1],glow=std::exp(-across*across*1.6f);
                if(glow>0.01f) {
                    const float cloud=galaxyNoiseAt(along*5.f+t*0.006f,across*0.9f+t*0.002f);
                    const float lane=smoothStep(0.48f,0.72f,galaxyNoiseAt(along*3.2f+9.f,across*1.6f+4.f-t*0.003f))*std::exp(-across*across*5.f);
                    const float density=glow*(0.45f+0.9f*cloud)*(1.f-0.75f*lane),edge=std::min(1.f,std::fabs(across)*0.8f);
                    const float core=std::exp(-across*across*6.f)*smoothStep(0.55f,0.85f,cloud)*(1.f-lane);
                    r=density*(235.f-110.f*edge)+core*90.f;gr=density*(215.f-70.f*edge)+core*70.f;b=density*255.f+core*40.f;
                }
            }
            uint8_t* out=&layer[index*3u];
            out[0]=uint8_t(std::clamp(r*0.6f,0.f,255.f));out[1]=uint8_t(std::clamp(gr*0.6f,0.f,255.f));out[2]=uint8_t(std::clamp(b*0.6f,0.f,255.f));
            any=any || out[0] || out[1] || out[2];
        }
        if(parts>1 && part==parts-1)galaxyLayer.swap(galaxyNext);
        galaxyAny=any;
        return any;
    }
    void blendGalaxy(const uint32_t* source,int sourceStride,uint32_t* target,int targetStride,int width,int height,int first,int step) {
        const int lw=width/4+2,lh=height/4+2;
        if(galaxyLayer.size()!=size_t(lw)*size_t(lh)*3u)return;
        if(galaxyColumns.size()!=size_t(width)) {
            galaxyColumns.assign(size_t(width),0);galaxyWeights.assign(size_t(width),0);
            for(int x=0;x<width;++x) {
                const float sx=std::max(0.f,(float(x)+0.5f)/4.f-0.5f);
                galaxyColumns[size_t(x)]=std::min(int(sx),lw-2);galaxyWeights[size_t(x)]=int((sx-float(galaxyColumns[size_t(x)]))*256.f);
            }
        }
        for(int y=first;y<height;y+=step) {
            const float sy=std::max(0.f,(float(y)+0.5f)/4.f-0.5f);
            const int row=std::min(int(sy),lh-2),wy=int((sy-float(row))*256.f);
            const uint8_t* upper=&galaxyLayer[size_t(row)*size_t(lw)*3u];const uint8_t* lower=upper+size_t(lw)*3u;
            const uint32_t* in=source+size_t(y)*size_t(sourceStride);
            uint32_t* out=target+size_t(y)*size_t(targetStride);
            for(int x=0;x<width;++x) {
                const size_t at=size_t(galaxyColumns[size_t(x)])*3u;const int wx=galaxyWeights[size_t(x)];
                const uint32_t value=in[x];
                int add[3];
                for(int channel=0;channel<3;++channel) {
                    const int top=upper[at+size_t(channel)]*(256-wx)+upper[at+3u+size_t(channel)]*wx;
                    const int bottom=lower[at+size_t(channel)]*(256-wx)+lower[at+3u+size_t(channel)]*wx;
                    add[channel]=(top*(256-wy)+bottom*wy)>>16;
                }
                if(!(add[0]|add[1]|add[2])){out[x]=value;continue;}
                uint32_t result=value&0xff000000u;
                for(int channel=0;channel<3;++channel) {
                    const int shift=16-channel*8,dst=int((value>>shift)&255u);
                    result|=uint32_t(dst+add[channel]-((dst*add[channel]+255)>>8))<<shift;
                }
                out[x]=result;
            }
        }
    }
    void paintVoyage(uint32_t* pixels,int stride,int width,int height,double now,bool advance) {
        const auto next=[&]{voyageSeed^=voyageSeed<<13;voyageSeed^=voyageSeed>>17;voyageSeed^=voyageSeed<<5;return float(voyageSeed&0xffffu)/65535.f;};
        if(voyage.empty())for(int i=0;i<360;++i)voyage.push_back({next()*2.f-1.f,next()*2.f-1.f,0.05f+next()*0.95f});
        float dt=0.f;
        if(advance){dt=voyageAt<0.0 ? 0.f : float(std::clamp(now-voyageAt,0.0,0.1));voyageAt=now;}
        const float cx=float(width)*0.5f,cy=float(height)*0.45f,scale=float(std::max(width,height))*0.18f;
        const auto plot=[&](int x,int y,float alpha) {
            if(x<0 || y<0 || x>=width || y>=height || alpha<=0.f)return;
            uint32_t& dst=pixels[size_t(y)*size_t(stride)+size_t(x)];
            const auto add=[&](int shift,float color){const int v=int((dst>>shift)&255u)+int(color*alpha);dst=(dst&~(255u<<shift))|(uint32_t(std::min(255,v))<<shift);};
            add(16,200.f);add(8,222.f);add(0,255.f);
        };
        for(auto& star:voyage) {
            star.z-=dt*0.06f;
            float px=cx+star.x/star.z*scale,py=cy+star.y/star.z*scale;
            if(star.z<=0.04f || px<0.f || py<0.f || px>=float(width) || py>=float(height)) {
                star={next()*2.f-1.f,next()*2.f-1.f,1.f};
                continue;
            }
            const float closeness=1.f-star.z,alpha=std::min(1.f,0.15f+closeness*closeness*0.95f);
            const float tz=std::min(1.f,star.z+0.05f),tx=cx+star.x/tz*scale,ty=cy+star.y/tz*scale;
            const int steps=int(std::clamp(std::hypot(px-tx,py-ty),1.f,float(p(14))));
            for(int s=1;s<=steps;++s){const float f=float(s)/float(steps+1);plot(int(px+(tx-px)*f),int(py+(ty-py)*f),alpha*(1.f-f)*0.5f);}
            plot(int(px),int(py),alpha);
            if(closeness>0.7f){plot(int(px)+1,int(py),alpha*0.7f);plot(int(px),int(py)+1,alpha*0.7f);plot(int(px)+1,int(py)+1,alpha*0.5f);}
        }
    }
    struct SkyStar { float x,y,phase,speed,bright; bool big; };
    std::vector<SkyStar> skyStars;
    int skyWidth=0,skyHeight=0;
    double shootingAt=-1.0,shootingNext=4.0;
    float shootingX=0,shootingY=0,shootingAngle=0.35f;
    bool skyTicking=false;
    void ensureSky(int width,int height) {
        if(width==skyWidth && height==skyHeight && !skyStars.empty())return;
        skyWidth=width;skyHeight=height;skyStars.clear();
        uint32_t seed=0x5eedu;
        const auto next=[&]{seed=seed*1664525u+1013904223u;return float((seed>>8)&0xffffu)/65535.f;};
        const int count=std::clamp(width*height/6500,60,700);
        for(int i=0;i<count;++i){SkyStar star{};star.x=next();star.y=next();star.phase=next()*6.2832f;star.speed=0.6f+next()*1.6f;star.bright=0.35f+next()*0.55f;star.big=next()>0.9f;skyStars.push_back(star);}
    }
    template<class Visible> void drawSky(uint32_t* pixels,int width,int height,int top,int bottom,Visible visible) {
        if(!pixels || width<=0 || height<=0)return;
        ensureSky(width,height);
        const bool moving=allowMotion();
        const double now=moving ? double(motionTicks())/1000.0 : 0.0;
        const auto plot=[&](int px,int py,uint32_t r,uint32_t g,uint32_t b,float alpha) {
            if(px<0 || py<top || px>=width || py>=bottom || py>=height || alpha<=0.f || !visible(px,py))return;
            uint32_t& dst=pixels[size_t(py)*size_t(width)+size_t(px)];
            const uint32_t a=uint32_t(std::clamp(alpha,0.f,1.f)*255.f),k=255u-a;
            const uint32_t rr=(((dst>>16)&255u)*k+r*a)/255u,gg=(((dst>>8)&255u)*k+g*a)/255u,bb=((dst&255u)*k+b*a)/255u;
            dst=(dst&0xff000000u)|(rr<<16)|(gg<<8)|bb;
        };
        for(const auto& star:skyStars) {
            const int sx=int(star.x*float(width)),sy=int(star.y*float(height));
            const float twinkle=moving ? 0.55f+0.45f*float(std::sin(now*star.speed+star.phase)) : 0.8f;
            const float alpha=std::min(1.f,star.bright*twinkle*1.05f);
            plot(sx,sy,150,215,240,alpha);
            if(star.big){plot(sx+1,sy,150,215,240,alpha*0.8f);plot(sx,sy+1,150,215,240,alpha*0.8f);plot(sx+1,sy+1,150,215,240,alpha*0.6f);}
        }
        if(!moving)return;
        if(shootingAt<0 && now>=shootingNext) {
            uint32_t seed=uint32_t(now*1000.0);
            const auto next=[&]{seed=seed*1664525u+1013904223u;return float((seed>>8)&0xffffu)/65535.f;};
            shootingAt=now;shootingX=0.1f+next()*0.6f;shootingY=0.05f+next()*0.35f;shootingAngle=0.25f+next()*0.25f;
        }
        if(shootingAt>=0) {
            const double age=(now-shootingAt)/0.9;
            if(age>=1.0){shootingAt=-1.0;shootingNext=now+8.0+std::fmod(now*7.31,6.0);return;}
            const float travel=float(p(260))*float(1.0-std::pow(1.0-age,2.0)),tail=float(p(120))*float(std::sin(age*3.14159));
            const float hx=shootingX*float(width)+std::cos(shootingAngle)*travel,hy=shootingY*float(height)+std::sin(shootingAngle)*travel;
            const float fade=float(std::min(1.0,std::min(age*5.0,(1.0-age)*4.0)));
            const int samples=std::max(8,int(tail));
            for(int i=0;i<=samples;++i) {
                const float t=float(i)/float(samples),px=hx-std::cos(shootingAngle)*tail*t,py=hy-std::sin(shootingAngle)*tail*t;
                plot(int(px),int(py),190,235,250,(1.f-t)*0.9f*fade);
            }
            plot(int(hx)+1,int(hy),255,255,255,fade);plot(int(hx),int(hy)+1,255,255,255,fade*0.8f);plot(int(hx),int(hy),255,255,255,fade);
        }
    }
    void animateSky() {
        if(!waveStyle() || page==wavePage || !hwnd){KillTimer(hwnd,11);endFrames(11);skyTicking=false;return;}
        if(IsIconic(hwnd) || dropdownOpen() || transitionAt)return;
        const bool shooting=shootingAt>=0 || (allowMotion() && double(motionTicks())/1000.0>=shootingNext-0.05);
        if(shooting)frames(11);else{endFrames(11);SetTimer(hwnd,11,33,nullptr);}
        motionTick=false;
        RECT area{};GetClientRect(hwnd,&area);area.top=p(shellHeader);area.bottom=std::max(area.top,area.bottom-p(dockReserve()));
        InvalidateRect(hwnd,&area,FALSE);
    }
    void paintProfileWave(HDC dc,int content) {
        const bool perks=heldTag().has_value();
        const auto g=profileWave(content,perks);
        panelBox(dc,g.left,124,g.right,226);
        heading(dc,L"Active profile",g.left+24,136,content-48);
        const auto profile=activeProfile();if(!profile)return;
        const auto style=profileStyle(profile->id);
        panelBox(dc,g.left,g.hero,g.right,g.heroBottom);
        caps(dc,L"AS PLAYERS SEE YOU",g.left+24,g.hero+14,200);
        CardFace face;
        face.name=wide(profile->name);face.id=profile->id;face.nameColor=readableName(chosenNameColor(),ink);face.effect=chosenEffect();
        const int chosenTitle=std::max(0,selected(ProfileTitle));
        if(chosenTitle>0 && unsigned(chosenTitle)<title_count)face.title=wide(title_choices[size_t(chosenTitle)].name);
        face.featured=featuredTitle(unsigned(std::max(0,selected(ProfileFeatured))),achievementMask(),holdsDevKey());
        face.tagline=style && !style->tagline.empty() ? L"\u201c"+wide(style->tagline)+L"\u201d" : std::wstring(L"Add a tagline below.");
        if(const int fighter=selected(ProfileFighter);fighter>0 && unsigned(fighter)<=fighter_count)face.fighter=wide(fighter_names[size_t(fighter-1)]);
        face.matches=style ? style->matches : 0u;face.friends=unsigned(friendCount());
        face.since=style && style->since>0 ? dateText(style->since) : std::wstring(L"First time online");
        auto mine=shownCard(profile->id);
        if(!mine.shared && style) {
            mine.shared=true;mine.card.matches=style->matches;mine.card.since=style->since;mine.card.tagline=style->tagline;
            mine.card.about=style->about;mine.card.fighter=style->fighter;mine.image=profileImage(style->image);
        }
        face.badges=profileBadges(mine,profile->id,face.name);
        face.backdrop=unsigned(std::max(0,selected(ProfileBackdrop)));face.frame=std::max(0,selected(ProfileFrame));
        const int accentIndex=std::max(0,selected(ProfileAccent));
        face.accent=accentIndex>0 && unsigned(accentIndex)<accent_count && unsigned(accentIndex)!=rgb_accent ? colorOf(accents[accentIndex].color) : orange;
        face.tag=heldTag();
        paintCardFace(dc,{g.left+14,g.hero+34,g.pictureLeft-22,g.heroBottom-14},face);
        caps(dc,L"PICTURE FRAMING",g.pictureLeft,g.hero+162,260);
        text(dc,style && !style->imageName.empty() ? wide(style->imageName) : L"PNG, JPEG or animated GIF",g.pictureLeft,g.hero+232,260,20,fonts.caption,ink,DT_LEFT|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
        const int sharing=selected(ProfileSharing);
        text(dc,sharing==2 ? L"Up to 16 MB. Your picture and card stay on this PC." :
                sharing==1 ? L"Up to 16 MB. Your picture stays on this PC; players see the rest of your card." :
                             L"Up to 16 MB. Players see a small copy; animated pictures share their first frame.",
             g.pictureLeft,g.hero+256,260,60,fonts.caption,muted,DT_WORDBREAK|DT_NOPREFIX);
        const int lookRight=g.left+g.half,label=g.label(g.half);
        panelBox(dc,g.left,g.look,lookRight,g.lookBottom);
        heading(dc,L"Card look",g.left+24,g.look+18,g.half-48);
        text(dc,L"Shown on your card online",g.left+24,g.look+20,g.half-48,20,fonts.caption,muted,DT_RIGHT|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
        const wchar_t* lookRows[]={L"Frame",L"Frame color",L"Card design",L"Title",L"Featured achievement",L"Avatar accent",L"Name color"};
        for(int i=0;i<7;++i)waveRow(dc,g.left+24,lookRight-24,g.rowY(g.look,i),label,lookRows[i],ink);
        panelBox(dc,g.rightLeft,g.look,g.right,g.lookBottom);
        heading(dc,L"About you",g.rightLeft+24,g.look+18,g.half-48);
        const wchar_t* aboutRows[]={L"Initials",L"Main fighter",L"Tagline",L"About me",L"Region",L"Playstyle"};
        for(int i=0;i<6;++i)waveRow(dc,g.rightLeft+24,g.right-24,g.rowY(g.look,i),label,aboutRows[i],ink);
        text(dc,L"Initials show when you have no picture. Titles unlock with levels and achievements.",g.rightLeft+24,g.rowY(g.look,6)+14,g.half-48,40,fonts.caption,muted,DT_WORDBREAK|DT_NOPREFIX);
        panelBox(dc,g.left,g.privacy,g.right,g.privacyBottom);
        heading(dc,L"Privacy & alerts",g.left+24,g.privacy+18,content-48);
        waveRow(dc,g.left+24,lookRight-24,g.rowY(g.privacy,0),label,L"Availability",ink);
        waveRow(dc,g.left+24,lookRight-24,g.rowY(g.privacy,1),label,L"Invites & requests",ink);
        waveRow(dc,g.rightLeft+24,g.right-24,g.rowY(g.privacy,0),label,L"Player card online",ink);
        waveRow(dc,g.rightLeft+24,g.right-24,g.rowY(g.privacy,1),label,L"Friends coming online",ink);
        text(dc,L"Availability is shared online. Quiet mode and Do not disturb keep requests without desktop alerts.",g.left+24,g.rowY(g.privacy,2)+10,content-48,22,fonts.caption,muted,DT_LEFT|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
        panelBox(dc,g.left,g.actions,g.right,g.actionsBottom);
        text(dc,L"Changes show on your card after you save.",g.left+24+540,g.actions+30,content-48-540,20,fonts.caption,muted,DT_LEFT|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
        const int wideLabel=std::clamp(content/4,180,280);
        waveRow(dc,g.left+24,g.right-24,g.actions+76,wideLabel,L"Copy appearance from",ink);
        if(const auto tag=heldTag())waveRow(dc,g.left+24,g.right-24,g.actions+132,wideLabel,wide(identity::tag_label(*tag))+L" channel link",readableName(storedColor(tagColor(*tag)),ink));
    }
    void paintProfile(HDC dc,int h,int content) {
        if(waveStyle()){paintProfileWave(dc,content);return;}
        const int right=contentLeft+content,split=contentLeft+std::clamp(content/3,260,340),side=split+44,width=right-side-24;
        panelBox(dc,contentLeft,124,right,226);
        heading(dc,L"Active profile",contentLeft+24,136,content-48);
        const bool perks=heldTag().has_value();
        panelBox(dc,contentLeft,246,split,1176);panelBox(dc,split+20,246,right,perks || waveStyle() ? 1176 : 1076);
        if(perks && !waveStyle()) {
            const auto tag=*heldTag();const int perkSide=split+44,perkWidth=right-perkSide-24;
            caps(dc,wide(identity::tag_label(tag))+L" CHANNEL LINK",perkSide,1100,perkWidth,readableName(storedColor(tagColor(tag)),ink));
            text(dc,L"Players see an Open channel button on your card",perkSide+perkWidth/3,1098,perkWidth-perkWidth/3,18,fonts.caption,faint,DT_RIGHT|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
        }
        const auto profile=activeProfile();if(!profile)return;
        const auto style=profileStyle(profile->id);
        if(perks)paintTagTheme(dc,*heldTag(),contentLeft,246,split,1176);
        paintBackdrop(dc,{p(contentLeft),p(246),p(split),p(1176)},unsigned(std::max(0,selected(ProfileBackdrop))),orange);
        heading(dc,L"Your player card",contentLeft+24,266,split-contentLeft-48);
        avatar(dc,p((contentLeft+split)/2),p(340),p(42),wide(profile->name),profile->id,panel,-1,false,std::max(0,selected(ProfileFrame)));
        nameText(dc,wide(profile->name),contentLeft+24,396,split-contentLeft-48,24,fonts.brand,readableName(chosenNameColor(),ink),DT_CENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX,profile->id,chosenEffect());
        const int chosenTitle=std::max(0,selected(ProfileTitle));
        if(chosenTitle>0 && unsigned(chosenTitle)<title_count)
            text(dc,wide(title_choices[size_t(chosenTitle)].name),contentLeft+24,422,split-contentLeft-48,18,fonts.label,gold,DT_CENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
        text(dc,style && !style->tagline.empty() ? L"\u201c"+wide(style->tagline)+L"\u201d" : L"Add a tagline to your player card.",contentLeft+24,442,split-contentLeft-48,22,fonts.caption,muted,DT_CENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
        if(const int fighter=selected(ProfileFighter);fighter>0 && unsigned(fighter)<=fighter_count)
            text(dc,L"MAINS "+wide(fighter_names[size_t(fighter-1)]),contentLeft+24,464,split-contentLeft-48,18,fonts.label,orange,DT_CENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
        caps(dc,L"PICTURE FRAMING",contentLeft+24,640,split-contentLeft-48);
        text(dc,style && !style->imageName.empty() ? wide(style->imageName) : L"PNG, JPEG or animated GIF",contentLeft+24,728,split-contentLeft-48,40,fonts.caption,muted,DT_WORDBREAK|DT_END_ELLIPSIS|DT_NOPREFIX);
        const int sharing=selected(ProfileSharing);
        text(dc,sharing==2 ? L"Up to 16 MB. Your original picture is kept. Your picture and card stay on this PC." :
                sharing==1 ? L"Up to 16 MB. Your original picture is kept. Your picture stays on this PC; players online see the rest of your card." :
                             L"Up to 16 MB. Your original picture is kept. Players online see a small copy of your picture and your card. Animated pictures share their first frame.",
             contentLeft+24,790,split-contentLeft-48,90,fonts.caption,muted,DT_WORDBREAK|DT_NOPREFIX);
        caps(dc,L"ONLINE STATS",contentLeft+24,906,split-contentLeft-48);
        const auto stat=[&](int y,const std::wstring& label,const std::wstring& value){
            text(dc,label,contentLeft+24,y,split-contentLeft-48,22,fonts.caption,muted,DT_LEFT|DT_SINGLELINE|DT_NOPREFIX);
            text(dc,value,contentLeft+24,y,split-contentLeft-54,22,fonts.strong,ink,DT_RIGHT|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
        };
        stat(934,L"Online matches",std::to_wstring(style ? style->matches : 0u));
        stat(962,L"Member since",style && style->since>0 ? dateText(style->since) : std::wstring(L"First time online"));
        stat(990,L"Friends",std::to_wstring(friendCount()));
        stat(1018,L"Recent opponents",std::to_wstring(recentCount()));
        {
            auto mine=shownCard(profile->id);
            if(!mine.shared && style) {
                mine.shared=true;mine.card.matches=style->matches;mine.card.since=style->since;mine.card.tagline=style->tagline;
                mine.card.about=style->about;mine.card.fighter=style->fighter;mine.image=profileImage(style->image);
            }
            paintLevel(dc,mine,contentLeft+24,1050,split-contentLeft-48);
            paintBadges(dc,profileBadges(mine,profile->id,wide(profile->name)),(contentLeft+split)/2,1100,split-contentLeft-48);
        }
        {
            const int top=1196,third=(content-48-32)/3;
            panelBox(dc,contentLeft,top,right,1400);
            heading(dc,L"Card style",contentLeft+24,top+14,content-48);
            text(dc,L"Frame, card design and title. Players online see them on your card.",contentLeft+150,top+17,content-174,20,fonts.caption,muted,DT_LEFT|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
            caps(dc,L"FRAME",contentLeft+24,top+48,third);
            caps(dc,L"CARD DESIGN",contentLeft+24+third+16,top+48,third);
            caps(dc,L"TITLE",contentLeft+24+2*(third+16),top+48,third);
            caps(dc,L"FRAME COLOR",contentLeft+24,top+128,third);
            caps(dc,L"FEATURED ACHIEVEMENT",contentLeft+24+third+16,top+128,2*third+16);
        }
        heading(dc,L"Appearance & notifications",side,266,width);
        if(waveStyle()) {
            const int labelWidth=std::clamp(width*2/5,150,300);
            std::vector<std::wstring> rows{L"Initials",L"Avatar accent",L"Name color",L"Main fighter",L"Tagline",L"About me",L"Region",L"Playstyle",
                                           L"Availability",L"Invites & requests",L"Player card online",L"Friends coming online"};
            const auto rowLine=[&](int y,const std::wstring& label,COLORREF tone){
                Gdiplus::Graphics graphics(dc);
                Gdiplus::LinearGradientBrush divider(Gdiplus::PointF(Gdiplus::REAL(p(side)),0),Gdiplus::PointF(Gdiplus::REAL(p(right-24)),0),Gdiplus::Color(70,255,236,210),Gdiplus::Color(8,255,236,210));
                graphics.FillRectangle(&divider,Gdiplus::REAL(p(side)),Gdiplus::REAL(p(y)),Gdiplus::REAL(p(right-24-side)),Gdiplus::REAL(std::max(1,p(1))));
                text(dc,label,side,y+14,labelWidth-12,28,fonts.body,tone,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
            };
            for(size_t i=0;i<rows.size();++i)rowLine(312+int(i)*waveOptionRow,rows[i],ink);
            text(dc,L"Availability is shared online. Quiet mode and Do not disturb keep requests without desktop alerts.",side,1044,width,24,fonts.caption,muted,DT_LEFT|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
            rowLine(1076,L"Copy appearance from",ink);
            if(const auto tag=heldTag())rowLine(1132,wide(identity::tag_label(*tag))+L" channel link",readableName(storedColor(tagColor(*tag)),ink));
            return;
        }
        const int half=(width-16)/2,second=side+(width+16)/2;
        caps(dc,L"INITIALS",side,306,half);
        caps(dc,L"AVATAR ACCENT",second,306,half);
        caps(dc,L"NAME COLOR",side,386,half);
        caps(dc,L"MAIN FIGHTER",second,386,half);
        caps(dc,L"TAGLINE",side,466,width);
        caps(dc,L"ABOUT ME",side,546,width);
        caps(dc,L"REGION",side,626,half);
        caps(dc,L"PLAYSTYLE",second,626,half);
        caps(dc,L"ONLINE AVAILABILITY",side,706,half);
        caps(dc,L"INVITES & FRIEND REQUESTS",second,706,half);
        caps(dc,L"PLAYER CARD ONLINE",side,786,half);
        caps(dc,L"FRIENDS COMING ONLINE",second,786,half);
        text(dc,L"Availability is shared online. Quiet mode and Do not disturb keep requests in Notifications without desktop alerts.",side,922,width,40,fonts.caption,muted,DT_WORDBREAK|DT_NOPREFIX);
        caps(dc,L"COPY APPEARANCE FROM ANOTHER PROFILE",side,966,width);
        text(dc,L"Copies the picture, initials, tagline, about, accent, name color, main fighter, region and playstyle. Your name, friends and stats stay yours.",side,1036,width,36,fonts.caption,muted,DT_WORDBREAK|DT_NOPREFIX);
    }
    void paintSaves(HDC dc, int h, int content) {
        const int right = contentLeft + content, panelBottom = h - 64;
        panelBox(dc, contentLeft, 124, right, 194);
        heading(dc, L"Automatic backups", contentLeft + 20, 138, content - 220);
        text(dc, L"Copies your memory card before and after every game session.", contentLeft + 20, 160,
             content - 220, 18, fonts.caption, muted, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

        panelBox(dc, contentLeft, 206, right, 322);
        heading(dc, L"Save tools", contentLeft + 20, 218, content - 40);
        text(dc, L"Manage your memory card and unlocks. Every change is backed up first.",
             contentLeft + 20, 290, content - 40, 18, fonts.caption, muted,
             DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

        panelBox(dc, contentLeft, 334, right, panelBottom);

        heading(dc, gameSaveList() ? L"Memory card" : L"Backup history", contentLeft + 20, 348, content - 390);
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
            const int bottom=std::max(478,panelBottom-84),middle=(430+bottom)/2;
            const bool filtered = !trimmed(textOf(SaveSearch)).empty();
            box(dc,{p(contentLeft+20),p(430),p(right-20),p(bottom)},field,border,8);
            text(dc,disc ? (filtered ? L"No matching items" : gameSaveList() ? L"No game saves yet" : L"No backups yet") : L"Choose your game disc",contentLeft+36,middle-28,
                 content-72,24,fonts.strong,ink,DT_CENTER|DT_SINGLELINE|DT_NOPREFIX);
            text(dc,disc ? (filtered ? L"Clear your search to see all saves and backups." : gameSaveList() ? L"Create a save in the game, or import an existing save above." : L"Back up your save before importing or unlocking content.")
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
        OffsetRect(&r, 0, -p(shellHeader - pageScroll));
        InflateRect(&r, p(10), p(7));
        box(dc, r, field, GetFocus() == control ? orange : border, 8);
    }
    void paintMods(HDC dc, int h, int content) {
        const int right = contentLeft + content, panelBottom = h - 64;
        panelBox(dc, contentLeft, 124, right, 196);
        const int folderWidth = std::max(80, content - 20 - (140 + 12 + 160 + 12 + 130 + 12 + 130) - 20 - 16);
        heading(dc, L"Mods folder", contentLeft + 20, 136, folderWidth);
        text(dc, mods::default_mods_directory().make_preferred().wstring(), contentLeft + 20, 160, folderWidth, 18,
             fonts.caption, muted, DT_LEFT | DT_SINGLELINE | DT_PATH_ELLIPSIS | DT_NOPREFIX);
        if (waveStyle()) {
            waveSection(dc, 212, 592, content, L"Load order", L"Top to bottom. Later mods win.", modList.size());
            if (modList.empty())
                waveEmpty(dc, contentLeft, right, 268, 528, L"No mods yet", L"Create one below, open the import tool, or drop mod folders into the mods folder.", false);
            waveSection(dc, 608, 920, content, L"What it adds", modList.empty() ? std::wstring(L"Select a mod to see its contents.") : std::wstring(L"Choose a character or stage to remove it."));
            return;
        }
        panelBox(dc, contentLeft, 208, right, panelBottom);
        const int half = (content - 52) / 2, contentsLeft = contentLeft + 32 + half;
        heading(dc, L"Load order", contentLeft + 20, 220, 120);
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

    }
    std::wstring timingRulesBase;
    void refreshTimingRules() {
        if (!controls.count(TimingRules))
            return;
        const auto category = speedrun::Category(settings.speedrun);
        const auto intro = wide(speedrun::category_rules(category));
        const auto details = speedrun::tournament(category)
            ? std::wstring(L"Tournament timing is automatic. F9 starts or restarts manually; F10 records a manual round split; F11 finishes manually. Permadeath is not used. Times show milliseconds.\r\n\r\n"
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
    std::string splitKey() const {
        return speedrun::splits_name(speedrun::Category(settings.speedrun),
            settings.permadeath && !speedrun::tournament(speedrun::Category(settings.speedrun)),
            settings.cpuDifficulty, settings.randomizer);
    }
    const Settings::SplitStyle* splitStyle(size_t index) const {
        const auto key = splitKey();
        const auto found = std::find_if(settings.splitStyles.begin(), settings.splitStyles.end(), [&](const auto& style) {
            return style.run == key && style.index == index;
        });
        return found == settings.splitStyles.end() ? nullptr : &*found;
    }
    std::wstring legacySplitName(size_t index) const {
        size_t row = 0, start = 0;
        while (start <= settings.overlaySplitNames.size()) {
            const auto end = settings.overlaySplitNames.find(',', start);
            if (row++ == index)
                return trimmed(wide(settings.overlaySplitNames.substr(start, end == std::string::npos ? end : end-start)));
            if (end == std::string::npos) break;
            start = end + 1;
        }
        return {};
    }
    std::wstring splitName(size_t index) const {
        if (const auto style = splitStyle(index)) {
            if (!style->name.empty()) return wide(style->name);
        } else if (const auto legacy = legacySplitName(index); !legacy.empty())
            return legacy;
        if (speedrun::tournament(speedrun::Category(settings.speedrun)))
            return index + 1 == splitBook.best().size() ? std::wstring(L"Final") : L"Round " + std::to_wstring(index + 1);
        return index + 1 == splitBook.best().size() ? std::wstring(L"Kid Buu") : L"Battle " + std::to_wstring(index + 1);
    }
    std::filesystem::path splitImageDirectory() const { return settings_path().parent_path()/"overlay"/"images"; }
    const speedrun::SplitImage* splitImage(const std::string& file) {
        if (file.empty()) return nullptr;
        auto [found, inserted] = splitImages.try_emplace(file);
        if (inserted) {
            try { found->second = speedrun::load_split_image(splitImageDirectory()/file); }
            catch (const std::exception& error) { std::cout << "[launcher:split-image] " << error.what() << std::endl; }
        }
        if (found->second.frames.size() > 1 && allowMotion() && !imageAnimationTicking) {
            imageAnimationTicking = true;SetTimer(hwnd, 3, motionInterval, nullptr);
        }
        return found->second.frames.empty() ? nullptr : &found->second;
    }
    void paintSplitImage(HDC dc, const std::string& file, RECT bounds, COLORREF backdrop) {
        if(const auto image=splitImage(file))paintLocalImage(dc,*image,bounds,backdrop);
    }
    void paintLocalImage(HDC dc,const speedrun::SplitImage& value,RECT bounds,COLORREF backdrop,bool cover=false) {
        const auto image=&value;
        const auto& rgba = image->frames[image->frame_at(allowMotion() ? motionTicks() : 0)].rgba;
        std::vector<uint32_t> pixels(size_t(image->width)*image->height);
        for (size_t i = 0; i < pixels.size(); ++i) {
            const unsigned alpha = rgba[i*4+3];
            const unsigned r = (rgba[i*4]*alpha+GetRValue(backdrop)*(255-alpha)+127)/255;
            const unsigned g = (rgba[i*4+1]*alpha+GetGValue(backdrop)*(255-alpha)+127)/255;
            const unsigned b = (rgba[i*4+2]*alpha+GetBValue(backdrop)*(255-alpha)+127)/255;
            pixels[i] = r<<16 | g<<8 | b;
        }
        const double sx=double(bounds.right-bounds.left)/image->width, sy=double(bounds.bottom-bounds.top)/image->height;
        const double scale=cover ? std::max(sx,sy) : std::min(sx,sy);
        const int width = std::max(1,int(std::lround(image->width*scale))), height = std::max(1,int(std::lround(image->height*scale)));
        const int left = (bounds.left+bounds.right-width)/2, top = (bounds.top+bounds.bottom-height)/2;
        BITMAPINFO info{};info.bmiHeader.biSize=sizeof(info.bmiHeader);info.bmiHeader.biWidth=LONG(image->width);
        info.bmiHeader.biHeight=-LONG(image->height);info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
        SavedDc saved(dc);SetStretchBltMode(dc,HALFTONE);SetBrushOrgEx(dc,0,0,nullptr);
        StretchDIBits(dc,left,top,width,height,0,0,int(image->width),int(image->height),pixels.data(),&info,DIB_RGB_COLORS,SRCCOPY);
    }
    unsigned styleRowCount() const {
        unsigned count = std::max(12u, unsigned(std::min(size_t(256),splitBook.best().size())));
        const auto key = splitKey();
        for (const auto& style : settings.splitStyles)
            if (style.run == key) count = std::max(count,style.index+1);
        return count;
    }
    void selectStyleRow() {
        const auto row = SendMessageW(controls[SplitStyleList], LB_GETCURSEL, 0, 0);
        styleSelection = row < 0 ? 0 : int(row);
        const auto style = splitStyle(size_t(styleSelection));
        SetWindowTextW(controls[SplitNameField], (style ? wide(style->name) : legacySplitName(size_t(styleSelection))).c_str());
        if (style && !style->image.empty()) splitImage(style->image);
        refreshEnabled();(pageDirty=true),InvalidateRect(hwnd,nullptr,FALSE);
    }
    void refreshStyleRows(int selected = -1) {
        if (!controls.count(SplitStyleList)) return;
        const auto list = controls[SplitStyleList];
        const int keep = std::clamp(selected < 0 ? styleSelection : selected,0,int(styleRowCount())-1);
        SendMessageW(list,WM_SETREDRAW,FALSE,0);SendMessageW(list,LB_RESETCONTENT,0,0);
        for (unsigned i = 0; i < styleRowCount(); ++i) {
            const auto name = splitName(i);
            SendMessageW(list,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(name.c_str()));
        }
        SendMessageW(list,LB_SETCURSEL,keep,0);
        SendMessageW(list,WM_SETREDRAW,TRUE,0);InvalidateRect(list,nullptr,FALSE);
        selectStyleRow();
    }
    Settings::SplitStyle editedStyle() const {
        auto style = splitStyle(size_t(styleSelection));
        auto next = style ? *style : Settings::SplitStyle{splitKey(),unsigned(styleSelection),{},{},{}};
        next.name = utf8(trimmed(textOf(SplitNameField)));
        return next;
    }
    void storeSplitStyle(Settings::SplitStyle style, const std::wstring& message) {
        Settings next = settings;
        next.splitStyles.erase(std::remove_if(next.splitStyles.begin(),next.splitStyles.end(),[&](const auto& old) {
            return old.run == style.run && old.index == style.index;
        }),next.splitStyles.end());
        next.splitStyles.push_back(std::move(style));
        write_settings(settings_path(),next);settings=std::move(next);
        refreshSplits();refreshStyleRows();status=message;error=false;(pageDirty=true),InvalidateRect(hwnd,nullptr,FALSE);
    }
    void saveSplitName() {
        if (settings.speedrun) storeSplitStyle(editedStyle(),L"Split name saved. Recorded times are unchanged.");
    }
    void attachSplitImage(const std::filesystem::path& source) {
        if (!settings.speedrun || source.empty()) return;
        auto imported = speedrun::import_split_image(source,splitImageDirectory());
        auto style = editedStyle();style.image=imported.file;style.imageName=imported.name;
        splitImages[imported.file]=std::move(imported.image);
        storeSplitStyle(std::move(style),L"Split image added. The original file was kept.");
    }
    void animateImages() {
        bool animated = false;
        if (page == overlayPage || page == speedrunPage)
            for (const auto& style : settings.splitStyles) {
                const auto image=splitImages.find(style.image);
                if (style.run==splitKey() && image!=splitImages.end() && image->second.frames.size()>1) {animated=true;break;}
            }
        if(page==0 || page==onlinePage || page==profilePage || page==playerPage) {
            const auto profile=activeProfile();const auto style=profile ? profileStyle(profile->id) : nullptr;
            const auto image=style ? profileImages.find(style->image) : profileImages.end();
            animated=animated || (image!=profileImages.end() && image->second.frames.size()>1);
        }
        const bool effects=effectPainted,pageEffects=pageEffectPainted;effectPainted=pageEffectPainted=false;
        if (!effects && (!animated || !allowMotion())) {KillTimer(hwnd,3);imageAnimationTicking=false;return;}
        if (dropdownOpen() || IsIconic(hwnd) || (!appActive && !effects)) {effectPainted=effects;pageEffectPainted=pageEffects;return;}
        const bool full=(animated && allowMotion() && motions.empty()) || (pageEffects && motions.empty());
        if (const UINT wanted=full ? 33u : 16u; wanted!=motionInterval){motionInterval=wanted;SetTimer(hwnd,3,wanted,nullptr);}
        if (full) (pageDirty=true),InvalidateRect(hwnd,nullptr,FALSE);
        else if (pageEffects || (animated && !motions.empty())) {
            RECT client{};GetClientRect(hwnd,&client);
            const int visibleHeight=std::max(1,unscale(client.bottom)-shellHeader-dockReserve());
            const LONG top=p(shellHeader),bottom=p(shellHeader+visibleHeight),shift=top-p(pageScroll);
            motionTick=true;
            for (const auto& motion:motions) {
                RECT area{motion.rect.left,std::max(top,motion.rect.top+shift),motion.rect.right,std::min(bottom,motion.rect.bottom+shift)};
                if (area.bottom>area.top) InvalidateRect(hwnd,&area,FALSE);
            }
        }
        if (effects)
            for (const int list : {PlayerList, FriendList}) {
                if (!controls.count(list) || !IsWindowVisible(controls[list])) continue;
                const auto& rows = list == PlayerList ? playerRows : friendRows;
                const auto top = SendMessageW(controls[list], LB_GETTOPINDEX, 0, 0);
                for (size_t row = size_t(std::max<LRESULT>(0, top)); row < rows.size() && row < size_t(std::max<LRESULT>(0, top)) + 24; ++row) {
                    RECT item{};
                    if (SendMessageW(controls[list], LB_GETITEMRECT, row, reinterpret_cast<LPARAM>(&item)) != LB_ERR &&
                        (effectOf(rows[row].first) || movingTag(rows[row].first) || [&] { const auto remote = remoteCard(rows[row].first); return remote && remote->card.accent == rgb_accent; }()))
                        InvalidateRect(controls[list], &item, FALSE);
                }
            }
        const int id = page == overlayPage ? SplitStyleList : SplitList;
        if (controls.count(id) && IsWindowVisible(controls[id])) InvalidateRect(controls[id],nullptr,FALSE);
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
    static int overlayStep(int) { return 52; }
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
        write_settings(settings_path(), next);
        settings = std::move(next);
        status = message;
        error = false;
        (pageDirty=true),InvalidateRect(hwnd, nullptr, FALSE);
    }
    void refreshTabs() {
        const auto list = controls[TabList];
        const auto chosen = SendMessageW(list, LB_GETCURSEL, 0, 0);
        const unsigned selectedTab = chosen >= 0 && chosen < LRESULT(dock_tab_count) ? settings.tabOrder[size_t(chosen)] : tab_count;
        SendMessageW(list, LB_RESETCONTENT, 0, 0);
        for (unsigned tab : settings.tabOrder) {
            if (!dock_tab(tab)) continue;
            const bool hidden = (settings.hiddenTabs >> tab) & 1u, fixed = (fixed_tabs >> tab) & 1u;
            const int slot = tabSlot(int(tab));
            const std::wstring row = std::wstring(hidden ? L"Off" : L"On") + L" \u00b7 " + tab_names[tab] + L" \u00b7 " +
                                     (hidden ? std::wstring(L"Hidden from the dock") : L"Shown, Ctrl+" + std::to_wstring(slot + 1)) +
                                     (fixed ? L". Always shown." : L"");
            SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(row.c_str()));
        }
        const auto selected = std::find(settings.tabOrder.begin(), settings.tabOrder.end(), selectedTab);
        SendMessageW(list, LB_SETCURSEL, selected == settings.tabOrder.end() ? WPARAM(-1) : WPARAM(selected - settings.tabOrder.begin()), 0);
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
        wavePalette = waveStyle();
        waveTint = waveSkyTop();
        apply_theme(settings.launcherTheme, settings.launcherAccent, settings.launcherColors);
        ambient.clear();
        chrome();
        layout();
        (pageDirty=true),RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME);
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
        heading(dc, L"OpenKai2 v" + wide(application_version), contentLeft + 20, 136, content - 200);
        text(dc, release ? L"Version " + wide(release->version) + L" is available." : std::wstring(L"You have this version installed."),
             contentLeft + 20, 162, content - 200, 18, fonts.caption, release ? gold : muted, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
        text(dc, message, contentLeft + 20, 184, content - 200, 18, fonts.caption, updateError ? red : muted,
             DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        const Grid grid(content);
        if (waveStyle()) {
            waveSection(dc, 224, 224 + 64 + 3 * waveOptionRow + 12, content, L"Preferences",
                settings.updateChannel ? L"Stable and prerelease: every published build, including test versions." : L"Stable: only versions marked stable.");
            optionRows(dc, 224 + 64, content, {L"Update channel", L"Automatic updates", L"Manual check"});
            waveSection(dc, 496, 640, content, L"How updates install", std::wstring());
            text(dc, L"With automatic updates on, the launcher looks for a new version between game sessions. A found update downloads, "
                     L"is checked against its published SHA-256, and installs after the game closes; your settings, saves and mods are kept.",
                 contentLeft + 24, 538, content - 48, 46, fonts.caption, muted, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
            text(dc, L"Reporting a problem? Attach startup.log or launcher.log from the logs folder.", contentLeft + 210, 598,
                 content - 234, 18, fonts.caption, muted, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            return;
        }
        panelBox(dc, contentLeft, 224, right, 372);
        heading(dc, L"Preferences", contentLeft + 20, 236, content - 40);
        caps(dc, L"UPDATE CHANNEL", grid.x(0), Grid::label(236, 0), grid.column);
        caps(dc, L"AUTOMATIC UPDATES", grid.x(1), Grid::label(236, 0), grid.column);
        caps(dc, L"MANUAL CHECK", grid.x(2), Grid::label(236, 0), grid.column);
        text(dc, settings.updateChannel ? L"Stable and prerelease: every published build, including test versions."
                                        : L"Stable: only versions marked stable.",
             grid.x(0), 344, content - 40, 18, fonts.caption, muted, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        panelBox(dc, contentLeft, 384, right, std::min(h - 64, 518));
        heading(dc, L"How updates install", contentLeft + 20, 396, content - 40);
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
            if (waveStyle() && !wave_color_role(role)) continue;
            const COLORREF color = shown_role_color(role, colorOf(look.*(color_roles[role].member)));
            wchar_t hex[8]{};
            swprintf(hex, 8, L"#%02X%02X%02X", GetRValue(color), GetGValue(color), GetBValue(color));
            const std::wstring row = std::wstring(hex) + L" \u00b7 " + color_roles[role].name +
                                     (settings.launcherColors[role] >= 0 ? L" (changed)" : L"") + L" \u00b7 " + color_roles[role].about;
            const auto added = SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(row.c_str()));
            if (added >= 0) SendMessageW(list, LB_SETITEMDATA, WPARAM(added), LPARAM(role));
        }
        SendMessageW(list, LB_SETTOPINDEX, WPARAM(std::max<LRESULT>(top, 0)), 0);
        SendMessageW(list, LB_SETCURSEL, chosen, 0);
    }
    void paintColors(HDC dc, int h, int content) {
        const int right = contentLeft + content, panelBottom = h - 64;
        if (waveStyle()) {
            const unsigned changed = unsigned(std::count_if(settings.launcherColors.begin(), settings.launcherColors.end(), [](int32_t value) { return value >= 0; }));
            waveSection(dc, 124, 196, content, std::wstring(L"Theme ") + themes[std::min(settings.launcherTheme, theme_count - 1u)].name,
                L"Double-click a part to recolor it. " + std::to_wstring(changed) + (changed == 1 ? L" color changed." : L" colors changed."));
            waveSection(dc, 212, 636, content, L"Colors", L"Select a part, then choose an action below.");
            return;
        }
        panelBox(dc, contentLeft, 124, right, 196);
        heading(dc, std::wstring(L"Theme ") + themes[std::min(settings.launcherTheme, theme_count - 1u)].name,
                contentLeft + 20, 136, content - 200);
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
        if (waveStyle()) {
            waveSection(dc, 124, 196, content, L"Tab order", L"Reorder or hide tabs. The home menu and Ctrl+1 to Ctrl+6 follow this order.");
            waveSection(dc, 212, 268 + int(dock_tab_count) * 40 + 70, content, L"Tabs", std::wstring());
            return;
        }
        panelBox(dc, contentLeft, 124, right, 196);
        heading(dc, L"Navigation dock", contentLeft + 20, 136, content - 200);
        text(dc, L"Reorder or hide tabs. Alt+1 to Alt+6 follow the dock. Play and Settings stay visible. "
                 L"Controls and Saves & backups are on Play.",
             contentLeft + 20, 160, content - 200, 32, fonts.caption, muted, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
        panelBox(dc, contentLeft, 208, right, panelBottom);
        caps(dc, L"TABS", contentLeft + 20, 212, 200);
    }
    void paintOverlay(HDC dc, int h, int content) {
        const bool wave = waveStyle();
        const int right = contentLeft + content, half = (content - 52) / 2;
        const int panelBottom = wave ? 1512 + 230 + 42 : h - 64;
        const int side = wave ? contentLeft + 24 : contentLeft + 32 + half, column = (content - 52 - half - 12) / 2;
        const int previewTop = wave ? 1512 : 584;
        if (wave) {
            waveSection(dc, 124, 196, content, L"Split appearance", L"Use your own names and images. PS2 time and verification labels stay fixed.");
            const auto category = speedrun::Category(settings.speedrun);
            waveSection(dc, 212, 676, content, L"Splits", settings.speedrun ? wide(speedrun::category_label(category, settings.cpuDifficulty, settings.randomizer)) : std::wstring(L"Select a category to edit its splits."));
            optionRows(dc, 276, content, {L"Run category"});
            optionRows(dc, 556, content, {L"Split name", L"Your image"});
            const auto style = splitStyle(size_t(styleSelection));
            const auto image = style ? splitImage(style->image) : nullptr;
            const int labelWidth = std::clamp(content * 2 / 5, 180, 420);
            text(dc, image ? std::to_wstring(image->width) + L" \u00d7 " + std::to_wstring(image->height) : style && !style->imageName.empty() ? wide(style->imageName) : std::wstring(L"PNG, JPEG or GIF. Files are copied."),
                 contentLeft + 24 + labelWidth + 304, 626, right - 24 - (contentLeft + 24 + labelWidth + 304), 20, fonts.caption, muted, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            waveSection(dc, 692, 756 + 12 * waveOptionRow + 12, content, L"Overlay", L"Where and how the timer appears in game.");
            optionRows(dc, 756, content, {L"Position", L"Size", L"Background", L"Split list", L"Text color", L"Ahead color", L"Behind color",
                                          L"Best segment color", L"Times shown to", L"Distance from edge", L"Corners", L"Title"});
            waveSection(dc, 1456, panelBottom, content, L"Preview", L"Example values");
        } else {
        panelBox(dc, contentLeft, 124, right, 196);
        heading(dc, L"Split appearance", contentLeft + 20, 136, content - 380);
        text(dc, L"Use your own names and images. PS2 time and verification labels stay fixed.",
             contentLeft + 20, 164, content - 380, 28, fonts.caption, muted, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
        panelBox(dc, contentLeft, 208, right, panelBottom);
        const auto category = speedrun::Category(settings.speedrun);
        caps(dc,L"RUN CATEGORY",contentLeft+20,214,half);
        caps(dc, settings.speedrun ? L"SPLITS · " + wide(speedrun::category_label(category, settings.cpuDifficulty, settings.randomizer))
                                  : L"SELECT A CATEGORY ABOVE TO EDIT SPLITS", contentLeft + 20, 290, half, muted, DT_END_ELLIPSIS);
        caps(dc, L"SPLIT NAME", contentLeft + 20, 534, half);
        caps(dc, L"YOUR IMAGE", contentLeft + 20, 608, half);
        const RECT tile{p(contentLeft + 20),p(688),p(contentLeft + 120),p(788)};
        box(dc,tile,field,border,5);
        const auto style = splitStyle(size_t(styleSelection));
        const auto image = style ? splitImage(style->image) : nullptr;
        if (image) {
            auto inside = tile;InflateRect(&inside,-p(6),-p(6));
            paintSplitImage(dc,style->image,inside,field);
        } else
            text(dc,L"No image",contentLeft+24,727,92,24,fonts.caption,muted,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
        text(dc,style && !style->imageName.empty() ? wide(style->imageName) : L"Choose your own PNG, JPEG or GIF.",
             contentLeft+136,698,half-116,44,fonts.body,ink,DT_LEFT|DT_WORDBREAK|DT_END_ELLIPSIS|DT_NOPREFIX);
        const auto detail = image ? std::to_wstring(image->width)+L" × "+std::to_wstring(image->height)+
            (image->frames.size()>1 ? L" · "+std::to_wstring(image->frames.size())+L" animated frames" : L"")
            : style && !style->image.empty() ? std::wstring(L"Image unavailable. Choose a replacement.")
                                          : std::wstring(L"Images are copied. Original files are kept.");
        text(dc,detail,contentLeft+136,750,half-116,40,fonts.caption,muted,DT_LEFT|DT_WORDBREAK|DT_NOPREFIX);

        const int step = overlayStep(h);
        const auto label = [&](int row, int col, const wchar_t* value) {
            caps(dc,value,side+col*(column+12),228+row*step,column,muted,DT_END_ELLIPSIS);
        };
        label(0,0,L"POSITION");label(0,1,L"SIZE");label(1,0,L"BACKGROUND");label(1,1,L"SPLIT LIST");
        label(2,0,L"TEXT COLOR");label(2,1,L"AHEAD COLOR");label(3,0,L"BEHIND COLOR");label(3,1,L"BEST SEGMENT COLOR");
        label(4,0,L"TIMES SHOWN TO");label(4,1,L"DISTANCE FROM EDGE");label(5,0,L"CORNERS");label(5,1,L"TITLE");
        caps(dc,L"PREVIEW · EXAMPLE VALUES",side,previewTop-20,right-side-20,muted,DT_END_ELLIPSIS);
        }
        const int previewWidth = wave ? right-24-side : right-20-side, previewHeight = panelBottom-42-previewTop;
        const COLORREF canvas = RGB(36,39,46);
        box(dc,{p(side),p(previewTop),p(side+previewWidth),p(previewTop+previewHeight)},canvas,canvas,4);
        const int scales[] = {85,100,125,150};
        const int factor = scales[std::min(settings.overlaySize,3u)], rowHeight = std::clamp(18*factor/100,15,24);
        const int inset = std::min(20,std::max(4,4+int(settings.overlayMargin)*6));
        const int rows = std::min(3,std::max(1,(previewHeight-88-(settings.overlayTitle.empty()?0:20))/rowHeight));
        const int boxHeight = 78+rows*rowHeight+(settings.overlayTitle.empty()?0:20);
        const int boxWidth = std::min(previewWidth-inset*2,std::max(200,280*factor/100));
        const int corner = int(settings.overlayCorner);
        const int left = corner%2 ? side+previewWidth-inset-boxWidth : side+inset;
        const int top = corner<2 ? previewTop+inset : previewTop+previewHeight-inset-boxHeight;
        const int shades[] = {0,105,180,55,235};
        const auto bg = mix(canvas,RGB(10,12,18),shades[std::min(settings.overlayBackground,4u)]);
        if (settings.overlayBackground) box(dc,{p(left),p(top),p(left+boxWidth),p(top+boxHeight)},bg,bg,settings.overlayShape?0:5);
        const auto color = [&](unsigned role) {const auto& c=speedrun::overlay_colors[settings.overlayColors[role]];return RGB(c.r,c.g,c.b);};
        const auto font = factor>=125 ? fonts.body : fonts.caption;
        int y = top+6;
        if (!settings.overlayTitle.empty()) {
            text(dc,wide(settings.overlayTitle),left+8,y,boxWidth-16,18,font,color(0),DT_LEFT|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);y+=20;
        }
        text(dc,L"Verified",left+8,y,boxWidth-16,18,fonts.caption,color(1),DT_RIGHT|DT_SINGLELINE|DT_NOPREFIX);y+=20;
        const int first=std::clamp(styleSelection-rows/2,0,std::max(0,int(styleRowCount())-rows));
        for (int i=0;i<rows;++i) {
            const size_t index=size_t(first+i);
            const auto entry=splitStyle(index);
            int nameLeft=left+8;
            if (entry && !entry->image.empty()) {
                paintSplitImage(dc,entry->image,{p(nameLeft),p(y+1),p(nameLeft+16),p(y+17)},bg);
                nameLeft+=22;
            }
            text(dc,splitName(index),nameLeft,y,boxWidth-(nameLeft-left)-76,rowHeight,font,color(0),DT_LEFT|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
            text(dc,L"0:00.00",left+boxWidth-72,y,64,rowHeight,font,color(0),DT_RIGHT|DT_SINGLELINE|DT_NOPREFIX);y+=rowHeight;
        }
        text(dc,L"0:00.00",left+8,y,boxWidth-16,24,fonts.brand,color(0),DT_RIGHT|DT_SINGLELINE|DT_NOPREFIX);y+=26;
        text(dc,L"PS2 time",left+8,y,boxWidth-84,18,fonts.caption,color(0),DT_LEFT|DT_SINGLELINE|DT_NOPREFIX);
        text(dc,L"0:00.00",left+boxWidth-72,y,64,18,fonts.caption,color(0),DT_RIGHT|DT_SINGLELINE|DT_NOPREFIX);
        text(dc,L"Names and images apply to the next game session.",side,panelBottom-30,previewWidth,18,fonts.caption,muted,
             DT_LEFT|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
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
    std::wstring stageHow(uint32_t record) const {
        static const uint32_t bases[] = {0, 1, 4, 5};
        const uint32_t base = record >= mods::stage_first_variant && record < mods::stage_count
                                  ? bases[record - mods::stage_first_variant]
                                  : record;
        return L"To play it, pick " + stageName(base) +
               L" on the stage select screen and confirm with Circle (C on the keyboard) instead of Cross.";
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
               L", a copy of another stage, so no stage is lost. " + stageHow(record) +
               L" Stage imports are experimental: its particle effects "
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
    void optionRows(HDC dc,int top,int content,const std::vector<std::wstring>& labels) {
        const int labelWidth=std::clamp(content*2/5,180,420);
        for(size_t i=0;i<labels.size();++i) {
            const int rowTop=top+int(i)*waveOptionRow;
            if(i>0) {
                Gdiplus::Graphics graphics(dc);
                Gdiplus::LinearGradientBrush divider(Gdiplus::PointF(Gdiplus::REAL(p(contentLeft+24)),0),Gdiplus::PointF(Gdiplus::REAL(p(contentLeft+content-24)),0),Gdiplus::Color(70,255,236,210),Gdiplus::Color(8,255,236,210));
                graphics.FillRectangle(&divider,Gdiplus::REAL(p(contentLeft+24)),Gdiplus::REAL(p(rowTop)),Gdiplus::REAL(p(content-48)),Gdiplus::REAL(std::max(1,p(1))));
            }
            text(dc,labels[i],contentLeft+24,rowTop+14,labelWidth-16,28,fonts.body,ink,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
        }
    }
    void waveSection(HDC dc,int top,int bottom,int content,const std::wstring& title,const std::wstring& note,size_t count=0) {
        const int right=contentLeft+content;
        panelBox(dc,contentLeft,top,right,bottom);
        text(dc,title,contentLeft+24,top+12,300,28,fonts.brand,ink,DT_LEFT|DT_SINGLELINE|DT_NOPREFIX);
        int noteLeft=contentLeft+24+measure(dc,title,fonts.brand)+14;
        if(count) {
            const auto number=std::to_wstring(count);
            const int width=std::max(24,measure(dc,number,fonts.caption)+14);
            box(dc,{p(noteLeft),p(top+17),p(noteLeft+width),p(top+37)},mix(panel,orange,60),mix(panel,orange,90),7);
            text(dc,number,noteLeft,top+17,width,20,fonts.caption,ink,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
            noteLeft+=width+12;
        }
        if(!note.empty())text(dc,note,noteLeft,top+18,std::max(40,right-300-noteLeft),20,fonts.caption,muted,DT_LEFT|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
    }
    void waveEmpty(HDC dc,int left,int right,int top,int bottom,const std::wstring& headline,const std::wstring& hint,bool icon) {
        const int mid=(top+bottom)/2-12,centre=(left+right)/2;
        const RECT ring{p(centre-18),p(mid-44),p(centre+18),p(mid-8)};
        navigationIcon(dc,icon ? OnlineTab : HomeProfile,ring,muted,p(26));
        text(dc,headline,left+20,mid,right-left-40,20,fonts.strong,muted,DT_CENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
        text(dc,hint,left+20,mid+22,right-left-40,18,fonts.caption,faint,DT_CENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
    }
    void paintOnlineWave(HDC dc,int content,bool online) {
        const int right=contentLeft+content;
        waveSection(dc,waveOnlinePlayersTop,waveOnlinePlayersTop+416,content,settings.onlinePlayerFilter ? L"Friends" : L"Players",
            online ? L"Select a player, then invite, add or view their profile." : L"Players appear here when you are online.",playerRows.size());
        if(playerRows.empty())
            waveEmpty(dc,contentLeft,right,waveOnlinePlayersTop+56,waveOnlinePlayersTop+356,!online ? L"You are offline" : settings.onlinePlayerFilter ? L"No friends online" : L"No players yet",
                !online ? L"Use Reconnect below to try the service again." : L"Try All players, or invite a friend by profile ID.",false);
        const size_t openRooms=size_t(std::count_if(roomRows.begin(),roomRows.end(),[](const online::Room& candidate){return candidate.version==application_version;}));
        waveSection(dc,waveOnlineRoomsTop,waveOnlineRoomsTop+392,content,L"Rooms",
            openRooms<roomRows.size() ? std::to_wstring(roomRows.size()-openRooms)+L" need another version" : std::wstring(L"Join a room, or type a host's address."),roomRows.size());
        if(roomRows.empty()) {
            const bool filtered=!trimmed(textOf(OnlineSearch)).empty() || settings.onlineRoomFilter!=1;
            waveEmpty(dc,contentLeft,right,waveOnlineRoomsTop+100,waveOnlineRoomsTop+320,online ? L"No matching rooms" : L"You are offline",
                online ? (filtered ? L"Change the filter, or host a room below." : L"Host one below, or invite a player.") : L"Use Reconnect to try the service again.",true);
        }
        const bool hosting=room && roomHost;
        waveSection(dc,waveOnlineHostTop,waveOnlineHostTop+64+4*waveOptionRow+12,content,L"Host a room",
            hosting ? L"Your room is open. Invite a player, or wait for someone to join." : online ? L"You are player 1. Your router's port is opened for you when it allows it." : L"Go online with a profile to host a room.");
        optionRows(dc,waveOnlineHostTop+64,content,{L"Room name",L"Password",L"Input delay",L"Who can see it"});
    }
    void paintOnline(HDC dc, int h, int content) {
        const int right = contentLeft + content;
        const Grid grid(content);
        auto section = [&](int left, int top, int edge, int bottom, const wchar_t *title, const std::wstring &note,
                           int noteRight = 0, size_t count = 0) {
            panelBox(dc, left, top, edge, bottom);
            heading(dc, title, left + 20, top + 12, 200);
            int noteLeft = left + 20 + measure(dc, title, fonts.strong) + 12;
            if (count) {
                const auto number = std::to_wstring(count);
                const int width = std::max(22, measure(dc, number, fonts.caption) + 12);
                box(dc, {p(noteLeft), p(top + 14), p(noteLeft + width), p(top + 32)}, mix(panel, orange, 40), mix(panel, orange, 40), 6);
                text(dc, number, noteLeft, top + 14, width, 18, fonts.caption, ink, DT_CENTER | DT_SINGLELINE | DT_NOPREFIX);
                noteLeft += width + 12;
            }
            noteLeft = std::max(noteLeft, left + 140);
            text(dc, note, noteLeft, top + 14, (noteRight ? noteRight : edge - 20) - noteLeft, 18, fonts.caption, muted,
                 DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        };
        auto label = [&](int top, int row, int column, const wchar_t *value) {
            caps(dc, value, grid.x(column), Grid::label(top, row), grid.column);
        };
        const bool taken = directory && directory->connected() && directory->name_taken();
        const bool online = directory && directory->connected() && !taken;
        const auto profile = activeProfile();
        const int friendsOnline = onlineFriendCount();
        const std::wstring service = taken ? std::wstring(L"Name taken by another player")
                                     : online ? (playerCount() <= 1 ? std::wstring(L"Online \u00b7 nobody else yet")
                                                                   : L"Online \u00b7 " + std::to_wstring(playerCount() - 1) +
                                                                         (playerCount() == 2 ? L" other player" : L" other players"))
                                     : connecting.valid() ? std::wstring(L"Connecting\u2026")
                                     : !profile ? std::wstring(L"Add a profile to go online")
                                     : L"Offline" + (onlineProblem.empty() ? std::wstring() : L": " + wide(onlineProblem));
        const COLORREF serviceTone = taken ? red : online ? green : connecting.valid() ? gold : faint;
        panelBox(dc, contentLeft, onlineProfileTop, right, onlineProfileTop + 124);
        const int cx = contentLeft + 20 + 30, cy = onlineProfileTop + 52;
        if (profile) {
            buffer.frame.halo({p(cx), p(cy), p(cx + 1), p(cy + 1)}, 0, p(40), avatarTone(profile->id), online ? 60 : 24);
            avatar(dc, p(cx), p(cy), p(30), wide(profile->name), profile->id, panel, online ? 1 : connecting.valid() ? 2 : 0);
        } else {
            ellipse(dc, p(cx), p(cy), p(30), mix(panel, border, 160));
            ellipse(dc, p(cx), p(cy), p(28), field);
            text(dc, L"?", cx - 30, cy - 15, 60, 30, fonts.brand, faint, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
        const int nameLeft = cx + 30 + 16, identity = grid.x(1) - 16 - nameLeft;
        caps(dc, L"YOUR PROFILE", nameLeft, onlineProfileTop + 20, identity);
        nameText(dc, profile ? wide(profile->name) : std::wstring(L"No profile yet"), nameLeft, onlineProfileTop + 36, identity, 26,
                 fonts.brand, profile ? nameInk(profile->id, ink) : muted, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX,
                 profile ? profile->id : std::string());
        ellipse(dc, p(nameLeft + 4), p(onlineProfileTop + 74), p(3), serviceTone);
        text(dc, service, nameLeft + 14, onlineProfileTop + 65, identity - 14, 18, fonts.caption, taken ? red : online ? ink : muted,
             DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        if (profile) {
            const std::wstring circle = std::to_wstring(friendCount()) + (friendCount() == 1 ? L" friend" : L" friends") +
                                        (friendsOnline ? L" \u00b7 " + std::to_wstring(friendsOnline) + L" online" : std::wstring());
            text(dc, circle, nameLeft, onlineProfileTop + 83, identity, 18, fonts.caption, muted,
                 DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        }
        fill(dc, {p(grid.x(1) - 9), p(onlineProfileTop + 20), p(grid.x(1) - 8), p(onlineProfileTop + 84)}, mix(panel, border, 160));
        if (waveStyle()) label(onlineProfileTop - 20, 0, 1, L"SHARE YOUR PROFILE ID");
        else {
            label(onlineProfileTop - 20, 0, 1, L"PLAYER CARD");
            label(onlineProfileTop - 20, 0, 2, L"SHARE");
        }
        if (waveStyle()) { paintOnlineWave(dc, content, online); return; }

        const bool hosting = room && roomHost;
        const std::wstring hostNote = hosting ? L"Your room is open. Invite a player, or wait for someone to join."
                                      : online ? L"You are player 1. Your router's port is opened for you when it allows it."
                                               : L"Go online with a profile to host a room.";
        section(contentLeft, onlineHostTop, right, onlineHostBottom, L"Host a room", hostNote);
        {
            const HostRow host(content);
            const wchar_t *labels[] = {L"ROOM NAME", L"PASSWORD", L"INPUT DELAY", L"WHO CAN SEE IT"};
            for (int i = 0; i < 4; ++i)
                caps(dc, labels[i], host.x(i), HostRow::label, host.column);
            ellipse(dc, p(host.x(4) + 4), p(HostRow::label + 7), p(3), hosting ? green : online ? orange : faint);
            caps(dc, hosting ? L"HOSTING NOW" : online ? L"READY TO HOST" : L"OFFLINE", host.x(4) + 14, HostRow::label,
                 host.column - 14, hosting ? green : muted);
        }
        const int split = contentLeft + (content * 62) / 100, bottom = h - 64 + 6;
        const size_t openRooms = size_t(std::count_if(roomRows.begin(), roomRows.end(), [](const online::Room &room) {
            return room.version == application_version;
        }));
        const std::wstring roomNote = openRooms < roomRows.size()
                                          ? std::to_wstring(roomRows.size() - openRooms) + L" need another version"
                                          : std::wstring();
        section(contentLeft, onlineListsTop, split - 6, bottom, L"Rooms", roomNote, split - 6 - 20 - 100 - 12, roomRows.size());
        section(split + 6, onlineListsTop, right, bottom, L"Players",
                std::wstring(), 0, playerRows.size());
        const int listTop = onlineListsTop + 90;
        auto empty = [&](int left, int edge, int listBottom, const std::wstring &headline, const std::wstring &hint, bool icon) {
            const int mid = (listTop + listBottom) / 2 - 12, centre = (left + edge) / 2;
            const RECT ring{p(centre - 18), p(mid - 44), p(centre + 18), p(mid - 8)};
            avatarGlyph(dc, ring, icon ? 0 : 1, faint);
            text(dc, headline, left + 20, mid, edge - left - 40, 20, fonts.strong, muted, DT_CENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            text(dc, hint, left + 20, mid + 22, edge - left - 40, 18, fonts.caption, faint, DT_CENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        };
        const bool filtered=!trimmed(textOf(OnlineSearch)).empty() || settings.onlineRoomFilter!=1;
        if (roomRows.empty())
            empty(contentLeft, split - 6, bottom - 110, online ? L"No matching rooms" : L"You are offline",
                  online ? (filtered ? L"Change the filter, or host a room above." : L"Host one above, or invite a player.") : L"Use Reconnect to try the service again.", true);
        if (playerRows.empty())
            empty(split + 6, right, bottom - 60,
                  !online ? L"You are offline" : settings.onlinePlayerFilter ? L"No friends online" : L"No matching players",
                  !online ? L"Players appear here when connected." : L"Try All players or clear your search.", false);
        const int joinTop = bottom - 6 - 103;
        fill(dc, {p(contentLeft + 20), p(joinTop), p(split - 6 - 20), p(joinTop) + 1}, mix(panel, border, 160));
    }
    static std::wstring sentenceCase(const wchar_t* label) {
        std::wstring value(label);
        for(size_t i=1;i<value.size();++i)value[i]=wchar_t(std::towlower(value[i]));
        for(const auto& [from,to]:std::initializer_list<std::pair<const wchar_t*,const wchar_t*>>{
                {L"Vsync",L"VSync"},{L"Fps",L"FPS"},{L" fps",L" FPS"},{L"Hd ",L"HD "},{L"Cpu ",L"CPU "},{L"Ps2 ",L"PS2 "},
                {L"discord",L"Discord"},{L"dragon ball",L"Dragon Ball"}}) {
            const std::wstring needle(from);
            for(size_t at=value.find(needle);at!=std::wstring::npos;at=value.find(needle,at+1))value.replace(at,needle.size(),to);
        }
        return value;
    }
    void paintSettings(HDC dc,int h,int content) {
        ui::SettingsLayout grid(content,contentLeft,waveStyle(),settingsTop());
        grid.filter(searchText(trimmed(textOf(SettingsSearch))));
        const wchar_t* titles[]={L"Display",L"Graphics",L"Game",L"Launcher"};
        const wchar_t* notes[]={L"Window, presentation and sound. Game simulation stays at 60 FPS.",
            L"Renderer, image clarity and the resolution used for 3D scenes.",
            L"Difficulty, textures and music. Speedrun categories enforce their own rules.",
            L"Appearance, navigation and Discord activity on this PC."};
        const std::array<std::vector<const wchar_t*>,4> labels{{
            {L"WINDOW MODE",L"RESOLUTION",L"ASPECT RATIO",L"FRAME RATE CAP",L"VSYNC",L"GAME AUDIO"},
            {L"RENDERER",L"ANTI-ALIASING",L"TEXTURE FILTERING",L"UPSCALING",L"INTERNAL RESOLUTION",L"IMAGE EFFECT",L"BRIGHTNESS",L"IMAGE PRESETS"},
            {L"CPU DIFFICULTY",L"HD TEXTURES",L"MUSIC",L"FPS COUNTER",L"RANDOMIZER",L"PS2 LOAD TIMES",L"RUN-AHEAD"},
            {L"THEME",L"ACCENT COLOR",L"START ON",L"DISCORD ACTIVITY",L"INTERFACE MOTION",L"HELP HINTS",L"INTERFACE STYLE",L"WAVE SKY",L"DRAGON BALL EMBLEM",L"CLOCK",L"WAVE BACKGROUND"}}};
        bool any=false;
        for(size_t group=0;group<labels.size();++group) {
            const int top=grid.sections[group];if(top<0)continue;any=true;
            panelBox(dc,contentLeft,top,contentLeft+content,grid.bottom(top,ui::SettingsLayout::fields[group]));
            text(dc,titles[group],contentLeft+24,top+14,200,26,fonts.brand,ink);
            text(dc,notes[group],contentLeft+24,top+46,content-48,20,fonts.caption,muted,
                 DT_LEFT|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
            for(size_t index=0;index<labels[group].size();++index) {
                const auto cell=grid.label(top,int(index));
                if(grid.list) {
                    const int rowTop=top+ui::SettingsLayout::header+int(index)*ui::SettingsLayout::listRow;
                    if(index>0) {
                        Gdiplus::Graphics graphics(dc);
                        Gdiplus::LinearGradientBrush divider(Gdiplus::PointF(Gdiplus::REAL(p(contentLeft+24)),0),Gdiplus::PointF(Gdiplus::REAL(p(contentLeft+content-24)),0),Gdiplus::Color(70,255,236,210),Gdiplus::Color(8,255,236,210));
                        graphics.FillRectangle(&divider,Gdiplus::REAL(p(contentLeft+24)),Gdiplus::REAL(p(rowTop)),Gdiplus::REAL(p(content-48)),Gdiplus::REAL(std::max(1,p(1))));
                    }
                    text(dc,sentenceCase(labels[group][index]),cell.x,rowTop+14,cell.width,28,fonts.body,ink,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
                } else caps(dc,labels[group][index],cell.x,cell.y,cell.width,muted);
            }
        }
        if(!any) {
            panelBox(dc,contentLeft,224,contentLeft+content,388);
            heading(dc,L"No matching settings",contentLeft+24,248,content-48);
            text(dc,L"Try a setting such as resolution, music or Discord, or choose a section above.",
                 contentLeft+24,292,content-48,48,fonts.body,muted,DT_WORDBREAK|DT_NOPREFIX);
        }
    }
    void paintSpeedrun(HDC dc, int h, int content) {
        const int right = contentLeft + content;
        const Grid grid(content);
        const bool wave = waveStyle();
        const bool compact = h < 700 && !wave;
        const int splitsTop = wave ? 444 : classicSplitsTop;
        const int rulesTop = wave ? splitsTop + (speedrunRulesTop - classicSplitsTop) : compact ? 496 : speedrunRulesTop;
        const int splitBottom = rulesTop - 12;
        if (wave) {
            waveSection(dc, speedrunTop, speedrunTop + 64 + 4 * waveOptionRow + 12, content, L"Run",
                settings.speedrun ? (speedrun::tournament(speedrun::Category(settings.speedrun)) ? L"Automatic timing. F9/F10/F11 override, F8 resets." : L"Mods are off during a run. F7 hides the overlay, F8 resets.") : L"Choose a category to time runs.");
            optionRows(dc, speedrunTop + 64, content, {L"Category", L"Permadeath", L"Randomizer", L"CPU difficulty"});
        } else {
        panelBox(dc, contentLeft, speedrunTop, right, speedrunTop + 160);
        heading(dc, L"Run", contentLeft + 20, speedrunTop + 12, 200);
        text(dc, settings.speedrun ? (speedrun::tournament(speedrun::Category(settings.speedrun)) ? L"Automatic timing. F9/F10/F11: manual overrides. F8 resets." : L"Mods are off while a category is chosen. F7 hides the overlay; F8 resets.")
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
        }
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
        heading(dc, L"How runs are timed", contentLeft + 20, rulesTop + 12, content - 40);
    }
    void paint() {
        if (painting) {
            ValidateRect(hwnd,nullptr);
            repaintPending=true;
            return;
        }
        painting=true;
        SetPropW(hwnd,L"OpenKai2.Painting",reinterpret_cast<HANDLE>(1));
        struct Painting {
            Window& window;
            ~Painting() {
                window.painting=false;
                RemovePropW(window.hwnd,L"OpenKai2.Painting");
                if (std::exchange(window.repaintPending,false)) (window.pageDirty=true),InvalidateRect(window.hwnd,nullptr,FALSE);
                if (!window.deferredMessages.empty()) PostMessageW(window.hwnd,deferredUiMessage,0,0);
            }
        } paintingScope{*this};
        PAINTSTRUCT ps{};
        auto dc = BeginPaint(hwnd, &ps);
        struct PaintEnd { HWND window; PAINTSTRUCT& state; ~PaintEnd(){EndPaint(window,&state);} } end{hwnd,ps};
        RECT rect{};
        GetClientRect(hwnd, &rect);
        if (rect.right <= 0 || rect.bottom <= 0) {
            return;
        }
        const RECT& update = ps.rcPaint;
        if (toastAt) motionTick = false;
        if (page != shownPage) {
            if (shownPage >= 0) {
                transitionDirection = navigatingBack ? 1 : -1;
                captureTransition(rect);
            }
            shownPage = page;
            navigatingBack = false;
        }
        if (!transitionAt && paintMotionsInPlace(update, rect)) {
            if (update.right > update.left && update.bottom > update.top)
                BitBlt(dc, update.left, update.top, update.right - update.left, update.bottom - update.top, buffer.dc, update.left, update.top, SRCCOPY);
            return;
        }
        const auto glowing = renderFrame(rect);
        if (!transitionAt && controlsDeferred && !revealPosted && hwnd) {revealPosted=true;PostMessageW(hwnd,revealMessage,0,0);}
        if (transitionAt) {
            blendTransition();
            if (transitionAt) {
                RECT whole{0, 0, rect.right, rect.bottom};
                BitBlt(dc, 0, 0, whole.right, whole.bottom, buffer.dc, 0, 0, SRCCOPY);
            }
        }
        paintToast(buffer.dc);
        if (update.right > update.left && update.bottom > update.top)
            BitBlt(dc, update.left, update.top, update.right - update.left, update.bottom - update.top, buffer.dc, update.left, update.top, SRCCOPY);
        if (paintedPage != page || paintedScroll != pageScroll || paintedSize != std::pair<int, int>{rect.right, rect.bottom} || paintedGlow != glowing) {
            paintedPage = page;
            paintedScroll = pageScroll;
            paintedSize = {rect.right, rect.bottom};
            paintedGlow = glowing;
            for (auto [id, control] : controls)
                InvalidateRect(control, nullptr, FALSE);
        }
    }
    std::vector<int> renderFrame(const RECT& rect) {
        if (rect.right <= 0 || rect.bottom <= 0)
            return {};
        if (laidOutPage != page)
            pageScroll = 0;
        if (page == wavePage) {
            pageScroll = 0;
            motions.clear();
            motionTick = false;
            recordingMotion = false;
            buffer.make(rect.right, rect.bottom);
            paintWave(buffer.dc, rect.right, rect.bottom);
            if (!waveTicking) startWave();
            GdiFlush();
            pageDirty = false;
            return {};
        }
        const int w=unscale(rect.right), h=unscale(rect.bottom);
        const int visibleHeight=std::max(1,h-shellHeader-dockReserve());
        const int pageHeight=std::max(visibleHeight,pageCanvasHeight());
        pageScroll = std::clamp(pageScroll, 0, pageHeight - visibleHeight);
        const int sourceTop=p(pageScroll), destinationTop=p(shellHeader);
        if (buffer.frame.width!=rect.right || buffer.frame.height!=std::max(int(rect.bottom),p(pageHeight))) ambient.clear();
        buffer.make(rect.right,std::max(int(rect.bottom),p(pageHeight)));
        auto memory = buffer.dc;
        const int content = w - contentLeft - margin;
        const size_t pixels = size_t(buffer.frame.width) * size_t(buffer.frame.height);
        const bool reuse = !pageDirty && pageCachePage == page && pageCacheWidth == buffer.frame.width &&
                           pageCacheHeight == buffer.frame.height && pageCache.size() == pixels;
        if (reuse) {
            GdiFlush();
            const size_t width = size_t(buffer.frame.width), height = size_t(buffer.frame.height);
            const auto rows = [&](size_t from, size_t to) {
                to = std::min(to, height);
                if (from < to)
                    std::copy(pageCache.begin() + from * width, pageCache.begin() + to * width, buffer.frame.pixels + from * width);
            };
            rows(0, size_t(std::max(0, destinationTop)));
            rows(size_t(std::max(0, sourceTop)), size_t(std::max(0, sourceTop + p(visibleHeight))));
            for(const auto& [id,control] : controls) {
                wchar_t kind[32]{};GetClassNameW(control,kind,32);
                if(_wcsicmp(kind,L"EDIT")==0 && !(GetWindowLongPtrW(control,GWL_STYLE)&ES_MULTILINE))
                    editFrame(memory,id);
            }
            replayMotions();
        } else {
            motions.clear();recordingMotion=true;
            if (ambient.size() != pixels) {
                fill(memory, {0,0,rect.right,buffer.frame.height}, waveStyle() ? waveSkyLow() : background);
                if (waveStyle()) {
                    buildWaveBackdrop(memory, rect.right, rect.bottom);
                    if (buffer.frame.height > rect.bottom) {
                        Gdiplus::Graphics graphics(memory);
                        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
                        Gdiplus::LinearGradientBrush deeper(Gdiplus::Point(0, int(rect.bottom) - 2), Gdiplus::Point(0, buffer.frame.height + 3),
                            drawingColor(waveSkyLow()), drawingColor(mix(waveSkyLow(), waveSkyTop(), 190)));
                        deeper.SetWrapMode(Gdiplus::WrapModeTileFlipXY);
                        graphics.FillRectangle(&deeper, 0, int(rect.bottom) - 1, int(rect.right), buffer.frame.height - int(rect.bottom) + 1);
                    }
                    GdiFlush();
                    paintGalaxy(buffer.frame.pixels, buffer.frame.width, int(rect.right), int(rect.bottom), allowMotion() ? double(motionTicks()) / 1000.0 : 0.0, false);
                }
                else {
                buffer.frame.radial(rect, rect.right * 3 / 4, -p(160), p(620), ember, 12);
                buffer.frame.radial(rect, 0, rect.bottom + p(60), p(440), orange, 5);
                }
                ambient.assign(buffer.frame.pixels, buffer.frame.pixels + pixels);
            } else {
                GdiFlush();
                std::copy(ambient.begin(), ambient.end(), buffer.frame.pixels);
            }
            if (page != 0)
                paintHeader(memory, w, content);
            if (page == finderPage) {
                panelBox(memory, contentLeft, 112, contentLeft + content, pageHeight - 40);
                heading(memory, L"Quick access", contentLeft + 20, 128, content - 220);
                text(memory, L"Type a page, setting, tool or player name. Use the arrow keys to choose, then press Enter.",
                     contentLeft + 20, 168, content - 40, 24, fonts.caption, muted);

                if (actionRows.empty()) text(memory, L"No matching actions. Try another search.",
                    contentLeft + 40, 278, content - 80, 48, fonts.body, muted);
            } else if(page==helpPage) {
                paintHelp(memory,pageHeight,content);
            } else if(page==notificationsPage) {
                paintNotifications(memory,pageHeight,content);
            } else if(page==achievementsPage) {
                paintAchievements(memory,pageHeight,content);
            } else if (page == 0) {
                paintPlay(memory, content);
                paintHeader(memory, w, content);
            }
            else if (page == profilePage)
                paintProfile(memory,pageHeight,content);
            else if (page == playerPage)
                paintPlayer(memory,pageHeight,content);
            else if (page == savesPage)
                paintSaves(memory, pageHeight, content);
            else if (page == modsPage)
                paintMods(memory, pageHeight, content);
            else if (page == importPage)
                paintImport(memory, pageHeight, content);
            else if (page == overlayPage)
                paintOverlay(memory, pageHeight, content);
            else if (page == tabsPage)
                paintTabs(memory, pageHeight, content);
            else if (page == colorsPage)
                paintColors(memory, pageHeight, content);
            else if (page == updatesPage)
                paintUpdates(memory, pageHeight, content);
            else if (page == controlsPage)
                paintControls(memory, pageHeight, content);
            else if (page == onlinePage)
                paintOnline(memory, pageHeight, content);
            else if (page == speedrunPage)
                paintSpeedrun(memory, pageHeight, content);
            else
                paintSettings(memory, pageHeight, content);
            for(const auto& [id,control] : controls) {
                wchar_t kind[32]{};GetClassNameW(control,kind,32);
                if(_wcsicmp(kind,L"EDIT")==0 && !(GetWindowLongPtrW(control,GWL_STYLE)&ES_MULTILINE))
                    editFrame(memory,id);
            }
            recordingMotion=false;
            sealMotions();
            GdiFlush();
            pageCache.assign(buffer.frame.pixels, buffer.frame.pixels + pixels);
            pageCachePage = page;
            pageCacheWidth = buffer.frame.width;
            pageCacheHeight = buffer.frame.height;
            pageDirty = false;
        }
        GdiFlush();
        const auto scroll=ui::ScrollCopy(buffer.frame.height,sourceTop,destinationTop,p(visibleHeight),rect.bottom);
        if (scroll.rows>0)
            std::memmove(buffer.frame.pixels+size_t(scroll.destination)*size_t(buffer.frame.width),
                         buffer.frame.pixels+size_t(scroll.source)*size_t(buffer.frame.width),
                         size_t(scroll.rows)*size_t(buffer.frame.width)*sizeof(uint32_t));
        if (waveStyle() && page != wavePage && pageCache.size() == ambient.size() && !pageCache.empty()) {
            const int bufferWidth = buffer.frame.width, canvasHeight = buffer.frame.height;
            const int top = destinationTop, bottom = destinationTop + p(visibleHeight);
            drawSky(buffer.frame.pixels, bufferWidth, std::min(canvasHeight, int(rect.bottom)), top, bottom, [&](int x, int y) {
                const int canvasRow = y - destinationTop + sourceTop;
                if (canvasRow < 0 || canvasRow >= canvasHeight) return false;
                const size_t index = size_t(canvasRow) * size_t(bufferWidth) + size_t(x);
                return pageCache[index] == ambient[index];
            });
            if (!skyTicking && hwnd) { skyTicking = true; SetTimer(hwnd, 11, 33, nullptr); }
        }
        fill(memory, {0, p(shellHeader + visibleHeight), rect.right, rect.bottom}, background);
        paintShell(memory,h);
        const int footer = h - 26;
        fill(memory, {p(contentLeft), p(footer - 12), p(contentLeft + content), p(footer - 12) + 1}, mix(background, border, 200));
        std::wstring message = status;
        if (busy()) {
            const auto current = progressText();
            if (!current.empty())
                message = current;
        }
        const bool connected = directory && directory->connected() && !directory->name_taken();
        const std::wstring build = L"v" + wide(application_version);
        const std::wstring link = connected ? std::wstring(L"Online") : activeProfile() ? std::wstring(L"Offline") : std::wstring();
        const int buildWidth = measure(memory, build, fonts.caption), linkWidth = link.empty() ? 0 : measure(memory, link, fonts.caption);
        const int right = contentLeft + content;
        text(memory, build, right - buildWidth, footer, buildWidth + 2, 20, fonts.caption, faint,
             DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
        int used = buildWidth + 18;
        if (!link.empty()) {
            const int x = right - used - linkWidth;
            ellipse(memory, p(x - 10), p(footer + 9), p(3), connected ? green : faint);
            text(memory, link, x, footer, linkWidth + 2, 20, fonts.caption, connected ? muted : faint,
                 DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
            used += linkWidth + 14;
            fill(memory, {p(right - buildWidth - 10), p(footer + 4), p(right - buildWidth - 9), p(footer + 15)}, border);
        }
        const COLORREF tone = error ? red : busy() ? orange : disc ? green : muted;
        ellipse(memory, p(contentLeft + 4), p(footer + 9), p(3), tone);
        if (busy())
            buffer.frame.halo({p(contentLeft + 4), p(footer + 9), p(contentLeft + 5), p(footer + 10)}, 0, p(8), orange, 80);
        const int messageWidth = waveStyle() ? std::max(120, w / 2 - 280 - contentLeft - 14) : content - 14 - used - 16;
        text(memory, message, contentLeft + 14, footer, messageWidth, 20,
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
        if (id >= PlayTab && id <= ControlsTab)
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
    static void iconShape(Gdiplus::Graphics& g,int id,Gdiplus::Color color) {
        Gdiplus::Pen pen(color,1.7f);pen.SetLineJoin(Gdiplus::LineJoinRound);
        pen.SetStartCap(Gdiplus::LineCapRound);pen.SetEndCap(Gdiplus::LineCapRound);
        Gdiplus::SolidBrush brush(color);
        auto line=[&](float a,float b,float c,float d){g.DrawLine(&pen,a,b,c,d);};
        if(id==OpenNotifications) {
            Gdiplus::GraphicsPath bell;bell.AddBezier(5.f,17.f,7.f,14.f,5.f,5.f,12.f,5.f);
            bell.AddBezier(12.f,5.f,19.f,5.f,17.f,14.f,19.f,17.f);bell.AddLine(19.f,17.f,5.f,17.f);
            g.DrawPath(&pen,&bell);line(10,21,14,21);line(12,2,12,5);
        } else if(id==HomeProfile) {g.DrawEllipse(&pen,8.f,3.f,8.f,8.f);g.DrawArc(&pen,4.f,12.f,16.f,16.f,180.f,180.f);}
        else if(id==OpenHelp){g.DrawEllipse(&pen,2.f,2.f,18.f,18.f);line(10,14,10,15);g.DrawArc(&pen,7.f,5.f,7.f,7.f,190.f,240.f);}
        else if(id==FindAction){g.DrawEllipse(&pen,3.f,3.f,12.f,12.f);line(14,14,21,21);}
        else if(id==PlayTab){Gdiplus::PointF p[]={{7,4},{21,12},{7,20}};g.FillPolygon(&brush,p,3);}
        else if(id==OnlineTab){g.DrawEllipse(&pen,2.f,2.f,20.f,20.f);g.DrawEllipse(&pen,8.f,2.f,8.f,20.f);line(2,12,22,12);}
        else if(id==SavesTab){g.DrawRectangle(&pen,4.f,3.f,16.f,18.f);g.DrawRectangle(&pen,8.f,3.f,8.f,6.f);g.DrawRectangle(&pen,8.f,14.f,8.f,7.f);}
        else if(id==SpeedrunTab){g.DrawEllipse(&pen,4.f,6.f,16.f,16.f);line(9,2,15,2);line(12,2,12,6);line(12,14,15,10);}
        else if(id==OpenAchievements){g.DrawArc(&pen,6.f,-6.f,12.f,20.f,0.f,180.f);line(6,3,18,3);g.DrawArc(&pen,2.f,3.f,6.f,6.f,90.f,180.f);g.DrawArc(&pen,16.f,3.f,6.f,6.f,270.f,180.f);line(12,14,12,18);line(8,20,16,20);line(9,18,15,18);}
        else if(id==ControlsTab){
            Gdiplus::GraphicsPath path;path.AddBezier(7.f,7.f,2.f,6.f,0.f,20.f,4.f,20.f);
            path.AddBezier(4.f,20.f,6.f,20.f,7.f,16.f,9.f,16.f);path.AddLine(9.f,16.f,15.f,16.f);
            path.AddBezier(15.f,16.f,17.f,16.f,18.f,20.f,20.f,20.f);path.AddBezier(20.f,20.f,24.f,20.f,22.f,6.f,17.f,7.f);
            path.CloseFigure();g.DrawPath(&pen,&path);line(7,10,7,14);line(5,12,9,12);g.FillEllipse(&brush,16.f,10.f,2.f,2.f);g.FillEllipse(&brush,18.f,13.f,2.f,2.f);
        } else if(id==UpdatesTab){line(12,3,12,15);line(8,11,12,15);line(12,15,16,11);line(4,16,4,21);line(4,21,20,21);line(20,21,20,16);}
        else if(id==ModsTab){line(12,2,22,7);line(22,7,12,12);line(12,12,2,7);line(2,7,12,2);line(2,7,2,17);line(2,17,12,22);line(12,22,22,17);line(22,17,22,7);line(12,12,12,22);}
        else {for(int i=0;i<3;++i){const float y=5.f+i*7.f,knob=i==1?16.f:8.f;line(3,y,21,y);g.FillEllipse(&brush,knob-2.5f,y-2.5f,5.f,5.f);}}
    }
    struct IconMask { int width=0,height=0; std::vector<uint8_t> alpha; };
    std::unordered_map<uint64_t,IconMask> iconMasks;
    const IconMask& iconMask(int id,int size,int fx,int fy) {
        const uint64_t key=(uint64_t(uint32_t(id))<<32)|(uint64_t(uint32_t(size))<<8)|uint64_t(fx<<2|fy);
        if(const auto found=iconMasks.find(key);found!=iconMasks.end())return found->second;
        if(iconMasks.size()>2400)iconMasks.clear();
        IconMask mask;mask.width=size+5;mask.height=size+5;
        constexpr int samples=4;
        Gdiplus::Bitmap source(mask.width*samples,mask.height*samples,PixelFormat32bppPARGB);
        {
            Gdiplus::Graphics graphics(&source);smoothDrawing(graphics);
            graphics.Clear(Gdiplus::Color(0,0,0,0));
            graphics.ScaleTransform(float(samples),float(samples));
            graphics.TranslateTransform(2.f+float(fx)/4.f,2.f+float(fy)/4.f);
            graphics.ScaleTransform(float(size)/24.f,float(size)/24.f);
            iconShape(graphics,id,Gdiplus::Color(255,255,255,255));
        }
        mask.alpha.assign(size_t(mask.width)*size_t(mask.height),0);
        Gdiplus::Rect bounds(0,0,mask.width*samples,mask.height*samples);Gdiplus::BitmapData data{};
        if(source.LockBits(&bounds,Gdiplus::ImageLockModeRead,PixelFormat32bppPARGB,&data)==Gdiplus::Ok) {
            for(int y=0;y<mask.height;++y)for(int x=0;x<mask.width;++x) {
                unsigned sum=0;
                for(int sy=0;sy<samples;++sy){const auto* row=static_cast<const unsigned char*>(data.Scan0)+(y*samples+sy)*data.Stride;
                    for(int sx=0;sx<samples;++sx)sum+=row[(x*samples+sx)*4+3];}
                mask.alpha[size_t(y)*size_t(mask.width)+size_t(x)]=uint8_t((sum+8)/16);
            }
            source.UnlockBits(&data);
        }
        return iconMasks.emplace(key,std::move(mask)).first->second;
    }
    void navigationIcon(HDC dc,int id,RECT r,COLORREF color,int pixels=0) {
        const float size=float(pixels>0 ? pixels : p(20)),x=pixels>0 ? float(r.left+r.right)/2-size/2 : float(r.left+p(17)),y=float(r.top)+(float(r.bottom-r.top)-size)/2;
        if(dc==buffer.dc && buffer.frame.pixels && size>=4.f && size<=512.f) {
            GdiFlush();
            const int ox=int(std::floor(x)),oy=int(std::floor(y));
            const int fx=std::clamp(int((x-float(ox))*4.f),0,3),fy=std::clamp(int((y-float(oy))*4.f),0,3);
            const auto& mask=iconMask(id,int(std::lround(size)),fx,fy);
            const int left=ox-2,top=oy-2;
            const int red=GetRValue(color),green=GetGValue(color),blue=GetBValue(color);
            for(int my=0;my<mask.height;++my) {
                const int py=top+my;if(py<0 || py>=buffer.frame.height)continue;
                uint32_t* row=buffer.frame.pixels+size_t(py)*size_t(buffer.frame.width);
                const uint8_t* coverage=mask.alpha.data()+size_t(my)*size_t(mask.width);
                for(int mx=0;mx<mask.width;++mx) {
                    const int a=coverage[mx],px=left+mx;
                    if(!a || px<0 || px>=buffer.frame.width)continue;
                    uint32_t& dst=row[px];
                    const auto mixChannel=[a](int base,int ink){return uint32_t(base+((ink-base)*a+127)/255);};
                    dst=(dst&0xff000000u)|mixChannel(int(dst>>16&255u),red)<<16|mixChannel(int(dst>>8&255u),green)<<8|mixChannel(int(dst&255u),blue);
                }
            }
            return;
        }
        sampledVector(dc,{LONG(x-2),LONG(y-2),LONG(std::ceil(x+size))+2,LONG(std::ceil(y+size))+2},[&](Gdiplus::Graphics& g){
        g.TranslateTransform(x,y);g.ScaleTransform(size/24.f,size/24.f);
        iconShape(g,id,drawingColor(color));
        });
    }
    void drawItem(const DRAWITEMSTRUCT &item) {
        auto dc = item.hDC;
        RECT r = item.rcItem;
        const bool enabled = (item.itemState & ODS_DISABLED) == 0;
        if (item.CtlType == ODT_BUTTON) {
            SavedDc saved(item.hDC);
            clipControlPaint(item.hDC, item.hwndItem);
            Canvas canvas(item.hDC, r);
            dc = canvas.dc;
            const int id = int(item.CtlID);
            const bool navigation = id >= PlayTab && id <= ControlsTab;
            const bool chosen = navigation && id == PlayTab + navigationPage();
            const bool primary = id == Play;
            const bool taskChoice = (id == TaskCharacters && importTask == Task::characters) ||
                                    (id == TaskStages && importTask == Task::stages) ||
                                    (id == TaskMovesets && importTask == Task::movesets) ||
                                    (id == TaskCapsules && importTask == Task::capsules) ||
                                    (id>=SettingsAll && id<=SettingsLauncher && searchText(trimmed(textOf(SettingsSearch)))==
                                        std::array<std::wstring,5>{L"",L"display",L"graphics",L"game",L"launcher"}[size_t(id-SettingsAll)]);
            const bool pressed = (item.itemState & ODS_SELECTED) != 0;
            const bool hot = hotButton == id && enabled;
            backdrop(dc, item.hwndItem, r, surface(id, r));
            COLORREF label;
            const bool lit = (primary || taskChoice || id == ImportSelected || (page==0 && id==JoinRoom)) && enabled;
            if (navigation) {
                if (hot && !chosen)
                    box(dc, r, mix(sidebar, ink, 10), mix(sidebar, ink, 10), 10);
                label = chosen || hot ? ink : muted;
            } else if (id == OpenNotifications && waveStyle()) {
                label = ink;
            } else if (lit) {
                const auto base = settings.launcherColors[main_button_role] >= 0 ? buttonTop : orange;
                const auto accent = pressed ? mix(base,background,40) : hot ? mix(base,ink,20) : base;
                box(dc,r,accent,accent,10);
                label = buttonInk;
            } else {
                const bool quick = id>=HomeControls && id<=HomeArrange;
                const bool onBanner = id == Play || id == LoadState;
                const COLORREF idle = onBanner ? mix(heroTo, bannerTitle, 26) : panel;
                const COLORREF color = !enabled ? idle : pressed ? field : hot ? hover : quick ? panel : raised;
                const COLORREF outline = !enabled ? mix(idle, onBanner ? bannerTitle : border, onBanner ? 50 : 140) : hot ? mix(border, orange, 110) : border;
                box(dc, r, color, outline, 10);
                label = enabled ? ink : onBanner ? mix(bannerTitle, heroTo, 120) : faint;
                if (id == DeleteSelected && enabled) label = red;
            }
            wchar_t text[100]{};
            GetWindowTextW(item.hwndItem, text, 100);
            auto old = SelectObject(dc, primary || navigation || taskChoice || id == ImportSelected ? fonts.strong
                                                                                                                 : fonts.body);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, label);
            RECT textRect = r;
            if (navigation) {
                RECT tile{r.left+(r.right-r.left)/2-p(27),r.top+p(5),r.right,r.top+p(31)};
                navigationIcon(dc,id,tile,chosen ? orange : hot ? ink : muted);
                textRect.top=r.top+p(32);textRect.bottom=r.bottom-p(5);
                SelectObject(dc,fonts.caption);
                SetTextColor(dc,chosen ? orange : label);
                DrawTextW(dc,text,-1,&textRect,DT_SINGLELINE|DT_VCENTER|DT_CENTER|DT_NOPREFIX);
                if(id==OnlineTab && noticeCount()>0)ellipse(dc,r.right-p(12),r.top+p(9),p(3),orange);
            } else if(id==OpenNotifications) {
                navigationIcon(dc,OpenNotifications,{r.left-p(7),r.top,r.right,r.bottom},hasNotice() ? orange : waveStyle() ? (hot ? ink : mix(muted,ink,120)) : muted);
                if(hasNotice()) {
                    const int count=noticeCount();
                    const std::wstring label=count>9 ? L"9+" : std::to_wstring(count);
                    const int cx=r.right-p(8),cy=r.top+p(8),radius=p(8);
                    ellipse(dc,cx,cy,radius+std::max(1,p(1)),surface(id,r));
                    ellipse(dc,cx,cy,radius,orange);
                    auto previous=SelectObject(dc,fonts.label);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,buttonInk);
                    RECT digits{cx-radius,cy-radius,cx+radius+1,cy+radius+1};
                    DrawTextW(dc,label.c_str(),-1,&digits,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
                    SelectObject(dc,previous);
                }
            } else if (id == FindAction) {
                navigationIcon(dc,FindAction,{r.left-p(3),r.top,r.right,r.bottom},muted);
                textRect.left += p(44); textRect.right -= p(64);
                DrawTextW(dc,text,-1,&textRect,DT_SINGLELINE|DT_VCENTER|DT_LEFT|DT_NOPREFIX);
                RECT key{r.right-p(56),r.top+p(7),r.right-p(8),r.bottom-p(7)};
                box(dc,key,field,border,4); SelectObject(dc,fonts.label);SetTextColor(dc,muted);
                DrawTextW(dc,L"Ctrl+K",-1,&key,DT_SINGLELINE|DT_VCENTER|DT_CENTER|DT_NOPREFIX);
            } else {
                if (primary && enabled) {
                    SIZE size{}; GetTextExtentPoint32W(dc,text,int(wcslen(text)),&size);
                    const int start = r.left + std::max(p(12),int(r.right-r.left-size.cx-p(32))/2);
                    const int cx = start + p(6), cy = (r.top + r.bottom) / 2, s = p(6);
                    const Gdiplus::PointF play[]={{float(cx-s/2),float(cy-s)},{float(cx+s),float(cy)},{float(cx-s/2),float(cy+s)}};
                    smoothPolygon(dc,play,3,label);
                    textRect.left = start + p(28);
                }
                const bool quick = id >= HomeControls && id <= HomeArrange;
                if (quick) {
                    const int target = id == HomeControls ? ControlsTab : id == HomeSaves ? SavesTab :
                                       id == HomeSettings ? SettingsTab : ModsTab;
                    navigationIcon(dc, target, {r.left-p(7),r.top,r.right,r.bottom}, muted);
                    textRect.left += p(42);
                    textRect.right -= p(28);
                    const int x=r.right-p(18), y=(r.top+r.bottom)/2;
                    smoothLines(dc,{{float(x-p(3)),float(y-p(3))},{float(x),float(y)},{float(x-p(3)),float(y+p(3))}},muted,float(std::max(1,p(1))));
                    SelectObject(dc,fonts.strong);
                    textRect.top += p(9); textRect.bottom = textRect.top + p(20);
                    DrawTextW(dc,text,-1,&textRect,DT_SINGLELINE|DT_LEFT|DT_END_ELLIPSIS|DT_NOPREFIX);
                    SelectObject(dc,fonts.caption); SetTextColor(dc,muted);
                    textRect.top += p(22); textRect.bottom = r.bottom-p(6);
                    const wchar_t *hint = id == HomeControls ? L"Keys & controller" : L"Memory card & backups";
                    DrawTextW(dc,hint,-1,&textRect,DT_SINGLELINE|DT_LEFT|DT_END_ELLIPSIS|DT_NOPREFIX);
                }
                if (!quick) DrawTextW(dc, text, -1, &textRect, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX |
                          (primary && enabled ? DT_LEFT : DT_CENTER));
            }
            SelectObject(dc, old);
            if ((item.itemState & ODS_FOCUS) && !(item.itemState & ODS_NOFOCUSRECT) && !chosen) {
                RECT focus = r;
                InflateRect(&focus, -p(3), -p(3));
                smoothBox(dc,focus,panel,lit ? buttonInk : navigation ? mix(sidebar,orange,70) : orange,float(p(8)),false);
            }
            return;
        }
        const bool chosen = (item.itemState & ODS_SELECTED) != 0;
        const bool list = item.CtlType == ODT_LISTBOX;
        std::optional<Canvas> rowCanvas;
        if (r.right > r.left && r.bottom > r.top) {rowCanvas.emplace(item.hDC, r);dc = rowCanvas->dc;}
        const bool friends = item.hwndItem == controls[FriendList];
        const COLORREF base = friends ? panel : list ? field : panel;
        fill(dc, r, base);
        if (chosen && waveStyle()) {
            Gdiplus::Graphics graphics(dc);
            const Gdiplus::REAL left = Gdiplus::REAL(r.left), top = Gdiplus::REAL(r.top), width = Gdiplus::REAL(r.right - r.left), height = Gdiplus::REAL(r.bottom - r.top);
            Gdiplus::LinearGradientBrush glow(Gdiplus::PointF(left, 0), Gdiplus::PointF(left + width, 0), Gdiplus::Color(80, 255, 226, 176), Gdiplus::Color(10, 255, 226, 176));
            graphics.FillRectangle(&glow, left, top, width, height);
            Gdiplus::LinearGradientBrush edge(Gdiplus::PointF(left, 0), Gdiplus::PointF(left + width, 0), Gdiplus::Color(170, 255, 240, 210), Gdiplus::Color(0, 255, 240, 210));
            graphics.FillRectangle(&edge, left, top, width, Gdiplus::REAL(std::max(1, p(1))));
            graphics.FillRectangle(&edge, left, top + height - Gdiplus::REAL(std::max(1, p(1))), width, Gdiplus::REAL(std::max(1, p(1))));
        } else if (chosen) {
            fill(dc, r, mix(base, orange, 26));
            fill(dc, {r.left, r.top + p(list ? 8 : 6), r.left + p(3), r.bottom - p(list ? 8 : 6)}, orange);
        }
        if (item.itemID != UINT(-1)) {
            drawingRow=true;
            const bool online=drawOnlineRow(dc, item, r, chosen, base);
            drawingRow=false;
            if (online) return;
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
        if (item.hwndItem == controls[ProfileNameColor] && item.itemID != UINT(-1) && !(item.itemState & ODS_COMBOBOXEDIT) && !value.empty()) {
            const int index = int(item.itemID);
            const auto effects = heldTag() ? online::tag_effects(*heldTag()) : std::vector<unsigned>{};
            const size_t slot = size_t(std::max(0, index - name_color_custom - 1));
            const unsigned effect = index > name_color_custom && slot < effects.size() ? effects[slot] : 0u;
            COLORREF tone = enabled ? ink : muted;
            if (index > 0 && index < name_color_custom) tone = readableName(storedColor(nameColorAt(index - 1)), ink);
            RECT cell = r;
            cell.left += p(4);
            if (index > 0 && index < name_color_custom) {
                ellipse(dc, r.left + p(5), (r.top + r.bottom) / 2, p(5), nameColorAt(index - 1));
                cell.left += p(14);
            }
            if (effect) {
                drawingRow = true;
                drawName(dc, value, cell, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX, std::string(), tone, effect);
                drawingRow = false;
            } else {
                SetTextColor(dc, tone);
                DrawTextW(dc, value.c_str(), int(value.size()), &cell, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            }
            SelectObject(dc, old);
            return;
        }
        if (item.hwndItem == controls[SplitStyleList]) {
            const auto style = splitStyle(item.itemID);
            if (style && !style->image.empty()) {
                paintSplitImage(dc,style->image,{r.left,r.top+p(6),r.left+p(26),r.bottom-p(6)},chosen?mix(base,orange,26):base);
                r.left += p(36);
            }
            SetTextColor(dc,enabled?ink:muted);
            const auto name=splitName(item.itemID);
            DrawTextW(dc,name.c_str(),int(name.size()),&r,DT_LEFT|DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
            SelectObject(dc,old);return;
        }
        if (item.hwndItem == controls[SplitList] && item.itemID < splitBook.best().size()) {
            const size_t index=item.itemID;
            const double segment=splitBook.best()[index]-(index?splitBook.best()[index-1]:0.0);
            parts={splitName(index),wide(splitTime(splitBook.best()[index])),wide(splitTime(segment)),
                   index<splitBook.golds().size() && splitBook.golds()[index]>0 ? wide(splitTime(splitBook.golds()[index])) : L"–"};
            const int width = r.right - r.left + p(24);
            SetTextColor(dc, enabled ? ink : muted);
            RECT nameCell = r;
            nameCell.right = r.left - p(14) + width * splitNameColumn / 100 - p(8);
            if (const auto style=splitStyle(item.itemID);style && !style->image.empty()) {
                paintSplitImage(dc,style->image,{nameCell.left,nameCell.top+p(5),nameCell.left+p(22),nameCell.bottom-p(5)},chosen?mix(base,orange,26):base);
                nameCell.left+=p(28);
            }
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
    bool selectedRoomLocked() const {
        const auto found = controls.find(RoomList);
        if (found == controls.end())
            return false;
        const auto row = SendMessageW(found->second, LB_GETCURSEL, 0, 0);
        return row >= 0 && size_t(row) < roomRows.size() && roomRows[size_t(row)].locked;
    }
    std::optional<online::Room> roomHostedBy(const std::string &name) const {
        for(const auto& candidate:availableRooms)
            if(candidate.host==name && candidate.version==application_version)return candidate;
        return std::nullopt;
    }
    std::wstring hostingLine(const std::string &name) const {
        const auto found=roomHostedBy(name);
        if(!found)return {};
        return L"Hosting " + (found->name.empty() ? std::wstring(L"a room") : wide(found->name)) +
               (found->locked ? L" · password" : L"");
    }
    static std::wstring statusLabel(const std::string &status) {
        if (status.empty())
            return L"Online";
        auto out = wide(status);
        out[0] = wchar_t(towupper(out[0]));
        return out;
    }
    bool drawOnlineRow(HDC dc, const DRAWITEMSTRUCT &item, RECT r, bool chosen, COLORREF base) {
        const size_t row = item.itemID;
        const bool rooms = item.hwndItem == controls[RoomList], players = item.hwndItem == controls[PlayerList],
                   friends = item.hwndItem == controls[FriendList];
        if (!(rooms && row < roomRows.size()) && !(players && row < playerDetails.size()) &&
            !(friends && row < friendRows.size() && row < friendStatus.size()))
            return false;
        const COLORREF ring = chosen ? mix(base, orange, 26) : base;
        if (row > 0)
            fill(dc, {r.left + p(12), r.top, r.right - p(12), r.top + 1}, mix(base, border, 160));
        const int cy = (r.top + r.bottom) / 2;
        const int radius = p(friends ? 13 : 15), textLeft = r.left + p(friends ? 12 : 14) + 2 * radius + p(friends ? 10 : 12);
        int right = r.right - p(12);
        std::wstring title, line;
        COLORREF titleColor = ink;
        auto old = SelectObject(dc, fonts.body);
        SetBkMode(dc, TRANSPARENT);
        if (rooms) {
            const auto &room = roomRows[row];
            const bool same = room.version == application_version;
            title = wide(room.name.empty() ? room.host + "'s room" : room.name);
            line = L"Hosted by " + wide(room.host);
            titleColor = same ? ink : muted;
            const auto hostId = playerNamed(room.host);
            avatar(dc, r.left + p(14) + radius, cy, radius, wide(room.host), hostId.empty() ? room.host : hostId, ring, -1, !same);
            if (!same) {
                const auto value = room.version.empty() ? std::wstring(L"OTHER VERSION") : L"V" + wide(room.version);
                tag(dc, right, cy, value, red);
                right -= tagWidth(dc, value) + p(6);
            } else {
                const auto found = roomPings.find(room.id);
                const std::wstring value = found == roomPings.end() ? std::wstring(L"CHECKING")
                                           : found->second < 0      ? std::wstring(L"RELAY")
                                                                    : std::to_wstring(found->second) + L" MS";
                const COLORREF tone = found == roomPings.end() || found->second < 0 ? muted
                                      : found->second < 80                         ? green
                                      : found->second < 160                        ? gold
                                                                                   : red;
                tag(dc, right, cy, value, tone);
                right -= tagWidth(dc, value) + p(6);
            }
            if (room.locked) {
                tag(dc, right, cy, L"PASSWORD", gold);
                right -= tagWidth(dc, L"PASSWORD") + p(6);
            }
            if (room.id == hostingRoom) {
                tag(dc, right, cy, L"YOURS", orange, true);
                right -= tagWidth(dc, L"YOURS") + p(6);
            }
        } else if (players) {
            const auto &player = playerDetails[row];
            const bool busy = player.status == "in a match" || player.status == "hosting" || player.status == "joining" || player.status == "away" || player.status == "do not disturb";
            title = wide(player.name);
            line = statusLabel(player.status);
            if (const auto hosted = hostingLine(player.name); !hosted.empty())
                line = hosted;
            else if (const auto card = cardLine(player.id); !card.empty())
                line += L" \u00b7 " + card;
            titleColor = nameInk(player.id, titleColor);
            avatar(dc, r.left + p(14) + radius, cy, radius, title, player.id, ring, busy ? 2 : 1);
            if (player.version != application_version) {
                const auto value = player.version.empty() ? std::wstring(L"OTHER VERSION") : L"V" + wide(player.version);
                tag(dc, right, cy, value, red);
                right -= tagWidth(dc, value) + p(6);
            }
            if(isFriend(player.id)) {
                tag(dc,right,cy,L"FRIEND",green);right-=tagWidth(dc,L"FRIEND")+p(6);
            }
            if(isBlocked(player.id)) {
                tag(dc,right,cy,L"BLOCKED",red);right-=tagWidth(dc,L"BLOCKED")+p(6);titleColor=muted;
            } else if(recentSeen(player.id)) {
                tag(dc,right,cy,L"PLAYED",sky);right-=tagWidth(dc,L"PLAYED")+p(6);
            }
            if (player.impostor) {
                tag(dc, right, cy, L"UNVERIFIED", red);
                right -= tagWidth(dc, L"UNVERIFIED") + p(6);
            }
            if (requestedFriends.count(player.id)) {
                tag(dc, right, cy, L"REQUESTED", sky);
                right -= tagWidth(dc, L"REQUESTED") + p(6);
            }
        } else {
            const auto &status = friendStatus[row];
            const bool on = !status.empty();
            const bool busy = status == "in a match" || status == "hosting" || status == "joining" || status == "away" || status == "do not disturb";
            title = wide(friendRows[row].second);
            line = on ? statusLabel(status) : homeScope == 3 ? playedText(recentSeen(friendRows[row].first)) : seenText(friendSeen(friendRows[row].first));
            if (const auto hosted = hostingLine(friendRows[row].second); on && !hosted.empty())
                line = hosted;
            else if (const auto card = cardLine(friendRows[row].first); on && !card.empty())
                line += L" \u00b7 " + card;
            titleColor = on ? nameInk(friendRows[row].first, ink) : muted;
            avatar(dc, r.left + p(12) + radius, cy, radius, title, friendRows[row].first, ring, on ? (busy ? 2 : 1) : 0, !on);
        }
        SetTextColor(dc, titleColor);
        SelectObject(dc, friends ? fonts.body : fonts.strong);
        RECT top{textLeft, r.top + p(friends ? 5 : 6), right - p(6), cy + p(friends ? 2 : 3)};
        const std::string titleId = players ? playerDetails[row].id : friends ? friendRows[row].first : std::string();
        drawName(dc, title, top, DT_SINGLELINE | DT_BOTTOM | DT_END_ELLIPSIS | DT_NOPREFIX, titleId, titleColor,
                 titleColor == muted ? 0u : ~0u);
        SelectObject(dc, fonts.caption);
        SetTextColor(dc, titleColor == ink ? mix(muted, ink, 60) : muted);
        RECT bottom{textLeft, cy + p(2), right - p(6), r.bottom - p(4)};
        DrawTextW(dc, line.c_str(), int(line.size()), &bottom, DT_SINGLELINE | DT_TOP | DT_END_ELLIPSIS | DT_NOPREFIX);
        SelectObject(dc, old);
        return true;
    }
    void controlGlyph(HDC dc, unsigned index, int x, int y) {
        const RECT tile{p(x), p(y), p(x + 30), p(y + 30)};
        box(dc, tile, mix(panel, field, 200), mix(panel, border, 160), 8);
        const int cx = p(x + 15), cy = p(y + 15), r = p(7);
        if(index>=input::Cross && index<=input::Triangle) {
            sampledVector(dc,tile,[&](Gdiplus::Graphics& graphics){
            const COLORREF colors[]={RGB(120,160,240),RGB(236,98,98),RGB(232,130,196),RGB(96,206,160)};
            const auto color=index==input::Cross ? colors[0] : index==input::Circle ? colors[1] : index==input::Square ? colors[2] : colors[3];
            Gdiplus::Pen pen(drawingColor(color),float(std::max(1,p(2))));pen.SetLineJoin(Gdiplus::LineJoinRound);
            pen.SetStartCap(Gdiplus::LineCapRound);pen.SetEndCap(Gdiplus::LineCapRound);
            if(index==input::Cross){graphics.DrawLine(&pen,float(cx-r),float(cy-r),float(cx+r),float(cy+r));graphics.DrawLine(&pen,float(cx+r),float(cy-r),float(cx-r),float(cy+r));}
            else if(index==input::Circle)graphics.DrawEllipse(&pen,float(cx-r),float(cy-r),float(r*2),float(r*2));
            else if(index==input::Square)graphics.DrawRectangle(&pen,float(cx-r),float(cy-r),float(r*2),float(r*2));
            else {Gdiplus::PointF points[]={{float(cx),float(cy-r)},{float(cx+r),float(cy+r)},{float(cx-r),float(cy+r)},{float(cx),float(cy-r)}};graphics.DrawLines(&pen,points,4);}
            });
        }
        else if (index <= input::Right) {
            sampledVector(dc, tile, [&](Gdiplus::Graphics& graphics) {
                Gdiplus::Pen pen(drawingColor(ink), float(std::max(1, p(2))));
                pen.SetLineJoin(Gdiplus::LineJoinRound);
                pen.SetStartCap(Gdiplus::LineCapRound);
                pen.SetEndCap(Gdiplus::LineCapRound);
                const float dx[] = {0.f, 0.f, -1.f, 1.f}, dy[] = {-1.f, 1.f, 0.f, 0.f};
                const float ux = dx[index], uy = dy[index], length = float(r), head = float(r) * 0.6f;
                const Gdiplus::PointF tip{float(cx) + ux * length, float(cy) + uy * length};
                graphics.DrawLine(&pen, float(cx) - ux * length, float(cy) - uy * length, tip.X, tip.Y);
                graphics.DrawLine(&pen, tip.X, tip.Y, tip.X - ux * head - uy * head, tip.Y - uy * head + ux * head);
                graphics.DrawLine(&pen, tip.X, tip.Y, tip.X - ux * head + uy * head, tip.Y - uy * head - ux * head);
            });
        } else {
            const wchar_t *labels[] = {L"L1", L"R1", L"L2", L"R2", L"ST", L"SE", L"L3", L"R3"};
            text(dc, labels[index - input::L1], x, y, 30, 30, fonts.label, ink, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
    }
    void paintControls(HDC dc, int h, int content) {
        const int right = contentLeft + content;
        const ControlsLayout grid(content);
        const int bindingsBottom = grid.bottom();
        panelBox(dc, contentLeft, controlsTop, right, bindingsBottom);
        heading(dc, L"Bindings", contentLeft + 20, controlsTop + 12, 200);
        text(dc, capturing >= 0 ? L"Press the new key now. Click the same box again to cancel."
                                : L"Click a key box, then press a key. Right-click a box to clear it.",
             contentLeft + 20, controlsTop + 42, content - 40, 18, fonts.caption, capturing >= 0 ? orange : muted,
             DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        const wchar_t *names[] = {L"Up", L"Down", L"Left", L"Right", L"Cross", L"Circle", L"Square", L"Triangle",
                                  L"L1", L"R1", L"L2", L"R2", L"Start", L"Select", L"L3", L"R3"};
        for (int half = 0; half < 2; ++half) {
            const int x = grid.x(unsigned(half * 8)), y = grid.y(unsigned(half * 8)) - 24;
            caps(dc, L"BUTTON", x, y, 100);
            caps(dc, L"KEY", x + 104, y, grid.key);
            caps(dc, L"ALT KEY", x + 104 + grid.key + 10, y, grid.key);
            caps(dc, L"CONTROLLER", x + 104 + 2 * grid.key + 20, y, grid.pad);
        }
        if (!grid.stacked) fill(dc, {p(contentLeft + 20 + grid.column + 11), p(controlRowTop - 6), p(contentLeft + 20 + grid.column + 12),
                  p(controlRowTop + 8 * 44 - 6)}, mix(panel, border, 160));
        for (unsigned i = 0; i < input::button_count; ++i) {
            const int x = grid.x(i), y = grid.y(i);
            if (i % 8u)
                fill(dc, {p(x), p(y - 4), p(x + grid.column), p(y - 4) + 1}, mix(panel, border, 90));
            controlGlyph(dc, i, x, y);
            text(dc, names[i], x + 38, y + 5, 62, 20, fonts.body, ink, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        }
        const int notesTop = bindingsBottom + 14, notesBottom = std::max(notesTop + 96, h - 64);
        panelBox(dc, contentLeft, notesTop, right, notesBottom);
        heading(dc, L"In game", contentLeft + 20, notesTop + 12, 200);
        text(dc, L"Player 1 uses the keyboard and the first controller. Player 2 uses the second controller.",
             contentLeft + 20, notesTop + 40, content - 40, 18, fonts.caption, muted, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        text(dc, L"Xbox, PlayStation and most other controllers use the same layout. F1 to F12 and Ctrl stay reserved for OpenKai2 shortcuts.",
             contentLeft + 20, notesTop + 60, content - 40, 18, fonts.caption, muted, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
    void refreshControls() {
        for (unsigned i = 0; i < input::button_count; ++i) {
            for (int set = 0; set < 2; ++set) {
                const int id = (set ? ControlAlternate : ControlKey) + int(i), slot = set * int(input::button_count) + int(i);
                const int value = set ? settings.controls.alternate[i] : settings.controls.keys[i];
                const auto label = capturing == slot ? std::wstring(L"Press a key…") : wide(input::key_name(value));
                if (const auto found = controls.find(id); found != controls.end()) {
                    SetWindowTextW(found->second, label.c_str());
                    InvalidateRect(found->second, nullptr, FALSE);
                }
            }
            if (const auto found = controls.find(ControlPad + int(i)); found != controls.end()) {
                const auto code = std::find(padChoices.begin(), padChoices.end(), settings.controls.pad[i]);
                SendMessageW(found->second, CB_SETCURSEL, WPARAM(code == padChoices.end() ? 0 : code - padChoices.begin()), 0);
            }
        }
        (pageDirty=true),InvalidateRect(hwnd, nullptr, FALSE);
    }
    bool saveControls(const input::Bindings &bindings, const std::wstring &message) {
        Settings next = settings;
        next.controls = bindings;
        try {
            write_settings(settings_path(), next);
        } catch (const std::exception &e) {
            status = L"Could not save the controls: " + wide(e.what());
            error = true;
            refreshControls();
            return false;
        }
        settings = std::move(next);
        status = message;
        error = false;
        refreshControls();
        return true;
    }
    void beginCapture(int slot) {
        capturing = capturing == slot ? -1 : slot;
        status = capturing >= 0 ? L"Press a key for " + wide(input::button_names[size_t(slot % int(input::button_count))]) + L"."
                                : std::wstring(L"Binding unchanged.");
        error = false;
        refreshControls();
    }
    bool captureKey(WPARAM wp, LPARAM lp) {
        if (capturing < 0 || page != controlsPage)
            return false;
        const bool extended = ((lp >> 24) & 1) != 0;
        unsigned code = unsigned(wp);
        if (code == VK_SHIFT)
            code = MapVirtualKeyW(unsigned((lp >> 16) & 0xFF), MAPVK_VSC_TO_VK_EX);
        if (code == VK_MENU)
            code = extended ? VK_RMENU : VK_LMENU;
        const int key = input::key_from_virtual(code, extended);
        if (key == input::no_input || !input::valid_key(key)) {
            status = L"That key is reserved for OpenKai2 or can't be read by the game. Try another.";
            error = true;
            (pageDirty=true),InvalidateRect(hwnd, nullptr, FALSE);
            return true;
        }
        const int slot = capturing;
        capturing = -1;
        auto next = settings.controls;
        const unsigned index = unsigned(slot) % input::button_count;
        std::wstring moved;
        for (unsigned i = 0; i < input::button_count; ++i)
            for (auto *set : {&next.keys, &next.alternate})
                if ((*set)[i] == key && !(i == index && set == (slot < int(input::button_count) ? &next.keys : &next.alternate))) {
                    (*set)[i] = input::no_input;
                    moved = wide(input::button_names[i]);
                }
        (slot < int(input::button_count) ? next.keys : next.alternate)[index] = key;
        saveControls(next, wide(input::key_name(key)) + L" is now " + wide(input::button_names[index]) +
                               (moved.empty() || moved == wide(input::button_names[index]) ? L"." : L", and no longer " + moved + L"."));
        return true;
    }
    bool clearControl(int id) {
        if (page != controlsPage)
            return false;
        const bool primary = id >= ControlKey && id < ControlKey + int(input::button_count);
        const bool alternate = id >= ControlAlternate && id < ControlAlternate + int(input::button_count);
        if (!primary && !alternate)
            return false;
        const unsigned index = unsigned(id - (primary ? ControlKey : ControlAlternate));
        capturing = -1;
        auto next = settings.controls;
        (primary ? next.keys : next.alternate)[index] = input::no_input;
        saveControls(next, wide(input::button_names[index]) + (primary ? L" key cleared." : L" alternate key cleared."));
        return true;
    }
    void choosePadButton(unsigned index) {
        const int choice = selected(ControlPad + int(index));
        if (choice < 0 || size_t(choice) >= padChoices.size())
            return;
        const int code = padChoices[size_t(choice)];
        auto next = settings.controls;
        std::wstring moved;
        if (code != input::no_input)
            for (unsigned i = 0; i < input::button_count; ++i)
                if (i != index && next.pad[i] == code) {
                    next.pad[i] = input::no_input;
                    moved = wide(input::button_names[i]);
                }
        next.pad[index] = code;
        saveControls(next, wide(input::pad_button_names[size_t(code)]) + L" is now " + wide(input::button_names[index]) +
                               (moved.empty() ? L"." : L", and no longer " + moved + L"."));
    }
    void refreshBackups() {
        savedGames = disc ? list_card_saves(card()) : std::vector<std::string>{};
        backups = disc ? list_backups(backupPath()) : std::vector<std::filesystem::path>{};
        const auto query = searchText(trimmed(textOf(SaveSearch)));
        savedGames.erase(std::remove_if(savedGames.begin(), savedGames.end(), [&](const std::string &name) {
            const auto label = name == "BASLUS-20779DBZ2" ? L"Budokai 2 USA " + wide(name) : wide(name);
            return !query.empty() && searchText(label).find(query) == std::wstring::npos;
        }), savedGames.end());
        backups.erase(std::remove_if(backups.begin(), backups.end(), [&](const std::filesystem::path &file) {
            return !query.empty() && searchText(file.filename().wstring()).find(query) == std::wstring::npos;
        }), backups.end());
        std::sort(savedGames.begin(), savedGames.end());
        if (selected(SaveSort) == 1) std::reverse(backups.begin(), backups.end());
        if (selected(SaveSort) == 2) std::sort(backups.begin(), backups.end(), [](const auto &a, const auto &b) {
            return a.filename() < b.filename();
        });
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
                               std::to_wstring((std::filesystem::file_size(p) + 1023) / 1024) + L" KB     " + p.filename().wstring();
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
        std::vector<mods::ModConflict> conflicts;
        try {
            conflicts = mods::mod_conflicts(modList);
        } catch (const std::exception &) {
        }
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
            for (const auto &conflict : conflicts)
                if (conflict.later == mod.name)
                    label += L"   ·   overrides " + wide(conflict.earlier) + L" (" + std::to_wstring(conflict.count) +
                             (conflict.count == 1 ? L" change)" : L" changes)");
                else if (conflict.earlier == mod.name)
                    label += L"   ·   partly overridden by " + wide(conflict.later);
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
        for (uint32_t id = 0; id < mods::roster_slots; ++id)
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
        if (extension == L".png" || extension == L".jpg" || extension == L".jpeg" || extension == L".gif") {
            if(page==profilePage)attachProfileImage(image);
            else if (page == overlayPage && settings.speedrun) attachSplitImage(image);
            else {
                status=L"Open your Profile page or Customize overlay to add a picture.";
                error=false;(pageDirty=true),InvalidateRect(hwnd,nullptr,FALSE);
            }
            return;
        }
        if (extension != L".iso") {
            status = L"Choose an ISO, or add PNG, JPEG and GIF images on your Profile or Speedrun overlay page.";
            (pageDirty=true),InvalidateRect(hwnd, nullptr, FALSE);
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
    bool fullscreen=false;
    LONG_PTR windowedStyle=0;
    WINDOWPLACEMENT windowedPlacement{sizeof(WINDOWPLACEMENT)};
    void coverMonitor() {
        MONITORINFO monitor{sizeof(monitor)};
        if(!GetMonitorInfoW(MonitorFromWindow(hwnd,MONITOR_DEFAULTTONEAREST),&monitor))return;
        const auto& r=monitor.rcMonitor;
        SetWindowPos(hwnd,HWND_TOP,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_NOOWNERZORDER|SWP_FRAMECHANGED);
    }
    void toggleFullscreen() {
        if(!fullscreen) {
            windowedStyle=GetWindowLongPtrW(hwnd,GWL_STYLE);
            windowedPlacement.length=sizeof(windowedPlacement);
            if(!GetWindowPlacement(hwnd,&windowedPlacement))return;
            fullscreen=true;
            SetWindowLongPtrW(hwnd,GWL_STYLE,(windowedStyle&~LONG_PTR(WS_OVERLAPPEDWINDOW))|WS_POPUP);
            coverMonitor();
            status=L"Fullscreen. Press F11 to return to a window.";error=false;
        } else {
            fullscreen=false;
            SetWindowLongPtrW(hwnd,GWL_STYLE,windowedStyle);
            SetWindowPlacement(hwnd,&windowedPlacement);
            SetWindowPos(hwnd,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOOWNERZORDER|SWP_FRAMECHANGED);
            status=L"Back to a window.";error=false;
        }
        layout();queueUiRestore();(pageDirty=true),InvalidateRect(hwnd,nullptr,FALSE);
    }
    void rememberPlacement() {
        WINDOWPLACEMENT placement{sizeof(placement)};
        if (fullscreen)
            placement = windowedPlacement;
        else if (!GetWindowPlacement(hwnd, &placement))
            return;
        RECT r = placement.rcNormalPosition;
        if (!fullscreen && !IsIconic(hwnd) && !IsZoomed(hwnd))
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
                     wearerMotions, stem, force, character = model.id](const std::shared_ptr<Progress> &progress) mutable {
                        std::vector<std::pair<uint32_t, std::string>> moveFields;
                        std::wstring voiceNote;
                        if (force) {
                            report(progress, L"Importing the voice", 0, 1);
                            if (const auto bank = mods::budokai3_voice_bank(*disc, character >> 8)) {
                                mods::ImportTarget target = targets.front();
                                target.file = mods::voice_archive;
                                target.name = stem + "_" + std::to_string(entry) + "_voice.bin";
                                target.source = utf8(disc->image().filename().wstring()) + ":" +
                                                mods::budokai3_voice_archive + "#" + std::to_string(*bank);
                                const auto written =
                                    mods::import_bytes(mod, target, disc->read_entry(mods::budokai3_voice_archive, *bank));
                                for (auto &character : added)
                                    character.voice = "add:" + written.filename().string();
                                voiceNote = L" It has its own Budokai 3 fight voice.";
                            }
                        }
                        if (ownMoves) {
                            report(progress, L"Converting the Budokai 3 moves", 0, 1);
                            const auto moveset = mods::budokai3_moveset(*disc, entry, *game, wearerMotions);
                            if (!moveset)
                                throw std::runtime_error("This Budokai 3 model has no move script next to it to convert");
                            voiceNote += L" " + std::to_wstring(moveset->renumbered) +
                                         (moveset->ownMatched
                                              ? L" of its moves were matched to Budokai 2's move numbers, using its own Budokai 2 moveset."
                                              : L" of its moves were matched to Budokai 2's move numbers; it is not in Budokai 2, so some special moves may do another move.");
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
                        result.message = summary + voiceNote + L" It loads the next time the game starts.";
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
        (pageDirty=true),InvalidateRect(hwnd, nullptr, FALSE);
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
                             L". " + stageHow(record);
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
        if (kind == Job::restore) {
            std::error_code missing;
            const auto written = std::filesystem::last_write_time(source, missing);
            restoredBefore = missing ? 0 : int64_t(std::chrono::duration_cast<std::chrono::seconds>(
                (written - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now()).time_since_epoch()).count());
        }
        if (kind == Job::deleteSaves)
            deletedBudokai = std::find(selectedGames.begin(), selectedGames.end(), std::string("BASLUS-20779DBZ2")) != selectedGames.end();
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
                               wide(settings.renderer) + onlineArguments +
                               (launchState ? L" --load-state=" + std::to_wstring(launchState) : std::wstring()) + L" " +
                               quote_argument(settings.iso.wstring());
        launchState = 0;
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
        const bool want = allowMotion() && (settings.launcherAccent == rgb_accent ||
            std::any_of(settings.profileStyles.begin(),settings.profileStyles.end(),[](const auto& style){return style.accent==rgb_accent;}));
        if (want != cycling) {
            cycling = want;
            if (want)
                SetTimer(hwnd, 2, rgb_frame_ms, nullptr);
            else
                KillTimer(hwnd, 2);
        }
    }
    void cycleFrame() {
        if (!cycling || dropdownOpen() || IsIconic(hwnd) || !appActive)
            return;
        wavePalette = waveStyle();
        waveTint = waveSkyTop();
        apply_theme(settings.launcherTheme, settings.launcherAccent, settings.launcherColors);
        const auto now = motionTicks();
        if (now - ambientAt >= 500u) {
            ambientAt = now;
            ambient.clear();
        }
        (pageDirty=true),InvalidateRect(hwnd, nullptr, FALSE);
        for (auto [id, control] : controls)
            if ((GetWindowLongPtrW(control, GWL_STYLE) & BS_TYPEMASK) == BS_OWNERDRAW && IsWindowVisible(control)) {
                wchar_t kind[16]{};
                if (GetClassNameW(control, kind, 16) && _wcsicmp(kind, L"Button") == 0)
                    InvalidateRect(control, nullptr, FALSE);
            }
    }
    void timer() {
        SYSTEMTIME local{};GetLocalTime(&local);const unsigned minute=local.wHour*60u+local.wMinute;
        if(minute!=displayedMinute) {if(waveStyle() && settings.waveSky==0 && minute%60==0 && displayedMinute!=~0u)restyle();displayedMinute=minute;RECT header{};GetClientRect(hwnd,&header);header.bottom=p(shellHeader);(pageDirty=true),InvalidateRect(hwnd,&header,FALSE);}
        presence.enable(settings.discordPresence);
        const char* pages[]={"Choosing a game","Setting up online play","Setting up a speedrun","Managing mods","Managing saves","Configuring settings","Checking for updates","Changing controls"};
        const int shown=std::clamp(navigationPage(),0,int(std::size(pages))-1);
        presence.update(running()?"Playing Budokai 2":"In launcher",running()?"In game":page==importPage?"Importing mods":pages[shown]);
        if(presence.status()!=lastPresenceStatus && !dropdownOpen()) {
            lastPresenceStatus=presence.status();(pageDirty=true),InvalidateRect(hwnd,nullptr,FALSE);
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
                    if (result.kind == Job::unlock) {
                        settings.achievementsPaused = true;
                        settings.achievementsPausedAt = int64_t(std::time(nullptr));
                        saveAchievementState();
                    } else if (settings.achievementsPaused && result.kind == Job::restore && restoredBefore < settings.achievementsPausedAt && restoredBefore > 0) {
                        settings.achievementsPaused = false;
                        saveAchievementState();
                    } else if (settings.achievementsPaused && result.kind == Job::deleteSaves && deletedBudokai) {
                        settings.achievementsPaused = false;
                        saveAchievementState();
                    }
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
                    if (activeJob == Job::launch && room && roomLaunched) {
                        room.reset();
                        hostingRoom.clear();
                        roomLaunched = false;
                    }
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
            summarizeGameLog(code);
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
                (installRequested || (appActive && !IsIconic(hwnd) && settings.autoUpdates && idle && (page == 0 || page == updatesPage)))) {
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
        if (busy() || (updating() && page == updatesPage)) (pageDirty=true),InvalidateRect(hwnd, nullptr, FALSE);
    }
    std::filesystem::path chooseFallback(bool folder, bool iso, const wchar_t *title, bool splitImage) {
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
        options.lpstrFilter = splitImage ? L"Images (*.png, *.jpg, *.jpeg, *.gif)\0*.png;*.jpg;*.jpeg;*.gif\0\0"
                              : iso     ? L"Disc images (*.iso)\0*.iso\0All files\0*.*\0\0"
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
    std::filesystem::path choose(bool folder, bool iso = false, const wchar_t *title = nullptr, bool splitImage = false) {
        picking = true;
        struct Reset {
            bool &flag;
            Window& window;
            ~Reset() { flag = false; window.queueUiRestore(); }
        } reset{picking,*this};
        IFileOpenDialog *dialog = nullptr;
        if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&dialog))))
            return chooseFallback(folder, iso, title, splitImage);
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
            const COMDLG_FILTERSPEC splitImages[] = {{L"Images (*.png, *.jpg, *.jpeg, *.gif)", L"*.png;*.jpg;*.jpeg;*.gif"}};
            const COMDLG_FILTERSPEC saves[] = {{L"Saves and backups (*.psu, *.zip)", L"*.psu;*.zip"},
                                               {L"All files", L"*.*"}};
            if (splitImage)
                dialog->SetFileTypes(1, splitImages);
            else if (iso || !title)
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
    std::filesystem::path chooseModPackage(bool saving, const std::wstring &suggested) {
        picking = true;
        struct Reset {
            bool &flag;
            Window &window;
            ~Reset() { flag = false; window.queueUiRestore(); }
        } reset{picking, *this};
        IFileDialog *dialog = nullptr;
        if (FAILED(CoCreateInstance(saving ? CLSID_FileSaveDialog : CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                    saving ? __uuidof(IFileSaveDialog) : __uuidof(IFileOpenDialog),
                                    reinterpret_cast<void **>(&dialog))))
            throw std::runtime_error("The Windows file picker could not open");
        struct Release {
            IFileDialog *dialog;
            ~Release() { dialog->Release(); }
        } release{dialog};
        DWORD flags = 0;
        dialog->GetOptions(&flags);
        dialog->SetOptions(flags | FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR | (saving ? FOS_OVERWRITEPROMPT : FOS_FILEMUSTEXIST));
        const COMDLG_FILTERSPEC types[] = {{L"Mod packages (*.okmod, *.zip)", L"*.okmod;*.zip"}, {L"All files", L"*.*"}};
        dialog->SetFileTypes(saving ? 1 : 2, types);
        dialog->SetFileTypeIndex(1);
        if (saving) {
            dialog->SetDefaultExtension(L"okmod");
            dialog->SetFileName(suggested.c_str());
        }
        dialog->SetTitle(saving ? L"Export the mod as a package" : L"Install a mod package");
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
    mutable std::string activeName, activeKey;
    mutable std::optional<online::Profile> activeValue;
    mutable bool heldTagChecked=false;
    mutable std::string heldTagId;
    mutable ULONGLONG heldTagTime=0;
    mutable std::optional<identity::Tag> heldTagValue;
    std::optional<online::Profile> activeProfile() const {
        for (size_t i = 0; i < settings.onlineProfiles.size() && i < settings.onlineKeys.size(); ++i)
            if (settings.onlineProfiles[i] == settings.onlineName) {
                if (activeValue && activeName == settings.onlineName && activeKey == settings.onlineKeys[i])
                    return activeValue;
                crypto::Seed seed{};
                if (!crypto::from_hex(settings.onlineKeys[i], seed.data(), seed.size()))
                    return std::nullopt;
                activeValue = online::make_profile(seed, settings.onlineName);
                activeName = settings.onlineName;
                activeKey = settings.onlineKeys[i];
                return activeValue;
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
        const auto source=controls[ProfileCopyFrom];SendMessageW(source,CB_RESETCONTENT,0,0);
        for(const auto& name:settings.onlineProfiles)SendMessageW(source,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(wide(name).c_str()));
        SendMessageW(source,CB_SETCURSEL,settings.onlineProfiles.size()>1 && chosen==0 ? 1 : 0,0);
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
        status = L"Add a profile on the Profile page before playing online.";
        error = true;
        openProfile();
        return false;
    }
    void addProfile() {
        auto name = utf8(trimmed(textOf(OnlineName)));
        if (name.empty() || name.size() > 24) {
            status = L"Type a profile name of up to 24 characters.";
            error = true;
            return;
        }
        if (const auto tag = identity::claimed_tag(name)) {
            if (!identity::tag_seed(*tag) && !installTagKey(*tag))
                return;
            name = identity::held_name(name);
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
            if (nameCheck)
                return;
            const auto seed = online::new_seed();
            const auto owner = online::make_profile(seed, name).id;
            status = L"Checking that " + wide(name) + L" is free\u2026";
            error = false;
            auto check = std::make_shared<NameCheck>();
            check->name = name;
            check->seed = seed;
            check->started = std::chrono::steady_clock::now();
            nameCheck = check;
            std::thread([check, owner, server = settings.onlineLobby, window = hwnd] {
                std::string problem;
                const auto state = online::lookup_name(server, check->name, owner, problem);
                {
                    std::lock_guard lock(check->mutex);
                    check->state = state;
                    check->done = true;
                }
                PostMessageW(window, onlineChanged, 0, 0);
            }).detach();
            refreshEnabled();
            (pageDirty=true),InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }
        useProfile(next, name);
    }
    bool installTagKey(identity::Tag tag) {
        const auto label = wide(identity::tag_label(tag));
        const std::wstring prompt = label + L" needs its name tag key. Choose the key file";
        const auto file = choose(false, false, prompt.c_str());
        if (file.empty()) {
            status = label + (tag == identity::Tag::developer ? L" is reserved for the OpenKai2 developer." : L" needs its name tag key.");
            error = true;
            return false;
        }
        const auto installed = identity::install_tag_key(file);
        heldTagChecked = false;
        if (installed != tag) {
            status = installed ? L"That key is for " + wide(identity::tag_label(*installed)) + L", not " + label + L". It was added." :
                                 L"That file is not an OpenKai2 name tag key.";
            error = true;
            return false;
        }
        status = label + L" key added. Any profile can now use the " + label + L" tag.";
        error = false;
        return true;
    }
    void renameProfile() {
        const auto profile = activeProfile();
        if (!profile) {
            status = L"Choose a profile to rename.";
            error = true;
            return;
        }
        const auto typed = utf8(trimmed(textOf(OnlineName)));
        if (typed.empty() || typed.size() > 24) {
            status = L"Type the new name, up to 24 characters, then choose Rename.";
            error = true;
            SetFocus(controls[OnlineName]);
            return;
        }
        if (const auto tag = identity::claimed_tag(typed); tag && !identity::tag_seed(*tag) && !installTagKey(*tag))
            return;
        const auto name = identity::renamed(profile->name, typed);
        if (name.size() > 24) {
            status = L"With its tag the name is longer than 24 characters. Choose a shorter name.";
            error = true;
            return;
        }
        if (name == profile->name) {
            status = identity::claimed_tag(name) == identity::Tag::developer ? L"The [DEV] profile keeps its name." : L"That is already this profile's name.";
            error = false;
            return;
        }
        const auto key = online::name_key(name);
        if (std::any_of(settings.onlineProfiles.begin(), settings.onlineProfiles.end(),
                        [&](const std::string &existing) { return existing != profile->name && online::name_key(existing) == key; })) {
            status = L"You already have a profile with that name.";
            error = true;
            return;
        }
        if (nameCheck)
            return;
        auto check = std::make_shared<NameCheck>();
        check->name = name;
        check->renameFrom = profile->name;
        check->seed = profile->seed;
        check->started = std::chrono::steady_clock::now();
        nameCheck = check;
        status = L"Checking that " + wide(name) + L" is free\u2026";
        error = false;
        std::thread([check, owner = profile->id, server = settings.onlineLobby, window = hwnd] {
            std::string problem;
            const auto state = online::lookup_name(server, check->name, owner, problem);
            {
                std::lock_guard lock(check->mutex);
                check->state = state;
                check->done = true;
            }
            PostMessageW(window, onlineChanged, 0, 0);
        }).detach();
        refreshEnabled();
        (pageDirty=true),InvalidateRect(hwnd, nullptr, FALSE);
    }
    void finishNameCheck() {
        if (!nameCheck)
            return;
        auto check = nameCheck;
        bool done = false;
        std::optional<online::NameState> state;
        {
            std::lock_guard lock(check->mutex);
            done = check->done;
            state = check->state;
        }
        if (!done && std::chrono::steady_clock::now() - check->started < std::chrono::seconds(12))
            return;
        nameCheck.reset();
        refreshEnabled();
        (pageDirty=true),InvalidateRect(hwnd, nullptr, FALSE);
        if (!state) {
            status = L"Could not reach the online service to check the name. Connect to the internet and try again.";
            error = true;
            return;
        }
        if (*state == online::NameState::taken) {
            status = wide(check->name) + L" is already used by another OpenKai2 profile. Profile names must be unique.";
            error = true;
            return;
        }
        Settings next = settings;
        const auto key = online::name_key(check->name);
        if (std::any_of(next.onlineProfiles.begin(), next.onlineProfiles.end(),
                        [&](const std::string &existing) { return existing != check->renameFrom && online::name_key(existing) == key; })) {
            status = L"You already have a profile with that name.";
            error = true;
            return;
        }
        if (!check->renameFrom.empty()) {
            const auto found = std::find(next.onlineProfiles.begin(), next.onlineProfiles.end(), check->renameFrom);
            if (found == next.onlineProfiles.end()) {
                status = L"That profile was removed before the rename finished.";
                error = true;
                return;
            }
            *found = check->name;
            if (next.onlineName == check->renameFrom)
                next.onlineName = check->name;
            write_settings(settings_path(), next);
            settings = std::move(next);
            SetWindowTextW(controls[OnlineName], L"");
            fillProfiles();
            reconnect();
            status = wide(check->renameFrom) + L" is now " + wide(check->name) + L". Friends, picture and stats stay with the profile.";
            error = false;
            refreshEnabled();
            return;
        }
        next.onlineProfiles.push_back(check->name);
        next.onlineKeys.resize(next.onlineProfiles.size() - 1);
        next.onlineKeys.push_back(crypto::to_hex(check->seed.data(), check->seed.size()));
        useProfile(next, check->name);
    }
    void useProfile(Settings &next, const std::string &name) {
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
    std::filesystem::path profileRecoveryPath() const {return settings_path().parent_path()/"profiles"/"recovery"/"last-removed.ini";}
    void recoverProfile() {
        auto next=settings;restore_profile(next,read_settings(profileRecoveryPath()));
        const auto name=next.onlineName;useProfile(next,name);
        std::error_code ignored;std::filesystem::remove(profileRecoveryPath(),ignored);
        status=L"Profile restored with its identity, friends and appearance.";if(page==profilePage)openProfile();layout();refreshEnabled();
    }
    void removeProfile() {
        if (settings.onlineProfiles.empty())
            return;
        const int chosen = std::clamp(selected(OnlineProfile), 0, int(settings.onlineProfiles.size()) - 1);
        Settings next = settings;
        const auto name = next.onlineProfiles[size_t(chosen)];
        if(MessageBoxW(hwnd,(L"Remove “"+wide(name)+L"” from this PC?\n\nIts online identity, friends and appearance can be recovered with Undo removal. Game saves are kept.").c_str(),L"Remove profile",MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2)!=IDYES)return;
        const auto identity=activeProfile();if(!identity)throw std::runtime_error("This profile has no identity");
        const auto recovery=profile_recovery(settings,size_t(chosen),identity->id);
        std::filesystem::create_directories(profileRecoveryPath().parent_path());
        write_settings(profileRecoveryPath(),recovery);
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
        next.profileStyles.erase(std::remove_if(next.profileStyles.begin(),next.profileStyles.end(),
                                                [&](const auto& style){return style.id==owner;}),next.profileStyles.end());
        next.onlineProfiles.erase(next.onlineProfiles.begin() + chosen);
        next.onlineName = next.onlineProfiles.empty() ? std::string("Player") : next.onlineProfiles.front();
        write_settings(settings_path(), next);
        settings = std::move(next);
        fillProfiles();
        reconnect();
        status = L"Profile " + wide(name) + L" removed. Undo removal restores it.";
        layout();
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
                publishCard();
            } else
                std::cout << "[launcher:online] could not connect: " << problem << std::endl;
            refreshOnlineLists();
            refreshEnabled();
            (pageDirty=true),InvalidateRect(hwnd, nullptr, FALSE);
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
                auto &queue = shared->notices;
                queue.erase(std::remove_if(queue.begin(), queue.end(),
                                           [&](const Notice &old) { return old.invite && old.sender() == invite.from_id; }),
                            queue.end());
                if (queue.size() < 20)
                    queue.push_back({true, invite, {}});
                if (shared->window)
                    PostMessageW(shared->window, onlineNotice, 0, 0);
            });
            opened->on_friend([shared](const online::FriendMessage &message) {
                std::lock_guard lock(shared->mutex);
                auto &queue = shared->notices;
                queue.erase(std::remove_if(queue.begin(), queue.end(),
                                           [&](const Notice &old) {
                                               return !old.invite && old.sender() == message.from_id &&
                                                      old.friendMessage.kind == message.kind;
                                           }),
                            queue.end());
                if (queue.size() < 40)
                    queue.push_back({false, {}, message});
                if (shared->window)
                    PostMessageW(shared->window, onlineNotice, 0, 0);
            });
            std::string problem;
            if (!opened->open(server, profile, application_version, problem))
                opened.reset();
            return std::make_pair(std::move(opened), problem);
        });
        (pageDirty=true),InvalidateRect(hwnd, nullptr, FALSE);
    }
    size_t playerCount() const { return directory ? directory->players().size() + 1 : 0; }
    int64_t friendSeen(const std::string &id) const {
        const auto profile = activeProfile();
        if (!profile)
            return 0;
        for (const auto &entry : settings.onlineFriends)
            if (entry.owner == profile->id && entry.id == id)
                return entry.seen;
        return 0;
    }
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
        std::string selectedRoom, selectedPlayer, roomAnchor, playerAnchor;
        const auto oldRoomTop=SendMessageW(controls[RoomList],LB_GETTOPINDEX,0,0);
        const auto oldPlayerTop=SendMessageW(controls[PlayerList],LB_GETTOPINDEX,0,0);
        if(oldRoomTop>=0 && size_t(oldRoomTop)<roomRows.size())roomAnchor=roomRows[size_t(oldRoomTop)].id;
        if(oldPlayerTop>=0 && size_t(oldPlayerTop)<playerRows.size())playerAnchor=playerRows[size_t(oldPlayerTop)].first;
        if (const auto row = SendMessageW(controls[RoomList], LB_GETCURSEL, 0, 0); row >= 0 && size_t(row) < roomRows.size())
            selectedRoom = roomRows[size_t(row)].id;
        if (const auto row = SendMessageW(controls[PlayerList], LB_GETCURSEL, 0, 0); row >= 0 && size_t(row) < playerRows.size())
            selectedPlayer = playerRows[size_t(row)].first;
        const auto query=utf8(trimmed(textOf(OnlineSearch)));
        availableRooms=directory ? directory->rooms() : std::vector<online::Room>{};
        roomRows = browse_rooms(availableRooms,application_version,settings.onlineRoomFilter,query);
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
        roomPings = pings;
        std::stable_sort(roomRows.begin(), roomRows.end(), [&](const online::Room &a, const online::Room &b) {
            auto rank = [&](const online::Room &room) {
                const auto found = pings.find(room.id);
                const int ping = found == pings.end() ? 5000 : found->second < 0 ? 4000 : found->second;
                return std::tuple(room.version != application_version, room.id != hostingRoom, ping);
            };
            return rank(a) < rank(b);
        });
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
        for(size_t i=0;i<roomRows.size();++i)
            if(roomRows[i].id==roomAnchor)SendMessageW(controls[RoomList],LB_SETTOPINDEX,i,0);
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
        if (profile && homeScope == 2) {
            for (const auto &player : players)
                if (player.id != profile->id && !isBlocked(player.id)) {
                    friendRows.emplace_back(player.id, player.name);
                    friendLabels.emplace_back(true, wide(player.name) + L" · " + wide(player.status));
                }
        } else if (profile)
            for (const auto &entry : homeScope == 3 ? settings.recentPlayers : settings.onlineFriends)
                if (entry.owner == profile->id) {
                    const auto found = std::find_if(players.begin(), players.end(), [&](const auto &player) { return player.id == entry.id; });
                    if(homeScope==1 && found==players.end())continue;
                    friendRows.emplace_back(entry.id, found == players.end() ? entry.name : found->name);
                    friendLabels.emplace_back(found != players.end(), wide(friendRows.back().second) + L" · " +
                                                  (found == players.end() ? std::wstring(L"offline") : wide(found->status)));
                }
        std::vector<size_t> order(friendRows.size());
        for (size_t i = 0; i < order.size(); ++i)
            order[i] = i;
        auto folded = [](const std::string &name) {
            std::string out = name;
            for (auto &c : out)
                c = char(std::tolower(static_cast<unsigned char>(c)));
            return out;
        };
        if (homeScope != 3)
            std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
                if (friendLabels[a].first != friendLabels[b].first)
                    return friendLabels[a].first > friendLabels[b].first;
                if (homeScope == 2 && isFriend(friendRows[a].first) != isFriend(friendRows[b].first))
                    return isFriend(friendRows[a].first);
                return folded(friendRows[a].second) < folded(friendRows[b].second);
            });
        {
            auto rows = friendRows;
            auto texts = friendLabels;
            for (size_t i = 0; i < order.size(); ++i) {
                friendRows[i] = rows[order[i]];
                friendLabels[i] = texts[order[i]];
            }
        }
        friendStatus.clear();
        for (const auto &row : friendRows) {
            const auto found = std::find_if(players.begin(), players.end(), [&](const auto &player) { return player.id == row.first; });
            friendStatus.push_back(found == players.end() ? std::string() : found->status.empty() ? std::string("online") : found->status);
        }
        std::vector<std::wstring> friendTexts;
        for (size_t i = 0; i < friendRows.size(); ++i)
            friendTexts.push_back((friendLabels[i].first ? L"On · " : L"Off · ") + friendLabels[i].second + L"\t" + wide(friendRows[i].first));
        if (friendTexts != shownFriendTexts || SendMessageW(controls[FriendList], LB_GETCOUNT, 0, 0) != LRESULT(friendRows.size())) {
            shownFriendTexts = friendTexts;
            const auto top = SendMessageW(controls[FriendList], LB_GETTOPINDEX, 0, 0);
            SendMessageW(controls[FriendList], WM_SETREDRAW, FALSE, 0);
            SendMessageW(controls[FriendList], LB_RESETCONTENT, 0, 0);
            for (size_t i = 0; i < friendRows.size(); ++i) {
                const auto label = friendTexts[i].substr(0, friendTexts[i].find(L'\t'));
                SendMessageW(controls[FriendList], LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
                if (friendRows[i].first == selectedFriend)
                    SendMessageW(controls[FriendList], LB_SETCURSEL, WPARAM(i), 0);
            }
            if (top > 0 && size_t(top) < friendRows.size())
                SendMessageW(controls[FriendList], LB_SETTOPINDEX, WPARAM(top), 0);
            SendMessageW(controls[FriendList], WM_SETREDRAW, TRUE, 0);
        }
        InvalidateRect(controls[FriendList], nullptr, FALSE);
        playerDetails.clear();
        auto others = browse_players(players,settings.onlinePlayerFilter!=0,query,[&](const auto& id){return isFriend(id);});
        std::stable_sort(others.begin(), others.end(), [&](const online::Player &a, const online::Player &b) {
            if ((a.version == application_version) != (b.version == application_version))
                return a.version == application_version;
            if (isBlocked(a.id) != isBlocked(b.id))
                return isBlocked(b.id);
            if (isFriend(a.id) != isFriend(b.id))
                return isFriend(a.id);
            if (a.impostor != b.impostor)
                return b.impostor;
            return folded(a.name) < folded(b.name);
        });
        for (const auto &player : others) {
            playerDetails.push_back(player);
            playerRows.emplace_back(player.id, player.name);
            labels.push_back(wide(player.name) + (player.impostor ? L" (name not verified)" : L"") + L" · " + wide(player.status) +
                             (player.version == application_version ? L"" : L" · version " + wide(player.version)));
        }
        std::vector<std::wstring> playerTexts;
        for (size_t i = 0; i < labels.size(); ++i)
            playerTexts.push_back(labels[i] + L"\t" + wide(playerRows[i].first));
        if (playerTexts != shownPlayerTexts || SendMessageW(controls[PlayerList], LB_GETCOUNT, 0, 0) != LRESULT(labels.size())) {
            shownPlayerTexts = playerTexts;
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
            for(size_t i=0;i<playerRows.size();++i)
                if(playerRows[i].first==playerAnchor)SendMessageW(controls[PlayerList],LB_SETTOPINDEX,i,0);
            SendMessageW(controls[PlayerList], WM_SETREDRAW, TRUE, 0);
        }
        InvalidateRect(controls[PlayerList], nullptr, FALSE);
        if (selectedPlayerId().empty() || !isFriend(selectedPlayerId()))
            SetWindowTextW(controls[AddFriend], L"Add friend");
        else
            SetWindowTextW(controls[AddFriend], L"Remove friend");
        layout();refreshEnabled();
        (pageDirty=true),InvalidateRect(hwnd, nullptr, FALSE);
    }
    std::string selectedPlayerId() const {
        if (page == playerPage)
            return viewedPlayer;
        const auto row = page==onlinePage ? SendMessageW(controls.at(PlayerList), LB_GETCURSEL, 0, 0) : -1;
        if (row >= 0 && size_t(row) < playerRows.size())
            return playerRows[size_t(row)].first;
        const auto chosen = page==0 ? SendMessageW(controls.at(FriendList), LB_GETCURSEL, 0, 0) : -1;
        return chosen >= 0 && size_t(chosen) < friendRows.size() ? friendRows[size_t(chosen)].first : std::string();
    }
    int noticeCount() const {
        std::lock_guard lock(onlineShared->mutex);
        return int(onlineShared->notices.size());
    }
    bool hasNotice() const {
        std::lock_guard lock(onlineShared->mutex);
        return !onlineShared->notices.empty();
    }
    struct NoticeView {
        bool invite = false, verified = false, friendly = false;
        std::wstring name, detail, age;
        std::string id;
        size_t waiting = 0;
    };
    static std::wstring ageText(std::chrono::steady_clock::time_point at) {
        const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - at).count();
        if (seconds < 60)
            return L"just now";
        if (seconds < 3600)
            return std::to_wstring(seconds / 60) + L" min ago";
        return std::to_wstring(seconds / 3600) + L" h ago";
    }
    std::optional<NoticeView> noticeView() const {
        std::lock_guard lock(onlineShared->mutex);
        if (onlineShared->notices.empty())
            return std::nullopt;
        const auto &notice = onlineShared->notices.front();
        NoticeView view;
        view.invite = notice.invite;
        view.id = notice.sender();
        const auto &name = notice.invite ? notice.inviteMessage.from_name : notice.friendMessage.from_name;
        view.name = wide(name);
        view.verified = directory && directory->name_state(name, view.id) == online::NameState::mine;
        view.friendly = isFriend(view.id);
        view.age = ageText(notice.at);
        view.waiting = onlineShared->notices.size() - 1;
        if (notice.invite) {
            const auto room = directory ? directory->room(notice.inviteMessage.room_id) : std::nullopt;
            view.detail = !room ? std::wstring(L"invited you to a room that has closed")
                          : room->version != application_version ? L"invited you, but uses OpenKai2 " + wide(room->version)
                          : room->name.empty() ? std::wstring(L"invited you to play versus")
                                               : L"invited you to " + wide(room->name);
        } else
            view.detail = L"wants to be your friend";
        return view;
    }
    std::wstring toastTitle,toastText;
    ULONGLONG toastAt=0;
    static constexpr ULONGLONG toastLife=6500;
    double toastPresence() const {
        if(!toastAt)return 0.0;
        const double age=double(motionTicks()-toastAt);
        if(age>=double(toastLife))return 0.0;
        const double in=std::min(1.0,age/280.0),out=std::min(1.0,(double(toastLife)-age)/420.0);
        return std::min(1.0-std::pow(1.0-in,3.0),out);
    }
    RECT toastRect() const {
        RECT client{};GetClientRect(hwnd,&client);
        const int width=p(360),height=p(78),top=p(page==wavePage ? 104 : shellHeader+12);
        const int slide=int((1.0-toastPresence())*p(48));
        return {client.right-p(margin)-width+slide,top,client.right-p(margin)+slide,top+height};
    }
    void showToast(const std::wstring& title,const std::wstring& message) {
        toastTitle=title;toastText=message;toastAt=motionTicks();
        SetTimer(hwnd,7,16,nullptr);
        RECT area=toastRect();area.left-=p(60);area.bottom+=p(8);InvalidateRect(hwnd,&area,FALSE);
    }
    void animateToast() {
        RECT client{};GetClientRect(hwnd,&client);
        RECT area{client.right-p(margin)-p(440),p(shellHeader),client.right,p(page==wavePage ? 104 : shellHeader+12)+p(92)};
        if(!toastPresence() && toastAt && motionTicks()-toastAt>=toastLife){toastAt=0;KillTimer(hwnd,7);}
        else {
            const double age=double(motionTicks()-toastAt);
            SetTimer(hwnd,7,age<300 || age>double(toastLife)-450 ? 16 : 250,nullptr);
        }
        InvalidateRect(hwnd,&area,FALSE);
    }
    void paintToast(HDC dc) {
        const double presence=toastPresence();
        if(presence<=0.01)return;
        const RECT r=toastRect();
        {
            Gdiplus::Graphics graphics(dc);
            graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            const Gdiplus::REAL x=Gdiplus::REAL(r.left),y=Gdiplus::REAL(r.top),w=Gdiplus::REAL(r.right-r.left),h=Gdiplus::REAL(r.bottom-r.top),radius=Gdiplus::REAL(p(12));
            Gdiplus::GraphicsPath card;
            card.AddArc(x,y,radius*2,radius*2,180,90);card.AddArc(x+w-radius*2,y,radius*2,radius*2,270,90);
            card.AddArc(x+w-radius*2,y+h-radius*2,radius*2,radius*2,0,90);card.AddArc(x,y+h-radius*2,radius*2,radius*2,90,90);card.CloseFigure();
            Gdiplus::SolidBrush shadow(Gdiplus::Color(BYTE(70*presence),0,0,0));
            Gdiplus::Matrix shift;shift.Translate(0,Gdiplus::REAL(p(4)));
            Gdiplus::GraphicsPath dropped;dropped.AddPath(&card,FALSE);dropped.Transform(&shift);
            graphics.FillPath(&shadow,&dropped);
            const COLORREF tint=waveStyle() ? mix(waveSkyTop(),RGB(255,255,255),30) : raised;
            Gdiplus::LinearGradientBrush face(Gdiplus::PointF(0,y),Gdiplus::PointF(0,y+h),Gdiplus::Color(BYTE(235*presence),GetRValue(tint),GetGValue(tint),GetBValue(tint)),
                Gdiplus::Color(BYTE(235*presence),GetRValue(tint)*3/4,GetGValue(tint)*3/4,GetBValue(tint)*3/4));
            graphics.FillPath(&face,&card);
            Gdiplus::SolidBrush accentBar(Gdiplus::Color(BYTE(255*presence),GetRValue(orange),GetGValue(orange),GetBValue(orange)));
            graphics.SetClip(&card);
            graphics.FillRectangle(&accentBar,x,y,Gdiplus::REAL(p(4)),h);
            const double life=std::clamp(double(motionTicks()-toastAt)/double(toastLife),0.0,1.0);
            Gdiplus::SolidBrush timer(Gdiplus::Color(BYTE(90*presence),255,236,200));
            graphics.FillRectangle(&timer,x,y+h-Gdiplus::REAL(std::max(1,p(2))),Gdiplus::REAL(w*(1.0-life)),Gdiplus::REAL(std::max(1,p(2))));
            graphics.ResetClip();
            Gdiplus::Pen edge(Gdiplus::Color(BYTE(80*presence),255,236,210),float(std::max(1,p(1))));
            graphics.DrawPath(&edge,&card);
        }
        if(presence<0.45)return;
        const RECT icon{r.left+p(16),r.top+p(19),r.left+p(56),r.top+p(59)};
        navigationIcon(dc,OpenNotifications,icon,orange,p(28));
        const int left=unscale(r.left)+66,top=unscale(r.top);
        text(dc,toastTitle,left,top+14,unscale(r.right-r.left)-80,24,fonts.strong,ink,DT_LEFT|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
        text(dc,toastText,left,top+40,unscale(r.right-r.left)-80,22,fonts.caption,muted,DT_LEFT|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
    }
    void notify(const std::wstring &title, const std::wstring &message) {
        const auto profile=activeProfile();const auto style=profile ? profileStyle(profile->id) : nullptr;
        if(!style || style->availability!=2)showToast(title,message);
        if(style && (!style->notifications || style->availability==2))return;
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
            queue.erase(std::remove_if(queue.begin(), queue.end(), [&](const Notice &notice) { return isBlocked(notice.sender()); }),
                        queue.end());
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
        InvalidateRect(controls[OnlineTab], nullptr, FALSE);
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
        InvalidateRect(controls[OnlineTab], nullptr, FALSE);
    }
    void toggleFriend() {
        const auto id = selectedPlayerId();
        if (id.empty() || !directory) {
            status = L"Choose a player.";
            error = true;
            return;
        }
        std::string name;
        for (const auto *rows : {&playerRows, &friendRows})
            for (const auto &row : *rows)
                if (name.empty() && row.first == id)
                    name = row.second;
        if (name.empty())
            name = knownName(id);
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
        finishNameCheck();
        ensureOnline();
        roomTick();
        const bool runningNow = running();
        if (directory && directory->connected()) {
            const auto profile=activeProfile();const auto style=profile ? profileStyle(profile->id) : nullptr;
            directory->set_status(profile_status(style ? style->availability : 0,runningNow,room!=nullptr,roomHost));
        }
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
                (pageDirty=true),InvalidateRect(hwnd, nullptr, FALSE);
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
        joinRoom(roomRows[size_t(row)]);
    }
    void joinRoom(online::Room room) {
        if (room.version != application_version) {
            status = L"This room uses OpenKai2 " + wide(room.version) + L". Both players need the same version.";
            error = true;
            return;
        }
        const auto password = textOf(JoinPassword);
        if (room.locked && password.empty()) {
            page=onlinePage;
            if(settings.onlineRoomFilter==2) {
                auto next=settings;next.onlineRoomFilter=0;write_settings(settings_path(),next);settings=std::move(next);
                SendMessageW(controls[RoomFilter],CB_SETCURSEL,0,0);
            }
            SetWindowTextW(controls[OnlineSearch],L"");refreshOnlineLists();
            for(size_t i=0;i<roomRows.size();++i)
                if(roomRows[i].id==room.id)SendMessageW(controls[RoomList],LB_SETCURSEL,i,0);
            layout();scrollPage(pageCanvasHeight()-pageVisibleHeight());refreshEnabled();
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
        launchState = 0;
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
        roomLaunched = false;
        room = std::make_unique<netplay::Room>(options);
        room->start();
        if (directory && directory->connected())
            directory->set_status(host ? "hosting" : "joining");
        status = host ? L"Opening your room\u2026" : L"Joining " + target + L"\u2026";
        error = false;
        refreshEnabled();
        (pageDirty=true),InvalidateRect(hwnd, nullptr, FALSE);
    }
    void closeRoom(const std::wstring &message) {
        lastRoomMessage.clear();
        room.reset();
        hostingRoom.clear();
        pendingInvitee.clear();
        if (directory && directory->connected())
            directory->set_status("online");
        status = message;
        error = false;
        refreshEnabled();
        (pageDirty=true),InvalidateRect(hwnd, nullptr, FALSE);
    }
    void roomTick() {
        if (!room || running())
            return;
        const auto state = room->status();
        std::wstring next = wide(state.message);
        if (state.message != lastRoomMessage) {
            lastRoomMessage = state.message;
            std::cout << "[launcher:online] room " << (roomHost ? "host" : "join") << ": " << state.message
                      << (state.relayed ? " (relay)" : "") << std::endl;
        }
        if (state.phase == netplay::RoomPhase::failed) {
            std::cout << "[launcher:online] room " << (roomHost ? "hosting" : "joining") << " failed: " << state.message
                      << std::endl;
            room.reset();
            hostingRoom.clear();
            status = next.empty() ? L"Could not connect." : next;
            error = true;
            refreshEnabled();
            (pageDirty=true),InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }
        if (state.phase == netplay::RoomPhase::ready && !roomLaunched) {
            roomLaunched = true;
            std::wstring arguments = L" --netplay=" +
                                     (roomHost ? L"host:" + std::to_wstring(state.port)
                                      : state.relayed ? L"join:127.0.0.1:" + std::to_wstring(state.local_port)
                                                      : L"join:" + wide(joinAddress) + L":" + std::to_wstring(roomPort)) +
                                     L" --netplay-delay=" + std::to_wstring(state.delay) + L" " +
                                     quote_argument(L"--netplay-name=" + wide(settings.onlineName)) + L" " +
                                     quote_argument(L"--netplay-password=" + wide(state.key)) + netplayStyle();
            onlineArguments = arguments;
            std::cout << "[launcher:online] room ready with " << state.opponent << " ping " << state.ping_ms
                      << " ms delay " << state.delay << (state.relayed ? " via relay" : " direct") << std::endl;
            recordOpponent(state.opponent);
            start(Job::launch, settings.iso);
            status = L"Connected to " + wide(state.opponent) + L" (" + std::to_wstring(state.ping_ms) +
                     L" ms). Starting the match\u2026";
            error = false;
            refreshEnabled();
            (pageDirty=true),InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }
        if (!next.empty() && next != status) {
            std::cout << "[launcher:online] " << state.message << std::endl;
            status = next;
            error = false;
            (pageDirty=true),InvalidateRect(hwnd, nullptr, FALSE);
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
        next.internalScale = selected(InternalScale) >= 4 ? 0u : unsigned(std::max(0, selected(InternalScale))) + 1u;
        next.hdTextures = unsigned(std::max(0, selected(HdTextures)));
        next.music = selected(Music) != 1;
        next.vsync = selected(Vsync) != 0;
        next.imageEffect = unsigned(std::clamp(selected(ImageEffect), 0, 5));
        next.brightness = unsigned(std::clamp(selected(Brightness), 0, 3));
        next.cpuDifficulty = unsigned(std::max(0, selected(CpuDifficulty)));
        next.autoBackups = selected(AutoBackup) != 0;
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
        if (page == settingsPage && preferences(next)!=preferences(settings)) previousSettings = settings;
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
    void syncOptions() {
        if(hoverHints)SendMessageW(hoverHints,TTM_ACTIVATE,settings.helpHints,0);
        const auto resolution = std::find(sizes.begin(), sizes.end(), std::make_pair(settings.width, settings.height));
        const auto rate = std::find(rates.begin(), rates.end(), settings.fps);
        const std::pair<int,int> choices[] = {
            {Renderer,settings.renderer=="gpu" ? 1 : settings.renderer=="vulkan" ? 2 : settings.renderer=="cpu" ? 3 : 0},
            {Resolution,int(resolution-sizes.begin())},{Fps,int(rate-rates.begin())},
            {WindowMode,int(settings.windowMode)},{Aspect,int(settings.aspect)},
            {Vsync,settings.vsync ? 1 : 0},{Audio,audioChoice(settings)},{ImageEffect,int(std::min(settings.imageEffect,5u))},{Brightness,int(std::min(settings.brightness,3u))},
            {Antialiasing,int(settings.antialiasing)},{Filtering,int(settings.textureFilter)},
            {Upscaler,int(settings.upscaler)},{InternalScale,settings.internalScale ? int(settings.internalScale)-1 : 4},
            {HdTextures,int(settings.hdTextures)},{Music,settings.music ? 0 : 1},
            {CpuDifficulty,int(settings.cpuDifficulty)},{FpsCounter,settings.fpsCounter ? 1 : 0},
            {RandomizerMode,settings.randomizer ? 1 : 0},{SpeedrunRandomizer,settings.randomizer ? 1 : 0},
            {LoadTimes,settings.ps2LoadTimes ? 1 : 0},{RunAhead,int(std::min(settings.runAhead,2u))},{LauncherTheme,int(settings.launcherTheme)},
            {LauncherAccent,int(settings.launcherAccent)},{StartPage,int(settings.startPage)},
            {DiscordActivity,settings.discordPresence ? 1 : 0},{LauncherMotion,settings.reducedMotion ? 1 : 0},{LauncherHints,settings.helpHints ? 0 : 1},{LauncherInterface,int(settings.launcherInterface)},{WaveSky,int(settings.waveSky)},{WaveEmblem,settings.waveEmblem ? 0 : 1},
            {LauncherClock,int(settings.launcherClock)},{WaveRibbons,galaxyIndex(settings.waveRibbons)}};
        for (auto [id,value] : choices) SendMessageW(controls[id],CB_SETCURSEL,value,0);
    }
    bool keyboardShortcut(int id) {
        const HWND focus = GetFocus();
        auto focused = [&](int control) { return controls.count(control) && controls.at(control) == focus; };
        if (id == IDOK) {
            if (page == finderPage && (focused(ActionSearch) || focused(ActionList)))
                openSelectedAction();
            else if (focused(SplitNameField))
                saveSplitName();
            else if (focused(OnlineName))
                addProfile();
            else if (focused(JoinPassword) && disc && !room)
                joinRoom();
            else if (focused(OnlineAddress) && disc && !room) {
                directJoin = true;
                startOnline(false);
            } else
                return false;
            (pageDirty=true),InvalidateRect(hwnd, nullptr, FALSE);
            return true;
        }
        if (id == IDCANCEL && !dropdownOpen() && navigateBack())
            return true;
        if (id == IDCANCEL && room) {
            closeRoom(roomHost ? L"Room closed." : L"Stopped joining.");
            return true;
        }
        return false;
    }
    void command(int id, int notification) {
        lastInput = std::chrono::steady_clock::now();
        if (const auto focus=GetFocus(); focus && IsChild(hwnd,focus)) lastFocusedControl=focus;
        if (closingDropdowns && (notification==CBN_SELCHANGE || notification==CBN_SELENDOK)) return;
        if ((id == IDOK || id == IDCANCEL) && keyboardShortcut(id))
            return;
        if(id==OpenHelp && notification==BN_CLICKED) {openHelp();return;}
        if(id==OpenWaveHome && notification==BN_CLICKED) {openWaveHome();return;}
        if(id==CopySetup && notification==BN_CLICKED) {copySetup();return;}
        if(id==OpenIsoFolder && notification==BN_CLICKED) {
            if(!settings.iso.empty()) {
                const auto result=reinterpret_cast<INT_PTR>(ShellExecuteW(hwnd,L"open",settings.iso.parent_path().c_str(),nullptr,nullptr,SW_SHOWNORMAL));
                error=result<=32;status=error?L"Could not open the saved ISO folder. Choose the ISO again with Ctrl+O.":L"ISO folder opened.";
            }
            return;
        }
        if((id==NextDockTab || id==PreviousDockTab) && notification==BN_CLICKED) {
            if(dropdownOpen())return;
            const auto tabs=visibleTabs();if(tabs.empty())return;
            const int next=(std::max(0,tabSlot(navigationPage()))+(id==NextDockTab?1:int(tabs.size())-1))%int(tabs.size());
            command(PlayTab+tabs[size_t(next)],BN_CLICKED);return;
        }
        if((id==PageTop || id==PageBottom) && notification==BN_CLICKED) {
            const auto focus=GetFocus();wchar_t kind[16]{};if(focus)GetClassNameW(focus,kind,16);
            if(_wcsicmp(kind,L"Edit")==0) {const auto at=id==PageTop?0:GetWindowTextLengthW(focus);SendMessageW(focus,EM_SETSEL,at,at);}
            else scrollPage(id==PageTop?0:pageCanvasHeight());
            return;
        }
        if(id==SaveCurrent && notification==BN_CLICKED) {
            if(page==profilePage)command(ProfileSave,BN_CLICKED);
            else if(page==overlayPage)saveSplitName();
            else {status=page==settingsPage?L"Settings are saved automatically.":L"There are no unsaved profile edits on this page.";error=false;}
            return;
        }
        if(id==UndoCurrent && notification==BN_CLICKED) {
            wchar_t kind[16]{};const auto focus=GetFocus();if(focus)GetClassNameW(focus,kind,16);
            if(_wcsicmp(kind,L"Edit")==0)SendMessageW(focus,EM_UNDO,0,0);
            else if(page==settingsPage && previousSettings)command(UndoSettings,BN_CLICKED);
            return;
        }
        if(id==ToggleLauncher && notification==BN_CLICKED) {toggleFullscreen();return;}
        if (id == FindAction && notification == BN_CLICKED) { openActionFinder(); return; }
        if(id==OpenNotifications && notification==BN_CLICKED) {page=notificationsPage;layout();(pageDirty=true),InvalidateRect(hwnd,nullptr,FALSE);return;}
        if(id==OpenAchievements && notification==BN_CLICKED) {openAchievements();return;}
        if(id==RaSignIn && notification==BN_CLICKED) {signInRetro();return;}
        if(id==RaSignOut && notification==BN_CLICKED) {
            settings.raToken.clear();retroSet={};retroChecked=false;retroStatus.clear();
            try{std::filesystem::remove(retro::cache_path());}catch(const std::exception&){}
            saveAchievementState();status=L"Signed out of RetroAchievements.";error=false;
            layout();refreshEnabled();checkRetro();return;
        }
        if(id==RaRefresh && notification==BN_CLICKED) {retroChecked=false;checkRetro();return;}
        if(id==RaCopyHash && notification==BN_CLICKED) {
            if(!retroHash.empty() && copyText(wide(retroHash))){status=L"Disc hash copied. Share it when asking RetroAchievements to link this disc.";error=false;}
            (pageDirty=true),InvalidateRect(hwnd,nullptr,FALSE);return;
        }
        if(id==RaOpenSet && notification==BN_CLICKED) {ShellExecuteW(hwnd,L"open",wide(retro::set_page).c_str(),nullptr,nullptr,SW_SHOWNORMAL);return;}
        if(id==RaOpenSite && notification==BN_CLICKED) {ShellExecuteW(hwnd,L"open",wide(retro::site).c_str(),nullptr,nullptr,SW_SHOWNORMAL);return;}
        if(id==RaHardcore && notification==CBN_SELCHANGE) {
            settings.raHardcore=selected(RaHardcore)==1;saveAchievementState();
            status=settings.raHardcore ? L"Hardcore on: save states, mods, CPU difficulty and the randomizer are off while you play." : L"Softcore on: save states and mods are allowed.";
            error=false;(pageDirty=true),InvalidateRect(hwnd,nullptr,FALSE);return;
        }
        if (id == ActionSearch && notification == EN_CHANGE) { refreshActions(); return; }
        if ((id == ActionOpen && notification == BN_CLICKED) || (id == ActionList && notification == LBN_DBLCLK)) {
            openSelectedAction(); return;
        }
        if ((id == ActionClose || id == BackToPlay) && notification == BN_CLICKED) { navigateBack(); return; }
        if (id == SaveSearch && notification == EN_CHANGE) {
            if (!busy()) { refreshBackups(); layout(); (pageDirty=true),InvalidateRect(hwnd, nullptr, FALSE); } return;
        }
        if (id == ClearSaveSearch && notification == BN_CLICKED) { SetWindowTextW(controls[SaveSearch], L""); SetFocus(controls[SaveSearch]); return; }
        if (id == SettingsSearch && notification == EN_CHANGE) {
            pageScroll = 0; layout(); (pageDirty=true),InvalidateRect(hwnd, nullptr, FALSE);
            for (int chip = SettingsAll; chip <= SettingsLauncher; ++chip)
                InvalidateRect(controls[chip], nullptr, FALSE);
            return;
        }
        if (id == ClearSettingsSearch && notification == BN_CLICKED) { SetWindowTextW(controls[SettingsSearch], L""); SetFocus(controls[SettingsSearch]); return; }
        if (id>=SettingsAll && id<=SettingsLauncher && notification==BN_CLICKED) {
            const wchar_t* filters[]={L"",L"display",L"graphics",L"game",L"launcher"};
            page=settingsPage;SetWindowTextW(controls[SettingsSearch],filters[id-SettingsAll]);
            pageScroll=0;layout();(pageDirty=true),InvalidateRect(hwnd,nullptr,FALSE);return;
        }
        if (id == UndoSettings && notification == BN_CLICKED && previousSettings) {
            auto next = settings;
            preferences(next) = preferences(*previousSettings);
            write_settings(settings_path(), next);
            settings = std::move(next); previousSettings.reset();
            syncOptions(); restyle();
            status = L"Last settings change undone."; error = false; return;
        }
        if ((id == ResetDisplay || id == ResetGame) && notification == BN_CLICKED) {
            const std::vector<std::pair<int,int>> choices = id == ResetDisplay ?
                std::vector<std::pair<int,int>>{{WindowMode,0},{Resolution,0},{Aspect,0},{Fps,6},{Vsync,1},{Audio,1}} :
                std::vector<std::pair<int,int>>{{CpuDifficulty,0},{HdTextures,0},{Music,0},{FpsCounter,0},{RandomizerMode,0},{LoadTimes,1},{RunAhead,0}};
            for (auto [control, value] : choices) SendMessageW(controls[control], CB_SETCURSEL, value, 0);
            saveOptions();
            if (id == ResetGame) { settings.randomizer=false; settings.ps2LoadTimes=true; settings.runAhead=0;
                SendMessageW(controls[SpeedrunRandomizer],CB_SETCURSEL,0,0); write_settings(settings_path(),settings); }
            status = id == ResetDisplay ? L"Display defaults restored. Undo is available." : L"Game defaults restored. Undo is available.";
            layout(); (pageDirty=true),InvalidateRect(hwnd,nullptr,FALSE); return;
        }
        if (const auto found = controls.find(id); found != controls.end() &&
            (notification == CBN_DROPDOWN || notification == CBN_CLOSEUP) &&
            (GetWindowLongPtrW(found->second, GWL_STYLE) & CBS_DROPDOWNLIST) == CBS_DROPDOWNLIST) {
            InvalidateRect(found->second, nullptr, FALSE);
        }
        if (picking)
            return;
        if (const auto found = controls.find(id); found != controls.end()) {
            wchar_t type[32]{};
            if (GetClassNameW(found->second, type, 32) && _wcsicmp(type, L"ComboBox") == 0) {
                if (notification == CBN_DROPDOWN)
                    dropdownOriginal[id] = SendMessageW(found->second, CB_GETCURSEL, 0, 0);
                if (notification == CBN_SELENDCANCEL || notification == CBN_CLOSEUP) {
                    if (const auto original = dropdownOriginal.find(id); original != dropdownOriginal.end()) {
                        SendMessageW(found->second, CB_SETCURSEL, original->second, 0);
                        InvalidateRect(found->second, nullptr, FALSE);
                        if (notification == CBN_CLOSEUP)
                            dropdownOriginal.erase(original);
                    }
                    if (notification == CBN_SELENDCANCEL)
                        return;
                }
                if (notification == CBN_SELCHANGE && dropdownOriginal.count(id))return;
                if (notification == CBN_SELENDOK) {
                    const auto foreground=GetForegroundWindow();
                    if(!appActive || !foreground || GetAncestor(foreground,GA_ROOTOWNER)!=hwnd) {
                        suspendUi();return;
                    }
                    dropdownOriginal.erase(id);
                    notification = CBN_SELCHANGE;
                }
            }
        }
        if (id == SaveSort && notification == CBN_SELCHANGE) {
            if (!busy()) { refreshBackups(); layout(); (pageDirty=true),InvalidateRect(hwnd, nullptr, FALSE); } return;
        }
        if (id == SplitStyleList && notification == LBN_SELCHANGE) {selectStyleRow();return;}
        if (id == SplitNameSave && notification == BN_CLICKED) {saveSplitName();return;}
        if (id == SplitStyleAdd && notification == BN_CLICKED && settings.speedrun && styleRowCount() < 256) {
            styleSelection = int(styleRowCount());
            storeSplitStyle({splitKey(),unsigned(styleSelection),{},{},{}},L"Split row added. Give it a name or choose your own image.");
            scrollPage(std::max(pageScroll,589-pageVisibleHeight()));
            SetFocus(controls[SplitNameField]);
            return;
        }
        if (id == SplitImageChoose && notification == BN_CLICKED && settings.speedrun) {
            attachSplitImage(choose(false,false,L"Choose a split image",true));return;
        }
        if (id == SplitImageRemove && notification == BN_CLICKED && settings.speedrun) {
            auto style=editedStyle();style.image.clear();style.imageName.clear();
            storeSplitStyle(std::move(style),L"Split image removed. The original file was kept.");return;
        }
        if (id == SplitStyleReset && notification == BN_CLICKED && settings.speedrun) {
            auto next=settings;
            const auto key=splitKey();
            next.splitStyles.erase(std::remove_if(next.splitStyles.begin(),next.splitStyles.end(),[&](const auto& style) {
                return style.run==key && style.index==unsigned(styleSelection);
            }),next.splitStyles.end());
            if (!legacySplitName(size_t(styleSelection)).empty())
                next.splitStyles.push_back({key,unsigned(styleSelection),{},{},{}});
            write_settings(settings_path(),next);settings=std::move(next);
            refreshSplits();refreshStyleRows();status=L"Split appearance reset. Recorded times are unchanged.";error=false;
            return;
        }
        if (id == NavigateBack && notification == BN_CLICKED) {
            navigateBack();
            return;
        }
        if (notification==BN_CLICKED && id==ProfileImageChoose) {
            attachProfileImage(choose(false,false,L"Choose a profile picture",true));return;
        }
        if (notification==BN_CLICKED && id==ProfileImageRemove) {
            saveProfileImage({},{});return;
        }
        if (notification==BN_CLICKED && id==HomeProfile) {openProfile();return;}
        if((id==ProfileFrame || id==ProfileBackdrop || id==ProfileTitle || id==ProfileFrameColor || id==ProfileFeatured) && notification==CBN_SELCHANGE) {
            if(id==ProfileFeatured && !featuredUnlocked(unsigned(std::max(0,selected(ProfileFeatured))),achievementMask(),holdsDevKey())) {
                const auto profile=activeProfile();const auto style=profile ? profileStyle(profile->id) : nullptr;
                status=L"Unlock that achievement to feature it on your card.";error=true;
                SendMessageW(controls[ProfileFeatured],CB_SETCURSEL,style ? style->featured : 0u,0);
            } else if(id==ProfileTitle) {
                const auto profile=activeProfile();const auto style=profile ? profileStyle(profile->id) : nullptr;
                const unsigned chosen=unsigned(std::max(0,selected(ProfileTitle)));
                if(!titleUnlocked(chosen,style ? style->matches : 0u,achievementMask(),holdsDevKey())) {
                    const auto& choice=title_choices[std::min(chosen,title_count-1)];
                    status=choice.achievement==developer_title ? std::wstring(L"Only profiles with the developer key can use this title.") : choice.achievement>=0 ? L"Earn "+wide(achievements::definitions[size_t(choice.achievement)].title)+L" to use this title."
                                                 : L"Reach level "+std::to_wstring(choice.level)+L" to use this title.";
                    error=true;
                    SendMessageW(controls[ProfileTitle],CB_SETCURSEL,style ? style->title : 0u,0);
                } else {status=L"Save your profile to show it on your card.";error=false;}
            } else {status=L"Save your profile to show it on your card.";error=false;}
            (pageDirty=true),InvalidateRect(hwnd,nullptr,FALSE);return;
        }
        if(id==ProfileNameColor && notification==CBN_SELCHANGE) {
            int index=selected(ProfileNameColor);
            if(index==name_color_custom) {
                static COLORREF recent[16]{};
                CHOOSECOLORW choice{};choice.lStructSize=sizeof(choice);choice.hwndOwner=hwnd;choice.rgbResult=shownColor(customNameColor);
                choice.lpCustColors=recent;choice.Flags=CC_FULLOPEN|CC_RGBINIT;
                if(ChooseColorW(&choice))customNameColor=storedColor(choice.rgbResult);
                else index=nameColorSelection;
                labelCustomColor(index);SendMessageW(controls[ProfileNameColor],CB_SETCURSEL,index,0);
            }
            nameColorSelection=index;(pageDirty=true),InvalidateRect(hwnd,nullptr,FALSE);return;
        }
        if((id==ProfileFighter || id==ProfileSharing) && notification==CBN_SELCHANGE) {(pageDirty=true),InvalidateRect(hwnd,nullptr,FALSE);return;}
        if(id==ProfileFit && notification==CBN_SELCHANGE) {
            const auto profile=activeProfile();if(!profile)return;
            Settings next=settings;
            auto found=std::find_if(next.profileStyles.begin(),next.profileStyles.end(),[&](const auto& style){return style.id==profile->id;});
            if(found==next.profileStyles.end()){next.profileStyles.push_back({profile->id,{},{},0,{},{}});found=std::prev(next.profileStyles.end());}
            found->pictureFit=unsigned(std::clamp(selected(ProfileFit),0,int(speedrun::framing_modes)-1));
            try{write_settings(settings_path(),next);settings=std::move(next);}catch(const std::exception& problem){status=wide(problem.what());error=true;return;}
            publishCard();status=L"Picture framing updated.";error=false;
            (pageDirty=true),InvalidateRect(hwnd,nullptr,FALSE);return;
        }
        if(notification==BN_CLICKED && id==ProfileManage) {openProfile();focusControl(OnlineName);return;}
        if(notification==BN_CLICKED && id==ProfileRecover) {recoverProfile();return;}
        if(notification==BN_CLICKED && id==ProfileCopyStyle) {
            const int index=selected(ProfileCopyFrom);const auto target=activeProfile();
            if(!target || index<0 || size_t(index)>=settings.onlineKeys.size())return;
            crypto::Seed seed{};if(!crypto::from_hex(settings.onlineKeys[size_t(index)],seed.data(),seed.size()))return;
            const auto source=online::make_profile(seed,settings.onlineProfiles[size_t(index)]);
            if(source.id==target->id){status=L"Choose a different profile to copy from.";error=false;return;}
            const auto from=profileStyle(source.id),own=profileStyle(target->id);
            auto copied=from ? *from : Settings::ProfileStyle{};copied.id=target->id;
            copied.notifications=own ? own->notifications : true;copied.availability=own ? own->availability : 0;copied.friendAlerts=own ? own->friendAlerts : true;
            copied.matches=own ? own->matches : 0;copied.since=own ? own->since : 0;
            Settings next=settings;
            next.profileStyles.erase(std::remove_if(next.profileStyles.begin(),next.profileStyles.end(),[&](const auto& style){return style.id==target->id;}),next.profileStyles.end());
            next.profileStyles.push_back(std::move(copied));write_settings(settings_path(),next);settings=std::move(next);
            cycleTimer();openProfile();publishCard();status=L"Appearance copied from "+wide(source.name)+L".";error=false;return;
        }
        if (notification==BN_CLICKED && (id==ProfileSave || id==ProfileReset || id==ProfileCopyId)) {
            const auto profile=activeProfile();if(!profile)return;
            if(id==ProfileCopyId) {
                const auto value=wide(profile->id);bool copied=false;
                if(OpenClipboard(hwnd)) {
                    const size_t bytes=(value.size()+1)*sizeof(wchar_t);
                    if(auto memory=GlobalAlloc(GMEM_MOVEABLE,bytes)) {
                        if(auto target=GlobalLock(memory)) {
                            std::memcpy(target,value.c_str(),bytes);GlobalUnlock(memory);EmptyClipboard();
                            copied=SetClipboardData(CF_UNICODETEXT,memory)!=nullptr;
                        }
                        if(!copied)GlobalFree(memory);
                    }
                    CloseClipboard();
                }
                status=copied ? L"Public profile ID copied." : L"Could not access the clipboard.";error=!copied;
            } else {
                Settings next=settings;
                next.profileStyles.erase(std::remove_if(next.profileStyles.begin(),next.profileStyles.end(),[&](const auto& s){return s.id==profile->id;}),next.profileStyles.end());
                if(id==ProfileSave) {
                    const auto old=profileStyle(profile->id);
                    Settings::ProfileStyle updated=old ? *old : Settings::ProfileStyle{};
                    updated.id=profile->id;updated.initials=utf8(trimmed(textOf(ProfileInitials)));updated.tagline=utf8(trimmed(textOf(ProfileTagline)));
                    updated.accent=unsigned(std::max(0,selected(ProfileAccent)));
                    updated.availability=unsigned(std::max(0,selected(ProfileAvailability)));
                    updated.pictureFit=unsigned(std::max(0,selected(ProfileFit)));updated.notifications=selected(ProfileNotifications)!=1;
                    updated.nameColor=chosenNameColor();updated.fighter=unsigned(std::max(0,selected(ProfileFighter)));updated.sharing=unsigned(std::max(0,selected(ProfileSharing)));
                    updated.friendAlerts=selected(ProfileFriendAlerts)!=1;updated.nameEffect=chosenEffect();
                    if(heldTag()) {
                        const auto link=utf8(trimmed(textOf(ProfileLink)));
                        if(!link.empty() && !online::valid_channel_link(link)) {
                            status=L"Channel links must start with https://www.twitch.tv/, https://www.youtube.com/, https://youtu.be/ or https://kick.com/.";
                            error=true;return;
                        }
                        updated.link=link;
                    }
                    updated.about=utf8(trimmed(textOf(ProfileAbout)));updated.region=unsigned(std::max(0,selected(ProfileRegion)));updated.playstyle=unsigned(std::max(0,selected(ProfilePlaystyle)));
                    applyCardLook(updated);
                    next.profileStyles.push_back(std::move(updated));
                } else if (const auto old=profileStyle(profile->id)) {
                    Settings::ProfileStyle kept{};kept.id=profile->id;kept.matches=old->matches;kept.since=old->since;
                    next.profileStyles.push_back(std::move(kept));
                }
                write_settings(settings_path(),next);settings=std::move(next);cycleTimer();openProfile();publishCard();
                const auto saved=profileStyle(profile->id);
                status=id!=ProfileSave ? L"Profile appearance reset." : saved && saved->sharing==2 ? L"Profile saved. Your card stays on this PC." :
                       directory && directory->connected() ? L"Profile saved. Players online now see your card." : L"Profile saved. Players will see your card when you go online.";
                error=false;
            }
            (pageDirty=true),InvalidateRect(hwnd,nullptr,FALSE);return;
        }
        if (notification==BN_CLICKED && id>=HomeControls && id<=HomeArrange) {
            const int target=id==HomeControls ? controlsPage :
                             id==HomeSaves ? savesPage : id==HomeArrange ? tabsPage : settingsPage;
            page=target;
            if(page==tabsPage)refreshTabs();
            layout();(pageDirty=true),InvalidateRect(hwnd,nullptr,FALSE);return;
        }
        if (id >= ControlKey && id < ControlKey + int(input::button_count) && notification == BN_CLICKED) {
            beginCapture(id - ControlKey);
            return;
        }
        if (id >= ControlAlternate && id < ControlAlternate + int(input::button_count) && notification == BN_CLICKED) {
            beginCapture(int(input::button_count) + id - ControlAlternate);
            return;
        }
        if (id >= ControlPad && id < ControlPad + int(input::button_count) && notification == CBN_SELCHANGE) {
            capturing = -1;
            choosePadButton(unsigned(id - ControlPad));
            return;
        }
        if (id == ControlsReset && notification == BN_CLICKED) {
            capturing = -1;
            saveControls(input::Bindings{}, L"Controls reset to the defaults.");
            return;
        }
        if (id == PlayTab && waveStyle()) {
            if(dropdownOpen())suspendUi();
            openWaveHome();
            return;
        }
        if (id >= PlayTab && id <= ControlsTab) {
            if(dropdownOpen())suspendUi();
            if (page == id - PlayTab) {
                SetFocus(controls[id]);
                return;
            }
            capturing = -1;
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
            layout();
            (pageDirty=true),InvalidateRect(hwnd, nullptr, FALSE);
            SetFocus(waveStyle() ? hwnd : controls[id]);
            return;
        }
        if (id == SaveListKind && notification == CBN_SELCHANGE) {
            SendMessageW(controls[SaveSort],CB_SETCURSEL,gameSaveList() ? 2 : 0,0);
            refreshBackups();
            layout();
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
        if (((id >= Renderer && id <= Music) || (id >= SpeedrunMode && id <= FpsCounter) || id == SpeedrunCpu || id == ImageEffect || id == Brightness) &&
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
                if(page==overlayPage)refreshStyleRows(0);
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
                status = settings.internalScale == 0
                             ? std::wstring(L"Internal resolution: automatic. 3D scenes render to match your display, up to 4x, "
                                            L"and step down by themselves if the graphics card can't hold 60 FPS.")
                             : settings.internalScale > 1
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
        if(id==HomeFriendScope && notification==CBN_SELCHANGE) {
            homeScope=std::max(0,selected(HomeFriendScope));
            if(settings.homeFriendScope!=unsigned(homeScope)){Settings next=settings;next.homeFriendScope=unsigned(homeScope);
                try{write_settings(settings_path(),next);settings=std::move(next);}catch(const std::exception&){}}
            refreshOnlineLists();layout();(pageDirty=true),InvalidateRect(hwnd,nullptr,FALSE);return;
        }
        if(notification==BN_CLICKED && (id==HomeFindPlayers || id==HomeCreateRoom)) {
            page=onlinePage;layout();SetFocus(controls[id==HomeCreateRoom ? OnlineRoom : OnlineSearch]);return;
        }
        if(id==HomeInvite && notification==BN_CLICKED) {invitePlayer();return;}
        if(id==OnlineSearch && notification==EN_CHANGE) {refreshOnlineLists();return;}
        if(id==ClearOnlineSearch && notification==BN_CLICKED) {
            SetWindowTextW(controls[OnlineSearch],L"");SetFocus(controls[OnlineSearch]);return;
        }
        if(id==FocusOnlineSearch && notification==BN_CLICKED) {
            if(page==settingsPage) {focusControl(SettingsSearch);return;}
            if(page==savesPage) {focusControl(SaveSearch);return;}
            if(page==finderPage) {focusControl(ActionSearch);return;}
            if(page==0 || page==onlinePage) {focusControl(OnlineSearch);return;}
            openActionFinder();return;
        }
        if((id==RoomFilter || id==PlayerFilter) && notification==CBN_SELCHANGE) {
            auto next=settings;next.onlineRoomFilter=unsigned(std::max(0,selected(RoomFilter)));
            next.onlinePlayerFilter=unsigned(std::max(0,selected(PlayerFilter)));
            write_settings(settings_path(),next);settings=std::move(next);refreshOnlineLists();return;
        }
        if ((id == OnlineDelay || id == OnlineListed || id == OnlineProfile) && notification == CBN_SELCHANGE) {
            if(id==OnlineProfile && (busy() || running() || room)) {fillProfiles();return;}
            const std::string before = settings.onlineName;
            if (saveOnline())
                status = id == OnlineDelay    ? (settings.onlineDelay ? L"Input delay saved. It is used when you host."
                                                                      : L"Input delay is picked from the ping when someone joins.")
                         : id == OnlineListed ? (settings.onlineVisibility == 0   ? L"Your rooms are shown to everyone."
                                                 : settings.onlineVisibility == 1 ? L"Only players you invite can find your rooms."
                                                                                  : L"Your rooms are not listed. Share your address instead.")
                                              : L"Profile " + wide(settings.onlineName) + L" selected.";
            if (settings.onlineName != before) {
                SendMessageW(controls[PlayerList],LB_SETCURSEL,WPARAM(-1),0);
                SendMessageW(controls[FriendList],LB_SETCURSEL,WPARAM(-1),0);
                reconnect();
                if(page==profilePage)openProfile();
            }
            refreshEnabled();
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
        if (id == ProfileRename && notification == BN_CLICKED) {
            renameProfile();
            return;
        }
        if (id == ProfileRemove && notification == BN_CLICKED) {
            removeProfile();
            return;
        }
        if (id == RefreshRooms && (notification == BN_CLICKED || notification == 1)) {
            if(!activeProfile()) {
                status=L"Create a profile to connect to the online service.";error=false;
                openProfile();return;
            }
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
        if (notification == BN_CLICKED && id == PlayerProfile) {
            const auto chosen = selectedPlayerId();
            openPlayer(chosen, knownName(chosen));
            return;
        }
        if (notification == BN_CLICKED && id == ProfilePreview) {
            if (const auto profile = activeProfile()) openPlayer(profile->id, profile->name);
            return;
        }
        if (notification == BN_CLICKED && id == PlayerChannel && page == playerPage) {
            const auto link = channelOf(viewedPlayer);
            if (online::valid_channel_link(link)) {
                const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(hwnd, L"open", wide(link).c_str(), nullptr, nullptr, SW_SHOWNORMAL));
                status = result > 32 ? L"Opening " + wide(link) : std::wstring(L"Could not open the channel link.");
                error = result <= 32;
            }
            return;
        }
        if (notification == BN_CLICKED && page == playerPage && (id == PlayerAction || id == PlayerFriend || id == PlayerBlock || id == PlayerCopyId)) {
            const auto profile = activeProfile();
            const bool own = profile && profile->id == viewedPlayer;
            if (id == PlayerAction) {
                const auto player = directory ? directory->player(viewedPlayer) : std::nullopt;
                if (own) openProfile();
                else if (const auto hosted = player ? roomHostedBy(player->name) : std::nullopt) joinRoom(*hosted);
                else invitePlayer();
            } else if (id == PlayerFriend) toggleFriend();
            else if (id == PlayerBlock) setBlocked(viewedPlayer, viewedName, !isBlocked(viewedPlayer));
            else {status = copyText(wide(viewedPlayer)) ? L"Profile ID copied." : L"Could not access the clipboard.";error = false;}
            refreshPlayerButtons();refreshEnabled();(pageDirty = true), InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }
        if (id == RoomList && notification == LBN_SELCHANGE) {
            SetWindowTextW(controls[JoinPassword],L"");
            layout();refreshEnabled();
            (pageDirty=true),InvalidateRect(hwnd, nullptr, FALSE);
            if (selectedRoomLocked())
                SetFocus(controls[JoinPassword]);
            return;
        }
        if ((id == PlayerList || id == FriendList) && notification == LBN_SELCHANGE) {
            SendMessageW(controls[id == PlayerList ? FriendList : PlayerList], LB_SETCURSEL, WPARAM(-1), 0);
            SetWindowTextW(controls[AddFriend], isFriend(selectedPlayerId()) ? L"Remove friend" : L"Add friend");
            refreshEnabled();return;
        }
        if (id == FriendList && notification == LBN_DBLCLK) {
            const auto chosen = SendMessageW(controls[FriendList], LB_GETCURSEL, 0, 0);
            const auto hosted = chosen >= 0 && size_t(chosen) < friendRows.size() && directory &&
                                        directory->player(friendRows[size_t(chosen)].first)
                                    ? roomHostedBy(friendRows[size_t(chosen)].second)
                                    : std::nullopt;
            if (disc && hosted && !room) {
                SetWindowTextW(controls[JoinPassword],L"");
                joinRoom(*hosted);
            } else if (disc)
                invitePlayer();
            else
                status = L"Choose your Budokai 2 ISO first.", error = true;
            (pageDirty=true),InvalidateRect(hwnd, nullptr, FALSE);
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
            previousSettings = settings;
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
            next.internalScale = defaults.internalScale; next.hdTextures = defaults.hdTextures; next.imageEffect = defaults.imageEffect; next.brightness = defaults.brightness;
            write_settings(settings_path(), next);
            previousSettings = settings;
            settings = std::move(next);
            const std::pair<int, int> choices[] = {{Renderer, 0}, {Fps, 6}, {Resolution, 0},
                {WindowMode, 0}, {Antialiasing, 0}, {Filtering, 0}, {Upscaler, 1}, {Vsync, 1}, {Aspect, 0}, {InternalScale, 4}, {HdTextures, 0}, {ImageEffect, 0}, {Brightness, 1}};
            for (auto [control, value] : choices) SendMessageW(controls[control], CB_SETCURSEL, value, 0);
            status = L"Display and graphics settings restored. Your disc, saves and update choices were kept.";
            error = false;
            refreshEnabled();
            return;
        }
        if (id == ReleaseNotes) {
            const auto address = L"https://github.com/7v0biscuit/OpenKai2/releases/tag/v" + wide(application_version);
            ShellExecuteW(hwnd, L"open", address.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
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
            (pageDirty=true),InvalidateRect(hwnd, nullptr, FALSE);
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
            refreshEnabled();
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
        if (id == RunAhead && notification == CBN_SELCHANGE) {
            if (page == settingsPage) previousSettings = settings;
            settings.runAhead = unsigned(std::clamp(selected(RunAhead), 0, 2));
            saveLauncherLook(settings.runAhead ? L"Run-ahead shows the game " + std::to_wstring(settings.runAhead) +
                                                     (settings.runAhead == 1 ? L" frame" : L" frames") +
                                                     L" ahead to cut input lag. It needs a faster PC and is off online and in speedrun mode."
                                               : L"Run-ahead is off.");
            (pageDirty=true),InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }
        if (id == LoadTimes && notification == CBN_SELCHANGE) {
            if (page == settingsPage) previousSettings = settings;
            settings.ps2LoadTimes = selected(LoadTimes) == 1;
            saveLauncherLook(settings.ps2LoadTimes ? L"Load times match a real PS2 drive."
                                                   : L"Loading as fast as this PC allows. Speedrun mode always uses PS2 load times.");
            (pageDirty=true),InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }
        if ((id == RandomizerMode || id == SpeedrunRandomizer) && notification == CBN_SELCHANGE) {
            if (page == settingsPage) previousSettings = settings;
            settings.randomizer = selected(id) == 1;
            SendMessageW(controls[id == RandomizerMode ? SpeedrunRandomizer : RandomizerMode], CB_SETCURSEL, settings.randomizer ? 1 : 0, 0);
            saveLauncherLook(settings.randomizer
                                 ? L"Randomizer on: your character changes every Dragon World and World Tournament fight. The CPU is not changed."
                                 : L"Randomizer off.");
            refreshSplits();
            refreshTimingRules();
            (pageDirty=true),InvalidateRect(hwnd, nullptr, FALSE);
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
                (pageDirty=true),InvalidateRect(hwnd, nullptr, FALSE);
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
                    (pageDirty=true),InvalidateRect(hwnd, nullptr, FALSE);
                    return;
                }
                settings.launcherTheme = theme;
                settings.launcherAccent = accent;
                settings.launcherColors = colors;
                message = std::wstring(L"Colors pasted: ") + themes[theme].name + L".";
            } else {
                const uint64_t tick = motionTicks();
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
            (pageDirty=true),InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }
        if (id == CloseColors && notification == BN_CLICKED) {
            page = settingsPage;
            layout();
            (pageDirty=true),InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }
        if (((id == ColorPick || id == ColorReset || id == ColorResetAll) && notification == BN_CLICKED) ||
            (id == ColorList && notification == LBN_DBLCLK)) {
            const auto picked = SendMessageW(controls[ColorList], LB_GETCURSEL, 0, 0);
            const auto row = picked < 0 ? picked : SendMessageW(controls[ColorList], LB_GETITEMDATA, WPARAM(picked), 0);
            std::wstring message;
            if (id == ColorResetAll) {
                if (MessageBoxW(hwnd, L"Return every color to the theme?", L"Reset colors", MB_YESNO | MB_ICONQUESTION) != IDYES)
                    return;
                settings.launcherColors = no_custom_colors();
                message = L"All colors follow the theme again.";
            } else if (row < 0 || row >= LRESULT(color_role_count)) {
                status = L"Pick a color first.";
                error = true;
                (pageDirty=true),InvalidateRect(hwnd, nullptr, FALSE);
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
                const COLORREF current = shown_role_color(unsigned(row), colorOf(look.*(color_roles[row].member)));
                CHOOSECOLORW choice{};
                choice.lStructSize = sizeof(choice);
                choice.hwndOwner = hwnd;
                choice.rgbResult = current;
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
            (pageDirty=true),InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }
        if (id == CloseTabs && notification == BN_CLICKED) {
            page = settingsPage;
            layout();
            (pageDirty=true),InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }
        if ((id == TabUp || id == TabDown || id == TabToggle || id == TabDefaults) && notification == BN_CLICKED) {
            const auto row = SendMessageW(controls[TabList], LB_GETCURSEL, 0, 0);
            std::wstring message;
            if (id == TabDefaults) {
                settings.tabOrder = default_tab_order;
                settings.hiddenTabs = 0;
                message = L"Tabs restored to their original order.";
            } else if (row < 0 || row >= LRESULT(dock_tab_count)) {
                status = L"Pick a tab first.";
                error = true;
                (pageDirty=true),InvalidateRect(hwnd, nullptr, FALSE);
                return;
            } else {
                const unsigned tab = settings.tabOrder[size_t(row)];
                if (id == TabToggle) {
                    if ((fixed_tabs >> tab) & 1u) {
                        status = std::wstring(tab_names[tab]) + L" is always shown.";
                        error = true;
                        (pageDirty=true),InvalidateRect(hwnd, nullptr, FALSE);
                        return;
                    }
                    settings.hiddenTabs ^= 1u << tab;
                    message = std::wstring(tab_names[tab]) + ((settings.hiddenTabs >> tab) & 1u ? L" is hidden." : L" is shown.");
                } else {
                    const LRESULT target = id == TabUp ? row - 1 : row + 1;
                    if (target < 0 || target >= LRESULT(dock_tab_count))
                        return;
                    std::swap(settings.tabOrder[size_t(row)], settings.tabOrder[size_t(target)]);
                    SendMessageW(controls[TabList], LB_SETCURSEL, WPARAM(target), 0);
                    message = std::wstring(tab_names[tab]) + L" moved " + (id == TabUp ? L"up." : L"down.");
                }
            }
            saveLauncherLook(message);
            refreshTabs();
            layout();
            (pageDirty=true),InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }
        if ((id == LauncherClock || id == WaveRibbons) && notification == CBN_SELCHANGE) {
            previousSettings = settings;
            settings.launcherClock = unsigned(std::clamp(selected(LauncherClock), 0, 3));
            settings.waveRibbons = galaxyValue(selected(WaveRibbons));
            saveLauncherLook(id == LauncherClock ? L"Clock changed." : L"Wave background changed.");
            (pageDirty=true),InvalidateRect(hwnd,nullptr,FALSE);
            return;
        }
        if ((id == WaveSky || id == WaveEmblem) && notification == CBN_SELCHANGE) {
            previousSettings = settings;
            settings.waveSky = unsigned(std::clamp(selected(WaveSky), 0, 11));
            settings.waveEmblem = selected(WaveEmblem) != 1;
            saveLauncherLook(id == WaveSky ? L"Wave sky changed." : settings.waveEmblem ? L"Dragon Ball emblem shown." : L"Dragon Ball emblem hidden.");
            waveBackdrop.clear();
            restyle();
            return;
        }
        if (id == LauncherInterface && notification == CBN_SELCHANGE) {
            previousSettings = settings;
            settings.launcherInterface = unsigned(std::clamp(selected(LauncherInterface), 0, 1));
            ambient.clear();waveBackdrop.clear();
            saveLauncherLook(waveStyle() ? L"Wave home on. Press Esc on a main page to return to it." : L"Classic interface on.");
            restyle();
            if (waveStyle()) openWaveHome();
            else { layout(); (pageDirty=true),InvalidateRect(hwnd, nullptr, FALSE); }
            return;
        }
        if ((id == LauncherTheme || id == LauncherAccent || id == StartPage || id==DiscordActivity || id==LauncherMotion || id==LauncherHints) && notification == CBN_SELCHANGE) {
            previousSettings = settings;
            settings.launcherTheme = unsigned(std::max(0, selected(LauncherTheme)));
            settings.launcherAccent = unsigned(std::max(0, selected(LauncherAccent)));
            settings.startPage = unsigned(std::max(0, selected(StartPage)));
            settings.discordPresence=selected(DiscordActivity)!=0;
            settings.reducedMotion=selected(LauncherMotion)==1;settings.helpHints=selected(LauncherHints)==0;
            if(hoverHints)SendMessageW(hoverHints,TTM_ACTIVATE,settings.helpHints,0);
            if(id==LauncherTheme) {
                settings.launcherAccent=0;settings.launcherColors=no_custom_colors();
                SendMessageW(controls[LauncherAccent],CB_SETCURSEL,0,0);
            }
            settings.lastPage = unsigned(std::clamp(navigationPage(), 0, int(tab_count) - 1));
            saveLauncherLook(id==LauncherMotion ? (settings.reducedMotion ? L"Reduced motion on. Navigation, GIFs and cycling accents stay still." : L"Smooth motion on. Windows animation preferences are respected.") :
                             id==LauncherHints ? (settings.helpHints ? L"Hover hints on." : L"Hover hints off. Keyboard shortcuts still work.") : id==DiscordActivity ? (settings.discordPresence ? L"Discord activity on." : L"Discord activity off.") :
                             id == StartPage ? (settings.startPage ? L"The launcher opens on the last page you used." : L"The launcher opens on Play.")
                                             : L"Theme " + std::wstring(themes[settings.launcherTheme].name) + L" with " +
                                                   (settings.launcherAccent ? std::wstring(accents[settings.launcherAccent].name) + L" accents." : L"its own accent color."));
            if (id == LauncherTheme || id==LauncherAccent || id==LauncherMotion)
                restyle();
            else
                (pageDirty=true),InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }
        if (id == OpenOverlay && notification == BN_CLICKED) {
            page = overlayPage;
            refreshStyleRows();
            layout();
            return;
        }
        if (id == CloseOverlay && notification == BN_CLICKED) {
            page = speedrunPage;
            layout();
            return;
        }
        if (((id >= OverlayCorner && id <= OverlaySplits) || (id >= OverlayText && id <= OverlayDecimals) || id == OverlayMargin || id == OverlayShape) &&
            notification == CBN_SELCHANGE) {
            saveOverlay(L"Overlay layout saved.");
            return;
        }
        if (id == OverlayTitle && notification == EN_KILLFOCUS) {
            saveOverlay(L"Overlay title saved.");
            return;
        }
        if (id == SplitList && notification == LBN_SELCHANGE) {
            const auto row = SendMessageW(controls[SplitList], LB_GETCURSEL, 0, 0);
            if (row >= 0 && size_t(row) < splitBook.best().size())
                SetWindowTextW(controls[SplitTime], wide(splitTime(splitBook.best()[size_t(row)])).c_str());
            (pageDirty=true),InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }
        if ((id == SplitSet || id == SplitAdd || id == SplitRemove || id == SplitReset) && notification == BN_CLICKED) {
            if (!settings.speedrun) {
                status = L"Choose a speedrun category first; each category keeps its own splits.";
                error = true;
                (pageDirty=true),InvalidateRect(hwnd, nullptr, FALSE);
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
            (pageDirty=true),InvalidateRect(hwnd, nullptr, FALSE);
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
        } else if (id == ChooseIso || (id == Play && !disc)) {
            auto p = choose(false, true);
            if (!p.empty()) {
                disc.reset();
                backups.clear();
                savedGames.clear();
                start(Job::verify, p);
            }
        } else if (id == VerifyIso) {
            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING | (candidateIso.empty() ? MF_GRAYED : 0), VerifySelectedIso, L"Verify selected ISO");
            AppendMenuW(menu, MF_STRING, ChooseIso, L"Choose another ISO…");
            RECT bounds{}; GetWindowRect(controls[VerifyIso], &bounds);
            const int chosen = int(TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
                bounds.left, bounds.bottom, 0, hwnd, nullptr));
            DestroyMenu(menu);
            if (chosen) command(chosen, BN_CLICKED);
        } else if (id == VerifySelectedIso && !candidateIso.empty())
            start(Job::verify, candidateIso);
        else if ((id == Play || id == LoadState) && disc) {
            launchState = id == LoadState ? 1u : 0u;
            start(Job::launch, settings.iso);
        }
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
                const auto file = backups[size_t(rows.front())];
                const auto contents = read_save(file);
                std::set<std::string> folders;
                uint64_t bytes = 0;
                for (const auto &entry : contents) { folders.insert(entry.name.substr(0,entry.name.find('/'))); bytes += entry.data.size(); }
                std::wstring prompt = file.filename().wstring() + L"\n" + std::to_wstring(folders.size()) +
                    L" save folders · " + bytesText(bytes) + L"\n";
                size_t shown = 0;
                for (const auto &name : folders) { if (++shown > 8) { prompt += L"\n…"; break; } prompt += L"\n" + wide(name); }
                prompt += L"\n\nRestore this backup? It replaces the current memory card. A recovery backup is created first.";
                if (MessageBoxW(hwnd, prompt.c_str(), L"Review backup", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES)
                    start(Job::restore, file);
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
        else if (id == ModInstall) {
            const auto package = chooseModPackage(false, L"");
            if (!package.empty()) {
                try {
                    const auto folder = mods::install_mod(mods::default_mods_directory(), package);
                    refreshMods();
                    for (size_t i = 0; i < modList.size(); ++i)
                        if (modList[i].folder == folder)
                            refreshMods(int(i));
                    status = L"Installed " + package.filename().wstring() + L". It is enabled and loads last.";
                    error = false;
                } catch (const std::exception &failure) {
                    status = L"Could not install the mod: " + wide(failure.what());
                    error = true;
                }
            }
        } else if (id == ModExport) {
            const auto index = SendMessageW(controls[ModList], LB_GETCURSEL, 0, 0);
            if (index < 0 || size_t(index) >= modList.size()) {
                status = L"Choose a mod to export.";
                error = true;
            } else {
                const auto &mod = modList[size_t(index)];
                const auto package = chooseModPackage(true, mod.folder.filename().wstring() + L".okmod");
                if (!package.empty()) {
                    try {
                        mods::export_mod(mod, package);
                        status = L"Exported " + wide(mod.name) + L" to " + package.filename().wstring() +
                                 L". Others can add it with Install mod.";
                        error = false;
                    } catch (const std::exception &failure) {
                        status = L"Could not export the mod: " + wide(failure.what());
                        error = true;
                    }
                }
            }
        }
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
        const int saved = SaveDC(dc);
        owner->clipControlPaint(dc, hwnd);
        RECT rect{};
        GetClientRect(hwnd, &rect);
        rect.left += owner->p(2);
        auto old = SelectObject(dc, owner->fonts.body);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, faint);
        if (hint)
            DrawTextW(dc, hint, -1, &rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        SelectObject(dc, old);
        RestoreDC(dc, saved);
        ReleaseDC(hwnd, dc);
    }
    if (message == WM_SETFOCUS || message == WM_KILLFOCUS)
        InvalidateRect(hwnd, nullptr, TRUE);
    return result;
}
LRESULT CALLBACK combo_procedure(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
    auto original = reinterpret_cast<WNDPROC>(GetPropW(hwnd, L"OpenKai2.ComboProcedure"));
    auto *owner = reinterpret_cast<Window *>(GetWindowLongPtrW(GetParent(hwnd), GWLP_USERDATA));
    if ((message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL) && !SendMessageW(hwnd, CB_GETDROPPEDSTATE, 0, 0))
        return SendMessageW(GetParent(hwnd), message, wp, lp);
    const auto nextFocus=reinterpret_cast<HWND>(wp);
    const bool cancelling=owner && !owner->closingDropdowns && message==WM_KILLFOCUS &&
        owner->dropdownOriginal.count(GetDlgCtrlID(hwnd)) && (!nextFocus || GetAncestor(nextFocus,GA_ROOTOWNER)!=owner->hwnd);
    if(cancelling) {owner->suspendUi();owner->closingDropdowns=true;}
    if(owner && (message==WM_SETFONT || message==CB_ADDSTRING || message==CB_INSERTSTRING || message==CB_DELETESTRING || message==CB_RESETCONTENT))
        owner->comboTextWidths.erase(GetDlgCtrlID(hwnd));
    if (message == WM_PAINT && owner) {
        PAINTSTRUCT ps;
        auto screen = BeginPaint(hwnd, &ps);
        const int saved = SaveDC(screen);
        owner->clipControlPaint(screen, hwnd);
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
        smoothLines(dc,{{float(x-owner->p(4)),float(y-flip*owner->p(2))},{float(x),float(y+flip*owner->p(2))},{float(x+owner->p(4)),float(y-flip*owner->p(2))}},enabled ? (focused || dropped || hot ? orange : muted) : faint,float(owner->p(1))+0.5f);
        canvas.reset();
        RestoreDC(screen, saved);
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
    if(cancelling)owner->closingDropdowns=false;
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
        if (self->deferDuringPaint(message,wp,lp))
            return message==controlKeyMessage || message==controlClearMessage ? 1 : 0;
        switch (message) {
        case WM_CREATE:
            self->initialize();
            return 0;
        case WM_PAINT:
            self->paint();
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case deferredUiMessage:
            self->dispatchDeferredUi();
            return 0;
        case WM_DRAWITEM:
            if (reinterpret_cast<DRAWITEMSTRUCT *>(lp)->CtlType == ODT_MENU)
                self->drawMenu(*reinterpret_cast<DRAWITEMSTRUCT *>(lp));
            else
                self->drawItem(*reinterpret_cast<DRAWITEMSTRUCT *>(lp));
            return TRUE;
        case WM_MEASUREITEM:
            if (reinterpret_cast<MEASUREITEMSTRUCT *>(lp)->CtlType == ODT_MENU) {
                self->measureMenu(*reinterpret_cast<MEASUREITEMSTRUCT *>(lp));
                return TRUE;
            }
            reinterpret_cast<MEASUREITEMSTRUCT *>(lp)->itemHeight = self->p(34);
            return TRUE;
        case WM_CONTEXTMENU:
            if (reinterpret_cast<HWND>(wp) != hwnd) {
                self->contextMenu(reinterpret_cast<HWND>(wp), lp);
                return 0;
            }
            break;
        case WM_COMMAND:
            if (HIWORD(wp) == CBN_CLOSEUP)
                PostMessageW(hwnd, pageClipMessage, 0, 0);
            self->command(LOWORD(wp), !lp && HIWORD(wp) == 1 ? BN_CLICKED : HIWORD(wp));
            return 0;
        case WM_ACTIVATEAPP:
            self->appActive=wp!=FALSE;
            if (self->appActive) self->queueUiRestore();
            else self->suspendUi();
            return 0;
        case WM_NCACTIVATE:
            if(!wp)self->suspendUi();
            break;
        case WM_ACTIVATE:
            if (LOWORD(wp)==WA_INACTIVE) self->suspendUi();
            else self->queueUiRestore();
            break;
        case WM_ENABLE:
            if (wp) self->queueUiRestore();
            break;
        case WM_DISPLAYCHANGE:
            if (self->fullscreen)
                self->coverMonitor();
            self->queueUiRestore();
            break;
        case WM_THEMECHANGED:
        case WM_EXITSIZEMOVE:
            self->queueUiRestore();
            break;
        case WM_TIMER:
            if (wp == 4)
                self->animateDock();
            else if (wp == 3)
                self->animateImages();
            else if (wp == 6)
                self->animateWave();
            else if (wp == 7)
                self->animateToast();
            else if (wp == 8)
                self->animatePageEnter();
            else if (wp == 11)
                self->animateSky();
            else if (wp == 9)
                self->animateTransition();
            else if (wp == 2)
                self->cycleFrame();
            else
                self->timer();
            return 0;
        case controlKeyMessage:
            return self->captureKey(wp, lp) ? 1 : 0;
        case controlClearMessage:
            return self->clearControl(int(wp)) ? 1 : 0;
        case pageClipMessage:
            if (self->layoutPending && !self->dropdownOpen())
                self->layout();
            else
                self->clipPageControls();
            (self->pageDirty=true),InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case restoreUiMessage:
            self->restoreUi();
            return 0;
        case onlineChanged:
            self->finishNameCheck();
            self->watchFriends();
            if (self->page == playerPage) {
                self->refreshPlayerButtons();
                (self->pageDirty = true), InvalidateRect(hwnd, nullptr, FALSE);
            }
            self->refreshOnlineLists();
            self->refreshEnabled();
            return 0;
        case onlineNotice:
            self->noticesArrived();
            return 0;
        case retroMessage:
            self->retroArrived(reinterpret_cast<Window::RetroReply*>(lp));
            return 0;
        case WM_SIZE:
            if (wp==SIZE_MINIMIZED) {self->wasMinimized=true;return 0;}
            if (!self->controls.empty())
                self->layout();
            if (self->wasMinimized) {self->wasMinimized=false;self->queueUiRestore();}
            return 0;
        case WM_DROPFILES:
            self->dropped(reinterpret_cast<HDROP>(wp));
            return 0;
        case WM_LBUTTONDOWN:
            if (self->toastPresence() > 0.3) {
                const RECT card = self->toastRect();
                const POINT at{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
                if (PtInRect(&card, at)) { self->toastAt = 0; KillTimer(hwnd, 7); self->command(OpenNotifications, BN_CLICKED); return 0; }
            }
            if (self->page == wavePage) { self->waveClick(GET_X_LPARAM(lp), GET_Y_LPARAM(lp)); return 0; }
            if (self->waveStyle() && GET_Y_LPARAM(lp) < self->p(shellHeader) && GET_X_LPARAM(lp) < self->p(260)) { self->openWaveHome(); return 0; }
            break;
        case WM_MOUSEWHEEL: {
            if (self->forwardingWheel || self->dropdownOpen()) return 0;
            if (self->page == wavePage) {
                self->wheelRemainder += GET_WHEEL_DELTA_WPARAM(wp);
                const int steps = self->wheelRemainder/WHEEL_DELTA;
                self->wheelRemainder -= steps*WHEEL_DELTA;
                if (steps) self->waveMove(0,-steps);
                return 0;
            }
            if (self->routeWheelToControl(wp,lp)) return 0;
            if (self->pageCanvasHeight() <= self->pageVisibleHeight()) return 0;
            self->wheelRemainder += GET_WHEEL_DELTA_WPARAM(wp);
            const int steps = self->wheelRemainder/WHEEL_DELTA;
            self->wheelRemainder -= steps*WHEEL_DELTA;
            UINT lines=3;SystemParametersInfoW(SPI_GETWHEELSCROLLLINES,0,&lines,0);
            const int distance=lines==WHEEL_PAGESCROLL ? self->pageVisibleHeight()-40 : int(std::min(lines,1000u))*18;
            if (steps) self->glideTo(self->scrollReported()-steps*distance);
            return 0;
        }
        case revealMessage:
            self->revealControls();
            return 0;
        case glideMessage: {
            self->glideStep();
            return 0;
        }
        case WM_VSCROLL: {
            const int code=LOWORD(wp),step=self->pageVisibleHeight()-40;
            const int next=code==SB_LINEUP ? self->pageScroll-32 : code==SB_LINEDOWN ? self->pageScroll+32 :
                code==SB_PAGEUP ? self->pageScroll-step : code==SB_PAGEDOWN ? self->pageScroll+step :
                code==SB_TOP ? 0 : code==SB_BOTTOM ? self->pageCanvasHeight() : self->pageScroll;
            self->scrollPage(next);return 0;
        }
        case WM_GETMINMAXINFO: {
            auto *limits = reinterpret_cast<MINMAXINFO *>(lp);
            MONITORINFO monitor{sizeof(monitor)};
            limits->ptMinTrackSize = {self->p(960), self->p(600)};
            if(GetMonitorInfoW(MonitorFromWindow(hwnd,MONITOR_DEFAULTTONEAREST),&monitor)) {
                limits->ptMinTrackSize.x=std::min(limits->ptMinTrackSize.x,monitor.rcWork.right-monitor.rcWork.left);
                limits->ptMinTrackSize.y=std::min(limits->ptMinTrackSize.y,monitor.rcWork.bottom-monitor.rcWork.top);
            }
            return 0;
        }
        case WM_DPICHANGED: {
            self->dpi = HIWORD(wp);
            self->ambient.clear();
            self->fonts.make(self->dpi);
            if (self->hoverHints) {
                SendMessageW(self->hoverHints, WM_SETFONT, reinterpret_cast<WPARAM>(self->fonts.caption), TRUE);
                SendMessageW(self->hoverHints, TTM_SETMAXTIPWIDTH, 0, self->p(360));
            }
            for (auto [id, c] : self->controls)
                SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(self->fonts.body), TRUE);
            SendMessageW(self->controls[TimingRules], WM_SETFONT, reinterpret_cast<WPARAM>(self->fonts.caption), TRUE);
            if (self->fullscreen)
                self->coverMonitor();
            else if (!IsZoomed(hwnd) && !IsIconic(hwnd)) {
                const auto r=fitWindowToMonitor(*reinterpret_cast<RECT *>(lp));
                SetWindowPos(hwnd, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top,
                             SWP_NOZORDER | SWP_NOACTIVATE);
            }
            self->layout();
            self->queueUiRestore();
            return 0;
        }
        case WM_CTLCOLORBTN:
        case WM_CTLCOLORSTATIC: {
            const auto target = reinterpret_cast<HWND>(lp);
            const COLORREF color = GetDlgCtrlID(target) == TimingRules ? panel
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
            const COLORREF color = target == self->controls[FriendList] ? panel : list ? field : panel;
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
            if (self && self->fineClock) {timeEndPeriod(1);self->fineClock=false;self->fastTimers=0;}
            self->stopGlideThread();
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
        (self->pageDirty=true),InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    } catch (...) {
        self->status = L"Something went wrong. Details are in launcher.log.";
        self->error = true;
        std::cerr << "[launcher:error] message=" << message << " failed with an unknown error" << std::endl;
        (self->pageDirty=true),InvalidateRect(hwnd, nullptr, FALSE);
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
    INITCOMMONCONTROLSEX commonControls{sizeof(commonControls), ICC_WIN95_CLASSES};
    InitCommonControlsEx(&commonControls);
    const auto com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    struct Com {
        HRESULT result;
        ~Com() {
            if (SUCCEEDED(result))
                CoUninitialize();
        }
    } cleanup{com};
    Gdiplus::GdiplusStartupInput vectorInput;
    ULONG_PTR vectorToken=0;
    if(Gdiplus::GdiplusStartup(&vectorToken,&vectorInput,nullptr)!=Gdiplus::Ok)
        throw std::runtime_error("Cannot initialize launcher drawing");
    struct DrawingCleanup {ULONG_PTR token;~DrawingCleanup(){Gdiplus::GdiplusShutdown(token);}} drawingCleanup{vectorToken};
    struct FontCleanup {
        HANDLE handle;
        ~FontCleanup() { if (handle) RemoveFontMemResourceEx(handle); }
    };
    DWORD uiInstalled = 0;
    auto uiResource = AddFontMemResourceEx(const_cast<unsigned char *>(ui_font), DWORD(ui_font_size), nullptr, &uiInstalled);
    FontCleanup uiFontCleanup{uiResource};
    DWORD textInstalled = 0, strongInstalled = 0;
    auto textResource = AddFontMemResourceEx(const_cast<unsigned char *>(inter_regular), DWORD(inter_regular_size), nullptr, &textInstalled);
    FontCleanup textFontCleanup{textResource};
    auto strongResource = AddFontMemResourceEx(const_cast<unsigned char *>(inter_semibold), DWORD(inter_semibold_size), nullptr, &strongInstalled);
    FontCleanup strongFontCleanup{strongResource};
    Window state;
    if (!textResource || !textInstalled) state.fonts.bodyFace=state.fonts.textFace=L"Segoe UI";
    if (!strongResource || !strongInstalled) state.fonts.strongFace=state.fonts.bodyFace;
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
    int width = std::min<int>(state.p(1280), int(work.right - work.left) - state.p(24));
    int height = std::min<int>(state.p(900), int(work.bottom - work.top) - state.p(24));
    int left = work.left + (work.right - work.left - width) / 2, top = work.top + (work.bottom - work.top - height) / 2;
    const auto &saved = state.settings;
    const RECT last{saved.windowLeft, saved.windowTop, saved.windowLeft + saved.windowWidth, saved.windowTop + saved.windowHeight};
    if (saved.windowWidth > 0 && saved.windowHeight > 0 && MonitorFromRect(&last, MONITOR_DEFAULTTONULL)) {
        left = saved.windowLeft;
        top = saved.windowTop;
        width = saved.windowWidth;
        height = saved.windowHeight;
    }
    const auto fitted=fitWindowToMonitor({left,top,left+width,top+height});
    left=fitted.left;top=fitted.top;width=fitted.right-fitted.left;height=fitted.bottom-fitted.top;
    auto hwnd = CreateWindowExW(WS_EX_COMPOSITED, type.lpszClassName, L"OpenKai2", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, left, top,
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
                         {FALT | FVIRTKEY, '1', ShortcutFirst},
                         {FALT | FVIRTKEY, '2', ShortcutFirst + 1},
                         {FALT | FVIRTKEY, '3', ShortcutFirst + 2},
                         {FALT | FVIRTKEY, '4', ShortcutFirst + 3},
                         {FALT | FVIRTKEY, '5', ShortcutFirst + 4},
                         {FALT | FVIRTKEY, '6', ShortcutFirst + 5},
                         {FCONTROL | FVIRTKEY, 'K', FindAction},
                         {FCONTROL | FSHIFT | FVIRTKEY, 'N', OpenNotifications},
                         {FCONTROL | FVIRTKEY, 'R', RefreshRooms},
                         {FCONTROL | FVIRTKEY, 'F', FocusOnlineSearch},
                         {FCONTROL | FVIRTKEY, 'O', ChooseIso},
                         {FCONTROL | FVIRTKEY, 'L', OpenLogs},
                         {FCONTROL | FVIRTKEY, 'P', HomeProfile},
                         {FALT | FVIRTKEY, VK_LEFT, NavigateBack},
                         {FVIRTKEY, VK_F1, OpenHelp},
                         {FVIRTKEY, VK_F11, ToggleLauncher},
                         {FCONTROL | FVIRTKEY, VK_NEXT, NextDockTab},
                         {FCONTROL | FVIRTKEY, VK_PRIOR, PreviousDockTab},
                         {FCONTROL | FVIRTKEY, VK_HOME, PageTop},
                         {FCONTROL | FVIRTKEY, VK_END, PageBottom},
                         {FCONTROL | FVIRTKEY, 'S', SaveCurrent},
                         {FCONTROL | FVIRTKEY, 'Z', UndoCurrent},
                         {FVIRTKEY, VK_F5, Play}};
    const auto accelerators = CreateAcceleratorTableW(shortcuts, int(std::size(shortcuts)));
    struct AcceleratorCleanup {
        HACCEL value;
        ~AcceleratorCleanup() { if (value) DestroyAcceleratorTable(value); }
    } shortcutCleanup{accelerators};
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (state.page == finderPage && message.message == WM_KEYDOWN && message.hwnd == state.controls[ActionSearch] &&
            (message.wParam == VK_UP || message.wParam == VK_DOWN)) {
            const auto list = state.controls[ActionList];
            const auto count = SendMessageW(list, LB_GETCOUNT, 0, 0);
            if (count > 0) {
                const auto row = SendMessageW(list, LB_GETCURSEL, 0, 0);
                SendMessageW(list, LB_SETCURSEL, WPARAM(std::clamp<LRESULT>(row + (message.wParam == VK_DOWN ? 1 : -1), 0, count - 1)), 0);
            }
            continue;
        }
        if (message.message == WM_MOUSEWHEEL && message.hwnd != hwnd &&
            !state.dropdownOpen() && state.routeWheelToControl(message.wParam, message.lParam))
            continue;
        const bool keyDown=message.message==WM_KEYDOWN || message.message==WM_SYSKEYDOWN;
        if(keyDown && message.wParam==VK_ESCAPE && state.capturing>=0) {
            state.capturing=-1;state.pendingCaptureAlt=0;state.status=L"Binding unchanged.";state.error=false;state.refreshControls();continue;
        }
        if (keyDown && message.wParam == 'A' && GetKeyState(VK_CONTROL) < 0 && GetKeyState(VK_MENU) >= 0) {
            wchar_t kind[16]{};
            GetClassNameW(message.hwnd, kind, 16);
            if (_wcsicmp(kind, L"EDIT") == 0) {
                SendMessageW(message.hwnd, EM_SETSEL, 0, -1);
                continue;
            }
        }
        if (keyDown && state.page == wavePage && !state.dropdownOpen() && GetKeyState(VK_CONTROL) >= 0 && GetKeyState(VK_MENU) >= 0) {
            const auto key = message.wParam;
            if (key == VK_LEFT || key == VK_RIGHT || key == VK_UP || key == VK_DOWN) {
                state.waveMove(key == VK_LEFT ? -1 : key == VK_RIGHT ? 1 : 0, key == VK_UP ? -1 : key == VK_DOWN ? 1 : 0);
                continue;
            }
            if (key == VK_RETURN || key == VK_SPACE) { state.waveActivate(); continue; }
            if (key == VK_ESCAPE || key == VK_BACK) continue;
        }
        if (keyDown && message.wParam == VK_ESCAPE && state.waveStyle() && state.page != wavePage &&
            !state.dropdownOpen() && state.capturing < 0) {
            wchar_t focused[16]{};
            GetClassNameW(GetFocus(), focused, 16);
            if (_wcsicmp(focused, L"EDIT") != 0 || state.page != finderPage) { state.waveBack(); continue; }
        }
        const bool keyUp=message.message==WM_KEYUP || message.message==WM_SYSKEYUP;
        const bool altKey=message.wParam==VK_MENU || message.wParam==VK_LMENU || message.wParam==VK_RMENU;
        if(state.capturing>=0 && altKey && keyDown)state.pendingCaptureAlt=message.lParam;
        if(state.capturing>=0 && altKey && keyUp && state.pendingCaptureAlt) {
            const auto press=state.pendingCaptureAlt;state.pendingCaptureAlt=0;
            if(SendMessageW(hwnd,controlKeyMessage,message.wParam,press))continue;
        }
        if (keyDown && message.wParam == VK_RETURN && !state.controls.empty() &&
            (message.hwnd == state.controls[PlayerList] || message.hwnd == state.controls[FriendList])) {
            if (const auto chosen = state.selectedPlayerId(); !chosen.empty()) {
                state.openPlayer(chosen, state.knownName(chosen));
                continue;
            }
        }
        if (keyDown && !(state.capturing>=0 && (altKey || GetKeyState(VK_MENU)<0)) &&
            SendMessageW(hwnd, controlKeyMessage, message.wParam, message.lParam))
            continue;
        if (message.message == WM_RBUTTONUP && message.hwnd != hwnd &&
            SendMessageW(hwnd, controlClearMessage, WPARAM(GetDlgCtrlID(message.hwnd)), 0))
            continue;
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
