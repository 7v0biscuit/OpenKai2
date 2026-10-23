// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace openkai2 {
struct Spu2PcmBlock {
    uint32_t core=0;
    bool stop=false;
    std::array<int16_t,512> samples{};
};

// SPU2 AutoDMA supplies groups of 256 left samples followed by 256 right
// samples. Ordinary voice/sample DMA is not PCM and must never enter this path.
class Spu2PcmCapture {
public:
    void reset(){used_={};blocks_.clear();}
    void stop(unsigned core){
        if(core>=2u)return;
        used_[core]=0;Spu2PcmBlock block;block.core=core;block.stop=true;blocks_.push_back(block);
    }
    void transfer(unsigned core,std::span<const uint8_t> bytes){
        if(core>=2u)return;
        for(auto byte:bytes){
            auto& n=used_[core];partial_[core][n++]=byte;
            if(n!=1024u)continue;
            n=0;
            if(blocks_.size()>=2048u)continue;
            Spu2PcmBlock block;block.core=core;
            auto sample=[&](unsigned offset){return int16_t(uint16_t(partial_[core][offset])
                |(uint16_t(partial_[core][offset+1u])<<8u));};
            for(unsigned i=0;i<256u;++i){block.samples[i*2u]=sample(i*2u);block.samples[i*2u+1u]=sample(512u+i*2u);}
            blocks_.push_back(block);
        }
    }
    bool available()const{return !blocks_.empty();}
    std::vector<Spu2PcmBlock> take(){auto result=std::move(blocks_);blocks_.clear();return result;}
private:
    std::array<std::array<uint8_t,1024>,2> partial_{};
    std::array<unsigned,2> used_{};
    std::vector<Spu2PcmBlock> blocks_;
};
}
