// SPDX-License-Identifier: GPL-3.0-only
#include "speedrun_images.h"
#include "split_image_test_data.h"
#include "disc_image.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace openkai2::speedrun;
namespace {
void require(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
template<class F> void rejects(F action) {try{action();}catch(const std::exception&){return;}throw std::runtime_error("Invalid image accepted");}
// Tiny codec fixtures contain only solid color pixels, not launcher artwork.
using namespace openkai2::test_images;
template<size_t N> void write(const std::filesystem::path& file,const unsigned char (&bytes)[N]) {
    std::ofstream out(file,std::ios::binary);out.write(reinterpret_cast<const char*>(bytes),N);
}
}
int main() {
    try {
        SplitImage timing;timing.frames.resize(3);timing.frames[0].delay_ms=80;timing.frames[1].delay_ms=120;timing.frames[2].delay_ms=160;
        require(timing.frame_at(0)==0 && timing.frame_at(79)==0 && timing.frame_at(80)==1 && timing.frame_at(199)==1 &&
                timing.frame_at(200)==2 && timing.frame_at(359)==2 && timing.frame_at(360)==0,"GIF frame timing changed");
        require(valid_image_file(std::string(64,'a')+".png") && valid_image_file(std::string(64,'b')+".jpeg"),"Valid image key rejected");
        require(!valid_image_file("../escape.png") && !valid_image_file(std::string(64,'c')+".bmp"),"Invalid image key accepted");
#ifdef _WIN32
        const auto root=std::filesystem::temp_directory_path()/L"OpenKai2 split image checks";
        std::filesystem::create_directories(root);
        struct Cleanup {std::filesystem::path path;~Cleanup(){std::error_code e;std::filesystem::remove_all(path,e);}} cleanup{root};
        write(root/L"alpha.png",png);write(root/L"photo.JPEG",jpeg);write(root/L"motion.gif",gif);
        auto alpha=load_split_image(root/L"alpha.png");
        require(alpha.width==3 && alpha.height==2 && alpha.frames.size()==1 && alpha.frames[0].rgba[7]==128 && alpha.frames[0].rgba[11]==0,"PNG alpha or dimensions changed");
        auto photo=load_split_image(root/L"photo.JPEG");
        require(photo.width==3 && photo.height==2 && photo.frames.size()==1,"JPEG could not be decoded");
        auto motion=load_split_image(root/L"motion.gif");
        require(motion.width==3 && motion.height==2 && motion.frames.size()==4,"GIF frame count or dimensions changed");
        const unsigned delays[]={80,120,160,200};
        for(size_t i=0;i<motion.frames.size();++i) {
            require(motion.frames[i].delay_ms==delays[i],"GIF delay was lost");
            for(size_t p=0;p<motion.frames[i].rgba.size();p+=4) {
                const auto expected=gif_expected+i*24+p;
                require(motion.frames[i].rgba[p+3]==expected[3],"GIF disposal changed transparency");
                if(expected[3])for(unsigned c=0;c<3;++c)
                    require(motion.frames[i].rgba[p+c]==expected[c],"GIF frame composition changed pixels");
            }
        }
        const auto imported=import_split_image(root/L"motion.gif",root/L"stored");
        require(valid_image_file(imported.file) && imported.name=="motion.gif" && std::filesystem::exists(root/L"motion.gif") &&
                openkai2::sha256_file(root/L"motion.gif")==openkai2::sha256_file(root/L"stored"/imported.file),"Import changed or removed the original file");
        require(import_split_image(root/L"motion.gif",root/L"stored").file==imported.file,"Duplicate image import did not reuse its file");
        std::ofstream(root/L"stored"/imported.file,std::ios::binary|std::ios::trunc)<<"damaged";
        import_split_image(root/L"motion.gif",root/L"stored");
        require(openkai2::sha256_file(root/L"motion.gif")==openkai2::sha256_file(root/L"stored"/imported.file),"Reimport did not repair the cached image");
        std::ofstream(root/L"bad.gif")<<"GIF89a";std::ofstream(root/L"empty.png")<<"not an image";
        rejects([&]{load_split_image(root/L"bad.gif");});
        rejects([&]{load_split_image(root/L"empty.png");});
        rejects([&]{import_split_image(root/L"alpha.png",root/L"alpha.png");});
        std::ofstream large(root/L"large.png",std::ios::binary);large.seekp(16*1024*1024);large.put(0);large.close();
        rejects([&]{load_split_image(root/L"large.png");});
#endif
#ifdef _WIN32
        std::cout<<"Split image checks passed: PNG alpha, JPEG, animated GIF timing, disposal, imports and invalid files.\n";
#else
        std::cout<<"Split image keys and animation timing passed. Decoding uses Windows WIC.\n";
#endif
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
