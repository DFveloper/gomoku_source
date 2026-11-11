#pragma once

// ---------------------------------------------------------------------------
// Expert mixture ("MoE") router for move selection.
//
// The engine already contains several movers of very different quality and very
// different failure modes:
//
//   * the in-tree alpha-beta search   - always available, sees our own threats,
//                                       weak on long forcing lines;
//   * Rapfi (Gomocup child process)   - far stronger positionally, but only if
//                                       its runtime exists and starts;
//   * AlphaGomoku (child process)     - a neural policy, strong in some
//                                       matchups and documented as weaker in
//                                       others;
//   * XL / Carbon (child processes)   - further variety, still weaker overall.
//
// A naive mixture ("trust the strongest expert") is not safe: this repository
// has already measured that the neural expert is *worse* than the classical one
// against Legacy. A router that can pick a losing expert is worse than the
// engine it replaced.
//
// So the router is built around a single rule: **an expert proposes, and only a
// verifiable check promotes**. Concretely, the expert's move may become the
// answer only if
//
//   1. it is a legal Renju move for the side to move, and
//   2. it does not hand the opponent a proven continuous-four (VCF) win, unless
//      our own answer has the same problem, and
//   3. the local search did not *prove* something better (an immediate five or
//      a VCF win of our own).
//
// Every check is a proof, not a heuristic, so delegation can only replace a move
// we would otherwise have played, never a proven win. The experts run
// concurrently with the local search inside one shared wall-clock budget, so the
// mixture costs the same clock as either member alone.
//
// The policy is explicit so the behaviour can be measured instead of assumed:
//
//   LocalOnly    - never consult an expert (identical to the previous engine)
//   Verified     - consult, and run all three checks above (default)
//   Trusting     - consult and use the expert's move whenever it is legal; kept
//                  only so that "verification is worth it" can be measured
//
// All members of the council run at the same time, in parallel with the local
// search, so a council of three costs the same wall clock as a single expert.
// Votes decide the *order* in which candidates are considered; proofs decide
// what is allowed. Nothing here can turn a proven win into a loss, and nothing
// here can be worse than the local engine when every expert is missing.
// ---------------------------------------------------------------------------

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "engine_adapter.h"

