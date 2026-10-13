#include "engine/platform/os.h"
#include "engine/core/crash_report.h"

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <initializer_list>

#include <SDL3/SDL.h>

#include "engine/core/log.h"

#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#else
#include <execinfo.h>
#include <signal.h>
#include <unistd.h>
#endif

namespace pt {
namespace {

std::filesystem::path g_dump_dir;
std::string g_build = "dev";
std::atomic<bool> g_exit_logged{false};
std::atomic<int> g_dumping{0};

#ifdef _WIN32
LONG WINAPI OnUnhandledException(EXCEPTION_POINTERS* info) {
    char reason[96];
    std::snprintf(reason, sizeof(reason), "unhandled exception %#lx at %p", info->ExceptionRecord->ExceptionCode,
                  info->ExceptionRecord->ExceptionAddress);
    WriteCrashDump(reason, info);
    LogExit(static_cast<int>(info->ExceptionRecord->ExceptionCode), "crash");
    return EXCEPTION_CONTINUE_SEARCH;
}

void OnPureCall() {
    WriteCrashDump("pure virtual function call");
    LogExit(3, "pure virtual call");
    std::_Exit(3);
}

void OnInvalidParameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t) {
    WriteCrashDump("invalid parameter passed to a C runtime function");
    LogExit(3, "invalid parameter");
    std::_Exit(3);
}

BOOL WINAPI OnConsoleControl(DWORD event) {
    const char* names[] = {"Ctrl+C", "Ctrl+Break", "console close", "?", "?", "logoff", "shutdown"};
    LogWarn("exit: console control event {} ({})", event, event < 7 ? names[event] : "?");
    LogExit(static_cast<int>(0xC000013A), "console control event");
    return FALSE;
}

bool SDLCALL OnWindowsMessage(void*, MSG* msg) {
    if (msg->message == WM_QUERYENDSESSION) {
        LogWarn("exit: Windows session ending (WM_QUERYENDSESSION)");
    } else if (msg->message == WM_ENDSESSION && msg->wParam) {
        LogExit(0, "Windows session end");
    }
    return true;
}
#endif

#ifdef _WIN32
void OnSignalAbort(int) {
    WriteCrashDump("abort()");
    LogExit(3, "abort");
}
#else
// No minidump outside Windows (docs/macos.md and docs/linux.md, crash dump): a fault or an abort leaves its signal and a
// backtrace in pt.log instead. Everything goes straight to the log's descriptor (and stderr) with write and
// backtrace_symbols_fd, never through LogWrite: its mutex may be held by the thread that faulted, and waiting for it would
// freeze the game instead of ending it. The lines carry no timestamp for that reason. The handler is one-shot (SA_RESETHAND)
// and raises the signal again, so the process still dies of it as before: the exit status is the signal's and the system
// writes its own report (macOS: ~/Library/Logs/DiagnosticReports). No alternate stack: a stack overflow dies without a
// backtrace (sigaltstack and SA_ONSTACK when one shows)
void OnFatalSignal(int sig, siginfo_t* info, void*) {
    const char* name = sig == SIGSEGV ? "SIGSEGV" : sig == SIGBUS ? "SIGBUS" : sig == SIGILL ? "SIGILL" : sig == SIGFPE ? "SIGFPE" : "SIGABRT";
    const int log = LogFileDescriptor();
    const int fd = log >= 0 ? log : 2;
    char line[128];
    const auto put = [&](int length) {
        if (length > 0) (void)write(fd, line, static_cast<size_t>(length));
    };
    put(std::snprintf(line, sizeof(line), "crash: %s at %p, backtrace:\n", name, info->si_addr));
    void* frames[64];
    backtrace_symbols_fd(frames, backtrace(frames, 64), fd);
    put(std::snprintf(line, sizeof(line), "exit: code %d (crash)\n", 128 + sig));
    raise(sig);
}
#endif

void OnAtExit() {
    LogExit(-1, "exit without a recorded code");
}

}

