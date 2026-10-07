#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#endif

#include "cli_commands.h"

int main(int argc, char** argv) {
    std::vector<std::string> args;
#ifdef _WIN32
    // argv is in the system code page; the command line itself is UTF-16, so paths with any character work.
    (void)argc;
    (void)argv;
    SetConsoleOutputCP(CP_UTF8);
    int wideCount = 0;
    wchar_t** wide = CommandLineToArgvW(GetCommandLineW(), &wideCount);
    if (wide) {
        for (int i = 1; i < wideCount; ++i) args.push_back(lpc::cli::wideToUtf8(wide[i]));
        LocalFree(wide);
    }
#else
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
#endif
    return lpc::cli::runCli(args, std::cout, std::cerr);
}
