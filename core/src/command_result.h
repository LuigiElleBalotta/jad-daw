#pragma once
#include <string>
#include <utility>

#include "lpc/command.h"

namespace lpc::detail {

inline ApplyResult fail(CommandError e) {
    ApplyResult r;
    r.error = std::move(e);
    return r;
}
inline ApplyResult fail(std::string code, std::string message) { return fail(CommandError{std::move(code), std::move(message)}); }
inline ApplyResult success(CommandPtr inverse) {
    ApplyResult r;
    r.inverse = std::move(inverse);
    return r;
}

}  // namespace lpc::detail
