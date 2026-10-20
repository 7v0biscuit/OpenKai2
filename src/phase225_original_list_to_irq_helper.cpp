// Phase225: original handler prefix -> decoded linked-node traversal ->
// original threshold/JAL -> original two wrappers + virtual CP0 shared helper.
// This isolated probe never executes real EE privileged ops, game IRQ or MMIO.
#define B2_PHASE225_LIBRARY_ONLY
#include "phase223_virtual_irq_original_handler.cpp"
#undef B2_PHASE225_LIBRARY_ONLY
#define B2_PHASE224_LIBRARY_ONLY
#include "phase224_original_list_walk.cpp"
#undef B2_PHASE224_LIBRARY_ONLY

namespace {
// Label FLAGS SP RA S0 S1 S2 A0 GLOBAL28 GLOBAL24 MMIO COUNTER,
// then three nodes x (NEXT,+16,+24,+32), first CP0 x5, second CP0 x5.
// Return list-state and 24 original double-helper output slots. Suppressed
// dispatches cannot enter native handler nor write virtual guest memory.
void case225(const std::vector<std::string>& f) {
 if(f.size()!=34)throw std::runtime_error("Phase225 requires label plus 33 hex arguments");
 uint64_t x[33]{};
 for(unsigned j=0;j<33;++j)x[j]=number(f[j+1].c_str());
 const unsigned flags=static_cast<unsigned>(x[0]);
 if(flags>15)throw std::runtime_error("unsupported virtual IRQ gate bits");
 const bool dispatch=(flags&1U) && (flags&2U) && (flags&4U);
 const bool pending=(flags&1U) && !(dispatch && (flags&8U));
 uint64_t list[17]{}; uint64_t chain[24]{};
 uint64_t list_entered=0,helper_calls=0;
 if(dispatch){
  CPU c;
  c.r[29]=x[1];c.r[31]=x[2];c.r[16]=x[3];c.r[17]=x[4];c.r[18]=x[5];c.r[4]=x[6];
  c.memory.write(kGlobal+28U,x[7],4);c.memory.write(kGlobal+24U,x[8],4);
  c.memory.write(kMmio,x[9],4);c.memory.write(kMmioCounter,x[10],4);
  std::array<Node,3> nodes{};
  for(unsigned i=0;i<3;++i){
   const unsigned n=11+4*i;
   nodes[i]={x[n],x[n+1],x[n+2],x[n+3]};
   const uint32_t a=kNode+i*0x40U;
   c.memory.write(a,nodes[i].next,4);
   c.memory.write(a+16U,nodes[i].v16,8);
   c.memory.write(a+24U,nodes[i].v24,8);
   c.memory.write(a+32U,nodes[i].v32,8);
  }
  translated_entry_prefix(c);
  translated_successor(c);
  if(c.next_pc!=0x1137f4U)
   throw std::runtime_error("fixture must enter list at 0x1137F4");
  list_entered=1;
  const Result traversal=translate(c.r[16],c.r[18],c.r[7],c.r[6],nodes);
  const uint64_t values[]={traversal.pc,traversal.r[2],traversal.r[3],traversal.r[4],
    traversal.r[5],traversal.r[6],traversal.r[7],traversal.r[16],traversal.r[17],traversal.r[18],
    traversal.first_branch_exit,traversal.null_link_exit,traversal.likely_taken,
    traversal.likely_annulled,traversal.backedges,traversal.loaded_nodes,traversal.stop_code};
  for(unsigned j=0;j<17;++j)list[j]=values[j];
  if(traversal.stop_code==1){
   for(unsigned r: {2U,3U,4U,5U,6U,7U,16U,17U,18U})c.r[r]=traversal.r[r];
   c.next_pc=traversal.pc;
   translated_return_or_threshold(c);
   translated_external_call_boundary(c);
   if(!c.external_call_pending)throw std::runtime_error("threshold failed to call original helper");
   const uint64_t first_site=c.external_call_site,first_target=c.next_pc;
   ++helper_calls;
   S first=virtual_helper(c.next_pc,c,static_cast<uint32_t>(x[23]),
    static_cast<uint32_t>(x[24]),static_cast<uint32_t>(x[25]),
    static_cast<uint32_t>(x[26]),static_cast<uint32_t>(x[27]));
   bool continuation=false;
   S second{};
   if(first.stop==1){
    if(first.pc!=c.r[31])throw std::runtime_error("first helper failed to return to original JAL link");
    c.r[2]=first.v0;c.r[3]=first.v1;c.r[6]=first.a2;
    c.r[4]=first.a0;c.r[5]=first.a1;
    c.next_pc=first.pc;
    translated_post_call(c);
    continuation=true;
    ++helper_calls;
    second=virtual_helper(c.next_pc,c,static_cast<uint32_t>(x[28]),
     static_cast<uint32_t>(x[29]),static_cast<uint32_t>(x[30]),
     static_cast<uint32_t>(x[31]),static_cast<uint32_t>(x[32]));
   }
   const uint64_t vals[]={first_site,first_target,first.pc,first.stop,first.status,
    first.store_addr,first.store_value,first.eret,continuation?1ULL:0ULL,
    continuation?c.next_pc:0ULL,continuation?c.r[31]:0ULL,
    continuation?c.r[29]:0ULL,continuation?c.r[16]:0ULL,
    continuation?c.r[17]:0ULL,continuation?c.r[18]:0ULL,
    continuation?c.r[4]:0ULL,second.pc,second.stop,second.status,second.store_addr,
    second.store_value,second.error_epc,second.eret,second.trace};
   static_assert(sizeof(vals)/sizeof(vals[0])==24,"original chain contract changed");
   for(unsigned j=0;j<24;++j)chain[j]=vals[j];
  }
 }
 std::cout<<"PHASE225 "<<hx(flags)<<' '<<hx(dispatch?1ULL:0ULL)<<' '
          <<hx(pending?1ULL:0ULL)<<' '<<hx(list_entered);
 for(uint64_t v:list)std::cout<<' '<<hx(v);
 for(uint64_t v:chain)std::cout<<' '<<hx(v);
 std::cout<<' '<<hx(helper_calls)<<'\n';
}
}
int main(){try{
 std::string line;
 while(std::getline(std::cin,line)){
  if(line.empty())continue;
  std::istringstream input(line);std::vector<std::string> fields;std::string x;
  while(input>>x)fields.push_back(x);
  case225(fields);
 }
 return std::cin.bad()?2:0;
}catch(const std::exception&e){std::cerr<<"PHASE225_ERROR "<<e.what()<<'\n';return 2;}}
