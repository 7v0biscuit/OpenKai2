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
#include <fstream>
#include <functional>
#include <future>
#include <iostream>
#include <map>
#include <utility>
#include <memory>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <thread>
#include <optional>
#include <set>
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

constexpr int contentLeft = 36, margin = 36;
constexpr int shellHeader = 76, shellDockReserve = 116;
constexpr int speedrunTop = 112, splitsTop = 284, speedrunRulesTop = 526;
constexpr int onlineProfileTop = 112, onlineHostTop = 248, onlineHostBottom = 352, onlineListsTop = 364;
struct LoungeLayout {
    bool compact;
    int left=contentLeft, friendsLeft, friendsWidth, friendsBottom, centre, centreRight, details, detailsTop,
        right, bottom, gameBottom, roomsTop, notesTop, notesBottom;
    LoungeLayout(int width,int height) : compact(width<1100),
        friendsWidth(compact ? std::clamp(width*28/100,240,292) : std::clamp(width*17/100,188,222)),
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
    ProfileFit=1220, ProfileNotifications, ProfileCopyFrom, ProfileCopyStyle, ProfileRecover, ProfileManage, LauncherMotion, LauncherHints, ProfileAvailability,
    OpenHelp=1240,NextDockTab,PreviousDockTab,PageTop,PageBottom,SaveCurrent,UndoCurrent,ToggleLauncher,CopySetup,OpenIsoFolder
};
constexpr int pageCount = 8, onlinePage = 1, speedrunPage = 2, modsPage = 3, savesPage = 4, settingsPage = 5, updatesPage = 6,
              controlsPage = 7, importPage = 8, overlayPage = 9, tabsPage = 10, colorsPage = 11,
              profilePage = 12, finderPage = 13, notificationsPage=14, helpPage=15;
