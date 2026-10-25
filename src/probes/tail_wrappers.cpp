#include <cstdint>
#include <cstdio>
#include <inttypes.h>
// Isolated stand-in model for two verified tail-jump wrappers.
// Not linked to the guest runtime; does not execute the destination routine.
struct State { uint64_t pc,a0,a1,ra,v0; };
static uint64_t lui_signextend(uint16_t hi) {
    return uint64_t(int64_t(int32_t(uint32_t(hi) << 16)));
}
static State execute(State s, uint32_t entry) {
    const uint16_t io = entry == 0x00113400u ? 0x1010u : 0x1020u;
    if (entry != 0x00113400u && entry != 0x00113410u) return s;
    // 0: daddu a1,a0,zero; 1: lui a0,0xB000
    s.a1=s.a0; s.a0=lui_signextend(0xB000);
    // 2: j 0x00113380; 3: ori a0,a0,IO (mandatory delay slot)
    s.pc=0x00113380u; s.a0 |= uint64_t(io);
    return s;
}
#ifndef B2_WRAPPER_CHAIN_LIBRARY_ONLY
int main(){
    unsigned int entry;
    uint64_t a0,a1,ra,v0;
    while (std::scanf("%x %" SCNx64 " %" SCNx64 " %" SCNx64 " %" SCNx64,
                      &entry,&a0,&a1,&ra,&v0)==5) {
        State s={entry,a0,a1,ra,v0};
        auto e=execute(s,entry);
        std::printf("%08X %016" PRIX64 " %016" PRIX64 " %016" PRIX64 " %016" PRIX64 "\n",
                    uint32_t(e.pc),e.a0,e.a1,e.ra,e.v0);
    }
    if(std::ferror(stdin)) return 2;
    return 0;
}

#endif // B2_WRAPPER_CHAIN_LIBRARY_ONLY: model linked into wrapper-to-helper probe
