#pragma once
#include <functional>
#include <iosfwd>
#include <memory>
#include <string>
#include <vector>

#include "lpc/plugin_host.h"

namespace lpc::cli {

// Supplies the plug-in host used by `render`. Set by builds that can host plug-ins; unset otherwise.
using PluginHostFactory = std::function<std::shared_ptr<lpc::IPluginHost>()>;
void setPluginHostFactory(PluginHostFactory factory);

// args exclude the program name. Returns the process exit code: 0 ok, 1 usage error, 2 runtime error.
int runCli(const std::vector<std::string>& args, std::ostream& out, std::ostream& err);

#ifdef _WIN32
// Converts a UTF-16 string (the Windows command line) to UTF-8.
std::string wideToUtf8(const wchar_t* wide);
#endif

// Implemented by play_command.cpp (with audio device) or play_unavailable.cpp (without).
int runPlay(const std::vector<std::string>& args, std::ostream& out, std::ostream& err);

}  // namespace lpc::cli
