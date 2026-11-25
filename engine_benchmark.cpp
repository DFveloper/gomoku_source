#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <sstream>
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
    std::string termination = "normal";
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
        request.referee_accept_black_foul = true;
        Move move{-1, -1};
        for (int attempt = 0; attempt < 2; ++attempt) {
            move = gomoku::run_external_engine(request, error);
            if (gomoku::is_legal_move(board, kSize, player, move) ||
                gomoku::black_forbidden_reply(board,kSize,player,move)) break;
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
                     const std::filesystem::path& runtime_root, bool alpha_teacher=false) {
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
        const bool lumen = engine == EngineVersion::CurrentV3;
        const auto played_engine=alpha_teacher&&lumen?EngineVersion::AlphaGomoku2026:engine;
        const int level = engine == EngineVersion::CurrentV3 ? lumen_level : opponent_level;
        std::string error;
        const auto started = std::chrono::steady_clock::now();
        Move move = choose(played_engine, board, result.moves, player, level, threads,
                           runtime_root, error, opponent_engine);
        const int elapsed = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count());
        if (std::getenv("GOMOKU_BENCH_TRACE")) {
            std::printf("ply=%zu player=%d engine=%d move=%d,%d elapsed_ms=%d error=%s\n",
                        result.moves.size() + 1, player, static_cast<int>(played_engine),
                        move.r, move.c, elapsed, error.empty() ? "none" : error.c_str());
            std::fflush(stdout);
        }
        long long& total = lumen ? result.lumen_ms : result.opponent_ms;
        int& turns = lumen ? result.lumen_turns : result.opponent_turns;
        int& maximum = lumen ? result.lumen_max_ms : result.opponent_max_ms;
        total += elapsed;
        ++turns;
        maximum = std::max(maximum, elapsed);
        if (!gomoku::is_legal_move(board, kSize, player, move)) {
            result.winner = 3 - player;
            if(gomoku::black_forbidden_reply(board,kSize,player,move)) {
                result.termination="black-forbidden";
                result.moves.push_back(move);
                return result;
            }
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
    auto parse_number = [](const char* text) {
        char* end = nullptr;
        const long value = std::strtol(text, &end, 10);
        return end != text && *end == '\0' && value >= 0 &&
               value <= std::numeric_limits<int>::max() ? static_cast<int>(value) : -1;
    };
    if(argc>1&&(std::string(argv[1])=="--replay-tail"||std::string(argv[1])=="--teacher-alpha-tail")) {
        const bool teacher=std::string(argv[1])=="--teacher-alpha-tail";
        if(argc<4||argc>6) {
            std::fprintf(stderr,"usage: engine_benchmark --replay-tail|--teacher-alpha-tail STAR_LOG PREFIX_PLIES [THREADS=6] [RUNTIME_ROOT]\n");
            return 2;
        }
        const int prefix=parse_number(argv[3]);
        const int threads=argc>4?parse_number(argv[4]):6;
        if(prefix<1||threads<1||threads>32)return 2;
        std::ifstream input(argv[2]);
        if(!input)return 2;
        int index=-1,opponent_id=-1;
        std::vector<Move> moves;
        std::string line;
        while(std::getline(input,line)) {
            if(line.rfind("star_case=",0)==0)
                std::sscanf(line.c_str(),"star_case=%d opponent=%d",&index,&opponent_id);
            if(line.rfind("moves=",0)!=0)continue;
            std::istringstream coordinates(line.substr(6));std::string token;
            while(coordinates>>token) {
                int r,c,consumed=0;
                if(std::sscanf(token.c_str(),"%d,%d%n",&r,&c,&consumed)!=2||
                   consumed!=static_cast<int>(token.size())||!gomoku::ok(kSize,r,c))return 2;
                moves.push_back({r,c});
            }
        }
        if(index<0||index>=18||(opponent_id!=4&&opponent_id!=5)||
           static_cast<size_t>(prefix)>=moves.size())return 2;
        moves.resize(static_cast<size_t>(prefix));
#ifdef GOMOKU_SOURCE_ROOT
        std::filesystem::path runtime=std::filesystem::path(GOMOKU_SOURCE_ROOT)/"third_party/runtime";
#else
        std::filesystem::path runtime="third_party/runtime";
#endif
        if(argc>5)runtime=std::filesystem::absolute(argv[5]);
        const auto opponent=static_cast<EngineVersion>(opponent_id);
        const int player=index%2+1;
        const auto game=play_game(player==1?EngineVersion::CurrentV3:opponent,
            player==2?EngineVersion::CurrentV3:opponent,3,3,threads,moves,runtime,teacher);
        const bool complete=game.winner!=0||game.moves.size()==kSize*kSize;
        const bool pass=game.error.empty()&&complete&&(game.winner==0||game.winner==player);
        std::printf("replay_case=%d opponent=%d prefix=%d winner=%d plies=%zu termination=%s status=%s error=%s diagnostic=%s\n",
            index,opponent_id,prefix,game.winner,game.moves.size(),game.termination.c_str(),
            pass?"PASS":"FAIL",game.error.empty()?"none":game.error.c_str(),teacher?"teacher-alpha":"tail-only");
        std::printf("moves=");print_history(game.moves);
        return pass?0:1;
    }
    // One independently runnable member of the nine-star-point acceptance set.
    // Only Black's first stone is prescribed; every subsequent move is played.
    if (argc > 1 && std::string(argv[1]) == "--star-case") {
        if (argc < 4 || argc > 6) {
            std::fprintf(stderr, "usage: engine_benchmark --star-case OPPONENT_ID CASE_0_TO_17 [THREADS=1] [RUNTIME_ROOT]\n");
            return 2;
        }
        const int opponent_id = parse_number(argv[2]);
        const int index = parse_number(argv[3]);
        const int threads = argc > 4 ? parse_number(argv[4]) : 1;
        if ((opponent_id != 4 && opponent_id != 5) || index < 0 || index >= 18 ||
            threads < 1 || threads > 32) return 2;
#ifdef GOMOKU_SOURCE_ROOT
        std::filesystem::path runtime = std::filesystem::path(GOMOKU_SOURCE_ROOT) / "third_party/runtime";
#else
        std::filesystem::path runtime = "third_party/runtime";
#endif
        if (argc > 5) runtime = std::filesystem::absolute(argv[5]);
        const auto opponent = static_cast<EngineVersion>(opponent_id);
        const int point = index / 2;
        const int lumen_player = index % 2 + 1;
        const Move start{3 + 4 * (point / 3), 3 + 4 * (point % 3)};
        const auto game = play_game(lumen_player == 1 ? EngineVersion::CurrentV3 : opponent,
                                    lumen_player == 2 ? EngineVersion::CurrentV3 : opponent,
                                    3, 3, threads, {start}, runtime);
        const bool complete = game.winner != 0 || game.moves.size() == kSize * kSize;
        const bool pass = game.error.empty() && complete &&
                          (game.winner == 0 || game.winner == lumen_player);
        std::printf("star_case=%d opponent=%d start=%d,%d lumen=%s winner=%d plies=%zu "
                    "lumen_avg_ms=%lld lumen_max_ms=%d opponent_avg_ms=%lld opponent_max_ms=%d "
                    "termination=%s status=%s error=%s\n",
                    index, opponent_id, start.r, start.c, lumen_player == 1 ? "black" : "white",
                    game.winner, game.moves.size(),
                    game.lumen_turns ? game.lumen_ms / game.lumen_turns : 0, game.lumen_max_ms,
                    game.opponent_turns ? game.opponent_ms / game.opponent_turns : 0, game.opponent_max_ms,
                    game.termination.c_str(), pass ? "PASS" : "FAIL", game.error.empty() ? "none" : game.error.c_str());
        std::printf("moves=");
        print_history(game.moves);
        return pass ? 0 : 1;
    }
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