constexpr UINT controlKeyMessage = WM_APP + 40, controlClearMessage = WM_APP + 41,
               pageClipMessage = WM_APP + 42, restoreUiMessage = WM_APP + 43,
               deferredUiMessage = WM_APP + 44, glideMessage = WM_APP + 45;
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
// Evaluate coverage in physical pixels so rounded edges remain smooth on every
// GDI backend. Large panels use an analytic contour; small vector icons are
// sampled at 4x resolution, then reduced before compositing onto the canvas.
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
    const wchar_t *bodyFace = L"OpenKai2 Display", *strongFace = L"OpenKai2 Display";
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
                               CLEARTYPE_QUALITY, DEFAULT_PITCH, face ? face : weight>=FW_SEMIBOLD ? strongFace : bodyFace);
        };
        body = font(16, FW_NORMAL);
        caption = font(15, FW_NORMAL);
        label = font(13, FW_NORMAL);
        heading = font(26, FW_SEMIBOLD);
        brand = font(20, FW_NORMAL);
        strong = font(16, FW_NORMAL);
        gameName = font(62, FW_NORMAL);
        title = font(32, FW_NORMAL);
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
    int styleSelection = 0;
    bool imageAnimationTicking = false;
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
    std::map<std::string, int> roomPings;
    std::vector<online::Player> playerDetails;
    std::vector<std::string> friendStatus;
    std::set<std::string> requestedFriends;
    struct NameCheck {
        std::mutex mutex;
        bool done = false;
        std::optional<online::NameState> state;
        std::string name;
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
    bool homeFriendsOnly=false;
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
        // Focus cancellation only changes transient control state. Handle it
        // immediately so it cannot be replayed against a newly opened popup.
        switch (message) {
        case WM_COMMAND: case WM_TIMER: case WM_SIZE: case WM_DPICHANGED:
        case WM_CLOSE: case WM_DROPFILES: case WM_MOUSEWHEEL: case WM_VSCROLL:
        case pageClipMessage: case restoreUiMessage: case onlineChanged: case onlineNotice:
        case controlKeyMessage: case controlClearMessage:
            break;
        default: return false;
        }
        // GDI and native controls can dispatch sent messages while a frame is
        // being drawn. Keep its fonts, page data and bitmap alive until EndPaint.
        // A DPI message owns a temporary RECT, so retain the value, not its pointer.
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
        if (dockStarted) {dockPosition=double(dockTarget);dockStarted=0;KillTimer(hwnd,4);}
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
    int finderFrom = 0, finderReturnPage = 0, profileReturnPage=0,helpReturnPage=0,helpOrigin=0;
    struct Action { int id; const wchar_t *name; const wchar_t *keywords; };
    const std::vector<Action> actions{
        {Play, L"Play game", L"start offline launch"},
        {ChooseIso, L"Choose a game ISO", L"disc browse file change"},
        {VerifyIso, L"Verify selected ISO", L"disc check files"},
        {HomeSaves, L"Saves & backups", L"memory card import restore delete"},
        {HomeControls, L"Controls", L"keyboard controller bindings"},
        {OnlineTab, L"Online", L"friends rooms multiplayer"},
        {HomeProfile, L"Customize profile", L"picture avatar gif name"},
        {SpeedrunTab, L"Speedrun", L"timer categories splits"},
        {OpenOverlay, L"Customize speedrun overlay", L"split images png jpeg gif"},
        {ModsTab, L"Mods", L"characters stages import"},
        {SettingsTab, L"Settings", L"display graphics audio theme"},
        {SettingsDisplay,L"Display settings",L"window resolution aspect ratio frame rate fps cap vsync volume audio fullscreen"},
        {SettingsGraphics,L"Graphics settings",L"renderer anti aliasing texture filtering upscaling fsr internal resolution image"},
        {SettingsGame,L"Game settings",L"cpu difficulty music textures randomizer load times fps counter run ahead input lag latency"},
        {SettingsLauncher,L"Launcher appearance",L"theme accent colors start page discord activity"},
        {ArrangeTabs, L"Arrange tabs", L"dock navigation order hide"},
        {UpdatesTab, L"Updates", L"release patch notes download"},
        {OpenLogs, L"Open logs folder", L"diagnostics startup launcher"},
        {OpenHelp,L"Help & shortcuts",L"guide keyboard getting started support setup"},
        {CopySetup,L"Copy setup summary",L"support settings report clipboard"},
        {OpenIsoFolder,L"Open ISO folder",L"disc location explorer"},
        {OpenNotifications,L"Notifications",L"friend requests invitations online"}
    };
    std::vector<size_t> actionRows;
    std::optional<Settings> previousSettings;
    static auto preferences(Settings &s) {
        return std::tie(s.renderer, s.fps, s.width, s.height, s.windowMode, s.aspect, s.antialiasing,
            s.textureFilter, s.upscaler, s.internalScale, s.hdTextures, s.music, s.vsync, s.cpuDifficulty,
            s.audio, s.volume, s.fpsCounter, s.randomizer, s.ps2LoadTimes, s.runAhead, s.launcherTheme, s.launcherAccent,
            s.launcherColors, s.startPage, s.discordPresence, s.reducedMotion, s.helpHints);
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
        const int id = actions[actionRows[size_t(row)]].id;
        page = finderReturnPage;
        layout();
        command(id, BN_CLICKED);
    }
    int pageVisibleHeight() const {
        RECT r{};GetClientRect(hwnd,&r);
        return std::max(1,unscale(r.bottom)-shellHeader-shellDockReserve);
    }
    int pageCanvasHeight() const {
        RECT bounds{}; GetClientRect(hwnd, &bounds);
        const int width = unscale(bounds.right) - contentLeft - margin;
        const int controlsHeight = width < 1180 ? 1120 : 760;
        int settingsHeight = 970;
        if (page == settingsPage && controls.count(SettingsSearch)) {
            ui::SettingsLayout grid(width);
            grid.filter(searchText(trimmed(textOf(SettingsSearch))));
            settingsHeight=grid.height();
        }
        return std::max(page == helpPage ? 940 : page == overlayPage ? 880 : page == onlinePage ? 740 : page == controlsPage ? controlsHeight :
                        page == settingsPage ? settingsHeight : page == profilePage ? 980 : page == 0 ? (width<1100 ? 1040 : 740) : page == savesPage ? 780 : 680, pageVisibleHeight());
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
        if (std::abs(scrollGoal-scrollAt)<0.5) {scrollAt=scrollGoal;scrollGliding=false;glideActive=false;}
        scrollPageNow(int(std::lround(scrollAt)));
        if (pageScroll!=int(std::lround(scrollAt))) {scrollGliding=false;glideActive=false;}
        SetPropW(hwnd,L"OpenKai2.ScrollPos",reinterpret_cast<HANDLE>(intptr_t(scrollReported())));
    }
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
        return page == helpPage ? helpOrigin : page == notificationsPage ? onlinePage : page == finderPage ? finderFrom : page == savesPage || page == controlsPage ? 0 :
               page == profilePage ? profileReturnPage : page == importPage ? modsPage : page == overlayPage ? speedrunPage : page == tabsPage || page == colorsPage ? settingsPage : page;
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
    bool allowMotion() const {
        if(settings.reducedMotion)return false;
        BOOL enabled=TRUE;
        return !SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&enabled,0) || enabled;
    }
    void updateDockMotion() {
        const int target=tabSlot(navigationPage());
        if(target<0)return;
        if(dockTarget<0 || !allowMotion() || IsIconic(hwnd)) {
            dockPosition=double(target);dockTarget=target;dockStarted=0;KillTimer(hwnd,4);return;
        }
        if(target!=dockTarget) {
            dockFrom=dockPosition;dockTarget=target;dockStarted=GetTickCount64();SetTimer(hwnd,4,16,nullptr);
        }
    }
    void animateDock() {
        if(!dockStarted)return;
        const double t=std::min(1.0,double(GetTickCount64()-dockStarted)/240.0);
        const double eased=1.0-std::pow(1.0-t,3.0);
        dockPosition=dockFrom+(double(dockTarget)-dockFrom)*eased;
        if(t>=1.0 || !allowMotion() || IsIconic(hwnd)) {
            dockPosition=double(dockTarget);dockStarted=0;KillTimer(hwnd,4);
        }
        if(dropdownOpen() || IsIconic(hwnd))return;
        RECT r{};GetClientRect(hwnd,&r);r.top=std::max(0L,r.bottom-p(108));r.bottom-=p(30);
        (pageDirty=true),RedrawWindow(hwnd,&r,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN);
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
        if(!profile){page=onlinePage;layout();SetFocus(controls[OnlineName]);(pageDirty=true),InvalidateRect(hwnd,nullptr,FALSE);return;}
        if(page!=profilePage)profileReturnPage=navigationPage();
        const auto style=profileStyle(profile->id);
        SetWindowTextW(controls[ProfileInitials],style ? wide(style->initials).c_str() : L"");
        SetWindowTextW(controls[ProfileTagline],style ? wide(style->tagline).c_str() : L"");
        SendMessageW(controls[ProfileAccent],CB_SETCURSEL,style ? style->accent : 0,0);
        SendMessageW(controls[ProfileFit],CB_SETCURSEL,style ? style->pictureFit : 0,0);
        SendMessageW(controls[ProfileAvailability],CB_SETCURSEL,style ? style->availability : 0,0);
        SendMessageW(controls[ProfileNotifications],CB_SETCURSEL,style && !style->notifications ? 1 : 0,0);
        page=profilePage;layout();(pageDirty=true),InvalidateRect(hwnd,nullptr,FALSE);
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
        write_settings(settings_path(),next);settings=std::move(next);cycleTimer();openProfile();
        status=file.empty() ? L"Profile picture removed. Your original file was kept." : L"Profile picture saved on this PC.";
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
            imageAnimationTicking=true;SetTimer(hwnd,3,33,nullptr);
        }
        return found->second.frames.empty() ? nullptr : &found->second;
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
    void paintRoundPhoto(HDC dc,const speedrun::SplitImage& image,int cx,int cy,int radius,bool fit) {
        const int side=std::max(2,radius*2);
        const auto& frame=image.frames[image.frame_at(allowMotion() ? GetTickCount64() : 0)].rgba;
        const double scale=fit ? std::min(double(side)/image.width,double(side)/image.height) : std::max(double(side)/image.width,double(side)/image.height);
        std::vector<unsigned char> pixels(size_t(side)*side*4);
        for(int y=0;y<side;++y)for(int x=0;x<side;++x) {
            const double coverage=std::clamp(radius+.5-std::hypot(x+.5-radius,y+.5-radius),0.0,1.0);
            if(coverage<=0)continue;
            if(fit && (std::abs(x+.5-side/2.0)>image.width*scale/2 || std::abs(y+.5-side/2.0)>image.height*scale/2))continue;
            const double sx=std::clamp((x+.5-side/2.0)/scale+image.width/2.0-.5,0.0,double(image.width-1));
            const double sy=std::clamp((y+.5-side/2.0)/scale+image.height/2.0-.5,0.0,double(image.height-1));
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
    void avatar(HDC dc, int cx, int cy, int radius, const std::wstring &name, const std::string &key, COLORREF ring,
                int presence = -1, bool dim = false) {
        const auto style=profileStyle(key);
        const auto own=style ? std::optional<online::Profile>{} : activeProfile();
        const auto accent=style ? (style->accent==rgb_accent ? colorOf(cycling_accent(allowMotion() ? GetTickCount64() : 0)) : style->accent ? colorOf(accents[style->accent].color) : orange) : own && own->id==key ? orange : avatarTone(key);
        const COLORREF tone = dim ? mix(accent, panel, 150) : accent;
        ellipse(dc, cx, cy, radius + std::max(1, radius / 8), ring);
        ellipse(dc, cx, cy, radius, tone);
        const auto image=style ? profileImage(style->image) : nullptr;
        if(image) {
            paintRoundPhoto(dc,*image,cx,cy,std::max(1,radius-std::max(1,p(2))),style->pictureFit==1);
        } else {
        auto old = SelectObject(dc, radius >= p(18) ? fonts.strong : fonts.label);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, dim ? mix(RGB(255, 255, 255), panel, 110) : RGB(255, 255, 255));
        const auto letters = style && !style->initials.empty() ? wide(style->initials) : initials(name);
        RECT r{cx - radius, cy - radius, cx + radius + 1, cy + radius + 1};
        DrawTextW(dc, letters.c_str(), int(letters.size()), &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(dc, old);
        }
        if (presence >= 0) {
            const int dot = std::max(p(3), radius * 3 / 10), x = cx + radius * 7 / 10, y = cy + radius * 7 / 10;
            ellipse(dc, x, y, dot + std::max(1, p(2)), ring);
            ellipse(dc, x, y, dot, presence == 2 ? gold : presence == 1 ? green : faint);
        }
    }
    void avatarGlyph(HDC dc, RECT r, int kind, COLORREF color) {
        auto old = SelectObject(dc, kind == 0 ? fonts.label : fonts.strong);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, color);
        const wchar_t *glyph = kind == 0 ? L"VS" : L"+";
        DrawTextW(dc, glyph, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(dc, old);
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
        const int darkness=(GetRValue(background)*299+GetGValue(background)*587+GetBValue(background)*114)/1000;
        fill(dc, {p(left) + cut(12), p(bottom), p(right) - cut(12), p(bottom) + p(1)}, mix(background, RGB(0, 0, 0), darkness>128 ? 24 : 100));
        box(dc, {p(left), p(top), p(right), p(bottom)}, panel, mix(panel, border, 150), 12);
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
                found->second.visible = false;
        } else
            ShowWindow(controls.at(id), SW_HIDE);
    }
    void place(int id, int x, int y, int w, int h, bool visible = true) {
        auto c = controls.at(id);
        const bool pageControl = !(id >= PlayTab && id <= ControlsTab) && id != FindAction && id != OpenNotifications;
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
        // The unmanifested launcher can load common-controls v5, which rejects
        // the v6-only lpReserved field included in sizeof(TOOLINFOW).
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
        hint(HomeProfile, L"Customize your profile picture, initials, accent and tagline on this PC.");
        hint(OpenNotifications,L"Friend requests and match invitations (Ctrl+Shift+N).");
        hint(HomeFriendScope,L"Show all your friends or just those who are online.");
        hint(HomeFindPlayers,L"Find players and manage friends on Online.");
        hint(HomeInvite,L"Invite the selected online friend. You can also double-click a friend to join or invite them.");
        hint(HomeCreateRoom,L"Choose a room name, visibility and password on Online before hosting.");
        hint(ProfileImageChoose,L"Choose your own PNG, JPEG or animated GIF, up to 16 MB. You can also drop a picture onto your Profile page.");
        hint(ProfileImageRemove,L"Return to initials. Your original picture is kept.");
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
        page = page == helpPage ? helpReturnPage : page == finderPage ? finderReturnPage : page == profilePage ? profileReturnPage : navigationPage();
        layout();
        (pageDirty=true),InvalidateRect(hwnd, nullptr, FALSE);
        SetFocus(controls.at(PlayTab + navigationPage()));
        return true;
    }
    void initialize() {
        settings.tabOrder = normalize_dock_order(settings.tabOrder);
        apply_theme(settings.launcherTheme, settings.launcherAccent, settings.launcherColors);
        if (settings.startPage && settings.lastPage < tab_count && !((settings.hiddenTabs >> settings.lastPage) & 1u))
            page = int(settings.lastPage);
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
                                                          {ProfileCopyStyle,L"Copy appearance"},
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
        combo(Antialiasing, {L"Off", L"FXAA", L"FXAA High", L"FXAA Ultra"}, int(std::min(settings.antialiasing, 3u)));
        combo(Filtering, {L"Game default", L"Nearest (sharp)", L"Bilinear (3D surfaces)"},
              int(settings.textureFilter));
        combo(Upscaler, {L"Nearest", L"Bilinear", L"Bicubic", L"AMD FSR 1", L"Lanczos", L"Sharp pixels"}, int(settings.upscaler));
        combo(InternalScale, {L"Native (1x)", L"2x native", L"3x native", L"4x native", L"Auto (match display)"},
              settings.internalScale == 0 ? 4 : int(std::clamp(settings.internalScale, 1u, 4u) - 1u));
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
            std::vector<std::wstring> delays{L"Auto"};
            for (unsigned d = 1; d <= 8; ++d)
                delays.push_back(std::to_wstring(d) + (d == 1 ? L" frame" : L" frames"));
            combo(OnlineDelay, delays, int(settings.onlineDelay));
        }
        combo(OnlineListed, {L"Public", L"Invite only", L"Not listed"}, int(settings.onlineVisibility));
        combo(OnlineProfile, {}, 0);
        combo(HomeFriendScope,{L"All friends",L"Online friends"},0);
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
        SendMessageW(controls[OnlineSearch],EM_SETLIMITTEXT,128,0);
        {std::vector<std::wstring> names;for(const auto& choice:accents)names.emplace_back(choice.name);combo(ProfileAccent,names,0);}
        combo(ProfileAvailability,{L"Available",L"Away",L"Do not disturb"},0);
        combo(ProfileFit,{L"Fill the circle",L"Fit the whole picture"},0);
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
        combo(StartPage, {L"Play", L"Last page used"}, int(std::min(settings.startPage, 1u)));
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
        for (auto [id, limit] : {std::pair{OnlineName, 24}, {OnlinePort, 5}, {OnlineAddress, 255}, {OnlineRoom, 40},
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
        if(pageChanged){pageScroll=0;laidOutPage=page;scrollGliding=false;glideActive=false;}
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
                  clientHeight - 104, dockCell, 56, tab != dockTabs.end());
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
        } else if (page == 0) {
            const LoungeLayout g(content,h);
            position(HomeFriendScope,g.friendsLeft+16,154,g.friendsWidth-32,32);
            position(FriendList,g.friendsLeft+12,202,g.friendsWidth-24,std::max(80,g.friendsBottom-370));
            if(friendRows.empty())conceal(FriendList);
            const int friendAction=(g.friendsWidth-40)/2;
            position(HomeFindPlayers,g.friendsLeft+16,g.friendsBottom-144,friendAction,30);
            position(HomeInvite,g.friendsLeft+24+friendAction,g.friendsBottom-144,friendAction,30);
            SetWindowTextW(controls[HomeProfile],activeProfile() ? L"Customize profile" : L"Create profile");
            position(HomeProfile,g.friendsLeft+16,g.friendsBottom-40,g.friendsWidth-32,28);
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
            const auto back=L"Back to "+std::wstring(tab_names[profileReturnPage]);
            SetWindowTextW(controls[NavigateBack],back.c_str());
            position(NavigateBack,right-166,28,166,36);
            const int split=contentLeft+std::clamp(content/3,260,340),side=split+44,width=right-side-24;
            position(OnlineProfile,contentLeft+24,166,std::min(340,content-460),40);
            position(ProfileManage,right-390,166,210,40);
            position(ProfileRecover,right-166,166,142,40);
            position(ProfileInitials,side,330,(width-16)/2,40);
            position(ProfileTagline,side,410,width,40);
            position(ProfileAccent,side+(width+16)/2,330,(width-16)/2,40);
            position(ProfileAvailability,side,490,width,40);
            position(ProfileNotifications,side,570,width,40);
            position(ProfileSave,side,650,160,40);
            position(ProfileReset,side+174,650,130,40);
            position(ProfileCopyId,contentLeft+24,480,split-contentLeft-48,36);
            position(ProfileImageChoose,contentLeft+24,532,split-contentLeft-48,36);
            position(ProfileImageRemove,contentLeft+24,580,split-contentLeft-48,36);
            position(ProfileFit,contentLeft+24,664,split-contentLeft-48,40);
            position(ProfileCopyFrom,side,794,width,40);
            position(ProfileCopyStyle,side,850,160,40);
        } else if (page == savesPage) {
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
            SendMessageW(controls[RoomList],LB_SETITEMHEIGHT,0,p(50));
            const Grid grid(content);
            const int bottom = h - 64;
            const int profileRow = Grid::field(onlineProfileTop - 20, 0), action = 84;
            position(OnlineProfile, grid.x(1), profileRow, grid.column - action - 8, 34);
            position(ProfileRemove, grid.x(1) + grid.column - action, profileRow, action, 34);
            position(OnlineName, grid.x(2)+10, profileRow+6, grid.column-action-28, 22);
            position(ProfileAdd, grid.x(2) + grid.column - action, profileRow, action, 34);
            position(HomeProfile,grid.x(1),192,grid.column,28);
            position(ProfileCopyId,grid.x(2),192,grid.column,28);
            if(std::filesystem::exists(profileRecoveryPath())) {
                conceal(ProfileCopyId);position(ProfileRecover,grid.x(2),192,grid.column,28);
            }
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
            position(InvitePlayer, playersLeft + 20, bottom - 48, (playersWidth - 10) / 2, 34);
            position(AddFriend, playersLeft + 30 + (playersWidth - 10) / 2, bottom - 48, (playersWidth - 10) / 2, 34);
            if (roomRows.empty())
                conceal(RoomList);
            if (playerRows.empty())
                conceal(PlayerList);
        } else if (page == overlayPage) {
            const int panelBottom = h - 64, half = (content - 52) / 2, side = contentLeft + 32 + half;
            position(CloseOverlay,right-166,28,166,36);
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
            position(CloseColors,right-166,28,166,36);
            position(ColorList, contentLeft + 20, 228, listWidth, std::max(120, panelBottom - 20 - 228));
            const int side = contentLeft + 20 + listWidth + 20, buttonWidth = std::min(170, right - 20 - side);
            position(ColorPick, side, 228, buttonWidth, 34);
            position(ColorReset, side, 272, buttonWidth, 34);
            position(ColorResetAll, side, 316, buttonWidth, 34);
            position(ColorSurprise, side, 380, buttonWidth, 34);
            position(ColorCopy, side, 424, buttonWidth, 34);
            position(ColorPaste, side, 468, buttonWidth, 34);
        } else if (page == controlsPage) {
            const ControlsLayout grid(content);
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
            position(TabList, contentLeft + 20, 228, listWidth, std::min(int(dock_tab_count) * 40 + 4, panelBottom - 20 - 228));
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
            ui::SettingsLayout grid(content);
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
                {Renderer,Antialiasing,Filtering,Upscaler,InternalScale,ClearImage},
                {CpuDifficulty,HdTextures,Music,FpsCounter,RandomizerMode,LoadTimes,RunAhead},
                {LauncherTheme,LauncherAccent,StartPage,DiscordActivity,LauncherMotion,LauncherHints}}};
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
            const int y=where.pageY+shellHeader-pageScroll;
            where.visible=where.wanted && y+where.pageH>shellHeader && y<viewportBottom;
            where.y=p(y+where.adjust);
            const auto control=controls.at(id);
            const bool shown=(GetWindowLongPtrW(control,GWL_STYLE)&WS_VISIBLE)!=0;
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
        fill(dc,{0,0,p(w),p(shellHeader)},sidebar);
        fill(dc,{p(margin),p(shellHeader-1),p(right),p(shellHeader)},border);
        buffer.frame.ball(p(margin+16),p(36),p(14),{0,0,p(w),p(shellHeader)});
        text(dc,L"OpenKai2",margin+42,19,180,28,fonts.brand,ink);
        text(dc,L"Budokai 2 for Windows",margin+42,44,210,18,fonts.caption,muted);
        SYSTEMTIME now{};GetLocalTime(&now);wchar_t clock[40]{};
        if(GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT,TIME_NOSECONDS,&now,nullptr,clock,40))
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
        const wchar_t* titles[]={L"Play Budokai 2",L"Online",L"Speedrun",L"Mods",L"Saves & backups",L"Settings",
            L"Updates",L"Controls",L"Import tool",L"Speedrun overlay",L"Arrange tabs",L"Theme colors",L"Profile",L"Find an action",L"Notifications",L"Help & shortcuts"};
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
            L"Your picture, initials and tagline are saved on this PC.",
            L"Search pages and tools. Use the arrow keys and Enter to open a result.",
            L"Review friend requests and match invitations.",
            L"Quick controls, setup guidance and tools for reporting a problem."};
        const RECT tile{p(contentLeft),p(28),p(contentLeft+40),p(68)};
        box(dc,tile,mix(panel,orange,22),mix(border,orange,70),12);
        const int icon=page==helpPage ? OpenHelp : page==profilePage ? HomeProfile : page==finderPage ? FindAction : page==notificationsPage ? OpenNotifications :
            PlayTab+(page==controlsPage || page==savesPage ? page : navigationPage());
        navigationIcon(dc,icon,{tile.left-p(7),tile.top,tile.right,tile.bottom},orange);
        text(dc,titles[page],contentLeft+56,27,content-264,42,fonts.title,ink,DT_LEFT|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
        text(dc,subtitles[page],contentLeft+56,74,content-56,32,fonts.caption,muted,DT_LEFT|DT_WORDBREAK|DT_NOPREFIX);
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
            {L"Ctrl + K",L"Find a tool or setting"},{L"Ctrl + Shift + N",L"Open notifications"},{L"F11",L"Maximize / restore the launcher"}};
        const int keyWidth=std::clamp(content/3,230,360);
        for(size_t i=0;i<std::size(shortcuts);++i) {
            const int y=188+int(i)*33;
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
        text(dc,notice->name,contentLeft+24,178,content-48,76,fonts.title,ink,DT_LEFT|DT_WORDBREAK|DT_END_ELLIPSIS|DT_NOPREFIX);
        text(dc,notice->detail,contentLeft+24,262,content-48,76,fonts.body,muted,DT_LEFT|DT_WORDBREAK|DT_NOPREFIX);
        const auto identity=notice->verified ? L"Verified identity · "+notice->age : L"Identity not verified · "+notice->age;
        text(dc,identity,contentLeft+24,350,content-48,24,fonts.caption,notice->verified ? green : muted);
        if(notice->invite && !disc)
            text(dc,L"Verify your ISO on Play before joining this match.",contentLeft+24,394,content-48,36,fonts.caption,muted,DT_WORDBREAK|DT_NOPREFIX);
        if(notice->waiting)
            text(dc,std::to_wstring(notice->waiting)+L" more waiting",contentLeft+24,520,content-48,24,fonts.caption,muted);
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
        heading(dc,L"Friends",g.friendsLeft+18,126,g.friendsWidth-36);
        if(friendRows.empty()) {
            const int emptyTop=std::max(206,(202+g.friendsBottom-160)/2-26);
            text(dc,homeFriendsOnly ? L"No friends online" : L"Your friends appear here.",g.friendsLeft+20,emptyTop,g.friendsWidth-40,32,fonts.body,muted,DT_CENTER|DT_WORDBREAK|DT_NOPREFIX);
            text(dc,L"Find players on Online to add a friend.",g.friendsLeft+20,emptyTop+34,g.friendsWidth-40,36,fonts.caption,faint,DT_CENTER|DT_WORDBREAK|DT_NOPREFIX);
        }
        fill(dc,{p(g.friendsLeft+16),p(g.friendsBottom-100),p(g.friendsLeft+g.friendsWidth-16),p(g.friendsBottom-99)},border);
        const auto profile=activeProfile();
        const auto name=profile ? wide(profile->name) : std::wstring(L"Local player");
        if(profile)avatar(dc,p(g.friendsLeft+33),p(g.friendsBottom-70),p(16),name,profile->id,panel);
        else {ellipse(dc,p(g.friendsLeft+33),p(g.friendsBottom-70),p(16),mix(panel,orange,25));text(dc,L"P",g.friendsLeft+17,g.friendsBottom-82,32,24,fonts.strong,orange,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);}
        text(dc,name,g.friendsLeft+58,g.friendsBottom-89,g.friendsWidth-76,22,fonts.strong,ink,DT_LEFT|DT_END_ELLIPSIS|DT_SINGLELINE|DT_NOPREFIX);
        const auto style=profile ? profileStyle(profile->id) : nullptr;
        const auto activity=directory && directory->connected() ? statusLabel(profile_status(style ? style->availability : 0,running(),room!=nullptr,roomHost)) : profile ? std::wstring(L"Offline · local profile") : std::wstring(L"Create an online profile");
        text(dc,activity,g.friendsLeft+58,g.friendsBottom-65,g.friendsWidth-76,18,fonts.label,muted,DT_LEFT|DT_END_ELLIPSIS|DT_SINGLELINE|DT_NOPREFIX);
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
            avatar(dc,p(g.details+42),p((g.detailsTop+81)),p(22),wide(chosen->host),chosen->host,panel);
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
    void paintProfile(HDC dc,int h,int content) {
        const int right=contentLeft+content,split=contentLeft+std::clamp(content/3,260,340),side=split+44,width=right-side-24;
        panelBox(dc,contentLeft,124,right,226);
        heading(dc,L"Active profile",contentLeft+24,136,content-48);
        panelBox(dc,contentLeft,246,split,916);panelBox(dc,split+20,246,right,916);
        const auto profile=activeProfile();if(!profile)return;
        const auto style=profileStyle(profile->id);
        heading(dc,L"Your player card",contentLeft+24,266,split-contentLeft-48);
        avatar(dc,p((contentLeft+split)/2),p(340),p(42),wide(profile->name),profile->id,panel);
        text(dc,wide(profile->name),contentLeft+24,396,split-contentLeft-48,24,fonts.brand,ink,DT_CENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
        text(dc,style && !style->tagline.empty() ? wide(style->tagline) : L"Add a tagline to your player card.",contentLeft+24,430,split-contentLeft-48,36,fonts.caption,muted,DT_CENTER|DT_WORDBREAK|DT_END_ELLIPSIS|DT_NOPREFIX);
        caps(dc,L"PICTURE FRAMING",contentLeft+24,640,split-contentLeft-48);
        text(dc,style && !style->imageName.empty() ? wide(style->imageName) : L"PNG, JPEG or animated GIF",contentLeft+24,728,split-contentLeft-48,40,fonts.caption,muted,DT_WORDBREAK|DT_END_ELLIPSIS|DT_NOPREFIX);
        text(dc,L"Up to 16 MB. Your original picture is kept. Pictures and card details stay on this PC.",contentLeft+24,790,split-contentLeft-48,70,fonts.caption,muted,DT_WORDBREAK|DT_NOPREFIX);
        heading(dc,L"Appearance & notifications",side,266,width);
        caps(dc,L"INITIALS",side,306,(width-16)/2);
        caps(dc,L"AVATAR ACCENT",side+(width+16)/2,306,(width-16)/2);
        caps(dc,L"TAGLINE",side,386,width);
        caps(dc,L"ONLINE AVAILABILITY",side,466,width);
        caps(dc,L"INVITES & FRIEND REQUESTS",side,546,width);
        text(dc,L"Availability is shared online. Quiet mode and Do not disturb keep requests in Notifications without desktop alerts.",side,710,width,40,fonts.caption,muted,DT_WORDBREAK|DT_NOPREFIX);
        caps(dc,L"COPY APPEARANCE FROM ANOTHER PROFILE",side,770,width);
        text(dc,L"Copies the picture, initials, tagline and accent. Your name, friends and alert preferences stay yours.",side+174,846,width-174,48,fonts.caption,muted,DT_WORDBREAK|DT_NOPREFIX);
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
        heading(dc, L"Mods folder", contentLeft + 20, 136, content - 360);
        text(dc, mods::default_mods_directory().make_preferred().wstring(), contentLeft + 20, 160, content - 360, 18,
             fonts.caption, muted, DT_LEFT | DT_SINGLELINE | DT_PATH_ELLIPSIS | DT_NOPREFIX);
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
            imageAnimationTicking = true;SetTimer(hwnd, 3, 33, nullptr);
        }
        return found->second.frames.empty() ? nullptr : &found->second;
    }
    void paintSplitImage(HDC dc, const std::string& file, RECT bounds, COLORREF backdrop) {
        if(const auto image=splitImage(file))paintLocalImage(dc,*image,bounds,backdrop);
    }
    void paintLocalImage(HDC dc,const speedrun::SplitImage& value,RECT bounds,COLORREF backdrop,bool cover=false) {
        const auto image=&value;
        const auto& rgba = image->frames[image->frame_at(allowMotion() ? GetTickCount64() : 0)].rgba;
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
        if(page==0 || page==onlinePage || page==profilePage) {
            const auto profile=activeProfile();const auto style=profile ? profileStyle(profile->id) : nullptr;
            const auto image=style ? profileImages.find(style->image) : profileImages.end();
            animated=animated || (image!=profileImages.end() && image->second.frames.size()>1);
        }
        if (!animated || !allowMotion()) {KillTimer(hwnd,3);imageAnimationTicking=false;return;}
        if (dropdownOpen() || IsIconic(hwnd) || !appActive) return;
        (pageDirty=true),InvalidateRect(hwnd,nullptr,FALSE);
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
        panelBox(dc, contentLeft, 124, right, 196);
        heading(dc, L"Navigation dock", contentLeft + 20, 136, content - 200);
        text(dc, L"Reorder or hide tabs. Alt+1 to Alt+6 follow the dock. Play and Settings stay visible. "
                 L"Controls and Saves & backups are on Play.",
             contentLeft + 20, 160, content - 200, 32, fonts.caption, muted, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
        panelBox(dc, contentLeft, 208, right, panelBottom);
        caps(dc, L"TABS", contentLeft + 20, 212, 200);
    }
    void paintOverlay(HDC dc, int h, int content) {
        const int right = contentLeft + content, panelBottom = h - 64, half = (content - 52) / 2;
        const int side = contentLeft + 32 + half, column = (content - 52 - half - 12) / 2;
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
        constexpr int previewTop = 584;
        caps(dc,L"PREVIEW · EXAMPLE VALUES",side,previewTop-20,right-side-20,muted,DT_END_ELLIPSIS);
        const int previewWidth = right-20-side, previewHeight = panelBottom-42-previewTop;
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
        text(dc, profile ? wide(profile->name) : std::wstring(L"No profile yet"), nameLeft, onlineProfileTop + 36, identity, 26,
             fonts.brand, profile ? ink : muted, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
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
        label(onlineProfileTop - 20, 0, 1, settings.onlineProfiles.size() > 1 ? L"SWITCH PROFILE" : L"PROFILE");
        label(onlineProfileTop - 20, 0, 2, L"NEW PROFILE");

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
            box(dc, ring, mix(panel, border, 60), mix(panel, border, 160), 10);
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
    void paintSettings(HDC dc,int h,int content) {
        ui::SettingsLayout grid(content);
        grid.filter(searchText(trimmed(textOf(SettingsSearch))));
        const wchar_t* titles[]={L"Display",L"Graphics",L"Game",L"Launcher"};
        const wchar_t* notes[]={L"Window, presentation and sound. Game simulation stays at 60 FPS.",
            L"Renderer, image clarity and the resolution used for 3D scenes.",
            L"Difficulty, textures and music. Speedrun categories enforce their own rules.",
            L"Appearance, navigation and Discord activity on this PC."};
        const std::array<std::vector<const wchar_t*>,4> labels{{
            {L"WINDOW MODE",L"RESOLUTION",L"ASPECT RATIO",L"FRAME RATE CAP",L"VSYNC",L"GAME AUDIO"},
            {L"RENDERER",L"ANTI-ALIASING",L"TEXTURE FILTERING",L"UPSCALING",L"INTERNAL RESOLUTION",L"IMAGE PRESETS"},
            {L"CPU DIFFICULTY",L"HD TEXTURES",L"MUSIC",L"FPS COUNTER",L"RANDOMIZER",L"PS2 LOAD TIMES",L"RUN-AHEAD"},
            {L"THEME",L"ACCENT COLOR",L"START ON",L"DISCORD ACTIVITY",L"INTERFACE MOTION",L"HELP HINTS"}}};
        bool any=false;
        for(size_t group=0;group<labels.size();++group) {
            const int top=grid.sections[group];if(top<0)continue;any=true;
            panelBox(dc,contentLeft,top,contentLeft+content,grid.bottom(top,ui::SettingsLayout::fields[group]));
            text(dc,titles[group],contentLeft+24,top+14,200,26,fonts.brand,ink);
            text(dc,notes[group],contentLeft+24,top+46,content-48,20,fonts.caption,muted,
                 DT_LEFT|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
            for(size_t index=0;index<labels[group].size();++index) {
                const auto cell=grid.label(top,int(index));
                caps(dc,labels[group][index],cell.x,cell.y,cell.width,muted);
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
        const bool compact = h < 700;
        const int rulesTop = compact ? 496 : speedrunRulesTop;
        const int splitBottom = rulesTop - 12;
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
        const auto glowing = renderFrame(rect);
        BitBlt(dc, 0, 0, rect.right, rect.bottom, buffer.dc, 0, 0, SRCCOPY);
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
        const int w=unscale(rect.right), h=unscale(rect.bottom);
        const int visibleHeight=std::max(1,h-shellHeader-shellDockReserve);
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
        } else {
            if (ambient.size() != pixels) {
                fill(memory, {0,0,rect.right,buffer.frame.height}, background);
                // The content canvas spans the window; navigation lives in the dock.
                buffer.frame.radial(rect, rect.right * 3 / 4, -p(160), p(620), ember, 12);
                buffer.frame.radial(rect, 0, rect.bottom + p(60), p(440), orange, 5);
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
                text(memory, L"Type a page, setting or tool. Use the arrow keys to choose, then press Enter.",
                     contentLeft + 20, 168, content - 40, 24, fonts.caption, muted);

                if (actionRows.empty()) text(memory, L"No matching actions. Try another search.",
                    contentLeft + 40, 278, content - 80, 48, fonts.body, muted);
            } else if(page==helpPage) {
                paintHelp(memory,pageHeight,content);
            } else if(page==notificationsPage) {
                paintNotifications(memory,pageHeight,content);
            } else if (page == 0) {
                paintPlay(memory, content);
                paintHeader(memory, w, content);
            }
            else if (page == profilePage)
                paintProfile(memory,pageHeight,content);
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
        text(memory, message, contentLeft + 14, footer, content - 14 - used - 16, 20,
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
    void navigationIcon(HDC dc,int id,RECT r,COLORREF color) {
        const float size=float(p(20)),x=float(r.left+p(17)),y=float(r.top)+(float(r.bottom-r.top)-size)/2;
        sampledVector(dc,{LONG(x-2),LONG(y-2),LONG(std::ceil(x+size))+2,LONG(std::ceil(y+size))+2},[&](Gdiplus::Graphics& g){
        g.TranslateTransform(x,y);g.ScaleTransform(size/24.f,size/24.f);
        Gdiplus::Pen pen(drawingColor(color),1.7f);pen.SetLineJoin(Gdiplus::LineJoinRound);
        pen.SetStartCap(Gdiplus::LineCapRound);pen.SetEndCap(Gdiplus::LineCapRound);
        Gdiplus::SolidBrush brush(drawingColor(color));
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
        else if(id==ControlsTab){
            Gdiplus::GraphicsPath path;path.AddBezier(7.f,7.f,2.f,6.f,0.f,20.f,4.f,20.f);
            path.AddBezier(4.f,20.f,6.f,20.f,7.f,16.f,9.f,16.f);path.AddLine(9.f,16.f,15.f,16.f);
            path.AddBezier(15.f,16.f,17.f,16.f,18.f,20.f,20.f,20.f);path.AddBezier(20.f,20.f,24.f,20.f,22.f,6.f,17.f,7.f);
            path.CloseFigure();g.DrawPath(&pen,&path);line(7,10,7,14);line(5,12,9,12);g.FillEllipse(&brush,16.f,10.f,2.f,2.f);g.FillEllipse(&brush,18.f,13.f,2.f,2.f);
        } else if(id==UpdatesTab){line(12,3,12,15);line(8,11,12,15);line(12,15,16,11);line(4,16,4,21);line(4,21,20,21);line(20,21,20,16);}
        else if(id==ModsTab){line(12,2,22,7);line(22,7,12,12);line(12,12,2,7);line(2,7,12,2);line(2,7,2,17);line(2,17,12,22);line(12,22,22,17);line(22,17,22,7);line(12,12,12,22);}
        else {for(int i=0;i<3;++i){const float y=5.f+i*7.f,knob=i==1?16.f:8.f;line(3,y,21,y);Gdiplus::SolidBrush bg(drawingColor(panel));g.FillEllipse(&bg,knob-2,y-2,4.f,4.f);g.DrawEllipse(&pen,knob-2,y-2,4.f,4.f);}}
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
                navigationIcon(dc,OpenNotifications,{r.left-p(7),r.top,r.right,r.bottom},hasNotice() ? orange : muted);
                if(hasNotice()) ellipse(dc,r.right-p(7),r.top+p(7),p(3),orange);
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
        const bool friends = item.hwndItem == controls[FriendList];
        const COLORREF base = friends ? panel : list ? field : panel;
        fill(dc, r, base);
        if (chosen) {
            fill(dc, r, mix(base, orange, 26));
            fill(dc, {r.left, r.top + p(list ? 8 : 6), r.left + p(3), r.bottom - p(list ? 8 : 6)}, orange);
        }
        if (item.itemID != UINT(-1) && drawOnlineRow(dc, item, r, chosen, base))
            return;
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
            avatar(dc, r.left + p(14) + radius, cy, radius, wide(room.host), room.host, ring, -1, !same);
            if (!same) {
                const auto value = L"V" + wide(room.version);
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
            avatar(dc, r.left + p(14) + radius, cy, radius, title, player.id, ring, busy ? 2 : 1);
            if (player.version != application_version) {
                const auto value = L"V" + wide(player.version);
                tag(dc, right, cy, value, red);
                right -= tagWidth(dc, value) + p(6);
            }
            if(isFriend(player.id)) {
                tag(dc,right,cy,L"FRIEND",green);right-=tagWidth(dc,L"FRIEND")+p(6);
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
            line = on ? statusLabel(status) : L"Offline";
            if (const auto hosted = hostingLine(friendRows[row].second); on && !hosted.empty())
                line = hosted;
            titleColor = on ? ink : muted;
            avatar(dc, r.left + p(12) + radius, cy, radius, title, friendRows[row].first, ring, on ? (busy ? 2 : 1) : 0, !on);
        }
        SetTextColor(dc, titleColor);
        SelectObject(dc, friends ? fonts.body : fonts.strong);
        RECT top{textLeft, r.top + p(friends ? 5 : 6), right - p(6), cy + p(friends ? 2 : 3)};
        DrawTextW(dc, title.c_str(), int(title.size()), &top, DT_SINGLELINE | DT_BOTTOM | DT_END_ELLIPSIS | DT_NOPREFIX);
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
            const wchar_t *arrows[] = {L"↑", L"↓", L"←", L"→"};
            text(dc, arrows[index], x, y, 30, 30, fonts.strong, ink, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
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
        apply_theme(settings.launcherTheme, settings.launcherAccent, settings.launcherColors);
        const auto now = GetTickCount64();
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
        if(minute!=displayedMinute) {displayedMinute=minute;RECT header{};GetClientRect(hwnd,&header);header.bottom=p(shellHeader);(pageDirty=true),InvalidateRect(hwnd,&header,FALSE);}
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
                        [&](const std::string &existing) { return online::name_key(existing) == key; })) {
            status = L"You already have a profile with that name.";
            error = true;
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
        if (profile)
            for (const auto &entry : settings.onlineFriends)
                if (entry.owner == profile->id) {
                    const auto found = std::find_if(players.begin(), players.end(), [&](const auto &player) { return player.id == entry.id; });
                    if(homeFriendsOnly && found==players.end())continue;
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
        std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
            if (friendLabels[a].first != friendLabels[b].first)
                return friendLabels[a].first > friendLabels[b].first;
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
        SendMessageW(controls[FriendList], WM_SETREDRAW, FALSE, 0);
        SendMessageW(controls[FriendList], LB_RESETCONTENT, 0, 0);
        for (size_t i = 0; i < friendRows.size(); ++i) {
            const auto label = (friendLabels[i].first ? L"On · " : L"Off · ") + friendLabels[i].second;
            SendMessageW(controls[FriendList], LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
            if (friendRows[i].first == selectedFriend)
                SendMessageW(controls[FriendList], LB_SETCURSEL, WPARAM(i), 0);
        }
        SendMessageW(controls[FriendList], WM_SETREDRAW, TRUE, 0);
        playerDetails.clear();
        auto others = browse_players(players,settings.onlinePlayerFilter!=0,query,[&](const auto& id){return isFriend(id);});
        std::stable_sort(others.begin(), others.end(), [&](const online::Player &a, const online::Player &b) {
            if ((a.version == application_version) != (b.version == application_version))
                return a.version == application_version;
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
        if (selectedPlayerId().empty() || !isFriend(selectedPlayerId()))
            SetWindowTextW(controls[AddFriend], L"Add friend");
        else
            SetWindowTextW(controls[AddFriend], L"Remove friend");
        layout();refreshEnabled();
        (pageDirty=true),InvalidateRect(hwnd, nullptr, FALSE);
    }
    std::string selectedPlayerId() const {
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
    void notify(const std::wstring &title, const std::wstring &message) {
        const auto profile=activeProfile();const auto style=profile ? profileStyle(profile->id) : nullptr;
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
            {Vsync,settings.vsync ? 1 : 0},{Audio,audioChoice(settings)},
            {Antialiasing,int(settings.antialiasing)},{Filtering,int(settings.textureFilter)},
            {Upscaler,int(settings.upscaler)},{InternalScale,settings.internalScale ? int(settings.internalScale)-1 : 4},
            {HdTextures,int(settings.hdTextures)},{Music,settings.music ? 0 : 1},
            {CpuDifficulty,int(settings.cpuDifficulty)},{FpsCounter,settings.fpsCounter ? 1 : 0},
            {RandomizerMode,settings.randomizer ? 1 : 0},{SpeedrunRandomizer,settings.randomizer ? 1 : 0},
            {LoadTimes,settings.ps2LoadTimes ? 1 : 0},{RunAhead,int(std::min(settings.runAhead,2u))},{LauncherTheme,int(settings.launcherTheme)},
            {LauncherAccent,int(settings.launcherAccent)},{StartPage,int(settings.startPage)},
            {DiscordActivity,settings.discordPresence ? 1 : 0},{LauncherMotion,settings.reducedMotion ? 1 : 0},{LauncherHints,settings.helpHints ? 0 : 1}};
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
        if(id==ToggleLauncher && notification==BN_CLICKED) {ShowWindow(hwnd,IsZoomed(hwnd)?SW_RESTORE:SW_MAXIMIZE);return;}
        if (id == FindAction && notification == BN_CLICKED) { openActionFinder(); return; }
        if(id==OpenNotifications && notification==BN_CLICKED) {page=notificationsPage;layout();(pageDirty=true),InvalidateRect(hwnd,nullptr,FALSE);return;}
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
            pageScroll = 0; layout(); (pageDirty=true),InvalidateRect(hwnd, nullptr, FALSE); return;
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
                // Native Windows can close the popup before sending its last preview.
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
        if(notification==BN_CLICKED && id==ProfileManage) {page=onlinePage;layout();focusControl(OnlineName);return;}
        if(notification==BN_CLICKED && id==ProfileRecover) {recoverProfile();return;}
        if(notification==BN_CLICKED && id==ProfileCopyStyle) {
            const int index=selected(ProfileCopyFrom);const auto target=activeProfile();
            if(!target || index<0 || size_t(index)>=settings.onlineKeys.size())return;
            crypto::Seed seed{};if(!crypto::from_hex(settings.onlineKeys[size_t(index)],seed.data(),seed.size()))return;
            const auto source=online::make_profile(seed,settings.onlineProfiles[size_t(index)]);
            if(source.id==target->id){status=L"Choose a different profile to copy from.";error=false;return;}
            const auto from=profileStyle(source.id),own=profileStyle(target->id);
            auto copied=from ? *from : Settings::ProfileStyle{};copied.id=target->id;
            copied.notifications=own ? own->notifications : true;copied.availability=own ? own->availability : 0;
            Settings next=settings;
            next.profileStyles.erase(std::remove_if(next.profileStyles.begin(),next.profileStyles.end(),[&](const auto& style){return style.id==target->id;}),next.profileStyles.end());
            next.profileStyles.push_back(std::move(copied));write_settings(settings_path(),next);settings=std::move(next);
            cycleTimer();openProfile();status=L"Appearance copied from "+wide(source.name)+L".";error=false;return;
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
                    next.profileStyles.push_back(std::move(updated));
                }
                write_settings(settings_path(),next);settings=std::move(next);cycleTimer();openProfile();
                status=id==ProfileSave ? L"Profile appearance saved on this PC." : L"Profile appearance reset.";error=false;
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
            SetFocus(controls[id]);
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
            homeFriendsOnly=selected(HomeFriendScope)==1;refreshOnlineLists();return;
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
        if (id == ProfileRemove && notification == BN_CLICKED) {
            removeProfile();
            return;
        }
        if (id == RefreshRooms && (notification == BN_CLICKED || notification == 1)) {
            if(!activeProfile()) {
                status=L"Create a profile to connect to the online service.";error=false;
                page=onlinePage;layout();SetFocus(controls[OnlineName]);return;
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
            next.internalScale = defaults.internalScale; next.hdTextures = defaults.hdTextures;
            write_settings(settings_path(), next);
            previousSettings = settings;
            settings = std::move(next);
            const std::pair<int, int> choices[] = {{Renderer, 0}, {Fps, 6}, {Resolution, 0},
                {WindowMode, 0}, {Antialiasing, 0}, {Filtering, 0}, {Upscaler, 1}, {Vsync, 1}, {Aspect, 0}, {InternalScale, 4}, {HdTextures, 0}};
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
            self->drawItem(*reinterpret_cast<DRAWITEMSTRUCT *>(lp));
            return TRUE;
        case WM_MEASUREITEM:
            reinterpret_cast<MEASUREITEMSTRUCT *>(lp)->itemHeight = self->p(34);
            return TRUE;
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
        case WM_THEMECHANGED:
        case WM_EXITSIZEMOVE:
            self->queueUiRestore();
            break;
        case WM_TIMER:
            if (wp == 4)
                self->animateDock();
            else if (wp == 3)
                self->animateImages();
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
            self->refreshOnlineLists();
            self->refreshEnabled();
            return 0;
        case onlineNotice:
            self->noticesArrived();
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
        case WM_MOUSEWHEEL: {
            if (self->forwardingWheel || self->dropdownOpen()) return 0;
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
            const auto r=fitWindowToMonitor(*reinterpret_cast<RECT *>(lp));
            SetWindowPos(hwnd, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
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
    Window state;
    if (!uiResource || !uiInstalled) state.fonts.bodyFace=state.fonts.strongFace=L"Segoe UI";
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
        const bool keyUp=message.message==WM_KEYUP || message.message==WM_SYSKEYUP;
        const bool altKey=message.wParam==VK_MENU || message.wParam==VK_LMENU || message.wParam==VK_RMENU;
        if(state.capturing>=0 && altKey && keyDown)state.pendingCaptureAlt=message.lParam;
        if(state.capturing>=0 && altKey && keyUp && state.pendingCaptureAlt) {
            const auto press=state.pendingCaptureAlt;state.pendingCaptureAlt=0;
            if(SendMessageW(hwnd,controlKeyMessage,message.wParam,press))continue;
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
