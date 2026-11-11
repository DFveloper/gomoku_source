// Equal-time duel referee: the in-tree engine against an external Gomocup
// engine, or against an earlier revision, with the same per-move time budget
// for both sides.
//
//   duel ENGINE [BUDGET_MS] [PAIRS] [THREADS]
//
// ENGINE is one of: rapfi, carbon, alphagomoku, xl, legacy, v2, self.
// BUDGET_MS is the per-move budget handed to both sides (default 2000).
// PAIRS is the number of openings, each played twice with colours swapped.
//
// Why a separate tool instead of engine_benchmark: the benchmark spawns a new
// external process per move (correct for a UI, but it charges process startup
// and NNUE loading to every single move) and it varies the budget per engine.
// This referee keeps one persistent session per game, gives both sides exactly
// the same clock, and reports per-colour results, because black's structural
// advantage makes a colour-dominated score meaningless.
//
// Every finished game is checked by this process, not by the engines: exactly
// five in a row for black, five or more for white, and every move must be legal
// Renju (no overline, double four or double three for black).
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#ifndef _WIN32
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "engine_adapter.h"
#include "engine_ensemble.h"

namespace {

using gomoku::EngineVersion;
using gomoku::Move;
constexpr int kSize = 15;

struct EngineSpec {
    std::string name;
    bool external = false;
    std::filesystem::path program;
};

struct SideStats {
    long long total_ms = 0;
    int turns = 0;
    int max_ms = 0;
    int timeouts = 0;
    int illegal = 0;
    int errors = 0;
};

struct GameResult {
    int winner = 0;   // 1 = black, 2 = white, 0 = draw
    int plies = 0;
    bool void_game = false;  // opponent crashed, misbehaved or timed out
    std::string note;
};

// ---------------------------------------------------------------------------
// External engine session
// ---------------------------------------------------------------------------

#ifndef _WIN32

class ExternalSession {
public:
    explicit ExternalSession(std::filesystem::path program) : program_(std::move(program)) {}
    ~ExternalSession() { close_session(); }

    bool open(int board_size, int turn_time_ms) {
        close_session();
        int to_child[2] = {-1, -1}, from_child[2] = {-1, -1};
        if (pipe(to_child) != 0 || pipe(from_child) != 0) return false;
        const pid_t pid = fork();
        if (pid < 0) return false;
        if (pid == 0) {
            dup2(to_child[0], STDIN_FILENO);
            dup2(from_child[1], STDOUT_FILENO);
            dup2(from_child[1], STDERR_FILENO);
            for (int fd : {to_child[0], to_child[1], from_child[0], from_child[1]}) close(fd);
            const std::string dir = program_.parent_path().string();
            if (!dir.empty()) {
                if (chdir(dir.c_str()) != 0) _exit(126);
            }
            execl(program_.c_str(), program_.filename().c_str(), static_cast<char*>(nullptr));
            _exit(127);
        }
        close(to_child[0]);
        close(from_child[1]);
        pid_ = pid;
        to_child_ = to_child[1];
        from_child_ = from_child[0];
        if (!send("START " + std::to_string(board_size))) return false;
        if (!send("INFO timeout_turn " + std::to_string(turn_time_ms))) return false;
        if (!send("INFO timeout_match 3600000")) return false;
        if (!send("INFO rule 4")) return false;  // Renju
        if (!send("INFO max_memory 1048576")) return false;

        if (pid_ > 0) {
            int status = 0;
            if (waitpid(pid_, &status, WNOHANG) == pid_) {  // died during setup
                pid_ = -1;
                return false;
            }
        }
        // The board exchange below asks the engine for its first move, so no
        // explicit isready handshake is needed; a short drain swallows the
        // "OK" and any configuration messages before the clock starts.
        drain(400);
        return true;
    }

