#pragma once
#include <iosfwd>
#include <string>
#include <vector>

#include "lpc/model.h"

namespace lpc::cli {

// snapshot, analyze, masking and apply: the commands that print JSON for a program that looks at a project and listens to it.
bool isEarsCommand(const std::string& cmd);
// The project with only the track named (or with the id) `selector` heard; throws std::runtime_error when there is no such track.
lpc::Project soloTrackProject(lpc::Project project, const std::string& selector);
int runEars(const std::vector<std::string>& args, std::ostream& out, std::ostream& err);

}  // namespace lpc::cli
