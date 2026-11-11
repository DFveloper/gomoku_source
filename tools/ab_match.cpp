// Paired A/B referee: plays two engine revisions against each other on the
// same openings with colours swapped, so a strength change is measured
// directly instead of being inferred from separate matches against a third
// engine.  The baseline revision is frozen in tools/fixtures/ and included
// under an alternate namespace by rewriting `gomoku` to `gomoku_base` for the
// duration of the include, which keeps both header-only engines in one
// translation unit.
//
//   ab_match [LEVEL=3] [PAIRS=1..8] [THREADS=4]
//
// Exit status is 0 when the working-tree engine does not lose the match.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#define gomoku gomoku_base
#include "fixtures/baseline_core.h"
#undef gomoku
#include "gomoku_core.h"

namespace {

constexpr int kSize = 15;
using gomoku::Move;
using BaseMove = gomoku_base::Move;

struct EngineSide {
    int wins = 0;
    long long ms = 0;
    int turns = 0;
    int max_ms = 0;
};

Move pick(bool baseline, int board[gomoku::kMaxBoard][gomoku::kMaxBoard], int p, int level,
          int threads, const std::vector<Move>& history) {
    // Both revisions see the same style model, built from the played moves.
    if (baseline) {
        std::vector<BaseMove> base_history;
        base_history.reserve(history.size());
        for (const Move& m : history) base_history.push_back(BaseMove{m.r, m.c});
        gomoku_base::OpponentStyle style =
            gomoku_base::analyze_opponent_style(kSize, base_history, 3 - p);
        BaseMove last{-1, -1};
        for (int i = static_cast<int>(base_history.size()) - 1; i >= 0; --i) {
            if ((i % 2 == 0 ? 1 : 2) == p) { last = base_history[i]; break; }
        }
        const BaseMove move =
            gomoku_base::choose_ai_move(board, kSize, p, level, &style, threads, last);
        return Move{move.r, move.c};
    }
    gomoku::OpponentStyle style = gomoku::analyze_opponent_style(kSize, history, 3 - p);
    Move last{-1, -1};
    for (int i = static_cast<int>(history.size()) - 1; i >= 0; --i) {
        if ((i % 2 == 0 ? 1 : 2) == p) { last = history[i]; break; }
    }
    return gomoku::choose_ai_move(board, kSize, p, level, &style, threads, last);
}

struct GameResult {
    int winner = 0;
    int plies = 0;
    std::string error;
};

GameResult play(bool black_baseline, const std::vector<Move>& opening, int level, int threads) {
    int board[gomoku::kMaxBoard][gomoku::kMaxBoard]{};
    std::vector<Move> history;
    GameResult result;
    for (size_t i = 0; i < opening.size(); ++i) {
        const int player = i % 2 == 0 ? 1 : 2;
        board[opening[i].r][opening[i].c] = player;
        history.push_back(opening[i]);
    }
    while (static_cast<int>(history.size()) < kSize * kSize) {
        const int player = history.size() % 2 == 0 ? 1 : 2;
        const bool baseline_turn = (player == 1) == black_baseline;
        Move move = pick(baseline_turn, board, player, level, threads, history);
        const bool legal = baseline_turn
            ? gomoku_base::is_legal_move(board, kSize, player, {move.r, move.c})
            : gomoku::is_legal_move(board, kSize, player, move);
        if (!legal) { result.winner = 3 - player; result.error = "illegal move"; return result; }
        board[move.r][move.c] = player;
        history.push_back(move);
        if (gomoku::check_win_at(board, kSize, move.r, move.c, player)) {
            result.winner = player;
            break;
        }
    }
    result.plies = static_cast<int>(history.size());
    return result;
}

}  // namespace

int main(int argc, char** argv) {
    const int level = argc > 1 ? std::atoi(argv[1]) : 3;
    const int pairs = argc > 2 ? std::atoi(argv[2]) : 2;
    const int threads = argc > 3 ? std::atoi(argv[3]) : 4;
    if (level < 1 || level > 5 || pairs < 1 || pairs > 8 || threads < 1 || threads > 32) {
        std::fprintf(stderr, "usage: ab_match [LEVEL=3] [PAIRS=2] [THREADS=4]\n");
        return 2;
    }
    const std::vector<std::vector<Move>> openings = {
        {{7, 7}, {7, 8}, {8, 7}, {6, 7}},
        {{7, 7}, {8, 8}, {6, 8}, {8, 6}, {6, 6}, {7, 8}},
        {{7, 7}, {6, 7}, {8, 8}, {8, 6}, {6, 8}, {7, 6}},
        {{7, 7}, {8, 7}, {6, 6}, {8, 8}, {6, 8}, {7, 6}},
        {{7, 7}, {7, 6}, {6, 8}, {8, 8}, {5, 9}, {9, 5}},
        {{7, 7}, {8, 6}, {6, 6}, {8, 8}, {6, 10}, {8, 10}},
        {{7, 7}, {6, 8}, {8, 6}, {5, 9}, {9, 5}, {6, 6}},
        {{7, 7}, {7, 8}, {6, 6}, {8, 8}, {6, 9}, {8, 6}},
    };
    int new_wins = 0, base_wins = 0, draws = 0;
    // Black has a large structural advantage, so per-colour results are reported
    // with the overall score.  A 2-2 split that is really "black won all four"
    // is not evidence of equal strength.
    int new_as_black = 0, black_games = 0, new_as_white = 0, white_games = 0;
    for (int pair = 0; pair < pairs; ++pair) {
        for (int swap = 0; swap < 2; ++swap) {
            const bool black_baseline = swap == 1;
            const auto started = std::chrono::steady_clock::now();
            GameResult game = play(black_baseline, openings[pair], level, threads);
            const int new_player = black_baseline ? 2 : 1;
            const char* verdict = game.winner == 0 ? "draw"
                                : game.winner == new_player ? "new" : "baseline";
            if (game.winner == new_player) ++new_wins;
            else if (game.winner == 0) ++draws;
            else ++base_wins;
            if (new_player == 1) { ++black_games; if (game.winner == 1) ++new_as_black; }
            else { ++white_games; if (game.winner == 2) ++new_as_white; }
            std::printf("game=%d new=%s winner=%s(%d) plies=%d error=%s wall=%lldms\n",
                        pair * 2 + swap + 1, black_baseline ? "white" : "black", verdict,
                        game.winner, game.plies,
                        game.error.empty() ? "none" : game.error.c_str(),
                        static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - started).count()));
            std::fflush(stdout);
        }
    }
    std::printf("summary level=%d pairs=%d new=%d baseline=%d draws=%d "
                "new_as_black=%d/%d new_as_white=%d/%d\n",
                level, pairs, new_wins, base_wins, draws,
                new_as_black, black_games, new_as_white, white_games);
    return base_wins > new_wins ? 1 : 0;
}
