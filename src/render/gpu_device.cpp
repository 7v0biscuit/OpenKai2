// SPDX-License-Identifier: GPL-3.0-only
#include "gpu_device.h"
#include "work_timings.h"
#include <condition_variable>
#include <deque>
#include <future>
#include <mutex>
#include <thread>
#include <utility>
#ifndef _WIN32
#include <dlfcn.h>
#endif
namespace openkai2::gpu {
namespace {
constexpr size_t queueDepth=16u;
}
struct Device::Impl {
    Context context;Api api;std::string description;std::thread thread;
    std::mutex mutex;std::condition_variable changed;std::deque<std::function<void(Api&)>> jobs;
    bool stopping=false,finished=false;std::exception_ptr failure;
    explicit Impl(Context c):context(std::move(c)){}
    ~Impl(){if(context.destroy)context.destroy();}
};
Device::Device(Context context):m(std::make_unique<Impl>(std::move(context))) {
    auto ready=std::make_shared<std::promise<void>>();auto future=ready->get_future();
    m->thread=std::thread([this,ready]{
        try {
            m->context.bind();m->api.load(m->context.resolve);
            Int major=0,minor=0;m->api.GetIntegerv(0x821b,&major);m->api.GetIntegerv(0x821c,&minor);
            if(major<4 || (major==4 && minor<3))throw std::runtime_error("OpenGL 4.3 is required for GPU rasterization");
            Int bindings=0;m->api.GetIntegerv(0x90dd,&bindings);
            if(bindings<6)throw std::runtime_error("Insufficient shader storage buffer bindings");
            m->description=reinterpret_cast<const char*>(m->api.GetString(0x1f01));
            ready->set_value();
            for(;;){
                std::function<void(Api&)> job;
                {
                    std::unique_lock lock(m->mutex);m->changed.wait(lock,[&]{return m->stopping || !m->jobs.empty();});
                    if(m->jobs.empty() && m->stopping)break;
                    job=std::move(m->jobs.front());m->jobs.pop_front();m->changed.notify_all();
                }
                try{job(m->api);}catch(...){std::lock_guard lock(m->mutex);if(!m->failure)m->failure=std::current_exception();}
            }
            m->api.Finish();
        }catch(...){
            const auto error=std::current_exception();
            {std::lock_guard lock(m->mutex);if(!m->failure)m->failure=error;}
            try{ready->set_exception(error);}catch(const std::future_error&){}
        }
        try{m->context.release();}catch(...){}
        {std::lock_guard lock(m->mutex);m->finished=true;m->changed.notify_all();}
    });
    try{future.get();}catch(...){stop();throw;}
}
Device::~Device(){stop();}
void Device::post(std::function<void(Api&)> job) {
    std::unique_lock lock(m->mutex);
    if(m->jobs.size()>=queueDepth){RuntimeWorkTimer wait(RuntimeWork::gpuWait);m->changed.wait(lock,[&]{return m->jobs.size()<queueDepth || m->failure || m->stopping || m->finished;});}
    m->changed.wait(lock,[&]{return m->jobs.size()<queueDepth || m->failure || m->stopping || m->finished;});
    if(m->failure)std::rethrow_exception(m->failure);
    if(m->stopping || m->finished)throw std::runtime_error("GPU device has stopped");
    m->jobs.push_back(std::move(job));m->changed.notify_all();
}
void Device::invoke(std::function<void(Api&)> job) {
    auto done=std::make_shared<std::promise<void>>();auto future=done->get_future();
    post([job=std::move(job),done](Api& api){try{job(api);done->set_value();}catch(...){done->set_exception(std::current_exception());}});
    future.get();
}
void Device::stop(){
    if(!m)return;
    {std::lock_guard lock(m->mutex);m->stopping=true;m->changed.notify_all();}
    if(m->thread.joinable())m->thread.join();
    if(m->context.destroy){auto destroy=std::move(m->context.destroy);destroy();}
}
bool Device::running()const{std::lock_guard lock(m->mutex);return !m->stopping && !m->finished && !m->failure;}
const std::string& Device::description()const{return m->description;}
namespace {std::mutex deviceMutex;std::shared_ptr<Device> device;}
std::shared_ptr<Device> current_device(){std::lock_guard lock(deviceMutex);return device;}
bool initialize_device(Context context,std::string& error){
    try{auto created=std::make_shared<Device>(std::move(context));std::lock_guard lock(deviceMutex);device=std::move(created);return true;}
    catch(const std::exception& e){error=e.what();return false;}
}
void shutdown_device(){std::shared_ptr<Device> previous;{std::lock_guard lock(deviceMutex);previous=std::move(device);}if(previous)previous->stop();}
#ifndef _WIN32
namespace {
    struct Egl {
        void* library=nullptr;void* display=nullptr;void* context=nullptr;void* surface=nullptr;
        using Proc=void(*)();Proc(*getProc)(const char*)=nullptr;
        unsigned(*makeCurrent)(void*,void*,void*,void*)=nullptr;
        unsigned(*destroyContext)(void*,void*)=nullptr;unsigned(*destroySurface)(void*,void*)=nullptr;unsigned(*terminate)(void*)=nullptr;
        ~Egl(){if(display){if(makeCurrent)makeCurrent(display,nullptr,nullptr,nullptr);if(context && destroyContext)destroyContext(display,context);if(surface && destroySurface)destroySurface(display,surface);if(terminate)terminate(display);}if(library)dlclose(library);}
        template<class T>T symbol(const char* name){auto p=reinterpret_cast<T>(dlsym(library,name));if(!p)throw std::runtime_error(std::string("Missing EGL symbol: ")+name);return p;}
    };
}
#endif
bool initialize_headless(std::string& error){
#ifdef _WIN32
    error="Headless EGL validation is available on Linux";return false;
#else

    struct State{std::shared_ptr<Egl> egl;};auto state=std::make_shared<State>();
    Context c;
    c.bind=[state]{
        state->egl=std::make_shared<Egl>();auto& e=*state->egl;e.library=dlopen("libEGL.so.1",RTLD_NOW|RTLD_LOCAL);if(!e.library)throw std::runtime_error("EGL is unavailable");
        e.getProc=e.symbol<decltype(e.getProc)>("eglGetProcAddress");
        auto platform=reinterpret_cast<void*(*)(unsigned,void*,const int*)>(e.getProc("eglGetPlatformDisplayEXT"));
        if(!platform)throw std::runtime_error("Surfaceless EGL is unavailable");e.display=platform(0x31dd,nullptr,nullptr);
        auto initialize=e.symbol<unsigned(*)(void*,int*,int*)>("eglInitialize");int major=0,minor=0;if(!e.display || !initialize(e.display,&major,&minor))throw std::runtime_error("Cannot initialize surfaceless EGL");
        e.terminate=e.symbol<decltype(e.terminate)>("eglTerminate");e.destroyContext=e.symbol<decltype(e.destroyContext)>("eglDestroyContext");e.destroySurface=e.symbol<decltype(e.destroySurface)>("eglDestroySurface");e.makeCurrent=e.symbol<decltype(e.makeCurrent)>("eglMakeCurrent");
        auto bind=e.symbol<unsigned(*)(unsigned)>("eglBindAPI");if(!bind(0x30a2))throw std::runtime_error("Cannot bind the EGL OpenGL API");
        auto choose=e.symbol<unsigned(*)(void*,const int*,void**,int,int*)>("eglChooseConfig");const int attributes[]={0x3033,1,0x3040,8,0x3024,8,0x3023,8,0x3022,8,0x3038};void* config=nullptr;int count=0;
        if(!choose(e.display,attributes,&config,1,&count) || !count)throw std::runtime_error("Cannot choose an EGL OpenGL configuration");
        auto create=e.symbol<void*(*)(void*,void*,void*,const int*)>("eglCreateContext");const int version[]={0x3098,4,0x30fb,3,0x30fd,1,0x3038};e.context=create(e.display,config,nullptr,version);
        auto pbuffer=e.symbol<void*(*)(void*,void*,const int*)>("eglCreatePbufferSurface");const int size[]={0x3057,1,0x3056,1,0x3038};e.surface=pbuffer(e.display,config,size);
        if(!e.context || !e.surface || !e.makeCurrent(e.display,e.surface,e.surface,e.context))throw std::runtime_error("Cannot create an EGL OpenGL 4.3 context");
    };
    c.resolve=[state](const char* name)->void*{return reinterpret_cast<void*>(state->egl->getProc(name));};
    c.release=[state]{state->egl.reset();};c.destroy=[]{};
    return initialize_device(std::move(c),error);
#endif
}
}
