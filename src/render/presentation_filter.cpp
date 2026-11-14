// SPDX-License-Identifier: GPL-3.0-only
#include "render/presentation_filter.h"
#include "graphics_settings.h"
#include "rlgl.h"
#include "openkai2_fsr_shader.h"
#include <algorithm>
#include <utility>
#include <string>
#include <cmath>
#include <cstring>
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
constexpr const char *sharpenSource = R"GLSL(#version 330
in vec2 fragTexCoord; in vec4 fragColor;
uniform sampler2D texture0; uniform vec4 colDiffuse;
uniform vec2 sourceSize; uniform vec2 contentMax;
out vec4 finalColor;
vec3 readAt(vec2 uv){return texture(texture0,clamp(uv,0.5/sourceSize,contentMax-0.5/sourceSize)).rgb;}
void main(){
 vec2 p=fragTexCoord,d=1.0/sourceSize;
 vec3 c=readAt(p),n=readAt(p+vec2(0,-d.y)),s=readAt(p+vec2(0,d.y)),e=readAt(p+vec2(d.x,0)),w=readAt(p+vec2(-d.x,0));
 vec3 low=min(c,min(min(n,s),min(e,w))),high=max(c,max(max(n,s),max(e,w)));
 vec3 sharp=c+(4.0*c-n-s-e-w)*0.35;
 finalColor=vec4(clamp(sharp,low,high),1.0)*fragColor*colDiffuse;
}
)GLSL";
constexpr const char *vibranceSource = R"GLSL(#version 330
in vec2 fragTexCoord; in vec4 fragColor;
uniform sampler2D texture0; uniform vec4 colDiffuse;
uniform vec2 sourceSize; uniform vec2 contentMax;
out vec4 finalColor;
void main(){
 vec3 c=texture(texture0,clamp(fragTexCoord,0.5/sourceSize,contentMax-0.5/sourceSize)).rgb;
 float l=dot(c,vec3(0.299,0.587,0.114)),spread=max(c.r,max(c.g,c.b))-min(c.r,min(c.g,c.b));
 vec3 v=mix(vec3(l),c,1.0+0.45*(1.0-spread));
 v=mix(v,v*v*(3.0-2.0*v),0.15);
 finalColor=vec4(clamp(v,0.0,1.0),1.0)*fragColor*colDiffuse;
}
)GLSL";
constexpr const char *scanlineSource = R"GLSL(#version 330
in vec2 fragTexCoord; in vec4 fragColor;
uniform sampler2D texture0; uniform vec4 colDiffuse;
uniform vec2 sourceSize; uniform vec2 contentMax; uniform float gameRows;
out vec4 finalColor;
void main(){
 vec2 p=clamp(fragTexCoord,0.5/sourceSize,contentMax-0.5/sourceSize);
 vec3 c=texture(texture0,p).rgb;
 float rows=max(gameRows,1.0),phase=fract(p.y/contentMax.y*rows);
 float beam=0.72+0.28*sin(3.14159265*phase);
 float lift=1.0+0.18*(1.0-beam);
 vec2 centred=p/contentMax-0.5;
 float vignette=1.0-0.35*dot(centred,centred);
 finalColor=vec4(clamp(c*beam*lift*vignette,0.0,1.0),1.0)*fragColor*colDiffuse;
}
)GLSL";
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
constexpr const char *fxaaQualityBody = R"GLSL(
in vec2 fragTexCoord; in vec4 fragColor;
uniform sampler2D texture0; uniform vec4 colDiffuse;
uniform vec2 sourceSize; uniform vec2 contentMax;
out vec4 finalColor;
vec3 readAt(vec2 uv){return texture(texture0,clamp(uv,0.5/sourceSize,contentMax-0.5/sourceSize)).rgb;}
float luma(vec3 c){return dot(c,vec3(0.299,0.587,0.114));}
void main(){
 vec2 p=fragTexCoord,d=1.0/sourceSize;
 vec3 centre=readAt(p);float m=luma(centre);
 float n=luma(readAt(p+vec2(0,-d.y))),s=luma(readAt(p+vec2(0,d.y))),w=luma(readAt(p+vec2(-d.x,0))),e=luma(readAt(p+vec2(d.x,0)));
 float hi=max(m,max(max(n,s),max(w,e))),lo=min(m,min(min(n,s),min(w,e))),range=hi-lo;
 if(range<max(EDGE_MIN,hi*EDGE_SCALE)){finalColor=vec4(centre,1.0)*fragColor*colDiffuse;return;}
 float nw=luma(readAt(p+vec2(-d.x,-d.y))),ne=luma(readAt(p+vec2(d.x,-d.y))),sw=luma(readAt(p+vec2(-d.x,d.y))),se=luma(readAt(p+vec2(d.x,d.y)));
 float edgeH=abs(-2.0*w+nw+sw)+2.0*abs(-2.0*m+n+s)+abs(-2.0*e+ne+se);
 float edgeV=abs(-2.0*n+nw+ne)+2.0*abs(-2.0*m+w+e)+abs(-2.0*s+sw+se);
 bool horizontal=edgeH>=edgeV;
 float a=horizontal ? n : w,b=horizontal ? s : e,ga=a-m,gb=b-m;
 bool first=abs(ga)>=abs(gb);float gradient=0.25*max(abs(ga),abs(gb));
 float stepLength=horizontal ? d.y : d.x,local;
 if(first){stepLength=-stepLength;local=0.5*(a+m);}else local=0.5*(b+m);
 vec2 start=p;if(horizontal)start.y+=stepLength*0.5;else start.x+=stepLength*0.5;
 vec2 offset=horizontal ? vec2(d.x,0.0) : vec2(0.0,d.y);
 vec2 u1=start-offset,u2=start+offset;
 float e1=luma(readAt(u1))-local,e2=luma(readAt(u2))-local;
 bool done1=abs(e1)>=gradient,done2=abs(e2)>=gradient;
 const float steps[12]=float[](1.0,1.0,1.0,1.0,1.5,2.0,2.0,2.0,2.0,4.0,8.0,8.0);
 for(int i=0;i<SEARCH_STEPS && !(done1 && done2);++i){
  if(!done1){u1-=offset*steps[i];e1=luma(readAt(u1))-local;done1=abs(e1)>=gradient;}
  if(!done2){u2+=offset*steps[i];e2=luma(readAt(u2))-local;done2=abs(e2)>=gradient;}
 }
 float d1=horizontal ? p.x-u1.x : p.y-u1.y,d2=horizontal ? u2.x-p.x : u2.y-p.y;
 bool nearer1=d1<d2;float nearest=min(d1,d2),length=d1+d2;
 float edgeOffset=((nearer1 ? e1 : e2)<0.0)!=(m<local) ? 0.5-nearest/length : 0.0;
 float average=(2.0*(n+s+w+e)+nw+ne+sw+se)/12.0;
 float sub=clamp(abs(average-m)/range,0.0,1.0);sub=(-2.0*sub+3.0)*sub*sub;
 float finalOffset=max(edgeOffset,sub*sub*SUBPIXEL);
 vec2 uv=p;if(horizontal)uv.y+=finalOffset*stepLength;else uv.x+=finalOffset*stepLength;
 finalColor=vec4(readAt(uv),1.0)*fragColor*colDiffuse;
})GLSL";
const std::string fxaaHighSource=std::string("#version 330\n#define EDGE_MIN 0.0625\n#define EDGE_SCALE 0.125\n#define SEARCH_STEPS 6\n#define SUBPIXEL 0.5\n")+fxaaQualityBody;
const std::string fxaaUltraSource=std::string("#version 330\n#define EDGE_MIN 0.0312\n#define EDGE_SCALE 0.063\n#define SEARCH_STEPS 12\n#define SUBPIXEL 0.75\n")+fxaaQualityBody;
constexpr const char *lanczosSource = R"GLSL(#version 330
in vec2 fragTexCoord; in vec4 fragColor;
uniform sampler2D texture0; uniform vec4 colDiffuse;
uniform vec2 sourceSize; uniform vec2 contentMax;
out vec4 finalColor;
float lanczos(float x){x=abs(x);if(x<1e-5)return 1.0;if(x>=3.0)return 0.0;float p=3.14159265*x;return 3.0*sin(p)*sin(p/3.0)/(p*p);}
void main(){vec2 pos=fragTexCoord*sourceSize-0.5,base=floor(pos),f=fract(pos);vec3 sum=vec3(0);float total=0.0;
 for(int y=-2;y<=3;++y)for(int x=-2;x<=3;++x){float w=lanczos(float(x)-f.x)*lanczos(float(y)-f.y);vec2 uv=(base+vec2(x,y)+0.5)/sourceSize;
  sum+=texture(texture0,clamp(uv,0.5/sourceSize,contentMax-0.5/sourceSize)).rgb*w;total+=w;}
 finalColor=vec4(clamp(sum/total,0.0,1.0),1.0)*fragColor*colDiffuse;
})GLSL";
constexpr const char *sharpSource = R"GLSL(#version 330
in vec2 fragTexCoord; in vec4 fragColor;
uniform sampler2D texture0; uniform vec4 colDiffuse;
uniform vec2 sourceSize; uniform vec2 contentMax;
out vec4 finalColor;
vec3 texel(vec2 at){return texture(texture0,clamp((at+0.5)/sourceSize,0.5/sourceSize,contentMax-0.5/sourceSize)).rgb;}
void main(){vec2 pos=fragTexCoord*sourceSize-0.5,base=floor(pos),f=fract(pos);
 vec2 region=max(fwidth(pos),vec2(1e-4));f=clamp((f-0.5)/region+0.5,0.0,1.0);
 vec3 top=mix(texel(base),texel(base+vec2(1,0)),f.x),bottom=mix(texel(base+vec2(0,1)),texel(base+vec2(1,1)),f.x);
 finalColor=vec4(mix(top,bottom,f.y),1.0)*fragColor*colDiffuse;
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
constexpr const char *downsampleSource = R"GLSL(#version 330
in vec2 fragTexCoord; in vec4 fragColor;
uniform sampler2D texture0; uniform vec4 colDiffuse;
uniform vec2 sourceSize; uniform vec2 contentMax;
out vec4 finalColor;
void main(){vec2 pos=fragTexCoord*sourceSize,span=max(abs(fwidth(pos)),vec2(1.0));vec3 sum=vec3(0);
 for(int y=0;y<4;++y)for(int x=0;x<4;++x){vec2 at=pos+((vec2(x,y)+0.5)*0.25-0.5)*span;
  sum+=texture(texture0,clamp(at/sourceSize,0.5/sourceSize,contentMax-0.5/sourceSize)).rgb;}
 finalColor=vec4(sum/16.0,1.0)*fragColor*colDiffuse;
})GLSL";
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
}
void PresentationFilter::drawCached(Rectangle destination) {
    DrawTexturePro(output_.texture,{0,0,float(output_.texture.width),-float(output_.texture.height)},
                   destination,{0,0},0,WHITE);
}
void PresentationFilter::draw(Texture2D frame, Rectangle source, Rectangle destination, uint64_t generation) {
    const auto& settings=graphics_settings();
    if(!generation || (!settings.antialiasing && settings.upscaler<2u && !settings.effect) ||
       destination.width<=0 || destination.height<=0){
        cacheValid_=false;drawImage(frame,source,destination,false);return;
    }
    const auto bits=[](float value){uint32_t result;std::memcpy(&result,&value,sizeof(result));return uint64_t(result);};
    const int w=std::max(1,int(std::round(destination.width))),h=std::max(1,int(std::round(destination.height)));
    const std::array<uint64_t,12> key={generation,frame.id,uint64_t(frame.width),uint64_t(frame.height),
        bits(source.x),bits(source.y),bits(source.width),bits(source.height),uint64_t(w),uint64_t(h),
        settings.antialiasing,settings.upscaler|(uint64_t(settings.effect)<<8u)};
    if(cacheValid_ && key==cacheKey_){++reused_;drawCached(destination);return;}
    cacheValid_=false;
    if(output_.texture.width!=w || output_.texture.height!=h){
        if(output_.id)UnloadRenderTexture(output_);
        output_=LoadRenderTexture(w,h);
        if(!IsRenderTextureValid(output_))throw std::runtime_error("Cannot allocate filtered frame cache");
        SetTextureFilter(output_.texture,TEXTURE_FILTER_POINT);
    }
    drawImage(frame,source,{0,0,float(w),float(h)},true);
    if(settings.effect)applyEffect(settings.effect,w,h,std::abs(source.height));
    cacheKey_=key;cacheValid_=true;++processed_;drawCached(destination);
}
void PresentationFilter::drawImage(Texture2D frame, Rectangle source, Rectangle destination, bool cacheOutput) {
    const auto &s = graphics_settings();
    if (s.antialiasing) {
        Shader &smoothing = s.antialiasing == 3 ? fxaaUltra_ : s.antialiasing == 2 ? fxaaHigh_ : edge_;
        if (!smoothing.id)
            smoothing = shader(s.antialiasing == 3 ? fxaaUltraSource.c_str() : s.antialiasing == 2 ? fxaaHighSource.c_str() : edgeSource);
        const auto w = int(source.width), h = int(source.height);
        if (target_.texture.width != w || target_.texture.height != h) {
            if (target_.id)
                UnloadRenderTexture(target_);
            target_ = LoadRenderTexture(w, h);
            if (!IsRenderTextureValid(target_))
                throw std::runtime_error("Cannot allocate antialiasing target");
        }
        SetTextureFilter(frame, TEXTURE_FILTER_BILINEAR);
        dimensions(smoothing, frame, source);
        BeginTextureMode(target_);
        ClearBackground(BLACK);
        BeginShaderMode(smoothing);
        DrawTexturePro(frame, source, {0, 0, float(w), float(h)}, {0, 0}, 0, WHITE);
        EndShaderMode();
        EndTextureMode();
        frame = target_.texture;
        source = {0, 0, float(w), -float(h)};
    }
    if (s.upscaler != 0 && (destination.width < source.width - 0.5f ||
                            destination.height < std::abs(source.height) - 0.5f)) {
        if (!downsample_.id)downsample_ = shader(downsampleSource);
        SetTextureFilter(frame, TEXTURE_FILTER_BILINEAR);
        dimensions(downsample_, frame, source);
        if(s.upscaler==3 && cacheOutput && sharpenAfterDownsample(frame,source,destination))return;
        if(cacheOutput){BeginTextureMode(output_);ClearBackground(BLACK);}
        BeginShaderMode(downsample_);
        DrawTexturePro(frame,source,destination,{0,0},0,WHITE);
        EndShaderMode();
        if(cacheOutput)EndTextureMode();
        return;
    }
    if (s.upscaler == 3 && destination.width >= source.width &&
        destination.height >= std::abs(source.height)) {
        drawFsr(frame, source, destination, cacheOutput);
        return;
    }
    const bool cubic = s.upscaler == 2;
    SetTextureFilter(frame, s.upscaler == 1 ? TEXTURE_FILTER_BILINEAR : TEXTURE_FILTER_POINT);
    const bool clampLinear = s.upscaler == 1 && (source.width < float(frame.width) ||
                                                 std::abs(source.height) < float(frame.height));
    Shader selected{};
    if (cubic) {
        if (!cubic_.id)cubic_ = shader(cubicSource);
        dimensions(cubic_,frame,source);selected=cubic_;
    } else if (s.upscaler == 4) {
        if (!lanczos_.id)lanczos_ = shader(lanczosSource);
        dimensions(lanczos_,frame,source);selected=lanczos_;
    } else if (s.upscaler == 5) {
        if (!sharp_.id)sharp_ = shader(sharpSource);
        dimensions(sharp_,frame,source);selected=sharp_;
    } else if (clampLinear) {
        if (!linear_.id)linear_ = shader(linearSource);
        dimensions(linear_,frame,source);selected=linear_;
    }
    if(cacheOutput){BeginTextureMode(output_);ClearBackground(BLACK);}
    if(selected.id)BeginShaderMode(selected);
    DrawTexturePro(frame,source,destination,{0,0},0,WHITE);
    if(selected.id)EndShaderMode();
    if(cacheOutput)EndTextureMode();
}
bool PresentationFilter::sharpenAfterDownsample(Texture2D frame, Rectangle source, Rectangle destination) {
    if(!rcas_.id){
        try{rcas_=fsrShader(true);}
        catch(const std::exception& error){std::cout<<"[display:filter] fsr sharpening unavailable: "<<error.what()<<std::endl;return false;}
    }
    const int w=std::max(1,int(std::round(destination.width))),h=std::max(1,int(std::round(destination.height)));
    if(fsrTarget_.texture.width!=w || fsrTarget_.texture.height!=h){
        if(fsrTarget_.id)UnloadRenderTexture(fsrTarget_);
        fsrTarget_=LoadRenderTexture(w,h);
        if(!IsRenderTextureValid(fsrTarget_))throw std::runtime_error("Cannot allocate AMD FSR 1 sharpening target");
    }
    BeginTextureMode(fsrTarget_);ClearBackground(BLACK);
    BeginShaderMode(downsample_);
    DrawTexturePro(frame,source,{0,0,float(w),float(h)},{0,0},0,WHITE);
    EndShaderMode();EndTextureMode();
    const float size[]={float(w),float(h)};
    SetShaderValue(rcas_,GetShaderLocation(rcas_,"sourceSize"),size,SHADER_UNIFORM_VEC2);
    BeginTextureMode(output_);ClearBackground(BLACK);
    BeginShaderMode(rcas_);
    DrawTexturePro(fsrTarget_.texture,{0,0,float(w),-float(h)},destination,{0,0},0,WHITE);
    EndShaderMode();EndTextureMode();
    return true;
}
void PresentationFilter::applyEffect(unsigned effect, int w, int h, float gameRows) {
    Shader &selected=effect==1 ? sharpenFx_ : effect==2 ? vibranceFx_ : scanlineFx_;
    if(!selected.id)selected=shader(effect==1 ? sharpenSource : effect==2 ? vibranceSource : scanlineSource);
    if(effectTarget_.texture.width!=w || effectTarget_.texture.height!=h){
        if(effectTarget_.id)UnloadRenderTexture(effectTarget_);
        effectTarget_=LoadRenderTexture(w,h);
        if(!IsRenderTextureValid(effectTarget_))throw std::runtime_error("Cannot allocate image effect target");
        SetTextureFilter(effectTarget_.texture,TEXTURE_FILTER_POINT);
    }
    const float size[]={float(w),float(h)},maximum[]={1.0f,1.0f};
    SetShaderValue(selected,GetShaderLocation(selected,"sourceSize"),size,SHADER_UNIFORM_VEC2);
    SetShaderValue(selected,GetShaderLocation(selected,"contentMax"),maximum,SHADER_UNIFORM_VEC2);
    if(const int rows=GetShaderLocation(selected,"gameRows");rows>=0)SetShaderValue(selected,rows,&gameRows,SHADER_UNIFORM_FLOAT);
    SetTextureFilter(output_.texture,TEXTURE_FILTER_POINT);
    BeginTextureMode(effectTarget_);ClearBackground(BLACK);
    BeginShaderMode(selected);
    DrawTexturePro(output_.texture,{0,0,float(w),-float(h)},{0,0,float(w),float(h)},{0,0},0,WHITE);
    EndShaderMode();EndTextureMode();
    std::swap(output_,effectTarget_);
}
void PresentationFilter::drawFsr(Texture2D frame, Rectangle source, Rectangle destination, bool cacheOutput) {
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
    SetShaderValue(rcas_,GetShaderLocation(rcas_,"sourceSize"),output,SHADER_UNIFORM_VEC2);
    if(cacheOutput){BeginTextureMode(output_);ClearBackground(BLACK);}
    BeginShaderMode(rcas_);
    DrawTexturePro(fsrTarget_.texture,{0,0,float(w),float(h)},destination,{0,0},0,WHITE);
    EndShaderMode();
    if(cacheOutput)EndTextureMode();
}
void PresentationFilter::close() {
    if(processed_ || reused_)
        std::cout<<"[display:filters] processed="<<processed_<<" reused="<<reused_<<std::endl;
    processed_=reused_=0;cacheValid_=false;
    if(output_.id){UnloadRenderTexture(output_);output_={};}
    if(fsrTarget_.id){UnloadRenderTexture(fsrTarget_);fsrTarget_={};}
    if(effectTarget_.id){UnloadRenderTexture(effectTarget_);effectTarget_={};}
    for(Shader* fx:{&sharpenFx_,&vibranceFx_,&scanlineFx_})if(fx->id){UnloadShader(*fx);*fx={};}
    if(easu_.id){UnloadShader(easu_);easu_={};}
    if(lanczos_.id){UnloadShader(lanczos_);lanczos_={};}
    if(sharp_.id){UnloadShader(sharp_);sharp_={};}
    if(downsample_.id){UnloadShader(downsample_);downsample_={};}
    if(rcas_.id){UnloadShader(rcas_);rcas_={};}
    if (target_.id) {
        UnloadRenderTexture(target_);
        target_ = {};
    }
    if (edge_.id) {
        UnloadShader(edge_);
        edge_ = {};
    }
    if(fxaaHigh_.id){UnloadShader(fxaaHigh_);fxaaHigh_={};}
    if(fxaaUltra_.id){UnloadShader(fxaaUltra_);fxaaUltra_={};}
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
        Rectangle activeSource{0,0,16,16},outputRect{16,8,48,48};
        auto draw = [&](unsigned aa, unsigned scaler, uint64_t generation = 0, unsigned effect = 0) {
            auto settings = saved;
            settings.antialiasing = aa;
            settings.upscaler = scaler;
            settings.effect = effect;
            set_graphics_settings(settings);
            BeginDrawing();
            ClearBackground(BLACK);
            filter.draw(source, activeSource, outputRect, generation);
            rlDrawRenderBatchActive();
            Image frame = LoadImageFromScreen();
            auto *colors = LoadImageColors(frame);
            std::vector<Color> result(colors, colors + frame.width * frame.height);
            UnloadImageColors(colors);
            UnloadImage(frame);
            EndDrawing();
            return result;
        };
        for (unsigned effect = 1; effect < 4; ++effect) {
            const auto plain = draw(0, 1, 500u + effect * 2u), styled = draw(0, 1, 501u + effect * 2u, effect);
            require(plain.size() == styled.size(), "Image effect changed the output size");
            unsigned changed = 0, lit = 0;
            for (unsigned y = 0; y < 64; ++y)
                for (unsigned x = 0; x < 80; ++x) {
                    const auto a = plain[y * 80 + x], b = styled[y * 80 + x];
                    if (x < 16 || x >= 64 || y < 8 || y >= 56)
                        require(b.r == 0 && b.g == 0 && b.b == 0, "Image effect changed letterbox pixels");
                    else {
                        lit += (b.r + b.g + b.b) > 60 ? 1u : 0u;
                        changed += (std::abs(int(a.r) - int(b.r)) + std::abs(int(a.g) - int(b.g)) + std::abs(int(a.b) - int(b.b))) > 6 ? 1u : 0u;
                    }
                }
            require(lit > 400u, "Image effect blanked the picture");
            require(changed > 0u, "Image effect did not change the picture");
        }
        for (unsigned aa = 0; aa < 4; ++aa)
            for (unsigned scaler = 0; scaler < 6; ++scaler) {
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
        const auto cubicOut = draw(0, 2), lanczosOut = draw(0, 4), integerSharp = draw(0, 5), integerNearest = draw(0, 0);
        require([&] {
            for (size_t i = 0; i < integerSharp.size(); ++i)
                if (std::abs(int(integerSharp[i].r) - int(integerNearest[i].r)) > 2) return false;
            return true;
        }(), "Sharp pixels blurred an integer scale");
        outputRect = {16, 8, 40, 40};
        const auto sharpOut = draw(0, 5), nearestOut = draw(0, 0);
        outputRect = {16, 8, 48, 48};
        unsigned lanczosDiff = 0, sharpSoft = 0, nearestSoft = 0;
        for (size_t i = 0; i < cubicOut.size(); ++i) {
            if (std::abs(int(lanczosOut[i].r) - int(cubicOut[i].r)) > 2) ++lanczosDiff;
            if (sharpOut[i].r > 8 && sharpOut[i].r < 247) ++sharpSoft;
            if (nearestOut[i].r > 8 && nearestOut[i].r < 247) ++nearestSoft;
        }
        require(lanczosDiff > 8, "Lanczos matched bicubic output");
        const std::string sharpCounts = "Sharp pixels did not keep edges crisp with a one-pixel blend: soft=" +
                                        std::to_string(sharpSoft) + " nearest=" + std::to_string(nearestSoft);
        require(sharpSoft > nearestSoft && sharpSoft < 400, sharpCounts.c_str());
        const auto equivalent=[](const std::vector<Color>& a,const std::vector<Color>& b){
            if(a.size()!=b.size())return false;
            for(size_t i=0;i<a.size();++i)
                if(std::abs(int(a[i].r)-int(b[i].r))>1 || std::abs(int(a[i].g)-int(b[i].g))>1 ||
                   std::abs(int(a[i].b)-int(b[i].b))>1 || a[i].a!=b[i].a)return false;
            return true;
        };
        for(unsigned aa=0;aa<4;++aa)for(unsigned scaler=0;scaler<6;++scaler){
            if(!aa && scaler<2)continue;
            const auto expected=draw(aa,scaler),first=draw(aa,scaler,100),repeat=draw(aa,scaler,100);
            require(equivalent(first,expected) && equivalent(repeat,expected),"Cached filtering changed image orientation or colors");
            std::vector<Color> edited(32u*32u,MAGENTA);
            for(unsigned y=0;y<16;++y)for(unsigned x=0;x<16;++x)edited[y*32u+x]=GREEN;
            UpdateTexture(source,edited.data());
            require(equivalent(draw(aa,scaler,100),expected),"Unchanged completion sequence repeated expensive filters");
            const auto advanced=draw(aa,scaler,101),green=draw(aa,scaler);
            require(equivalent(advanced,green) && !equivalent(advanced,expected),"Completed frame did not invalidate filtered output");
            for(unsigned y=0;y<16;++y)for(unsigned x=0;x<16;++x)edited[y*32u+x]=x>y ? WHITE : BLACK;
            UpdateTexture(source,edited.data());
        }
        draw(1,3,102);outputRect={8,8,64,40};
        const auto resized=draw(1,3,102),resizedReference=draw(1,3);
        require(equivalent(resized,resizedReference),"Resizing reused an output with old dimensions");
        draw(1,3,102);activeSource={0,0,8,8};
        const auto cropped=draw(1,3,102),croppedReference=draw(1,3);
        require(equivalent(cropped,croppedReference),"Active source changes reused an old filtered crop");
        {
            std::vector<Color> checker(32u*32u,MAGENTA);
            for(unsigned y=0;y<16;++y)for(unsigned x=0;x<16;++x)checker[y*32u+x]=((x^y)&1u) ? WHITE : BLACK;
            UpdateTexture(source,checker.data());
            outputRect={16,8,4,4};
            for(unsigned scaler=1;scaler<6;++scaler){
                const auto reduced=draw(0,scaler);
                for(unsigned y=9;y<11;++y)for(unsigned x=17;x<19;++x){
                    const auto c=reduced[y*80u+x];
                    require(c.r>80 && c.r<176 && c.g>80 && c.g<176 && c.b>80 && c.b<176,
                            ("Display filter "+std::to_string(scaler)+" aliased a larger frame instead of averaging it").c_str());
                }
            }
            for(unsigned y=0;y<16;++y)for(unsigned x=0;x<16;++x)checker[y*32u+x]=x>y ? WHITE : BLACK;
            UpdateTexture(source,checker.data());
        }
        activeSource={0,0,16,16};outputRect={16,8,48,48};
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
        require(edgeCount(2) > edgeCount(0), "FXAA High did not smooth the diagonal edge");
        require(edgeCount(3) > edgeCount(0), "FXAA Ultra did not smooth the diagonal edge");
        filter.close();
        UnloadTexture(source);
        CloseWindow();
        set_graphics_settings(saved);
        std::cout << "Display filter comparisons passed: sharpen, vibrant colors and CRT scanline effects, all twenty-four AA/scaler combinations (FXAA, FXAA High and FXAA Ultra) (including FSR 1 EASU/RCAS, Lanczos and sharp pixels), content "
                     "bounds, orientation, letterboxing, downscaling of larger frames, native-size edge smoothing and completion-aware filter reuse.\n";
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
}
