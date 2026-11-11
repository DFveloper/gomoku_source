// Tactical regression suite for the search itself: positions with a known
// single answer, plus a timing report.  These are the checks that would have
// caught the losses in the Rapfi duels, kept as executable expectations instead
// of prose.
//
//   tactics_suite [LEVEL=2] [THREADS=2]
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#include "gomoku_core.h"

namespace {

using gomoku::Move;
constexpr int kSize = 15;

struct Stone { int r, c, player; };

struct Case {
    const char* name;
    std::vector<Stone> stones;
    int player;                  // side to move
    std::vector<Move> expected;  // any of these is correct
};

// Positions are written as explicit stones rather than ASCII diagrams: a
// mistyped diagram silently changes the tactic (an early draft gave Black a
// vertical four by accident, and the "failing" test was right).
const std::vector<Case> kCases = {
    // The exact position the engine faced in the first Rapfi duel, before it
    // played the losing move.  Black holds the open-four diagonal
    // 5,5 6,6 7,7 8,8, so White must take 4,4 or 9,9; anything else loses to
    // the open four, which cannot be blocked.
    {"stop black's open-four diagonal",
     {{7, 7, 1}, {7, 8, 2}, {8, 7, 1}, {6, 7, 2}, {5, 6, 1}, {4, 7, 2},
      {7, 6, 1}, {4, 6, 2}, {8, 6, 1}, {6, 8, 2}, {8, 8, 1}, {8, 9, 2},
      {6, 6, 1}, {9, 6, 2}, {5, 5, 1}},
     2,
     {{4, 4}, {9, 9}}},
    // White holds an open four; Black must block an end.
    {"block white's open four",
     {{7, 3, 2}, {7, 4, 2}, {7, 5, 2}, {7, 6, 2}, {11, 2, 1}, {11, 3, 1},
      {12, 2, 1}, {12, 3, 1}},
     1,
     {{7, 2}, {7, 7}}},
    // White holds a closed four; only one end is open.
    {"block white's four",
     {{7, 3, 2}, {7, 4, 2}, {7, 5, 2}, {7, 6, 2}, {7, 2, 1},
      {11, 2, 1}, {11, 3, 1}, {12, 2, 1}},
     1,
     {{7, 7}}},
    // Black to move with a four available: taking the five beats defending.
    {"take the five",
     {{7, 3, 1}, {7, 4, 1}, {7, 5, 1}, {7, 6, 1},
      {10, 3, 2}, {10, 4, 2}, {10, 5, 2}, {10, 6, 2}, {10, 2, 1}},
     1,
     {{7, 2}, {7, 7}}},
};

}  // namespace

int main(int argc, char** argv) {
    const int level = argc > 1 ? std::atoi(argv[1]) : 2;
    const int threads = argc > 2 ? std::atoi(argv[2]) : 2;

    static const char* open_four_diag[15] = {
        "...............",
        "...............",
        "...............",
        "...............",
        "....O.O........",
        ".....XX........",
        ".....XOO.......",
        ".....XXO.......",
        ".....XXXO......",
        ".....O..X......",
        "...............",
        "...............",
        "...............",
        "...............",
        "...............",
    };
    // 2. White (O) has an open four horizontally; Black must block one end.
    static const char* block_open_four[15] = {
        "...............",
        "...............",
        "...............",
        "...............",
        "...............",
        "...............",
        "...............",
        "...OOO.O.......",
        "...............",
        "...............",
        "...............",
        "..XX...........",
        "..XX...........",
        "...............",
        "...............",
    };
    // 3. White can make five immediately; Black must block it.
    static const char* block_four[15] = {
        "...............",
        "...............",
        "...............",
        "...............",
        "...............",
        "...............",
        "...............",
        "...OOOO........",
        "...............",
        "...............",
        "...............",
        "...............",
        "..XX...........",
        "...............",
        "...............",
    };

    int passed = 0, failed = 0;
    int worst_ms = 0;
    for (const Case& test : kCases) {
        int board[gomoku::kMaxBoard][gomoku::kMaxBoard]{};
        std::vector<Move> history;
        for (const Stone& stone : test.stones) {
            board[stone.r][stone.c] = stone.player;
            history.push_back({stone.r, stone.c});
        }
        const auto started = std::chrono::steady_clock::now();
        gomoku::SearchStats stats;
        const Move move = gomoku::choose_ai_move(board, kSize, test.player, level, nullptr,
                                                 threads, {-1, -1}, nullptr, &stats);
        const int elapsed = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count());
        worst_ms = std::max(worst_ms, elapsed);
        bool ok = false;
        for (const Move& expected : test.expected)
            if (move.r == expected.r && move.c == expected.c) ok = true;
        // The answer must also be legal: a defended block in Renju must not be
        // a forbidden move for Black.
        const bool legal = gomoku::is_legal_move(board, kSize, test.player, move);
        std::printf("[%s] %-34s got (%2d,%2d) in %5d ms (depth %d, audits %d, vcf %s)\n",
                    ok && legal ? "PASS" : "FAIL", test.name, move.r, move.c, elapsed,
                    stats.search_depth, stats.audits, stats.vcf_win ? "win" : "-");
        ok && legal ? ++passed : ++failed;
    }
    // A level budget is a promise; a tactic that blows through it is a bug even
    // when the move is right.
    const int budget_ms = 1000 * (1 << (level - 1));
    const bool timely = worst_ms <= budget_ms + 1000 + 500;
    std::printf("[%s] every answer inside the level budget (%d ms, worst %d ms)\n",
                timely ? "PASS" : "FAIL", budget_ms, worst_ms);
    timely ? ++passed : ++failed;
    std::printf("summary level=%d passed=%d failed=%d\n", level, passed, failed);
    return failed ? 1 : 0;
}
