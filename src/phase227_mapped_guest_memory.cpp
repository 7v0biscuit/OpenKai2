// Phase227: translated original handler entry + original MIPS linked-list window
// through the SAME bounded guest address-space backend. A virtual segment alias
// never dereferences a host pointer, executes CP0, or accesses host MMIO.
#define B2_PHASE227_LIBRARY_ONLY
#include "phase226_live_guest_memory_chain.cpp"
#undef B2_PHASE227_LIBRARY_ONLY

namespace {
constexpr uint64_t U32_MASK = 0xffffffffULL;
uint32_t segment_alias(uint64_t p,unsigned mode,unsigned index){
 const uint32_t lo=static_cast<uint32_t>(p);
 if(!lo)return 0;
 if(lo>=0x20000000U)throw std::runtime_error("fixture raw pointer outside low mapped region");
 unsigned route=mode==3 ? (index%2U)+1U:mode;
 if(route==0)return lo;
 return lo|(route==1?0x80000000U:0xa0000000U);
}
unsigned classify(const std::exception& e){
 const std::string x=e.what();
 if(x.find("unaligned")!=std::string::npos)return 3;
 if(x.find("noncanonical")!=std::string::npos)return 5;
 if(x.find("unmapped")!=std::string::npos || x.find("outside mapped")!=std::string::npos || x.find("overflow")!=std::string::npos)return 4;
 throw;
}
void case227(const std::vector<std::string>& f){
 if(f.size()!=39)throw std::runtime_error("Phase227 expects label, 36 Phase226 values, mode and fault");
 uint64_t x[33]{};
 for(unsigned i=0;i<33;++i)x[i]=number(f[i+1].c_str());
 const unsigned mode=static_cast<unsigned>(number(f[37].c_str()));
 const unsigned fault=static_cast<unsigned>(number(f[38].c_str()));
 if(mode>3||fault>5||x[0]>15)throw std::runtime_error("bad Phase227 fixture mode/fault/flags");
 const bool dispatch=(x[0]&7)==7;
 uint64_t list[17]{};
 uint64_t entered=0,loads=0,bytes_read=0,target=0;
 unsigned result=0;
 if(dispatch){
  try{
   CPU c;
   c.r[29]=x[1];c.r[31]=x[2];c.r[16]=x[3];c.r[17]=x[4];c.r[18]=x[5];c.r[4]=x[6];
   c.memory.write(kGlobal+28U,x[7],4);
   uint32_t head=segment_alias(x[8],mode,0);
   if(fault==1)head|=1U;
   if(fault==2)head=0x70001000U;
   if(fault==3)head=0xc0210080U;
   c.memory.write(kGlobal+24U,head,4);
   c.memory.write(kMmio,x[9],4);c.memory.write(kMmioCounter,x[10],4);
   for(unsigned i=0;i<3;++i){
    const unsigned j=11+4*i;
    uint32_t next=segment_alias(x[j],mode,i+1);
    if(i==0 && fault==4)next=0x00400000U;
    const uint32_t a=kNode+i*0x40U;
    c.memory.write(a,next,4);
    c.memory.write(a+16U,x[j+1],8);
    c.memory.write(a+24U,x[j+2],8);
    c.memory.write(a+32U,x[j+3],8);
   }
   translated_entry_prefix(c);
   translated_successor(c);
   if(c.next_pc!=0x1137f4U)throw std::runtime_error("fixture did not reach original list");
   entered=1;
   const unsigned patch_node=static_cast<unsigned>(number(f[34].c_str()));
   const unsigned patch_field=static_cast<unsigned>(number(f[35].c_str()));
   const uint64_t patch_value=number(f[36].c_str());
   if(patch_node>2||patch_field>3)throw std::runtime_error("bad fixture patch");
   if(patch_node){
    const uint32_t physical=kNode+patch_node*0x40U;
    const unsigned off=patch_field==0?0U:patch_field==1?16U:patch_field==2?24U:32U;
    // Deliberately mutate through the OTHER segment to test coherence.
    const uint32_t patch_address=physical|(mode==2?0x80000000U:0xa0000000U);
    c.memory.write(patch_address+off,patch_value,off?8:4);
   }
   if(fault==5)c.r[6]=0x0000000080210080ULL; // non-sign-extended KSEG0
   GuestReadAudit audit;
   const Result traversal=translate_live_memory(c.r[16],c.r[18],c.r[7],c.r[6],c.memory,audit);
   entered=1;loads=audit.loads;bytes_read=audit.bytes;
   const uint64_t fields[]={traversal.pc,traversal.r[2],traversal.r[3],traversal.r[4],
    traversal.r[5],traversal.r[6],traversal.r[7],traversal.r[16],traversal.r[17],traversal.r[18],
    traversal.first_branch_exit,traversal.null_link_exit,traversal.likely_taken,
    traversal.likely_annulled,traversal.backedges,traversal.loaded_nodes,traversal.stop_code};
   for(unsigned i=0;i<17;++i)list[i]=fields[i];
   result=traversal.stop_code==2?2:1;
   target=result==1?(traversal.r[2]?0x113410ULL:0x113400ULL):0;
  }catch(const std::exception& e){result=classify(e);}
 }
 std::cout<<"PHASE227 "<<hx(x[0])<<' '<<hx(mode)<<' '<<hx(fault)
          <<' '<<hx(dispatch?1:0)<<' '<<hx(result)<<' '<<hx(entered);
 for(auto v:list)std::cout<<' '<<hx(v);
 std::cout<<' '<<hx(loads)<<' '<<hx(bytes_read)<<' '<<hx(target)<<'\n';
}
void memory_case(const std::vector<std::string>& f){
 if(f.size()!=6)throw std::runtime_error("Phase227 MEM requires label, address, width, value, read alias, fault expectation");
 const uint32_t address=static_cast<uint32_t>(number(f[1].c_str()));
 const unsigned width=static_cast<unsigned>(number(f[2].c_str()));
 const uint64_t val=number(f[3].c_str());
 const uint32_t alias=static_cast<uint32_t>(number(f[4].c_str()));
 const uint64_t seed=number(f[5].c_str());
 Mem m;
 m.write(kNode,0x44332211,4);
 unsigned fault=0;uint64_t readback=0,before=0;
 before=m.read(kNode,4);
 try{m.write(address,val,width);readback=m.read(alias,width);}
 catch(const std::exception&e){fault=classify(e);}
 std::cout<<"PHASE227_MEM "<<hx(fault)<<' '<<hx(readback)<<' '<<hx(m.read(kNode,4))<<' '<<hx(before)<<' '<<hx(seed)<<'\n';
}
}
int main(){
 try{
  std::string line;
  while(std::getline(std::cin,line)){
   if(line.empty())continue;
   std::istringstream is(line);std::vector<std::string> f;std::string v;
   while(is>>v)f.push_back(v);
   if(f.empty())continue;
   if(f[0]=="MEM")memory_case(f);
   else case227(f);
  }
  return std::cin.bad()?2:0;
 }catch(const std::exception&e){std::cerr<<"PHASE227_ERROR "<<e.what()<<'\n';return 2;}
}
