// SPDX-License-Identifier: GPL-3.0-only
#include "gpu_compute.h"
#include "gl_api.h"
#include "gpu_layout.h"
#include "openkai2_gpu_shader.h"
#include <algorithm>
#include <array>
#include <stdexcept>

namespace openkai2::gpu {
namespace {

UInt compile(Api &gl, const char *source) {
    const auto shader = gl.CreateShader(ComputeShader);
    gl.ShaderSource(shader, 1, &source, nullptr);
    gl.CompileShader(shader);
    int ok = 0, length = 0;
    gl.GetShaderiv(shader, 0x8b81, &ok);
    if (!ok) {
        gl.GetShaderiv(shader, 0x8b84, &length);
        std::vector<char> log(std::max(length, 1));
        gl.GetShaderInfoLog(shader, int(log.size()), nullptr, log.data());
        gl.DeleteShader(shader);
        throw std::runtime_error("GPU shader: " + std::string(log.data()));
    }
    const auto program = gl.CreateProgram();
    gl.AttachShader(program, shader);
    gl.LinkProgram(program);
    gl.DeleteShader(shader);
    gl.GetProgramiv(program, 0x8b82, &ok);
    if (!ok) {
        gl.GetProgramiv(program, 0x8b84, &length);
        std::vector<char> log(std::max(length, 1));
        gl.GetProgramInfoLog(program, int(log.size()), nullptr, log.data());
        gl.DeleteProgram(program);
        throw std::runtime_error("GPU program: " + std::string(log.data()));
    }
    return program;
}

class OpenGlCompute final : public Compute {
  public:
    OpenGlCompute(Api &gl, const std::string &description) : gl_(gl), description_(description) {}
    const std::string &description() const override { return description_; }

    void create(const std::vector<uint32_t> &tables) override {
        raster_ = compile(gl_, shaderSource);
        transfer_ = compile(gl_, transferShaderSource);
        Int bindings = 0;
        gl_.GetIntegerv(0x90dd, &bindings);
        if (bindings >= 9) {
            const std::string source = shaderSource, marker = "#version 430 core\n";
            const auto at = source.find(marker);
            if (at != std::string::npos) {
                const auto variant = [&](const char *name) {
                    std::string text = source;
                    text.insert(at + marker.size(), std::string("#define ") + name + "\n");
                    return compile(gl_, text.c_str());
                };
                hires_ = variant("HIRES");
                seed_ = variant("SEED");
                gl_.GenBuffers(int(extra_.size()), extra_.data());
                for (auto buffer : extra_) {
                    gl_.BindBuffer(ShaderStorageBuffer, buffer);
                    gl_.BufferData(ShaderStorageBuffer, 256, nullptr, DynamicDraw);
                }
            }
        }
        gl_.GenBuffers(1, &job_);
        gl_.GenBuffers(int(buffers_.size()), buffers_.data());
        gl_.BindBuffer(ShaderStorageBuffer, buffers_[0]);
        gl_.BufferData(ShaderStorageBuffer, VramBytes, nullptr, DynamicDraw);
        gl_.BindBuffer(ShaderStorageBuffer, buffers_[5]);
        gl_.BufferData(ShaderStorageBuffer, Size(tables.size() * 4u), tables.data(), DynamicDraw);
        gl_.check("GPU resource creation");
    }

    void upload(uint32_t offset, const void *data, size_t size) override {
        gl_.MemoryBarrier(ShaderStorageBarrier | BufferUpdateBarrier);
        gl_.BindBuffer(ShaderStorageBuffer, buffers_[0]);
        gl_.BufferSubData(ShaderStorageBuffer, offset, Size(size), data);
        gl_.check("GPU VRAM upload");
    }

    void raster(const std::vector<uint32_t> &commands, const std::vector<uint32_t> &bins,
                const std::vector<uint32_t> &references, const std::vector<uint32_t> &palette) override {
        const std::array<const std::vector<uint32_t> *, 4> arrays = {&commands, &bins, &references, &palette};
        for (unsigned i = 0; i < 4u; ++i) {
            gl_.BindBuffer(ShaderStorageBuffer, buffers_[i + 1u]);
            gl_.BufferData(ShaderStorageBuffer, Size(arrays[i]->size() * 4u), arrays[i]->data(), DynamicDraw);
        }
        for (unsigned i = 0; i < 6u; ++i)
            gl_.BindBufferBase(ShaderStorageBuffer, i, buffers_[i]);
        gl_.UseProgram(raster_);
        gl_.DispatchCompute(unsigned(bins.size() / 4u), 1u, 1u);
        gl_.MemoryBarrier(ShaderStorageBarrier | BufferUpdateBarrier);
        gl_.check("GPU raster dispatch");
    }

