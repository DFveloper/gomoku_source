#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "engine_strength.h"

namespace {

using gomoku::EngineVersion;
using gomoku::Move;
constexpr int kSize = 15;

struct GameResult {
    int winner = 0;
    std::vector<Move> moves;
    long long lumen_ms = 0;
    long long opponent_ms = 0;
    int lumen_turns = 0;
    int opponent_turns = 0;
    int lumen_max_ms = 0;
    int opponent_max_ms = 0;
    std::string error;
};

Move last_move_for(const std::vector<Move>& moves, int player) {
    for (int i = static_cast<int>(moves.size()) - 1; i >= 0; --i) {
        if ((i % 2 == 0 ? 1 : 2) == player) return moves[i];
    }
    return {-1, -1};
}

Move choose(EngineVersion engine, int board[gomoku::kMaxBoard][gomoku::kMaxBoard],
            const std::vector<Move>& history, int player, int level, int threads,
            const std::filesystem::path& runtime_root, std::string& error,
            EngineVersion opponent_engine) {
    if (engine == EngineVersion::CurrentV3)
        return gomoku::choose_strong_move(
            board, kSize, history, player, level, threads, runtime_root, error,
            nullptr, opponent_engine);
    if (engine >= EngineVersion::CarbonClassic) {
        gomoku::ExternalEngineRequest request;
        request.version = engine;
        request.moves = history;
        request.player = player;
        request.level = level;
        request.turn_time_ms = 5000;
        request.board_size = kSize;
        request.program = gomoku::external_runtime_path(runtime_root, engine);
        Move move{-1, -1};
        for (int attempt = 0; attempt < 2; ++attempt) {
            move = gomoku::run_external_engine(request, error);
            if (gomoku::is_legal_move(board, kSize, player, move)) break;
        }
        return move;
    }
    gomoku::OpponentStyle style = gomoku::analyze_opponent_style(kSize, history, 3 - player);
    return gomoku::choose_versioned_move(board, kSize, player, level, engine, &style,
                                         threads, last_move_for(history, player));
}

GameResult play_game(EngineVersion black, EngineVersion white, int opponent_level,
                     int lumen_level, int threads,
                     const std::vector<Move>& opening,
                     const std::filesystem::path& runtime_root) {
    GameResult result;
    int board[gomoku::kMaxBoard][gomoku::kMaxBoard]{};
    for (size_t i = 0; i < opening.size(); ++i) {
        const int player = i % 2 == 0 ? 1 : 2;
        Move move = opening[i];
        if (!gomoku::is_legal_move(board, kSize, player, move)) {
            result.error = "illegal benchmark opening";
            return result;
        }
        board[move.r][move.c] = player;
        result.moves.push_back(move);
    }

    while (result.moves.size() < static_cast<size_t>(kSize * kSize)) {
        const int player = result.moves.size() % 2 == 0 ? 1 : 2;
        const EngineVersion engine = player == 1 ? black : white;
        const EngineVersion opponent_engine = player == 1 ? white : black;
        const int level = engine == EngineVersion::CurrentV3 ? lumen_level : opponent_level;
        std::string error;
        const auto started = std::chrono::steady_clock::now();
        Move move = choose(engine, board, result.moves, player, level, threads,
                           runtime_root, error, opponent_engine);
        const int elapsed = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count());
        const bool lumen = engine == EngineVersion::CurrentV3;
        long long& total = lumen ? result.lumen_ms : result.opponent_ms;
        int& turns = lumen ? result.lumen_turns : result.opponent_turns;
        int& maximum = lumen ? result.lumen_max_ms : result.opponent_max_ms;
        total += elapsed;
        ++turns;
        maximum = std::max(maximum, elapsed);
        if (!gomoku::is_legal_move(board, kSize, player, move)) {
            result.winner = 3 - player;
            result.error = error.empty() ? "illegal move" : error;
            return result;
        }
        board[move.r][move.c] = player;
        result.moves.push_back(move);
        if (gomoku::check_win_at(board, kSize, move.r, move.c, player)) {
            result.winner = player;
            return result;
        }
    }
    return result;
}

