#include <iostream>
#include <string>
#include <vector>

#include "cli_commands.h"

int main(int argc, char** argv) {
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
    return lpc::cli::runCli(args, std::cout, std::cerr);
}