    // Sends the whole position and waits for one coordinate.
    Move think(const std::vector<Move>& moves, int engine_player, int turn_time_ms,
               int wait_ms, bool& timed_out, bool& protocol_error) {
        timed_out = false;
        protocol_error = false;
        // Rapfi (and Carbon) treat every BOARD as an incremental continuation,
        // so a full replay each turn is the form that works for them: the
        // engine sees the whole position again and answers for side 1.
        std::string board = "BOARD\n";
        for (size_t i = 0; i < moves.size(); ++i) {
            const bool black_stone = i % 2 == 0;
            const bool own = (engine_player == 1) == black_stone;
            board += std::to_string(moves[i].c) + "," + std::to_string(moves[i].r) + "," +
                     (own ? "1" : "2") + "\n";
        }
        board += "DONE\n";
        drain(0);
        if (!send(board)) {
            protocol_error = true;
            return {-1, -1};
        }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(wait_ms);
        std::string pending;
        while (std::chrono::steady_clock::now() < deadline) {
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now()).count();
            if (remaining <= 0) break;
            pollfd fds{from_child_, POLLIN, 0};
            const int ready = poll(&fds, 1, static_cast<int>(std::min<long long>(remaining, 50)));
            if (ready <= 0) continue;
            char buffer[4096];
            const ssize_t count = read(from_child_, buffer, sizeof(buffer));
            if (count <= 0) {
                protocol_error = true;
                return {-1, -1};
            }
            pending.append(buffer, buffer + count);
            if (std::getenv("DUEL_DEBUG")) {
                std::fwrite(buffer, 1, static_cast<size_t>(count), stderr);
            }
            size_t line_end;
            while ((line_end = pending.find('\n')) != std::string::npos) {
                std::string line = pending.substr(0, line_end);
                pending.erase(0, line_end + 1);
                int x = -1, y = -1;
                char trailing = 0;
                if (std::sscanf(line.c_str(), " %d , %d %c", &x, &y, &trailing) == 2) {
                    return {y, x};  // Gomocup sends (column,row)
                }
            }
        }
        timed_out = true;
        return {-1, -1};
    }

    void finish_game() {
        if (to_child_ >= 0) send("END");
    }

    void drain(int wait_ms) {
        if (from_child_ < 0) return;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(wait_ms);
        do {
            pollfd fds{from_child_, POLLIN, 0};
            const int ready = poll(&fds, 1, wait_ms > 0 ? 5 : 0);
            if (ready <= 0) break;
            char buffer[4096];
            if (read(from_child_, buffer, sizeof(buffer)) <= 0) break;
        } while (std::chrono::steady_clock::now() < deadline);
    }

private:
    bool send(const std::string& text) {
        if (to_child_ < 0) return false;
        std::string data = text;
        if (data.empty() || data.back() != '\n') data += '\n';
        size_t sent = 0;
        while (sent < data.size()) {
            const ssize_t count = write(to_child_, data.data() + sent, data.size() - sent);
            if (count <= 0) return false;
            sent += static_cast<size_t>(count);
        }
        return true;
    }

