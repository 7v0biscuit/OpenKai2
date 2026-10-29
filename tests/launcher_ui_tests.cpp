// SPDX-License-Identifier: GPL-3.0-only
#include "launcher/settings.h"
#include "launcher/window.h"
#include "launcher/update.h"
#include "mods/mod_library.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>

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
void mouse_click(HWND control, int x, int y) {
    POINT point{x,y}; ClientToScreen(control,&point);
    // Consecutive test actions are independent clicks, not a double-click on
    // the same arrow (which native combo boxes interpret differently).
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
    // A real click also positions the pointer and focuses the field. Direct
    // CB_SHOWDROPDOWN can leave focus in the previous combo on Windows.
    SetForegroundWindow(GetAncestor(combo,GA_ROOT));
    RECT field{};GetClientRect(combo,&field);
    mouse_click(combo,field.right-10,(field.bottom-field.top)/2);
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!SendMessageW(combo,CB_GETDROPPEDSTATE,0,0) &&
          std::chrono::steady_clock::now()<deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if(!SendMessageW(combo,CB_GETDROPPEDSTATE,0,0))
        throw std::runtime_error("Dropdown did not open: "+std::to_string(GetDlgCtrlID(combo)));
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
    const auto key=[](WORD code) {
        INPUT events[2]{};
        for(auto &event:events){event.type=INPUT_KEYBOARD;event.ki.wVk=code;}
        events[1].ki.dwFlags=KEYEVENTF_KEYUP;
        require(SendInput(2,events,sizeof(INPUT))==2,"Cannot submit native keyboard input");
    };
    key(VK_HOME);
    wait_until([&]{return SendMessageW(combo,CB_GETCURSEL,0,0)==0;},"Home did not select the first option");
    for(int i=0;i<index;++i) {
        key(VK_DOWN);
        wait_until([&]{return SendMessageW(combo,CB_GETCURSEL,0,0)==i+1;},"Arrow did not select the next option");
    }
    require(SendMessageW(combo,CB_GETDROPPEDSTATE,0,0)!=0,
            "Changing a setting closed the dropdown before confirmation");
    key(VK_RETURN);
    wait_until([&]{return !SendMessageW(combo,CB_GETDROPPEDSTATE,0,0);},"Confirm did not close the popup");
    const auto actual=SendMessageW(combo,CB_GETCURSEL,0,0);
    if(actual!=index)throw std::runtime_error("Native keyboard selection: control="+
        std::to_string(GetDlgCtrlID(combo))+" expected="+std::to_string(index)+
        " actual="+std::to_string(actual));
}
}
// Only the network boundary is substituted. Controls, notifications, files and
// the launcher event loop are real; release parsing and the Windows worker have
// their own integration checks. A stalled check must not lock out the library.
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
        write_settings(settings_path(),initial);
        std::exception_ptr failure;
        std::thread driver([&] {
            HWND w{};
            try {
                w=wait_launcher();
                wait_until([] { return updateStarted.load(); }, "Background update did not start");
                require(IsWindowEnabled(GetDlgItem(w,110)) != FALSE,
                        "Background update check blocked choosing an ISO");
                click(w,104);
                mouse_choice(GetDlgItem(w,174),1);
                require(read_settings(settings_path()).discordPresence,"Discord preference was not saved");
                mouse_choice(GetDlgItem(w,174),0);
                require(!read_settings(settings_path()).discordPresence,"Discord preference did not switch off");
                for(int id=140;id<=150;++id) {
                    if(id==148)continue; // Session backups live on the Saves page.
                    auto combo=GetDlgItem(w,id);
                    open_dropdown(combo);
                    SendMessageW(combo,CB_SHOWDROPDOWN,FALSE,0);
                }
                keyboard_choice(GetDlgItem(w,141),3); // 120 FPS
                require(read_settings(settings_path()).fps==120,"FPS was not persisted by native interaction");
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
                click(w,102);
                keyboard_choice(GetDlgItem(w,148),0);
                require(!read_settings(settings_path()).autoBackups,"Backup preference did not persist");
                releaseUpdate = true;
                click(w,104);
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
                w = wait_launcher(); click(w,104);
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
                require(!reset.discordPresence,"Reset graphics changed the Discord preference");
                require(reset.audio && reset.volume==50,"Reset graphics changed the audio preference");
                SetWindowPos(w,nullptr,0,0,1000,680,SWP_NOZORDER|SWP_NOACTIVATE);
                for(int id : {140,141,142,143,144,145,146,147,149,150,160,161,162,163,164,170,173,174}) {
                    RECT bounds{},client{};GetWindowRect(GetDlgItem(w,id),&bounds);GetClientRect(w,&client);
                    MapWindowPoints(nullptr,w,reinterpret_cast<POINT*>(&bounds),2);
                    require(bounds.left>=0 && bounds.top>=0 && bounds.right<=client.right &&
                            bounds.bottom<=client.bottom,"A settings control is clipped at the minimum window size");
                }
                // Navigation shortcuts must work while a native field has focus.
                for(auto route : {std::pair<WORD,int>{'2',120},{'1',110},{'4',160},{'3',185},{'2',148},{'4',140}}) {
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
                click(w,102);
                RECT previous{};
                for(int id : {121,120,122,124,127}) {
                    RECT bounds{},client{};GetWindowRect(GetDlgItem(w,id),&bounds);GetClientRect(w,&client);
                    MapWindowPoints(nullptr,w,reinterpret_cast<POINT*>(&bounds),2);
                    require(IsWindowVisible(GetDlgItem(w,id)) && bounds.left>=previous.right &&
                            bounds.right<=client.right && bounds.bottom<=client.bottom,
                            "Save tools overlap or are clipped at the minimum size");
                    previous=bounds;
                }
                require(!IsWindowEnabled(GetDlgItem(w,127)),"Unlocks were enabled without a verified disc");
                // Mods: create one, disable it, and lay out the import tool at the minimum size.
                click(w,103);
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
                for(int id=181;id<=188;++id) inside(id,"A mods control is clipped at the minimum window size");
                click(w,188);
                for(int id : {200,201,205,206,207,208,209,210,211,212,213})
                    inside(id,"An import tool control is clipped at the minimum window size");
                require(!IsWindowEnabled(GetDlgItem(w,210)) && !IsWindowEnabled(GetDlgItem(w,200)),
                        "Importing was enabled without a source and a verified disc");
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
