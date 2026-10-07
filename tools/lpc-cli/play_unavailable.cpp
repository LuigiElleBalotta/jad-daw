#include <ostream>

#include "cli_commands.h"

namespace lpc::cli {

int runPlay(const std::vector<std::string>&, std::ostream&, std::ostream& err) {
    err << "error: this build has no audio device support (configure with -DLPC_WITH_JUCE=ON)\n";
    return 2;
}

}  // namespace lpc::cli
