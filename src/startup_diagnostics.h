// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#include <iosfwd>
struct EeKernelSnapshot;
namespace openkai2 {
struct StartupActivity {
    uint64_t dma=0, gif=0, gs=0, vif=0;
};
class StartupDiagnostics {
    unsigned samples_=0;
public:
    bool due(uint64_t elapsed_ms) const;
    bool sample(uint64_t elapsed_ms,const EeKernelSnapshot&,StartupActivity,std::ostream&);
    void report(const char* reason,uint64_t elapsed_ms,const EeKernelSnapshot&,StartupActivity,std::ostream&) const;
};
}
