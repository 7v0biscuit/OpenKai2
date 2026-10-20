// virtual IRQ11 admission -> the ORIGINAL handler bounded C++
// translation -> both actual compiled wrapper/helper segments in ONE process.
// Gate bits are a fixture contract, NOT a real PS2 INTC or scheduler.
// No guest IRQ is registered, and CP0/MMIO is always virtual.
#define B2_IRQ_HANDLER_LIBRARY_ONLY
#include "helper_continuation.cpp"
#undef B2_IRQ_HANDLER_LIBRARY_ONLY
#include <vector>

namespace {
// Format: NAME FLAGS then 14 original handler arguments then 5+5 CP0 inputs.
// The 24 output columns exactly match the HelperContinuation original-chain contract;
// metadata and execution counters additionally audit suppressed dispatch.
void one_case(const std::vector<std::string>& fields) {
 if(fields.size()!=26)throw std::runtime_error("expected NAME FLAGS 14 handler words 10 CP0 words");
 uint64_t values[25]{};
 for(std::size_t j=1;j<fields.size();++j)values[j-1]=number(fields[j].c_str());
 const unsigned flags=static_cast<unsigned>(values[0]);
 if(flags>15)throw std::runtime_error("invalid virtual IRQ admission flags");
 const uint64_t queued=(flags&1U)?1:0;
 const uint64_t dispatch=(queued && (flags&2U) && (flags&4U))?1:0;
 const uint64_t pending=(queued && !(dispatch && (flags&8U)))?1:0;
 uint64_t out[24]{};
 uint64_t guest_handler_entered=0,helper_calls=0;
 if(dispatch){
  // Deliberately do not even execute original handler setup unless admitted.
  guest_handler_entered=1;
  CPU cpu;
  const auto a=[&values](std::size_t j){return values[j];};
  // HelperContinuation argv[N] maps here to values[N-1] because FLAGS was inserted.
  cpu.r[29]=a(1);cpu.r[31]=a(2);cpu.r[16]=a(3);
  cpu.r[17]=a(4);cpu.r[18]=a(5);cpu.r[4]=a(6);
  cpu.r[2]=0xdeadbeef;cpu.r[3]=0xdeaddead;
  cpu.r[6]=0x7777777777777777ULL;cpu.r[7]=0x8888888888888888ULL;
  cpu.memory.write(kGlobal+28U,a(7),4);
  cpu.memory.write(kGlobal+24U,a(8),4);
  cpu.memory.write(kMmio,a(9),4);
  cpu.memory.write(kMmioCounter,a(14),4);
  cpu.memory.write(kNode+32U,a(10),8);
  cpu.memory.write(kNode+16U,a(11),8);
  cpu.memory.write(kNode+24U,a(12),8);
  cpu.memory.write(kNode,a(13),4);
  translated_entry_prefix(cpu);
  translated_successor(cpu);
  translated_return_or_threshold(cpu);
  translated_external_call_boundary(cpu);
  if(!cpu.external_call_pending || (cpu.next_pc!=0x113400U && cpu.next_pc!=0x113410U))
   throw std::runtime_error("original handler has no external call at admitted checkpoint");
  const uint64_t first_site=cpu.external_call_site,first_target=cpu.next_pc;
  ++helper_calls;
  S first=virtual_helper(cpu.next_pc,cpu,static_cast<uint32_t>(a(15)),
   static_cast<uint32_t>(a(16)),static_cast<uint32_t>(a(17)),
   static_cast<uint32_t>(a(18)),static_cast<uint32_t>(a(19)));
  S second{};
  bool continuation=false;
  if(first.stop==1){
   if(first.pc!=cpu.r[31])throw std::runtime_error("first virtual ERET returned to wrong handler PC");
   cpu.r[2]=first.v0;cpu.r[3]=first.v1;cpu.r[6]=first.a2;
   cpu.r[4]=first.a0;cpu.r[5]=first.a1;
   cpu.next_pc=first.pc;
   translated_post_call(cpu);
   continuation=true;
   ++helper_calls;
   second=virtual_helper(cpu.next_pc,cpu,static_cast<uint32_t>(a(20)),
    static_cast<uint32_t>(a(21)),static_cast<uint32_t>(a(22)),
    static_cast<uint32_t>(a(23)),static_cast<uint32_t>(a(24)));
  }
  const uint64_t result[]={
   first_site,first_target,first.pc,first.stop,first.status,first.store_addr,
   first.store_value,first.eret,continuation?1ULL:0ULL,
   continuation?cpu.next_pc:0ULL,continuation?cpu.r[31]:0ULL,
   continuation?cpu.r[29]:0ULL,continuation?cpu.r[16]:0ULL,
   continuation?cpu.r[17]:0ULL,continuation?cpu.r[18]:0ULL,
   continuation?cpu.r[4]:0ULL,
   second.pc,second.stop,second.status,second.store_addr,second.store_value,
   second.error_epc,second.eret,second.trace
  };
  static_assert(sizeof(result)/sizeof(result[0])==24,"HelperContinuation result contract drift");
  for(unsigned j=0;j<24;++j)out[j]=result[j];
 }
 // 4 gate outcomes, 24 original-chain columns, 1 helper-call counter.
 std::cout<<"IRQ_HANDLER "<<hx(flags)<<' '<<hx(dispatch)<<' '
          <<hx(pending)<<' '<<hx(guest_handler_entered);
 for(uint64_t v:out)std::cout<<' '<<hx(v);
 std::cout<<' '<<hx(helper_calls)<<'\n';
}
}

#ifndef B2_LIST_IRQ_CHAIN_LIBRARY_ONLY
int main(){
 try{
  std::string line;
  while(std::getline(std::cin,line)){
   if(line.empty())continue;
   std::istringstream stream(line);
   std::vector<std::string> fields;
   std::string value;
   while(stream>>value)fields.push_back(value);
   one_case(fields);
  }
  return std::cin.bad()?2:0;
 }catch(const std::exception& exc){
  std::cerr<<"IRQ_HANDLER_ERROR "<<exc.what()<<'\n';return 2;
 }
}

#endif // B2_LIST_IRQ_CHAIN_LIBRARY_ONLY