void print_history(const std::vector<Move>& moves) {
    for (size_t i = 0; i < moves.size(); ++i) {
        if (i) std::putchar(' ');
        std::printf("%d,%d", moves[i].r, moves[i].c);
    }
    std::putchar('\n');
}

}  // namespace

int main(int argc, char** argv) {
    const int opponent_id = argc > 1 ? std::atoi(argv[1]) : 4;
    const int level = argc > 2 ? std::atoi(argv[2]) : 3;
    const int pairs = argc > 3 ? std::atoi(argv[3]) : 1;
    const int threads = argc > 4 ? std::atoi(argv[4]) : 8;
    const int lumen_level = argc > 5 ? std::atoi(argv[5]) : level;
    if (opponent_id < 0 || opponent_id >= static_cast<int>(EngineVersion::Count) ||
        opponent_id == static_cast<int>(EngineVersion::CurrentV3) ||
        level < 1 || level > 5 || lumen_level < 1 || lumen_level > 5 ||
        pairs < 1 || pairs > 4 || threads < 1 || threads > 32) {
        std::fprintf(stderr, "usage: engine_benchmark OPPONENT_ID [OPPONENT_LEVEL=3] "
                             "[PAIRS=1] [THREADS=8] [LUMEN_LEVEL=OPPONENT_LEVEL]\n");
        return 2;
    }
    const EngineVersion opponent = static_cast<EngineVersion>(opponent_id);
#ifdef GOMOKU_SOURCE_ROOT
    std::filesystem::path runtime_root =
        std::filesystem::path(GOMOKU_SOURCE_ROOT) / "third_party" / "runtime";
#else
    std::filesystem::path runtime_root = "third_party/runtime";
#endif
    const auto adjacent = std::filesystem::absolute(argv[0]).parent_path() / "third_party/runtime";
    if (std::filesystem::exists(adjacent)) runtime_root = adjacent;
    const std::vector<std::vector<Move>> openings = {
        {{7, 7}, {7, 8}, {8, 7}, {6, 7}},
        {{7, 7}, {8, 8}, {6, 8}, {8, 6}, {6, 6}, {7, 8}},
        {{7, 7}, {6, 7}, {8, 8}, {8, 6}, {6, 8}, {7, 6}},
        {{7, 7}, {8, 7}, {6, 6}, {8, 8}, {6, 8}, {7, 6}},
    };

    int lumen_wins = 0, opponent_wins = 0, draws = 0, errors = 0;
    for (int pair = 0; pair < pairs; ++pair) {
        for (int swap = 0; swap < 2; ++swap) {
            EngineVersion black = swap == 0 ? EngineVersion::CurrentV3 : opponent;
            EngineVersion white = swap == 0 ? opponent : EngineVersion::CurrentV3;
            GameResult game = play_game(black, white, level, lumen_level, threads,
                                        openings[pair], runtime_root);
            if (!game.error.empty()) ++errors;
            const int lumen_player = swap == 0 ? 1 : 2;
            if (game.winner == lumen_player) ++lumen_wins;
            else if (game.winner == 0) ++draws;
            else ++opponent_wins;
            std::printf("game=%d lumen=%s winner=%d plies=%zu lumen_avg_ms=%lld lumen_max_ms=%d "
                        "opponent_avg_ms=%lld opponent_max_ms=%d error=%s\n",
                        pair * 2 + swap + 1, swap == 0 ? "black" : "white", game.winner,
                        game.moves.size(), game.lumen_turns ? game.lumen_ms / game.lumen_turns : 0,
                        game.lumen_max_ms,
                        game.opponent_turns ? game.opponent_ms / game.opponent_turns : 0,
                        game.opponent_max_ms, game.error.empty() ? "none" : game.error.c_str());
            std::printf("moves=");
            print_history(game.moves);
            std::fflush(stdout);
        }
    }
    std::printf("summary opponent=%s opponent_level=%d lumen_level=%d "
                "lumen=%d opponent=%d draws=%d\n",
                gomoku::engine_version_name(opponent), level, lumen_level,
                lumen_wins, opponent_wins, draws);
    return opponent_wins == 0 && errors == 0 ? 0 : 1;
}