    void close_session() {
        if (to_child_ >= 0) { close(to_child_); to_child_ = -1; }
        if (from_child_ >= 0) { close(from_child_); from_child_ = -1; }
        if (pid_ > 0) {
            int status = 0;
            for (int attempt = 0; attempt < 100; ++attempt) {
                const pid_t done = waitpid(pid_, &status, WNOHANG);
                if (done == pid_ || done < 0) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            kill(pid_, SIGKILL);
            waitpid(pid_, &status, 0);
            pid_ = -1;
        }
    }

    std::filesystem::path program_;
    pid_t pid_ = -1;
    int to_child_ = -1;
    int from_child_ = -1;
};

#endif  // !_WIN32

// ---------------------------------------------------------------------------
// Local engine
// ---------------------------------------------------------------------------

std::filesystem::path runtime_root_for_ensemble() {
#ifdef GOMOKU_SOURCE_ROOT
    return std::filesystem::path(GOMOKU_SOURCE_ROOT) / "third_party" / "runtime";
#else
    return std::filesystem::current_path() / "third_party" / "runtime";
#endif
}

EngineSpec make_spec(const std::string& name) {
    EngineSpec spec;
    spec.name = name;
    const std::filesystem::path root =
#ifdef GOMOKU_SOURCE_ROOT
        std::filesystem::path(GOMOKU_SOURCE_ROOT);
#else
        std::filesystem::current_path();
#endif
    const std::filesystem::path runtime = root / "third_party" / "runtime";
    // A locally built `<binary>.native*` is preferred when present: the bundled
    // runtimes are linked against a newer glibc than some hosts provide, and
    // (for Rapfi) a native build also gets the host's SIMD width.
    auto pick = [&](const char* dir, const char* bundled, const char* native) {
        const std::filesystem::path preferred = runtime / dir / native;
        if (!native) return runtime / dir / bundled;
        return std::filesystem::exists(preferred) ? preferred : runtime / dir / bundled;
    };
    if (name == "rapfi") {
        spec.external = true;
        spec.program = pick("rapfi", "pbrain-rapfi", "pbrain-rapfi.native-glibc236");
    } else if (name == "carbon") {
        spec.external = true;
        spec.program = pick("carbon", "pbrain-carbon", nullptr);
    } else if (name == "alphagomoku" || name == "alpha") {
        spec.external = true;
        spec.program = pick("alphagomoku", "pbrain-agomoku", "pbrain-agomoku.native-glibc236");
    } else if (name == "xl") {
        spec.external = true;
        spec.program = pick("xl", "pbrain-xl", "pbrain-xl.native-glibc236");
    }
    return spec;
}


// Applies the mixture environment overrides shared by the per-position report
// and the match modes: which binary is the first expert, which engines join the
// council, and whether votes or the first member decide.
// The first expert's binary: the opponent's own binary when it is an external
// engine, otherwise a locally built Rapfi, otherwise whatever the runtime lookup
// finds. Getting this wrong is silent - the expert just never answers and the
// mixture quietly degrades to the local engine - so it is resolved in one place.
std::filesystem::path expert_program_for(const EngineSpec& opponent_spec) {
    if (opponent_spec.external) return opponent_spec.program;
    const EngineSpec rapfi = make_spec("rapfi");
    return rapfi.program;
}

void apply_ensemble_env(gomoku::EnsembleConfig& config) {
    if (const char* program = std::getenv("DUEL_MIX_EXPERT_PROGRAM"))
        config.expert_program = program;
    if (const char* council = std::getenv("DUEL_MIX_COUNCIL")) {
        std::string list(council), item;
        std::size_t pos = 0;
        while (pos <= list.size()) {
            const std::size_t comma = list.find(',', pos);
            item = list.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
            if (!item.empty()) {
                const EngineSpec member = make_spec(item);
                if (member.external) {
                    gomoku::EnsembleExpert extra;
                    extra.program = member.program;
                    extra.version = (item == "alpha" || item == "alphagomoku")
                        ? gomoku::EngineVersion::AlphaGomoku2026
                        : item == "xl" ? gomoku::EngineVersion::XLNiren
                        : gomoku::EngineVersion::Rapfi2025;
                    if (std::filesystem::exists(extra.program))
                        config.extra_experts.push_back(extra);
                }
            }
            if (comma == std::string::npos) break;
            pos = comma + 1;
        }
    }
    if (const char* dictator = std::getenv("DUEL_MIX_DICTATOR"))
        if (std::string(dictator) == "1") config.prefer_consensus = false;
    if (const char* fraction = std::getenv("DUEL_MIX_FRACTION"))
        config.expert_budget_fraction = std::atof(fraction);
}

// Maps a per-move budget onto the in-tree engine's level, whose budgets are
// 1, 2, 4, 8 and 16 seconds.  Only exact matches are accepted so the duel is
// genuinely equal-time.
int level_for_budget(int budget_ms) {
    for (int level = 1; level <= 5; ++level)
        if (budget_ms == 1000 * (1 << (level - 1))) return level;
    return 0;
}

bool overline_for_black(const int board[gomoku::kMaxBoard][gomoku::kMaxBoard], int sz, Move move) {
    static const int dr[] = {0, 1, 1, 1};
    static const int dc[] = {1, 0, 1, -1};
    for (int d = 0; d < 4; ++d)
        if (gomoku::count_line(board, sz, move.r, move.c, dr[d], dc[d], 1) > 5) return true;
    return false;
}

}  // namespace