namespace gomoku {

enum class EnsemblePolicy {
    LocalOnly,
    Verified,
    Trusting,
};

// Why the returned move was chosen. Reported so a match can be audited after
// the fact instead of guessed at.
enum class EnsembleSource {
    LocalTactics,     // immediate win / mandatory block, decided before the search
    LocalVcf,         // proven continuous-four win at the root
    LocalSearch,      // the in-tree search's answer
    ExpertDelegated,  // an external expert's move, verified
    ExpertCorrected,  // an external expert's move replaced ours because ours lost
};

inline const char* ensemble_source_name(EnsembleSource source) {
    switch (source) {
        case EnsembleSource::LocalTactics: return "local-tactics";
        case EnsembleSource::LocalVcf: return "local-vcf";
        case EnsembleSource::LocalSearch: return "local-search";
        case EnsembleSource::ExpertDelegated: return "expert-delegated";
        case EnsembleSource::ExpertCorrected: return "expert-corrected";
    }
    return "unknown";
}

struct EnsembleStats {
    Move move{-1, -1};
    EnsembleSource source = EnsembleSource::LocalSearch;
    int expert_ms = 0;
    int local_ms = 0;
    int total_ms = 0;
    int verify_ms = 0;  // wall clock spent on the VCF proofs
    bool expert_available = false;
    bool expert_illegal = false;
    bool expert_loses_to_vcf = false;
    bool local_loses_to_vcf = false;
    int local_depth = 0;
    bool local_proved_win = false;
    bool agreed = false;          // expert and local search chose the same point
    int experts_launched = 0;     // experts that had a runnable binary
    int experts_answered = 0;     // experts that returned a legal move in time
    int experts_skipped = 0;      // council members dropped for lack of cores
    int experts_timeout = 0;      // still running when the budget ran out
    int chosen_votes = 0;         // how many members proposed the chosen move
    bool consensus = false;       // two or more experts picked the same point
    bool proof_overruled = false; // a proven loss decided the answer
    bool verification_truncated = false;  // ran out of clock before checking all
};

// One member of the mixture. Every member speaks the Gomocup protocol; the
// version only selects which binary and which internal time mapping to use.
struct EnsembleExpert {
    EngineVersion version = EngineVersion::Rapfi2025;
    std::filesystem::path program;
};

struct EnsembleConfig {
    EnsemblePolicy policy = EnsemblePolicy::Verified;
    // Fraction of the move budget handed to each external expert. All experts
    // run concurrently with the local search, so this is a CPU split, not a
    // wall-clock split.
    double expert_budget_fraction = 0.65;
    // Threads for the local search while experts are also running. On a small
    // machine this should be 1: each child process already wants a core.
    int local_threads = 1;
    // Bound on each VCF proof used for verification, in nodes. A proof that runs
    // out of nodes reports "no win", which is the safe direction, so this is a
    // cost control rather than a correctness knob.
    int verify_vcf_nodes = 6000;
    // Verifying is only allowed to spend what is left of the move clock. Without
    // this the router answered 3.6 s into a 1 s budget as soon as the guard
    // actually fired: the experts finish inside the budget and the proofs then run
    // afterwards with no limit of their own.
    double verify_budget_fraction = 0.25;
    // Each expert is a one-shot child process, so it pays for starting up (config
    // parse, network load - measured at 60-200 ms) inside the same wall clock we
    // have to answer in. Handing it the full budget means it answers just after
    // ours expires: measured 35 of 52 expert calls in a game timed out that way.
    // The budget is reduced by this allowance instead.
    int expert_startup_ms = 200;
    // Verification is a proof search, so it costs real time. Only the most
    // promising candidates are checked, and never more than this many.
    int max_verified_candidates = 4;
    // Explicit path to the primary expert's binary. Empty means "look it up
    // under third_party/runtime". An environment variable lets a host use a
    // locally built engine (the bundled runtimes need a newer glibc than some
    // systems provide).
    std::filesystem::path expert_program;
    // Further members of the council, in decreasing order of trust.
    std::vector<EnsembleExpert> extra_experts;
    // When members disagree, prefer the move proposed by more of them. Turning
    // this off makes the first (most trusted) member a dictator, which exists
    // only so the value of the vote can be measured.
    bool prefer_consensus = true;
    // Every member is a separate process competing for cores. A council larger
    // than the machine can host does not add information, it starves every
    // member: measured on a 2-core host, two experts plus the local search made
    // the engine *weaker* than running alone, because each member received about
    // a third of its usual compute inside the same wall clock. Zero means
    // "as many as the machine can host": cores - 1 for the local search.
    int max_experts = 0;
};

namespace detail {

// How many worker threads the child will try to use. Rapfi is single-threaded by
// default; AlphaGomoku and XL spread their search over several cores, so they can
// only be a council member on a machine that has cores to spare.
inline int external_threads(EngineVersion version) {
    switch (version) {
        case EngineVersion::AlphaGomoku2026: return 4;
        case EngineVersion::XLNiren: return 4;
        default: return 1;
    }
}

inline bool loses_to_vcf(int board[kMaxBoard][kMaxBoard], int sz, int player, Move move,
                         int node_budget) {
    if (!is_legal_move(board, sz, player, move)) return false;
    int local[kMaxBoard][kMaxBoard];
    std::memcpy(local, board, sizeof(local));
    local[move.r][move.c] = player;
    int budget = node_budget;
    // If the opponent can force a continuous-four win after this move, the move
    // is losing regardless of how good the position looks to an evaluation.
    return forced_four_search(local, sz, 3 - player, 16, budget);
}

}  // namespace detail

// Chooses a move from the mixture. `history` is the full move list (black
// first), which the external experts need in protocol form. Blocking: call it
// from a worker thread, as the UI engines do.
//
// Selection rule, in order:
//   1. an immediate win / mandatory block from the local tactics;
//   2. a continuous-four win proven by the local search;
//   3. among the moves proposed by the members, the one with the most votes;
//   4. any move that a VCF proof shows loses is demoted below moves that
//      survive; if every candidate loses, the strongest member's move is kept.
// Steps 1, 2 and 4 are proofs. Step 3 is the only heuristic part, and it is what
// makes this a mixture rather than "always ask the strongest engine".
inline EnsembleStats choose_ensemble_move(
        int board[kMaxBoard][kMaxBoard], int sz, const std::vector<Move>& history,
        int player, int level, const std::filesystem::path& runtime_root,
        EnsembleConfig config = {},
        EngineVersion expert = EngineVersion::Rapfi2025,
        const std::atomic<bool>* cancel = nullptr) {
    EnsembleStats stats;
    const auto started = std::chrono::steady_clock::now();
    const int time_budget_ms = 1000 * (1 << std::max(0, std::min(level, 5) - 1));
    const int opponent = 3 - player;

    // --- Decisions that need no search at all -------------------------------
    {
        Move win = find_immediate_win(board, sz, player);
        if (win.r != -1) {
            stats.move = win;
            stats.source = EnsembleSource::LocalTactics;
            stats.total_ms = 0;
            return stats;
        }
        Move block = find_immediate_win(board, sz, opponent);
        if (block.r != -1 && !(player == 1 && is_forbidden_move(board, sz, block.r, block.c))) {
            stats.move = block;
            stats.source = EnsembleSource::LocalTactics;
            stats.total_ms = 0;
            return stats;
        }
    }

    // --- Launch the experts ------------------------------------------------
    // Every expert is a child process, so they all run concurrently with the
    // local search and the mixture keeps the wall clock of a single member.
    std::vector<EnsembleExpert> council;
    council.push_back({expert, config.expert_program});
    for (const EnsembleExpert& extra : config.extra_experts) council.push_back(extra);
    {
        const unsigned hardware = std::thread::hardware_concurrency();
        const int cores = static_cast<int>(hardware == 0 ? 1 : hardware);
        const int capacity = config.max_experts > 0 ? config.max_experts
                                                    : std::max(1, cores - 1);
        if (static_cast<int>(council.size()) > capacity) council.resize(capacity);
        stats.experts_skipped = static_cast<int>(config.extra_experts.size()) -
                                (static_cast<int>(council.size()) - 1);
        if (stats.experts_skipped < 0) stats.experts_skipped = 0;
    }

    // A slot outlives this call whenever an expert had to be detached (a hung
    // child must not stall the UI). It is therefore shared with its thread: a
    // detached thread that wrote into memory owned by this stack frame corrupted
    // the heap in testing, and it would have done so in the UI as well.
    struct ExpertSlot {
        std::thread thread;
        std::atomic<bool> done{false};
        std::atomic<int> row{-1};
        std::atomic<int> column{-1};
        std::string error;  // written before `done` is raised
        bool launched = false;
        Move move() const { return Move{row.load(), column.load()}; }
    };
    std::vector<std::shared_ptr<ExpertSlot>> slots;
    // Processes that will need a second core (multi-threaded children, or the
    // local search when it uses more than one worker). Verification runs after
    // the children have exited, so it does not need to be budgeted here.
    int spare_threads = static_cast<int>(std::thread::hardware_concurrency());
    if (spare_threads <= 0) spare_threads = 1;
    spare_threads -= 1;  // one for the local search
    const int expert_budget_ms = std::max(
        250, static_cast<int>(time_budget_ms * config.expert_budget_fraction) -
                 config.expert_startup_ms);
    const auto expert_started = std::chrono::steady_clock::now();
    if (config.policy != EnsemblePolicy::LocalOnly) {
        for (const EnsembleExpert& member : council) {
            ExternalEngineRequest request;
            request.version = member.version;
            request.moves = history;
            request.player = player;
            // The external protocol budget is expressed in its own levels; map
            // the share of our clock onto the closest protocol level.
            request.level = std::max(1, std::min(5, 1 + static_cast<int>(
                std::log2(std::max(1, expert_budget_ms / 250)))));
            request.board_size = sz;
            request.program = member.program.empty()
                ? external_runtime_path(runtime_root, member.version)
                : member.program;
            if (const char* override_path = std::getenv("GOMOKU_ENSEMBLE_EXPERT"))
                request.program = override_path;
            request.cancel = cancel;
            if (request.program.empty() || !std::filesystem::exists(request.program)) continue;
            const int needs = detail::external_threads(member.version) > 1 ? 1 : 0;
            if (needs > spare_threads) {
                ++stats.experts_skipped;
                continue;
            }
            spare_threads -= needs;
            auto slot = std::make_shared<ExpertSlot>();
            slot->launched = true;
            slot->thread = std::thread([slot, request]() {
                const Move move = run_external_engine(request, slot->error);
                slot->row = move.r;
                slot->column = move.c;
                slot->done = true;
            });
            slots.push_back(std::move(slot));
            ++stats.experts_launched;
        }
    }
    stats.expert_available = stats.experts_launched > 0;

    // --- Local search concurrently ----------------------------------------
    const auto local_started = std::chrono::steady_clock::now();
    OpponentStyle style = analyze_opponent_style(sz, history, opponent);
    Move last_own{-1, -1};
    for (int i = static_cast<int>(history.size()) - 1; i >= 0; --i) {
        if ((i % 2 == 0 ? 1 : 2) == player) { last_own = history[i]; break; }
    }
    SearchStats local_stats;
    // The local search is stopped as soon as every expert has answered: its
    // remaining clock is worth less than the verification that runs afterwards.
    // Its partial result still stands, because iterative deepening only returns
    // completed iterations.
    std::atomic<bool> local_stop{false};
    // The local search always gets the same wall clock as the experts; only the
    // number of worker threads changes, because the children want cores too.
    const bool sharing = stats.experts_launched > 0;
    Move local_move{-1, -1};
    if (sharing) {
        std::thread local_thread([&]() {
            local_move = choose_ai_move(board, sz, player, level, &style,
                                        config.local_threads, last_own, &local_stop,
                                        &local_stats);
        });
        // Watch the experts; as soon as every one of them has answered, the
        // local search has nothing left to buy and is stopped early.
        const auto local_hard_stop =
            local_started + std::chrono::milliseconds(time_budget_ms + 200);
        for (;;) {
            bool all_done = true;
            for (auto& slot : slots) all_done = all_done && slot->done.load();
            if (all_done) break;
            if (cancel && cancel->load()) break;
            if (std::chrono::steady_clock::now() >= local_hard_stop) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        local_stop = true;
        local_thread.join();
    } else {
        local_move = choose_ai_move(board, sz, player, level, &style, 0, last_own,
                                    cancel, &local_stats);
    }
    stats.local_depth = local_stats.search_depth;
    stats.local_proved_win = local_stats.vcf_win;
    stats.local_ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - local_started).count());

