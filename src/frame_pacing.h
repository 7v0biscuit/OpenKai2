// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
namespace openkai2 {
struct FramePacingSummary {
    uint64_t frames=0,intervals=0,elapsedUs=0,workUs=0,maxUs=0,late=0;
    uint64_t p50Us=0,p95Us=0,p99Us=0;
    double fps() const {return elapsedUs ? double(intervals)*1000000.0/double(elapsedUs) : 0.0;}
};
// Measure completed game presentations. Window redraws never enter these
// samples. Percentile values are upper bounds in 1 ms buckets.
class FramePacing {
public:
    void restart(uint64_t nowUs) {
        *this=FramePacing{};previous_=nowUs;havePrevious_=true;
    }
    void record(uint64_t nowUs,uint64_t workUs) {
        ++summary_.frames;summary_.workUs+=workUs;
        if(havePrevious_ && nowUs>=previous_){
            const auto interval=nowUs-previous_;++summary_.intervals;summary_.elapsedUs+=interval;
            summary_.maxUs=std::max(summary_.maxUs,interval);summary_.late+=interval>25000u;
            ++buckets_[std::min<uint64_t>((interval+999u)/1000u,buckets_.size()-1u)];
        }
        previous_=nowUs;havePrevious_=true;
    }
    FramePacingSummary summary() const {
        auto result=summary_;
        const auto percentile=[&](unsigned percent){
            const auto rank=(summary_.intervals*percent+99u)/100u;if(!rank)return uint64_t(0);
            uint64_t count=0;for(unsigned i=0;i<buckets_.size();++i){count+=buckets_[i];
                if(count>=rank)return i+1u==buckets_.size() ? summary_.maxUs : uint64_t(i)*1000u;}
            return summary_.maxUs;
        };
        result.p50Us=percentile(50u);result.p95Us=percentile(95u);result.p99Us=percentile(99u);return result;
    }
private:
    FramePacingSummary summary_{};std::array<uint64_t,512> buckets_{};
    uint64_t previous_=0;bool havePrevious_=false;
};
}
