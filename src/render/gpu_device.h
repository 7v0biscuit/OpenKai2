// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "gl_api.h"
#include "gpu_compute.h"
#include <memory>
#include <functional>
#include <string>
namespace openkai2::gpu {
struct Context {
    std::function<void()> bind,release,destroy;
    std::function<void*(const char*)> resolve;
    bool vulkan=false;
};
class Host {
public:
    virtual ~Host()=default;
    virtual std::unique_ptr<Compute> create()=0;
};
class Device {
public:
    explicit Device(Context context);
    ~Device();
    Device(const Device&)=delete;Device& operator=(const Device&)=delete;
    void post(std::function<void(Host&)> job);
    void invoke(std::function<void(Host&)> job);
    void stop();
    bool running() const;
    const std::string& description() const;
private:
    struct Impl;std::unique_ptr<Impl> m;
};
std::shared_ptr<Device> current_device();
bool initialize_device(Context context,std::string& error);
bool initialize_headless(std::string& error);
bool initialize_vulkan(std::string& error);
std::unique_ptr<Compute> make_opengl_compute(Api& gl,const std::string& description);
void shutdown_device();
}