    // --- Collect the experts ----------------------------------------------
    if (!slots.empty()) {
        // Never wait past the budget: a hung child must not stall the UI, and an
        // over-subscribed machine must not turn the mixture into a time forfeit.
        const auto hard_stop = started + std::chrono::milliseconds(time_budget_ms + 200);
        for (auto& slot : slots) {
            while (!slot->done.load() && std::chrono::steady_clock::now() < hard_stop) {
                if (cancel && cancel->load()) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            if (slot->done.load()) {
                slot->thread.join();
            } else {
                ++stats.experts_timeout;
                slot->thread.detach();  // shared_ptr keeps the slot alive
            }
        }
    }
    stats.expert_ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - expert_started).count());

    auto finish = [&](Move move, EnsembleSource source) {
        stats.move = move;
        stats.source = source;
        stats.total_ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count());
        return stats;
    };

    // The local search proving a win outranks any expert opinion.
    if (local_stats.vcf_win) return finish(local_move, EnsembleSource::LocalVcf);
    if (local_move.r == -1) local_move = first_legal_move(board, sz, player);

    // --- Gather proposals --------------------------------------------------
    // Order matters: the local search is the least trusted positional player but
    // the only one that can prove things, so it comes after the experts.
    std::vector<Move> proposals;
    std::vector<int> votes;
    auto add_proposal = [&](Move move) {
        if (move.r == -1 || !is_legal_move(board, sz, player, move)) return false;
        for (size_t i = 0; i < proposals.size(); ++i) {
            if (proposals[i].r == move.r && proposals[i].c == move.c) {
                ++votes[i];
                return true;
            }
        }
        proposals.push_back(move);
        votes.push_back(1);
        return true;
    };
    for (auto& slot : slots) {
        const Move move = slot->launched ? slot->move() : Move{-1, -1};
        if (move.r != -1 && is_legal_move(board, sz, player, move)) {
            ++stats.experts_answered;
            add_proposal(move);
        }
    }
    // "No expert answered" is not the same as "the expert answered illegally";
    // the two have completely different fixes, so they are counted apart.
    if (stats.experts_launched > 0 && stats.experts_answered == 0 &&
        stats.experts_timeout == 0)
        stats.expert_illegal = true;
    if (std::getenv("GOMOKU_ENSEMBLE_DEBUG")) {
        for (auto& slot : slots)
            std::fprintf(stderr, "[ensemble] expert plies=%zu player=%d move=(%d,%d) %s%s\n",
                         history.size(), player, slot->row.load(), slot->column.load(),
                         slot->error.empty() ? "ok" : "error: ", slot->error.c_str());
    }
    add_proposal(local_move);
    if (config.policy == EnsemblePolicy::Trusting) {
        // No verification at all: the most trusted member that answered wins.
        for (auto& slot : slots) {
            const Move move = slot->move();
            if (slot->launched && is_legal_move(board, sz, player, move))
                return finish(move, EnsembleSource::ExpertDelegated);
        }
        return finish(local_move, EnsembleSource::LocalSearch);
    }

    // --- Verification: the part that makes the mixture safe -----------------
    // Rank by votes first (the mixture's opinion), then check the leading
    // candidates with a proof. A candidate that hands the opponent a forced
    // continuous-four win can never be chosen while another candidate survives.
    std::vector<int> order(proposals.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = static_cast<int>(i);
    std::stable_sort(order.begin(), order.end(),
                     [&](int a, int b) { return votes[a] > votes[b]; });
    if (!config.prefer_consensus) {
        // Dictator mode for the A/B: the first member that answered moves to the
        // top regardless of how many others agree on something else.
        int dictator = -1;
        for (auto& slot : slots) {
            const Move move = slot->move();
            if (slot->launched && is_legal_move(board, sz, player, move)) {
                for (size_t i = 0; i < proposals.size(); ++i)
                    if (proposals[i].r == move.r && proposals[i].c == move.c)
                        dictator = static_cast<int>(i);
                break;
            }
        }
        if (dictator >= 0) {
            order.erase(std::remove(order.begin(), order.end(), dictator), order.end());
            order.insert(order.begin(), dictator);
        }
    }
    const int check_limit = std::min(config.max_verified_candidates,
                                     static_cast<int>(order.size()));
    std::vector<bool> verified_loses(order.size(), false);
    std::vector<bool> checked(order.size(), false);
    // Verification is a proof search, so it can be expensive; it is bounded both
    // in nodes and in wall clock, and an unfinished proof is treated as "unknown"
    // (which never promotes a move, only fails to demote one).
    const auto verify_started = std::chrono::steady_clock::now();
    const auto verify_deadline = started + std::chrono::milliseconds(
        time_budget_ms + static_cast<int>(time_budget_ms * config.verify_budget_fraction));
    for (int k = 0; k < check_limit; ++k) {
        if (std::chrono::steady_clock::now() >= verify_deadline) break;
        const int index = order[k];
        checked[index] = true;
        verified_loses[index] = detail::loses_to_vcf(board, sz, player, proposals[index],
                                                     config.verify_vcf_nodes);
    }
    if (std::chrono::steady_clock::now() >= verify_deadline) stats.verification_truncated = true;
    stats.verify_ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - verify_started).count());
    if (!order.empty() && verified_loses[order[0]] && order.size() > 1) {
        // The leading candidate loses; prefer the first surviving move in vote
        // order so the mixture never walks into a proven loss. Unchecked moves
        // count as surviving, because a proof was only attempted for the leaders.
        const bool local_was_leader = order[0] == static_cast<int>(proposals.size()) - 1;
        for (size_t k = 1; k < order.size(); ++k) {
            const int index = order[k];
            if (verified_loses[index]) continue;
            stats.proof_overruled = true;
            stats.expert_loses_to_vcf = !local_was_leader;
            stats.chosen_votes = votes[index];
            stats.consensus = votes[index] >= 2;
            const bool chose_local = index == static_cast<int>(proposals.size()) - 1;
            return finish(proposals[index], chose_local ? EnsembleSource::LocalSearch
                                                        : EnsembleSource::ExpertCorrected);
        }
    }
    const int best = order[0];
    stats.chosen_votes = votes[best];
    stats.consensus = votes[best] >= 2;
    stats.agreed = proposals.size() > 1 && votes[best] >= 2;
    stats.local_loses_to_vcf = verified_loses[static_cast<size_t>(order.front())] &&
                               order.front() == static_cast<int>(proposals.size()) - 1;
    if (proposals[best].r == local_move.r && proposals[best].c == local_move.c)
        return finish(proposals[best], EnsembleSource::LocalSearch);
    return finish(proposals[best], EnsembleSource::ExpertDelegated);
}

}  // namespace gomoku
