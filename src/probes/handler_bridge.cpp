// composes BOUNDED translated original Timer2 handler call sites
// with the earlier translated tail wrappers and a virtual CP0/MMIO helper.
// No live guest IRQ, no actual PS2 scheduler, no host MMIO, no function registry.
#define B2_HANDLER_LIBRARY_ONLY
#include "handler_list_probe.cpp"
#undef B2_HANDLER_LIBRARY_ONLY
#define B2_WRAPPER_CHAIN_LIBRARY_ONLY
#include "tail_wrappers.cpp"
#include "mmio_eret.cpp"
#undef B2_WRAPPER_CHAIN_LIBRARY_ONLY

int main(int argc,char**argv) {
 try {
  // Existing handler's 14 case inputs + five virtual CPU observation inputs.
  if(argc!=21) {std::cerr<<"expected case, 14 handler args, 5 virtual CP0 args\n";return 2;}
  CPU cpu;
  cpu.r[29]=number(argv[2]);cpu.r[31]=number(argv[3]);cpu.r[16]=number(argv[4]);
  cpu.r[17]=number(argv[5]);cpu.r[18]=number(argv[6]);cpu.r[4]=number(argv[7]);
  cpu.r[2]=0xdeadbeef;cpu.r[3]=0xdeaddead;cpu.r[6]=0x7777777777777777ULL;
  cpu.r[7]=0x8888888888888888ULL;
  cpu.memory.write(kGlobal+28U,number(argv[8]),4);
  cpu.memory.write(kGlobal+24U,number(argv[9]),4);
  cpu.memory.write(kMmio,number(argv[10]),4);
  cpu.memory.write(kMmioCounter,number(argv[15]),4);
  cpu.memory.write(kNode+32U,number(argv[11]),8);
  cpu.memory.write(kNode+16U,number(argv[12]),8);
  cpu.memory.write(kNode+24U,number(argv[13]),8);
  cpu.memory.write(kNode,number(argv[14]),4);
  translated_entry_prefix(cpu);
  translated_successor(cpu);
  translated_return_or_threshold(cpu);
  translated_external_call_boundary(cpu);
  if(!cpu.external_call_pending || cpu.external_call_executed ||
    (cpu.next_pc!=0x113400U && cpu.next_pc!=0x113410U)) {
    std::cerr<<"handler case did not reach a safe external call boundary\n";return 3;
  }
  const uint64_t handler_site=cpu.external_call_site;
  const uint64_t handler_target=cpu.external_call_target;
  State w{cpu.next_pc,cpu.r[4],cpu.r[5],cpu.r[31],cpu.r[2]};
  w=execute(w,static_cast<uint32_t>(handler_target));
  if(w.pc!=0x113380U || w.ra!=cpu.r[31]) {
   std::cerr<<"unexpected tail wrapper result\n";return 4;
  }
  S s{};
  s.status=static_cast<U32>(number(argv[16]));
  s.post_ei1=static_cast<U32>(number(argv[17]));
  s.post_ei2=static_cast<U32>(number(argv[18]));
  s.epc=static_cast<U32>(number(argv[19]));
  s.error_epc=static_cast<U32>(number(argv[20]));
  s.v0=w.v0;s.v1=cpu.r[3];s.a2=cpu.r[6];
  s.a0=w.a0;s.a1=w.a1;s.ra=w.ra;s.sp=cpu.r[29];
  s=run(s);  // In-memory ONLY. ERET is a modeled PC update.
  const uint64_t values[]={
   handler_site,handler_target,cpu.r[31],cpu.r[4],cpu.r[5],cpu.r[2],cpu.r[3],cpu.r[6],cpu.r[29],
   w.pc,w.a0,w.a1,w.ra,
   s.pc,s.status,s.error_epc,s.epc,s.v0,s.v1,s.a2,s.a0,s.a1,s.ra,s.sp,
   s.reads,s.eis,s.status_writes,s.syncs,s.stores,s.trace,s.stop,s.store_addr,
   s.store_value,s.eret,s.error_epc_writes
  };
  std::cout<<"HANDLER_BRIDGE_CHAIN";
  for(uint64_t v:values)std::cout<<' '<<hx(v);
  std::cout<<'\n';return 0;
 } catch(const std::exception&exc){std::cerr<<"HANDLER_BRIDGE_ERROR "<<exc.what()<<'\n';return 2;}
}
