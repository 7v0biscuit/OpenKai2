// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "runtime/gs/gs_backend.h"
#include <memory>
#include <string>
namespace openkai2 {
struct GpuRenderStats {
    uint64_t gpuDraws=0,cpuDraws=0,batches=0,tiles=0,uploads=0,readbacks=0,culled=0;
    uint64_t paletteLoads=0,paletteReuses=0;
};
class GpuRasterBackend final:public GSRasterBackend {
public:
    GpuRasterBackend();~GpuRasterBackend()override;
    void Initialize(uint8_t*,uint32_t)override;void Reset()override;
    void Submit(const GSPrimitiveBatch&)override;void LoadClut(const GSTex0Reg&,const GSTexClutReg&)override;
    void BeginTransfer(const GSTransferCommand&)override;void UploadImage(const uint8_t*,uint32_t)override;
    void Flush()override;void TextureFlush()override;void Sync(GSSyncReason)override;
    PresentationFrame Present(const GSPresentationRequest&)override;
    bool ClearFramebuffer(const GSContext&,uint32_t)override;
    uint32_t ConsumeLocalToHostBytes(uint8_t*,uint32_t)override;
    uint32_t ReadVram(uint32_t,uint32_t,uint32_t,uint32_t,uint32_t)const override;
    void WriteVram(uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t)override;
    void SnapshotVram(std::vector<uint8_t>&)const override;GSTransferSnapshot GetTransferSnapshot()const override;
    GpuRenderStats stats()const;
private:
    struct Impl;std::unique_ptr<Impl> m;
};
GpuRenderStats gpu_render_statistics();
std::unique_ptr<GSRasterBackend> make_raster_backend();
void initialize_window_gpu();void shutdown_window_gpu();
void set_renderer_preference(const std::string& preference);
const std::string& renderer_preference();
}
