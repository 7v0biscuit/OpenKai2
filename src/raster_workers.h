// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <functional>
#include <cstdint>
#include <memory>

namespace openkai2 {
// All tasks complete before run returns. Index zero runs on the calling thread.
class RasterWorkers {
public:
    explicit RasterWorkers(unsigned count);
    ~RasterWorkers();
    RasterWorkers(const RasterWorkers&)=delete;
    RasterWorkers& operator=(const RasterWorkers&)=delete;
    unsigned count() const;
    void run(unsigned count,const std::function<void(unsigned)>& task);
private:
    struct State;
    std::unique_ptr<State> state_;
};
RasterWorkers& raster_workers();
// A bounded override for deterministic serial/parallel renderer comparisons.
unsigned set_raster_worker_limit(unsigned);
unsigned raster_worker_limit();
uint64_t raster_parallel_batches();
void verify_raster_workers();
}
