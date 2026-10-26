// SPDX-License-Identifier: GPL-3.0-only
#include "launcher/update.h"
#include <algorithm>
#include <cctype>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string_view>
namespace openkai2::launcher {
namespace {
[[noreturn]] void invalid() { throw std::runtime_error("Invalid GitHub release metadata"); }
// Bounded JSON reader. Unknown GitHub fields are parsed too, so malformed/truncated
// responses cannot accidentally turn into a usable update candidate.
struct Json {
    enum Type { null, boolean, number, string, array, object } type = null;
    std::string value;
    std::vector<Json> items;
    std::map<std::string, Json> fields;
    const Json &at(const std::string &key) const {
        const auto i = fields.find(key);
        if (type != object || i == fields.end()) invalid();
        return i->second;
    }
    std::string text() const { if (type != string) invalid(); return value; }
    bool flag() const { if (type != boolean) invalid(); return value == "true"; }
    uint64_t size() const {
        if (type != number || value.empty() || value.size() > 12 ||
            value.find_first_not_of("0123456789") != std::string::npos) invalid();
        return std::stoull(value);
    }
};
class Reader {
    std::string_view input;
    size_t pos = 0, nodes = 0;
    void space() { while (pos < input.size() && (input[pos]==' ' || input[pos]=='\r' || input[pos]=='\n' || input[pos]=='\t')) ++pos; }
    bool take(char c) { space(); if (pos<input.size() && input[pos]==c) {++pos; return true;} return false; }
    unsigned hex() {
        unsigned n=0;
        for(unsigned i=0;i<4;++i) {
            if(pos==input.size()) invalid();
            const auto c=input[pos++];
            const auto v=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;
            if(v<0) invalid(); n=n*16+unsigned(v);
        }
        return n;
    }
    std::string text() {
        if(!take('"')) invalid();
        std::string out;
        while(pos<input.size()) {
            unsigned char c=input[pos++];
            if(c=='"') return out;
            if(c<32) invalid();
            if(c!='\\') {out+=char(c); continue;}
            if(pos==input.size()) invalid();
            const char escape=input[pos++];
            switch(escape) {
            case '"':case '\\':case '/':out+=escape;break;
            case 'b':out+='\b';break;case 'f':out+='\f';break;case 'n':out+='\n';break;
            case 'r':out+='\r';break;case 't':out+='\t';break;
            case 'u': {
                unsigned cp=hex();
                if(cp>=0xd800 && cp<=0xdbff) {
                    if(pos+2>input.size() || input.substr(pos,2)!="\\u") invalid();
                    pos+=2;const auto low=hex();if(low<0xdc00||low>0xdfff) invalid();
                    cp=0x10000+((cp-0xd800)<<10)+(low-0xdc00);
                } else if(cp>=0xdc00 && cp<=0xdfff) invalid();
                if(cp<0x80) out+=char(cp);
                else if(cp<0x800) {out+=char(0xc0|(cp>>6));out+=char(0x80|(cp&63));}
                else {if(cp>=0x10000) out+=char(0xf0|(cp>>18));
                      out+=char((cp>=0x10000?0x80:0xe0)|((cp>>12)&(cp>=0x10000?63:15)));
                      out+=char(0x80|((cp>>6)&63));out+=char(0x80|(cp&63));}
                break;
            }
            default:invalid();
            }
        }
        invalid();
    }
    Json read(unsigned depth) {
        if(depth>32 || ++nodes>200000) invalid();
        space(); if(pos==input.size()) invalid();
        Json j;
        if(input[pos]=='"') {j.type=Json::string;j.value=text();}
        else if(take('[')) {
            j.type=Json::array;
            if(!take(']')) {do {j.items.push_back(read(depth+1));}while(take(','));if(!take(']'))invalid();}
        } else if(take('{')) {
            j.type=Json::object;
            if(!take('}')) {do {auto key=text();if(!take(':'))invalid();
                if(!j.fields.emplace(key,read(depth+1)).second)invalid();
            }while(take(','));if(!take('}'))invalid();}
        } else if(input.substr(pos,4)=="true") {j.type=Json::boolean;j.value="true";pos+=4;}
        else if(input.substr(pos,5)=="false") {j.type=Json::boolean;j.value="false";pos+=5;}
        else if(input.substr(pos,4)=="null") pos+=4;
        else {
            const auto start=pos;
            if(input[pos]=='-')++pos;
            if(pos==input.size())invalid();
            if(input[pos]=='0')++pos;
            else {if(input[pos]<'1'||input[pos]>'9')invalid();while(pos<input.size()&&std::isdigit(static_cast<unsigned char>(input[pos])))++pos;}
            auto digits=[&] {const auto s=pos;while(pos<input.size()&&std::isdigit(static_cast<unsigned char>(input[pos])))++pos;if(s==pos)invalid();};
            if(pos<input.size()&&input[pos]=='.') {++pos;digits();}
            if(pos<input.size()&&(input[pos]=='e'||input[pos]=='E')) {++pos;if(pos<input.size()&&(input[pos]=='+'||input[pos]=='-'))++pos;digits();}
            j.type=Json::number;j.value=std::string(input.substr(start,pos-start));
        }
        return j;
    }
public:
    explicit Reader(const std::string &s):input(s) {if(s.size()>8*1024*1024)invalid();}
    Json parse() {auto j=read(0);space();if(pos!=input.size())invalid();return j;}
};
struct Version { std::vector<std::string> core, pre; };
std::vector<std::string> identifiers(const std::string &s, bool numericOnly) {
    std::vector<std::string> result;
    std::istringstream stream(s);std::string part;
    if(s.empty()||s.back()=='.')invalid();
    while(std::getline(stream,part,'.')) {
        if(part.empty()||part.find_first_not_of("0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz-")!=std::string::npos)invalid();
        const bool numeric=part.find_first_not_of("0123456789")==std::string::npos;
        if((numericOnly&&!numeric)||(numeric&&part.size()>1&&part[0]=='0'))invalid();
        result.push_back(part);
    }
    return result;
}
Version version(std::string s) {
    if(s.empty()||s.size()>100)invalid();
    const auto plus=s.find('+');if(plus!=s.npos) { // Build identifiers may contain leading zeros.
        auto build=s.substr(plus+1);if(build.empty()||build.front()=='.'||build.back()=='.'||build.find("..")!=build.npos||build.find_first_not_of("0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz-.")!=build.npos)invalid();s.resize(plus);
    }
    const auto dash=s.find('-');Version v;
    if(dash!=s.npos) {v.pre=identifiers(s.substr(dash+1),false);s.resize(dash);}
    v.core=identifiers(s,true);if(v.core.size()!=3)invalid();return v;
}
int numeric(const std::string &a,const std::string &b) {
    return a.size()!=b.size()?(a.size()<b.size()?-1:1):a==b?0:a<b?-1:1;
}
} // namespace
int compare_versions(const std::string &a,const std::string &b) {
    auto x=version(a),y=version(b);
    for(size_t i=0;i<3;++i)if(auto c=numeric(x.core[i],y.core[i]))return c;
    if(x.pre.empty()||y.pre.empty())return x.pre.empty()==y.pre.empty()?0:x.pre.empty()?1:-1;
    for(size_t i=0;i<std::min(x.pre.size(),y.pre.size());++i) {
        const auto &p=x.pre[i],&q=y.pre[i];const bool pn=p.find_first_not_of("0123456789")==p.npos,qn=q.find_first_not_of("0123456789")==q.npos;
        const auto c=pn&&qn?numeric(p,q):pn!=qn?pn?-1:1:p==q?0:p<q?-1:1;if(c)return c;
    }
    return x.pre.size()==y.pre.size()?0:x.pre.size()<y.pre.size()?-1:1;
}
bool trusted_asset_url(const std::string &url,const std::string &tag,const std::string &name) {
    if(tag.empty()||tag[0]!='v'||tag.find_first_not_of("v0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz-.+")!=tag.npos)return false;
    if(name!="OpenKai2.exe"&&name!="OpenKai2-windows-x64.zip"&&name!="SHA256SUMS.txt")return false;
    return url=="https://github.com/7v0biscuit/OpenKai2/releases/download/"+tag+"/"+name;
}
const UpdateAsset &update_asset(const UpdateRelease &r,const std::string &name) {
    const UpdateAsset *found=nullptr;
    for(const auto &a:r.assets)if(a.name==name) {if(found)invalid();found=&a;}
    if(!found||!trusted_asset_url(found->url,r.tag,name)||!found->size||found->size>512ull*1024*1024)throw std::runtime_error("Release is missing a supported Windows update asset");
    return *found;
}
std::optional<UpdateRelease> select_release(const std::string &json,const std::string &current,bool prereleases) {
    (void)version(current);auto document=Reader(json).parse();if(document.type!=Json::array)invalid();
    std::optional<UpdateRelease> chosen;
    for(const auto &item:document.items) {
        if(item.at("draft").flag())continue;
        const auto tag=item.at("tag_name").text();
        if(tag.empty()||tag[0]!='v')continue;
        Version v;try {v=version(tag.substr(1));}catch(const std::exception &) {continue;}
        const bool pre=item.at("prerelease").flag()||!v.pre.empty();
        if(pre&&!prereleases)continue;
        if(compare_versions(tag.substr(1),current)<=0)continue;
        UpdateRelease r{tag.substr(1),tag,pre,{}};
        const auto &assets=item.at("assets");if(assets.type!=Json::array)invalid();
        for(const auto &a:assets.items) r.assets.push_back({a.at("name").text(),a.at("browser_download_url").text(),a.at("size").size()});
        // Incomplete publication is ignored; the next check can pick it up.
        try {update_asset(r,"SHA256SUMS.txt");update_asset(r,"OpenKai2.exe");update_asset(r,"OpenKai2-windows-x64.zip");}
        catch(const std::exception &) {continue;}
        if(!chosen||compare_versions(r.version,chosen->version)>0)chosen=std::move(r);
    }
    return chosen;
}
std::string checksum_for(const std::string &data,const std::string &name) {
    if(data.size()>65536)invalid();std::istringstream in(data);std::string line,result;
    while(std::getline(in,line)) {
        if(!line.empty()&&line.back()=='\r')line.pop_back();
        if(line.empty())continue;
        if(line.size()<67||line[64]!=' '||(line[65]!=' '&&line[65]!='*')||line.substr(0,64).find_first_not_of("0123456789abcdefABCDEF")!=line.npos)throw std::runtime_error("Invalid release checksum file");
        if(line.substr(66)==name) {if(!result.empty())throw std::runtime_error("Duplicate release checksum");result=line.substr(0,64);}
    }
    if(result.empty())throw std::runtime_error("Update checksum is missing");
    std::transform(result.begin(),result.end(),result.begin(),[](unsigned char c){return char(std::tolower(c));});return result;
}
} // namespace openkai2::launcher
