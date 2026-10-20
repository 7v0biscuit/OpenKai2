// Phase 224: bounded native translation of the original retail MIPS traversal
// PC 0x1137F4..0x11385C. No guest scheduler, no original handler binding.
// Only virtual, bounded node memory is touched. Branch-likely delay handling
// is explicit; C++ unsigned arithmetic implements the EE 64-bit wraparound.
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
constexpr uint32_t BASE = 0x210040U;
constexpr int MAX_BACKEDGES = 4;
uint64_t parse(const std::string& s) {
 if(s.empty() || s[0]=='-')throw std::runtime_error("negative/blank input");
 char*end=nullptr; const auto v=std::strtoull(s.c_str(),&end,0);
 if(end==s.c_str() || *end)throw std::runtime_error("invalid integer");
 return v;
}
uint64_t s32(uint64_t v) {return (v&0x80000000ULL) ? (v&0xffffffffULL)|0xffffffff00000000ULL : (v&0xffffffffULL);}
struct Node {uint64_t next,v16,v24,v32;};
struct Result {
 std::array<uint64_t,32> r{};
 uint64_t pc=0x1137f4;
 uint64_t first_branch_exit=0,null_link_exit=0,likely_taken=0,likely_annulled=0;
 uint64_t backedges=0,loaded_nodes=0,stop_code=0;
};
const Node& node_at(const std::array<Node,3>&nodes,uint64_t p) {
 const uint32_t low=static_cast<uint32_t>(p);
 if(p!=s32(low) || low<BASE || (low-BASE)%0x40 || (low-BASE)/0x40>=3)
  throw std::runtime_error("unmapped or misaligned virtual linked node");
 return nodes[(low-BASE)/0x40];
}
// The first BEQ (0x1137EC) has already transferred control to 0x1137F4;
// s0/a2/s2/a3 are initial concrete register inputs, not guessed guest state.
Result translate(uint64_t s0,uint64_t s2,uint64_t a3,uint64_t a2,const std::array<Node,3>&nodes){
 Result o;auto&r=o.r;r[16]=s0;r[18]=s2;r[7]=a3;r[6]=a2;
 auto load=[&](uint64_t address)->const Node&{++o.loaded_nodes;return node_at(nodes,address);};
 // 1137F4..113810: LD / DADDIU / LD / LD / DADDU / DSUBU / SLTU / BEQ.
 const Node&first=load(r[6]);
 r[2]=first.v32;r[3]=r[16]+0x7333ULL;r[4]=first.v16;r[5]=first.v24;
 r[2]+=r[4];r[2]-=r[5];r[3]=(r[2]<r[3])?1:0;
 // 113814 ANDI executes as a normal BEQ delay slot regardless of result.
 r[17]=r[18]&3ULL;
 if(r[3]==0){
  o.first_branch_exit=1;o.pc=0x113854; // 113854 ANDI runs once more.
  r[17]=r[18]&3ULL;
 } else {
  // 113818 DADDU s0,v0,zero; 11381C NOP.
  r[16]=r[2];
  for(;;){
   // 113820 LW a2,0(a2); 113824 BEQ; 113828 DADDIU always in slot.
   const Node&prior=node_at(nodes,r[6]);
   r[6]=s32(prior.next);r[3]=r[16]+0x7333ULL;
   if(!r[6]){o.null_link_exit=1;o.pc=0x113858;break;}
   // 11382C..113844 second list node, unsigned range comparison.
   const Node&next=load(r[6]);
   r[2]=next.v32;r[4]=next.v16;r[5]=next.v24;
   r[2]+=r[4];r[2]-=r[5];r[3]=(r[2]<r[3])?1:0;
   // 113844 BEQL v1,zero,11385C; slot executes ONLY if taken.
   if(!r[3]){
    o.likely_taken=1;
    // 113848 DSUBU v0,s0,a3, *executed* likely delay slot.
    r[2]=r[16]-r[7];o.pc=0x11385c;break;
   }
   o.likely_annulled++;
   // 11384C unconditional branch to 113820, with 113850 DADDU s0,v0.
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

}
#ifndef B2_PHASE224_LIBRARY_ONLY
namespace {
void one_case(const std::vector<std::string>&fields){
 if(fields.size()!=17)throw std::runtime_error("expected label + s0,s2,a3,a2 + 3x4 node words");
 std::array<uint64_t,16> a{};
 for(int j=0;j<16;++j)a[j]=parse(fields[j+1]);
 std::array<Node,3> n{};
 for(int j=0;j<3;++j) n[j]={a[4+j*4],a[5+j*4],a[6+j*4],a[7+j*4]};
 const Result o=translate(a[0],a[1],a[2],a[3],n);
 const uint64_t vals[]={o.pc,o.r[2],o.r[3],o.r[4],o.r[5],o.r[6],o.r[7],o.r[16],o.r[17],o.r[18],o.first_branch_exit,o.null_link_exit,o.likely_taken,o.likely_annulled,o.backedges,o.loaded_nodes,o.stop_code};
 std::cout<<"PHASE224";
 for(uint64_t v:vals)std::cout<<" 0x"<<std::hex<<v;
 std::cout<<'\n';
}
}
int main(){try{
 std::string line;
 while(std::getline(std::cin,line)){
  if(line.empty())continue;
  std::istringstream is(line);std::vector<std::string>v;std::string s;
  while(is>>s)v.push_back(s);
  one_case(v);
 }
 return std::cin.bad()?2:0;
}catch(const std::exception&e){std::cerr<<"PHASE224_ERROR "<<e.what()<<'\n';return 2;}}

#endif // B2_PHASE224_LIBRARY_ONLY
