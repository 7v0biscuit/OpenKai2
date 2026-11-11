// SPDX-License-Identifier: GPL-3.0-only
#include "speedrun_images.h"
#include "disc_image.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincodec.h>
#include <propidl.h>
#endif

namespace openkai2::speedrun {
namespace {
constexpr size_t max_file_bytes = 16u * 1024u * 1024u;
constexpr unsigned max_edge = 96, max_frames = 300;
constexpr uint64_t max_decode_pixels = 64u * 1024u * 1024u;

std::vector<uint8_t> read_image(const std::filesystem::path& file) {
    std::error_code error;
    const auto size = std::filesystem::file_size(file, error);
    if (error || !size || size > max_file_bytes)
        throw std::runtime_error("Choose a PNG, JPEG or GIF up to 16 MB.");
    std::vector<uint8_t> bytes(size_t(size), uint8_t{});
    std::ifstream in(file, std::ios::binary);
    if (!in.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size())))
        throw std::runtime_error("Could not read the selected image.");
    return bytes;
}

#ifdef _WIN32
template<class T> struct ComPtr {
    T* value = nullptr;
    ~ComPtr() { if (value) value->Release(); }
    T* operator->() const { return value; }
    T** address() { return &value; }
};
void checked(HRESULT result) {
    if (FAILED(result))
        throw std::runtime_error("Could not decode this image. Choose a valid PNG, JPEG or GIF.");
}
unsigned metadata(IWICMetadataQueryReader* reader, const wchar_t* name, unsigned fallback = 0) {
    if (!reader)
        return fallback;
    PROPVARIANT value{};
    const auto result = reader->GetMetadataByName(name, &value);
    unsigned out = fallback;
    if (SUCCEEDED(result)) {
        if (value.vt == VT_UI1) out = value.bVal;
        else if (value.vt == VT_UI2) out = value.uiVal;
        else if (value.vt == VT_UI4) out = value.ulVal;
        else if (value.vt == VT_BOOL) out = value.boolVal != VARIANT_FALSE;
    }
    PropVariantClear(&value);
    return out;
}

ImageFrame resize_frame(const std::vector<uint8_t>& source, unsigned width, unsigned height,
                        unsigned out_width, unsigned out_height, unsigned delay) {
    ImageFrame frame;
    frame.delay_ms = delay;
    frame.rgba.resize(size_t(out_width) * out_height * 4);
    for (unsigned y = 0; y < out_height; ++y) {
        const double sy = std::clamp((double(y) + 0.5) * height / out_height - 0.5, 0.0, double(height - 1));
        const auto y0 = unsigned(sy), y1 = std::min(y0 + 1, height - 1);
        for (unsigned x = 0; x < out_width; ++x) {
            const double sx = std::clamp((double(x) + 0.5) * width / out_width - 0.5, 0.0, double(width - 1));
            const auto x0 = unsigned(sx), x1 = std::min(x0 + 1, width - 1);
            const double fx = sx - x0, fy = sy - y0;
            const size_t samples[] = {(size_t(y0)*width+x0)*4, (size_t(y0)*width+x1)*4,
                                      (size_t(y1)*width+x0)*4, (size_t(y1)*width+x1)*4};
            const double weights[] = {(1-fx)*(1-fy), fx*(1-fy), (1-fx)*fy, fx*fy};
            double alpha = 0, rgb[3]{};
            for (unsigned i = 0; i < 4; ++i) {
                const double weight = source[samples[i]+3] * weights[i];
                alpha += weight;
                for (unsigned c = 0; c < 3; ++c)
                    rgb[c] += source[samples[i]+c] * weight;
            }
            const auto target = (size_t(y)*out_width+x)*4;
            frame.rgba[target+3] = uint8_t(std::lround(alpha));
            for (unsigned c = 0; c < 3; ++c)
                frame.rgba[target+c] = alpha > 0 ? uint8_t(std::lround(rgb[c] / alpha)) : 0;
        }
    }
    return frame;
}

