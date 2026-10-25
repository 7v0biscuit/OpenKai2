// original guest Timer2 handler continuation AFTER its first helper
// returns. The two retail paths finish by tail-jumping through the other
// wrapper. Both calls are virtual CP0/MMIO; no actual guest scheduler or IRQ.
#define B2_HANDLER_LIBRARY_ONLY
#include "handler_list_probe.cpp"
#undef B2_HANDLER_LIBRARY_ONLY
#define B2_WRAPPER_CHAIN_LIBRARY_ONLY
#include "tail_wrappers.cpp"
#include "mmio_eret.cpp"
#undef B2_WRAPPER_CHAIN_LIBRARY_ONLY

namespace {
// Real retail instruction windows are 0x11388C..0x1138AC and
// 0x1138C0..0x1138E4; this is a direct native translation of those 19
// original instructions with a mandatory J delay slot, NOT a new MIPS VM.
void translated_post_call(CPU& c) {
 const uint32_t sp=static_cast<uint32_t>(c.r[29]);
 if(c.next_pc==0x11388cU){
  // LUI a0; LD ra; ORI a0; LD s1; AND a0,s2; LD s0; LD s2;
  // J 0x113400; ADDIU sp,sp,64 (delay slot)
  c.r[4]=sext32(0xffff0000U);
  c.r[31]=c.memory.read(sp+48U,8);
  c.r[4]|=0xf7ffULL;
  c.r[17]=c.memory.read(sp+16U,8);
  c.r[4]&=c.r[18];
  c.r[16]=c.memory.read(sp,8);
  c.r[18]=c.memory.read(sp+32U,8);
  c.next_pc=0x113400U;
  c.r[29]=sext32(sp+64U);
 } else if(c.next_pc==0x1138c0U) {
  // SLL a0,s1,2; LD ra; DSRLV a0,s0,a0; LD s2; LD s1;
  // DSLL32 a0,a0,0; DSRA32 a0,a0,0; LD s0;
  // J 0x113410; ADDIU sp,sp,64 (delay slot)
  c.r[4]=sext32(static_cast<uint32_t>(c.r[17])<<2U);
  c.r[31]=c.memory.read(sp+48U,8);
  c.r[4]=c.r[16]>>(c.r[4]&63ULL);
  c.r[18]=c.memory.read(sp+32U,8);
  c.r[17]=c.memory.read(sp+16U,8);
  c.r[4]=sext32(c.r[4]);
  c.r[16]=c.memory.read(sp,8);
  c.next_pc=0x113410U;
  c.r[29]=sext32(sp+64U);
 } else throw std::runtime_error("unexpected first helper continuation PC");
}
S virtual_helper(uint64_t entry, CPU& c,uint32_t init,uint32_t e1,uint32_t e2,uint32_t epc,uint32_t old) {
 State w{c.next_pc,c.r[4],c.r[5],c.r[31],c.r[2]};
 w=execute(w,static_cast<uint32_t>(entry));
 if(w.pc!=0x113380U || w.ra!=c.r[31]) throw std::runtime_error("invalid tail wrapper");
 S s{};s.status=init;s.post_ei1=e1;s.post_ei2=e2;s.epc=epc;s.error_epc=old;
 s.v0=w.v0;s.v1=c.r[3];s.a2=c.r[6];s.a0=w.a0;s.a1=w.a1;
 s.ra=w.ra;s.sp=c.r[29];
 return run(s);
}
}
// Included by IrqHandler to reuse the EXACT native translations in one process.
#ifndef B2_IRQ_HANDLER_LIBRARY_ONLY
int main(int argc,char**argv) {
 try {
  // Name + 14 original handler numbers + 5 CP0 observations for each of 2 helpers.
  if(argc!=26){std::cerr<<"expected case name, 14 handler args, two sets of five CP0 args\n";return 2;}
  CPU cpu;
  cpu.r[29]=number(argv[2]);cpu.r[31]=number(argv[3]);cpu.r[16]=number(argv[4]);
  cpu.r[17]=number(argv[5]);cpu.r[18]=number(argv[6]);cpu.r[4]=number(argv[7]);
  cpu.r[2]=0xdeadbeef;cpu.r[3]=0xdeaddead;
  cpu.r[6]=0x7777777777777777ULL;cpu.r[7]=0x8888888888888888ULL;
  cpu.memory.write(kGlobal+28U,number(argv[8]),4);
  cpu.memory.write(kGlobal+24U,number(argv[9]),4);
  cpu.memory.write(kMmio,number(argv[10]),4);
  cpu.memory.write(kMmioCounter,number(argv[15]),4);
  cpu.memory.write(kNode+32U,number(argv[11]),8);
  cpu.memory.write(kNode+16U,number(argv[12]),8);
  cpu.memory.write(kNode+24U,number(argv[13]),8);
  cpu.memory.write(kNode,number(argv[14]),4);
  translated_entry_prefix(cpu);translated_successor(cpu);
  translated_return_or_threshold(cpu);translated_external_call_boundary(cpu);
  if(!cpu.external_call_pending || (cpu.next_pc!=0x113400U && cpu.next_pc!=0x113410U))
   throw std::runtime_error("original handler did not reach external JAL");
  const uint64_t first_site=cpu.external_call_site,first_target=cpu.next_pc;
  S first=virtual_helper(cpu.next_pc,cpu,static_cast<uint32_t>(number(argv[16])),
   static_cast<uint32_t>(number(argv[17])),static_cast<uint32_t>(number(argv[18])),
   static_cast<uint32_t>(number(argv[19])),static_cast<uint32_t>(number(argv[20])));
  S second{};
  bool continuation=false;
  if(first.stop==1) {
   // A guest ERET must genuinely return to the MIPS JAL link address;
   // never invent an ordinary subroutine return on a polling/invalid stop.
   if(first.pc!=cpu.r[31])throw std::runtime_error("ERET did not resume JAL link address");
   cpu.r[2]=first.v0;cpu.r[3]=first.v1;cpu.r[6]=first.a2;
   cpu.r[4]=first.a0;cpu.r[5]=first.a1;
   cpu.next_pc=first.pc;
   translated_post_call(cpu);
   continuation=true;
   second=virtual_helper(cpu.next_pc,cpu,static_cast<uint32_t>(number(argv[21])),
    static_cast<uint32_t>(number(argv[22])),static_cast<uint32_t>(number(argv[23])),
    static_cast<uint32_t>(number(argv[24])),static_cast<uint32_t>(number(argv[25])));
  }
  const uint64_t out[]={
   first_site,first_target,first.pc,first.stop,first.status,first.store_addr,
   first.store_value,first.eret,continuation?1ULL:0ULL,
   continuation?cpu.next_pc:0ULL,continuation?cpu.r[31]:0ULL,
   continuation?cpu.r[29]:0ULL,continuation?cpu.r[16]:0ULL,
   continuation?cpu.r[17]:0ULL,continuation?cpu.r[18]:0ULL,
   continuation?cpu.r[4]:0ULL,
   second.pc,second.stop,second.status,second.store_addr,second.store_value,
   second.error_epc,second.eret,second.trace
  };
  std::cout<<"HELPER_CONTINUATION_CHAIN";
  for(uint64_t val:out)std::cout<<' '<<hx(val);
  std::cout<<'\n';return 0;
 }catch(const std::exception&e){std::cerr<<"HELPER_CONTINUATION_ERROR "<<e.what()<<'\n';return 2;}
}

#endif // B2_IRQ_HANDLER_LIBRARY_ONLY
