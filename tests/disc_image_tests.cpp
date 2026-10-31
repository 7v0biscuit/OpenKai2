// SPDX-License-Identifier: GPL-3.0-only
#include "disc_image.h"
#include "iso_fixture.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace fs=std::filesystem;
using namespace openkai2::test;
namespace {
constexpr const char* abc_sha="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
unsigned checks=0;
void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
    ++checks;
}
template<class Function> void rejected(Function function, const char* description) {
    bool failed=false;
    try { function(); } catch (const std::runtime_error&) { failed=true; }
    check(failed,description);
}
}

int main() {
    const auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root=fs::temp_directory_path()/("openkai2-disc-test-"+std::to_string(stamp));
    fs::create_directories(root);
    try {
        const auto iso=root/"game with spaces.iso", cache=root/"cache";
        const auto hash_input=root/"hash-input";
        write(hash_input,{});
        check(openkai2::sha256_file(hash_input)=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855","Empty SHA-256");
        write(hash_input,{'a','b','c'});
        check(openkai2::sha256_file(hash_input)==abc_sha,"SHA-256 abc vector");
        write(hash_input,Bytes(1000000,'a'));
        check(openkai2::sha256_file(hash_input)=="cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0","SHA-256 multi-block vector");
        openkai2::DiscOptions options;
        options.expected_executable_sha256=abc_sha;
        const auto valid=fixture(); write(iso,valid);
        openkai2::IsoIndex index(iso);
        check(index.find("cdrom0:\\usr\\data.bin;1") && index.find("usr/data.bin")->sector==23,"Original ISO file sector retained");
        check(index.find("USR")->directory,"ISO directory lookup");
        check(index.find("USR/../SLUS_207.79")==nullptr,"ISO lookup rejects parent traversal");
        auto mounted=openkai2::prepare_disc(iso,cache,options);
        check(mounted.files.size()==3,"All disc files indexed");
        check(mounted.bytes_verified==61,"All disc file bytes verified");
        check(mounted.files_repaired==3,"Initial extraction");
        check(read(mounted.executable)=="abc","Executable extracted exactly");
        check(read(mounted.root/"USR/DATA.BIN")=="abcdef","Disc data extracted exactly");
        check(openkai2::prepare_disc(iso,cache,options).files_repaired==0,"Valid cache reused after re-verification");
        write(mounted.root/"USR/DATA.BIN",{'b','a','d','b','a','d'});
        check(openkai2::prepare_disc(iso,cache,options).files_repaired==1,"Same-size cache corruption repaired");
        check(read(mounted.root/"USR/DATA.BIN")=="abcdef","Corruption repaired from ISO");
        fs::remove(mounted.executable);
        check(openkai2::prepare_disc(iso,cache,options).files_repaired==1,"Missing cache file repaired");
        fs::remove(iso);
        rejected([&]{openkai2::prepare_disc(iso,cache,options);},"ISO required even with a populated cache");
        write(iso,valid);
        rejected([&]{openkai2::prepare_disc(iso,cache);},"Synthetic executable rejected by retail identity check");
        auto invalid=[&](Bytes bytes,const char* description) {
            write(iso,bytes);
            rejected([&]{openkai2::prepare_disc(iso,cache,options);},description);
        };
        auto bytes=valid; bytes.pop_back(); invalid(bytes,"Truncated sector rejected");
        bytes=valid; both32(bytes,16*sector+80,100); invalid(bytes,"Truncated volume rejected");
        bytes=valid; both16(bytes,16*sector+128,4096); invalid(bytes,"Unsupported sector size rejected");
        bytes=valid; bytes[16*sector+84]^=1; invalid(bytes,"Endian metadata mismatch rejected");
        bytes=valid; bytes[17*sector+1]='X'; invalid(bytes,"Malformed volume descriptor rejected");
        bytes=valid; bytes[21*sector]='x'; invalid(bytes,"Altered executable rejected");
        bytes=valid; both32(bytes,20*sector+2*34+2,100); invalid(bytes,"Out-of-range file extent rejected");
        bytes=valid; place(bytes,20*sector+2*34,record("..\\ESCAPE;1",23,6)); invalid(bytes,"Path traversal rejected");
        bytes=valid; place(bytes,20*sector+2*34,record("CON;1",23,6)); invalid(bytes,"Windows device filename rejected");
        bytes=valid; place(bytes,20*sector+2*34,record("DATA.BIN;1",23,6));
        place(bytes,20*sector+2*34+record("DATA.BIN;1",23,6).size(),record("data.bin;1",23,6));
        invalid(bytes,"Case-insensitive filename collision rejected");
        bytes=valid; place(bytes,19*sector+2*34,record("BACK",18,sector,2)); invalid(bytes,"Directory cycle rejected");
        bytes=valid; place(bytes,19*sector+2*34,record("OTHER_IRX;1",22,52)); invalid(bytes,"Missing CRI module rejected");
        bytes=valid; place(bytes,20*sector+2*34,record("DATA.BIN;1",23,3,0x80));
        place(bytes,20*sector+2*34+record("DATA.BIN;1",23,3,0x80).size(),record("DATA.BIN;1",24,3));
        bytes[24*sector]='d'; bytes[24*sector+1]='e'; bytes[24*sector+2]='f'; write(iso,bytes);
        check(read(openkai2::prepare_disc(iso,cache,options).root/"USR/DATA.BIN")=="abcdef","Multi-extent file reconstructed");
        bytes=valid; place(bytes,20*sector+2*34,record("DATA.BIN;1",23,3,0x80));
        invalid(bytes,"Incomplete multi-extent file rejected");
        write(iso,valid);
        fs::create_directory(mounted.root.parent_path()/".openkai2-lock");
        rejected([&]{openkai2::prepare_disc(iso,cache,options);},"Concurrent cache mutation prevented");
        fs::remove(mounted.root.parent_path()/".openkai2-lock");
        fs::remove(mounted.root/"USR/DATA.BIN");
        std::error_code error;
        fs::create_symlink(hash_input,mounted.root/"USR/DATA.BIN",error);
        if (error) std::cout << "SKIP cache symlink check: " << error.message() << '\n';
        else rejected([&]{openkai2::prepare_disc(iso,cache,options);},"Cache symlink rejected");
        fs::remove_all(root);
        std::cout << checks << " disc checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Disc test failed: " << error.what() << '\n';
        std::error_code ignored; fs::remove_all(root,ignored);
        return 1;
    }
}
