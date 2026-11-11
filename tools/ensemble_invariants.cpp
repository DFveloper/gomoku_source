// Checks the two invariants the expert mixture claims, directly, on constructed
// positions. These are the properties that make delegation shippable, so they are
// tested as assertions rather than inferred from match results.
//
//   1. Degradation: with no expert binary available the mixture returns exactly
//      the move the local engine returns.
//   2. Proof supremacy: a position where the side to move has a continuous-four
//      win must produce a forcing move, whether or not an expert is consulted.
//
//   ensemble_invariants [EXPERT_BINARY]

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "engine_adapter.h"
#include "engine_ensemble.h"

using namespace gomoku;

namespace {

constexpr int kSize = 15;
int failures = 0;

void fill(int board[kMaxBoard][kMaxBoard], const std::vector<Move>& moves) {
    std::memset(board, 0, sizeof(int) * kMaxBoard * kMaxBoard);
    for (size_t i = 0; i < moves.size(); ++i) board[moves[i].r][moves[i].c] = i % 2 == 0 ? 1 : 2;
}

void check(bool condition, const char* what) {
    std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", what);
    if (!condition) ++failures;
}

// Position from the analysed Rapfi loss: White is to move and Rapfi's open four
// cannot be blocked, so the only sound answer starts a forcing line of our own.
const std::vector<Move> kForcedPosition = {
    {7, 7}, {7, 8}, {8, 7}, {6, 7}, {8, 9}, {5, 8}, {8, 8}, {4, 9}};

}  // namespace

int main(int argc, char** argv) {
    const std::filesystem::path root =
#ifdef GOMOKU_SOURCE_ROOT
        std::filesystem::path(GOMOKU_SOURCE_ROOT);
#else
        std::filesystem::current_path();
#endif
    ;
    const std::filesystem::path runtime = root / "third_party" / "runtime";

    // --- 1. Degradation -----------------------------------------------------
    // Both a tactical position (answered before any search) and a quiet one (the
    // search decides) must come out identical to the local engine alone.
    {
        const std::vector<std::vector<Move>> cases = {
            {{7, 7}, {7, 8}, {8, 7}, {6, 7}, {8, 9}, {5, 8}, {8, 8}, {4, 9}, {3, 8}, {7, 6}},
            {{7, 7}, {8, 8}, {6, 6}, {8, 6}},
        };
        bool searched_case_seen = false;
        for (const std::vector<Move>& moves : cases) {
            int board[kMaxBoard][kMaxBoard];
            fill(board, moves);
            const int player = moves.size() % 2 == 0 ? 1 : 2;
            OpponentStyle style{};
            const Move local = choose_ai_move(board, kSize, player, 1, &style, 1,
                                              moves.back());

            EnsembleConfig config;  // Verified by default
            // Point the primary expert at a path that cannot exist, and give the
            // council a member that cannot exist either: this is the "runtime is
            // missing" case a packaged build hits on an unsupported host.
            config.expert_program = "/nonexistent/pbrain-rapfi";
            config.extra_experts.push_back({EngineVersion::AlphaGomoku2026,
                                            "/nonexistent/pbrain-agomoku"});
            const EnsembleStats result =
                choose_ensemble_move(board, kSize, moves, player, 1, runtime, config);
            std::printf("      plies=%zu local=(%d,%d) mixture=(%d,%d) source=%s total=%dms\n",
                        moves.size(), local.r, local.c, result.move.r, result.move.c,
                        ensemble_source_name(result.source), result.total_ms);
            check(local.r == result.move.r && local.c == result.move.c,
                  "missing experts degrade to the local engine's exact move");
            check(!result.expert_available, "missing experts are reported as unavailable");
            if (result.source == EnsembleSource::LocalSearch) searched_case_seen = true;
        }
        check(searched_case_seen, "at least one case was decided by the search, not tactics");
    }

    // --- 2. Proof supremacy -------------------------------------------------
    {
        int board[kMaxBoard][kMaxBoard];
        fill(board, kForcedPosition);
        const int player = 1;

        EnsembleConfig config;
        config.expert_program = "/nonexistent/pbrain-rapfi";
        const EnsembleStats local_only =
            choose_ensemble_move(board, kSize, kForcedPosition, player, 1, runtime, config);
        check(local_only.source == EnsembleSource::LocalVcf,
              "a proven VCF win is reported as such");

        // The move must be part of a forcing line, not merely "the engine liked
        // it": playing it must still leave a proven win.
        check(is_legal_move(board, kSize, player, local_only.move), "the proven move is legal");
        int probe[kMaxBoard][kMaxBoard];
        std::memcpy(probe, board, sizeof(probe));
        probe[local_only.move.r][local_only.move.c] = player;
        int budget = 40000;
        check(forced_four_search(probe, kSize, player, 16, budget),
              "playing the proven move keeps a continuous-four win");

        // With an expert present the proof must still win the argument.
        if (argc > 1) {
            EnsembleConfig with_expert;
            with_expert.expert_program = argv[1];
            const EnsembleStats delegated = choose_ensemble_move(
                board, kSize, kForcedPosition, player, 1, runtime, with_expert);
            std::printf("      with expert=%s source=%s move=(%d,%d)\n",
                        delegated.expert_available ? "yes" : "no",
                        ensemble_source_name(delegated.source), delegated.move.r,
                        delegated.move.c);
            check(!delegated.expert_available || delegated.source == EnsembleSource::LocalVcf,
                  "an expert cannot override a proven win");
        }
    }

    std::printf("summary failures=%d\n", failures);
    return failures == 0 ? 0 : 1;
}
