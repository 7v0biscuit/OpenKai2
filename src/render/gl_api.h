// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#ifdef _WIN32
#define OPENKAI_GL_CALL __stdcall
#else
#define OPENKAI_GL_CALL
#endif
namespace openkai2::gpu {
using Enum=unsigned;using UInt=unsigned;using Int=int;using Size=std::ptrdiff_t;
#define OPENKAI_GL_FUNCTIONS(X) \
 X(const unsigned char*,GetString,(Enum)) \
 X(void,GetIntegerv,(Enum,Int*)) \
 X(Enum,GetError,()) \
 X(UInt,CreateShader,(Enum)) \
 X(void,ShaderSource,(UInt,Int,const char*const*,const Int*)) \
 X(void,CompileShader,(UInt)) \
 X(void,GetShaderiv,(UInt,Enum,Int*)) \
 X(void,GetShaderInfoLog,(UInt,Int,Int*,char*)) \
 X(void,DeleteShader,(UInt)) \
 X(UInt,CreateProgram,()) \
 X(void,AttachShader,(UInt,UInt)) \
 X(void,LinkProgram,(UInt)) \
 X(void,GetProgramiv,(UInt,Enum,Int*)) \
 X(void,GetProgramInfoLog,(UInt,Int,Int*,char*)) \
 X(void,DeleteProgram,(UInt)) \
 X(void,UseProgram,(UInt)) \
 X(void,GenBuffers,(Int,UInt*)) \
 X(void,DeleteBuffers,(Int,const UInt*)) \
 X(void,BindBuffer,(Enum,UInt)) \
 X(void,BufferData,(Enum,Size,const void*,Enum)) \
 X(void,BufferSubData,(Enum,Size,Size,const void*)) \
 X(void,GetBufferSubData,(Enum,Size,Size,void*)) \
 X(void,BindBufferBase,(Enum,UInt,UInt)) \
 X(void,DispatchCompute,(UInt,UInt,UInt)) \
 X(void,MemoryBarrier,(Enum)) \
 X(void,Finish,())
struct Api {
#define DECLARE(result,name,args) using name##Proc=result(OPENKAI_GL_CALL*)args;name##Proc name=nullptr;
 OPENKAI_GL_FUNCTIONS(DECLARE)
#undef DECLARE
 void load(const std::function<void*(const char*)>& resolver) {
#define LOAD(result,name,args) name=reinterpret_cast<name##Proc>(resolver("gl" #name));if(!name)throw std::runtime_error("OpenGL function unavailable: gl" #name);
 OPENKAI_GL_FUNCTIONS(LOAD)
#undef LOAD
 }
 void check(const char* operation) {const auto error=GetError();if(error)throw std::runtime_error(std::string(operation)+" failed, GL error "+std::to_string(error));}
};
#undef OPENKAI_GL_FUNCTIONS
inline constexpr Enum ShaderStorageBuffer=0x90d2,DynamicDraw=0x88e8,ComputeShader=0x91b9;
inline constexpr Enum ShaderStorageBarrier=0x2000,BufferUpdateBarrier=0x200;
}
#undef OPENKAI_GL_CALL
