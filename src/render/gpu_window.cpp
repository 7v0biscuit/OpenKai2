// SPDX-License-Identifier: GPL-3.0-only
#include "gpu_backend.h"
#include "gpu_device.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
namespace openkai2 {
namespace {
std::string preference=[] {const auto value=std::getenv("OPENKAI2_RENDERER");return value ? std::string(value) : std::string("auto");}();
}
void set_renderer_preference(const std::string& value){if(value!="auto" && value!="gpu" && value!="cpu")throw std::runtime_error("Renderer must be auto, gpu, or cpu");preference=value;}
const std::string& renderer_preference(){return preference;}
void initialize_window_gpu(){
    if(preference=="cpu"){std::cout<<"[renderer] selected=cpu reason=requested\n";return;}
    if(preference!="auto" && preference!="gpu")throw std::runtime_error("OPENKAI2_RENDERER must be auto, gpu, or cpu");
    GLFWwindow* share=glfwGetCurrentContext();const bool ownsGlfw=share==nullptr;
    std::string error;GLFWwindow* worker=nullptr;
    if(glfwInit()){
        glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
        worker=glfwCreateWindow(1,1,"OpenKai2 GPU renderer",nullptr,share);
        // Raylib's visible window already exists. Restore its default context
        // hints before any later window creation in the host application.
        glfwWindowHint(GLFW_VISIBLE,GLFW_TRUE);glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
    }
    if(worker){
        gpu::Context context;
        context.bind=[worker]{glfwMakeContextCurrent(worker);if(glfwGetCurrentContext()!=worker)throw std::runtime_error("Cannot bind the GPU renderer context");};
        context.release=[]{glfwMakeContextCurrent(nullptr);};
        context.resolve=[](const char* name)->void*{return reinterpret_cast<void*>(glfwGetProcAddress(name));};
        context.destroy=[worker,ownsGlfw]{glfwDestroyWindow(worker);if(ownsGlfw)glfwTerminate();};
        if(gpu::initialize_device(std::move(context),error)){
            std::cout<<"[renderer] selected=opengl-compute device="<<gpu::current_device()->description()<<"\n";return;
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
            <<" tiles="<<s.tiles<<" uploaded_bytes="<<s.uploads<<" readback_bytes="<<s.readbacks<<" culled_draws="<<s.culled<<'\n';
    }
    gpu::shutdown_device();
}
}
