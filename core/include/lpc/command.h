#pragma once
#include <memory>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "lpc/model.h"

namespace lpc {

struct CommandError {
    std::string code;     // stable, machine-readable (for clients and AIs)
    std::string message;  // human-readable
};

class Command;
using CommandPtr = std::unique_ptr<Command>;

struct ApplyResult {
    CommandPtr inverse;                 // set on success
    std::optional<CommandError> error;  // set on failure; the project is then unchanged
    bool ok() const { return !error.has_value(); }
};

class Command {
public:
    virtual ~Command() = default;
    virtual std::string type() const = 0;
    virtual nlohmann::json toJson() const = 0;
    virtual ApplyResult apply(Project& project) const = 0;  // all-or-nothing
};

}  // namespace lpc
