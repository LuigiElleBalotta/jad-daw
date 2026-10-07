#pragma once
#include <iosfwd>
#include <string>
#include <vector>

namespace lpc::cli {

// args exclude the program name. Returns the process exit code: 0 ok, 1 usage error, 2 runtime error.
int runCli(const std::vector<std::string>& args, std::ostream& out, std::ostream& err);

#ifdef _WIN32
// Converts a UTF-16 string (the Windows command line) to UTF-8.
std::string wideToUtf8(const wchar_t* wide);
#endif

// Implemented by play_command.cpp (with audio device) or play_unavailable.cpp (without).
int runPlay(const std::vector<std::string>& args, std::ostream& out, std::ostream& err);

}  // namespace lpc::cli