void InstallCrashReporting(const std::filesystem::path& dump_dir, const std::string& build) {
    g_dump_dir = dump_dir;
    g_build = build.empty() ? "dev" : build;
#ifdef _WIN32
    SetUnhandledExceptionFilter(OnUnhandledException);
    _set_purecall_handler(OnPureCall);
    _set_invalid_parameter_handler(OnInvalidParameter);
    SetConsoleCtrlHandler(OnConsoleControl, TRUE);
    SDL_SetWindowsMessageHook(OnWindowsMessage, nullptr);
    std::signal(SIGABRT, OnSignalAbort);
#else
    struct sigaction fatal{};
    fatal.sa_sigaction = OnFatalSignal;
    fatal.sa_flags = SA_SIGINFO | SA_RESETHAND;
    for (const int sig : {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT}) sigaction(sig, &fatal, nullptr);
    // glibc's first backtrace() loads libgcc_s (dlopen, malloc): done here, not in the handler
    void* warm[1];
    backtrace(warm, 1);
#endif
    std::atexit(OnAtExit);
    std::set_terminate([] {
        if (const std::exception_ptr error = std::current_exception()) {
            try {
                std::rethrow_exception(error);
            } catch (const std::exception& e) {
                LogError("terminate: uncaught exception: {}", e.what());
            } catch (...) {
                LogError("terminate: uncaught exception of an unknown type");
            }
        } else {
            LogError("terminate: called with no active exception");
        }
#ifdef _WIN32
        WriteCrashDump("std::terminate");
        LogExit(3, "terminate");
        std::_Exit(3);
#else
        // the abort is the signal handler's: backtrace, exit line, the system's own report
        std::abort();
#endif
    });
#ifdef _WIN32
    LogInfo("crash reporting: build {}, dumps to {}", g_build, g_dump_dir.empty() ? std::string(".") : pt::os::PathToUtf8(g_dump_dir));
#else
    LogInfo("crash reporting: build {}, a backtrace in the log on a fault or abort", g_build);
#endif
}

std::filesystem::path WriteCrashDump(const char* reason, void* exception_pointers) {
    // one dump per process: a second fault while writing (or a handler chain) only logs
    if (g_dumping.fetch_add(1) != 0) {
        LogError("crash: {} (dump already written)", reason);
        return {};
    }
    std::filesystem::path path;
#ifdef _WIN32
    SYSTEMTIME t;
    GetLocalTime(&t);
    char name[96];
    std::snprintf(name, sizeof(name), "pt-crash-%s-%04u%02u%02u-%02u%02u%02u.dmp", g_build.c_str(), t.wYear, t.wMonth, t.wDay, t.wHour,
                  t.wMinute, t.wSecond);
    path = g_dump_dir / name;
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION exception{GetCurrentThreadId(), static_cast<EXCEPTION_POINTERS*>(exception_pointers), FALSE};
        const auto type = static_cast<MINIDUMP_TYPE>(MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithThreadInfo |
                                                     MiniDumpWithUnloadedModules);
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, type, exception_pointers ? &exception : nullptr, nullptr,
                          nullptr);
        CloseHandle(file);
    } else {
        path.clear();
    }
#endif
    LogError("crash: {}, dump {}", reason, path.empty() ? std::string("not written") : pt::os::PathToUtf8(path));
    return path;
}

void FatalError(const std::string& reason, bool show_message, int code) {
    LogError("fatal: {}", reason);
    const std::filesystem::path dump = WriteCrashDump(reason.c_str());
    if (show_message) {
        // a dump only where one was written (Windows); the dumps go next to the log
        const std::string text = "P.T. stopped: " + reason + "\n\nThe log" + (dump.empty() ? "" : " and a crash dump") + " were written to:\n" +
                                 pt::os::PathToUtf8(g_dump_dir) + "\n\nPlease send pt.log" + (dump.empty() ? "" : " and the .dmp file") +
                                 " with your report.";
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "P.T.", text.c_str(), nullptr);
    }
    LogExit(code, "fatal error");
    std::_Exit(code);
}

void LogExit(int code, const char* how) {
    if (g_exit_logged.exchange(true)) {
        return;
    }
    LogInfo("exit: code {} ({})", code, how);
}

}
