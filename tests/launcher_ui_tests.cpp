// SPDX-License-Identifier: GPL-3.0-only
#include "launcher/settings.h"
#include "launcher/window.h"
#include "launcher/update.h"
#include "mods/mod_library.h"
#include "speedrun_images.h"
#include "speedrun_items.h"
#include "split_image_test_data.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <fstream>
#include <cstring>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <cwchar>

using namespace openkai2::launcher;
namespace {
HWND found{};
std::atomic<bool> updateStarted{false}, releaseUpdate{false};
BOOL CALLBACK find_launcher(HWND w, LPARAM) {
    DWORD pid=0;GetWindowThreadProcessId(w,&pid);
    wchar_t type[100]{};GetClassNameW(w,type,100);
    if(pid==GetCurrentProcessId() && std::wstring(type)==L"OpenKai2.Launcher") {found=w;return FALSE;}
    return TRUE;
}
void require(bool condition,const char *message) {
    if(!condition)throw std::runtime_error(message);
}
HWND wait_launcher() {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);
    while(std::chrono::steady_clock::now()<deadline) {
        EnumWindows(find_launcher,0);if(found && IsWindowVisible(found) && GetDlgItem(found,149))return found;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    throw std::runtime_error("Launcher window did not become ready");
}
void click(HWND w,int id) {
    require(GetDlgItem(w,id)!=nullptr,"Missing launcher action");
    SendMessageW(GetDlgItem(w,id),BM_CLICK,0,0);
}
void key_press(WORD code, WORD modifier=0) {
    INPUT events[4]{};
    const int count=modifier ? 4 : 2;
    for(auto& event:events)event.type=INPUT_KEYBOARD;
    if(modifier) {
        events[0].ki.wVk=modifier;events[1].ki.wVk=code;
        events[2].ki.wVk=code;events[2].ki.dwFlags=KEYEVENTF_KEYUP;
        events[3].ki.wVk=modifier;events[3].ki.dwFlags=KEYEVENTF_KEYUP;
    } else {
        events[0].ki.wVk=code;
        events[1].ki.wVk=code;events[1].ki.dwFlags=KEYEVENTF_KEYUP;
    }
    require(SendInput(count,events,sizeof(INPUT))==UINT(count),"Cannot submit native keyboard input");
}
void drop_file(HWND window,const std::filesystem::path& file) {
    const auto path=file.wstring();
    const size_t bytes=sizeof(DROPFILES)+(path.size()+2)*sizeof(wchar_t);
    auto memory=GlobalAlloc(GMEM_MOVEABLE|GMEM_ZEROINIT,bytes);
    require(memory!=nullptr,"Cannot allocate a file drop");
    auto data=static_cast<unsigned char*>(GlobalLock(memory));
    require(data!=nullptr,"Cannot prepare a file drop");
    auto drop=reinterpret_cast<DROPFILES*>(data);drop->pFiles=sizeof(DROPFILES);drop->fWide=TRUE;
    std::memcpy(data+sizeof(DROPFILES),path.c_str(),(path.size()+1)*sizeof(wchar_t));
    GlobalUnlock(memory);
    SendMessageW(window,WM_DROPFILES,reinterpret_cast<WPARAM>(memory),0);
}
void mouse_click(HWND control, int x, int y) {
    POINT point{x,y}; ClientToScreen(control,&point);
    static POINT previous{-10000,-10000};
    static auto last=std::chrono::steady_clock::now()-std::chrono::seconds(2);
    const auto interval=std::chrono::milliseconds(GetDoubleClickTime()+20);
    if(std::abs(point.x-previous.x)<=GetSystemMetrics(SM_CXDOUBLECLK) &&
       std::abs(point.y-previous.y)<=GetSystemMetrics(SM_CYDOUBLECLK)) {
        const auto elapsed=std::chrono::steady_clock::now()-last;
        if(elapsed<interval)std::this_thread::sleep_for(interval-elapsed);
    }
    previous=point;last=std::chrono::steady_clock::now();
    require(SetCursorPos(point.x,point.y)!=FALSE,"Cannot position native input");
    INPUT input[2]{};
    input[0].type=INPUT_MOUSE;input[0].mi.dwFlags=MOUSEEVENTF_LEFTDOWN;
    input[1].type=INPUT_MOUSE;input[1].mi.dwFlags=MOUSEEVENTF_LEFTUP;
    require(SendInput(2,input,sizeof(INPUT))==2,"Cannot submit native mouse input");
}
void open_dropdown(HWND combo) {
    SetForegroundWindow(GetAncestor(combo,GA_ROOT));
    RECT field{};GetClientRect(combo,&field);
    mouse_click(combo,field.right-10,(field.bottom-field.top)/2);
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!SendMessageW(combo,CB_GETDROPPEDSTATE,0,0) &&
          std::chrono::steady_clock::now()<deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if(!SendMessageW(combo,CB_GETDROPPEDSTATE,0,0)) {
        POINT point{field.right-10,(field.bottom-field.top)/2};ClientToScreen(combo,&point);
        HWND under=WindowFromPoint(point);
        wchar_t type[64]{};GetClassNameW(under,type,64);
        std::string open;
        for(int id=100;id<260;++id){HWND c=GetDlgItem(GetAncestor(combo,GA_ROOT),id);if(c&&SendMessageW(c,CB_GETDROPPEDSTATE,0,0))open+=" "+std::to_string(id);}
        RECT r{};GetWindowRect(combo,&r);
        throw std::runtime_error("Dropdown did not open: "+std::to_string(GetDlgCtrlID(combo))+" under="+std::to_string(GetDlgCtrlID(under))+
            " class="+std::string(type,type+wcslen(type))+" open="+open+" visible="+std::to_string(IsWindowVisible(combo))+
            " rect="+std::to_string(r.left)+","+std::to_string(r.top)+","+std::to_string(r.right)+","+std::to_string(r.bottom));
    }
    COMBOBOXINFO info{};info.cbSize=sizeof(info);
    require(GetComboBoxInfo(combo,&info)!=FALSE,"Cannot inspect dropdown");
    RECT list{};GetClientRect(info.hwndList,&list);
    const auto count=SendMessageW(combo,CB_GETCOUNT,0,0);
    const auto item=SendMessageW(combo,CB_GETITEMHEIGHT,0,0);
    if(list.bottom-list.top<std::min<LRESULT>(count,6)*item)
        throw std::runtime_error("Dropdown popup is too short: control="+std::to_string(GetDlgCtrlID(combo))+
            " height="+std::to_string(list.bottom-list.top)+" item="+std::to_string(item));
}
void mouse_choice(HWND combo,int index) {
    open_dropdown(combo);
    COMBOBOXINFO info{};info.cbSize=sizeof(info);
    require(GetComboBoxInfo(combo,&info)!=FALSE,"Cannot inspect dropdown list");
    const auto row=SendMessageW(combo,CB_GETITEMHEIGHT,0,0);
    const auto top=SendMessageW(info.hwndList,LB_GETTOPINDEX,0,0);
    mouse_click(info.hwndList,16,int((index-top)*row+row/2));
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(SendMessageW(combo,CB_GETDROPPEDSTATE,0,0) &&
          std::chrono::steady_clock::now()<deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    const auto actual=SendMessageW(combo,CB_GETCURSEL,0,0);
    if(actual!=index)throw std::runtime_error("Native mouse selection: control="+
        std::to_string(GetDlgCtrlID(combo))+" expected="+std::to_string(index)+
        " actual="+std::to_string(actual));
}
template<class Predicate> void wait_until(Predicate ready, const char *failure) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!ready()) {
        if (std::chrono::steady_clock::now() >= deadline) throw std::runtime_error(failure);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
void keyboard_choice(HWND combo,int index) {
    open_dropdown(combo);
    key_press(VK_HOME);
    wait_until([&]{return SendMessageW(combo,CB_GETCURSEL,0,0)==0;},"Home did not select the first option");
    for(int i=0;i<index;++i) {
        key_press(VK_DOWN);
        wait_until([&]{return SendMessageW(combo,CB_GETCURSEL,0,0)==i+1;},"Arrow did not select the next option");
    }
    if (!SendMessageW(combo,CB_GETDROPPEDSTATE,0,0))
        throw std::runtime_error("Changing a setting closed the dropdown before confirmation: control="+
            std::to_string(GetDlgCtrlID(combo))+" selection="+std::to_string(index));
    key_press(VK_RETURN);
    wait_until([&]{return !SendMessageW(combo,CB_GETDROPPEDSTATE,0,0);},"Confirm did not close the popup");
    const auto actual=SendMessageW(combo,CB_GETCURSEL,0,0);
    if(actual!=index)throw std::runtime_error("Native keyboard selection: control="+
        std::to_string(GetDlgCtrlID(combo))+" expected="+std::to_string(index)+
        " actual="+std::to_string(actual));
}
}
namespace openkai2::launcher {
std::optional<UpdateRelease> check_for_updates(bool) {
    updateStarted = true;
    while (!releaseUpdate.load()) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    return {};
}
StagedUpdate stage_update(const UpdateRelease &, const std::function<void(const std::string &)> &) {
    throw std::runtime_error("UI test unexpectedly tried to download an update");
}
void apply_update(const StagedUpdate &) {
    throw std::runtime_error("UI test unexpectedly tried to install an update");
}
}
int main() {
    try {
        const auto root=std::filesystem::temp_directory_path()/
            (L"OpenKai2 UI "+std::to_wstring(GetCurrentProcessId()));
        std::filesystem::create_directories(root);
        struct Cleanup {std::filesystem::path path;~Cleanup(){std::error_code ec;std::filesystem::remove_all(path,ec);}} cleanup{root};
        require(SetEnvironmentVariableW(L"LOCALAPPDATA",root.c_str())!=FALSE,"Cannot isolate settings");
        Settings initial;initial.autoUpdates=true;initial.discordPresence=false;
        initial.onlineName="UI test";initial.onlineProfiles={"UI test"};initial.onlineKeys={std::string(64,'a')};
        initial.onlineLobby="127.0.0.1:9";
        write_settings(settings_path(),initial);
        std::exception_ptr failure;
        std::thread driver([&] {
            HWND w{};
            try {
                w=wait_launcher();
                wait_until([] { return updateStarted.load(); }, "Background update did not start");
                require(IsWindowEnabled(GetDlgItem(w,110)) != FALSE,
                        "Background update check blocked choosing an ISO");
                for (auto size : {std::pair{960,600},std::pair{1000,680},std::pair{1440,960}}) {
                    SetWindowPos(w,nullptr,0,0,size.first,size.second,SWP_NOZORDER|SWP_NOACTIVATE);
                    RECT client{};GetClientRect(w,&client);
                    for (int id=101;id<=108;++id) {
                        auto tab=GetDlgItem(w,id);RECT bounds{};GetWindowRect(tab,&bounds);
                        MapWindowPoints(nullptr,w,reinterpret_cast<POINT*>(&bounds),2);
                        require(IsWindowVisible(tab) && bounds.left>=0 && bounds.right<=client.right && bounds.top>=0 && bounds.bottom<=client.bottom-26,
                                "A dock tab extends beyond the launcher border");
                        wchar_t label[64]{};GetWindowTextW(tab,label,64);auto dc=GetDC(tab);
                        auto old=SelectObject(dc,reinterpret_cast<HFONT>(SendMessageW(tab,WM_GETFONT,0,0)));SIZE text{};
                        GetTextExtentPoint32W(dc,label,int(wcslen(label)),&text);SelectObject(dc,old);ReleaseDC(tab,dc);
                        require(text.cx+8<=bounds.right-bounds.left,"A dock tab's label is clipped");
                    }
                }
                SetWindowPos(w,nullptr,0,0,1280,900,SWP_NOZORDER|SWP_NOACTIVATE);
                require(GetDlgItem(w,1002)==nullptr,"Settings is duplicated in the header");
                HWND hints{};
                while ((hints=FindWindowExW(nullptr,hints,TOOLTIPS_CLASSW,nullptr)))
                    if (GetWindow(hints,GW_OWNER)==w)break;
                require(hints!=nullptr,"Launcher hover hints are missing");
                wchar_t hint[512]{};
                TOOLINFOW tool{TTTOOLINFOW_V2_SIZE};tool.uFlags=TTF_IDISHWND;tool.hwnd=w;
                tool.uId=reinterpret_cast<UINT_PTR>(GetDlgItem(w,101));tool.lpszText=hint;
                SendMessageW(hints,TTM_GETTEXTW,512,reinterpret_cast<LPARAM>(&tool));
                require(std::wstring(hint).find(L"Ctrl+1")!=std::wstring::npos,"Dock hints do not show the matching keyboard shortcut");
                click(w,1001);
                require(IsWindowVisible(GetDlgItem(w,1007)),"Profile customization did not open");
                SetWindowTextW(GetDlgItem(w,1007),L"UT");SetWindowTextW(GetDlgItem(w,1008),L"Local appearance");
                keyboard_choice(GetDlgItem(w,1009),3);click(w,1010);
                const auto style=read_settings(settings_path());
                require(style.profileStyles.size()==1 && style.profileStyles[0].initials=="UT" &&
                            style.profileStyles[0].tagline=="Local appearance" && style.profileStyles[0].accent==3 &&
                            style.onlineKeys==initial.onlineKeys && style.onlineName==initial.onlineName,
                        "Profile customization changed identity or did not persist");
                key_press(VK_LEFT,VK_MENU);
                wait_until([&]{return IsWindowVisible(GetDlgItem(w,112)) && !IsWindowVisible(GetDlgItem(w,1007));},
                           "Alt+Left did not return from profile customization");
                click(w,106);
                require(GetDlgItem(w,174)==nullptr,"The Discord toggle is still in the launcher");
                require(read_settings(settings_path()).discordPresence,"Discord activity is not on by default");
                for(int id=140;id<=150;++id) {
                    if(id==148)continue;
                    auto combo=GetDlgItem(w,id);
                    open_dropdown(combo);
                    SendMessageW(combo,CB_SHOWDROPDOWN,FALSE,0);
                }
                keyboard_choice(GetDlgItem(w,141),3);
                require(read_settings(settings_path()).fps==120,"FPS was not persisted by native interaction");
                open_dropdown(GetDlgItem(w,141));key_press(VK_HOME);
                wait_until([&]{return SendMessageW(GetDlgItem(w,141),CB_GETCURSEL,0,0)==0;},"A dropdown could not be previewed");
                require(read_settings(settings_path()).fps==120,"An unconfirmed dropdown choice was saved");
                SendMessageW(w,WM_APP+30,0,0);
                require(SendMessageW(GetDlgItem(w,141),CB_GETDROPPEDSTATE,0,0)!=0,
                        "An online list refresh closed an open settings dropdown");
                key_press(VK_ESCAPE);
                wait_until([&]{return !SendMessageW(GetDlgItem(w,141),CB_GETDROPPEDSTATE,0,0);},"Escape did not close the dropdown");
                require(read_settings(settings_path()).fps==120 && SendMessageW(GetDlgItem(w,141),CB_GETCURSEL,0,0)==3,
                        "Canceling a dropdown changed the saved preference");
                mouse_choice(GetDlgItem(w,144),1);
                require(read_settings(settings_path()).antialiasing==1,"Mouse anti-aliasing selection was not saved");
                mouse_choice(GetDlgItem(w,150),3);
                require(read_settings(settings_path()).audio && read_settings(settings_path()).volume==50,
                        "Audio preference was not persisted");
                keyboard_choice(GetDlgItem(w,149),1);
                require(read_settings(settings_path()).aspect==1,"Aspect ratio was not persisted");
                keyboard_choice(GetDlgItem(w,143),1);
                require(!IsWindowEnabled(GetDlgItem(w,142)),"Borderless resolution should follow the desktop");
                keyboard_choice(GetDlgItem(w,143),0);
                require(IsWindowEnabled(GetDlgItem(w,142))!=FALSE,"Windowed resolution remained disabled");
                mouse_choice(GetDlgItem(w,146),3);
                require(read_settings(settings_path()).upscaler==3,"FSR selection was not persisted");
                keyboard_choice(GetDlgItem(w,145),2);
                require(read_settings(settings_path()).textureFilter==2,"Texture filtering was not saved");
                click(w,105);
                auto saves = GetDlgItem(w,126);
                require((GetWindowLongPtrW(saves,GWL_STYLE) & LBS_EXTENDEDSEL) != 0,
                        "Save list does not allow multiple selections");
                require(!IsWindowEnabled(GetDlgItem(w,128)), "Delete is enabled without any saves");
                keyboard_choice(GetDlgItem(w,175),1);
                wait_until([&]{return !IsWindowVisible(GetDlgItem(w,123));}, "Restore is visible in game-save view");
                keyboard_choice(GetDlgItem(w,175),0);
                keyboard_choice(GetDlgItem(w,148),0);
                require(!read_settings(settings_path()).autoBackups,"Backup preference did not persist");
                releaseUpdate = true;
                click(w,107);
                wait_until([&] { return IsWindowEnabled(GetDlgItem(w,161)) != FALSE; },
                           "Update preferences did not unlock after the check completed");
                keyboard_choice(GetDlgItem(w,161),0);
                keyboard_choice(GetDlgItem(w,160),0);
                require(read_settings(settings_path()).updateChannel==0,"Release channel was not persisted");
                auto persisted=read_settings(settings_path());
                require(!persisted.autoUpdates,"Changing another preference re-enabled automatic updates");
            } catch(...) {failure=std::current_exception();}
            releaseUpdate = true;
            if(w)PostMessageW(w,WM_CLOSE,0,0);
        });
        try {run_window();}catch(...) {driver.join();throw;}
        driver.join();
        if(failure)std::rethrow_exception(failure);
        std::thread reopened([&] {
            HWND w{};
            try {
                w = wait_launcher(); click(w,106);
                require(SendMessageW(GetDlgItem(w,141),CB_GETCURSEL,0,0)==3,
                        "Saved FPS was not restored when reopening the launcher");
                require(SendMessageW(GetDlgItem(w,144),CB_GETCURSEL,0,0)==1,
                        "Saved anti-aliasing was not restored");
                require(SendMessageW(GetDlgItem(w,149),CB_GETCURSEL,0,0)==1,
                        "Saved aspect ratio was not restored");
                require(SendMessageW(GetDlgItem(w,146),CB_GETCURSEL,0,0)==3,
                        "Saved FSR preference was not restored");
                click(w,173);
                const auto clear=read_settings(settings_path());
                require(clear.antialiasing==0 && clear.textureFilter==0 && clear.upscaler==2,
                        "Clear image preset was not saved");
                require(clear.fps==120 && clear.aspect==1 && !clear.autoBackups && !clear.autoUpdates,
                        "Clear image preset changed display pacing or save preferences");
                click(w,170);
                auto reset = read_settings(settings_path());
                require(reset.fps==240 && reset.aspect==0 && reset.antialiasing==0 &&
                        reset.width==1280 && reset.height==720 && reset.renderer=="auto",
                        "Graphics defaults did not reset");
                require(!reset.autoBackups && !reset.autoUpdates && reset.updateChannel==0,
                        "Reset graphics changed save/update preferences");
                require(reset.discordPresence,"Reset graphics changed the Discord preference");
                require(reset.audio && reset.volume==50,"Reset graphics changed the audio preference");
                const auto dpiForWindow = reinterpret_cast<UINT(WINAPI *)(HWND)>(
                    GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
                const int dpi = dpiForWindow ? int(dpiForWindow(w)) : 96;
                for (int height : {860, 920, 1000}) {
                    SetWindowPos(w,nullptr,0,0,MulDiv(1000,dpi,96),MulDiv(height,dpi,96),
                                 SWP_NOZORDER|SWP_NOACTIVATE);
                    for(int id : {140,141,142,143,144,145,146,147,149,150,151,152,153,154,
                                  160,161,162,163,164,170,171,173,179}) {
                        RECT bounds{},client{};GetWindowRect(GetDlgItem(w,id),&bounds);GetClientRect(w,&client);
                        MapWindowPoints(nullptr,w,reinterpret_cast<POINT*>(&bounds),2);
                        require(bounds.left>=0 && bounds.top>=0 && bounds.right<=client.right &&
                                bounds.bottom<=client.bottom-MulDiv(110,dpi,96),
                                "A settings control overlaps the status footer or leaves the window");
                    }
                }
                click(w,103);
                auto rules = GetDlgItem(w,290);
                require(rules && IsWindowVisible(rules), "Speedrun timing rules are missing");
                require((GetWindowLongPtrW(rules,GWL_STYLE)&ES_READONLY)!=0,
                        "Speedrun timing rules can be edited");
                wchar_t rulesText[4096]{};
                GetWindowTextW(rules,rulesText,4096);
                require(std::wstring(rulesText).find(L"logs\\speedruns")!=std::wstring::npos &&
                            std::wstring(rulesText).find(L"moderators")!=std::wstring::npos,
                        "The end of the timing rules was discarded");
                require(!IsWindowVisible(GetDlgItem(w,263)),"The split list covers its empty-state message");
                for(int height : {860,920,1000}) {
                    SetWindowPos(w,nullptr,0,0,MulDiv(1000,dpi,96),MulDiv(height,dpi,96),
                                 SWP_NOZORDER|SWP_NOACTIVATE);
                    for(int id : {177,178,236,222,264,265,266,267,268,290}) {
                        RECT bounds{},client{};GetWindowRect(GetDlgItem(w,id),&bounds);GetClientRect(w,&client);
                        MapWindowPoints(nullptr,w,reinterpret_cast<POINT*>(&bounds),2);
                        require(IsWindowVisible(GetDlgItem(w,id)) && bounds.left>=0 && bounds.top>=0 &&
                                    bounds.right<=client.right && bounds.bottom<=client.bottom-MulDiv(110,dpi,96),
                                ("A speedrun control overlaps the dock or leaves the window: id="+std::to_string(id)).c_str());
                    }
                    SendMessageW(rules,EM_LINESCROLL,0,10000);
                    require(SendMessageW(rules,EM_GETFIRSTVISIBLELINE,0,0)>0,
                            "The remaining timing rules cannot be scrolled into view");
                    SendMessageW(rules,EM_LINESCROLL,0,-10000);
                }
                SetWindowPos(w,nullptr,0,0,MulDiv(1000,dpi,96),MulDiv(680,dpi,96),SWP_NOZORDER|SWP_NOACTIVATE);
                SCROLLINFO wheel{sizeof(wheel),SIF_POS};GetScrollInfo(w,SB_VERT,&wheel);
                require(wheel.nPos==0,"A resized page started at the wrong scroll position");
                SendMessageW(w,WM_MOUSEWHEEL,MAKEWPARAM(0,WORD(short(-60))),0);GetScrollInfo(w,SB_VERT,&wheel);
                require(wheel.nPos==0,"A partial mouse-wheel step moved too far");
                SendMessageW(w,WM_MOUSEWHEEL,MAKEWPARAM(0,WORD(short(-60))),0);GetScrollInfo(w,SB_VERT,&wheel);
                require(wheel.nPos>0,"High-resolution mouse-wheel steps were discarded");
                SendMessageW(w,WM_VSCROLL,SB_BOTTOM,0);
                require(IsWindowVisible(GetDlgItem(w,290)),"Timing rules cannot be reached in a short window");
                click(w,222);
                require(IsWindowVisible(GetDlgItem(w,1020)),"The split appearance list disappeared in a short window");
                SendMessageW(w,WM_VSCROLL,SB_BOTTOM,0);
                require(IsWindowVisible(GetDlgItem(w,1020)) && IsWindowVisible(GetDlgItem(w,294)),
                        "Overlay controls cannot be reached by scrolling");
                for (int id : {1020,234}) {
                    auto clipped=CreateRectRgn(0,0,0,0);
                    RECT visiblePart{};
                    require(GetWindowRgn(GetDlgItem(w,id),clipped)!=ERROR && GetRgnBox(clipped,&visiblePart)!=NULLREGION && visiblePart.top>0,
                            "A list or dropdown overlaps the fixed header while scrolling");
                    DeleteObject(clipped);
                }
                click(w,103);
                SendMessageW(w,WM_VSCROLL,SB_TOP,0);
                SetWindowPos(w,nullptr,0,0,MulDiv(1100,dpi,96),MulDiv(920,dpi,96),SWP_NOZORDER|SWP_NOACTIVATE);
                keyboard_choice(GetDlgItem(w,177),1);
                click(w,222);
                require(!GetDlgItem(w,224) && !GetDlgItem(w,225),"The removed visibility checklist is still present");
                require(SendMessageW(GetDlgItem(w,1020),LB_GETCOUNT,0,0)==12,"Split customization has no editable rows");
                SetWindowTextW(GetDlgItem(w,294),L"Raditz · Arrival");click(w,1025);
                auto appearance=read_settings(settings_path());
                require(appearance.splitStyles.size()==1 && appearance.splitStyles[0].name=="Raditz · Arrival" &&
                            appearance.splitStyles[0].run=="any.txt","A split name did not save under its category");
                require(!std::filesystem::exists(settings_path().parent_path()/"splits"/"any.txt"),"Editing a split name changed recorded times");
                const auto userImage=root/L"Owned animation.gif";
                {std::ofstream out(userImage,std::ios::binary);out.write(reinterpret_cast<const char*>(openkai2::test_images::gif),sizeof(openkai2::test_images::gif));}
                drop_file(w,userImage);
                appearance=read_settings(settings_path());
                require(appearance.splitStyles.size()==1 && openkai2::speedrun::valid_image_file(appearance.splitStyles[0].image) &&
                            !appearance.splitStyles[0].image.empty() && std::filesystem::exists(userImage),"A user's GIF was not attached or its original was removed");
                const auto media=settings_path().parent_path()/"overlay"/"images"/appearance.splitStyles[0].image;
                require(openkai2::speedrun::load_split_image(media).frames.size()==4,"Imported GIF animation was lost");
                const auto protectedItems=openkai2::speedrun::overlay_bit(openkai2::speedrun::show_ps2_time)|openkai2::speedrun::overlay_bit(openkai2::speedrun::show_status);
                require((appearance.overlayItems&protectedItems)==protectedItems,"PS2 time or verification could be hidden by customization");
                click(w,223);keyboard_choice(GetDlgItem(w,177),3);click(w,222);
                wchar_t customName[200]{};GetWindowTextW(GetDlgItem(w,294),customName,200);
                require(!*customName && !IsWindowEnabled(GetDlgItem(w,1023)),"Tournament splits inherited Dragon World names or images");
                click(w,223);keyboard_choice(GetDlgItem(w,177),1);click(w,222);
                GetWindowTextW(GetDlgItem(w,294),customName,200);
                require(std::wstring(customName)==L"Raditz · Arrival","Returning to a category lost its split appearance");
                click(w,1023);
                require(read_settings(settings_path()).splitStyles[0].image.empty() && std::filesystem::exists(userImage),"Removing an image deleted the original");
                click(w,1024);click(w,1021);
                require(SendMessageW(GetDlgItem(w,1020),LB_GETCOUNT,0,0)==13,"A new split appearance row was not added");
                click(w,1024);
                require(SendMessageW(GetDlgItem(w,1020),LB_GETCOUNT,0,0)==12,"Resetting an added row left it behind");
                click(w,223);
                SetWindowTextW(GetDlgItem(w,264),L"1:00:00.123");
                click(w,266);
                require(SendMessageW(GetDlgItem(w,263),LB_GETCOUNT,0,0)==1 &&
                            IsWindowVisible(GetDlgItem(w,263)),"An added split stays hidden");
                SendMessageW(GetDlgItem(w,263),LB_SETCURSEL,0,0);
                click(w,267);
                require(!IsWindowVisible(GetDlgItem(w,263)),"The empty split list was not hidden after removal");
                require(SendMessageW(GetDlgItem(w,177),CB_GETCOUNT,0,0)==9,
                        "Published speedrun categories are missing");
                keyboard_choice(GetDlgItem(w,177),3);
                GetWindowTextW(rules,rulesText,4096);
                require(std::wstring(rulesText).find(L"F9")!=std::wstring::npos &&
                            std::wstring(rulesText).find(L"manual")!=std::wstring::npos &&
                            !IsWindowEnabled(GetDlgItem(w,178)),
                        "Tournament timing controls or permadeath state are incorrect");
                SetWindowTextW(GetDlgItem(w,264),L"0:20.123");click(w,266);
                require(SendMessageW(GetDlgItem(w,263),LB_GETCOUNT,0,0)==1,"Tournament split was not saved");
                keyboard_choice(GetDlgItem(w,177),4);
                require(SendMessageW(GetDlgItem(w,263),LB_GETCOUNT,0,0)==0,"Tournament difficulties share splits");
                keyboard_choice(GetDlgItem(w,177),3);
                require(SendMessageW(GetDlgItem(w,263),LB_GETCOUNT,0,0)==1,"Tournament split was lost");
                SendMessageW(GetDlgItem(w,263),LB_SETCURSEL,0,0);click(w,267);
                for(int category : {6,7,8}) {
                    keyboard_choice(GetDlgItem(w,177),category);
                    require(IsWindowEnabled(GetDlgItem(w,178)),"Dragon World permadeath remained disabled");
                    require(read_settings(settings_path()).speedrun==unsigned(category),
                            "New category preference was not persisted");
                }
                keyboard_choice(GetDlgItem(w,177),0);
                for(auto route : {std::pair<WORD,int>{'5',120},{'1',110},{'8',160},{'4',185},{'5',148},{'6',400},{'7',140},{'3',177},{'2',245}}) {
                    SetForegroundWindow(w);
                    INPUT keys[4]{};
                    for(auto& key:keys)key.type=INPUT_KEYBOARD;
                    keys[0].ki.wVk=VK_CONTROL;keys[1].ki.wVk=route.first;
                    keys[2].ki.wVk=route.first;keys[2].ki.dwFlags=KEYEVENTF_KEYUP;
                    keys[3].ki.wVk=VK_CONTROL;keys[3].ki.dwFlags=KEYEVENTF_KEYUP;
                    require(SendInput(4,keys,sizeof(INPUT))==4,"Cannot submit page shortcut");
                    wait_until([&]{return IsWindowVisible(GetDlgItem(w,route.second))!=FALSE;},
                        "Page shortcut did not navigate from the focused control");
                }
                click(w,101);click(w,1006);
                SendMessageW(GetDlgItem(w,295),LB_SETCURSEL,2,0);click(w,297);
                require(read_settings(settings_path()).tabOrder[3]==2,"Dock tabs cannot be moved freely across the old groups");
                key_press('4',VK_CONTROL);
                wait_until([&]{return IsWindowVisible(GetDlgItem(w,177));},"A moved tab's keyboard shortcut did not follow the dock");
                click(w,106);click(w,301);click(w,299);click(w,300);
                click(w,105);
                RECT previous{};
                for(int id : {121,120,122,127}) {
                    RECT bounds{},client{};GetWindowRect(GetDlgItem(w,id),&bounds);GetClientRect(w,&client);
                    MapWindowPoints(nullptr,w,reinterpret_cast<POINT*>(&bounds),2);
                    require(IsWindowVisible(GetDlgItem(w,id)) && bounds.left>=previous.right &&
                            bounds.right<=client.right && bounds.bottom<=client.bottom,
                            "Save tools overlap or are clipped at the minimum size");
                    previous=bounds;
                }
                require(!IsWindowEnabled(GetDlgItem(w,127)),"Unlocks were enabled without a verified disc");
                click(w,104);
                SetWindowTextW(GetDlgItem(w,186),L"Test mod");
                click(w,187);
                require(SendMessageW(GetDlgItem(w,180),LB_GETCOUNT,0,0)==1 && IsWindowVisible(GetDlgItem(w,180)),
                        "A created mod was not listed");
                SendMessageW(GetDlgItem(w,180),LB_SETCURSEL,0,0);
                click(w,181);
                const auto mods=openkai2::mods::scan_mods(openkai2::mods::default_mods_directory());
                require(mods.size()==1 && mods[0].name=="Test mod" && !mods[0].enabled,"Disabling a mod was not saved");
                RECT client{};GetClientRect(w,&client);
                auto inside=[&](int id,const char* message){
                    RECT bounds{};GetWindowRect(GetDlgItem(w,id),&bounds);
                    MapWindowPoints(nullptr,w,reinterpret_cast<POINT*>(&bounds),2);
                    require(IsWindowVisible(GetDlgItem(w,id)) && bounds.left>=0 && bounds.top>=0 &&
                            bounds.right<=client.right && bounds.bottom<=client.bottom,message);
                };
                for(int id=181;id<=192;++id) inside(id,"A mods control is clipped at the minimum window size");
                require(SendMessageW(GetDlgItem(w,189),LB_GETCOUNT,0,0)==1 && !IsWindowEnabled(GetDlgItem(w,192)),
                        "An empty mod's contents were not listed, or Remove character was enabled");
                SetWindowTextW(GetDlgItem(w,186),L"Renamed mod");
                click(w,190);
                require(openkai2::mods::scan_mods(openkai2::mods::default_mods_directory())[0].name=="Renamed mod",
                        "Renaming a mod was not saved");
                click(w,188);
                for(int id : {200,201,213,214,215,216,219,220})
                    inside(id,"An import tool control is clipped at the minimum window size");
                require(!IsWindowVisible(GetDlgItem(w,202)) && !IsWindowVisible(GetDlgItem(w,210)),
                        "Character lists were shown without a verified disc");
                require(!IsWindowEnabled(GetDlgItem(w,200)) && !IsWindowEnabled(GetDlgItem(w,220)),
                        "Reading or reporting was enabled without a disc");
                click(w,215);
                require(!IsWindowVisible(GetDlgItem(w,202)),"Stages showed the character list");
                click(w,214);
                click(w,213);
                require(IsWindowVisible(GetDlgItem(w,180))!=FALSE,"Back to mods did not return to the mods page");
            } catch (...) { failure=std::current_exception(); }
            if(w)PostMessageW(w,WM_CLOSE,0,0);
        });
        try { run_window(); } catch (...) { reopened.join();throw; }
        reopened.join();
        if(failure)std::rethrow_exception(failure);
        std::cout<<"Launcher native interaction passed: popup height, mouse/keyboard selection, background checks, persistence across restart, reset and minimum-size layout.\n";
        return 0;
    }catch(const std::exception &e) {std::cerr<<"::error title=Launcher interaction::"<<e.what()<<'\n';return 1;}
}
