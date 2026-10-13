// Thin launcher — the exe a user actually runs, in the top-level folder next
// to resources.dat/data.bin. The real app (ConUIApp.exe) and its runtime
// DLLs live in bin/ (see CMakeLists.txt's app-target/launcher sections for
// why: Windows resolves an exe's directly-linked DLLs from its own directory
// before main() ever runs, so nothing ConUIApp.exe's own code could do at
// startup would redirect that search to bin/ retroactively). Only a separate
// process that starts it can — which is all this file does: find
// ConUIApp.exe in bin/, tell it (via CONUI_APP_ROOT, read in
// Core/Utils.cpp's GetExeDirectory()) which top-level folder to treat as
// home, launch it, and exit — the visible window belongs to ConUIApp.exe
// from that point on, same as if it had been launched directly.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <vector>

int main() {
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    std::string exe_path(path);
    size_t last = exe_path.find_last_of("\\/");
    std::string root_dir = (last != std::string::npos) ? exe_path.substr(0, last) : ".";

    SetEnvironmentVariableA("CONUI_APP_ROOT", root_dir.c_str());

    std::string child_exe = root_dir + "\\bin\\ConUIApp.exe";
    std::string cmdline   = "\"" + child_exe + "\"";

    std::vector<char> cmdline_buf(cmdline.begin(), cmdline.end());
    cmdline_buf.push_back('\0');

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    BOOL ok = CreateProcessA(
        child_exe.c_str(),
        cmdline_buf.data(),
        nullptr, nullptr, FALSE,
        0,
        nullptr, // inherit this process's environment (CONUI_APP_ROOT included, just set above)
        root_dir.c_str(),
        &si, &pi);

    if (!ok) {
        std::string msg = "Could not start ConUIApp.exe in bin\\ (error " + std::to_string(GetLastError()) + ").";
        MessageBoxA(nullptr, msg.c_str(), "ConUI", MB_ICONERROR);
        return 1;
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 0;
}
