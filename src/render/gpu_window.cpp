// SPDX-License-Identifier: GPL-3.0-only
#include "gpu_backend.h"
#include "gpu_device.h"
#include "graphics_settings.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <string>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <unistd.h>
#endif
namespace openkai2 {
namespace {
uint64_t system_memory_bytes() {
#ifdef _WIN32
    MEMORYSTATUSEX status{};status.dwLength=sizeof(status);
    return GlobalMemoryStatusEx(&status) ? uint64_t(status.ullTotalPhys) : 0u;
#else
    const long pages=sysconf(_SC_PHYS_PAGES),size=sysconf(_SC_PAGESIZE);
    return pages>0 && size>0 ? uint64_t(pages)*uint64_t(size) : 0u;
#endif
}
void cap_automatic_scale(const std::string& description) {
    auto settings=graphics_settings();
    if(!settings.automaticScale)return;
    std::string name=description;for(auto& c:name)c=char(std::tolower((unsigned char)c));
    const auto has=[&](const char* text){return name.find(text)!=std::string::npos;};
    const uint64_t video=gpu::device_memory_bytes(),ram=system_memory_bytes(),gib=1024ull*1024ull*1024ull;
    unsigned cap=4u;
    if(gpu::device_integrated() || has("intel") || has("uhd") || has("iris") || has("radeon(tm) graphics") || has("vega 8") || has("vega 6"))cap=1u;
    else if(video && video<=gib*5/2)cap=1u;
    else if(video && video<=gib*9/2)cap=2u;
    else if(!video && (has("gtx 9") || has("gtx 10") || has("gt 1030") || has(" mx") || has("rx 5") || has("rx 4")))cap=2u;
    if(ram && ram<=gib*17/2)cap=std::min(cap,2u);
    if(settings.hdTextures)cap=std::max(cap,2u);
    if(settings.internalScale<=cap)return;
    std::cout<<"[display] internal_scale=auto lowered "<<settings.internalScale<<"x to "<<cap<<"x for "<<description
             <<" video_mb="<<video/(1024u*1024u)<<" ram_mb="<<ram/(1024u*1024u)<<"\n";
    settings.internalScale=cap;set_graphics_settings(settings);
}
std::string preference=[] {const auto value=std::getenv("OPENKAI2_RENDERER");return value ? std::string(value) : std::string("auto");}();
}
void set_renderer_preference(const std::string& value){if(value!="auto" && value!="gpu" && value!="vulkan" && value!="cpu")throw std::runtime_error("Renderer must be auto, gpu, vulkan, or cpu");preference=value;}
const std::string& renderer_preference(){return preference;}
void initialize_window_gpu(){
    if(preference=="cpu"){std::cout<<"[renderer] selected=cpu reason=requested\n";return;}
    if(preference!="auto" && preference!="gpu" && preference!="vulkan")throw std::runtime_error("OPENKAI2_RENDERER must be auto, gpu, vulkan, or cpu");
    std::string error;
    if(preference=="vulkan"){
        if(gpu::initialize_vulkan(error)){std::cout<<"[renderer] selected=vulkan-compute device="<<gpu::current_device()->description()<<"\n";cap_automatic_scale(gpu::current_device()->description());return;}
        std::cerr<<"[renderer] vulkan unavailable reason="<<error<<", trying OpenGL\n";error.clear();
    }
    GLFWwindow* share=glfwGetCurrentContext();const bool ownsGlfw=share==nullptr;
    GLFWwindow* worker=nullptr;
    if(glfwInit()){
        glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
        worker=glfwCreateWindow(1,1,"OpenKai2 GPU renderer",nullptr,share);
        glfwWindowHint(GLFW_VISIBLE,GLFW_TRUE);glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
    }
    if(worker){
        gpu::Context context;
        context.bind=[worker]{glfwMakeContextCurrent(worker);if(glfwGetCurrentContext()!=worker)throw std::runtime_error("Cannot bind the GPU renderer context");};
        context.release=[]{glfwMakeContextCurrent(nullptr);};
        context.resolve=[](const char* name)->void*{return reinterpret_cast<void*>(glfwGetProcAddress(name));};
        context.destroy=[worker,ownsGlfw]{glfwDestroyWindow(worker);if(ownsGlfw)glfwTerminate();};
        if(gpu::initialize_device(std::move(context),error)){
            std::cout<<"[renderer] selected=opengl-compute device="<<gpu::current_device()->description()<<"\n";cap_automatic_scale(gpu::current_device()->description());return;
        }
    }else{
        const char* reason=nullptr;glfwGetError(&reason);error=reason ? reason : "OpenGL 4.3 context creation failed";
        if(ownsGlfw)glfwTerminate();
    }
    std::cerr<<"[renderer] selected=cpu reason="<<error<<"\n";
    if(preference=="gpu")throw std::runtime_error("GPU renderer unavailable: "+error);
}
void shutdown_window_gpu(){
    if(gpu::current_device()){
        const auto s=gpu_render_statistics();
        std::cout<<"[renderer:gpu] gpu_draws="<<s.gpuDraws<<" cpu_draws="<<s.cpuDraws<<" batches="<<s.batches
            <<" tiles="<<s.tiles<<" uploaded_bytes="<<s.uploads<<" readback_bytes="<<s.readbacks<<" culled_draws="<<s.culled<<" gpu_transfers="<<s.gpuTransfers<<'\n';
        std::cout<<"[renderer:palette] loads="<<s.paletteLoads<<" reused="<<s.paletteReuses<<'\n';
        for(const auto& [key,count]:gpu_fallback_counts())std::cout<<"[renderer:fallback] "<<key<<" draws="<<count<<'\n';
        for(const auto& wait:gpu_wait_counts())if(wait[1])
            std::cout<<"[renderer:wait] reason="<<gpu_wait_name(unsigned(wait[0]))<<" waits="<<wait[1]<<" total_us="<<wait[2]<<'\n';
    }
    gpu::shutdown_device();
}
}
