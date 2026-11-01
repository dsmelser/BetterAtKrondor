/*
 * Crash reports on Windows: the exception, the faulting address and the call stack,
 * symbolized from the PDB, printed to stderr before the process dies. Bring-up of the
 * reconstructed code meets plenty of these (pointers the 16-bit original got away with).
 */
#ifdef _WIN32
#include <stdio.h>

#include <windows.h>
#include <dbghelp.h>

#pragma comment(lib, "dbghelp.lib")

static void print_frame(HANDLE proc, DWORD64 addr) {
    char buf[sizeof(SYMBOL_INFO) + 256];
    SYMBOL_INFO *sym = (SYMBOL_INFO *)buf;
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen = 255;
    DWORD64 disp = 0;
    IMAGEHLP_LINE64 line = {sizeof line};
    DWORD lineDisp = 0;
    if (SymFromAddr(proc, addr, &disp, sym)) {
        if (SymGetLineFromAddr64(proc, addr, &lineDisp, &line))
            fprintf(stderr, "  %s+0x%llx  (%s:%lu)\n", sym->Name, (unsigned long long)disp,
                    line.FileName, line.LineNumber);
        else
            fprintf(stderr, "  %s+0x%llx\n", sym->Name, (unsigned long long)disp);
    } else {
        fprintf(stderr, "  0x%08llx\n", (unsigned long long)addr);
    }
}

static LONG WINAPI on_crash(EXCEPTION_POINTERS *ep) {
    HANDLE proc = GetCurrentProcess();
    EXCEPTION_RECORD *er = ep->ExceptionRecord;
    fprintf(stderr, "\nCrash: exception 0x%08lX at %p", er->ExceptionCode, er->ExceptionAddress);
    if (er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && er->NumberParameters >= 2)
        fprintf(stderr, " (%s address %p)", er->ExceptionInformation[0] ? "writing" : "reading",
                (void *)er->ExceptionInformation[1]);
    fprintf(stderr, "\n");

    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(proc, NULL, TRUE);
    CONTEXT ctx = *ep->ContextRecord;
    STACKFRAME64 frame = {0};
#ifdef _M_IX86
    DWORD machine = IMAGE_FILE_MACHINE_I386;
    frame.AddrPC.Offset = ctx.Eip;
    frame.AddrFrame.Offset = ctx.Ebp;
    frame.AddrStack.Offset = ctx.Esp;
#else
    DWORD machine = IMAGE_FILE_MACHINE_AMD64;
    frame.AddrPC.Offset = ctx.Rip;
    frame.AddrFrame.Offset = ctx.Rbp;
    frame.AddrStack.Offset = ctx.Rsp;
#endif
    frame.AddrPC.Mode = frame.AddrFrame.Mode = frame.AddrStack.Mode = AddrModeFlat;
    /* A call through a bad function pointer stops outside every module, where the walk cannot
       start: start it from the return address the call pushed. */
    void *sp = (void *)(uintptr_t)frame.AddrStack.Offset;
    if (SymGetModuleBase64(proc, frame.AddrPC.Offset) == 0 && !IsBadReadPtr(sp, sizeof(uintptr_t))) {
        uintptr_t ret = *(uintptr_t *)sp;
        fprintf(stderr, "  (no code there: called from 0x%08llx)\n", (unsigned long long)ret);
        frame.AddrPC.Offset = ret;
        frame.AddrStack.Offset += sizeof(uintptr_t);
#ifdef _M_IX86
        ctx.Eip = (DWORD)ret;
        ctx.Esp = (DWORD)frame.AddrStack.Offset;
#else
        ctx.Rip = ret;
        ctx.Rsp = frame.AddrStack.Offset;
#endif
    }
    for (int i = 0; i < 32; i++) {
        if (!StackWalk64(machine, proc, GetCurrentThread(), &frame, &ctx, NULL,
                         SymFunctionTableAccess64, SymGetModuleBase64, NULL))
            break;
        if (frame.AddrPC.Offset == 0)
            break;
        print_frame(proc, frame.AddrPC.Offset);
    }
    fflush(stderr);
    return EXCEPTION_EXECUTE_HANDLER;
}

void crash_reporter_install(void) {
    SetUnhandledExceptionFilter(on_crash);
}

void crash_print_stack(void) {
    void *frames[32];
    HANDLE proc = GetCurrentProcess();
    USHORT n = CaptureStackBackTrace(1, 32, frames, NULL);
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(proc, NULL, TRUE);
    for (USHORT i = 0; i < n; i++)
        print_frame(proc, (DWORD64)(uintptr_t)frames[i] - 1);
    fflush(stderr);
}
#else
void crash_reporter_install(void) {
}

void crash_print_stack(void) {
}
#endif
