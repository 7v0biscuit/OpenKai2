// standalone virtual Timer2 IRQ11 handoff *test adapter*.
// This is NOT actual PS2 INTC policy, a real scheduler, an original game
// IRQ handler, or proof of hardware timing. The queue/ack policy is an
// explicit harness contract; CP0 EI behavior remains supplied test snapshots.
// Only in-memory guest state is ever modified.
#define B2_WRAPPER_CHAIN_LIBRARY_ONLY
#include "tail_wrappers.cpp"
#include "mmio_eret.cpp"
#undef B2_WRAPPER_CHAIN_LIBRARY_ONLY

int main(){
 U32 entry,flags,initial,e1,e2,epc,old_eepc;
 U64 v0,v1,a2,in_a0,in_a1,ra,sp;
 // flags: bit0 queued IRQ11, bit1 route unmasked, bit2 callback registered,
 // bit3 explicitly acknowledged. Deliberately independent from CP0 EIE.
 while(std::scanf("%" SCNx32 " %" SCNx32 " %" SCNx32 " %" SCNx32 " %" SCNx32
   " %" SCNx64 " %" SCNx64 " %" SCNx64 " %" SCNx64 " %" SCNx64
   " %" SCNx64 " %" SCNx64 " %" SCNx32 " %" SCNx32,
   &entry,&flags,&initial,&e1,&e2,&v0,&v1,&a2,&in_a0,&in_a1,&ra,&sp,
   &epc,&old_eepc)==14){
  if((entry!=UINT32_C(0x113400)&&entry!=UINT32_C(0x113410))||flags>15){
   std::fputs("Invalid entry or IRQ11 flags\n",stderr);return 3;
  }
  const U32 queued=flags&1, unmasked=(flags>>1)&1, registered=(flags>>2)&1;
  const U32 ack=(flags>>3)&1;
  const U32 dispatch=(queued&&unmasked&&registered)?1:0;
  const U32 pending=(queued && !(dispatch&&ack))?1:0;
  // Uniform 31-column output: 4 boundary metadata + 27 existing chain
  // fields. Zero chain fields indicate no guest code was invoked.
  std::printf("%08" PRIX32 " %08" PRIX32 " %08" PRIX32 " %08" PRIX32,
              entry,flags,dispatch,pending);
  if(!dispatch){
   for(int i=0;i<27;++i)std::fputs(" 00000000",stdout);
   std::fputc('\n',stdout);continue;
  }
  State wrapper{entry,in_a0,in_a1,ra,v0};
  wrapper=execute(wrapper,entry); // Actual previously translated wrapper code.
  if(wrapper.pc!=UINT32_C(0x113380)||wrapper.ra!=ra){
   std::fputs("Tail-wrapper contract violated\n",stderr);return 4;
  }
  S s{};
  s.status=initial;s.post_ei1=e1;s.post_ei2=e2;s.epc=epc;
  s.error_epc=old_eepc;s.v0=wrapper.v0;s.v1=v1;s.a2=a2;
  s.a0=wrapper.a0;s.a1=wrapper.a1;s.ra=wrapper.ra;s.sp=sp;
  s=run(s); // Actual previously translated bounded virtual helper model.
  std::printf(" %08" PRIX32 " %08" PRIX32 " %016" PRIX64
    " %016" PRIX64 " %016" PRIX64 " "
    "%08" PRIX32 " %08" PRIX32 " %08" PRIX32 " %08" PRIX32
    " %016" PRIX64 " %016" PRIX64 " %016" PRIX64 " %016" PRIX64
    " %016" PRIX64 " %016" PRIX64 " %016" PRIX64
    " %08" PRIX32 " %08" PRIX32 " %08" PRIX32 " %08" PRIX32
    " %08" PRIX32 " %016" PRIX64 " %08" PRIX32
    " %016" PRIX64 " %016" PRIX64 " %08" PRIX32 " %08" PRIX32 "\n",
    entry,U32(wrapper.pc),wrapper.a0,wrapper.a1,wrapper.ra,
    s.pc,s.status,s.error_epc,s.epc,s.v0,s.v1,s.a2,s.a0,s.a1,s.ra,s.sp,
    s.reads,s.eis,s.status_writes,s.syncs,s.stores,s.trace,s.stop,
    s.store_addr,s.store_value,s.eret,s.error_epc_writes);
 }
 return std::ferror(stdin)?2:0;
}
