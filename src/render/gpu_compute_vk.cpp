// SPDX-License-Identifier: GPL-3.0-only
#include "gpu_compute.h"
#include <algorithm>
#include <memory>
#include <mutex>
#include <chrono>
#include <iostream>
#include <vector>
#include <cstring>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#ifndef OPENKAI2_NO_VULKAN
#define VK_NO_PROTOTYPES
#include "vulkan/vulkan_core.h"
#include "openkai2_gpu_spirv.h"
#endif

namespace openkai2::gpu {
#ifdef OPENKAI2_NO_VULKAN
std::unique_ptr<Compute> make_vulkan_compute(std::string &error) {
    error = "this build has no Vulkan support";
    return nullptr;
}
bool vulkan_available() noexcept { return false; }
#else
namespace {
constexpr uint32_t bindingCount = 9;
#define OPENKAI2_VK_INSTANCE(X)                                                                                       \
    X(vkDestroyInstance) X(vkEnumeratePhysicalDevices) X(vkGetPhysicalDeviceProperties)                                \
    X(vkGetPhysicalDeviceFeatures) X(vkGetPhysicalDeviceQueueFamilyProperties) X(vkGetPhysicalDeviceMemoryProperties) \
    X(vkCreateDevice) X(vkGetDeviceProcAddr)
#define OPENKAI2_VK_DEVICE(X)                                                                                         \
    X(vkDestroyDevice) X(vkGetDeviceQueue) X(vkCreateBuffer) X(vkDestroyBuffer) X(vkGetBufferMemoryRequirements)      \
    X(vkAllocateMemory) X(vkFreeMemory) X(vkQueueWaitIdle) X(vkBindBufferMemory) X(vkMapMemory) X(vkUnmapMemory)                          \
    X(vkCreateShaderModule) X(vkDestroyShaderModule) X(vkCreateDescriptorSetLayout) X(vkDestroyDescriptorSetLayout)   \
    X(vkCreatePipelineLayout) X(vkDestroyPipelineLayout) X(vkCreateComputePipelines) X(vkDestroyPipeline)             \
    X(vkCreateDescriptorPool) X(vkDestroyDescriptorPool) X(vkAllocateDescriptorSets) X(vkUpdateDescriptorSets)        \
    X(vkCreateCommandPool) X(vkDestroyCommandPool) X(vkAllocateCommandBuffers) X(vkBeginCommandBuffer)                \
    X(vkEndCommandBuffer) X(vkResetCommandBuffer) X(vkCmdBindPipeline) X(vkCmdBindDescriptorSets) X(vkCmdDispatch)    \
    X(vkCmdCopyBuffer) X(vkCmdPipelineBarrier) X(vkCmdFillBuffer) X(vkQueueSubmit) X(vkCreateFence)                    \
    X(vkDestroyFence) X(vkWaitForFences) X(vkResetFences) X(vkDeviceWaitIdle)                                \
    X(vkCreateQueryPool) X(vkDestroyQueryPool) X(vkCmdResetQueryPool) X(vkCmdWriteTimestamp) X(vkGetQueryPoolResults)

struct Loader {
    PFN_vkGetInstanceProcAddr getInstanceProcAddr = nullptr;
    PFN_vkCreateInstance createInstance = nullptr;
    void *library = nullptr;
    bool load() {
        if (getInstanceProcAddr)
            return true;
#ifdef _WIN32
        library = reinterpret_cast<void *>(LoadLibraryW(L"vulkan-1.dll"));
        if (!library)
            return false;
        getInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
            reinterpret_cast<void *>(GetProcAddress(static_cast<HMODULE>(library), "vkGetInstanceProcAddr")));
#else
        library = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
        if (!library)
            return false;
        getInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(library, "vkGetInstanceProcAddr"));
#endif
        if (getInstanceProcAddr)
            createInstance =
                reinterpret_cast<PFN_vkCreateInstance>(getInstanceProcAddr(VK_NULL_HANDLE, "vkCreateInstance"));
        return getInstanceProcAddr && createInstance;
    }
};
Loader &loader() {
    static Loader value;
    return value;
}

void check(VkResult result, const char *operation) {
    if (result != VK_SUCCESS)
        throw std::runtime_error(std::string("Vulkan ") + operation + " failed (" + std::to_string(int(result)) + ")");
}

#define DECLARE(name) PFN_##name name = nullptr;
OPENKAI2_VK_INSTANCE(DECLARE)
OPENKAI2_VK_DEVICE(DECLARE)
#undef DECLARE

struct Buffer {
    VkBuffer handle = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
    void *mapped = nullptr;
};

struct Shared {
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t family = 0;
    VkPhysicalDeviceMemoryProperties memory{};
    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline raster = VK_NULL_HANDLE, transfer = VK_NULL_HANDLE, hires = VK_NULL_HANDLE, seed = VK_NULL_HANDLE, resolve = VK_NULL_HANDLE;
    std::string description;
    size_t storageWords = 0;
    float timestampPeriod = 0.0f;
    uint64_t timestampMask = 0;

