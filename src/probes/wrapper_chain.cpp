// Bounded, standalone integration of TWO original retail wrapper paths with
// the in-memory CP0 / MMIO / ERET model. Neither runtime nor host hardware
// is accessed. Do not treat this as verified PS2 hardware behavior.
// Compile the actual previous native implementations into ONE probe, rather
// than reimplementing or assuming wrapper effects in the downstream model.
#define B2_WRAPPER_CHAIN_LIBRARY_ONLY
#include "tail_wrappers.cpp"
#include "mmio_eret.cpp"
#undef B2_WRAPPER_CHAIN_LIBRARY_ONLY

int main() {
 U32 entry, initial, e1, e2, epc, old_error_epc;
 U64 v0,v1,a2,original_a0,original_a1,ra,sp;
 while(std::scanf("%" SCNx32 " %" SCNx32 " %" SCNx32 " %" SCNx32
                  " %" SCNx64 " %" SCNx64 " %" SCNx64 " %" SCNx64
                  " %" SCNx64 " %" SCNx64 " %" SCNx64 " %" SCNx32 " %" SCNx32,
       &entry,&initial,&e1,&e2,&v0,&v1,&a2,&original_a0,&original_a1,
       &ra,&sp,&epc,&old_error_epc)==13) {
  if(entry!=UINT32_C(0x113400) && entry!=UINT32_C(0x113410)) {
   std::fprintf(stderr,"Invalid wrapper entry %08" PRIX32 "\n",entry);
   return 3;
  }
  State wrapper{entry,original_a0,original_a1,ra,v0};
  wrapper=execute(wrapper,entry);
  if(wrapper.pc!=UINT32_C(0x113380) || wrapper.ra!=ra) {
   std::fputs("Wrapper state violates tail-jump contract\n",stderr);
   return 4;
  }
  S s{};
  s.status=initial;s.post_ei1=e1;s.post_ei2=e2;s.epc=epc;
  s.error_epc=old_error_epc;
  s.v0=wrapper.v0;s.v1=v1;s.a2=a2;s.a0=wrapper.a0;s.a1=wrapper.a1;
  s.ra=wrapper.ra;s.sp=sp;
  s=run(s); // Existing MmioEret model operates only on local test state.
  std::printf("%08" PRIX32 " %08" PRIX32 " %016" PRIX64
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
