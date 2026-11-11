// Measures each member of the mixture on its own, at the same clock, so that a
// mixture result can be attributed instead of guessed.
//
// For every position it asks each engine for a move with an identical wall-clock
// budget, then plays the move forward a fixed number of plies against the
// local engine and scores the resulting position: 1 for an immediate win, 1 for
// a position the local engine cannot hold, -1 for a loss. Grading a single move
// this way is noisy, so the report is meant to be read as "which members are
// ever better than the local search", not as a rating list.
//
//   expert_strength RAPFI_BINARY ALPHA_BINARY [BUDGET_MS=1000]
//
// Prints one row per (position, engine) plus a per-engine summary.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "engine_adapter.h"

using namespace gomoku;

namespace {

constexpr int kSize = 15;
constexpr int kThreads = 1;

struct Sample {
    std::vector<Move> moves;
    const char* name;
};

struct EngineRef {
    std::string name;
    EngineVersion version = EngineVersion::Rapfi2025;
    std::filesystem::path program;
};

// Midgame positions taken from the recorded duel games, with live threats on
// both sides. `player` is whoever is to move.
const std::vector<Sample> kSamples = {
    {{{7, 7}, {7, 8}, {8, 7}, {6, 7}}, "open-2"},
    {{{7, 7}, {8, 8}, {6, 8}, {8, 6}, {6, 6}, {7, 8}, {9, 5}, {5, 9}, {9, 9}, {5, 5}}, "cross-4"},
    {{{7, 7}, {6, 7}, {8, 8}, {8, 6}, {6, 8}, {7, 6}, {9, 7}, {5, 7}, {9, 8}, {5, 8}}, "double-3"},
    {{{7, 7}, {8, 6}, {6, 6}, {8, 8}, {6, 10}, {8, 10}, {9, 5}, {5, 9}, {9, 10}, {5, 5}}, "split"},
    {{{7, 7}, {6, 8}, {8, 6}, {5, 9}, {9, 5}, {6, 6}, {10, 4}, {4, 10}, {7, 5}, {7, 9}}, "long-line"},
    {{{7, 7}, {7, 6}, {8, 7}, {6, 7}, {8, 8}, {6, 8}, {9, 9}, {5, 5}, {9, 7}, {5, 9},
      {10, 10}, {4, 4}, {8, 5}, {6, 9}}, "hot-midgame"},
    {{{7, 7}, {8, 8}, {6, 6}, {8, 6}, {6, 8}, {9, 9}, {5, 5}, {9, 7}, {5, 7},
      {10, 10}, {4, 4}, {8, 4}, {6, 10}, {7, 5}, {7, 9}, {8, 10}}, "late-midgame"},
};

void fill_board(int board[kMaxBoard][kMaxBoard], const std::vector<Move>& moves) {
    std::memset(board, 0, sizeof(int) * kMaxBoard * kMaxBoard);
    for (size_t i = 0; i < moves.size(); ++i)
        board[moves[i].r][moves[i].c] = i % 2 == 0 ? 1 : 2;
}

// Plays `candidate` and lets the local engine answer, then scores the outcome
// from the candidate's point of view: +1 if it wins outright, -1 if the opponent
// wins on the reply, 0 otherwise. A cheap but honest tie-break.
int score_move(int board[kMaxBoard][kMaxBoard], int player, Move candidate, int level) {
    if (!is_legal_move(board, kSize, player, candidate)) return -100;
    if (check_win_at(board, kSize, candidate.r, candidate.c, player)) return 1;
    int next[kMaxBoard][kMaxBoard];
    std::memcpy(next, board, sizeof(next));
    next[candidate.r][candidate.c] = player;
    std::vector<Move> history;  // the search only needs the board
    OpponentStyle style{};
    Move reply = choose_ai_move(next, kSize, 3 - player, level, &style, kThreads, Move{-1, -1});
    if (reply.r == -1) return 0;
    if (check_win_at(next, kSize, reply.r, reply.c, 3 - player)) return -1;
    return 0;
}

}  // namespace

// `name:version:path` entries separated by ';', where version is "std" or
// "alpha". Used instead of argv so that the same list can drive the mixture.
std::vector<EngineRef> engines_from_env() {
    std::vector<EngineRef> result;
    const char* spec = std::getenv("EXPERT_STRENGTH_ENGINES");
    if (!spec) return result;
    const std::string list(spec);
    std::size_t pos = 0;
    while (pos <= list.size()) {
        const std::size_t semi = list.find(';', pos);
        const std::string item =
            list.substr(pos, semi == std::string::npos ? std::string::npos : semi - pos);
        const std::size_t first = item.find(':');
        const std::size_t second = first == std::string::npos ? std::string::npos
                                                              : item.find(':', first + 1);
        if (first != std::string::npos && second != std::string::npos) {
            EngineRef ref;
            ref.name = item.substr(0, first);
            ref.version = item.substr(first + 1, second - first - 1) == "alpha"
                ? EngineVersion::AlphaGomoku2026 : EngineVersion::Rapfi2025;
            ref.program = item.substr(second + 1);
            if (!ref.name.empty() && !ref.program.empty()) result.push_back(ref);
        }
        if (semi == std::string::npos) break;
        pos = semi + 1;
    }
    return result;
}

