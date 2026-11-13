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
#include "win32_test_crash.h"
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
#include <set>
#include <cwchar>

using namespace openkai2::launcher;
namespace {
HWND found{};
std::atomic<bool> updateStarted{false}, releaseUpdate{false};
std::atomic<int> paintInterruptions{0};
std::atomic<HWND> paintStressWindow{nullptr};
LRESULT CALLBACK interrupt_paint(int code,WPARAM wp,LPARAM lp) {
    static thread_local bool injecting=false;
    const auto* event=reinterpret_cast<const CWPSTRUCT*>(lp);
    const auto window=paintStressWindow.load();
    if (code>=0 && !injecting && window && event->hwnd!=window &&
        GetAncestor(event->hwnd,GA_ROOT)==window &&
        GetPropW(window,L"OpenKai2.Painting") && paintInterruptions.load()<12) {
        const int step=paintInterruptions.fetch_add(1);
        injecting=true;
        // Interrupt the canvas at a real native-control call, as a sent resize,
        // navigation or monitor-DPI message can do on Windows.
        SetWindowPos(window,nullptr,0,0,step%2 ? 1440 : 960,step%2 ? 960 : 600,
                     SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
        RECT proposed{};GetWindowRect(window,&proposed);
        SendMessageW(window,WM_DPICHANGED,MAKEWPARAM(96,96),reinterpret_cast<LPARAM>(&proposed));
        proposed={}; // The deferred DPI message must have copied its temporary RECT.
        const int page=step%2 ? 106 : 103;
        SendMessageW(window,WM_COMMAND,MAKEWPARAM(page,BN_CLICKED),LPARAM(GetDlgItem(window,page)));
        SendMessageW(window,WM_VSCROLL,SB_BOTTOM,0);
        RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW);
        injecting=false;
    }
    return CallNextHookEx(nullptr,code,wp,lp);
}
BOOL CALLBACK find_launcher(HWND w, LPARAM) {
    DWORD pid=0;GetWindowThreadProcessId(w,&pid);
    wchar_t type[100]{};GetClassNameW(w,type,100);
    if(pid==GetCurrentProcessId() && std::wstring(type)==L"OpenKai2.Launcher") {found=w;return FALSE;}
    return TRUE;
}
void require(bool condition,const char *message) {
    if(!condition)throw std::runtime_error(message);
}
RECT visible_bounds(HWND window, HWND control) {
    RECT bounds{};
    GetWindowRect(control,&bounds);
    MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&bounds),2);
    const auto region=CreateRectRgn(0,0,0,0);
    if(GetWindowRgn(control,region)!=ERROR) {
        RECT clipped{};
        const auto kind=GetRgnBox(region,&clipped);
        DeleteObject(region);
        if(kind==ERROR || kind==NULLREGION)return {};
        OffsetRect(&clipped,bounds.left,bounds.top);
        IntersectRect(&bounds,&bounds,&clipped);
    } else DeleteObject(region);
    return bounds;
}
HWND wait_launcher() {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);
    while(std::chrono::steady_clock::now()<deadline) {
        EnumWindows(find_launcher,0);
        if(found && IsWindowVisible(found) && GetDlgItem(found,149)) {
            const auto user=GetModuleHandleW(L"user32.dll");
            const auto windowContext=reinterpret_cast<HANDLE(WINAPI*)(HWND)>(GetProcAddress(user,"GetWindowDpiAwarenessContext"));
            const auto threadContext=reinterpret_cast<HANDLE(WINAPI*)(HANDLE)>(GetProcAddress(user,"SetThreadDpiAwarenessContext"));
            if(windowContext && threadContext)threadContext(windowContext(found));
            return found;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    throw std::runtime_error("Launcher window did not become ready");
}
void click(HWND w,int id) {
    require(GetDlgItem(w,id)!=nullptr,"Missing launcher action");
    SendMessageW(GetDlgItem(w,id),BM_CLICK,0,0);
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while (GetPropW(w,L"OpenKai2.PendingUi") && std::chrono::steady_clock::now()<deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    require(!GetPropW(w,L"OpenKai2.PendingUi"),"A launcher action remained queued after painting");
}
void reveal_control(HWND control) {
    const auto window = GetAncestor(control,GA_ROOT);
    const auto dpiForWindow = reinterpret_cast<UINT(WINAPI *)(HWND)>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"GetDpiForWindow"));
    const int dpi = dpiForWindow ? int(dpiForWindow(window)) : 96;
    for (int attempt=0;attempt<100;++attempt) {
        RECT bounds{},client{};GetWindowRect(control,&bounds);GetClientRect(window,&client);
        MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&bounds),2);
        if (IsWindowVisible(control) && bounds.top>=MulDiv(76,dpi,96) && bounds.bottom<=client.bottom-MulDiv(116,dpi,96)) return;
        const WPARAM direction = bounds.top<MulDiv(76,dpi,96) ? SB_LINEUP : SB_LINEDOWN;
        SCROLLINFO before{sizeof(before),SIF_POS},after{sizeof(after),SIF_POS};
        GetScrollInfo(window,SB_VERT,&before);SendMessageW(window,WM_VSCROLL,direction,0);GetScrollInfo(window,SB_VERT,&after);
        if(before.nPos==after.nPos) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    RECT bounds{},client{};GetWindowRect(control,&bounds);GetClientRect(window,&client);
    MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&bounds),2);
    SCROLLINFO scroll{sizeof(scroll),SIF_ALL};GetScrollInfo(window,SB_VERT,&scroll);
    throw std::runtime_error("Cannot scroll to control "+std::to_string(GetDlgCtrlID(control))+
        " visible="+std::to_string(IsWindowVisible(control))+" client="+std::to_string(client.right)+","+std::to_string(client.bottom)+
        " bounds="+std::to_string(bounds.left)+","+std::to_string(bounds.top)+","+std::to_string(bounds.right)+","+std::to_string(bounds.bottom)+
        " scroll="+std::to_string(scroll.nPos)+" range="+std::to_string(scroll.nMax));
}
POINT control_center(HWND window,int id) {
    const auto bounds=visible_bounds(window,GetDlgItem(window,id));
    require(bounds.right>bounds.left && bounds.bottom>bounds.top,"Wheel target is not visible");
    POINT point{(bounds.left+bounds.right)/2,(bounds.top+bounds.bottom)/2};
    ClientToScreen(window,&point);
    require(SetCursorPos(point.x,point.y)!=FALSE,"Cannot position the wheel over a page control");
    return point;
}
void wheel_over(HWND window,int id,short delta) {
    const auto point=control_center(window,id);
    SendMessageW(window,WM_MOUSEWHEEL,MAKEWPARAM(0,WORD(delta)),MAKELPARAM(point.x,point.y));
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
HWND image_picker(const wchar_t* title) {
    std::pair<const wchar_t*,HWND> picker{title,nullptr};
    EnumWindows([](HWND candidate,LPARAM result)->BOOL {
        DWORD pid{};GetWindowThreadProcessId(candidate,&pid);
        wchar_t title[128]{};GetWindowTextW(candidate,title,128);
        if(pid==GetCurrentProcessId() && std::wstring(title)==reinterpret_cast<std::pair<const wchar_t*,HWND>*>(result)->first && IsWindowVisible(candidate)) {
            reinterpret_cast<std::pair<const wchar_t*,HWND>*>(result)->second=candidate;return FALSE;
        }
        return TRUE;
    },reinterpret_cast<LPARAM>(&picker));
    return picker.second;
}
void choose_image(HWND window,const std::filesystem::path& path,int action=1022,const wchar_t* title=L"Choose a split image") {
    require(IsWindowEnabled(GetDlgItem(window,action)),"Choose image is disabled for an active category");
    PostMessageW(GetDlgItem(window,action),BM_CLICK,0,0);
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
    HWND picker{};
    while(!(picker=image_picker(title)) && std::chrono::steady_clock::now()<deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    require(picker!=nullptr,"Choose image did not open the native file picker");
    struct ClosePicker {
        HWND window;
        ~ClosePicker(){if(IsWindow(window))PostMessageW(window,WM_CLOSE,0,0);}
    } closePicker{picker};
    if(GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"wine_get_version")) {
        // This portable Wine environment has no working shell file browser.
        // Native Windows runs exercise file selection; Wine checks opening and cancellation.
        PostMessageW(picker,WM_CLOSE,0,0);
        while(image_picker(title) && std::chrono::steady_clock::now()<deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        require(!image_picker(title),"The image picker could not be cancelled");
        SendMessageW(window,WM_NULL,0,0);
        std::cout<<"Wine shell: checked picker opening/cancellation; importing through file drop.\n";
        drop_file(window,path);
        return;
    }
    SetForegroundWindow(picker);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    key_press('N',VK_MENU);
    key_press('A',VK_CONTROL);
    // Alt+N focuses File name in both the modern and fallback Windows pickers.
    std::vector<INPUT> input;
    for(wchar_t ch:path.wstring()) {
        INPUT down{};down.type=INPUT_KEYBOARD;down.ki.wScan=ch;down.ki.dwFlags=KEYEVENTF_UNICODE;
        input.push_back(down);down.ki.dwFlags|=KEYEVENTF_KEYUP;input.push_back(down);
    }
    require(SendInput(UINT(input.size()),input.data(),sizeof(INPUT))==input.size(),"Cannot enter an image path");
    // Wait for queued keyboard input before activating the dialog's Open action.
    const auto expected=path.wstring();
    bool entered=false;
    while(!entered && std::chrono::steady_clock::now()<deadline) {
        std::pair<const std::wstring*,bool> state{&expected,false};
        EnumChildWindows(picker,[](HWND child,LPARAM value)->BOOL {
            auto& state=*reinterpret_cast<std::pair<const std::wstring*,bool>*>(value);
            wchar_t cls[32]{},text[32768]{};GetClassNameW(child,cls,32);
            if(_wcsicmp(cls,L"Edit")==0) {
                GetWindowTextW(child,text,32768);
                if(text==*state.first)state.second=true;
            }
            return TRUE;
        },reinterpret_cast<LPARAM>(&state));
        entered=state.second;
        if(!entered)std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    require(entered,"The image path did not reach the file-name field");
    HWND open{};
    EnumChildWindows(picker,[](HWND child,LPARAM result)->BOOL {
        wchar_t cls[32]{};GetClassNameW(child,cls,32);
        if(GetDlgCtrlID(child)==IDOK && _wcsicmp(cls,L"Button")==0) {
            *reinterpret_cast<HWND*>(result)=child;return FALSE;
        }
        return TRUE;
    },reinterpret_cast<LPARAM>(&open));
    require(open!=nullptr,"The image picker has no Open button");
    PostMessageW(open,BM_CLICK,0,0);
    while(image_picker(title) && std::chrono::steady_clock::now()<deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if(image_picker(title)) {
        PostMessageW(picker,WM_CLOSE,0,0);
        throw std::runtime_error("The native image picker did not accept the selected file");
    }
    // Wait for the UI thread to finish importing after the modal dialog closes.
    SendMessageW(window,WM_NULL,0,0);
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
    reveal_control(combo);
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
        GUITHREADINFO gui{sizeof(gui)};GetGUIThreadInfo(GetWindowThreadProcessId(combo,nullptr),&gui);
        std::cerr<<"[ui:test] dropdown focus="<<GetDlgCtrlID(gui.hwndFocus)<<" capture="<<GetDlgCtrlID(gui.hwndCapture)
                 <<" active="<<(gui.hwndActive==GetAncestor(combo,GA_ROOT))<<" flags="<<gui.flags
                 <<" left_down="<<bool(GetAsyncKeyState(VK_LBUTTON)&0x8000)<<"\n";
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
LRESULT CALLBACK focus_target_procedure(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    if(message==WM_APP+1) {AllowSetForegroundWindow(DWORD(wp));return 0;}
    if(message==WM_DESTROY) {PostQuitMessage(0);return 0;}
    return DefWindowProcW(window,message,wp,lp);
}
int run_focus_target() {
    WNDCLASSW type{};type.lpfnWndProc=focus_target_procedure;type.hInstance=GetModuleHandleW(nullptr);
    type.lpszClassName=L"OpenKai2.TestFocus";type.hbrBackground=HBRUSH(COLOR_WINDOW+1);
    RegisterClassW(&type);
    auto window=CreateWindowW(type.lpszClassName,L"Launcher focus test",WS_OVERLAPPEDWINDOW,
        20,20,520,260,nullptr,nullptr,type.hInstance,nullptr);
    if(!window)return 2;
    ShowWindow(window,SW_SHOW);SetForegroundWindow(window);
    MSG message{};while(GetMessageW(&message,nullptr,0,0)>0){TranslateMessage(&message);DispatchMessageW(&message);}
    return 0;
}
struct FocusTarget {
    PROCESS_INFORMATION process{};
    HWND window{};
    FocusTarget() {
        wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);
        auto command=quote_argument(executable)+L" --focus-target";
        STARTUPINFOW startup{};startup.cb=sizeof(startup);
        require(CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,
                              nullptr,nullptr,&startup,&process)!=FALSE,"Cannot start the focus test window");
        try {
            wait_until([&]{window=FindWindowW(L"OpenKai2.TestFocus",nullptr);DWORD pid=0;
                if(window)GetWindowThreadProcessId(window,&pid);return window && pid==process.dwProcessId;},
                "Focus test window did not open");
        } catch(...) {TerminateProcess(process.hProcess,2);CloseHandle(process.hThread);CloseHandle(process.hProcess);throw;}
    }
    void away() {
        SetForegroundWindow(window);
        wait_until([&]{return GetForegroundWindow()==window;},"Cannot switch away from the launcher");
    }
    void back(HWND launcher) {
        SendMessageW(window,WM_APP+1,GetCurrentProcessId(),0);
        SetForegroundWindow(launcher);
        wait_until([&]{return GetForegroundWindow()==launcher;},"Cannot return to the launcher");
        SendMessageW(launcher,WM_NULL,0,0);
    }
    ~FocusTarget() {
        if(window)PostMessageW(window,WM_CLOSE,0,0);
        if(WaitForSingleObject(process.hProcess,3000)==WAIT_TIMEOUT)TerminateProcess(process.hProcess,2);
        CloseHandle(process.hThread);CloseHandle(process.hProcess);
    }
};
void check_shell_alignment(HWND launcher) {
    const auto dpiForWindow=reinterpret_cast<UINT(WINAPI*)(HWND)>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"GetDpiForWindow"));
    const int dpi=dpiForWindow?int(dpiForWindow(launcher)):96;
    RECT client{},field{};GetClientRect(launcher,&client);GetWindowRect(GetDlgItem(launcher,1210),&field);
    MapWindowPoints(nullptr,launcher,reinterpret_cast<POINT*>(&field),2);
    require(std::abs(field.right-(client.right-MulDiv(36,dpi,96)))<=1,
            "Scrollbar changes left the launcher header at a stale width");
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
int main(int argc,char** argv) {
    if(argc==2 && std::string(argv[1])=="--focus-target")return run_focus_target();
    enable_test_crash_trace();
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
                std::cout<<"[ui:stage] lounge layout and filtering"<<std::endl;
                wait_until([] { return updateStarted.load(); }, "Background update did not start");
                require(IsWindowEnabled(GetDlgItem(w,110)) != FALSE,
                        "Background update check blocked choosing an ISO");
                require(!IsWindowVisible(GetDlgItem(w,1100)),"The header action search bar is still shown");
                {
                    const auto field=GetDlgItem(w,140);const auto dc=GetDC(field);
                    const auto previous=SelectObject(dc,reinterpret_cast<HFONT>(SendMessageW(field,WM_GETFONT,0,0)));
                    wchar_t face[80]{};GetTextFaceW(dc,80,face);TEXTMETRICW metrics{};GetTextMetricsW(dc,&metrics);SelectObject(dc,previous);ReleaseDC(field,dc);
                    const auto windowDpi=reinterpret_cast<UINT(WINAPI*)(HWND)>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"GetDpiForWindow"));
                    require(metrics.tmHeight<=MulDiv(22,windowDpi?windowDpi(w):96,96),"Launcher font line metrics exceed the field height");
                    require(std::wstring(face)==L"OpenKai2 Display","The Budokai-style launcher font was not loaded");
                }
                key_press(VK_NEXT,VK_CONTROL);wait_until([&]{return IsWindowVisible(GetDlgItem(w,246));},"Next-tab shortcut did not open Online");
                key_press(VK_PRIOR,VK_CONTROL);wait_until([&]{return IsWindowVisible(GetDlgItem(w,112));},"Previous-tab shortcut did not return to Play");
                key_press(VK_F1);wait_until([&]{return IsWindowVisible(GetDlgItem(w,1013));},"F1 did not open Help");
                key_press(VK_END,VK_CONTROL);reveal_control(GetDlgItem(w,1248));click(w,1248);
                std::wstring summary;
                wait_until([&] {
                    if(!OpenClipboard(nullptr))return false;
                    const auto data=GetClipboardData(CF_UNICODETEXT);
                    if(data)if(const auto value=static_cast<const wchar_t*>(GlobalLock(data))) {summary=value;GlobalUnlock(data);}
                    CloseClipboard();return summary.find(L"OpenKai2 v")!=std::wstring::npos;
                },"The setup summary was not copied");
                require(summary.find(L"Renderer:")!=std::wstring::npos && summary.find(L"UI test")==std::wstring::npos && summary.find(std::wstring(64,L'a'))==std::wstring::npos &&
                        summary.find(L":\\")==std::wstring::npos,"The support summary is incomplete or contains private profile data");
                key_press(VK_LEFT,VK_MENU);wait_until([&]{return IsWindowVisible(GetDlgItem(w,112));},"Help did not return to Play");
                key_press(VK_F11);wait_until([&]{return IsZoomed(w)!=FALSE;},"F11 did not maximize the launcher");
                key_press(VK_F11);wait_until([&]{return IsZoomed(w)==FALSE;},"F11 did not restore the launcher");

                std::cout<<"[ui:stage] resize and application focus recovery"<<std::endl;
                {
                    std::cout<<"[ui:stage] navigation and DPI changes during painting"<<std::endl;
                    click(w,106);
                    paintStressWindow=w;paintInterruptions=0;
                    const auto hook=SetWindowsHookExW(WH_CALLWNDPROC,interrupt_paint,nullptr,GetWindowThreadProcessId(w,nullptr));
                    require(hook!=nullptr,"Cannot install the native repaint stress hook");
                    struct Unhook {HHOOK hook;~Unhook(){UnhookWindowsHookEx(hook);paintStressWindow=nullptr;}} unhook{hook};
                    RedrawWindow(w,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW);
                    wait_until([&]{return paintInterruptions.load()==12 && !GetPropW(w,L"OpenKai2.PendingUi");},
                               "Reentrant resize, navigation and DPI changes did not finish");
                    SendMessageW(w,WM_VSCROLL,SB_TOP,0);
                    wait_until([&]{return IsWindowVisible(GetDlgItem(w,1200));},"The final deferred navigation was lost");
                    RECT bounds{};GetWindowRect(w,&bounds);
                    require(bounds.right-bounds.left>=960 && bounds.bottom-bounds.top>=600,
                            "A deferred DPI change retained an expired rectangle");
                }
                click(w,101);
                for(int height:{600,960,600,960}) {
                    SetWindowPos(w,nullptr,0,0,1440,height,SWP_NOZORDER|SWP_NOACTIVATE);
                    SendMessageW(w,WM_NULL,0,0);check_shell_alignment(w);
                }
                {
                    FocusTarget other;other.back(w);
                    for(int page:{106,103,102,101}) {
                        click(w,page);SendMessageW(w,WM_VSCROLL,SB_BOTTOM,0);
                        SCROLLINFO before{sizeof(before),SIF_POS},after{sizeof(after),SIF_POS};GetScrollInfo(w,SB_VERT,&before);
                        other.away();other.back(w);
                        GetScrollInfo(w,SB_VERT,&after);
                        require(before.nPos==after.nPos,"Switching applications lost the launcher scroll position");
                        check_shell_alignment(w);
                    }
                    click(w,106);
                    const auto fps=GetDlgItem(w,141);open_dropdown(fps);key_press(VK_HOME);
                    wait_until([&]{return SendMessageW(fps,CB_GETCURSEL,0,0)==0;},"The dropdown preview did not receive Home");
                    SendMessageW(w,WM_NCACTIVATE,FALSE,0);
                    require(!SendMessageW(fps,CB_GETDROPPEDSTATE,0,0) && read_settings(settings_path()).fps==240,
                            "Nonclient deactivation committed a dropdown preview");
                    SendMessageW(w,WM_NCACTIVATE,TRUE,0);
                    open_dropdown(fps);key_press(VK_HOME);
                    wait_until([&]{return SendMessageW(fps,CB_GETCURSEL,0,0)==0;},"The focus test did not receive its dropdown preview");
                    other.away();other.back(w);
                    require(!SendMessageW(fps,CB_GETDROPPEDSTATE,0,0) && read_settings(settings_path()).fps==240,
                            "Switching applications saved an unconfirmed dropdown choice or left its popup open");
                    SendMessageW(w,WM_VSCROLL,SB_BOTTOM,0);SCROLLINFO before{sizeof(before),SIF_POS},after{sizeof(after),SIF_POS};
                    GetScrollInfo(w,SB_VERT,&before);ShowWindow(w,SW_MINIMIZE);
                    wait_until([&]{return IsIconic(w)!=FALSE;},"Launcher did not minimize");
                    ShowWindow(w,SW_RESTORE);other.back(w);GetScrollInfo(w,SB_VERT,&after);
                    require(before.nPos==after.nPos,"Minimizing lost the launcher scroll position");check_shell_alignment(w);
                    click(w,101);
                }
                for (auto size : {std::pair{960,600},std::pair{1000,680},std::pair{1440,960}}) {
                    SetWindowPos(w,nullptr,0,0,size.first,size.second,SWP_NOZORDER|SWP_NOACTIVATE);
                    RECT client{};GetClientRect(w,&client);
                    for (int id=101;id<=108;++id) {
                        auto tab=GetDlgItem(w,id);RECT bounds{};GetWindowRect(tab,&bounds);
                        if (id==105 || id==108) {require(!IsWindowVisible(tab),"A Play tool remains in the dock");continue;}
                        MapWindowPoints(nullptr,w,reinterpret_cast<POINT*>(&bounds),2);
                        require(IsWindowVisible(tab) && bounds.left>=0 && bounds.right<=client.right && bounds.top>=0 && bounds.bottom<=client.bottom-26,
                                "A dock tab extends beyond the launcher border");
                        wchar_t label[64]{};GetWindowTextW(tab,label,64);auto dc=GetDC(tab);
                        auto old=SelectObject(dc,reinterpret_cast<HFONT>(SendMessageW(tab,WM_GETFONT,0,0)));SIZE text{};
                        GetTextExtentPoint32W(dc,label,int(wcslen(label)),&text);SelectObject(dc,old);ReleaseDC(tab,dc);
                        require(text.cx+8<=bounds.right-bounds.left,"A dock tab's label is clipped");
                    }
                }
                for(auto size:{std::pair{960,680},std::pair{1440,960}}) {
                    SetWindowPos(w,nullptr,0,0,size.first,size.second,SWP_NOZORDER|SWP_NOACTIVATE);
                    for(int id:{1000,1003,1004,1060,1061,1062,1063,1042,1043,1045,111,112,256,114}) {
                        reveal_control(GetDlgItem(w,id));
                        RECT bounds{},client{};GetWindowRect(GetDlgItem(w,id),&bounds);GetClientRect(w,&client);
                        MapWindowPoints(nullptr,w,reinterpret_cast<POINT*>(&bounds),2);
                        require(bounds.left>=0 && bounds.right<=client.right,("A lounge control leaves the window: "+std::to_string(id)).c_str());
                    }
                    SendMessageW(w,WM_VSCROLL,SB_TOP,0);
                }
                RedrawWindow(w,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW|RDW_ALLCHILDREN);
                auto edgeDc=GetDC(GetDlgItem(w,1043));std::set<COLORREF> edgeColors;
                for(int y=0;y<10;++y)for(int x=0;x<10;++x)edgeColors.insert(GetPixel(edgeDc,x,y));
                ReleaseDC(GetDlgItem(w,1043),edgeDc);
                require(edgeColors.size()>5,"Rounded launcher edges have no fractional color coverage");
                require(!IsWindowEnabled(GetDlgItem(w,256)),"Join is enabled with no room selected");
                require(!IsWindowEnabled(GetDlgItem(w,1062)),"Invite is enabled with no online friend selected");
                wchar_t isoAction[40]{};GetWindowTextW(GetDlgItem(w,111),isoAction,40);
                require(std::wstring(isoAction)==L"Verify ISO" && !IsWindowVisible(GetDlgItem(w,110)),
                        "The Play page still exposes Change ISO");
                key_press('K',VK_CONTROL);
                wait_until([&]{return IsWindowVisible(GetDlgItem(w,1101));},"Ctrl+K did not open the action finder");
                SetWindowTextW(GetDlgItem(w,1101),L"controls");
                require(SendMessageW(GetDlgItem(w,1102),LB_GETCOUNT,0,0)==1,"Action search did not narrow the results");
                key_press(VK_RETURN);
                wait_until([&]{return IsWindowVisible(GetDlgItem(w,1105));},"The action finder did not open Controls");
                const auto beforeCancel=read_settings(settings_path()).controls;
                click(w,410);key_press(VK_ESCAPE);
                wait_until([&]{wchar_t label[100]{};GetWindowTextW(GetDlgItem(w,410),label,100);return std::wstring(label).find(L"Press a key")==std::wstring::npos;},"Escape did not cancel binding capture");
                require(read_settings(settings_path()).controls.keys==beforeCancel.keys,"Canceling capture changed a binding");
                for(int id=450;id<466;++id) {
                    const auto control=GetDlgItem(w,id);reveal_control(control);
                    wchar_t label[200]{};GetWindowTextW(control,label,200);RECT bounds{};GetClientRect(control,&bounds);
                    auto dc=GetDC(control);auto old=SelectObject(dc,reinterpret_cast<HFONT>(SendMessageW(control,WM_GETFONT,0,0)));
                    SIZE extent{};GetTextExtentPoint32W(dc,label,int(wcslen(label)),&extent);SelectObject(dc,old);ReleaseDC(control,dc);
                    require(extent.cx+40<=bounds.right,"A controller binding label is clipped");
                }
                click(w,1105);
                require(IsWindowVisible(GetDlgItem(w,1004)) && !IsWindowVisible(GetDlgItem(w,108)),
                        "Back to Play restored a removed dock tab");
                keyboard_choice(GetDlgItem(w,1060),1);keyboard_choice(GetDlgItem(w,1060),0);
                SetWindowTextW(GetDlgItem(w,1042),L"Kame");click(w,1045);
                require(!IsWindowEnabled(GetDlgItem(w,1045)),"The lounge search did not clear");
                SetWindowPos(w,nullptr,0,0,1280,900,SWP_NOZORDER|SWP_NOACTIVATE);
                require(GetDlgItem(w,1002)==nullptr,"Settings is duplicated in the header");
                require(GetDlgItem(w,1001)==nullptr,"Profile is still in the header");
                HWND hints{};
                while ((hints=FindWindowExW(nullptr,hints,TOOLTIPS_CLASSW,nullptr)))
                    if (GetWindow(hints,GW_OWNER)==w)break;
                require(hints!=nullptr,"Launcher hover hints are missing");
                wchar_t hint[512]{};
                TOOLINFOW tool{TTTOOLINFOW_V2_SIZE};tool.uFlags=TTF_IDISHWND;tool.hwnd=w;
                tool.uId=reinterpret_cast<UINT_PTR>(GetDlgItem(w,101));tool.lpszText=hint;
                SendMessageW(hints,TTM_GETTEXTW,512,reinterpret_cast<LPARAM>(&tool));
                require(std::wstring(hint).find(L"Ctrl+1")!=std::wstring::npos,"Dock hints do not show the matching keyboard shortcut");
                click(w,1000);
                require(IsWindowVisible(GetDlgItem(w,1007)),"Profile customization did not open");
                {
                    const auto combo=GetDlgItem(w,1009);RECT field{};GetClientRect(combo,&field);
                    const auto palette=make_theme(initial.launcherTheme,initial.launcherAccent,initial.launcherColors);
                    const auto dc=GetDC(combo);const auto pixel=GetPixel(dc,field.right/2,field.bottom/2);ReleaseDC(combo,dc);
                    require(pixel==RGB(palette.field.r,palette.field.g,palette.field.b),
                            "Changing pages exposed a control background from the previous page");
                }

                SetWindowTextW(GetDlgItem(w,1007),L"UT");SetWindowTextW(GetDlgItem(w,1008),L"Local appearance");
                keyboard_choice(GetDlgItem(w,1009),3);key_press('S',VK_CONTROL);
                wait_until([&]{const auto saved=read_settings(settings_path());return !saved.profileStyles.empty() && saved.profileStyles[0].tagline=="Local appearance";},"Ctrl+S did not save the profile");
                const auto style=read_settings(settings_path());
                require(style.profileStyles.size()==1 && style.profileStyles[0].initials=="UT" &&
                            style.profileStyles[0].tagline=="Local appearance" && style.profileStyles[0].accent==3 &&
                            style.onlineKeys==initial.onlineKeys && style.onlineName==initial.onlineName,
                        "Profile customization changed identity or did not persist");
                {
                    const auto tab=GetDlgItem(w,101);RECT bounds{};GetClientRect(tab,&bounds);
                    const auto palette=make_theme(initial.launcherTheme,initial.launcherAccent,initial.launcherColors);
                    const auto dc=GetDC(tab);const auto pixel=GetPixel(dc,bounds.right/2,bounds.bottom-6);ReleaseDC(tab,dc);
                    require(pixel!=RGB(palette.accent.r,palette.accent.g,palette.accent.b),"The selected dock tab still has an underline");
                }
                std::cout<<"[ui:stage] profile image selection"<<std::endl;
                const auto portrait=root/L"My profile animation.gif";
                {std::ofstream out(portrait,std::ios::binary);out.write(reinterpret_cast<const char*>(openkai2::test_images::gif),sizeof(openkai2::test_images::gif));}
                choose_image(w,portrait,1040,L"Choose a profile picture");
                auto picture=read_settings(settings_path());
                require(picture.profileStyles.size()==1 && !picture.profileStyles[0].image.empty() &&
                        picture.profileStyles[0].imageName=="My profile animation.gif" && std::filesystem::exists(portrait),
                        "Choosing a profile picture failed or removed the original");
                const auto copied=settings_path().parent_path()/"profiles"/"images"/picture.profileStyles[0].image;
                require(openkai2::speedrun::load_split_image(copied).frames.size()==4,"A profile GIF lost its animation");
                click(w,1010);
                require(read_settings(settings_path()).profileStyles[0].image==picture.profileStyles[0].image,
                        "Saving profile text discarded its picture");
                click(w,1041);
                require(read_settings(settings_path()).profileStyles[0].image.empty() && std::filesystem::exists(portrait) &&
                        !IsWindowEnabled(GetDlgItem(w,1041)),"Removing a profile picture deleted the original or stayed enabled");
                drop_file(w,portrait);
                require(!read_settings(settings_path()).profileStyles[0].image.empty(),"Dropping a profile picture did not work");
                keyboard_choice(GetDlgItem(w,1220),1);keyboard_choice(GetDlgItem(w,1221),1);keyboard_choice(GetDlgItem(w,1228),2);click(w,1010);
                picture=read_settings(settings_path());
                require(picture.profileStyles[0].pictureFit==1 && !picture.profileStyles[0].notifications && picture.profileStyles[0].availability==2,
                        "Profile picture framing or quiet mode was not saved");
                Settings recovered;recovered.onlineName="Recovered profile";recovered.onlineProfiles={recovered.onlineName};recovered.onlineKeys={std::string(64,'c')};
                const auto recoveryPath=settings_path().parent_path()/"profiles"/"recovery"/"last-removed.ini";
                std::filesystem::create_directories(recoveryPath.parent_path());write_settings(recoveryPath,recovered);
                click(w,1000);require(IsWindowEnabled(GetDlgItem(w,1224)),"Profile recovery is unavailable");click(w,1224);
                auto restored=read_settings(settings_path());
                require(restored.onlineName=="Recovered profile" && restored.onlineKeys.back()==std::string(64,'c') && !std::filesystem::exists(recoveryPath),
                        "Profile recovery did not restore the original identity");
                keyboard_choice(GetDlgItem(w,1222),0);click(w,1223);
                restored=read_settings(settings_path());
                require(restored.profileStyles.size()==2 && restored.profileStyles.back().image==picture.profileStyles[0].image && restored.profileStyles.back().notifications && restored.profileStyles.back().availability==0,
                        "Copying appearance lost its picture or overwrote notification preferences");
                keyboard_choice(GetDlgItem(w,246),0);
                require(read_settings(settings_path()).onlineName==initial.onlineName,"The profile page cannot switch identities");
                click(w,1210);require(IsWindowVisible(GetDlgItem(w,1013)),"Notification inbox has no return action");
                click(w,1013);click(w,101);click(w,1000);
                key_press(VK_LEFT,VK_MENU);
                wait_until([&]{return IsWindowVisible(GetDlgItem(w,112)) && !IsWindowVisible(GetDlgItem(w,1007));},
                           "Alt+Left did not return from profile customization");
                click(w,102);
                SendMessageW(w,WM_VSCROLL,SB_TOP,0);
                for(int id:{1000,1012,1042,1043,1044,1045})
                    require(IsWindowVisible(GetDlgItem(w,id)),"Online profile or browse controls are missing");
                std::cout<<"[ui:stage] safe profile removal and recovery"<<std::endl;
                const auto beforeRemoval=read_settings(settings_path());
                for(int answer:{IDNO,IDYES}) {
                    PostMessageW(GetDlgItem(w,248),BM_CLICK,0,0);
                    HWND confirm{};wait_until([&]{confirm=FindWindowW(L"#32770",L"Remove profile");return confirm!=nullptr;},"Profile removal did not ask for confirmation");
                    wait_until([&] {
                        const auto keep=GetDlgItem(confirm,IDNO);GUITHREADINFO info{sizeof info};
                        GetGUIThreadInfo(GetWindowThreadProcessId(confirm,nullptr),&info);
                        return keep && ((GetWindowLongPtrW(keep,GWL_STYLE)&BS_TYPEMASK)==BS_DEFPUSHBUTTON || info.hwndFocus==keep);
                    },"Profile deletion does not default to keeping the profile");
                    SendMessageW(GetDlgItem(confirm,answer),BM_CLICK,0,0);
                    wait_until([&]{return !IsWindow(confirm);},"Profile removal confirmation stayed open");
                    SendMessageW(w,WM_NULL,0,0);
                    require(read_settings(settings_path()).onlineProfiles.size()==beforeRemoval.onlineProfiles.size()-(answer==IDYES ? 1u : 0u),
                            "Profile removal ignored the confirmation choice");
                }
                click(w,1224);
                require(read_settings(settings_path()).onlineKeys.back()==beforeRemoval.onlineKeys[0],"Undo removal replaced the profile identity");
                SetWindowTextW(GetDlgItem(w,1042),L"Kame House");
                require(IsWindowEnabled(GetDlgItem(w,1045)),"Online search cannot be cleared");
                click(w,1045);
                wchar_t query[128]{};GetWindowTextW(GetDlgItem(w,1042),query,128);
                require(!*query && !IsWindowEnabled(GetDlgItem(w,1045)),"Clearing online search left stale text or controls");
                keyboard_choice(GetDlgItem(w,1043),2);keyboard_choice(GetDlgItem(w,1044),1);
                require(read_settings(settings_path()).onlineRoomFilter==2 && read_settings(settings_path()).onlinePlayerFilter==1,
                        "Online room and friend filters did not save");
                const auto currentDpi=[](HWND window) {
                    const auto getDpi=reinterpret_cast<UINT(WINAPI*)(HWND)>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"GetDpiForWindow"));
                    return getDpi ? getDpi(window) : 96u;
                }(w);
                for(int height:{680,960}) {
                    SetWindowPos(w,nullptr,0,0,MulDiv(1100,currentDpi,96),MulDiv(height,currentDpi,96),SWP_NOZORDER|SWP_NOACTIVATE);
                    for(int id:{1000,1012,1042,1043,1044,1045,243,244,245,256,258,259}) {
                        bool reachable=false;
                        for(int position:{SB_TOP,SB_BOTTOM}) {
                            SendMessageW(w,WM_VSCROLL,position,0);
                            if(!IsWindowVisible(GetDlgItem(w,id)))continue;
                            RECT bounds{},client{};GetWindowRect(GetDlgItem(w,id),&bounds);GetClientRect(w,&client);
                            MapWindowPoints(nullptr,w,reinterpret_cast<POINT*>(&bounds),2);
                            if(bounds.top>=MulDiv(76,currentDpi,96) && bounds.bottom<=client.bottom-MulDiv(116,currentDpi,96))reachable=true;
                        }
                        require(reachable,"An Online action cannot be reached above the dock");
                    }
                }
                SendMessageW(w,WM_VSCROLL,SB_TOP,0);
                click(w,1000);
                require(IsWindowVisible(GetDlgItem(w,1040)),"Online has no direct route to profile pictures");
                mouse_click(GetDlgItem(w,106),16,16);
                wait_until([&]{return IsWindowVisible(GetDlgItem(w,1110));},"Settings did not open before the search shortcut");
                key_press('F',VK_CONTROL);
                wait_until([&]{GUITHREADINFO info{sizeof info};GetGUIThreadInfo(GetWindowThreadProcessId(w,nullptr),&info);return info.hwndFocus==GetDlgItem(w,1110);},"Ctrl+F did not focus settings search");
                SendMessageW(w,WM_VSCROLL,SB_BOTTOM,0);
                click(w,106);
                require(GetDlgItem(w,174)!=nullptr,"The saved Discord preference has no launcher control");
                require(!read_settings(settings_path()).discordPresence,"Startup ignored the saved Discord preference");
                keyboard_choice(GetDlgItem(w,1226),1);keyboard_choice(GetDlgItem(w,1227),1);
                require(read_settings(settings_path()).reducedMotion && !read_settings(settings_path()).helpHints,
                        "Interface motion or hint preferences were not saved");
                click(w,1114);require(read_settings(settings_path()).helpHints && read_settings(settings_path()).reducedMotion,
                        "Undo did not restore help hints independently");
                keyboard_choice(GetDlgItem(w,1226),0);
                click(w,1200);

                SetWindowTextW(GetDlgItem(w,1110),L"graphics");
                require(IsWindowVisible(GetDlgItem(w,140)) && !IsWindowVisible(GetDlgItem(w,143)),
                        "Settings search did not filter and reflow sections");
                SetWindowTextW(GetDlgItem(w,1110),L"nothing matches");
                require(!IsWindowVisible(GetDlgItem(w,140)),"An unmatched settings search leaves controls visible");
                click(w,1111);
                for(int id=140;id<=150;++id) {
                    if(id==148)continue;
                    auto combo=GetDlgItem(w,id);
                    open_dropdown(combo);
                    SendMessageW(combo,CB_SHOWDROPDOWN,FALSE,0);
                }
                keyboard_choice(GetDlgItem(w,141),3);
                require(read_settings(settings_path()).fps==120,"FPS was not persisted by native interaction");
                SendMessageW(w,WM_COMMAND,MAKEWPARAM(141,CBN_SELCHANGE),LPARAM(GetDlgItem(w,141)));
                key_press('Z',VK_CONTROL);wait_until([&]{return read_settings(settings_path()).fps==240;},"Ctrl+Z did not undo the settings change");
                require(read_settings(settings_path()).fps==240 && !read_settings(settings_path()).discordPresence,
                        "Undo settings failed or changed unrelated preferences");
                wait_until([&]{return !IsWindowEnabled(GetDlgItem(w,1114));},"Undo stayed enabled with no previous change");
                keyboard_choice(GetDlgItem(w,141),3);
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
                click(w,101);click(w,1004);
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
                require(!reset.discordPresence,"Reset graphics changed the Discord preference");
                require(reset.audio && reset.volume==50,"Reset graphics changed the audio preference");
                const auto dpiForWindow = reinterpret_cast<UINT(WINAPI *)(HWND)>(
                    GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
                const int dpi = dpiForWindow ? int(dpiForWindow(w)) : 96;
                for (int height : {860, 920, 1000}) {
                    SetWindowPos(w,nullptr,0,0,MulDiv(1000,dpi,96),MulDiv(height,dpi,96),
                                 SWP_NOZORDER|SWP_NOACTIVATE);
                    for(int id : {140,141,142,143,144,145,146,147,149,150,151,152,153,154,
                                  170,171,173,179,1210,1110,1111,1112,1113,1114}) {
                        if (id!=1210) reveal_control(GetDlgItem(w,id));
                        RECT bounds{},client{};GetWindowRect(GetDlgItem(w,id),&bounds);GetClientRect(w,&client);
                        MapWindowPoints(nullptr,w,reinterpret_cast<POINT*>(&bounds),2);
                        require(bounds.left>=0 && bounds.top>=0 && bounds.right<=client.right &&
                                bounds.bottom<=client.bottom-MulDiv(110,dpi,96),
                                "A settings control overlaps the status footer or leaves the window");
                    }
                }
                keyboard_choice(GetDlgItem(w,303),7);keyboard_choice(GetDlgItem(w,302),2);
                require(read_settings(settings_path()).launcherTheme==2 && read_settings(settings_path()).launcherAccent==0 &&
                        read_settings(settings_path()).launcherColors==no_custom_colors(),"Choosing a named theme retained a different accent palette");
                click(w,1114);require(read_settings(settings_path()).launcherTheme==0 && read_settings(settings_path()).launcherAccent==7,
                        "Undo did not restore the previous theme and accent");
                keyboard_choice(GetDlgItem(w,302),0);
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
                for(int height : {768,800,860,920,1000}) {
                    SetWindowPos(w,nullptr,0,0,MulDiv(1000,dpi,96),MulDiv(height,dpi,96),
                                 SWP_NOZORDER|SWP_NOACTIVATE);
                    SendMessageW(w,WM_VSCROLL,SB_TOP,0);
                    for(int id : {177,178,236,222,264,265,266,267,268,290}) {
                        RECT client{};GetClientRect(w,&client);
                        const auto bounds=visible_bounds(w,GetDlgItem(w,id));
                        const auto message="A speedrun control overlaps the dock: id="+std::to_string(id)+
                            " requested="+std::to_string(height)+" dpi="+std::to_string(dpi)+
                            " client="+std::to_string(client.right)+","+std::to_string(client.bottom)+
                            " visible="+std::to_string(bounds.left)+","+std::to_string(bounds.top)+","+
                            std::to_string(bounds.right)+","+std::to_string(bounds.bottom);
                        wait_until([&] {
                            if(id==290 && !IsWindowVisible(GetDlgItem(w,id)))return true;
                            RECT actualClient{};GetClientRect(w,&actualClient);
                            const auto actual=visible_bounds(w,GetDlgItem(w,id));
                            return IsWindowVisible(GetDlgItem(w,id)) && actual.right>actual.left && actual.bottom>actual.top &&
                                actual.left>=0 && actual.top>=MulDiv(76,dpi,96) && actual.right<=actualClient.right &&
                                actual.bottom<=actualClient.bottom-MulDiv(116,dpi,96);
                        },message.c_str());
                    }
                    SendMessageW(w,WM_VSCROLL,SB_BOTTOM,0);
                    RECT bounds{},client{};GetWindowRect(rules,&bounds);GetClientRect(w,&client);
                    MapWindowPoints(nullptr,w,reinterpret_cast<POINT*>(&bounds),2);
                    require(IsWindowVisible(rules) && bounds.top>=MulDiv(76,dpi,96) &&
                                bounds.bottom<=client.bottom-MulDiv(116,dpi,96),
                            "The full timing-rules field cannot be reached by scrolling");
                    SCROLLINFO pageBefore{sizeof(pageBefore),SIF_POS},pageAfter{sizeof(pageAfter),SIF_POS};
                    GetScrollInfo(w,SB_VERT,&pageBefore);
                    wheel_over(w,290,-WHEEL_DELTA);GetScrollInfo(w,SB_VERT,&pageAfter);
                    require(SendMessageW(rules,EM_GETFIRSTVISIBLELINE,0,0)>0 && pageBefore.nPos==pageAfter.nPos,
                            "Scrolling timing rules moved the launcher page");
                    SendMessageW(rules,EM_LINESCROLL,0,10000);
                    require(SendMessageW(rules,EM_GETFIRSTVISIBLELINE,0,0)>0,
                            "The remaining timing rules cannot be scrolled into view");
                    SendMessageW(rules,EM_LINESCROLL,0,-10000);
                }
                SendMessageW(w,WM_VSCROLL,SB_TOP,0);
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
                require(IsWindowEnabled(GetDlgItem(w,1020)) && !IsWindowEnabled(GetDlgItem(w,294)),
                        "Split browsing requires activating a speedrun, or editing is enabled without a category");
                wheel_over(w,1020,-WHEEL_DELTA);
                GetScrollInfo(w,SB_VERT,&wheel);
                require(SendMessageW(GetDlgItem(w,1020),LB_GETTOPINDEX,0,0)>0 && wheel.nPos==0,
                        "Scrolling the split list moved the launcher page");
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
                SendMessageW(w,WM_VSCROLL,SB_TOP,0);
                SetWindowPos(w,nullptr,0,0,MulDiv(1100,dpi,96),MulDiv(920,dpi,96),SWP_NOZORDER|SWP_NOACTIVATE);
                require(IsWindowVisible(GetDlgItem(w,177)),"Overlay editing has no category selector");
                keyboard_choice(GetDlgItem(w,177),1);
                for(int id:{294,1021,1022,1025})
                    require(IsWindowEnabled(GetDlgItem(w,id)),"Choosing a category in the overlay did not enable editing");
                require(!GetDlgItem(w,224) && !GetDlgItem(w,225),"The removed visibility checklist is still present");
                require(SendMessageW(GetDlgItem(w,1020),LB_GETCOUNT,0,0)==12,"Split customization has no editable rows");
                mouse_choice(GetDlgItem(w,226),1);
                require(read_settings(settings_path()).overlayCorner==1,"The overlay position was not saved");
                const auto list=GetDlgItem(w,1020);
                SendMessageW(list,LB_SETTOPINDEX,0,0);
                const auto point=control_center(w,1020);
                PostMessageW(GetDlgItem(w,226),WM_MOUSEWHEEL,MAKEWPARAM(0,WORD(short(-WHEEL_DELTA))),MAKELPARAM(point.x,point.y));
                wait_until([&]{GetScrollInfo(w,SB_VERT,&wheel);return SendMessageW(list,LB_GETTOPINDEX,0,0)>0 || wheel.nPos!=0;},
                           "The split list did not receive a wheel event sent to another control");
                require(wheel.nPos==0 && read_settings(settings_path()).overlayCorner==1 && SendMessageW(list,LB_GETCURSEL,0,0)==0,
                        "Scrolling splits changed the page, selected row or focused dropdown value");
                SendMessageW(list,LB_SETTOPINDEX,11,0);
                const auto lastTop=SendMessageW(list,LB_GETTOPINDEX,0,0);
                wheel_over(w,1020,-WHEEL_DELTA);GetScrollInfo(w,SB_VERT,&wheel);
                require(SendMessageW(list,LB_GETTOPINDEX,0,0)==lastTop && wheel.nPos==0,
                        "The launcher scrolls when the split list reaches its end");
                SendMessageW(list,LB_SETTOPINDEX,0,0);
                wheel_over(w,1020,-60);wheel_over(w,1020,-60);GetScrollInfo(w,SB_VERT,&wheel);
                require(SendMessageW(list,LB_GETTOPINDEX,0,0)>0 && wheel.nPos==0,
                        "Partial wheel steps do not scroll splits independently");
                SendMessageW(list,LB_SETTOPINDEX,0,0);
                SetWindowTextW(GetDlgItem(w,294),L"Raditz · Arrival");click(w,1025);
                auto appearance=read_settings(settings_path());
                require(appearance.splitStyles.size()==1 && appearance.splitStyles[0].name=="Raditz · Arrival" &&
                            appearance.splitStyles[0].run=="any.txt","A split name did not save under its category");
                require(!std::filesystem::exists(settings_path().parent_path()/"splits"/"any.txt"),"Editing a split name changed recorded times");
                const auto userImage=root/L"Owned animation.gif";
                {std::ofstream out(userImage,std::ios::binary);out.write(reinterpret_cast<const char*>(openkai2::test_images::gif),sizeof(openkai2::test_images::gif));}
                require(std::filesystem::exists(userImage) && std::filesystem::file_size(userImage)>0,"The picker fixture was not written");
                choose_image(w,userImage);
                appearance=read_settings(settings_path());
                require(appearance.splitStyles.size()==1 && openkai2::speedrun::valid_image_file(appearance.splitStyles[0].image) &&
                            !appearance.splitStyles[0].image.empty() && std::filesystem::exists(userImage),"A user's GIF was not attached or its original was removed");
                const auto media=settings_path().parent_path()/"overlay"/"images"/appearance.splitStyles[0].image;
                require(openkai2::speedrun::load_split_image(media).frames.size()==4,"Imported GIF animation was lost");
                const auto protectedItems=openkai2::speedrun::overlay_bit(openkai2::speedrun::show_ps2_time)|openkai2::speedrun::overlay_bit(openkai2::speedrun::show_status);
                require((appearance.overlayItems&protectedItems)==protectedItems,"PS2 time or verification could be hidden by customization");
                keyboard_choice(GetDlgItem(w,177),3);
                wchar_t customName[200]{};GetWindowTextW(GetDlgItem(w,294),customName,200);
                require(!*customName && !IsWindowEnabled(GetDlgItem(w,1023)),"Tournament splits inherited Dragon World names or images");
                keyboard_choice(GetDlgItem(w,177),1);
                GetWindowTextW(GetDlgItem(w,294),customName,200);
                require(std::wstring(customName)==L"Raditz · Arrival","Returning to a category lost its split appearance");
                click(w,1023);
                require(read_settings(settings_path()).splitStyles[0].image.empty() && std::filesystem::exists(userImage),"Removing an image deleted the original");
                click(w,1024);click(w,1021);
                require(SendMessageW(list,LB_GETCOUNT,0,0)==13,"A new split appearance row was not added");
                RECT added{},listClient{};GetClientRect(list,&listClient);
                SendMessageW(list,LB_GETITEMRECT,12,reinterpret_cast<LPARAM>(&added));
                require(SendMessageW(list,LB_GETCURSEL,0,0)==12 && added.top>=0 && added.bottom<=listClient.bottom,
                        "The new split row is selected but hidden below the list");
                SetWindowTextW(GetDlgItem(w,294),L"Bonus split");click(w,1025);
                drop_file(w,userImage);
                appearance=read_settings(settings_path());
                require(appearance.splitStyles.size()==1 && appearance.splitStyles[0].index==12 &&
                            appearance.splitStyles[0].name=="Bonus split" && !appearance.splitStyles[0].image.empty(),
                        "A newly added split cannot save its name and image");
                click(w,223);click(w,222);
                require(SendMessageW(list,LB_GETCOUNT,0,0)==13,"Reopening the editor lost an added split");
                keyboard_choice(GetDlgItem(w,177),0);
                require(!IsWindowEnabled(GetDlgItem(w,1021)) && !IsWindowEnabled(GetDlgItem(w,1022)),
                        "Editing remains enabled without a category");
                keyboard_choice(GetDlgItem(w,177),1);
                SendMessageW(list,LB_SETCURSEL,12,0);
                SendMessageW(w,WM_COMMAND,MAKEWPARAM(1020,LBN_SELCHANGE),reinterpret_cast<LPARAM>(list));
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
                SetWindowPos(w,nullptr,0,0,MulDiv(1000,dpi,96),MulDiv(768,dpi,96),SWP_NOZORDER|SWP_NOACTIVATE);
                for(auto route : {std::pair<WORD,int>{'1',112},{'6',160},{'4',185},{'5',1110},{'3',177},{'2',246}}) {
                    SetForegroundWindow(w);
                    INPUT keys[4]{};
                    for(auto& key:keys)key.type=INPUT_KEYBOARD;
                    keys[0].ki.wVk=VK_CONTROL;keys[1].ki.wVk=route.first;
                    keys[2].ki.wVk=route.first;keys[2].ki.dwFlags=KEYEVENTF_KEYUP;
                    keys[3].ki.wVk=VK_CONTROL;keys[3].ki.dwFlags=KEYEVENTF_KEYUP;
                    require(SendInput(4,keys,sizeof(INPUT))==4,"Cannot submit page shortcut");
                    wait_until([&]{return IsWindowVisible(GetDlgItem(w,route.second))!=FALSE;},
                        ("Ctrl+"+std::string(1,char(route.first))+" did not show page control "+std::to_string(route.second)).c_str());
                }
                click(w,106);click(w,301);
                SendMessageW(GetDlgItem(w,295),LB_SETCURSEL,2,0);click(w,297);
                require(read_settings(settings_path()).tabOrder[3]==2,"Dock tabs cannot be moved freely across the old groups");
                key_press('4',VK_CONTROL);
                wait_until([&]{return IsWindowVisible(GetDlgItem(w,177));},"A moved tab's keyboard shortcut did not follow the dock");
                click(w,106);click(w,301);click(w,299);click(w,300);
                click(w,101);click(w,1004);
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
                auto inside=[&](int id,const char* message){
                    for(auto position : {SB_TOP,SB_BOTTOM}) {
                        SendMessageW(w,WM_VSCROLL,position,0);
                        RECT bounds{},client{};GetWindowRect(GetDlgItem(w,id),&bounds);GetClientRect(w,&client);
                        MapWindowPoints(nullptr,w,reinterpret_cast<POINT*>(&bounds),2);
                        if(IsWindowVisible(GetDlgItem(w,id)) && bounds.left>=0 && bounds.top>=MulDiv(76,dpi,96) &&
                            bounds.right<=client.right && bounds.bottom<=client.bottom-MulDiv(116,dpi,96))return;
                    }
                    throw std::runtime_error(std::string(message)+": id="+std::to_string(id));
                };
                for(int id=181;id<=192;++id) inside(id,"A mods control is clipped at the minimum window size");
                require(SendMessageW(GetDlgItem(w,189),LB_GETCOUNT,0,0)==1 && !IsWindowEnabled(GetDlgItem(w,192)),
                        "An empty mod's contents were not listed, or Remove character was enabled");
                SetWindowTextW(GetDlgItem(w,186),L"Renamed mod");
                click(w,190);
                require(openkai2::mods::scan_mods(openkai2::mods::default_mods_directory())[0].name=="Renamed mod",
                        "Renaming a mod was not saved");
                SendMessageW(w,WM_VSCROLL,SB_TOP,0);
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
                std::cout<<"[ui:stage] display scale and work-area bounds"<<std::endl;
                MONITORINFO screen{sizeof screen};require(GetMonitorInfoW(MonitorFromWindow(w,MONITOR_DEFAULTTONEAREST),&screen)!=FALSE,"Cannot read the display work area");
                RECT original{};GetWindowRect(w,&original);
                const auto getDpi=reinterpret_cast<UINT(WINAPI*)(HWND)>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"GetDpiForWindow"));
                const UINT originalDpi=getDpi ? getDpi(w) : 96;
                RECT suggested{screen.rcWork.left+16,screen.rcWork.top+16,screen.rcWork.right+640,screen.rcWork.bottom+640};
                SendMessageW(w,WM_DPICHANGED,MAKEWPARAM(192,192),LPARAM(&suggested));
                MINMAXINFO limits{};SendMessageW(w,WM_GETMINMAXINFO,0,LPARAM(&limits));
                RECT scaled{};GetWindowRect(w,&scaled);
                require(scaled.left>=screen.rcWork.left && scaled.top>=screen.rcWork.top && scaled.right<=screen.rcWork.right && scaled.bottom<=screen.rcWork.bottom,
                        "Changing display scale pushed the launcher outside its monitor");
                require(limits.ptMinTrackSize.x<=screen.rcWork.right-screen.rcWork.left && limits.ptMinTrackSize.y<=screen.rcWork.bottom-screen.rcWork.top,
                        "The minimum launcher size exceeds the monitor at 200% scale");
                SendMessageW(w,WM_DPICHANGED,MAKEWPARAM(originalDpi,originalDpi),LPARAM(&original));

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
