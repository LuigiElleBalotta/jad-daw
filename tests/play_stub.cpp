#ifdef LPC_HAVE_CLI_LIB

#include <ostream>

#include "cli_commands.h"

namespace lpc::cli {

int runPlay(const std::vector<std::string>&, std::ostream&, std::ostream& err) {
    err << "error: audio output is not available in the test build\n";
    return 2;
}

}  // namespace lpc::cli

#endif