    Shared() {
        try {
            open();
        } catch (...) {
            close();
            throw;
        }
    }
    ~Shared() { close(); }
    Shared(const Shared &) = delete;
    Shared &operator=(const Shared &) = delete;

  private:
    void open() {
        if (!loader().load())
            throw std::runtime_error("Vulkan is not installed");
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName = "OpenKai2";
        app.apiVersion = VK_API_VERSION_1_0;
        VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        info.pApplicationInfo = &app;
        check(loader().createInstance(&info, nullptr, &instance), "instance creation");
#define LOAD_INSTANCE(name)                                                                                          \
    name = reinterpret_cast<PFN_##name>(loader().getInstanceProcAddr(instance, #name));                              \
    if (!name)                                                                                                       \
        throw std::runtime_error("Vulkan function unavailable: " #name);
        OPENKAI2_VK_INSTANCE(LOAD_INSTANCE)
#undef LOAD_INSTANCE
        choose_device();
        create_layout();
    }

    void close() {
        if (device && vkDestroyDevice) {
            if (vkDeviceWaitIdle)
                vkDeviceWaitIdle(device);
            if (hires)
                vkDestroyPipeline(device, hires, nullptr);
            if (seed)
                vkDestroyPipeline(device, seed, nullptr);
            if (resolve)
                vkDestroyPipeline(device, resolve, nullptr);
            if (raster)
                vkDestroyPipeline(device, raster, nullptr);
            if (transfer)
                vkDestroyPipeline(device, transfer, nullptr);
            if (pipelineLayout)
                vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
            if (setLayout)
                vkDestroyDescriptorSetLayout(device, setLayout, nullptr);
            vkDestroyDevice(device, nullptr);
            device = VK_NULL_HANDLE;
        }
        if (instance && vkDestroyInstance)
            vkDestroyInstance(instance, nullptr);
        instance = VK_NULL_HANDLE;
    }

    void choose_device() {
        uint32_t count = 0;
        check(vkEnumeratePhysicalDevices(instance, &count, nullptr), "device listing");
        std::vector<VkPhysicalDevice> devices(count);
        check(vkEnumeratePhysicalDevices(instance, &count, devices.data()), "device listing");
        int best = -1;
        for (auto candidate : devices) {
            VkPhysicalDeviceProperties properties{};
            vkGetPhysicalDeviceProperties(candidate, &properties);
            uint32_t families = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &families, nullptr);
            std::vector<VkQueueFamilyProperties> list(families);
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &families, list.data());
            for (uint32_t i = 0; i < families; ++i) {
                if (!(list[i].queueFlags & VK_QUEUE_COMPUTE_BIT))
                    continue;
                const int score = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU     ? 4
                                  : properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 3
                                  : properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU    ? 2
                                                                                                    : 1;
                if (score > best) {
                    best = score;
                    physical = candidate;
                    family = i;
                    description = std::string(properties.deviceName) + " (Vulkan)";
                    storageWords = properties.limits.maxStorageBufferRange / 4u;
                    timestampPeriod = properties.limits.timestampPeriod;
                    timestampMask = list[i].timestampValidBits >= 64u ? ~uint64_t(0)
                                    : list[i].timestampValidBits ? (uint64_t(1) << list[i].timestampValidBits) - 1u
                                                                 : 0u;
                }
                break;
            }
        }
        if (best < 0)
            throw std::runtime_error("no Vulkan GPU supports compute");
        const float priority = 1.0f;
        VkDeviceQueueCreateInfo request{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        request.queueFamilyIndex = family;
        request.queueCount = 1;
        request.pQueuePriorities = &priority;
        VkPhysicalDeviceFeatures enabled{};
        VkDeviceCreateInfo info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        info.queueCreateInfoCount = 1;
        info.pQueueCreateInfos = &request;
        info.pEnabledFeatures = &enabled;
        check(vkCreateDevice(physical, &info, nullptr, &device), "device creation");
#define LOAD_DEVICE(name)                                                                                            \
    name = reinterpret_cast<PFN_##name>(vkGetDeviceProcAddr(device, #name));                                         \
    if (!name)                                                                                                       \
        throw std::runtime_error("Vulkan function unavailable: " #name);
        OPENKAI2_VK_DEVICE(LOAD_DEVICE)
#undef LOAD_DEVICE
        vkGetDeviceQueue(device, family, 0, &queue);
        vkGetPhysicalDeviceMemoryProperties(physical, &memory);
    }

    VkPipeline make_pipeline(const uint32_t *code, size_t bytes) {
        VkShaderModuleCreateInfo module{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        module.codeSize = bytes;
        module.pCode = code;
        VkShaderModule shader = VK_NULL_HANDLE;
        check(vkCreateShaderModule(device, &module, nullptr, &shader), "shader creation");
        VkComputePipelineCreateInfo info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        info.stage.module = shader;
        info.stage.pName = "main";
        info.layout = pipelineLayout;
        VkPipeline pipeline = VK_NULL_HANDLE;
        const VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline);
        vkDestroyShaderModule(device, shader, nullptr);
        check(result, "pipeline creation");
        return pipeline;
    }

    void create_layout() {
        VkDescriptorSetLayoutBinding bindings[bindingCount]{};
        for (uint32_t i = 0; i < bindingCount; ++i) {
            bindings[i].binding = i;
            bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            bindings[i].descriptorCount = 1;
            bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        }
        VkDescriptorSetLayoutCreateInfo layout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        layout.bindingCount = bindingCount;
        layout.pBindings = bindings;
        check(vkCreateDescriptorSetLayout(device, &layout, nullptr, &setLayout), "descriptor layout creation");
        VkPipelineLayoutCreateInfo pipeline{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pipeline.setLayoutCount = 1;
        pipeline.pSetLayouts = &setLayout;
        check(vkCreatePipelineLayout(device, &pipeline, nullptr, &pipelineLayout), "pipeline layout creation");
        raster = make_pipeline(openkai2_raster_spirv, sizeof(openkai2_raster_spirv));
        transfer = make_pipeline(openkai2_transfer_spirv, sizeof(openkai2_transfer_spirv));
        hires = make_pipeline(openkai2_hires_spirv, sizeof(openkai2_hires_spirv));
        seed = make_pipeline(openkai2_seed_spirv, sizeof(openkai2_seed_spirv));
        resolve = make_pipeline(openkai2_resolve_spirv, sizeof(openkai2_resolve_spirv));
    }
};

std::shared_ptr<Shared> acquire_shared() {
    static std::weak_ptr<Shared> cached;
    if (auto existing = cached.lock())
        return existing;
    auto created = std::make_shared<Shared>();
    cached = created;
    return created;
}

constexpr unsigned timeKinds = 7u;
const char *const timeNames[timeKinds] = {"raster", "raster_hires", "seed", "resolve", "transfer", "upload", "other"};

void record_gpu_time(double nanoseconds, const double (&parts)[timeKinds]) {
    static std::mutex mutex;
    static std::chrono::steady_clock::time_point began{};
    static double busy = 0.0, kinds[timeKinds]{};
    static uint64_t submissions = 0;
    std::lock_guard lock(mutex);
    const auto now = std::chrono::steady_clock::now();
    if (began == std::chrono::steady_clock::time_point{})
        began = now;
    busy += nanoseconds;
    for (unsigned i = 0; i < timeKinds; ++i)
        kinds[i] += parts[i];
    ++submissions;
    const double seconds = std::chrono::duration<double>(now - began).count();
    if (seconds >= 30.0) {
        std::cout << "[renderer:gpu-time] seconds=" << int(seconds + 0.5) << " gpu_ms=" << uint64_t(busy / 1e6)
                  << " busy_percent=" << int(busy / 1e7 / seconds) << " submissions=" << submissions;
        for (unsigned i = 0; i < timeKinds; ++i)
            std::cout << ' ' << timeNames[i] << "_ms=" << uint64_t(kinds[i] / 1e6);
        std::cout << std::endl;
        began = now;
        busy = 0.0;
        for (auto &kind : kinds)
            kind = 0.0;
        submissions = 0;
    }
}

class VulkanCompute final : public Compute {
  public:
    VulkanCompute() : shared_(acquire_shared()) {
        device_ = shared_->device;
        queue_ = shared_->queue;
        try {
            VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
            pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            pool.queueFamilyIndex = shared_->family;
            check(vkCreateCommandPool(device_, &pool, nullptr, &pool_), "command pool creation");
            VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
            allocate.commandPool = pool_;
            allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocate.commandBufferCount = 1;
            VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
            for (auto &frame : frames_) {
                check(vkAllocateCommandBuffers(device_, &allocate, &frame.command), "command buffer creation");
                check(vkCreateFence(device_, &fence, nullptr, &frame.fence), "fence creation");
            }
            check(vkAllocateCommandBuffers(device_, &allocate, &presentCommand_), "command buffer creation");
            if (shared_->timestampMask && shared_->timestampPeriod > 0.0f) {
                VkQueryPoolCreateInfo queries{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
                queries.queryType = VK_QUERY_TYPE_TIMESTAMP;
                queries.queryCount = frameCount * timeStride;
                if (vkCreateQueryPool(device_, &queries, nullptr, &timestamps_) != VK_SUCCESS)
                    timestamps_ = VK_NULL_HANDLE;
            }
            check(vkCreateFence(device_, &fence, nullptr, &presentFence_), "fence creation");
            VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, bindingCount * 2u};
            VkDescriptorPoolCreateInfo descriptors{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
            descriptors.maxSets = 2;
            descriptors.poolSizeCount = 1;
            descriptors.pPoolSizes = &size;
            check(vkCreateDescriptorPool(device_, &descriptors, nullptr, &descriptorPool_), "descriptor pool creation");
            VkDescriptorSetLayout layouts[2] = {shared_->setLayout, shared_->setLayout};
            VkDescriptorSetAllocateInfo sets{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
            sets.descriptorPool = descriptorPool_;
            sets.descriptorSetCount = 2;
            sets.pSetLayouts = layouts;
            VkDescriptorSet allocated[2]{};
            check(vkAllocateDescriptorSets(device_, &sets, allocated), "descriptor set creation");
            rasterSet_ = allocated[0];
            transferSet_ = allocated[1];
        } catch (...) {
            release();
            throw;
        }
    }
    ~VulkanCompute() override { release(); }

    const std::string &description() const override { return shared_->description; }

    void create(const std::vector<uint32_t> &tables) override {
        vram_ = make_buffer(4u * 1024u * 1024u, false);
        tables_ = make_buffer(std::max<VkDeviceSize>(tables.size() * 4u, 256u), false);
        commands_ = make_buffer(2u << 20u, false);
        bins_ = make_buffer(1u << 20u, false);
        references_ = make_buffer(4u << 20u, false);
        palette_ = make_buffer(1u << 20u, false);
        job_ = make_buffer(1u << 20u, false);
        for (auto &frame : frames_)
            frame.staging = make_buffer(16u << 20u, true);
        readback_ = make_buffer(4u * 1024u * 1024u, true);
        hiColor_ = make_buffer(256u, false);
        hiDepth_ = make_buffer(256u, false);
        hiParams_ = make_buffer(256u, false);
        write_sets();
        begin();
        vkCmdFillBuffer(command_, vram_.handle, 0, VK_WHOLE_SIZE, 0);
        barrier();
        copy_in(tables_, 0, tables.data(), tables.size() * 4u);
        barrier();
        flush();
    }

    void upload(uint32_t offset, const void *data, size_t size) override {
        if (!size)
            return;
        copy_in(vram_, offset, data, size);
        barrier();
        mark(5u);
    }

    void raster(const std::vector<uint32_t> &commands, const std::vector<uint32_t> &bins,
                const std::vector<uint32_t> &references, const std::vector<uint32_t> &palette) override {
        if (bins.size() < 4u)
            return;
        ensure(commands_, commands.size() * 4u);
        ensure(bins_, bins.size() * 4u);
        ensure(references_, references.size() * 4u);
        ensure(palette_, palette.size() * 4u);
        copy_in(commands_, 0, commands.data(), commands.size() * 4u);
        copy_in(bins_, 0, bins.data(), bins.size() * 4u);
        copy_in(references_, 0, references.data(), references.size() * 4u);
        copy_in(palette_, 0, palette.data(), palette.size() * 4u);
        barrier();
        vkCmdBindPipeline(command_, VK_PIPELINE_BIND_POINT_COMPUTE, shared_->raster);
        vkCmdBindDescriptorSets(command_, VK_PIPELINE_BIND_POINT_COMPUTE, shared_->pipelineLayout, 0, 1, &rasterSet_, 0, nullptr);
        vkCmdDispatch(command_, uint32_t(bins.size() / 4u), 1, 1);
        barrier();
        mark(0u);
        if (++dispatches_ >= 4u || frames_[current_].head >= (8u << 20u))
            submit();
    }

    void transfer(const std::vector<uint32_t> &job, unsigned pixels) override {
        if (!pixels)
            return;
        ensure(job_, job.size() * 4u);
        copy_in(job_, 0, job.data(), job.size() * 4u);
        barrier();
        vkCmdBindPipeline(command_, VK_PIPELINE_BIND_POINT_COMPUTE, shared_->transfer);
        vkCmdBindDescriptorSets(command_, VK_PIPELINE_BIND_POINT_COMPUTE, shared_->pipelineLayout, 0, 1, &transferSet_, 0,
                                nullptr);
        const unsigned groups = pixels / 64u + unsigned(pixels % 64u != 0u);
        const unsigned rows = (groups + 65534u) / 65535u;
        vkCmdDispatch(command_, (groups + rows - 1u) / rows, rows, 1);
        barrier();
        mark(4u);
        if (++dispatches_ >= 4u)
            submit();
    }

    void read(const std::vector<Range> &ranges, uint8_t *destination) override {
        if (ranges.empty())
            return;
        begin();
        std::vector<VkBufferCopy> copies;
        for (const auto &range : ranges)
            if (range.size && range.offset + uint64_t(range.size) <= readback_.size)
                copies.push_back(VkBufferCopy{range.offset, range.offset, range.size});
        if (!copies.empty())
            vkCmdCopyBuffer(command_, vram_.handle, readback_.handle, uint32_t(copies.size()), copies.data());
        flush();
        for (const auto &copy : copies)
            std::memcpy(destination + copy.dstOffset, static_cast<uint8_t *>(readback_.mapped) + copy.dstOffset,
                        size_t(copy.size));
    }

    void finish() override { flush(); }

    bool hires_supported() const override { return true; }
    size_t storage_word_limit() const override { return shared_->storageWords; }

    void hires_reserve(size_t colorWords, size_t depthWords) override {
        ensure(hiColor_, VkDeviceSize(colorWords) * 4u);
        ensure(hiDepth_, VkDeviceSize(depthWords) * 4u);
    }

    void hires_raster(const std::vector<uint32_t> &params, unsigned groups) override {
        if (!groups)
            return;
        ensure(hiParams_, params.size() * 4u);
        copy_in(hiParams_, 0, params.data(), params.size() * 4u);
        barrier();
        vkCmdBindPipeline(command_, VK_PIPELINE_BIND_POINT_COMPUTE, shared_->hires);
        vkCmdBindDescriptorSets(command_, VK_PIPELINE_BIND_POINT_COMPUTE, shared_->pipelineLayout, 0, 1, &rasterSet_, 0,
                                nullptr);
        vkCmdDispatch(command_, groups, 1, 1);
        barrier();
        mark(1u);
        if (++dispatches_ >= 4u)
            submit();
    }

    void raster_hires(const std::vector<uint32_t> &commands, const std::vector<uint32_t> &bins,
                      const std::vector<uint32_t> &references, const std::vector<uint32_t> &palette,
                      const std::vector<uint32_t> &params, unsigned groups) override {
        if (bins.size() < 4u || !groups) {
            raster(commands, bins, references, palette);
            hires_raster(params, groups);
            return;
        }
        ensure(commands_, commands.size() * 4u);
        ensure(bins_, bins.size() * 4u);
        ensure(references_, references.size() * 4u);
        ensure(palette_, palette.size() * 4u);
        ensure(hiParams_, params.size() * 4u);
        copy_in(commands_, 0, commands.data(), commands.size() * 4u);
        copy_in(bins_, 0, bins.data(), bins.size() * 4u);
        copy_in(references_, 0, references.data(), references.size() * 4u);
        copy_in(palette_, 0, palette.data(), palette.size() * 4u);
        copy_in(hiParams_, 0, params.data(), params.size() * 4u);
        barrier();
        vkCmdBindDescriptorSets(command_, VK_PIPELINE_BIND_POINT_COMPUTE, shared_->pipelineLayout, 0, 1, &rasterSet_, 0, nullptr);
        vkCmdBindPipeline(command_, VK_PIPELINE_BIND_POINT_COMPUTE, shared_->raster);
        vkCmdDispatch(command_, uint32_t(bins.size() / 4u), 1, 1);
        vkCmdBindPipeline(command_, VK_PIPELINE_BIND_POINT_COMPUTE, shared_->hires);
        vkCmdDispatch(command_, groups, 1, 1);
        barrier();
        mark(1u);
        dispatches_ += 2u;
        if (dispatches_ >= 4u || frames_[current_].head >= (8u << 20u))
            submit();
    }

    void hires_seed(const std::vector<uint32_t> &params, unsigned groupsX, unsigned groupsY) override {
        if (!groupsX || !groupsY)
            return;
        ensure(hiParams_, params.size() * 4u);
        copy_in(hiParams_, 0, params.data(), params.size() * 4u);
        barrier();
        vkCmdBindPipeline(command_, VK_PIPELINE_BIND_POINT_COMPUTE, shared_->seed);
        vkCmdBindDescriptorSets(command_, VK_PIPELINE_BIND_POINT_COMPUTE, shared_->pipelineLayout, 0, 1, &rasterSet_, 0,
                                nullptr);
        vkCmdDispatch(command_, groupsX, groupsY, 1);
        barrier();
        mark(2u);
    }

    bool hires_resolve(const std::vector<uint32_t> &params, unsigned groupsX, unsigned groupsY) override {
        if (!groupsX || !groupsY || !shared_->resolve)
            return false;
        ensure(hiParams_, params.size() * 4u);
        copy_in(hiParams_, 0, params.data(), params.size() * 4u);
        barrier();
        vkCmdBindPipeline(command_, VK_PIPELINE_BIND_POINT_COMPUTE, shared_->resolve);
        vkCmdBindDescriptorSets(command_, VK_PIPELINE_BIND_POINT_COMPUTE, shared_->pipelineLayout, 0, 1, &rasterSet_, 0,
                                nullptr);
        vkCmdDispatch(command_, groupsX, groupsY, 1);
        barrier();
        mark(3u);
        return true;
    }

    void hires_read(size_t offsetWords, size_t words, uint32_t *destination) override {
        if (!words)
            return;
        const VkDeviceSize bytes = VkDeviceSize(words) * 4u;
        if (bytes > readback_.size) {
            flush();
            vkDeviceWaitIdle(device_);
            free_buffer(readback_);
            readback_ = make_buffer(bytes, true);
        }
        begin();
        const VkBufferCopy copy{VkDeviceSize(offsetWords) * 4u, 0, bytes};
        vkCmdCopyBuffer(command_, hiColor_.handle, readback_.handle, 1, &copy);
        flush();
        std::memcpy(destination, readback_.mapped, size_t(bytes));
    }

    void hires_upload(size_t offsetWords, const uint32_t *source, size_t words) override {
        const VkDeviceSize offset = VkDeviceSize(offsetWords) * 4u, bytes = VkDeviceSize(words) * 4u;
        if (!words || offset + bytes > hiColor_.size)
            return;
        for (VkDeviceSize done = 0; done < bytes;) {
            const VkDeviceSize part = std::min<VkDeviceSize>(bytes - done, 8u << 20u);
            copy_in(hiColor_, offset + done, reinterpret_cast<const uint8_t *>(source) + done, size_t(part));
            done += part;
        }
        barrier();
        mark(5u);
    }

    std::function<void()> present_read(const std::vector<Range> &ranges, uint8_t *destination, size_t hiresOffset,
                                       size_t hiresWords, uint32_t *hiresDestination) override {
        std::lock_guard lock(presentMutex_);
        if (presentPending_) {
            check(vkWaitForFences(device_, 1, &presentFence_, VK_TRUE, UINT64_MAX), "fence wait");
            presentPending_ = false;
        }
        std::vector<VkBufferCopy> copies;
        VkDeviceSize nativeBytes = 0;
        for (const auto &range : ranges)
            if (range.size && range.offset + uint64_t(range.size) <= vram_.size) {
                copies.push_back(VkBufferCopy{range.offset, range.offset, range.size});
                nativeBytes = std::max<VkDeviceSize>(nativeBytes, VkDeviceSize(range.offset) + range.size);
            }
        const VkDeviceSize hiresBytes =
            hiresWords && (hiresOffset + hiresWords) * 4u <= hiColor_.size ? VkDeviceSize(hiresWords) * 4u : 0u;
        if (copies.empty() && !hiresBytes)
            return {};
        if (nativeBytes > presentNative_.size) {
            free_buffer(presentNative_);
            presentNative_ = make_buffer(vram_.size, true);
        }
        if (hiresBytes > presentHires_.size) {
            free_buffer(presentHires_);
            presentHires_ = make_buffer(hiresBytes, true);
        }
        submit();
        check(vkResetFences(device_, 1, &presentFence_), "fence reset");
        check(vkResetCommandBuffer(presentCommand_, 0), "command reset");
        VkCommandBufferBeginInfo info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        check(vkBeginCommandBuffer(presentCommand_, &info), "command recording");
        VkMemoryBarrier before{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        before.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
        before.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(presentCommand_, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &before, 0, nullptr, 0, nullptr);
        if (!copies.empty())
            vkCmdCopyBuffer(presentCommand_, vram_.handle, presentNative_.handle, uint32_t(copies.size()), copies.data());
        if (hiresBytes) {
            const VkBufferCopy copy{VkDeviceSize(hiresOffset) * 4u, 0, hiresBytes};
            vkCmdCopyBuffer(presentCommand_, hiColor_.handle, presentHires_.handle, 1, &copy);
        }
        VkMemoryBarrier after{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        after.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        after.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(presentCommand_, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &after, 0,
                             nullptr, 0, nullptr);
        check(vkEndCommandBuffer(presentCommand_), "command recording");
        VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &presentCommand_;
        check(vkQueueSubmit(queue_, 1, &submitInfo, presentFence_), "submission");
        presentPending_ = true;
        return [this, copies = std::move(copies), destination, hiresBytes, hiresDestination] {
            std::lock_guard lock(presentMutex_);
            if (presentPending_) {
                check(vkWaitForFences(device_, 1, &presentFence_, VK_TRUE, UINT64_MAX), "fence wait");
                presentPending_ = false;
            }
            for (const auto &copy : copies)
                std::memcpy(destination + copy.dstOffset, static_cast<uint8_t *>(presentNative_.mapped) + copy.dstOffset,
                            size_t(copy.size));
            if (hiresBytes)
                std::memcpy(hiresDestination, presentHires_.mapped, size_t(hiresBytes));
        };
    }

    void destroy() override { release(); }

  private:
    static constexpr unsigned frameCount = 4u, timeStride = 66u;
    struct Frame {
        VkCommandBuffer command = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
        Buffer staging;
        VkDeviceSize head = 0;
        bool pending = false, timed = false;
        unsigned marks = 0u;
        uint8_t kinds[timeStride - 2u]{};
    };

    void mark(unsigned kind) {
        auto &frame = frames_[current_];
        if (!timestamps_ || !recording_ || frame.marks >= timeStride - 2u)
            return;
        vkCmdWriteTimestamp(command_, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timestamps_,
                            current_ * timeStride + 2u + frame.marks);
        frame.kinds[frame.marks++] = uint8_t(kind);
    }

    uint32_t memory_type(uint32_t bits, VkMemoryPropertyFlags wanted, VkMemoryPropertyFlags fallback) const {
        for (auto flags : {wanted, fallback})
            for (uint32_t i = 0; i < shared_->memory.memoryTypeCount; ++i)
                if ((bits & (1u << i)) && (shared_->memory.memoryTypes[i].propertyFlags & flags) == flags)
                    return i;
        throw std::runtime_error("no suitable Vulkan memory");
    }

    Buffer make_buffer(VkDeviceSize size, bool host) {
        Buffer buffer;
        buffer.size = std::max<VkDeviceSize>(size, 256u);
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        info.size = buffer.size;
        info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        check(vkCreateBuffer(device_, &info, nullptr, &buffer.handle), "buffer creation");
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(device_, buffer.handle, &requirements);
        VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocate.allocationSize = requirements.size;
        allocate.memoryTypeIndex =
            host ? memory_type(requirements.memoryTypeBits,
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                                   VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
                 : memory_type(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);
        check(vkAllocateMemory(device_, &allocate, nullptr, &buffer.memory), "memory allocation");
        check(vkBindBufferMemory(device_, buffer.handle, buffer.memory, 0), "memory binding");
        if (host)
            check(vkMapMemory(device_, buffer.memory, 0, VK_WHOLE_SIZE, 0, &buffer.mapped), "memory mapping");
        return buffer;
    }

    void free_buffer(Buffer &buffer) {
        if (!device_)
            return;
        if (buffer.mapped)
            vkUnmapMemory(device_, buffer.memory);
        if (buffer.handle)
            vkDestroyBuffer(device_, buffer.handle, nullptr);
        if (buffer.memory)
            vkFreeMemory(device_, buffer.memory, nullptr);
        buffer = Buffer{};
    }

    void write_sets() {
        const Buffer *raster[bindingCount] = {&vram_,    &commands_, &bins_,    &references_, &palette_,
                                              &tables_, &hiColor_,  &hiDepth_, &hiParams_};
        const Buffer *transfer[bindingCount] = {&vram_,    &job_,     &bins_,    &references_, &palette_,
                                                &tables_, &hiColor_, &hiDepth_, &hiParams_};
        VkDescriptorBufferInfo infos[bindingCount * 2u]{};
        VkWriteDescriptorSet writes[bindingCount * 2u]{};
        for (uint32_t i = 0; i < bindingCount * 2u; ++i) {
            const Buffer *buffer = i < bindingCount ? raster[i] : transfer[i - bindingCount];
            infos[i] = VkDescriptorBufferInfo{buffer->handle, 0, VK_WHOLE_SIZE};
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = i < bindingCount ? rasterSet_ : transferSet_;
            writes[i].dstBinding = i % bindingCount;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[i].pBufferInfo = &infos[i];
        }
        vkUpdateDescriptorSets(device_, bindingCount * 2u, writes, 0, nullptr);
    }

    void wait(Frame &frame) {
        if (!frame.pending)
            return;
        check(vkWaitForFences(device_, 1, &frame.fence, VK_TRUE, UINT64_MAX), "fence wait");
        if (frame.timed) {
            uint64_t stamps[timeStride]{};
            const uint32_t first = uint32_t(&frame - frames_) * timeStride, count = 2u + frame.marks;
            if (vkGetQueryPoolResults(device_, timestamps_, first, count, sizeof(uint64_t) * count, stamps,
                                      sizeof(uint64_t), VK_QUERY_RESULT_64_BIT) == VK_SUCCESS) {
                const double period = double(shared_->timestampPeriod);
                const auto span = [&](uint64_t from, uint64_t to) { return double((to - from) & shared_->timestampMask) * period; };
                double parts[timeKinds]{};
                uint64_t previous = stamps[0];
                for (unsigned i = 0; i < frame.marks; ++i) {
                    parts[frame.kinds[i]] += span(previous, stamps[2u + i]);
                    previous = stamps[2u + i];
                }
                parts[timeKinds - 1u] += span(previous, stamps[1]);
                record_gpu_time(span(stamps[0], stamps[1]), parts);
            }
            frame.timed = false;
        }
        check(vkResetFences(device_, 1, &frame.fence), "fence reset");
        check(vkResetCommandBuffer(frame.command, 0), "command reset");
        frame.pending = false;
        frame.head = 0;
    }

    void begin() {
        if (recording_)
            return;
        auto &frame = frames_[current_];
        wait(frame);
        command_ = frame.command;
        VkCommandBufferBeginInfo info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        check(vkBeginCommandBuffer(command_, &info), "command recording");
        recording_ = true;
        if (timestamps_) {
            const uint32_t first = current_ * timeStride;
            vkCmdResetQueryPool(command_, timestamps_, first, timeStride);
            vkCmdWriteTimestamp(command_, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, timestamps_, first);
            frame.marks = 0u;
        }
    }

    void barrier() {
        begin();
        VkMemoryBarrier memory{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        memory.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
        memory.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT |
                               VK_ACCESS_TRANSFER_WRITE_BIT;
        const VkPipelineStageFlags stages = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
        vkCmdPipelineBarrier(command_, stages, stages, 0, 1, &memory, 0, nullptr, 0, nullptr);
    }

    void submit() {
        dispatches_ = 0;
        if (!recording_)
            return;
        auto &frame = frames_[current_];
        if (timestamps_) {
            vkCmdWriteTimestamp(command_, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timestamps_, current_ * timeStride + 1u);
            frame.timed = true;
        }
        check(vkEndCommandBuffer(command_), "command recording");
        recording_ = false;
        VkSubmitInfo info{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        info.commandBufferCount = 1;
        info.pCommandBuffers = &command_;
        check(vkQueueSubmit(queue_, 1, &info, frame.fence), "submission");
        frame.pending = true;
        current_ = (current_ + 1u) % frameCount;
    }

    void flush() {
        submit();
        for (auto &frame : frames_)
            wait(frame);
    }

    void ensure(Buffer &buffer, VkDeviceSize size) {
        if (size <= buffer.size)
            return;
        flush();
        vkDeviceWaitIdle(device_);
        free_buffer(buffer);
        buffer = make_buffer(std::max<VkDeviceSize>(size, buffer.size * 2u), false);
        write_sets();
    }

    VkDeviceSize stage(const void *data, size_t size) {
        const VkDeviceSize aligned = (VkDeviceSize(size) + 15u) & ~VkDeviceSize(15u);
        begin();
        if (frames_[current_].head + aligned > frames_[current_].staging.size) {
            submit();
            begin();
        }
        auto &frame = frames_[current_];
        if (aligned > frame.staging.size) {
            free_buffer(frame.staging);
            frame.staging = make_buffer(aligned * 2u, true);
        }
        const VkDeviceSize offset = frame.head;
        std::memcpy(static_cast<uint8_t *>(frame.staging.mapped) + offset, data, size);
        frame.head += aligned;
        return offset;
    }

    void copy_in(Buffer &target, VkDeviceSize offset, const void *data, size_t size) {
        if (!size)
            return;
        const VkDeviceSize from = stage(data, size);
        const VkBufferCopy copy{from, offset, size};
        vkCmdCopyBuffer(command_, frames_[current_].staging.handle, target.handle, 1, &copy);
    }

    void release() {
        if (!device_)
            return;
        if (recording_) {
            vkEndCommandBuffer(command_);
            recording_ = false;
        }
        vkQueueWaitIdle(queue_);
        for (auto *buffer : {&vram_, &tables_, &commands_, &bins_, &references_, &palette_, &job_, &readback_,
                             &hiColor_, &hiDepth_, &hiParams_, &presentNative_, &presentHires_})
            free_buffer(*buffer);
        if (presentFence_)
            vkDestroyFence(device_, presentFence_, nullptr);
        if (timestamps_)
            vkDestroyQueryPool(device_, timestamps_, nullptr);
        timestamps_ = VK_NULL_HANDLE;
        presentFence_ = VK_NULL_HANDLE;
        presentCommand_ = VK_NULL_HANDLE;
        presentPending_ = false;
        for (auto &frame : frames_) {
            free_buffer(frame.staging);
            if (frame.fence)
                vkDestroyFence(device_, frame.fence, nullptr);
            frame = Frame{};
        }
        if (descriptorPool_)
            vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
        if (pool_)
            vkDestroyCommandPool(device_, pool_, nullptr);
        descriptorPool_ = VK_NULL_HANDLE;
        pool_ = VK_NULL_HANDLE;
        device_ = VK_NULL_HANDLE;
        shared_.reset();
    }

    std::shared_ptr<Shared> shared_;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    VkCommandPool pool_ = VK_NULL_HANDLE;
    Frame frames_[frameCount];
    unsigned current_ = 0, dispatches_ = 0;
    VkCommandBuffer command_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSet rasterSet_ = VK_NULL_HANDLE, transferSet_ = VK_NULL_HANDLE;
    Buffer vram_, tables_, commands_, bins_, references_, palette_, job_, readback_, hiColor_, hiDepth_, hiParams_;
    Buffer presentNative_, presentHires_;
    VkCommandBuffer presentCommand_ = VK_NULL_HANDLE;
    VkFence presentFence_ = VK_NULL_HANDLE;
    VkQueryPool timestamps_ = VK_NULL_HANDLE;
    std::mutex presentMutex_;
    bool presentPending_ = false;
    bool recording_ = false;
};
}

std::unique_ptr<Compute> make_vulkan_compute(std::string &error) {
    try {
        return std::make_unique<VulkanCompute>();
    } catch (const std::exception &failure) {
        error = failure.what();
        return nullptr;
    }
}

bool vulkan_available() noexcept { return loader().load(); }
#endif
}
