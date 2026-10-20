// deterministic CP0 observation-replay contract, NEVER host CP0/MMIO.
// Inputs: opcode(1=EI,2=MTC0 Status,3=ERET), status_before, write_value,
//         event_mode(0=none,1=external clear EIE,2=external set EIE), EPC, ErrorEPC.
// Output: expected Status after the instruction+DECLARED event, expected next PC.
// This executable does NOT capture or authenticate a PS2/PCSX2 hardware trace.
#include <cstdint>
#include <cstdio>
#include <inttypes.h>
using U32=std::uint32_t;
static constexpr U32 EIE=UINT32_C(0x10000),EDI=UINT32_C(0x20000);
static bool ei_allowed(U32 status) {
 return (status&EDI)||(status&UINT32_C(6))||!(status&UINT32_C(0x18));
}
static bool evaluate(U32 kind,U32 before,U32 value,U32 external,U32 epc,U32 error_epc,
                     U32& after,U32& next_pc) {
 if(kind<1||kind>3||external>2)return false;
 if(kind==1){
  after=ei_allowed(before)?(before|EIE):before;
  next_pc=UINT32_C(0x11339C);
 } else if(kind==2){
  after=value;next_pc=UINT32_C(0x1133D4);
 } else {
  if(before&UINT32_C(4)) {after=before&~UINT32_C(4);next_pc=error_epc;}
  else {after=before&~UINT32_C(2);next_pc=epc;}
 }
 if(external==1) after &= ~EIE;
 else if(external==2) after |= EIE;
 return true;
}
int main() {
 U32 kind,before,value,event,epc,error_epc,after,next_pc;
 while(std::scanf("%" SCNx32 " %" SCNx32 " %" SCNx32 " %" SCNx32
                  " %" SCNx32 " %" SCNx32,&kind,&before,&value,&event,
                  &epc,&error_epc)==6) {
  if(!evaluate(kind,before,value,event,epc,error_epc,after,next_pc)){
   std::fputs("Invalid observation input\n",stderr);return 3;
  }
  std::printf("%08" PRIX32 " %08" PRIX32 "\n",after,next_pc);
 }
 return std::ferror(stdin)?2:0;
}
