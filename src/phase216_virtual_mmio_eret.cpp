#include <cstdint>
#include <cstdio>
#include <inttypes.h>
#include <atomic>
// Standalone C++ model. NEVER dereferences guest MMIO addresses, invokes CP0,
// changes interrupts, nor executes ERET on the host. EI output is fixture data.
using U64=std::uint64_t; using U32=std::uint32_t;
struct S {
 U32 status,post_ei1,post_ei2,epc,error_epc,pc=0,reads=0,eis=0,
      status_writes=0,syncs=0,stores=0,stop=0,eret=0,error_epc_writes=0;
 U64 v0,v1,a2,a0,a1,ra,sp,trace=0,store_addr=0,store_value=0;
};
static U64 sx(U32 x){return (x&UINT32_C(0x80000000)) ? (UINT64_C(0xFFFFFFFF00000000)|U64(x)) : U64(x);}
static void event(S& s, U64 c){s.trace=(s.trace<<4)|c;}
static void sync_virtual(S& s){std::atomic_thread_fence(std::memory_order_seq_cst);s.syncs++;event(s,3);}
static bool allowed_virtual_address(U64 a){
 return a==UINT64_C(0xFFFFFFFFB0001000)||a==UINT64_C(0xFFFFFFFFB0001010)||a==UINT64_C(0xFFFFFFFFB0001020);
}
static S run(S s){
 // Pinned instructions 0x113380-0x113390: MFC0/lui/and/beq/lui delay.
 s.v1=sx(s.status);s.reads++;event(s,1);
 s.v0=sx(UINT32_C(0x00010000));s.v1&=s.v0;
 s.a2=sx(UINT32_C(0x00010000));
 if(s.v1){
  for(unsigned k=0;k<2;k++){
   // Virtual EI: caller provides the observed CP0 snapshot (NOT real EI).
   s.status=k==0?s.post_ei1:s.post_ei2;s.eis++;event(s,2);
   sync_virtual(s);
   s.v0=sx(s.status);s.reads++;event(s,1);
   s.v0&=s.a2;
   if(!s.v0)break;
   if(k==1){s.pc=UINT32_C(0x113398);s.stop=2;return s;}
  }
 }
 // 0x1133BC-0x1133D4: MFC0, SYNC, ORI, XORI, OR, MTC0 Status, SYNC.
 s.v0=sx(s.status);s.reads++;event(s,1);
 sync_virtual(s);
 s.v0|=U64(6);s.v0^=U64(2);s.v0|=s.v1;
 s.status=U32(s.v0);s.status_writes++;event(s,4);
 sync_virtual(s);
 // 0x1133D8 SW a1,0(a0): only validated in-memory record.
 if(!allowed_virtual_address(s.a0)){
  s.pc=UINT32_C(0x1133D8);s.stop=3;return s;
 }
 s.store_addr=s.a0;s.store_value=U64(U32(s.a1));s.stores++;event(s,5);
 // 0x1133DC MTC0 ra,$30 (ErrorEPC), 0x1133E0 SYNC.
 s.error_epc=U32(s.ra);s.error_epc_writes++;event(s,6);
 sync_virtual(s);
 // 0x1133E4 ERET; no delay slot. 0x1133E8 JR ra unreachable.
 s.eret++;event(s,7);
 if(s.status & U32(4)){s.pc=s.error_epc;s.status&=~U32(4);}
 else{s.pc=s.epc;s.status&=~U32(2);}
 s.stop=1;
 return s;
}
#ifndef B2_PHASE217_LIBRARY_ONLY
int main(){
 U32 init,e1,e2,epc,old_error_epc;U64 v0,v1,a2,a0,a1,ra,sp;
 while(std::scanf("%" SCNx32 " %" SCNx32 " %" SCNx32 " %" SCNx64 " %" SCNx64 " %" SCNx64 " %" SCNx64 " %" SCNx64 " %" SCNx64 " %" SCNx64 " %" SCNx32 " %" SCNx32,
         &init,&e1,&e2,&v0,&v1,&a2,&a0,&a1,&ra,&sp,&epc,&old_error_epc)==12){
  S s{};s.status=init;s.post_ei1=e1;s.post_ei2=e2;s.epc=epc;s.error_epc=old_error_epc;
  s.v0=v0;s.v1=v1;s.a2=a2;s.a0=a0;s.a1=a1;s.ra=ra;s.sp=sp;
  s=run(s);
  std::printf("%08" PRIX32 " %08" PRIX32 " %08" PRIX32 " %08" PRIX32
   " %016" PRIX64 " %016" PRIX64 " %016" PRIX64 " %016" PRIX64
   " %016" PRIX64 " %016" PRIX64 " %016" PRIX64
   " %08" PRIX32 " %08" PRIX32 " %08" PRIX32 " %08" PRIX32
   " %08" PRIX32 " %016" PRIX64 " %08" PRIX32
   " %016" PRIX64 " %016" PRIX64 " %08" PRIX32 " %08" PRIX32 "\n",
   s.pc,s.status,s.error_epc,s.epc,s.v0,s.v1,s.a2,s.a0,s.a1,s.ra,s.sp,
   s.reads,s.eis,s.status_writes,s.syncs,s.stores,s.trace,s.stop,s.store_addr,s.store_value,
   s.eret,s.error_epc_writes);
 }
 return std::ferror(stdin)?2:0;
}

#endif // B2_PHASE217_LIBRARY_ONLY: model linked into wrapper-to-helper probe
