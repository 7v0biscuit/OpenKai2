// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <windows.h>
#include <dbghelp.h>
#include <cstdio>

// Preserve the failing callback's stack in CI, where Windows normally reports
// only STATUS_FATAL_USER_CALLBACK_EXCEPTION to CTest.
inline LONG CALLBACK trace_test_crash(EXCEPTION_POINTERS* failure) {
    const DWORD code=failure->ExceptionRecord->ExceptionCode;
    if((code&0xf0000000u)!=0xc0000000u)return EXCEPTION_CONTINUE_SEARCH;
    static thread_local bool tracing=false;
    if(tracing)return EXCEPTION_CONTINUE_SEARCH;
    tracing=true;
    std::fprintf(stderr,"[ui:fault] code=0x%08lx address=%p\n",code,failure->ExceptionRecord->ExceptionAddress);
    CONTEXT context=*failure->ContextRecord;
    STACKFRAME64 frame{};
    frame.AddrPC.Offset=context.Rip;frame.AddrPC.Mode=AddrModeFlat;
    frame.AddrStack.Offset=context.Rsp;frame.AddrStack.Mode=AddrModeFlat;
    frame.AddrFrame.Offset=context.Rbp;frame.AddrFrame.Mode=AddrModeFlat;
    const HANDLE process=GetCurrentProcess(),thread=GetCurrentThread();
    for(unsigned i=0;i<24 && frame.AddrPC.Offset;++i) {
        alignas(SYMBOL_INFO) char storage[sizeof(SYMBOL_INFO)+MAX_SYM_NAME]{};
        auto* symbol=reinterpret_cast<SYMBOL_INFO*>(storage);symbol->SizeOfStruct=sizeof(SYMBOL_INFO);symbol->MaxNameLen=MAX_SYM_NAME;
        DWORD64 displacement=0;
        if(SymFromAddr(process,frame.AddrPC.Offset,&displacement,symbol))
            std::fprintf(stderr,"[ui:stack] %s+0x%llx",symbol->Name,static_cast<unsigned long long>(displacement));
        else std::fprintf(stderr,"[ui:stack] 0x%llx",static_cast<unsigned long long>(frame.AddrPC.Offset));
        IMAGEHLP_LINE64 line{};line.SizeOfStruct=sizeof(line);DWORD offset=0;
        if(SymGetLineFromAddr64(process,frame.AddrPC.Offset,&offset,&line))std::fprintf(stderr," %s:%lu",line.FileName,line.LineNumber);
        std::fputc('\n',stderr);
        if(!StackWalk64(IMAGE_FILE_MACHINE_AMD64,process,thread,&frame,&context,nullptr,SymFunctionTableAccess64,SymGetModuleBase64,nullptr))break;
    }
    std::fflush(stderr);tracing=false;
    return EXCEPTION_CONTINUE_SEARCH;
}
inline void enable_test_crash_trace() {
    SymSetOptions(SYMOPT_LOAD_LINES|SYMOPT_UNDNAME|SYMOPT_DEFERRED_LOADS);
    SymInitialize(GetCurrentProcess(),nullptr,TRUE);
    AddVectoredExceptionHandler(1,trace_test_crash);
}
