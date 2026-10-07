#include <catch2/catch_test_macros.hpp>
#include "lpc/commands.h"
#include "lpc/undo_stack.h"
#include "lpc/model_json.h"
#include "random_commands.h"

using namespace lpc;

TEST_CASE("undo: random command sequences undo and redo exactly", "[undo][property]") {
    for (std::uint64_t seed = 1; seed <= 25; ++seed) {
        CAPTURE(seed);
        std::mt19937_64 rng(seed);
        Project p(Uuid::random(rng));
        std::vector<Project> snapshots{p};  // snapshots[i] = state after i accepted commands
        std::vector<nlohmann::json> log;
        UndoStack stack;
        int rejected = 0;

        for (int i = 0; i < 250; ++i) {
            CommandPtr c = test::randomCommand(p, rng);
            if (!c) continue;
            const nlohmann::json j = c->toJson();
            const Project before = p;
            const auto err = stack.execute(p, std::move(c));
            if (err) {
                ++rejected;
                REQUIRE(p == before);  // a rejected command changes nothing
            } else {
                log.push_back(j);
                snapshots.push_back(p);
            }
        }
        REQUIRE(snapshots.size() > 30);  // the generator must produce mostly valid commands
        const Project finalState = p;

        // undo step by step: every intermediate state matches the recorded snapshot
        for (std::size_t i = snapshots.size() - 1; i > 0; --i) {
            CAPTURE(i, log[i - 1].dump());  // the command being undone
            REQUIRE_FALSE(stack.undo(p).has_value());
            CAPTURE(toJson(p).dump(), toJson(snapshots[i - 1]).dump());
            REQUIRE(p == snapshots[i - 1]);
        }
        REQUIRE_FALSE(stack.canUndo());

        // and redo walks forward through the same states
        for (std::size_t i = 1; i < snapshots.size(); ++i) {
            REQUIRE_FALSE(stack.redo(p).has_value());
            REQUIRE(p == snapshots[i]);
        }

        // replaying the serialized commands on a fresh project reproduces the final state
        Project replay(finalState.master()->id);
        for (const nlohmann::json& j : log) REQUIRE(commandFromJson(j)->apply(replay).ok());
        REQUIRE(replay == finalState);
    }
}