SplitImage decode_image(const std::vector<uint8_t>& bytes) {
    const auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    struct Apartment { HRESULT result; ~Apartment() { if (SUCCEEDED(result)) CoUninitialize(); } } apartment{initialized};
    ComPtr<IWICImagingFactory> factory;
    checked(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                             IID_IWICImagingFactory, reinterpret_cast<void**>(factory.address())));
    ComPtr<IWICStream> stream;
    checked(factory->CreateStream(stream.address()));
    checked(stream->InitializeFromMemory(const_cast<BYTE*>(bytes.data()), DWORD(bytes.size())));
    ComPtr<IWICBitmapDecoder> decoder;
    checked(factory->CreateDecoderFromStream(stream.value, nullptr, WICDecodeMetadataCacheOnLoad, decoder.address()));
    GUID format{};
    checked(decoder->GetContainerFormat(&format));
    const bool gif = IsEqualGUID(format, GUID_ContainerFormatGif);
    if (!gif && !IsEqualGUID(format, GUID_ContainerFormatPng) && !IsEqualGUID(format, GUID_ContainerFormatJpeg))
        throw std::runtime_error("Only PNG, JPEG and GIF images are supported.");
    UINT count = 0;
    checked(decoder->GetFrameCount(&count));
    if (!count || count > max_frames)
        throw std::runtime_error("Choose a GIF with up to 300 frames.");
    if (!gif) count = 1;
    ComPtr<IWICBitmapFrameDecode> first;
    checked(decoder->GetFrame(0, first.address()));
    UINT width = 0, height = 0;
    checked(first->GetSize(&width, &height));
    ComPtr<IWICMetadataQueryReader> global;
    decoder->GetMetadataQueryReader(global.address());
    if (gif) {
        width = metadata(global.value, L"/logscrdesc/Width", width);
        height = metadata(global.value, L"/logscrdesc/Height", height);
    }
    if (!width || !height || width > 4096 || height > 4096 || uint64_t(width)*height*count > max_decode_pixels)
        throw std::runtime_error("This image is too large. Choose a smaller image or a shorter GIF.");
    SplitImage result;
    const double scale = std::min(1.0, double(max_edge) / std::max(width, height));
    result.width = std::max(1u, unsigned(std::lround(width * scale)));
    result.height = std::max(1u, unsigned(std::lround(height * scale)));
    std::vector<uint8_t> canvas(size_t(width)*height*4, 0), previous;
    unsigned disposal = 0, old_left = 0, old_top = 0, old_width = 0, old_height = 0;
    uint8_t background[4]{};
    if (gif) {
        ComPtr<IWICMetadataQueryReader> first_metadata;
        first->GetMetadataQueryReader(first_metadata.address());
        const auto bg = metadata(global.value, L"/logscrdesc/BackgroundColorIndex");
        const bool transparent = metadata(first_metadata.value, L"/grctlext/TransparencyFlag") &&
            metadata(first_metadata.value, L"/grctlext/TransparentColorIndex") == bg;
        ComPtr<IWICPalette> palette;
        if (!transparent && SUCCEEDED(factory->CreatePalette(palette.address())) &&
            SUCCEEDED(decoder->CopyPalette(palette.value))) {
            WICColor colors[256]{}; UINT used = 0;
            if (SUCCEEDED(palette->GetColors(256, colors, &used)) && bg < used) {
                const auto color = colors[bg];
                background[0]=uint8_t(color>>16); background[1]=uint8_t(color>>8);
                background[2]=uint8_t(color); background[3]=255;
            }
        }
        for (size_t p = 0; p < canvas.size(); p += 4)
            std::copy(background, background+4, canvas.begin()+p);
    }
    for (UINT index = 0; index < count; ++index) {
        if (disposal == 2)
            for (unsigned y = old_top; y < old_top+old_height; ++y)
                for (unsigned x = old_left; x < old_left+old_width; ++x)
                    std::copy(background, background+4, canvas.begin()+(size_t(y)*width+x)*4);
        else if (disposal == 3 && !previous.empty())
            canvas = previous;
        ComPtr<IWICBitmapFrameDecode> frame;
        checked(decoder->GetFrame(index, frame.address()));
        UINT fw = 0, fh = 0;
        checked(frame->GetSize(&fw, &fh));
        ComPtr<IWICMetadataQueryReader> query;
        frame->GetMetadataQueryReader(query.address());
        const unsigned left = gif ? metadata(query.value, L"/imgdesc/Left") : 0;
        const unsigned top = gif ? metadata(query.value, L"/imgdesc/Top") : 0;
        if (!fw || !fh || left >= width || top >= height || fw > width-left || fh > height-top)
            throw std::runtime_error("The image contains an invalid frame.");
        disposal = gif ? metadata(query.value, L"/grctlext/Disposal") : 0;
        if (disposal == 3) previous = canvas;
        ComPtr<IWICFormatConverter> converter;
        checked(factory->CreateFormatConverter(converter.address()));
        checked(converter->Initialize(frame.value, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone,
                                      nullptr, 0, WICBitmapPaletteTypeCustom));
        std::vector<uint8_t> pixels(size_t(fw)*fh*4);
        checked(converter->CopyPixels(nullptr, fw*4, UINT(pixels.size()), pixels.data()));
        for (unsigned y = 0; y < fh; ++y)
            for (unsigned x = 0; x < fw; ++x) {
                const auto from = (size_t(y)*fw+x)*4, to = (size_t(y+top)*width+x+left)*4;
                if (!gif || pixels[from+3])
                    std::copy(pixels.begin()+from, pixels.begin()+from+4, canvas.begin()+to);
            }
        const unsigned raw_delay = gif ? metadata(query.value, L"/grctlext/Delay", 10) : 10;
        result.frames.push_back(resize_frame(canvas, width, height, result.width, result.height,
                                             raw_delay ? raw_delay*10 : 100));
        old_left=left; old_top=top; old_width=fw; old_height=fh;
    }
    return result;
}
#else
SplitImage decode_image(const std::vector<uint8_t>&) {
    throw std::runtime_error("Custom split images require Windows.");
}
#endif
}

