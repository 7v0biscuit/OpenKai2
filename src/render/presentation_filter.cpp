// SPDX-License-Identifier: GPL-3.0-only
#include "render/presentation_filter.h"
#include "graphics_settings.h"
#include "rlgl.h"
#include "openkai2_fsr_shader.h"
#include <algorithm>
#include <string>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace openkai2 {
namespace {
constexpr const char *linearSource = R"GLSL(#version 330
in vec2 fragTexCoord; in vec4 fragColor;
uniform sampler2D texture0; uniform vec4 colDiffuse;
uniform vec2 sourceSize; uniform vec2 contentMax;
out vec4 finalColor;
void main(){finalColor=texture(texture0,clamp(fragTexCoord,0.5/sourceSize,contentMax-0.5/sourceSize))*fragColor*colDiffuse;}
)GLSL";
// FXAA direction estimation, followed by edge-constrained two/four-tap filtering.
// The source pass runs at native resolution before the selected spatial scaler.
constexpr const char *edgeSource = R"GLSL(#version 330
in vec2 fragTexCoord; in vec4 fragColor;
uniform sampler2D texture0; uniform vec4 colDiffuse;
uniform vec2 sourceSize; uniform vec2 contentMax;
out vec4 finalColor;
vec3 readAt(vec2 uv){return texture(texture0,clamp(uv,0.5/sourceSize,contentMax-0.5/sourceSize)).rgb;}
float luma(vec3 c){return dot(c,vec3(0.299,0.587,0.114));}
void main(){
 vec2 p=fragTexCoord,d=1.0/sourceSize;
 vec3 c=readAt(p),nw=readAt(p+vec2(-1,-1)*d),ne=readAt(p+vec2(1,-1)*d);
 vec3 sw=readAt(p+vec2(-1,1)*d),se=readAt(p+vec2(1,1)*d);
 float m=luma(c),a=luma(nw),b=luma(ne),e=luma(sw),f=luma(se);
 float lo=min(m,min(min(a,b),min(e,f))),hi=max(m,max(max(a,b),max(e,f)));
 if(hi-lo<max(0.0312,hi*0.125)){finalColor=vec4(c,1.0)*fragColor*colDiffuse;return;}
 vec2 dir=vec2(-((a+b)-(e+f)),(a+e)-(b+f));
 float reduce=max((a+b+e+f)*(0.25*0.125),0.0078125);
 dir=clamp(dir/(min(abs(dir.x),abs(dir.y))+reduce),vec2(-8),vec2(8))*d;
 vec3 two=0.5*(readAt(p+dir*(-1.0/6.0))+readAt(p+dir*(1.0/6.0)));
 vec3 four=two*0.5+0.25*(readAt(p-dir*0.5)+readAt(p+dir*0.5));
 float result=luma(four);finalColor=vec4(result<lo || result>hi ? two : four,1.0)*fragColor*colDiffuse;
})GLSL";
constexpr const char *cubicSource = R"GLSL(#version 330
in vec2 fragTexCoord; in vec4 fragColor;
uniform sampler2D texture0; uniform vec4 colDiffuse;
uniform vec2 sourceSize; uniform vec2 contentMax;
out vec4 finalColor;
float kernel(float x){x=abs(x);return x<1.0 ? (1.5*x-2.5)*x*x+1.0 : (x<2.0 ? ((-0.5*x+2.5)*x-4.0)*x+2.0 : 0.0);}
void main(){vec2 pos=fragTexCoord*sourceSize-0.5,base=floor(pos),f=fract(pos);vec3 sum=vec3(0);
 for(int y=-1;y<=2;++y)for(int x=-1;x<=2;++x){vec2 uv=(base+vec2(x,y)+0.5)/sourceSize;
  sum+=texture(texture0,clamp(uv,0.5/sourceSize,contentMax-0.5/sourceSize)).rgb*kernel(float(x)-f.x)*kernel(float(y)-f.y);}
 finalColor=vec4(clamp(sum,0.0,1.0),1.0)*fragColor*colDiffuse;
})GLSL";
// FSR's algorithm is supplied by AMD's unmodified headers. The adapters below
// gather clamped active-content pixels, excluding framebuffer padding.
// Coordinates stay in top-down source space even after the optional FXAA pass.
constexpr const char *fsrEasuAdapter = R"GLSL(
in vec2 fragTexCoord; in vec4 fragColor;
uniform sampler2D texture0; uniform vec4 colDiffuse;
uniform vec2 sourceSize, contentMax, outputSize;
uniform int sourceFlipped;
out vec4 finalColor;
vec3 fsrRead(ivec2 p) {
 ivec2 extent=ivec2(contentMax*sourceSize+0.5);
 p=clamp(p,ivec2(0),extent-1);
 if(sourceFlipped!=0)p.y=extent.y-1-p.y;
 return texelFetch(texture0,p,0).rgb;
}
vec4 fsrGather(vec2 uv,int channel) {
 ivec2 p=ivec2(floor(uv*sourceSize-0.5));
 return vec4(fsrRead(p+ivec2(0,1))[channel],fsrRead(p+ivec2(1,1))[channel],
             fsrRead(p+ivec2(1,0))[channel],fsrRead(p)[channel]);
}
vec4 FsrEasuRF(vec2 uv){return fsrGather(uv,0);}
vec4 FsrEasuGF(vec2 uv){return fsrGather(uv,1);}
vec4 FsrEasuBF(vec2 uv){return fsrGather(uv,2);}
void main() {
 uvec4 c0,c1,c2,c3;
 FsrEasuCon(c0,c1,c2,c3,contentMax.x*sourceSize.x,contentMax.y*sourceSize.y,
            sourceSize.x,sourceSize.y,outputSize.x,outputSize.y);
 vec3 color;FsrEasuF(color,uvec2(gl_FragCoord.xy),c0,c1,c2,c3);
 finalColor=vec4(clamp(color,0.0,1.0),1.0);
})GLSL";
constexpr const char *fsrRcasAdapter = R"GLSL(
in vec2 fragTexCoord; in vec4 fragColor;
uniform sampler2D texture0; uniform vec4 colDiffuse;
uniform vec2 sourceSize;
out vec4 finalColor;
vec4 FsrRcasLoadF(ivec2 p){return texelFetch(texture0,clamp(p,ivec2(0),ivec2(sourceSize)-1),0);}
void FsrRcasInputF(inout float r,inout float g,inout float b){}
void main() {
 uvec4 constants;FsrRcasCon(constants,0.2);
 vec3 color;FsrRcasF(color.r,color.g,color.b,uvec2(fragTexCoord*sourceSize),constants);
 finalColor=vec4(clamp(color,0.0,1.0),1.0)*fragColor*colDiffuse;
})GLSL";
Shader fsrShader(bool sharpen) {
    std::string source="#version 430\n#define A_GPU 1\n#define A_GLSL 1\n";
    source+=sharpen ? "#define FSR_RCAS_F 1\n" : "#define FSR_EASU_F 1\n";
    for(const auto *part:fsrSourceParts)source+=part;
    source+=sharpen ? fsrRcasAdapter : fsrEasuAdapter;
    auto result=LoadShaderFromMemory(nullptr,source.c_str());
    if(!IsShaderValid(result) || GetShaderLocation(result,"sourceSize")<0 ||
       GetShaderLocation(result,sharpen ? "texture0" : "outputSize")<0)
        throw std::runtime_error("AMD FSR 1 requires OpenGL 4.3 and working EASU/RCAS shaders");
    return result;
}
Shader shader(const char *source) {
    auto result = LoadShaderFromMemory(nullptr, source);
    if (!IsShaderValid(result) || GetShaderLocation(result, "sourceSize") < 0 ||
        GetShaderLocation(result, "contentMax") < 0)
        throw std::runtime_error("Cannot initialize the selected display filter");
    return result;
}
void dimensions(Shader s, Texture2D texture, Rectangle content) {
    const float size[] = {float(texture.width), float(texture.height)};
    const float maximum[] = {content.width / size[0],
                             (content.height < 0 ? -content.height : content.height) / size[1]};
    SetShaderValue(s, GetShaderLocation(s, "sourceSize"), size, SHADER_UNIFORM_VEC2);
    SetShaderValue(s, GetShaderLocation(s, "contentMax"), maximum, SHADER_UNIFORM_VEC2);
}
} // namespace
void PresentationFilter::draw(Texture2D frame, Rectangle source, Rectangle destination) {
    const auto &s = graphics_settings();
    if (s.antialiasing) {
        if (!edge_.id)
            edge_ = shader(edgeSource);
        const auto w = int(source.width), h = int(source.height);
        if (target_.texture.width != w || target_.texture.height != h) {
            if (target_.id)
                UnloadRenderTexture(target_);
            target_ = LoadRenderTexture(w, h);
            if (!IsRenderTextureValid(target_))
                throw std::runtime_error("Cannot allocate antialiasing target");
        }
        SetTextureFilter(frame, TEXTURE_FILTER_BILINEAR);
        dimensions(edge_, frame, source);
        BeginTextureMode(target_);
        ClearBackground(BLACK);
        BeginShaderMode(edge_);
        DrawTexturePro(frame, source, {0, 0, float(w), float(h)}, {0, 0}, 0, WHITE);
        EndShaderMode();
        EndTextureMode();
        frame = target_.texture;
        source = {0, 0, float(w), -float(h)};
    }
    if (s.upscaler == 3 && destination.width >= source.width &&
        destination.height >= std::abs(source.height)) {
        drawFsr(frame, source, destination);
        return;
    }
    // EASU is an upscaler; use bicubic for a smaller output window.
    const bool cubic = s.upscaler >= 2;
    SetTextureFilter(frame, s.upscaler == 1 ? TEXTURE_FILTER_BILINEAR : TEXTURE_FILTER_POINT);
    const bool clampLinear = s.upscaler == 1 && (source.width < float(frame.width) ||
                                                 std::abs(source.height) < float(frame.height));
    if (cubic) {
        if (!cubic_.id)
            cubic_ = shader(cubicSource);
        dimensions(cubic_, frame, source);
        BeginShaderMode(cubic_);
    } else if (clampLinear) {
        if (!linear_.id)
            linear_ = shader(linearSource);
        dimensions(linear_, frame, source);
        BeginShaderMode(linear_);
    }
    DrawTexturePro(frame, source, destination, {0, 0}, 0, WHITE);
    if (cubic || clampLinear)
        EndShaderMode();
}
void PresentationFilter::drawFsr(Texture2D frame, Rectangle source, Rectangle destination) {
    const bool initializing=!easu_.id;
    if(initializing)easu_=fsrShader(false);
    if(!rcas_.id)rcas_=fsrShader(true);
    const int w=std::max(1,int(std::round(destination.width)));
    const int h=std::max(1,int(std::round(destination.height)));
    if(fsrTarget_.texture.width!=w || fsrTarget_.texture.height!=h) {
        if(fsrTarget_.id)UnloadRenderTexture(fsrTarget_);
        fsrTarget_=LoadRenderTexture(w,h);
        if(!IsRenderTextureValid(fsrTarget_))
            throw std::runtime_error("Cannot allocate AMD FSR 1 upscaling target");
    }
    if(initializing)
        std::cout << "[display:filter] fsr=1 method=easu+rcas source=" << source.width << 'x'
                  << std::abs(source.height) << " output=" << w << 'x' << h << std::endl;
    dimensions(easu_,frame,source);
    const float output[]={float(w),float(h)};
    const int flipped=source.height<0 ? 1 : 0;
    SetShaderValue(easu_,GetShaderLocation(easu_,"outputSize"),output,SHADER_UNIFORM_VEC2);
    SetShaderValue(easu_,GetShaderLocation(easu_,"sourceFlipped"),&flipped,SHADER_UNIFORM_INT);
    BeginTextureMode(fsrTarget_);
    ClearBackground(BLACK);
    BeginShaderMode(easu_);
    DrawTexturePro(frame,{0,0,source.width,std::abs(source.height)},
                   {0,0,float(w),float(h)},{0,0},0,WHITE);
    EndShaderMode();EndTextureMode();
    // EASU writes logical top-down rows into increasing texture Y. Unlike the
    // FXAA render target it therefore needs no Y inversion for presentation.
    SetShaderValue(rcas_,GetShaderLocation(rcas_,"sourceSize"),output,SHADER_UNIFORM_VEC2);
    BeginShaderMode(rcas_);
    DrawTexturePro(fsrTarget_.texture,{0,0,float(w),float(h)},destination,{0,0},0,WHITE);
    EndShaderMode();
}
void PresentationFilter::close() {
    if(fsrTarget_.id){UnloadRenderTexture(fsrTarget_);fsrTarget_={};}
    if(easu_.id){UnloadShader(easu_);easu_={};}
    if(rcas_.id){UnloadShader(rcas_);rcas_={};}
    if (target_.id) {
        UnloadRenderTexture(target_);
        target_ = {};
    }
    if (edge_.id) {
        UnloadShader(edge_);
        edge_ = {};
    }
    if (cubic_.id) {
        UnloadShader(cubic_);
        cubic_ = {};
    }
    if (linear_.id) {
        UnloadShader(linear_);
        linear_ = {};
    }
}
int test_presentation_filters() {
    const auto saved = graphics_settings();
    const auto require = [](bool ok, const char *why) {
        if (!ok)
            throw std::runtime_error(why);
    };
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(80, 64, "OpenKai2 display checks");
    if (!IsWindowReady())
        throw std::runtime_error("Display checks require OpenGL 3.3");
    Texture2D source{};
    PresentationFilter filter;
    try {
        Image image = GenImageColor(32, 32, MAGENTA);
        auto *pixels = static_cast<Color *>(image.data);
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x)
                pixels[y * 32 + x] = x > y ? WHITE : BLACK;
        source = LoadTextureFromImage(image);
        UnloadImage(image);
        auto draw = [&](unsigned aa, unsigned scaler) {
            auto settings = saved;
            settings.antialiasing = aa;
            settings.upscaler = scaler;
            set_graphics_settings(settings);
            BeginDrawing();
            ClearBackground(BLACK);
            filter.draw(source, {0, 0, 16, 16}, {16, 8, 48, 48});
            rlDrawRenderBatchActive();
            Image frame = LoadImageFromScreen();
            auto *colors = LoadImageColors(frame);
            std::vector<Color> result(colors, colors + frame.width * frame.height);
            UnloadImageColors(colors);
            UnloadImage(frame);
            EndDrawing();
            return result;
        };
        for (unsigned aa = 0; aa < 2; ++aa)
            for (unsigned scaler = 0; scaler < 4; ++scaler) {
                auto result = draw(aa, scaler);
                require(result.size() == 80u * 64u, "Display output dimensions changed");
                for (unsigned y = 0; y < 64; ++y)
                    for (unsigned x = 0; x < 80; ++x) {
                        const auto c = result[y * 80 + x];
                        require(!(c.r > 200 && c.b > 200 && c.g < 40),
                                "Display filter sampled padding outside active content");
                        if (x < 16 || x >= 64 || y < 8 || y >= 56)
                            require(c.r == 0 && c.g == 0 && c.b == 0,
                                    "Display filter changed letterbox pixels");
                    }
                const auto white = result[14 * 80 + 58], black = result[50 * 80 + 22];
                require(white.r > 245 && white.g > 245 && white.b > 245 && black.r < 8 &&
                            black.g < 8 && black.b < 8,
                        "Display filter flipped or corrupted the source");
            }
        const auto bilinear = draw(0, 1), fsr = draw(0, 3);
        unsigned changed = 0;
        for (size_t i = 0; i < fsr.size(); ++i)
            if (std::abs(int(fsr[i].r) - int(bilinear[i].r)) > 8) ++changed;
        require(changed > 16, "FSR did not produce edge-adaptive output distinct from bilinear");
        // At 1:1 scale, intermediate edge values must come from FXAA rather
        // than bilinear enlargement of the black/white input.
        auto edgeCount = [&](unsigned aa) {
            auto settings = saved;
            settings.antialiasing = aa;
            settings.upscaler = 0;
            set_graphics_settings(settings);
            BeginDrawing();
            ClearBackground(BLACK);
            filter.draw(source, {0, 0, 16, 16}, {0, 0, 16, 16});
            rlDrawRenderBatchActive();
            auto image = LoadImageFromScreen();
            auto *colors = LoadImageColors(image);
            unsigned count = 0;
            for (unsigned y = 1; y < 15; ++y)
                for (unsigned x = 1; x < 15; ++x) {
                    const auto c = colors[y * 80 + x];
                    if (c.r > 8 && c.r < 247)
                        ++count;
                }
            UnloadImageColors(colors);
            UnloadImage(image);
            EndDrawing();
            return count;
        };
        require(edgeCount(1) > edgeCount(0), "FXAA did not smooth the diagonal edge");
        filter.close();
        UnloadTexture(source);
        CloseWindow();
        set_graphics_settings(saved);
        std::cout << "Display filter comparisons passed: all eight AA/scaler combinations (including FSR 1 EASU/RCAS), content "
                     "bounds, orientation, letterboxing and native-size edge smoothing.\n";
        return 0;
    } catch (...) {
        filter.close();
        if (source.id)
            UnloadTexture(source);
        CloseWindow();
        set_graphics_settings(saved);
        throw;
    }
}
} // namespace openkai2
