#include <wx/wx.h>
#include "app_frame.h"
#include "logger.h"
#include <unistd.h>
#include <fcntl.h>
#include <spawn.h>
#include <signal.h>
#include <execinfo.h>
#include <cstdio>
#include <cstring>

extern char **environ;

static void crash_handler(int sig) {
    void*  frames[64];
    int    n    = backtrace(frames, 64);
    char** syms = backtrace_symbols(frames, n);
    std::string trace = "=== CRASH  signal=" + std::to_string(sig) + " ===\n";
    for (int i = 0; i < n; ++i)
        if (syms) trace += std::string(syms[i]) + "\n";
    free(syms);
    Logger::get().log(trace);
    signal(sig, SIG_DFL);
    raise(sig);
}

class TestTakerApp : public wxApp {
public:
    bool OnInit() override;
    bool OnExceptionInMainLoop() override;
};

wxIMPLEMENT_APP_NO_MAIN(TestTakerApp);

int main(int argc, char* argv[]) {
    // When launched from a terminal, re-exec self as a detached process so
    // WKWebView's rendering pipeline initialises correctly.
    if (isatty(STDIN_FILENO)) {
        posix_spawn_file_actions_t fa;
        posix_spawn_file_actions_init(&fa);
        posix_spawn_file_actions_addopen(&fa, STDIN_FILENO,  "/dev/null", O_RDONLY, 0);
        posix_spawn_file_actions_addopen(&fa, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
        posix_spawn_file_actions_addopen(&fa, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
        pid_t child;
        posix_spawnp(&child, argv[0], &fa, nullptr, argv, environ);
        posix_spawn_file_actions_destroy(&fa);
        return 0;
    }

    signal(SIGSEGV, crash_handler);
    signal(SIGABRT, crash_handler);
    signal(SIGBUS,  crash_handler);

    char cwdbuf[4096] = {};
    std::string cwd = getcwd(cwdbuf, sizeof(cwdbuf)) ? cwdbuf : "?";
    Logger::get().log("=== test-taker startup  pid=" + std::to_string(getpid())
                      + "  cwd=" + cwd);

    return wxEntry(argc, argv);
}

bool TestTakerApp::OnExceptionInMainLoop() {
    try { throw; }
    catch (const std::exception& e) {
        Logger::get().log(std::string("=== EXCEPTION: ") + e.what() + " ===");
    }
    catch (...) {
        Logger::get().log("=== UNKNOWN EXCEPTION in main loop ===");
    }
    return false;
}

bool TestTakerApp::OnInit() {
    AppFrame* frame = new AppFrame();
    frame->Show(true);
    return true;
}
