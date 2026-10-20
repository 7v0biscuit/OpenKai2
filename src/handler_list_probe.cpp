// Phase211: isolated, low-64-bit native translation of the first straight-line
// prefix of the pinned USA handler. No retail ELF, dispatch registration, PS2
// runtime, or guest execution. This is NOT a full function implementation.
// Entry instructions: 0x00113770..0x00113798 inclusive; stop before 0x0011379C.
// The final bgez has an unconditionally executed lw delay slot.
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace {
constexpr uint64_t MASK32=0xffffffffULL;
constexpr uint32_t kGlobal = 0x0033b288;
constexpr uint32_t kMmio = 0x10001010;
constexpr uint32_t kMmioCounter = 0x10001000;
constexpr uint32_t kNode = 0x00210040;
uint64_t sext32(uint64_t val) {
  const uint32_t v=static_cast<uint32_t>(val);
  return (v&0x80000000U) ? (0xffffffff00000000ULL|v) : static_cast<uint64_t>(v);
}
uint64_t number(const char* arg) {
  char* end=nullptr;
  const auto parsed=std::strtoull(arg,&end,0);
  if (!arg[0] || end==arg || *end) throw std::runtime_error("bad numeric arg");
  return static_cast<uint64_t>(parsed);
}
// Phase227: bounded guest physical regions shared by EE 32-bit direct-map
// windows. This is NOT a TLB, RAM allocation, full EE bus or MMIO device.
// The older low-address physical fixtures remain backward compatible.
struct Mem {
  std::unordered_map<uint32_t,uint8_t> bytes;
  static bool stack(uint32_t addr) { return addr>=0x001ffe00 && addr<0x00200300; }
  static bool global(uint32_t addr) { return addr>=kGlobal && addr<kGlobal+0x80; }
  static bool permitted(uint32_t addr) { return stack(addr)||global(addr)|| (addr>=kMmio && addr<kMmio+4) || (addr>=kMmioCounter && addr<kMmioCounter+4) || (addr>=kNode && addr<kNode+0xc0); }
  static uint32_t translate(uint32_t addr) {
    // Explicit low-address test-fixture window and MIPS KSEG0/KSEG1 aliases.
    // 0x20000000..0x7fffffff needs TLB mapping (unsupported),
    // and 0xc0000000..0xffffffff is unsupported in this sandbox.
    if(addr<0x20000000U)return addr;
    if(addr>=0x80000000U && addr<0xc0000000U)return addr&0x1fffffffU;
    throw std::runtime_error("unmapped guest segment");
  }
  static uint32_t check(uint64_t virtual_addr) {
    if(virtual_addr>0xffffffffULL)throw std::runtime_error("guest address overflow");
    const uint32_t physical=translate(static_cast<uint32_t>(virtual_addr));
    if(!permitted(physical))throw std::runtime_error("guest address outside mapped probe regions");
    return physical;
  }
  uint8_t get(uint32_t addr) const {
    const uint32_t physical=check(addr);
    const auto at=bytes.find(physical);
    return at==bytes.end() ? (stack(physical)?0xa5:0x00) : at->second;
  }
  void put(uint32_t addr,uint8_t x) {
    const uint32_t physical=check(addr);
    bytes[physical]=x;
  }
  static std::array<uint32_t,8> validate(uint32_t addr,unsigned n) {
    if(n!=1 && n!=2 && n!=4 && n!=8)throw std::runtime_error("invalid guest access width");
    if((addr&(n-1))!=0)throw std::runtime_error("unaligned guest access");
    std::array<uint32_t,8> addresses{};
    for(unsigned i=0;i<n;++i)addresses[i]=check(static_cast<uint64_t>(addr)+i);
    return addresses;
  }
  uint64_t read(uint32_t addr,unsigned n) const {
    const auto addresses=validate(addr,n);
    uint64_t v=0;
    for(unsigned i=0;i<n;++i) {
      const auto at=bytes.find(addresses[i]);
      const uint32_t p=addresses[i];
      const uint8_t b=(at==bytes.end())?(stack(p)?0xa5:0x00):at->second;
      v|=(static_cast<uint64_t>(b)<<(8*i));
    }
    return v;
  }
  void write(uint32_t addr,uint64_t v,unsigned n) {
    // Validate the entire access before committing any bytes: no partial writes.
    const auto addresses=validate(addr,n);
    for(unsigned i=0;i<n;++i)bytes[addresses[i]]=static_cast<uint8_t>(v>>(8*i));
  }
};

struct CPU {
  std::array<uint64_t,32> r{};
  Mem memory;
  uint64_t next_pc=0;
  bool branch_likely_taken=false;
  bool delay_executed=false;
  bool mmio_read=false;
  bool list_head_read=false;
  bool followup_branch_taken=false;
  bool followup_delay_executed=false;
  bool zero_exit_tested=false;
  bool returned_from_handler=false;
  bool return_delay_executed=false;
  bool threshold_branch_seen=false;
  bool threshold_branch_taken=false;
  bool threshold_delay_executed=false;
  bool external_call_pending=false;
  bool external_call_executed=false;
  bool external_call_delay_executed=false;
  bool mmio_counter_read=false;
  uint64_t external_call_target=0;
  uint64_t external_call_site=0;
};
// Literal operations are a direct native C++ translation, not a generic
// interpreter. Memory semantics and register sign extension are explicit.
void translated_entry_prefix(CPU& cpu) {
  // 0x00113770: addiu sp,sp,-64 -- 32-bit ADDIU result sign-extends to 64.
  cpu.r[29]=sext32(static_cast<uint32_t>(cpu.r[29])-64U);
  // 0x00113774: lui v0,0x0034.
  cpu.r[2]=sext32(0x00340000U);
  // 0x00113778: sd ra,48(sp).
  cpu.memory.write(static_cast<uint32_t>(cpu.r[29])+48U,cpu.r[31],8);
  // 0x0011377C: addiu v0,v0,-19832.
  cpu.r[2]=sext32(static_cast<uint32_t>(cpu.r[2])-19832U);
  // 0x00113780: sd s2,32(sp).
  cpu.memory.write(static_cast<uint32_t>(cpu.r[29])+32U,cpu.r[18],8);
  // 0x00113784: daddu a3,a0,zero -- copy full low 64 bits.
  cpu.r[7]=cpu.r[4];
  // 0x00113788/8c: save s1 and s0.
  cpu.memory.write(static_cast<uint32_t>(cpu.r[29])+16U,cpu.r[17],8);
  cpu.memory.write(static_cast<uint32_t>(cpu.r[29]),cpu.r[16],8);
  // 0x00113790: lw v1,28(v0) sign-extends 32-bit value.
  cpu.r[3]=sext32(cpu.memory.read(static_cast<uint32_t>(cpu.r[2])+28U,4));
  // 0x00113794: bgez v1,target. MIPS delay slot must always execute.
  const bool taken=(cpu.r[3]>>63U)==0;
  // 0x00113798: lw a2,24(v0), the branch delay slot.
  cpu.r[6]=sext32(cpu.memory.read(static_cast<uint32_t>(cpu.r[2])+24U,4));
  cpu.next_pc=taken ? 0x001138e8U : 0x0011379cU;
}
// Only on the negative bgez path, translate exactly five more original
// instructions 0x0011379C..0x001137AC. No call, IRQ, or guest execution.
void translated_successor(CPU& cpu) {
  if (cpu.next_pc != 0x0011379cU) return;
  // 0x11379C: lui v0,0x1000; 0x1137A0: ori v0,v0,0x1010.
  cpu.r[2]=sext32(0x10000000U);
  cpu.r[2]=(cpu.r[2]&~0xffffULL)|((cpu.r[2]&0xffffULL)|0x1010ULL);
  // 0x1137A4: lw s2,0(v0), sign-extended 32-bit MMIO read.
  cpu.r[18]=sext32(cpu.memory.read(static_cast<uint32_t>(cpu.r[2]),4));
  cpu.mmio_read=true;
  // 0x1137A8: bnel a2,zero,0x1137DC.
  cpu.branch_likely_taken=cpu.r[6]!=0;
  if (cpu.branch_likely_taken) {
    // 0x1137AC: ld v0,32(a2) is executed ONLY on taken branch.
    cpu.r[2]=cpu.memory.read(static_cast<uint32_t>(cpu.r[6])+32U,8);
    cpu.delay_executed=true;
    cpu.next_pc=0x001137dcU;
  } else {
    // MIPS branch-likely: annul delay slot when condition is false.
    cpu.next_pc=0x001137b0U;
  }
  if (cpu.next_pc == 0x001137dcU) {
    // Four retail EE instructions 0x1137DC..0x1137E8, only on taken bnel.
    // ld v1,16(a2); ld a0,24(a2); daddu v0,v0,v1; lw a2,0(a2)
    const uint32_t node=static_cast<uint32_t>(cpu.r[6]);
    cpu.r[3]=cpu.memory.read(node+16U,8);
    cpu.r[4]=cpu.memory.read(node+24U,8);
    cpu.r[2]+=cpu.r[3]; // DADDU wraparound, no signed overflow exception.
    cpu.r[6]=sext32(cpu.memory.read(node,4));
    cpu.list_head_read=true;
    cpu.next_pc=0x001137ecU; // continue through the original BEQ + delay slot
  }
  if (cpu.next_pc == 0x001137ecU) {
    // 0x1137EC: beq a2,zero,0x113854. 0x1137F0: dsubu s0,v0,a0.
    // BEQ executes its delay slot on BOTH outcomes (unlike BNEL).
    const bool zero_next = cpu.r[6] == 0ULL;
    cpu.r[16] = cpu.r[2] - cpu.r[4]; // DSUBU wraps at 64 bits.
    cpu.followup_delay_executed = true;
    cpu.followup_branch_taken = zero_next;
    cpu.next_pc = zero_next ? 0x00113854U : 0x001137f4U;
    if (zero_next) {
      // 0x113854: andi s1,s2,3 (16-bit zero-extended mask).
      cpu.r[17] = cpu.r[18] & 3ULL;
      // 0x113858: dsubu v0,s0,a3 (64-bit wraparound).
      cpu.r[2] = cpu.r[16] - cpu.r[7];
      // 0x11385C: slti v0,v0,0x7333; signed 64-bit compare.
      // The sign-bit test avoids implementation-defined uint64->int64 cast.
      cpu.r[2] = ((cpu.r[2] >> 63U) || cpu.r[2] < 0x7333ULL) ? 1ULL : 0ULL;
      cpu.zero_exit_tested = true;
      cpu.next_pc = 0x00113860U; // STOP before subsequent BEQ.
    }
  }
}


// Two additional original blocks are independently validated by an opcode oracle.
// 0x1138E8..0x1138FC is the early-return epilogue reached on nonnegative bgez.
// 0x113860..0x113864 tests the zero-exit threshold decision, but stops before
// its external-helper successors at 0x113868 or 0x1138B0.
void translated_return_or_threshold(CPU& cpu) {
  if (cpu.next_pc == 0x001138e8U) {
    const uint32_t sp=static_cast<uint32_t>(cpu.r[29]);
    // ld ra,48(sp); ld s2,32(sp); ld s1,16(sp); ld s0,0(sp)
    cpu.r[31]=cpu.memory.read(sp+48U,8);
    cpu.r[18]=cpu.memory.read(sp+32U,8);
    cpu.r[17]=cpu.memory.read(sp+16U,8);
    cpu.r[16]=cpu.memory.read(sp,8);
    // jr ra, followed by mandatory addiu sp,sp,64 delay slot.
    const uint64_t target=cpu.r[31];
    cpu.r[29]=sext32(static_cast<uint32_t>(cpu.r[29])+64U);
    cpu.return_delay_executed=true;
    cpu.returned_from_handler=true;
    cpu.next_pc=target; // preserve the 64-bit JR target in the diagnostic
    return;
  }
  if (cpu.next_pc == 0x00113860U) {
    // 0x113860: beq v0,zero,0x1138B0.
    const bool taken=cpu.r[2]==0ULL;
    // 0x113864: lui v0,0x1000 executes whether branch is taken or not.
    cpu.r[2]=sext32(0x10000000U);
    cpu.threshold_branch_seen=true;
    cpu.threshold_branch_taken=taken;
    cpu.threshold_delay_executed=true;
    cpu.next_pc=taken ? 0x001138b0U : 0x00113868U;
  }
}

// Independently bounded original handler call-site translations. JAL's delay
// slot runs before control transfers. The external target is NEVER invoked.
void translated_external_call_boundary(CPU& cpu) {
  if (cpu.next_pc==0x001138b0U) {
    // 0x1138B0: lui a0,0xFFFF; 0x1138B4: ori a0,a0,0xF7FF.
    cpu.r[4]=sext32(0xFFFF0000U);
    cpu.r[4]=cpu.r[4] | 0xF7FFULL;
    // 0x1138B8: jal 0x113400, with 0x1138BC: and a0,s2,a0.
    cpu.r[31]=0x001138c0U;
    cpu.r[4]=cpu.r[18] & cpu.r[4];
    cpu.external_call_pending=true;
    cpu.external_call_delay_executed=true;
    cpu.external_call_target=0x00113400U;
    cpu.external_call_site=0x001138b8U;
    cpu.next_pc=cpu.external_call_target;
  } else if (cpu.next_pc==0x00113868U) {
    // 0x113868: sll a1,s1,2. MIPS SLL writes 32-bit sign extension.
    cpu.r[5]=sext32(static_cast<uint32_t>(cpu.r[17])<<2U);
    // 0x11386C: ori v0,v0,0x1000; 0x113870: addiu a0,zero,0x7333.
    cpu.r[2]=cpu.r[2] | 0x1000ULL;
    cpu.r[4]=0x7333ULL;
    // 0x113874: dsrlv a0,a0,a1, 6-bit shift mask.
    cpu.r[4]>>=(cpu.r[5] & 63ULL);
    // 0x113878: lw v1,0(v0).
    cpu.r[3]=sext32(cpu.memory.read(static_cast<uint32_t>(cpu.r[2]),4));
    cpu.mmio_counter_read=true;
    // 0x11387C: dsll32 a0,a0,0; 0x113880: dsra32 a0,a0,0.
    cpu.r[4]=sext32(cpu.r[4]);
    // 0x113884: jal 0x113410; 0x113888: addu a0,v1,a0 (32-bit).
    cpu.r[31]=0x0011388cU;
    cpu.r[4]=sext32(static_cast<uint32_t>(cpu.r[3])+static_cast<uint32_t>(cpu.r[4]));
    cpu.external_call_pending=true;
    cpu.external_call_delay_executed=true;
    cpu.external_call_target=0x00113410U;
    cpu.external_call_site=0x00113884U;
    cpu.next_pc=cpu.external_call_target;
  }
}

std::string hx(uint64_t v) {
  std::ostringstream s;s<<"0x"<<std::hex<<std::uppercase<<std::setw(16)<<std::setfill('0')<<v;
  return s.str();
}
uint64_t stack_fnv(const Mem& mem,uint32_t base) {
  uint64_t digest=14695981039346656037ULL;
  for(uint32_t i=0;i<128;++i) {
    digest^=mem.get(base+i);digest*=1099511628211ULL;
  }
  return digest;
}
} // namespace

