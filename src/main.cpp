// SPDX-License-Identifier: GPL-3.0-only
#include "host_audio.h"
#include "disc_image.h"
#include "display_settings.h"
#include "openkai2_build_revision.h"
#include "runtime_checks.h"
#include "render/gpu_backend.h"
#include "render/gpu_checks.h"
#include "runtime_layout.h"
#include "ps2_runtime.h"
#include <exception>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <mutex>
#include <stdexcept>
#include <streambuf>
#include <vector>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#endif

namespace {
class TeeBuffer : public std::streambuf {
    std::streambuf *console_, *file_;
    std::mutex& mutex_;
protected:
    std::streamsize xsputn(const char* data, std::streamsize size) override {
        std::lock_guard<std::mutex> guard(mutex_);
        file_->sputn(data,size);
        return console_->sputn(data,size);
    }
    int overflow(int value) override {
        if (traits_type::eq_int_type(value,traits_type::eof())) return traits_type::not_eof(value);
        const char ch=traits_type::to_char_type(value);
        return xsputn(&ch,1)==1 ? value : traits_type::eof();
    }
    int sync() override {
        std::lock_guard<std::mutex> guard(mutex_);
        const auto file_result=file_->pubsync();
        const auto console_result=console_->pubsync();
        return file_result==0 && console_result==0 ? 0 : -1;
    }
public:
    TeeBuffer(std::streambuf* console, std::streambuf* file, std::mutex& mutex)
        : console_(console), file_(file), mutex_(mutex) {}
};

class StartupLog {
    std::ofstream file_;
    std::mutex mutex_;
    TeeBuffer output_{std::cout.rdbuf(),file_.rdbuf(),mutex_};
    TeeBuffer errors_{std::cerr.rdbuf(),file_.rdbuf(),mutex_};
    std::streambuf *old_output_=nullptr, *old_errors_=nullptr;
public:
    std::filesystem::path path;
    void open(const char* filename="startup.log") {
        path=openkai2::default_cache_directory().parent_path()/"logs"/filename;
        std::filesystem::create_directories(path.parent_path());
        file_.open(path,std::ios::out|std::ios::trunc);
        if (!file_) throw std::runtime_error("Cannot write startup log: "+path.string());
        old_output_=std::cout.rdbuf(&output_);
        old_errors_=std::cerr.rdbuf(&errors_);
    }
    ~StartupLog() {
        if (old_output_) { std::cout.flush(); std::cout.rdbuf(old_output_); }
        if (old_errors_) { std::cerr.flush(); std::cerr.rdbuf(old_errors_); }
    }
};

void usage() {
    std::cout << "Usage: OpenKai2 [--renderer=auto|gpu|cpu] [--fps-limit=0..240] <path-to-Budokai-2-USA.iso>\n"
              << "Host display limit defaults to 240; 0 is unlimited. Game timing stays at its original rate.\n"
              << "The original game ISO is required on every boot.\n"
              << "OpenKai2 --self-test checks compiled callback dispatch without booting the game.\n"
              << "OpenKai2 --gpu-self-test checks GPU rendering without a game ISO.\n"
              << "OpenKai2 --audio-test reports the disabled host audio status.\n";
#ifdef _WIN32
    std::cout << "Double-click OpenKai2.exe to choose an ISO.\n";
#endif
}

#ifdef _WIN32
std::filesystem::path choose_iso() {
    wchar_t filename[32768]{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize=sizeof(dialog);
    dialog.lpstrFilter=L"Game ISO (*.iso)\0*.iso\0All files (*.*)\0*.*\0\0";
    dialog.lpstrFile=filename;
    dialog.nMaxFile=DWORD(std::size(filename));
    dialog.lpstrTitle=L"OpenKai2 - Select Budokai 2 USA ISO";
    dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR|OFN_EXPLORER;
    if (GetOpenFileNameW(&dialog)) return filename;
    const auto error=CommDlgExtendedError();
    if (error) throw std::runtime_error("Cannot open ISO selection dialog (error "+std::to_string(error)+")");
    return {};
}
#endif
} // namespace

int main(int argc, char* argv[]) {
    StartupLog log;
    bool interactive=false;
    try {
        if(const auto value=std::getenv("OPENKAI2_FPS_LIMIT"))
            openkai2::set_window_fps_limit(openkai2::parse_window_fps_limit(value));
        std::filesystem::path image;
#ifdef _WIN32
        // Preserve Unicode paths supplied by Explorer or the command line.
        int count=0;
        auto arguments=CommandLineToArgvW(GetCommandLineW(),&count);
        if (!arguments) throw std::runtime_error("Cannot read command line");
        std::vector<std::wstring> positional;
        for(int i=1;i<count;++i)positional.emplace_back(arguments[i]);
        LocalFree(arguments);
        for(auto i=positional.begin();i!=positional.end();){
            if(i->rfind(L"--renderer=",0u)==0u){
                const auto value=i->substr(11u);openkai2::set_renderer_preference(std::string(value.begin(),value.end()));i=positional.erase(i);
            }else if(i->rfind(L"--fps-limit=",0u)==0u){
                const auto value=i->substr(12u);
                openkai2::set_window_fps_limit(openkai2::parse_window_fps_limit(std::string(value.begin(),value.end())));
                i=positional.erase(i);
            }else ++i;
        }
        count=int(positional.size())+1;
        const std::wstring argument=count==2 ? positional.front() : L"";
        if (count==2 && (argument==L"--help" || argument==L"-h")) { usage(); return 0; }
        if (count==2 && argument==L"--audio-test") { log.open("audio-test.log");return openkai2::test_host_audio(); }
        if (count==2 && argument==L"--self-test") return openkai2::run_runtime_checks();
        if (count==2 && argument==L"--gpu-self-test") {log.open("gpu-test.log");return openkai2::run_gpu_checks(true);}
        if (count>2) { usage(); return 2; }
        interactive=count==1;
        if (!interactive) image=argument;
#else
        std::vector<std::string> positional;
        for(int i=1;i<argc;++i){
            const std::string argument=argv[i];
            if(argument.rfind("--renderer=",0u)==0u)openkai2::set_renderer_preference(argument.substr(11u));
            else if(argument.rfind("--fps-limit=",0u)==0u)
                openkai2::set_window_fps_limit(openkai2::parse_window_fps_limit(argument.substr(12u)));
            else positional.push_back(argument);
        }
        if(positional.size()==1u && (positional[0]=="--help" || positional[0]=="-h")){usage();return 0;}
        if(positional.size()==1u && positional[0]=="--audio-test"){log.open("audio-test.log");return openkai2::test_host_audio();}
        if(positional.size()==1u && positional[0]=="--self-test")return openkai2::run_runtime_checks();
        if(positional.size()==1u && positional[0]=="--gpu-self-test"){log.open("gpu-test.log");return openkai2::run_gpu_checks(true);}
        if(positional.size()!=1u){usage();return 2;}
        image=positional[0];
#endif
        log.open();
        std::cout << "[build] source_revision=" OPENKAI2_SOURCE_REVISION " renderer="
                  << openkai2::renderer_preference() << " presentation=async" << std::endl;
        std::cout << "[display] fps_limit=" << openkai2::window_fps_limit() << " game_timing=original\n";
        openkai2::verify_runtime_layout(sizeof(VU1Interpreter),sizeof(PS2Runtime));
        std::cout << "[launcher] Startup log: " << log.path.string() << std::endl;
#ifdef _WIN32
        if (interactive) {
            image=choose_iso();
            if (image.empty()) { std::cout << "ISO selection cancelled.\n"; return 0; }
        }
#endif
        openkai2::DiscOptions options;
        size_t verified=0;
        options.progress=[&verified](const std::string& message) {
            if (message.rfind("Verifying ",0)==0 && ++verified>1 && verified%128!=0) return;
            std::cout << "[disc] " << message << std::endl;
        };
        const auto disc=openkai2::prepare_disc(image,openkai2::default_cache_directory(),options);
        std::cout << "[disc] Verified " << disc.files.size() << " files (" << disc.bytes_verified
                  << " bytes); refreshed " << disc.files_repaired << " cached files.\n"
                  << "[disc] ISO SHA-256: " << disc.image_sha256 << std::endl;
        PS2Runtime runtime;
        if (!runtime.initialize("OpenKai2 | Dragon Ball Z: Budokai 2"))
            throw std::runtime_error("Runtime initialization failed; see the startup log");
        if (!runtime.loadELF(disc.executable.string()))
            throw std::runtime_error("Cannot load the verified game executable; see the startup log");
        auto paths=PS2Runtime::getIoPaths();
        paths.cdImage=std::filesystem::absolute(image);
        PS2Runtime::setIoPaths(paths);
        runtime.run();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[OpenKai2] " << error.what() << std::endl;
#ifdef _WIN32
        if (interactive) {
            const auto message=std::string(error.what())+"\n\nStartup log: "+log.path.string();
            MessageBoxA(nullptr,message.c_str(),"OpenKai2 startup failed",MB_OK|MB_ICONERROR);
        }
#endif
        return 1;
    }
}