int main(int argc, char** argv) {
    if (argc < 3 && !std::getenv("EXPERT_STRENGTH_ENGINES")) {
        std::fprintf(stderr, "usage: expert_strength RAPFI_BINARY ALPHA_BINARY [BUDGET_MS=1000]\n"
                             "   or: EXPERT_STRENGTH_ENGINES=\"rapfi:std:<path>;alpha:alpha:<path>\"\n");
        return 2;
    }
    const int budget_ms = std::getenv("EXPERT_STRENGTH_ENGINES")
        ? (argc > 1 ? std::atoi(argv[1]) : 1000)
        : (argc > 3 ? std::atoi(argv[3]) : 1000);
    const int level = budget_ms == 1000 ? 1 : budget_ms == 2000 ? 2 : 3;
    std::printf("budget=%dms level=%d positions=%zu threads=%d\n", budget_ms, level,
                kSamples.size(), kThreads);

    struct Engine {
        std::string name;
        EngineVersion version;
        std::filesystem::path program;
        int wins = 0, losses = 0, neutral = 0, illegal = 0, failed = 0;
        long total_ms = 0;
    };
    std::vector<Engine> engines;
    const std::vector<EngineRef> refs = engines_from_env();
    if (!refs.empty()) {
        for (const EngineRef& ref : refs)
            engines.push_back({ref.name, ref.version, ref.program, 0, 0, 0, 0, 0, 0});
    } else {
        engines.push_back({"rapfi", EngineVersion::Rapfi2025, argv[1], 0, 0, 0, 0, 0, 0});
        engines.push_back({"alpha", EngineVersion::AlphaGomoku2026, argv[2], 0, 0, 0, 0, 0, 0});
    }

    for (size_t i = 0; i < kSamples.size(); ++i) {
        int board[kMaxBoard][kMaxBoard];
        fill_board(board, kSamples[i].moves);
        const int player = kSamples[i].moves.size() % 2 == 0 ? 1 : 2;

        // The local engine is a member too: it is what every expert must beat.
        {
            std::vector<Move> history = kSamples[i].moves;
            OpponentStyle style = analyze_opponent_style(kSize, history, 3 - player);
            Move last{-1, -1};
            for (int k = static_cast<int>(history.size()) - 1; k >= 0; --k)
                if ((k % 2 == 0 ? 1 : 2) == player) { last = history[k]; break; }
            SearchStats stats;
            const auto started = std::chrono::steady_clock::now();
            const Move local_move = choose_ai_move(board, kSize, player, level, &style,
                                                  kThreads, last, nullptr, &stats);
            const long ms = static_cast<long>(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started).count());
            const int score = score_move(board, player, local_move, level);
            std::printf("position=%zu %-13s engine=%-8s move=(%2d,%2d) score=%+3d ms=%ld depth=%d\n",
                        i, kSamples[i].name, "local", local_move.r, local_move.c, score, ms,
                        stats.search_depth);
        }

        for (Engine& engine : engines) {
            ExternalEngineRequest request;
            request.version = engine.version;
            request.moves = kSamples[i].moves;
            request.player = player;
            request.level = level;
            request.board_size = kSize;
            request.program = engine.program;
            std::string error;
            const auto started = std::chrono::steady_clock::now();
            Move move = run_external_engine(request, error);
            const long ms = static_cast<long>(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started).count());
            engine.total_ms += ms;
            const int score = score_move(board, player, move, level);
            if (score == -100) {
                ++engine.illegal;
                ++engine.failed;
            } else if (score > 0) {
                ++engine.wins;
            } else if (score < 0) {
                ++engine.losses;
            } else {
                ++engine.neutral;
            }
            std::printf("position=%zu %-13s engine=%-8s move=(%2d,%2d) score=%+3d ms=%ld%s%s\n",
                        i, kSamples[i].name, engine.name.c_str(), move.r, move.c, score, ms,
                        error.empty() ? "" : " error=", error.c_str());
            std::fflush(stdout);
        }
    }

    for (const Engine& engine : engines)
        std::printf("engine=%s positions=%zu wins=%d neutral=%d losses=%d illegal=%d avg_ms=%ld\n",
                    engine.name.c_str(), kSamples.size(), engine.wins, engine.neutral,
                    engine.losses, engine.illegal,
                    static_cast<long>(engine.total_ms / static_cast<long>(kSamples.size())));
    return 0;
}