#ifndef B2_HANDLER_LIBRARY_ONLY
int main(int argc,char** argv) {
  try {
    // probe CASE SP RA S0 S1 S2 A0 SIGNED_GLOBAL_VALUE DELAY_SLOT_GLOBAL_VALUE
    if(argc!=16) { std::cerr<<"expected: case plus 14 numeric values\n";return 2; }
    CPU cpu;
    const std::string name=argv[1];
    for(size_t i=0;i<name.size();++i) if(!((name[i]>='a'&&name[i]<='z')||(name[i]>='0'&&name[i]<='9')||name[i]=='_')) throw std::runtime_error("invalid case id");
    cpu.r[29]=number(argv[2]);cpu.r[31]=number(argv[3]);cpu.r[16]=number(argv[4]);
    cpu.r[17]=number(argv[5]);cpu.r[18]=number(argv[6]);cpu.r[4]=number(argv[7]);
    cpu.r[2]=0xdeadbeef;cpu.r[3]=0xdeaddead;cpu.r[6]=0x7777777777777777ULL;cpu.r[7]=0x8888888888888888ULL;
    cpu.memory.write(kGlobal+28U,number(argv[8]),4);
    cpu.memory.write(kGlobal+24U,number(argv[9]),4);
    cpu.memory.write(kMmio,number(argv[10]),4);
    cpu.memory.write(kMmioCounter,number(argv[15]),4);
    cpu.memory.write(kNode+32U,number(argv[11]),8);
    // Private synthetic node fields. Never read game memory directly.
    cpu.memory.write(kNode+16U,number(argv[12]),8);
    cpu.memory.write(kNode+24U,number(argv[13]),8);
    cpu.memory.write(kNode,number(argv[14]),4);
    translated_entry_prefix(cpu);
    translated_successor(cpu);
    uint32_t stackbase=static_cast<uint32_t>(cpu.r[29]);
    translated_return_or_threshold(cpu);
    translated_external_call_boundary(cpu);
    std::cout<<"PHASE211_BRANCH {\"case\":\""<<name<<"\",\"status\":\"TRANSLATED_BOUNDED_BLOCK_RETURNED\""
             <<",\"next_pc\":\""<<hx(cpu.next_pc)<<"\""
             <<",\"sp\":\""<<hx(cpu.r[29])<<"\""
             <<",\"ra\":\""<<hx(cpu.r[31])<<"\""
             <<",\"v0\":\""<<hx(cpu.r[2])<<"\""
             <<",\"v1\":\""<<hx(cpu.r[3])<<"\""
             <<",\"a2\":\""<<hx(cpu.r[6])<<"\""
             <<",\"a0\":\""<<hx(cpu.r[4])<<"\""
             <<",\"a3\":\""<<hx(cpu.r[7])<<"\""
             <<",\"s2\":\""<<hx(cpu.r[18])<<"\""
             <<",\"s0\":\""<<hx(cpu.r[16])<<"\""
             <<",\"s1\":\""<<hx(cpu.r[17])<<"\""
             <<",\"delay_executed\":"<<(cpu.delay_executed?"true":"false")
             <<",\"branch_likely_taken\":"<<(cpu.branch_likely_taken?"true":"false")
             <<",\"mmio_read\":"<<(cpu.mmio_read?"true":"false")
             <<",\"list_head_read\":"<<(cpu.list_head_read?"true":"false")
             <<",\"followup_branch_taken\":"<<(cpu.followup_branch_taken?"true":"false")
             <<",\"followup_delay_executed\":"<<(cpu.followup_delay_executed?"true":"false")
             <<",\"zero_exit_tested\":"<<(cpu.zero_exit_tested?"true":"false")
             <<",\"returned_from_handler\":"<<(cpu.returned_from_handler?"true":"false")
             <<",\"return_delay_executed\":"<<(cpu.return_delay_executed?"true":"false")
             <<",\"threshold_branch_seen\":"<<(cpu.threshold_branch_seen?"true":"false")
             <<",\"threshold_branch_taken\":"<<(cpu.threshold_branch_taken?"true":"false")
             <<",\"threshold_delay_executed\":"<<(cpu.threshold_delay_executed?"true":"false")
             <<",\"external_call_pending\":"<<(cpu.external_call_pending?"true":"false")
             <<",\"external_call_executed\":"<<(cpu.external_call_executed?"true":"false")
             <<",\"external_call_delay_executed\":"<<(cpu.external_call_delay_executed?"true":"false")
             <<",\"external_call_target\":\""<<hx(cpu.external_call_target)<<"\""
             <<",\"external_call_site\":\""<<hx(cpu.external_call_site)<<"\""
             <<",\"mmio_counter_read\":"<<(cpu.mmio_counter_read?"true":"false")
             <<",\"saved_ra\":\""<<hx(cpu.memory.read(stackbase+48U,8))<<"\""
             <<",\"saved_s2\":\""<<hx(cpu.memory.read(stackbase+32U,8))<<"\""
             <<",\"saved_s1\":\""<<hx(cpu.memory.read(stackbase+16U,8))<<"\""
             <<",\"saved_s0\":\""<<hx(cpu.memory.read(stackbase,8))<<"\""
             <<",\"stack_checksum\":\""<<hx(stack_fnv(cpu.memory,stackbase))<<"\"}"
             <<std::endl;
    return 0;
  } catch(const std::exception& e) {std::cerr<<"PHASE211_ERROR "<<e.what()<<"\n";return 2;}
}

#endif // B2_HANDLER_LIBRARY_ONLY: embedded in phase221 bridged test
