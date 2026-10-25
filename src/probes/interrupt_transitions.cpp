// EE interrupt-enable permission and external-event checkpoint model.
// All state and MMIO are VIRTUAL. A checkpoint clear is an injected observation,
// not a claim that EI or PS2 hardware automatically clears EIE.
#define B2_WRAPPER_CHAIN_LIBRARY_ONLY
#include "tail_wrappers.cpp"
#include "mmio_eret.cpp"
#undef B2_WRAPPER_CHAIN_LIBRARY_ONLY

static constexpr U32 EIE=UINT32_C(0x00010000), EDI=UINT32_C(0x00020000);
static constexpr U32 EXL=UINT32_C(2), ERL=UINT32_C(4), KSU=UINT32_C(0x18);
static bool ei_permitted(U32 status) {
 // EDI permits EI in user/supervisor; otherwise kernel only. Exception levels
 // imply kernel privileges. This is a mode/permission contract, not timing.
 return (status & EDI)!=0 || (status & (EXL|ERL))!=0 || (status & KSU)==0;
}
static U32 modeled_ei(U32 status) {
 return ei_permitted(status)?(status|EIE):status;
}
static U32 checkpoint(U32 status,U32 mask,unsigned idx) {
 // Deliberate injected state update, separate from the EI instruction.
 return (mask & (U32(1)<<(idx-1)))?(status&~EIE):status;
}
int main(){
 U32 entry,initial,mode,epc,old_error_epc;
 U64 v0,v1,a2,original_a0,original_a1,ra,sp;
 while(std::scanf("%" SCNx32 " %" SCNx32 " %" SCNx32
                  " %" SCNx64 " %" SCNx64 " %" SCNx64 " %" SCNx64
                  " %" SCNx64 " %" SCNx64 " %" SCNx64
                  " %" SCNx32 " %" SCNx32,
          &entry,&initial,&mode,&v0,&v1,&a2,&original_a0,&original_a1,
          &ra,&sp,&epc,&old_error_epc)==12) {
  if((entry!=UINT32_C(0x113400)&&entry!=UINT32_C(0x113410)) || mode>3){
   std::fputs("Invalid wrapper or checkpoint mode\n",stderr);return 3;
  }
  U32 after1=checkpoint(modeled_ei(initial),mode,1);
  U32 after2=checkpoint(modeled_ei(after1),mode,2);
  State wrapper{entry,original_a0,original_a1,ra,v0};
  wrapper=execute(wrapper,entry);
  if(wrapper.pc!=UINT32_C(0x113380)||wrapper.ra!=ra){
   std::fputs("Broken retail tail jump\n",stderr);return 4;
  }
  S s{};
  s.status=initial;s.post_ei1=after1;s.post_ei2=after2;s.epc=epc;
  s.error_epc=old_error_epc;s.v0=wrapper.v0;s.v1=v1;s.a2=a2;
  s.a0=wrapper.a0;s.a1=wrapper.a1;s.ra=wrapper.ra;s.sp=sp;
  s=run(s);
  std::printf("%08" PRIX32 " %08" PRIX32 " "
    "%08" PRIX32 " %08" PRIX32 " %016" PRIX64
    " %016" PRIX64 " %016" PRIX64
    " %08" PRIX32 " %08" PRIX32 " %08" PRIX32 " %08" PRIX32
    " %016" PRIX64 " %016" PRIX64 " %016" PRIX64 " %016" PRIX64
    " %016" PRIX64 " %016" PRIX64 " %016" PRIX64
    " %08" PRIX32 " %08" PRIX32 " %08" PRIX32 " %08" PRIX32
    " %08" PRIX32 " %016" PRIX64 " %08" PRIX32
    " %016" PRIX64 " %016" PRIX64 " %08" PRIX32 " %08" PRIX32 "\n",
    after1,after2,entry,U32(wrapper.pc),wrapper.a0,wrapper.a1,wrapper.ra,
    s.pc,s.status,s.error_epc,s.epc,s.v0,s.v1,s.a2,s.a0,s.a1,s.ra,s.sp,
    s.reads,s.eis,s.status_writes,s.syncs,s.stores,s.trace,s.stop,
    s.store_addr,s.store_value,s.eret,s.error_epc_writes);
 }
 return std::ferror(stdin)?2:0;
}
