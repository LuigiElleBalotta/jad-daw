#pragma once

namespace lpc::test::rt {

void enter();
void leave();
long violations();  // allocations seen on threads that were inside a Scope
void reset();

struct Scope {
    Scope() { enter(); }
    ~Scope() { leave(); }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
};

}  // namespace lpc::test::rt
