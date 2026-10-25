// original handler prefix -> decoded linked-node traversal ->
// original threshold/JAL -> original two wrappers + virtual CP0 shared helper.
// This isolated probe never executes real EE privileged ops, game IRQ or MMIO.
#define B2_LIST_IRQ_CHAIN_LIBRARY_ONLY
#include "irq_handler.cpp"
#undef B2_LIST_IRQ_CHAIN_LIBRARY_ONLY
#define B2_LIST_WALK_LIBRARY_ONLY
#include "list_walk.cpp"
#undef B2_LIST_WALK_LIBRARY_ONLY

namespace {
// Label FLAGS SP RA S0 S1 S2 A0 GLOBAL28 GLOBAL24 MMIO COUNTER,
// then three nodes x (NEXT,+16,+24,+32), first CP0 x5, second CP0 x5.
// Return list-state and 24 original double-helper output slots. Suppressed
// dispatches cannot enter native handler nor write virtual guest memory.

// same ORIGINAL retail list instructions, but every LD and LW
// resolves against the caller's live byte-addressable guest-memory snapshot.
// No host address is ever dereferenced. No detached Node lookup is permitted.
struct GuestReadAudit { uint64_t loads=0, bytes=0; };
Result translate_live_memory(uint64_t s0,uint64_t s2,uint64_t a3,uint64_t a2,
                             const Mem& guest,GuestReadAudit& audit){
 Result o;auto&r=o.r;r[16]=s0;r[18]=s2;r[7]=a3;r[6]=a2;
 auto ld=[&](uint64_t a,unsigned offset)->uint64_t{
  const uint32_t lo=static_cast<uint32_t>(a);
  if(a!=s32(lo))throw std::runtime_error("noncanonical guest pointer");
  ++audit.loads;audit.bytes+=8;
  const uint64_t location=static_cast<uint64_t>(lo)+offset;
  if(location>0xffffffffULL)throw std::runtime_error("guest address overflow");
  return guest.read(static_cast<uint32_t>(location),8);
 };
 auto lw=[&](uint64_t a)->uint64_t{
  const uint32_t lo=static_cast<uint32_t>(a);
  if(a!=s32(lo))throw std::runtime_error("noncanonical guest pointer");
  ++audit.loads;audit.bytes+=4;
  return s32(guest.read(lo,4));
 };
 // Original 1137F4..113814 including unconditional BEQ delay slot.
 r[2]=ld(r[6],32);++o.loaded_nodes;r[3]=r[16]+0x7333ULL;
 r[4]=ld(r[6],16);r[5]=ld(r[6],24);
 r[2]+=r[4];r[2]-=r[5];r[3]=(r[2]<r[3])?1:0;
 r[17]=r[18]&3ULL;
 if(r[3]==0){
  o.first_branch_exit=1;o.pc=0x113854;
  r[17]=r[18]&3ULL;
 } else {
  r[16]=r[2];
  for(;;){
   // 113820 LW a2,0(a2). 113824 BEQ, slot 113828 DADDIU.
   r[6]=lw(r[6]);r[3]=r[16]+0x7333ULL;
   if(!r[6]){o.null_link_exit=1;o.pc=0x113858;break;}
   r[2]=ld(r[6],32);++o.loaded_nodes;
   r[4]=ld(r[6],16);r[5]=ld(r[6],24);
   r[2]+=r[4];r[2]-=r[5];r[3]=(r[2]<r[3])?1:0;
   if(!r[3]){
    o.likely_taken=1;
    r[2]=r[16]-r[7]; // BEQL delay slot executes if taken.
    o.pc=0x11385c;break;
   }
   ++o.likely_annulled;
   r[16]=r[2];++o.backedges;
   if(o.backedges>=MAX_BACKEDGES){o.stop_code=2;o.pc=0x113820;break;}
  }
 }
 if(o.stop_code==0){
  if(o.pc==0x113854){r[17]=r[18]&3ULL;o.pc=0x113858;}
  if(o.pc==0x113858){r[2]=r[16]-r[7];o.pc=0x11385c;}
  if(o.pc==0x11385c){
   r[2]=(r[2]>>63 || r[2]<0x7333ULL)?1ULL:0ULL;
   o.pc=0x113860;o.stop_code=1;
  }
 }
 return o;
}

void run_live_memory_case(const std::vector<std::string>& f) {
 if(f.size()!=37)throw std::runtime_error("LiveGuestMemory requires label plus 33 original words and 3 patch words");
 uint64_t x[33]{};
 for(unsigned j=0;j<33;++j)x[j]=number(f[j+1].c_str());
 const unsigned flags=static_cast<unsigned>(x[0]);
 if(flags>15)throw std::runtime_error("unsupported virtual IRQ gate bits");
 const bool dispatch=(flags&1U) && (flags&2U) && (flags&4U);
 const bool pending=(flags&1U) && !(dispatch && (flags&8U));
 uint64_t list[17]{}; uint64_t chain[24]{};
 uint64_t list_entered=0,helper_calls=0;
 GuestReadAudit reads{};
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
  // Change one node field AFTER the original prefix read its own memory.
  // Every subsequent LD/LW sees the updated byte array, unlike ListIrqChain's
  // pre-decoded detached node model. Patch=0 performs no write.
  const unsigned patch_node=static_cast<unsigned>(number(f[34].c_str()));
  const unsigned patch_field=static_cast<unsigned>(number(f[35].c_str()));
  const uint64_t patch_value=number(f[36].c_str());
  if(patch_node>2 || patch_field>3)throw std::runtime_error("unsupported guest patch field");
  if(patch_node){
   const unsigned off=patch_field==0?0U:patch_field==1?16U:patch_field==2?24U:32U;
   c.memory.write(kNode+patch_node*0x40U+off,patch_value,off?8:4);
  }
  const Result traversal=translate_live_memory(c.r[16],c.r[18],c.r[7],c.r[6],c.memory,reads);
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
 std::cout<<"LIVE_GUEST_MEMORY "<<hx(flags)<<' '<<hx(dispatch?1ULL:0ULL)<<' '
          <<hx(pending?1ULL:0ULL)<<' '<<hx(list_entered);
 for(uint64_t v:list)std::cout<<' '<<hx(v);
 for(uint64_t v:chain)std::cout<<' '<<hx(v);
 std::cout<<' '<<hx(helper_calls)<<' '<<hx(reads.loads)<<' '<<hx(reads.bytes)<<'\n';
}
}
#ifndef B2_MAPPED_GUEST_MEMORY_LIBRARY_ONLY
int main(){try{
 std::string line;
 while(std::getline(std::cin,line)){
  if(line.empty())continue;
  std::istringstream input(line);std::vector<std::string> fields;std::string x;
  while(input>>x)fields.push_back(x);
  run_live_memory_case(fields);
 }
 return std::cin.bad()?2:0;
}catch(const std::exception&e){std::cerr<<"LIVE_GUEST_MEMORY_ERROR "<<e.what()<<'\n';return 2;}}

#endif // B2_MAPPED_GUEST_MEMORY_LIBRARY_ONLY
