// SPDX-License-Identifier: GPL-3.0-only
#include "launcher/update.h"
#include "launcher/settings.h"
#include "launcher/version.h"
#include "launcher/update_script.h"
#include "disc_image.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#include <objbase.h>
#include <array>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>

namespace openkai2::launcher {
namespace {
std::wstring wide(const std::string &s) {
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),nullptr,0);
    if(!n&&!s.empty())throw std::runtime_error("Invalid update URL");
    std::wstring out(n,0);MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),out.data(),n);return out;
}
struct Internet {
    HINTERNET value{};
    explicit Internet(HINTERNET v):value(v) {if(!v)throw std::runtime_error("Cannot connect to GitHub for updates");}
    ~Internet(){WinHttpCloseHandle(value);}
    operator HINTERNET() const{return value;}
};
struct Handle {
    HANDLE value{};
    ~Handle(){if(value)CloseHandle(value);}
};
std::wstring unique_id() {
    GUID id{};if(FAILED(CoCreateGuid(&id)))throw std::runtime_error("Cannot create update staging directory");
    wchar_t buffer[40]{};StringFromGUID2(id,buffer,40);return buffer;
}
struct Url {std::wstring host,path;};
Url parse_url(const std::wstring &url) {
    URL_COMPONENTS c{};c.dwStructSize=sizeof(c);
    c.dwHostNameLength=c.dwUrlPathLength=c.dwExtraInfoLength=c.dwUserNameLength=c.dwPasswordLength=DWORD(-1);
    if(!WinHttpCrackUrl(url.c_str(),DWORD(url.size()),0,&c)||c.nScheme!=INTERNET_SCHEME_HTTPS||c.nPort!=443||c.dwUserNameLength||c.dwPasswordLength)throw std::runtime_error("Unsafe update redirect");
    Url out{std::wstring(c.lpszHostName,c.dwHostNameLength),std::wstring(c.lpszUrlPath,c.dwUrlPathLength)};
    if(c.dwExtraInfoLength)out.path.append(c.lpszExtraInfo,c.dwExtraInfoLength);
    if(out.path.find(L'#')!=out.path.npos)throw std::runtime_error("Unsafe update redirect");
    if(out.host!=L"api.github.com"&&out.host!=L"github.com"&&out.host!=L"release-assets.githubusercontent.com"&&out.host!=L"objects.githubusercontent.com"&&out.host!=L"github-releases.githubusercontent.com")throw std::runtime_error("Update download left GitHub");
    return out;
}
std::string fetch(std::wstring url,uint64_t limit,const std::filesystem::path &file={},
                  const std::function<void(const std::string &)> &progress={}) {
    Internet session(WinHttpOpen(L"OpenKai2/" L"0.1.25",WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0));
    WinHttpSetTimeouts(session,10000,10000,15000,15000);
    DWORD protocols=WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
    if(!WinHttpSetOption(session,WINHTTP_OPTION_SECURE_PROTOCOLS,&protocols,sizeof(protocols)))throw std::runtime_error("TLS 1.2 is unavailable for updates");
    for(unsigned redirects=0;redirects<6;++redirects) {
        const auto parsed=parse_url(url);
        Internet connection(WinHttpConnect(session,parsed.host.c_str(),443,0));
        Internet request(WinHttpOpenRequest(connection,L"GET",parsed.path.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE));
        DWORD redirect=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        if(!WinHttpSetOption(request,WINHTTP_OPTION_REDIRECT_POLICY,&redirect,sizeof(redirect)))throw std::runtime_error("Cannot restrict update redirects");
        const wchar_t *headers=L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n";
        if(!WinHttpSendRequest(request,headers,DWORD(-1),WINHTTP_NO_REQUEST_DATA,0,0,0)||!WinHttpReceiveResponse(request,nullptr))throw std::runtime_error("GitHub update check failed. Check your connection and try again");
        DWORD status=0,size=sizeof(status);
        if(!WinHttpQueryHeaders(request,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&size,WINHTTP_NO_HEADER_INDEX))throw std::runtime_error("Cannot read GitHub update response");
        if(status==301||status==302||status==303||status==307||status==308) {
            DWORD bytes=0;WinHttpQueryHeaders(request,WINHTTP_QUERY_LOCATION,WINHTTP_HEADER_NAME_BY_INDEX,nullptr,&bytes,WINHTTP_NO_HEADER_INDEX);
            if(!bytes||bytes>32768)throw std::runtime_error("Invalid update redirect");
            std::wstring next(bytes/sizeof(wchar_t),0);
            if(!WinHttpQueryHeaders(request,WINHTTP_QUERY_LOCATION,WINHTTP_HEADER_NAME_BY_INDEX,next.data(),&bytes,WINHTTP_NO_HEADER_INDEX))throw std::runtime_error("Cannot read update redirect");
            next.resize(wcslen(next.c_str()));(void)parse_url(next);url=std::move(next);continue;
        }
        if(status!=200)throw std::runtime_error(status==403||status==429?"GitHub temporarily limited update checks. Try again later":"GitHub update download returned HTTP "+std::to_string(status));
        std::ofstream out;
        if(!file.empty()) {out.open(file,std::ios::binary|std::ios::trunc);if(!out)throw std::runtime_error("Cannot stage the downloaded update");}
        std::string response;std::array<char,65536> buffer{};uint64_t total=0,last=0;
        for(;;) {
            DWORD got=0;
            if(!WinHttpReadData(request,buffer.data(),DWORD(buffer.size()),&got))throw std::runtime_error("Update download was interrupted");
            if(!got)break;
            total+=got;if(total>limit)throw std::runtime_error("Update response exceeds its expected size");
            if(out.is_open()) {out.write(buffer.data(),got);if(!out)throw std::runtime_error("Cannot write the update download");}
            else response.append(buffer.data(),got);
            if(progress&&total-last>=1024*1024) {last=total;progress("Downloading update: "+std::to_string(total/(1024*1024))+" MB");}
        }
        if(out.is_open()) {out.close();if(!out)throw std::runtime_error("Cannot finish the update download");}
        return response;
    }
    throw std::runtime_error("Too many update download redirects");
}
std::wstring environment(const wchar_t *key) {
    DWORD n=GetEnvironmentVariableW(key,nullptr,0);if(!n||n>32768)return {};
    std::wstring s(n,0);DWORD read=GetEnvironmentVariableW(key,s.data(),n);if(!read||read>=n)return {};s.resize(read);return s;
}
std::filesystem::path executable() {
    std::array<wchar_t,32768> value{};auto n=GetModuleFileNameW(nullptr,value.data(),DWORD(value.size()));
    if(!n||n>=value.size())throw std::runtime_error("Cannot locate the current application");return std::filesystem::path(value.data());
}
}
std::optional<UpdateRelease> check_for_updates(bool prereleases) {
    return select_release(fetch(L"https://api.github.com/repos/7v0biscuit/OpenKai2/releases?per_page=100",8*1024*1024),application_version,prereleases);
}
StagedUpdate stage_update(const UpdateRelease &release,const std::function<void(const std::string &)> &progress) {
    StagedUpdate result;
    result.version=release.version;
    auto outer=environment(L"OPENKAI2_PORTABLE_PATH");
    result.portable=!outer.empty();
    result.target=result.portable?std::filesystem::path(outer):executable().parent_path();
    if(result.portable&&(!result.target.is_absolute()||result.target.extension()!=L".exe"))throw std::runtime_error("Cannot identify the portable update destination");
    const auto name=result.portable?"OpenKai2.exe":"OpenKai2-windows-x64.zip";
    const auto &asset=update_asset(release,name),&sums=update_asset(release,"SHA256SUMS.txt");
    if(sums.size>65536)throw std::runtime_error("Release checksum response is too large");
    const auto checksums=fetch(wide(sums.url),65536);
    result.sha256=checksum_for(checksums,name);
    result.directory=default_cache_directory().parent_path()/"updates"/unique_id();
    std::filesystem::create_directories(result.directory);
    struct Cleanup {std::filesystem::path dir;bool keep=false;~Cleanup(){if(!keep){std::error_code ec;std::filesystem::remove_all(dir,ec);}}} cleanup{result.directory};
    result.package=result.directory/name;
    progress("Downloading OpenKai2 "+release.version+"…");
    fetch(wide(asset.url),asset.size,result.package,progress);
    if(std::filesystem::file_size(result.package)!=asset.size||sha256_file(result.package)!=result.sha256)throw std::runtime_error("Downloaded update failed SHA-256 verification; the current application was kept");
    std::ofstream script(result.directory/"apply.ps1",std::ios::binary);
    script<<update_script;script.close();if(!script)throw std::runtime_error("Cannot prepare the update worker");
    cleanup.keep=true;return result;
}
void apply_update(const StagedUpdate &update) {
    std::array<wchar_t,32768> system{};UINT n=GetSystemDirectoryW(system.data(),UINT(system.size()));
    if(!n||n>=system.size())throw std::runtime_error("Cannot locate Windows PowerShell");
    const auto powershell=std::filesystem::path(system.data())/L"WindowsPowerShell/v1.0/powershell.exe";
    if(!std::filesystem::is_regular_file(powershell))throw std::runtime_error("Windows PowerShell 5.1 is required to apply updates");
    const auto eventName=L"Local\\OpenKai2.UpdateReady."+unique_id();
    Handle ready{CreateEventW(nullptr,TRUE,FALSE,eventName.c_str())};if(!ready.value)throw std::runtime_error("Cannot start the update worker");
    auto wrapper=environment(L"OPENKAI2_PORTABLE_PID");
    if(wrapper.empty()||wrapper.size()>10||wrapper.find_first_not_of(L"0123456789")!=wrapper.npos)wrapper=L"0";
    auto quote=[](const std::filesystem::path &p){return quote_argument(p.wstring());};
    std::wstring command=quote(powershell)+L" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "+quote(update.directory/"apply.ps1")+
        L" -ParentId "+std::to_wstring(GetCurrentProcessId())+L" -WrapperId "+wrapper+
        L" -ReadyEvent "+quote_argument(eventName)+L" -Package "+quote(update.package)+
        L" -Expected "+wide(update.sha256)+L" -Target "+quote(update.target)+L" -Version "+wide(update.version)+L" -Portable "+(update.portable?L"1":L"0");
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    if(!CreateProcessW(powershell.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,update.directory.c_str(),&startup,&process))throw std::runtime_error("Cannot start the Windows update worker");
    Handle child{process.hProcess},thread{process.hThread};
    HANDLE wait[]{ready.value,child.value};
    if(WaitForMultipleObjects(2,wait,FALSE,20000)!=WAIT_OBJECT_0) {
        TerminateProcess(child.value,1);WaitForSingleObject(child.value,5000);
        throw std::runtime_error("The update worker could not start. The current application was kept");
    }
}
}