size_t SplitImage::frame_at(uint64_t milliseconds) const {
    uint64_t duration = 0;
    for (const auto& frame : frames) duration += frame.delay_ms;
    if (!duration) return 0;
    milliseconds %= duration;
    for (size_t i = 0; i < frames.size(); ++i) {
        if (milliseconds < frames[i].delay_ms) return i;
        milliseconds -= frames[i].delay_ms;
    }
    return 0;
}

bool valid_image_file(const std::string& file) {
    if (file.empty()) return true;
    if (file.size() < 68 || file.size() > 69 || file[64] != '.') return false;
    if (!std::all_of(file.begin(), file.begin()+64, [](unsigned char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    })) return false;
    const auto extension = file.substr(64);
    return extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".gif";
}

SplitImage load_split_image(const std::filesystem::path& file) { return decode_image(read_image(file)); }

ImportedImage import_split_image(const std::filesystem::path& source, const std::filesystem::path& directory) {
    auto extension = source.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    if (extension != ".png" && extension != ".jpg" && extension != ".jpeg" && extension != ".gif")
        throw std::runtime_error("Choose a PNG, JPEG or GIF image.");
    const auto bytes = read_image(source);
    ImportedImage result;
    result.image = decode_image(bytes);
    result.file = sha256_text(std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size())) + extension;
    const auto filename = source.filename().u8string();
    result.name.assign(reinterpret_cast<const char*>(filename.data()), filename.size());
    std::filesystem::create_directories(directory);
    const auto destination = directory / result.file;
    if (!std::filesystem::exists(destination) || sha256_file(destination) != result.file.substr(0,64)) {
        auto temporary = destination; temporary += ".tmp";
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
        out.close();
        if (!out) throw std::runtime_error("Could not store the split image.");
#ifdef _WIN32
        if (!MoveFileExW(temporary.c_str(),destination.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Could not store the split image.");
#else
        std::filesystem::rename(temporary,destination);
#endif
    }
    return result;
}
}