int main(int argc, char** argv) {
#ifndef _WIN32
    // A dead child turns pipe writes into SIGPIPE; treat that as a normal
    // write failure so a missing/broken runtime is reported, not fatal.
    signal(SIGPIPE, SIG_IGN);
    if (argc < 2) {
        std::fprintf(stderr, "usage: duel ENGINE [BUDGET_MS=2000] [PAIRS=1..8] [THREADS=2]\n"
                             "  ENGINE: rapfi | carbon | alphagomoku | xl | legacy | v2 | self | mixture\n"
                             "  mixture mode reads DUEL_OPPONENT (default rapfi), "
                             "DUEL_MIX_COUNCIL=alpha,xl, DUEL_MIX_DICTATOR=1\n");
        return 2;
    }
    const std::string engine_name = argv[1];
    const int budget_ms = argc > 2 ? std::atoi(argv[2]) : 2000;
    const int pairs = argc > 3 ? std::atoi(argv[3]) : 1;
    const int threads = argc > 4 ? std::atoi(argv[4]) : 2;
    // Ensemble mode: instead of duelling, report what the mixture would play on
    // each duel position, with the clock it would spend. `DUEL_ENSEMBLE=trusting`
    // drops verification so the two policies can be compared.
    if (std::getenv("DUEL_ENSEMBLE")) {
        const bool trusting = std::string(std::getenv("DUEL_ENSEMBLE")) == "trusting";
        gomoku::EnsembleConfig config;
        config.policy = trusting ? gomoku::EnsemblePolicy::Trusting
                                 : gomoku::EnsemblePolicy::Verified;
        config.local_threads = threads;
        // Prefer a locally built expert when one exists (see
        // tools/build_native_engines.sh); fall back to the runtime lookup.
        const std::filesystem::path native = runtime_root_for_ensemble() / "rapfi" /
                                             "pbrain-rapfi.native-glibc236";
        if (std::filesystem::exists(native)) config.expert_program = native;
        apply_ensemble_env(config);
        const int core_level = level_for_budget(budget_ms);
        std::printf("ensemble policy=%s positions=%d level=%d experts=%zu order=%s\n",
                    trusting ? "trusting" : "verified", 7, core_level,
                    1 + config.extra_experts.size(),
                    config.prefer_consensus ? "votes" : "first-member");
        // Positions taken from the real duel games: deliberately midgame, with
        // live threats on both sides.
        const std::vector<std::vector<Move>> samples = {
            {{7, 7}, {7, 8}, {8, 7}, {6, 7}},
            {{7, 7}, {7, 8}, {8, 7}, {6, 7}, {5, 6}, {4, 7}, {7, 6}, {4, 6}, {8, 6}, {6, 8},
             {8, 8}, {8, 9}, {6, 6}, {9, 6}, {5, 5}},
            {{7, 7}, {8, 8}, {6, 8}, {8, 6}, {6, 6}, {7, 8}, {9, 5}, {5, 9}, {9, 9}, {5, 5}},
            {{7, 7}, {6, 7}, {8, 8}, {8, 6}, {6, 8}, {7, 6}, {9, 7}, {5, 7}, {9, 8}, {5, 8}},
            {{7, 7}, {7, 8}, {6, 6}, {8, 8}, {6, 9}, {8, 6}, {9, 9}, {5, 5}, {10, 10}, {4, 4}},
            {{7, 7}, {8, 6}, {6, 6}, {8, 8}, {6, 10}, {8, 10}, {9, 5}, {5, 9}, {9, 10}, {5, 5}},
            {{7, 7}, {6, 8}, {8, 6}, {5, 9}, {9, 5}, {6, 6}, {10, 4}, {4, 10}, {7, 5}, {7, 9}},
        };
        for (size_t i = 0; i < samples.size(); ++i) {
            const std::vector<Move>& moves = samples[i];
            int board[gomoku::kMaxBoard][gomoku::kMaxBoard]{};
            for (size_t k = 0; k < moves.size(); ++k)
                board[moves[k].r][moves[k].c] = k % 2 == 0 ? 1 : 2;
            const int player = moves.size() % 2 == 0 ? 1 : 2;
            gomoku::EnsembleStats result = gomoku::choose_ensemble_move(
                board, kSize, moves, player, core_level, runtime_root_for_ensemble(), config,
                gomoku::EngineVersion::Rapfi2025);
            std::printf("position=%zu plies=%zu player=%d move=(%2d,%2d) source=%-16s "
                        "expert=%s(%d/%d answered, %d skipped) votes=%d overruled=%d illegal=%d "
                        "expert_vcf=%d local_vcf=%d depth=%d "
                        "expert_ms=%d local_ms=%d total_ms=%d\n",
                        i, moves.size(), player, result.move.r, result.move.c,
                        gomoku::ensemble_source_name(result.source),
                        result.expert_available ? "yes" : "no",
                        result.experts_answered, result.experts_launched,
                        result.experts_skipped,
                        result.chosen_votes, result.proof_overruled ? 1 : 0,
                        result.expert_illegal ? 1 : 0,
                        result.expert_loses_to_vcf ? 1 : 0, result.local_loses_to_vcf ? 1 : 0,
                        result.local_depth, result.expert_ms, result.local_ms, result.total_ms);
            std::fflush(stdout);
        }
        return 0;
    }
    const int level = level_for_budget(budget_ms);
    // Diagnostic mode: give the in-tree engine a different clock than the
    // opponent, to measure how much time it needs to match at equal quality.
    const int core_budget_ms = std::getenv("DUEL_CORE_BUDGET")
        ? std::atoi(std::getenv("DUEL_CORE_BUDGET")) : budget_ms;
    const int core_level = level_for_budget(core_budget_ms);
    if (core_level == 0) {
        std::fprintf(stderr, "DUEL_CORE_BUDGET must be 1000, 2000, 4000, 8000 or 16000\n");
        return 2;
    }
    if (level == 0) {
        std::fprintf(stderr, "BUDGET_MS must be 1000, 2000, 4000, 8000 or 16000 "
                             "(the in-tree engine's levels)\n");
        return 2;
    }
    if (pairs < 1 || pairs > 8 || threads < 1 || threads > 32) {
        std::fprintf(stderr, "PAIRS must be 1..8 and THREADS 1..32\n");
        return 2;
    }
    const bool mixture = engine_name == "mixture" || engine_name == "mixture-trusting";
    // In mixture mode the referee itself plays the mixture, so the opponent is
    // whichever engine DUEL_EXPERT names (default: rapfi).
    // In mixture mode the referee plays the mixture, so the opponent comes from
    // `DUEL_OPPONENT` (or `DUEL_EXPERT`, which also names the consulted expert).
    const char* opponent_env = std::getenv("DUEL_OPPONENT");
    if (!opponent_env) opponent_env = std::getenv("DUEL_EXPERT");
    const std::string expert_name = mixture ? (opponent_env ? opponent_env : "rapfi") : engine_name;
    const bool local_opponent = expert_name == "legacy" || expert_name == "v2" ||
                                expert_name == "self";
    const EngineSpec spec = make_spec(expert_name);
    if (spec.external && !std::filesystem::exists(spec.program)) {
        std::fprintf(stderr, "external engine not found: %s\n", spec.program.c_str());
        return 2;
    }
    // Symmetric A/B: both sides play the mixture, differing only in whether
    // expert moves are verified. CPU contention is then identical on both sides,
    // so the result isolates the guard rather than the scheduling.
    const bool symmetric_mixture = std::getenv("DUEL_SYMMETRIC_MIXTURE") != nullptr;

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

    gomoku::EnsembleConfig mixture_config;
    mixture_config.local_threads = threads;
    if (mixture) {
        mixture_config.policy = engine_name == "mixture-trusting"
            ? gomoku::EnsemblePolicy::Trusting : gomoku::EnsemblePolicy::Verified;
        // Give the expert the whole move clock by default: it is the strongest
        // member, and `DUEL_MIX_FRACTION` exists only to measure the cost of
        // splitting it.
        mixture_config.expert_budget_fraction = 1.0;
        mixture_config.expert_program = expert_program_for(spec);
        // Extra members of the council come from `DUEL_MIX_COUNCIL=alpha,xl`.
        // They run concurrently, so the council costs no extra wall clock.
        apply_ensemble_env(mixture_config);
    }
    int source_counts[5] = {0, 0, 0, 0, 0};
    int agreements = 0, mixture_moves = 0;
    // Expert behaviour over a whole game: a mixture that never gets an answer is
    // just the local engine wearing a longer name, and that must be visible.
    int expert_launched = 0, expert_answered = 0, expert_illegal = 0, expert_overruled = 0;
    int expert_timeout = 0;
    int expert_ms_max = 0; long expert_ms_total = 0;
    int core_wins = 0, opponent_wins = 0, draws = 0, voids = 0;
    int core_as_black = 0, black_games = 0, core_as_white = 0, white_games = 0;
    SideStats core_stats, opponent_stats;

    for (int pair = 0; pair < pairs; ++pair) {
        for (int swap = 0; swap < 2; ++swap) {
            const int core_player = swap == 0 ? 1 : 2;
            int board[gomoku::kMaxBoard][gomoku::kMaxBoard]{};
            std::vector<Move> moves;
            for (const Move& m : openings[pair % openings.size()]) {
                board[m.r][m.c] = moves.size() % 2 == 0 ? 1 : 2;
                moves.push_back(m);
            }
            GameResult result;
            std::string note;
            std::vector<Move> final_moves;  // kept for DUEL_MOVES post-mortems

#ifdef _WIN32
            ExternalSession* unused = nullptr;
            (void)unused;
#else
            std::unique_ptr<ExternalSession> session;
            if (spec.external && !symmetric_mixture) {
                session = std::make_unique<ExternalSession>(spec.program);
                if (!session->open(kSize, budget_ms)) {
                    std::fprintf(stderr, "failed to start %s\n", spec.program.c_str());
                    return 2;
                }
            }
#endif
            while (true) {
                if (static_cast<int>(moves.size()) >= kSize * kSize) break;
                const int player = moves.size() % 2 == 0 ? 1 : 2;
                const bool core_turn = player == core_player;
                Move move{-1, -1};
                const auto started = std::chrono::steady_clock::now();
                SideStats& stats = core_turn ? core_stats : opponent_stats;
                gomoku::EnsembleSource chosen_source = gomoku::EnsembleSource::LocalSearch;
                if (core_turn) {
                    if (mixture) {
                        gomoku::EnsembleStats ensemble = gomoku::choose_ensemble_move(
                            board, kSize, moves, player, core_level,
                            runtime_root_for_ensemble(), mixture_config,
                            gomoku::EngineVersion::Rapfi2025);
                        move = ensemble.move;
                        chosen_source = ensemble.source;
                        ++source_counts[static_cast<int>(chosen_source)];
                        ++mixture_moves;
                        if (ensemble.agreed) ++agreements;
                        expert_launched += ensemble.experts_launched;
                        expert_answered += ensemble.experts_answered;
                        if (ensemble.expert_illegal) ++expert_illegal;
                        expert_timeout += ensemble.experts_timeout;
                        if (ensemble.proof_overruled) ++expert_overruled;
                        expert_ms_max = std::max(expert_ms_max, ensemble.expert_ms);
                        expert_ms_total += ensemble.expert_ms;
                    } else {
                        gomoku::OpponentStyle style =
                            gomoku::analyze_opponent_style(kSize, moves, 3 - player);
                        Move last{-1, -1};
                        for (int i = static_cast<int>(moves.size()) - 1; i >= 0; --i)
                            if ((i % 2 == 0 ? 1 : 2) == player) { last = moves[i]; break; }
                        // The core side is always the current engine; `engine_name`
                        // only selects its opponent.
                        move = gomoku::choose_ai_move(board, kSize, player, core_level, &style,
                                                     threads, last);
                    }
                } else if (symmetric_mixture) {
                    gomoku::EnsembleConfig other = mixture_config;
                    other.policy = mixture_config.policy == gomoku::EnsemblePolicy::Verified
                        ? gomoku::EnsemblePolicy::Trusting
                        : gomoku::EnsemblePolicy::Verified;
                    gomoku::EnsembleStats ensemble = gomoku::choose_ensemble_move(
                        board, kSize, moves, player, core_level,
                        runtime_root_for_ensemble(), other,
                        gomoku::EngineVersion::Rapfi2025);
                    move = ensemble.move;
                } else if (local_opponent) {
                    const EngineVersion version = expert_name == "legacy"
                        ? EngineVersion::LegacyV1 : EngineVersion::StrengthenedV2;
                    move = gomoku::choose_versioned_move(board, kSize, player, level, version);
                } else {
#ifndef _WIN32
                    bool timed_out = false, protocol_error = false;
                    move = session->think(moves, player, budget_ms,
                                          budget_ms * 4 + 20000, timed_out, protocol_error);
                    if (timed_out) ++stats.timeouts;
                    if (protocol_error) ++stats.errors;
#endif
                }
                const int elapsed = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - started).count());
                stats.total_ms += elapsed;
                ++stats.turns;
                stats.max_ms = std::max(stats.max_ms, elapsed);
                // Over budget by more than a quarter is a real overrun, not
                // scheduling jitter; on a Gomocup server it would be a forfeit.
                if (core_turn && elapsed > budget_ms * 5 / 4) ++stats.timeouts;
                if (!gomoku::is_legal_move(board, kSize, player, move)) {
                    ++stats.illegal;
                    result.winner = 3 - player;
                    // A game the opponent forfeits to a crash, a protocol
                    // violation or a timeout is not evidence of strength, so it
                    // is excluded from the score rather than counted as a win.
                    result.void_game = !core_turn;
                    note = (core_turn ? "core" : "opponent") + std::string(" played an illegal move");
                    break;
                }
                board[move.r][move.c] = player;
                moves.push_back(move);
                if (gomoku::check_win_at(board, kSize, move.r, move.c, player)) {
                    if (player == 1 && overline_for_black(board, kSize, move)) {
                        result.winner = 2;
                        note = "black overline";
                    } else {
                        result.winner = player;
                    }
                    break;
                }
            }
            result.plies = static_cast<int>(moves.size());
            final_moves = moves;
            if (result.void_game) {
                ++voids;
            } else {
                if (result.winner == core_player) ++core_wins;
                else if (result.winner == 0) ++draws;
                else ++opponent_wins;
                if (core_player == 1) { ++black_games; if (result.winner == 1) ++core_as_black; }
                else { ++white_games; if (result.winner == 2) ++core_as_white; }
            }
            if (mixture) {
                std::printf("sources local-tactics=%d local-vcf=%d local-search=%d "
                            "expert-delegated=%d expert-corrected=%d\n",
                            source_counts[0], source_counts[1], source_counts[2],
                            source_counts[3], source_counts[4]);
            }
            std::printf("game=%d core=%s winner=%s(%d)%s plies=%d core_budget=%dms bpudget=%dms "
                        "core_avg=%lldms core_max=%dms opp_avg=%lldms opp_max=%dms %s\n",
                        pair * 2 + swap + 1, core_player == 1 ? "black" : "white",
                        result.winner == 0 ? "draw" : (result.winner == core_player ? "core" : "opponent"),
                        result.winner, result.void_game ? " VOID" : "", result.plies,
                        core_budget_ms, budget_ms,
                        core_stats.turns ? core_stats.total_ms / core_stats.turns : 0,
                        core_stats.max_ms,
                        opponent_stats.turns ? opponent_stats.total_ms / opponent_stats.turns : 0,
                        opponent_stats.max_ms,
                        note.empty() ? "" : note.c_str());
            if (std::getenv("DUEL_MOVES")) {
                std::printf("moves=");
                for (size_t i = 0; i < final_moves.size(); ++i)
                    std::printf("%s%d,%d", i ? " " : "", final_moves[i].r, final_moves[i].c);
                std::printf("\n");
            }
            std::fflush(stdout);
        }
    }
    if (mixture && mixture_moves > 0) {
        std::printf("agreement expert==local on %d/%d moves (%.0f%%)\n", agreements, mixture_moves,
                    100.0 * agreements / mixture_moves);
        std::printf("experts launched=%d answered=%d timed_out=%d illegal=%d overruled=%d "
                    "avg_ms=%ld max_ms=%d\n",
                    expert_launched, expert_answered, expert_timeout, expert_illegal,
                    expert_overruled, expert_ms_total / mixture_moves, expert_ms_max);
    }
    std::printf("summary opponent=%s budget=%dms pairs=%d core=%d opponent=%d draws=%d void=%d "
                "core_as_black=%d/%d core_as_white=%d/%d core_timeouts=%d opp_timeouts=%d "
                "core_illegal=%d opp_illegal=%d\n",
                mixture ? ("mixture vs " + expert_name).c_str() : engine_name.c_str(),
                budget_ms, pairs, core_wins, opponent_wins, draws, voids,
                core_as_black, black_games, core_as_white, white_games,
                core_stats.timeouts, opponent_stats.timeouts,
                core_stats.illegal, opponent_stats.illegal);
    return opponent_wins == 0 && draws == 0 ? 0 : 1;
#else
    (void)argc; (void)argv;
    std::fprintf(stderr, "duel is POSIX-only; use engine_benchmark on Windows\n");
    return 2;
#endif
}
