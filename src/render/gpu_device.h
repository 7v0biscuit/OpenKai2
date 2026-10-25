// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "gl_api.h"
#include <memory>
#include <functional>
#include <string>
namespace openkai2::gpu {
struct Context {
    std::function<void()> bind,release,destroy;
    std::function<void*(const char*)> resolve;
};
class Device {
public:
    explicit Device(Context context);
    ~Device();
    Device(const Device&)=delete;Device& operator=(const Device&)=delete;
    void post(std::function<void(Api&)> job);
    void invoke(std::function<void(Api&)> job);
    void stop();
    bool running() const;
    const std::string& description() const;
private:
    struct Impl;std::unique_ptr<Impl> m;
};
std::shared_ptr<Device> current_device();
bool initialize_device(Context context,std::string& error);
bool initialize_headless(std::string& error);
void shutdown_device();
}
