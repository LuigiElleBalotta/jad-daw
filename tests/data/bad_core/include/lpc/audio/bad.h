#pragma once
#include <mutex>

// Fixture for the lock check: this file must be reported as a violation.
struct Bad {
    std::mutex m;
};