    void transfer(const std::vector<uint32_t> &job, unsigned pixels) override {
        gl_.BindBuffer(ShaderStorageBuffer, job_);
        gl_.BufferData(ShaderStorageBuffer, Size(job.size() * 4u), job.data(), DynamicDraw);
        gl_.BindBufferBase(ShaderStorageBuffer, 0, buffers_[0]);
        gl_.BindBufferBase(ShaderStorageBuffer, 1, job_);
        gl_.BindBufferBase(ShaderStorageBuffer, 5, buffers_[5]);
        gl_.UseProgram(transfer_);
        gl_.DispatchCompute((pixels + 63u) / 64u, 1u, 1u);
        gl_.MemoryBarrier(ShaderStorageBarrier | BufferUpdateBarrier);
        gl_.check("GPU VRAM transfer");
    }

    void read(const std::vector<Range> &ranges, uint8_t *destination) override {
        gl_.MemoryBarrier(ShaderStorageBarrier | BufferUpdateBarrier);
        gl_.BindBuffer(ShaderStorageBuffer, buffers_[0]);
        for (const auto &range : ranges)
            gl_.GetBufferSubData(ShaderStorageBuffer, range.offset, range.size, destination + range.offset);
        gl_.check("GPU VRAM readback");
    }

    void finish() override { gl_.Finish(); }

    bool hires_supported() const override { return hires_ != 0u; }

    void hires_reserve(size_t colorWords, size_t depthWords) override {
        if (!hires_)
            return;
        const size_t sizes[2] = {colorWords, depthWords};
        for (unsigned i = 0; i < 2u; ++i) {
            if (sizes[i] <= reserved_[i])
                continue;
            gl_.BindBuffer(ShaderStorageBuffer, extra_[i]);
            gl_.BufferData(ShaderStorageBuffer, Size(sizes[i] * 4u), nullptr, DynamicDraw);
            reserved_[i] = sizes[i];
        }
        gl_.check("GPU hi-res reserve");
    }

    void hires_raster(const std::vector<uint32_t> &params, unsigned groups) override {
        if (!hires_ || !groups)
            return;
        bindExtras(params);
        for (unsigned i = 0; i < 6u; ++i)
            gl_.BindBufferBase(ShaderStorageBuffer, i, buffers_[i]);
        gl_.UseProgram(hires_);
        gl_.DispatchCompute(groups, 1u, 1u);
        gl_.MemoryBarrier(ShaderStorageBarrier | BufferUpdateBarrier);
        gl_.check("GPU hi-res dispatch");
    }

    void hires_seed(const std::vector<uint32_t> &params, unsigned groupsX, unsigned groupsY) override {
        if (!seed_ || !groupsX || !groupsY)
            return;
        gl_.MemoryBarrier(ShaderStorageBarrier | BufferUpdateBarrier);
        bindExtras(params);
        gl_.BindBufferBase(ShaderStorageBuffer, 0, buffers_[0]);
        gl_.BindBufferBase(ShaderStorageBuffer, 5, buffers_[5]);
        gl_.UseProgram(seed_);
        gl_.DispatchCompute(groupsX, groupsY, 1u);
        gl_.MemoryBarrier(ShaderStorageBarrier | BufferUpdateBarrier);
        gl_.check("GPU hi-res seed");
    }

    void hires_read(size_t offsetWords, size_t words, uint32_t *destination) override {
        if (!hires_ || !words)
            return;
        gl_.MemoryBarrier(ShaderStorageBarrier | BufferUpdateBarrier);
        gl_.BindBuffer(ShaderStorageBuffer, extra_[0]);
        gl_.GetBufferSubData(ShaderStorageBuffer, Size(offsetWords * 4u), Size(words * 4u), destination);
        gl_.check("GPU hi-res readback");
    }

    void destroy() override {
        if (extra_[0])
            gl_.DeleteBuffers(int(extra_.size()), extra_.data());
        if (hires_)
            gl_.DeleteProgram(hires_);
        if (seed_)
            gl_.DeleteProgram(seed_);
        extra_.fill(0u);
        hires_ = seed_ = 0u;
        reserved_[0] = reserved_[1] = 0u;
        if (buffers_[0])
            gl_.DeleteBuffers(int(buffers_.size()), buffers_.data());
        if (job_)
            gl_.DeleteBuffers(1, &job_);
        if (raster_)
            gl_.DeleteProgram(raster_);
        if (transfer_)
            gl_.DeleteProgram(transfer_);
        buffers_.fill(0u);
        job_ = raster_ = transfer_ = 0u;
    }

  private:
    void bindExtras(const std::vector<uint32_t> &params) {
        gl_.BindBuffer(ShaderStorageBuffer, extra_[2]);
        gl_.BufferData(ShaderStorageBuffer, Size(params.size() * 4u), params.data(), DynamicDraw);
        for (unsigned i = 0; i < 3u; ++i)
            gl_.BindBufferBase(ShaderStorageBuffer, 6u + i, extra_[i]);
    }
    Api &gl_;
    std::array<UInt, 3> extra_{};
    UInt hires_ = 0u, seed_ = 0u;
    size_t reserved_[2] = {0u, 0u};
    std::string description_;
    std::array<UInt, 6> buffers_{};
    UInt raster_ = 0u, transfer_ = 0u, job_ = 0u;
};
}

std::unique_ptr<Compute> make_opengl_compute(Api &gl, const std::string &description) {
    return std::make_unique<OpenGlCompute>(gl, description);
}
}
