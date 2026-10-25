#include <cstdint>
#include <cstdio>
#include <inttypes.h>
#include <atomic>
// Isolated mathematical model of a bounded *portion* of 0x00113380.
// Does NOT execute EI, DI, MTC0, ERET, hardware stores, or runtime calls.
struct State {
    std::uint32_t status;
    std::uint64_t v0,v1,a2,a0,a1,ra,sp;
    std::uint32_t pc,syncs,stop;
};
static std::uint64_t signextend32(std::uint32_t v) {
    if ((v & UINT32_C(0x80000000)) != 0)
        return UINT64_C(0xffffffff00000000) | std::uint64_t(v);
    return std::uint64_t(v);
}
static State execute(State s) {
    // 0x00113380: mfc0 v1,Status, reads a caller-supplied snapshot only
    // 0x00113384: lui v0,0x0001; 0x00113388: and v1,v1,v0
    s.v1=signextend32(s.status);
    s.v0=signextend32(UINT32_C(0x00010000));
    s.v1 &= s.v0;
    // 0x0011338C: beq v1,zero,+11; delay: 0x00113390 lui a2,1
    const bool branch=s.v1==0;
    s.a2=signextend32(UINT32_C(0x00010000));
    if (!branch) {
        // NOP at 0x00113394; STOP BEFORE privileged EI at 0x00113398
        s.pc=UINT32_C(0x00113398);s.syncs=0;s.stop=1;
        return s;
    }
    // 0x001133BC: mfc0 v0,Status, read caller snapshot
    s.v0=signextend32(s.status);
    // 0x001133C0: SYNC ordering fence, no guest hardware interaction
    std::atomic_thread_fence(std::memory_order_seq_cst);
    s.syncs=1;
    // ORI, XORI and OR execute with zero-extended 16-bit immediates.
    // 0x001133C4: ori v0,v0,6
    s.v0 |= UINT64_C(6);
    // 0x001133C8: xori v0,v0,2
    s.v0 ^= UINT64_C(2);
    // 0x001133CC: or v0,v0,v1
    s.v0 |= s.v1;
    // STOP BEFORE privileged MTC0 at 0x001133D0
    s.pc=UINT32_C(0x001133D0);s.stop=2;
    return s;
}
int main() {
    unsigned int status;
    std::uint64_t v0,v1,a2,a0,a1,ra,sp;
    while(std::scanf("%x %" SCNx64 " %" SCNx64 " %" SCNx64 " %" SCNx64
                     " %" SCNx64 " %" SCNx64 " %" SCNx64,
                     &status,&v0,&v1,&a2,&a0,&a1,&ra,&sp)==8) {
        State s{std::uint32_t(status),v0,v1,a2,a0,a1,ra,sp,0,0,0};
        s=execute(s);
        std::printf("%08" PRIX32 " %016" PRIX64 " %016" PRIX64 " %016" PRIX64
                    " %016" PRIX64 " %016" PRIX64 " %016" PRIX64 " %016" PRIX64
                    " %08" PRIX32 " %08" PRIX32 "\n",
                    s.pc,s.v0,s.v1,s.a2,s.a0,s.a1,s.ra,s.sp,s.syncs,s.stop);
    }
    return std::ferror(stdin)?2:0;
}
