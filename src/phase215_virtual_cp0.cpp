#include <cstdint>
#include <cstdio>
#include <inttypes.h>
#include <atomic>
// Isolated test model. EI uses fixture-provided post-EI CP0 snapshots.
// MTC0 affects only this local State. No game/host hardware is touched.
using U64=std::uint64_t;using U32=std::uint32_t;
struct State {
 U32 cp0, next1,next2, pc=0, reads=0, eis=0, mtc0s=0, syncs=0, hw_writes=0, stop=0;
 U64 v0,v1,a2,a0,a1,ra,sp,trace=0;
};
static U64 sx(U32 x){return x&UINT32_C(0x80000000)?UINT64_C(0xffffffff00000000)|U64(x):U64(x);}
static void event(State &s, unsigned int code){s.trace=(s.trace<<4)|U64(code);}
static void sync_(State &s){std::atomic_thread_fence(std::memory_order_seq_cst);s.syncs++;event(s,3);}
static State run(State s){
 // 0x113380 mfc0 v1,Status; 0x113384 lui v0,1; 0x113388 and;
 s.v1=sx(s.cp0);s.reads++;event(s,1);
 s.v0=sx(UINT32_C(0x10000));s.v1&=s.v0;
 // 0x11338c beq ...; 0x113390 delay-slot lui a2,1.
 s.a2=sx(UINT32_C(0x10000));
 if(s.v1!=0){
  // EI at 0x113398; poll status; bne 0x1133b4 with NOP delay slot.
  // Bounded by two fixture-driven transition samples to avoid endless loops.
  for(unsigned int k=0;k<2;k++){
   s.eis++;event(s,2);
   s.cp0=(k==0?s.next1:s.next2); // fixture transition, NOT ISA claim
   sync_(s); // 0x11339c
   s.v0=sx(s.cp0);s.reads++;event(s,1); // 0x1133a0
   s.v0&=s.a2; // 0x1133a4
   if(s.v0==0)break; // 0x1133b4 bne; 0x1133b8 NOP
   if(k==1){s.pc=UINT32_C(0x00113398);s.stop=2;return s;}
  }
 }
 // 0x1133bc mfc0 Status, 0x1133c0 SYNC;
 s.v0=sx(s.cp0);s.reads++;event(s,1);
 sync_(s);
 // 0x1133c4 ori v0,6; 0x1133c8 xori v0,2; 0x1133cc or v0,v1;
 s.v0|=U64(6);s.v0^=U64(2);s.v0|=s.v1;
 // 0x1133d0 MTC0 Status (virtual only); 0x1133d4 SYNC.
 s.cp0=U32(s.v0);s.mtc0s++;event(s,4);
 sync_(s);
 // Stop before 0x1133d8 SW a1,0(a0): no memory / MMIO writes.
 s.pc=UINT32_C(0x001133d8);s.stop=1;return s;
}
int main(){
 unsigned int cp0,next1,next2;U64 v0,v1,a2,a0,a1,ra,sp;
 while(std::scanf("%x %x %x %" SCNx64 " %" SCNx64 " %" SCNx64 " %" SCNx64 " %" SCNx64 " %" SCNx64 " %" SCNx64,
 &cp0,&next1,&next2,&v0,&v1,&a2,&a0,&a1,&ra,&sp)==10){
  State s{}; s.cp0=U32(cp0);s.next1=U32(next1);s.next2=U32(next2);
  s.v0=v0;s.v1=v1;s.a2=a2;s.a0=a0;s.a1=a1;s.ra=ra;s.sp=sp;
  s=run(s);
  std::printf("%08" PRIX32 " %08" PRIX32 " %016" PRIX64 " %016" PRIX64 " %016" PRIX64
   " %016" PRIX64 " %016" PRIX64 " %016" PRIX64 " %016" PRIX64
   " %08" PRIX32 " %08" PRIX32 " %08" PRIX32 " %08" PRIX32 " %08" PRIX32 " %016" PRIX64 " %08" PRIX32 "\n",
   s.pc,s.cp0,s.v0,s.v1,s.a2,s.a0,s.a1,s.ra,s.sp,s.reads,s.eis,s.mtc0s,s.syncs,s.hw_writes,s.trace,s.stop);
 }
 return std::ferror(stdin)?2:0;
}
