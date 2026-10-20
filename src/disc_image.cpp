// SPDX-License-Identifier: GPL-3.0-only
#include "disc_image.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace openkai2 {
namespace {
constexpr uint64_t sector_size = 2048;
using Bytes = std::vector<uint8_t>;

class Sha256 {
    std::array<uint32_t, 8> state_{{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                  0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19}};
    std::array<uint8_t, 64> block_{};
    uint64_t size_ = 0;
    size_t used_ = 0;
    static uint32_t rotate(uint32_t x, unsigned n) { return (x >> n) | (x << (32 - n)); }
    void compress() {
        static constexpr uint32_t k[] = {
            0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
            0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
            0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
            0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
            0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
            0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
            0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
            0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
        uint32_t w[64]{};
        for (unsigned i = 0; i < 16; ++i)
            w[i] = (uint32_t(block_[4*i]) << 24) | (uint32_t(block_[4*i+1]) << 16)
                 | (uint32_t(block_[4*i+2]) << 8) | block_[4*i+3];
        for (unsigned i = 16; i < 64; ++i) {
            const auto a = rotate(w[i-15], 7) ^ rotate(w[i-15], 18) ^ (w[i-15] >> 3);
            const auto b = rotate(w[i-2], 17) ^ rotate(w[i-2], 19) ^ (w[i-2] >> 10);
            w[i] = w[i-16] + a + w[i-7] + b;
        }
        auto a=state_[0], b=state_[1], c=state_[2], d=state_[3];
        auto e=state_[4], f=state_[5], g=state_[6], h=state_[7];
        for (unsigned i = 0; i < 64; ++i) {
            const auto s1 = rotate(e,6) ^ rotate(e,11) ^ rotate(e,25);
            const auto t1 = h + s1 + ((e & f) ^ (~e & g)) + k[i] + w[i];
            const auto s0 = rotate(a,2) ^ rotate(a,13) ^ rotate(a,22);
            const auto t2 = s0 + ((a & b) ^ (a & c) ^ (b & c));
            h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
        }
        state_[0]+=a; state_[1]+=b; state_[2]+=c; state_[3]+=d;
        state_[4]+=e; state_[5]+=f; state_[6]+=g; state_[7]+=h;
    }
public:
    void update(const uint8_t* data, size_t size) {
        size_ += size;
        while (size) {
            const auto count = std::min(size, block_.size()-used_);
            std::copy_n(data, count, block_.data()+used_);
            data += count; size -= count; used_ += count;
            if (used_ == block_.size()) { compress(); used_=0; }
        }
    }
    std::string finish() {
        const uint64_t bits = size_ * 8;
        const uint8_t marker=0x80, zero=0;
        update(&marker,1);
        while (used_ != 56) update(&zero,1);
        uint8_t length[8];
        for (unsigned i=0; i<8; ++i) length[7-i]=uint8_t(bits >> (8*i));
        update(length,8);
        std::ostringstream out;
        out << std::hex << std::setfill('0');
        for (auto word : state_) out << std::setw(8) << word;
        return out.str();
    }
};

[[noreturn]] void fail(const std::string& message) { throw std::runtime_error(message); }

uint32_t both32(const uint8_t* p) {
    uint32_t le=0, be=0;
    for (unsigned i=0; i<4; ++i) { le |= uint32_t(p[i]) << (8*i); be=(be<<8)|p[i+4]; }
    if (le != be) fail("ISO has inconsistent directory or volume metadata");
    return le;
}

uint16_t both16(const uint8_t* p) {
    const auto le=uint16_t(p[0] | (uint16_t(p[1])<<8));
    const auto be=uint16_t((uint16_t(p[2])<<8) | p[3]);
    if (le != be) fail("ISO has inconsistent volume metadata");
    return le;
}

std::string upper(std::string value) {
    for (auto& ch : value) ch=char(std::toupper(static_cast<unsigned char>(ch)));
    return value;
}

std::string filename(std::string value, bool directory) {
    const auto version=value.find(';');
    if (version != std::string::npos) {
        const auto suffix=value.substr(version+1);
        if (directory || suffix.empty() || !std::all_of(suffix.begin(),suffix.end(),
            [](unsigned char c){return c>='0' && c<='9';})) fail("Invalid ISO filename version");
        value.resize(version);
    }
    if (!directory && !value.empty() && value.back()=='.') value.pop_back();
    if (value.empty() || value=="." || value==".." || value.back()=='.' || value.back()==' ')
        fail("ISO contains an invalid filename");
    for (const unsigned char ch : value)
        if (ch<32 || ch>=127 || std::string("/\\:*?\"<>|").find(char(ch))!=std::string::npos)
            fail("ISO contains an unsafe or unsupported filename");
    const auto base=upper(value.substr(0,value.find('.')));
    if (base=="CON" || base=="PRN" || base=="AUX" || base=="NUL"
        || (base.size()==4 && (base.substr(0,3)=="COM" || base.substr(0,3)=="LPT")
            && base[3]>='0' && base[3]<='9')) fail("ISO contains a Windows device filename");
    return value;
}

struct Extent { uint64_t offset, size; };
struct Entry { std::filesystem::path path; std::vector<Extent> extents; uint64_t size=0; };

class Iso {
    std::ifstream stream_;
    uint64_t image_size_, volume_size_=0;
    std::set<uint32_t> directories_;
    std::set<std::string> paths_;
    std::vector<Entry> files_;
    std::vector<IsoEntry> entries_;
    Extent extent(const uint8_t* record) const {
        if (record[1] || record[26] || record[27]) fail("Extended or interleaved ISO records are unsupported");
        if (both16(record+28)!=1) fail("Multi-volume ISO files are unsupported");
        const Extent e{uint64_t(both32(record+2))*sector_size, both32(record+10)};
        if (e.offset>volume_size_ || e.size>volume_size_-e.offset) fail("ISO file extent is outside the image");
        return e;
    }
    void directory(Extent dir, const std::filesystem::path& parent, unsigned depth) {
        if (depth>32 || directories_.size()>=10000 || dir.size>64*1024*1024)
            fail("ISO directory limits exceeded");
        if (!directories_.insert(uint32_t(dir.offset/sector_size)).second)
            fail("ISO contains a directory cycle or alias");
        Bytes bytes(size_t(dir.size), uint8_t(0));
        read(dir.offset,bytes.data(),bytes.size());
        bool continuing=false;
        std::string continuing_key;
        for (size_t pos=0; pos<bytes.size();) {
            const size_t length=bytes[pos];
            if (!length) { pos=((pos/sector_size)+1)*sector_size; continue; }
            if (length<34 || length>bytes.size()-pos || (pos%sector_size)+length>sector_size)
                fail("ISO has a malformed directory record");
            const auto* r=bytes.data()+pos;
            const size_t name_size=r[32];
            if (!name_size || 33+name_size>length) fail("ISO has a malformed filename record");
            const auto e=extent(r);
            if (name_size==1 && r[33]<=1) { pos+=length; continue; }
            const bool is_directory=(r[25]&2)!=0, more=(r[25]&0x80)!=0;
            if (r[25]&0x6c) fail("ISO contains an unsupported file record");
            const auto path=parent/filename(std::string(reinterpret_cast<const char*>(r+33),name_size),is_directory);
            if (path.generic_string().size()>220) fail("ISO path exceeds the supported length");
            const auto key=upper(path.generic_string());
            if (continuing && key!=continuing_key) fail("ISO multi-extent file is incomplete");
            if (is_directory) {
                if (more || continuing || !paths_.insert(key).second) fail("ISO has a conflicting directory");
                entries_.push_back({path,uint32_t(e.offset/sector_size),e.size,true});
                directory(e,path,depth+1);
            } else {
                if (!continuing) {
                    if (!paths_.insert(key).second) fail("ISO contains conflicting filenames");
                    if (files_.size()>=200000) fail("ISO file count exceeds the supported limit");
                    files_.push_back({path,{ },0});
                    entries_.push_back({path,uint32_t(e.offset/sector_size),0,false});
                }
                files_.back().extents.push_back(e);
                if (files_.back().extents.size()>1024) fail("Too many extents in an ISO file");
                files_.back().size+=e.size;
                entries_.back().size=files_.back().size;
                continuing=more;
                continuing_key=key;
            }
            pos+=length;
        }
        if (continuing) fail("ISO multi-extent file is incomplete");
    }
public:
    explicit Iso(const std::filesystem::path& path) : stream_(path,std::ios::binary) {
        if (!stream_) fail("Game ISO is required. Pass the path to your Budokai 2 USA .iso file.");
        stream_.seekg(0,std::ios::end);
        const auto end=stream_.tellg();
        if (end<0) fail("Could not determine ISO size");
        image_size_=uint64_t(end);
        if (image_size_<18*sector_size || image_size_%sector_size)
            fail("Expected a complete 2048-byte-sector ISO image");
        bool found=false, terminated=false;
        Bytes primary;
        for (uint64_t sector=16; sector<256 && sector*sector_size<image_size_; ++sector) {
            Bytes descriptor(sector_size);
            read(sector*sector_size,descriptor.data(),descriptor.size());
            if (!std::equal(descriptor.begin()+1,descriptor.begin()+6,"CD001") || descriptor[6]!=1)
                fail("Expected an ISO9660 game disc image");
            if (descriptor[0]==1) {
                if (found) fail("ISO has multiple primary volume descriptors");
                primary=descriptor; found=true;
            }
            if (descriptor[0]==255) { terminated=true; break; }
        }
        if (!found || !terminated) fail("ISO volume descriptors are incomplete");
        if (both16(primary.data()+128)!=sector_size || both16(primary.data()+120)!=1
            || both16(primary.data()+124)!=1) fail("Unsupported ISO block size or volume sequence");
        volume_size_=uint64_t(both32(primary.data()+80))*sector_size;
        if (!volume_size_ || volume_size_>image_size_) fail("ISO volume is truncated");
        const auto* root=primary.data()+156;
        if (root[0]<34 || root[32]!=1 || root[33]!=0 || !(root[25]&2)) fail("ISO root directory is invalid");
        const auto root_extent=extent(root);
        entries_.push_back({{},uint32_t(root_extent.offset/sector_size),root_extent.size,true});
        directory(root_extent,{},0);
    }
    void read(uint64_t offset, uint8_t* buffer, size_t count) {
        if (offset>image_size_ || count>image_size_-offset) fail("ISO read exceeds the image");
        stream_.clear(); stream_.seekg(std::streamoff(offset));
        stream_.read(reinterpret_cast<char*>(buffer),std::streamsize(count));
        if (size_t(stream_.gcount())!=count) fail("ISO is truncated or unreadable");
    }
    const std::vector<Entry>& files() const { return files_; }
    const std::vector<IsoEntry>& entries() const { return entries_; }
    std::string hash(const Entry& entry, std::ostream* output=nullptr) {
        Sha256 digest;
        Bytes buffer(1024*1024);
        for (const auto& e : entry.extents) {
            for (uint64_t done=0; done<e.size;) {
                const auto count=size_t(std::min<uint64_t>(buffer.size(),e.size-done));
                read(e.offset+done,buffer.data(),count);
                digest.update(buffer.data(),count);
                if (output) {
                    output->write(reinterpret_cast<const char*>(buffer.data()),std::streamsize(count));
                    if (!*output) fail("Cannot write disc cache; check free disk space");
                }
                done+=count;
            }
        }
        return digest.finish();
    }
};

// Inspect each cache component before writing, including the supplied root.
void make_safe_directory(const std::filesystem::path& path) {
    if (path.empty()) return;
    auto current=path.root_path();
    for (const auto& component : path.relative_path()) {
        current/=component;
        std::error_code error;
        const auto status=std::filesystem::symlink_status(current,error);
        if (std::filesystem::is_symlink(status)) fail("Disc cache contains a symbolic link");
        if (std::filesystem::exists(status)) {
            if (!std::filesystem::is_directory(status)) fail("Disc cache path is not a directory");
        } else {
            if (error && error!=std::errc::no_such_file_or_directory) fail("Cannot inspect disc cache directory");
            std::filesystem::create_directory(current);
        }
    }
}

struct CacheLock {
    std::filesystem::path path;
    explicit CacheLock(const std::filesystem::path& root) : path(root/".openkai2-lock") {
        if (!std::filesystem::create_directory(path)) fail("Disc cache is in use; close other instances first");
    }
    ~CacheLock() { std::error_code ignored; std::filesystem::remove(path,ignored); }
};
} // namespace

IsoIndex::IsoIndex(const std::filesystem::path& image) : entries_(Iso(image).entries()) {
    for (size_t i=0; i<entries_.size(); ++i)
        lookup_.emplace(upper(entries_[i].path.generic_string()),i);
}

const IsoEntry* IsoIndex::find(std::string path) const {
    std::replace(path.begin(),path.end(),'\\','/');
    const auto colon=path.find(':');
    if (colon!=std::string::npos) {
        const auto device=upper(path.substr(0,colon));
        if (device!="CDROM" && device!="CDROM0") return nullptr;
        path.erase(0,colon+1);
    }
    std::string key;
    std::istringstream components(path);
    std::string component;
    try {
        while (std::getline(components,component,'/')) {
            if (component.empty() || component==".") continue;
            if (!key.empty()) key+='/';
            key+=upper(filename(component,false));
        }
    } catch (const std::runtime_error&) { return nullptr; }
    const auto found=lookup_.find(key);
    return found==lookup_.end() ? nullptr : &entries_[found->second];
}

std::string sha256_file(const std::filesystem::path& file) {
    std::ifstream input(file,std::ios::binary);
    if (!input) fail("Cannot read file: "+file.string());
    Sha256 digest;
    Bytes buffer(1024*1024);
    while (input) {
        input.read(reinterpret_cast<char*>(buffer.data()),std::streamsize(buffer.size()));
        digest.update(buffer.data(),size_t(input.gcount()));
    }
    if (!input.eof()) fail("Failed while reading file: "+file.string());
    return digest.finish();
}

DiscMount prepare_disc(const std::filesystem::path& image, const std::filesystem::path& cache,
                       const DiscOptions& options) {
    if (!std::filesystem::is_regular_file(image)) fail("Game ISO is required; a cached extraction cannot replace it");
    const auto original_size=std::filesystem::file_size(image);
    const auto original_time=std::filesystem::last_write_time(image);
    if (options.progress) options.progress("Reading game ISO and verifying disc identity...");
    Iso iso(image);
    const Entry* executable=nullptr;
    bool module=false, usr=false;
    for (const auto& entry : iso.files()) {
        const auto key=upper(entry.path.generic_string());
        if (key=="SLUS_207.79") executable=&entry;
        if (key=="MODULES/CRI_ADXI.IRX" && entry.size>=52) module=true;
        if (key.rfind("USR/",0)==0) usr=true;
    }
    if (!executable || iso.hash(*executable)!=options.expected_executable_sha256)
        fail("ISO does not contain the supported Budokai 2 USA executable");
    if (!module || !usr) fail("Game ISO is incomplete: original MODULES/CRI_ADXI.IRX and USR files are required");
    DiscMount result;
    result.image_sha256=sha256_file(image);
    result.root=std::filesystem::absolute(cache).lexically_normal()/result.image_sha256/"game";
    make_safe_directory(result.root);
    CacheLock lock(result.root.parent_path());
    for (const auto& entry : iso.files()) {
        if (options.progress) options.progress("Verifying "+entry.path.generic_string());
        const auto digest=iso.hash(entry);
        const auto output=result.root/entry.path;
        make_safe_directory(output.parent_path());
        std::error_code error;
        const auto status=std::filesystem::symlink_status(output,error);
        if (std::filesystem::is_symlink(status)) fail("Disc cache contains a symbolic link");
        if (std::filesystem::exists(status) && !std::filesystem::is_regular_file(status))
            fail("Disc cache contains a conflicting entry");
        if (error && error!=std::errc::no_such_file_or_directory) fail("Cannot inspect cached disc file");
        if (!std::filesystem::exists(status) || std::filesystem::file_size(output)!=entry.size
            || sha256_file(output)!=digest) {
            const auto temporary=lock.path/"extracting";
            try {
                std::ofstream stream(temporary,std::ios::binary|std::ios::trunc);
                if (!stream) fail("Cannot create disc cache file");
                if (iso.hash(entry,&stream)!=digest) fail("ISO changed during extraction");
                stream.close();
                if (!stream || sha256_file(temporary)!=digest) fail("Disc cache verification failed after writing");
                if (std::filesystem::exists(output)) std::filesystem::remove(output);
                std::filesystem::rename(temporary,output);
            } catch (...) {
                std::error_code ignored; std::filesystem::remove(temporary,ignored); throw;
            }
            ++result.files_repaired;
        }
        result.bytes_verified+=entry.size;
        result.files.push_back({entry.path,entry.size,digest});
    }
    if (!std::filesystem::is_regular_file(image) || std::filesystem::file_size(image)!=original_size
        || std::filesystem::last_write_time(image)!=original_time) fail("ISO changed during verification; restart with an unchanged image");
    result.executable=result.root/executable->path;
    return result;
}

std::filesystem::path default_cache_directory() {
#ifdef _WIN32
    if (const auto* root=_wgetenv(L"LOCALAPPDATA")) return std::filesystem::path(root)/"OpenKai2/discs";
#else
    if (const auto* root=std::getenv("XDG_CACHE_HOME")) return std::filesystem::path(root)/"openkai2/discs";
    if (const auto* root=std::getenv("HOME")) return std::filesystem::path(root)/".cache/openkai2/discs";
#endif
    return std::filesystem::temp_directory_path()/"openkai2/discs";
}
} // namespace openkai2
