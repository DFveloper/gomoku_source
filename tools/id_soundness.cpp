// Soundness check for the iterative-deepening root.
//
// Whenever the engine answers from a pass that finished (stats.search_depth_complete),
// that pass searched every root at `search_depth` with a full window, so the move
// it returns must be the plain argmax of that pass.  This recomputes the same
// pass independently (single thread, its own table, no aspiration, no ordering
// reuse) and compares.  It tests the bookkeeping iterative deepening added --
// value attribution, reordering, bound bookkeeping, tie-breaking -- rather than
// demanding bit-identity with a differently orchestrated baseline.  Answers that
// came from a partial aspirated pass are reported as skipped, not as failures.
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>
#include <algorithm>

#include "gomoku_core.h"

namespace {

using gomoku::Move;

// Independent full-window pass, mirroring the engine's root evaluation but with
// no ordering reuse, no aspiration and a single worker.
Move ground_truth_move(int board[gomoku::kMaxBoard][gomoku::kMaxBoard], int sz, int p,
                       int depth) {
    auto roots = gomoku::build_root_candidates(board, sz, p, nullptr);
    int best_value = -210000000;
    Move best{-1, -1};
    int best_key = -1;
    for (const auto& item : roots) {
        int local[gomoku::kMaxBoard][gomoku::kMaxBoard];
        std::memcpy(local, board, sizeof(local));
        const Move m = item.second;
        local[m.r][m.c] = p;
        int value;
        if (gomoku::check_win_at(local, sz, m.r, m.c, p)) {
            value = 200000000 + depth;
        } else if (gomoku::has_forced_four_win(local, sz, 3 - p, 6, 320)) {
            value = -190000000 - depth;
        } else {
            gomoku::TranspositionTable table;
            int scratch[gomoku::kMaxBoard][gomoku::kMaxBoard];
            std::memcpy(scratch, local, sizeof(scratch));
            value = -gomoku::negamax(scratch, sz, 3 - p, depth, -210000000, 210000000,
                                     m, p, nullptr, 2, nullptr, nullptr, nullptr,
                                     gomoku::position_hash(scratch, sz, 3 - p), &table);
        }
        if (value > best_value || (value == best_value && item.first > best_key)) {
            best_value = value;
            best = m;
            best_key = item.first;
        }
    }
    return best;
}

}  // namespace

int main(int argc, char** argv) {
    const int level = argc > 1 ? std::atoi(argv[1]) : 2;
    const int positions = argc > 2 ? std::atoi(argv[2]) : 8;
    std::mt19937 rng(4242);
    int same = 0, differ = 0, skipped = 0;
    for (int trial = 0; trial < positions; ++trial) {
        int board[gomoku::kMaxBoard][gomoku::kMaxBoard]{};
        const int plies = 2 + static_cast<int>(rng() % 8);
        for (int i = 0; i < plies; ++i) {
            const int player = i % 2 == 0 ? 1 : 2;
            for (int attempt = 0; attempt < 200; ++attempt) {
                const int r = static_cast<int>(rng() % 15), c = static_cast<int>(rng() % 15);
                if (board[r][c]) continue;
                if (player == 1 && gomoku::is_forbidden_move(board, 15, r, c)) continue;
                board[r][c] = player;
                break;
            }
        }
        const int player = plies % 2 == 0 ? 1 : 2;
        gomoku::SearchStats stats;
        const Move chosen = gomoku::choose_ai_move(board, 15, player, level, nullptr, 1,
                                                   {-1, -1}, nullptr, &stats);
        if (stats.search_depth == 0 || !stats.search_depth_complete) {
            std::printf("trial %2d: answer came from a partial pass at depth %d (skip)\n",
                        trial, stats.search_depth);
            ++skipped;
            continue;
        }
        if (stats.vcf_win || stats.vhazard_avoided > 0) {
            std::printf("trial %2d: tactical override applied (vcf=%d avoided=%d) (skip)\n",
                        trial, stats.vcf_win ? 1 : 0, stats.vhazard_avoided);
            ++skipped;
            continue;
        }
        const Move expected = ground_truth_move(board, 15, player, stats.search_depth);
        const bool equal = chosen.r == expected.r && chosen.c == expected.c;
        if (equal) ++same; else ++differ;
        std::printf("trial %2d plies=%2d depth=%d engine=(%2d,%2d) argmax=(%2d,%2d) %s\n",
                    trial, plies, stats.search_depth, chosen.r, chosen.c, expected.r, expected.c,
                    equal ? "ok" : "MISMATCH");
        std::fflush(stdout);
    }
    std::printf("summary ok=%d mismatch=%d skipped=%d\n", same, differ, skipped);
    return (differ || same == 0) ? 1 : 0;
}
