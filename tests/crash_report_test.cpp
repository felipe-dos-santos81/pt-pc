// The POSIX crash handler (src/engine/core/crash_report.cpp): a segfault leaves its name and a backtrace in pt.log, and the
// process still dies of the signal, so the system's own report and the exit status are as without the handler. A forked
// child faults; the parent reads what it left.
#include <sys/wait.h>
#include <unistd.h>

#include <csignal>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>

#include "engine/core/crash_report.h"
#include "engine/core/log.h"

int main() {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / ("pt_crash_report_test_" + std::to_string(getpid()));
    std::filesystem::create_directories(dir);
    const std::filesystem::path log = dir / "pt.log";
    const pid_t child = fork();
    if (child == 0) {
        pt::LogSetFile(log);
        pt::InstallCrashReporting(dir, "test");
        volatile int* nowhere = nullptr;
        *nowhere = 1;
        _exit(0);
    }
    int status = 0;
    waitpid(child, &status, 0);
    int failures = 0;
    if (!WIFSIGNALED(status) || WTERMSIG(status) != SIGSEGV) {
        std::printf("FAIL: the child did not die of SIGSEGV (status %#x)\n", status);
        ++failures;
    }
    std::ifstream in(log);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (text.find("crash: SIGSEGV") == std::string::npos) {
        std::printf("FAIL: the log does not name the signal\n");
        ++failures;
    }
    // a frame from backtrace_symbols_fd names the executable and an address on one line (the dump folder line has no address)
    bool frame = false;
    std::istringstream lines(text);
    for (std::string line; std::getline(lines, line);) {
        frame = frame || (line.find("crash_report_test") != std::string::npos && line.find("0x") != std::string::npos);
    }
    if (!frame) {
        std::printf("FAIL: the log holds no backtrace frame\n");
        ++failures;
    }
    if (text.find("exit: code 139 (crash)") == std::string::npos) {
        std::printf("FAIL: the log has no exit line for the crash\n");
        ++failures;
    }
    if (failures) std::printf("--- pt.log ---\n%s--- end ---\n", text.c_str());
    std::filesystem::remove_all(dir);
    std::printf("%d failures\n", failures);
    return failures ? 1 : 0;
}
