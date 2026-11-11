#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <iostream>
#include <string>
#include <sstream>
#include <vector>

#include "gomoku_core.h"
#include "engine_adapter.h"

namespace {

int board[gomoku::kMaxBoard][gomoku::kMaxBoard];
int sz = 15;
int turn = 1;
bool game_over = false;
int winner = 0;
int default_depth = 4;
FILE* agent_log = nullptr;
std::vector<gomoku::Move> move_history;

gomoku::Move last_history_move_for(int p) {
    for (int i = static_cast<int>(move_history.size()) - 1; i >= 0; --i) {
        if (((i % 2 == 0) ? 1 : 2) == p) return move_history[i];
    }
    return {-1, -1};
}

void clear_board() {
    std::memset(board, 0, sizeof(board));
    turn = 1;
    game_over = false;
    winner = 0;
    move_history.clear();
}

void log_line(const std::string& s) {
    if (!agent_log) return;
    std::fprintf(agent_log, "%s\n", s.c_str());
    std::fflush(agent_log);
}

const char* status_cstr() {
    if (game_over) {
        if (winner == 1) return "BLACK_WINS";
        if (winner == 2) return "WHITE_WINS";
        return "DRAW";
    }
    return turn == 1 ? "BLACK_TO_MOVE" : "WHITE_TO_MOVE";
}

void log_board_snapshot(const char* tag) {
    if (!agent_log) return;
    std::fprintf(agent_log, "[%s] SIZE %d TURN %s STATUS %s\n", tag, sz, turn == 1 ? "BLACK" : "WHITE", status_cstr());
    for (int r = 0; r < sz; r++) {
        for (int c = 0; c < sz; c++) {
            char ch = '.';
            if (board[r][c] == 1) ch = 'X';
            else if (board[r][c] == 2) ch = 'O';
            std::fputc(ch, agent_log);
        }
        std::fputc('\n', agent_log);
    }
    std::fflush(agent_log);
}

void print_board() {
    std::printf("   ");
    for (int c = 0; c < sz; c++) std::printf("%2d", c);
    std::printf("\n");
    for (int r = 0; r < sz; r++) {
        std::printf("%2d ", r);
        for (int c = 0; c < sz; c++) {
            char ch = '.';
            if (board[r][c] == 1) ch = 'X';
            else if (board[r][c] == 2) ch = 'O';
            std::printf(" %c", ch);
        }
        std::printf("\n");
    }
}

std::string status() {
    return status_cstr();
}

void print_state() {
    std::printf("SIZE %d\n", sz);
    std::printf("TURN %s\n", turn == 1 ? "BLACK" : "WHITE");
    std::printf("STATUS %s\n", status().c_str());
    std::printf("BOARD\n");
    print_board();
}

void apply_move(int r, int c, int p) {
    board[r][c] = p;
    move_history.push_back({r, c});
    if (gomoku::check_win_at(board, sz, r, c, p)) {
        game_over = true;
        winner = p;
    } else if (move_history.size() >= static_cast<size_t>(sz * sz)) {
        game_over = true;
        winner = 0;
    } else {
        turn = 3 - p;
    }
}

bool expect_move(const char* name, gomoku::Move got, std::initializer_list<gomoku::Move> ok_moves) {
    for (gomoku::Move m : ok_moves) {
        if (got.r == m.r && got.c == m.c) {
            std::printf("[PASS] %s -> %d %d\n", name, got.r, got.c);
            return true;
        }
    }
    std::printf("[FAIL] %s -> got %d %d\n", name, got.r, got.c);
    return false;
}

bool run_tests() {
    bool pass = true;

    clear_board();
    board[7][5] = board[7][6] = board[7][7] = board[7][8] = 1;
    pass &= expect_move("block straight four", gomoku::choose_ai_move(board, sz, 2, 3), {{7, 4}, {7, 9}});

    clear_board();
    board[8][3] = board[8][4] = board[8][5] = board[8][6] = 2;
    pass &= expect_move("take immediate win", gomoku::choose_ai_move(board, sz, 2, 3), {{8, 2}, {8, 7}});

    clear_board();
    board[7][5] = board[7][6] = board[7][8] = board[7][9] = 1;
    pass &= expect_move("block broken four", gomoku::choose_ai_move(board, sz, 2, 3), {{7, 7}});

    clear_board();
    for (int c = 3; c <= 7; c++) board[4][c] = 1;
    pass &= gomoku::check_win_at(board, sz, 4, 5, 1);
    std::printf("[%s] last-move win check\n", pass ? "PASS" : "FAIL");

    clear_board();
    board[7][6] = board[7][8] = board[6][7] = board[8][7] = 1;
    pass &= gomoku::is_forbidden_move(board, sz, 7, 7);
    std::printf("[%s] double-three forbidden check\n", pass ? "PASS" : "FAIL");

    clear_board();
    board[7][7] = board[7][8] = 1;
    board[6][7] = board[8][8] = 2;
    int black_eval = gomoku::static_eval(board, sz, 1);
    int white_eval = gomoku::static_eval(board, sz, 2);
    bool symmetric = black_eval == -white_eval;
    pass &= symmetric;
    std::printf("[%s] antisymmetric long-game evaluation\n", symmetric ? "PASS" : "FAIL");

    gomoku::OpponentStyle attacking_style, defending_style;
    for (int sample = 0; sample < 5; ++sample) {
        clear_board();
        int row = 3 + sample;
        board[row][4] = board[row][5] = board[row][6] = 1;
        gomoku::observe_opponent_move(attacking_style, board, sz, {row, 7});
        clear_board();
        board[row][4] = board[row][5] = board[row][6] = 2;
        gomoku::observe_opponent_move(defending_style, board, sz, {row, 7});
    }
    bool style_learned = attacking_style.attack_preference > attacking_style.defense_preference &&
                         defending_style.defense_preference > defending_style.attack_preference &&
                         attacking_style.confidence() > 0.5;
    pass &= style_learned;
    std::printf("[%s] opponent attack/defense style learning\n", style_learned ? "PASS" : "FAIL");

    clear_board();
    board[7][5] = board[7][6] = 1;
    board[10][5] = board[10][6] = 2;
    gomoku::OpponentStyle aggressive, defensive;
    aggressive.observations = defensive.observations = 8;
    aggressive.attack_preference = 1.0;
    aggressive.defense_preference = 0.0;
    defensive.attack_preference = 0.0;
    defensive.defense_preference = 1.0;
    bool style_applied =
        gomoku::style_move_bonus(board, sz, 1, {7, 7}, &aggressive) >
            gomoku::style_move_bonus(board, sz, 1, {7, 7}, &defensive) &&
        gomoku::style_move_bonus(board, sz, 1, {10, 7}, &defensive) >
            gomoku::style_move_bonus(board, sz, 1, {10, 7}, &aggressive);
    pass &= style_applied;
    std::printf("[%s] style-conditioned tree move ordering\n", style_applied ? "PASS" : "FAIL");

    auto baseline = gomoku::ranked_candidates(board, sz, 1, 8, nullptr);
    auto modeled = gomoku::ranked_candidates(board, sz, 1, 8, &aggressive);
    bool baseline_preserved = true;
    for (int i = 0; i < 6 && i < static_cast<int>(baseline.size()); ++i) {
        bool found = false;
        for (const auto& item : modeled) {
            if (item.second.r == baseline[i].second.r && item.second.c == baseline[i].second.c) found = true;
        }
        baseline_preserved &= found;
    }
    pass &= baseline_preserved;
    std::printf("[%s] style model preserves safe baseline replies\n", baseline_preserved ? "PASS" : "FAIL");

    clear_board();
    board[7][7] = board[7][8] = 2;
    board[7][6] = board[7][10] = 1;
    board[10][7] = board[10][8] = 2;
    int dead_penalty = gomoku::stale_continuation_penalty(board, sz, 2, {7, 9}, {7, 8});
    int live_penalty = gomoku::stale_continuation_penalty(board, sz, 2, {10, 9}, {7, 8});
    bool stale_abandoned = dead_penalty > 0 && live_penalty == 0;
    pass &= stale_abandoned;
    std::printf("[%s] abandon dead continuation\n", stale_abandoned ? "PASS" : "FAIL");

    // Root parallelism must not corrupt the answer.  Iterative deepening
    // deliberately lets the reached depth depend on how much time the workers
    // had, so "same move for every thread count" is no longer the invariant to
    // test.  The invariant that still holds, and that a worker race or a lost
    // root would break, is: whatever depth the engine reports, the move it
    // returns is the plain argmax of a clean single-threaded pass at that depth.
    auto argmax_at_depth = [&](int player, int depth) {
        auto roots = gomoku::ranked_candidates(board, sz, player, 18, nullptr);
        int best_value = -210000000, best_key = -1;
        gomoku::Move best{-1, -1};
        const int opponent = 3 - player;
        for (const auto& item : roots) {
            int local[gomoku::kMaxBoard][gomoku::kMaxBoard];
            std::memcpy(local, board, sizeof(local));
            const gomoku::Move move = item.second;
            local[move.r][move.c] = player;
            int value;
            if (gomoku::check_win_at(local, sz, move.r, move.c, player)) {
                value = 200000000 + depth;
            } else if (gomoku::has_forced_four_win(local, sz, opponent, 6, 320)) {
                value = -190000000 - depth;
            } else {
                int scratch[gomoku::kMaxBoard][gomoku::kMaxBoard];
                std::memcpy(scratch, local, sizeof(scratch));
                const std::uint64_t hash = gomoku::position_hash(scratch, sz, opponent);
                value = -gomoku::negamax(scratch, sz, opponent, depth, -210000000, 210000000,
                                         move, player, nullptr, 2, nullptr, nullptr, nullptr,
                                         hash, nullptr);
            }
            if (value > best_value || (value == best_value && item.first > best_key)) {
                best_value = value;
                best = move;
                best_key = item.first;
            }
        }
        return best;
    };
    clear_board();
    board[7][7] = board[8][8] = 1;
    board[7][8] = board[8][7] = 2;
    bool parallel_sound = gomoku::kMaxSearchThreads == 32;
    int parallel_checked = 0;
    for (int threads : {1, 2, 4, 32}) {
        gomoku::SearchStats stats;
        const gomoku::Move move = gomoku::choose_ai_move(board, sz, 2, 1, nullptr, threads,
                                                         {-1, -1}, nullptr, &stats);
        if (move.r == -1 || !stats.search_depth_complete || stats.search_depth == 0) continue;
        const gomoku::Move expected = argmax_at_depth(2, stats.search_depth);
        parallel_sound = parallel_sound && move.r == expected.r && move.c == expected.c;
        ++parallel_checked;
    }
    pass &= parallel_sound && parallel_checked > 0;
    std::printf("[%s] thread-count-independent root answer (%d thread settings)\n",
                parallel_sound && parallel_checked > 0 ? "PASS" : "FAIL", parallel_checked);

    // Iterative deepening is allowed to reach a different depth on a different
    // thread count, but it must never answer from a pass shallower than the
    // level's guarantee, and it must report the depth it actually used.
    {
        clear_board();
        board[7][7] = board[8][8] = 1;
        board[7][8] = board[8][7] = 2;
        bool ladder_honest = true;
        int deepest = 0;
        for (int threads : {1, 2, 4}) {
            gomoku::SearchStats stats;
            const gomoku::Move move = gomoku::choose_ai_move(board, sz, 1, 2, nullptr, threads,
                                                             {-1, -1}, nullptr, &stats);
            ladder_honest = ladder_honest && move.r != -1 && stats.budget_ms == 2000 &&
                            stats.nominal_depth >= 2 && stats.completed_depth >= stats.nominal_depth &&
                            stats.search_depth >= stats.completed_depth - 2 &&
                            stats.elapsed_ms <= stats.budget_ms + 1000 + 500;
            deepest = std::max(deepest, stats.completed_depth);
        }
        pass &= ladder_honest;
        std::printf("[%s] deepening honours the level guarantee and budget (%d threads, deepest %d)\n",
                    ladder_honest ? "PASS" : "FAIL", 4, deepest);
    }

    clear_board();
    board[7][7] = board[8][8] = 1;
    board[7][8] = board[8][7] = 2;
    int uncached_value = gomoku::negamax(board, sz, 1, 1, -210000000, 210000000,
                                         {-1, -1}, 2);
    gomoku::TranspositionTable table(1u << 12);
    std::uint64_t hash = gomoku::position_hash(board, sz, 1);
    int cached_value = gomoku::negamax(board, sz, 1, 1, -210000000, 210000000,
                                       {-1, -1}, 2, nullptr, 2, nullptr, nullptr,
                                       nullptr, hash, &table);
    int reused_value = gomoku::negamax(board, sz, 1, 1, -210000000, 210000000,
                                       {-1, -1}, 2, nullptr, 2, nullptr, nullptr,
                                       nullptr, hash, &table);
    bool transposition_exact = cached_value == uncached_value && reused_value == cached_value;
    pass &= transposition_exact;
    std::printf("[%s] exact transposition-table reuse\n", transposition_exact ? "PASS" : "FAIL");

    clear_board();
    const std::vector<gomoku::Move> vct_horizon = {
        {7,7},{7,8},{8,7},{6,7},{5,6},{7,6},{5,8},{8,5},{4,7},{6,5},
        {6,9},{3,6},{5,9},{5,5},{7,5},{4,5},{4,10},{3,5},{2,5}
    };
    for (size_t i = 0; i < vct_horizon.size(); ++i)
        board[vct_horizon[i].r][vct_horizon[i].c] = i % 2 == 0 ? 1 : 2;
    board[9][4] = 2;
    auto vct_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    bool vct_setup_detected =
        gomoku::opponent_has_unavoidable_vcf_setup(board, sz, 1, vct_deadline, 12, 48);
    pass &= vct_setup_detected;
    std::printf("[%s] one-reply VCT horizon proof\n", vct_setup_detected ? "PASS" : "FAIL");

    bool version_catalog = true;
    for (int i = 0; i < static_cast<int>(gomoku::EngineVersion::Count); ++i) {
        const char* name = gomoku::engine_version_name(static_cast<gomoku::EngineVersion>(i));
        version_catalog &= name && std::strcmp(name, "Unknown") != 0;
    }
    pass &= version_catalog;
    std::printf("[%s] seven-engine match catalog\n", version_catalog ? "PASS" : "FAIL");

    clear_board();
    for (int r = 0; r < sz; ++r) for (int c = 0; c < sz; ++c) board[r][c] = ((r + c) % 2) + 1;
    gomoku::Move no_move = gomoku::choose_ai_move(board, sz, 2, 1);
    gomoku::Move legacy_no_move = gomoku::legacy_choose_move(board, sz, 2, 1);
    bool draw_safe = no_move.r == -1 && no_move.c == -1 &&
                     legacy_no_move.r == -1 && legacy_no_move.c == -1;
    pass &= draw_safe;
    std::printf("[%s] all built-in full-board draw handling\n", draw_safe ? "PASS" : "FAIL");

    clear_board();
    for (int r = 0; r < sz; ++r) for (int c = 0; c < sz; ++c) board[r][c] = 2;
    board[0][0] = 0;
    gomoku::Move emergency = gomoku::first_legal_move(board, sz, 2);
    bool isolated_fallback = emergency.r == 0 && emergency.c == 0;
    pass &= isolated_fallback;
    std::printf("[%s] isolated legal fallback move\n", isolated_fallback ? "PASS" : "FAIL");

    gomoku::ExternalEngineRequest request;
    request.board_size = sz;
    request.player = 2;
    request.moves = {{7, 7}};
    gomoku::Move parsed;
    bool adapter_validation = gomoku::parse_external_move_line("8,7\r", parsed) &&
                              parsed.r == 7 && parsed.c == 8 &&
                              gomoku::legal_external_move(request, parsed) &&
                              gomoku::parse_external_move_line("7,7", parsed) &&
                              !gomoku::legal_external_move(request, parsed) &&
                              !gomoku::parse_external_move_line("8,7 debug", parsed);
    pass &= adapter_validation;
    std::printf("[%s] external move parsing and legality\n", adapter_validation ? "PASS" : "FAIL");

    clear_board();
    for (int r = 0; r < sz; ++r) for (int c = 0; c < sz; ++c) board[r][c] = 1;
    board[7][7] = 0;
    move_history.resize(sz * sz - 1, {0, 0});
    apply_move(7, 7, 2);
    bool final_square_draw = game_over && winner == 0;
    pass &= final_square_draw;
    std::printf("[%s] final-square draw state\n", final_square_draw ? "PASS" : "FAIL");

    // The packed pattern table must reproduce the reference string matcher
    // exactly.  '.' is a literal empty cell and '#' is off-board, so a matcher
    // that treats '.' as a wildcard would silently change every score.
    {
        auto reference = [](const char line[16]) {
            gomoku::ThreatInfo info;
            int run = 0;
            for (int i = 0; i < 15; ++i) {
                if (line[i] == 'X') info.longest = std::max(info.longest, ++run);
                else run = 0;
            }
            int best[gomoku::TH_KIND_COUNT] = {};
            for (const gomoku::PatternDef& pattern : gomoku::pattern_table()) {
                const int len = static_cast<int>(std::strlen(pattern.pat));
                for (int i = 0; i + len <= 15; ++i) {
                    if (i > 7 || i + len <= 7) continue;
                    bool match = true;
                    for (int k = 0; k < len && match; ++k)
                        match = line[i + k] == pattern.pat[k];
                    if (!match) continue;
                    info.count[pattern.kind]++;
                    best[pattern.kind] = std::max(best[pattern.kind], pattern.score);
                }
            }
            for (int i = 0; i < gomoku::TH_KIND_COUNT; ++i) info.score += best[i];
            return info;
        };
        clear_board();
        board[7][7] = board[7][8] = board[6][6] = 1;
        board[8][8] = board[5][9] = 2;
        bool pattern_exact = true;
        int lines_checked = 0;
        static const int dr[] = {0, 1, 1, 1};
        static const int dc[] = {1, 0, 1, -1};
        for (int r = -1; r <= sz; ++r) {
            for (int c = -1; c <= sz; ++c) {
                if (!gomoku::ok(sz, r, c) || board[r][c]) continue;
                for (int d = 0; d < 4; ++d) {
                    for (int p = 1; p <= 2; ++p) {
                        char line[16];
                        gomoku::encode_line(board, sz, r, c, dr[d], dc[d], p, line);
                        const gomoku::ThreatInfo expected = reference(line);
                        const gomoku::ThreatInfo actual = gomoku::evaluate_line(line);
                        ++lines_checked;
                        pattern_exact = pattern_exact && expected.score == actual.score &&
                                        expected.longest == actual.longest;
                        for (int k = 0; k < gomoku::TH_KIND_COUNT; ++k)
                            pattern_exact = pattern_exact && expected.count[k] == actual.count[k];
                    }
                }
            }
        }
        pass &= pattern_exact && lines_checked > 0;
        std::printf("[%s] packed pattern matcher matches reference (%d lines)\n",
                    pattern_exact ? "PASS" : "FAIL", lines_checked);
    }

    // Iterative deepening must report honestly: the number of roots it claims
    // completed has to equal the count of finished roots, and extra depth may
    // only ever be added on top of a completed guaranteed depth.
    {
        clear_board();
        board[7][7] = board[8][8] = 1;
        board[7][8] = board[8][7] = 2;
        gomoku::SearchStats stats;
        const gomoku::Move move = gomoku::choose_ai_move(board, sz, 1, 1, nullptr, 2,
                                                         {-1, -1}, nullptr, &stats);
        const bool reported = move.r != -1 && stats.roots > 0 &&
                              stats.roots_completed >= 0 &&
                              stats.roots_completed <= stats.roots &&
                              stats.completed_depth >= 1 &&
                              stats.completed_depth >= stats.nominal_depth &&  // level is a floor
                              stats.search_depth >= stats.completed_depth - 2;
        pass &= reported;
        std::printf("[%s] iterative-deepening depth report (target %d, completed %d, answered at %d)\n",
                    reported ? "PASS" : "FAIL", stats.nominal_depth,
                    stats.completed_depth, stats.search_depth);
    }

    clear_board();
    std::atomic<bool> cancelled{true};
    bool built_in_cancel = true;
    for (int version = 0; version <= static_cast<int>(gomoku::EngineVersion::CurrentV3); ++version) {
        gomoku::Move cancelled_move = gomoku::choose_versioned_move(
            board, sz, 1, 5, static_cast<gomoku::EngineVersion>(version), nullptr, 4,
            {-1, -1}, &cancelled);
        built_in_cancel &= cancelled_move.r == -1 && cancelled_move.c == -1;
    }
    pass &= built_in_cancel;
    std::printf("[%s] built-in search cancellation\n", built_in_cancel ? "PASS" : "FAIL");

    return pass;
}

void print_help() {
    std::printf("Commands:\n");
    std::printf("  play              interactive human black vs AI white\n");
    std::printf("  agent             text protocol for Codex / other LLMs\n");
    std::printf("  genmove           read current board from stdin, print AI move\n");
    std::printf("  selfmatch [hi] [lo] [pairs] [threads]  level-doubling self-match\n");
    std::printf("  genmove-history [depth] [threads]  style-aware move (defaults: 4, auto)\n");
    std::printf("  test              run tactical regression tests\n");
    std::printf("Agent commands: new [size], play r c, ai, show, state, legal r c, depth n, quit\n");
    std::printf("Board input for genmove: first line size, then size lines using X O .\n");
}

bool read_board_stdin() {
    clear_board();
    if (std::scanf("%d", &sz) != 1 || sz < 9 || sz > gomoku::kMaxBoard) return false;
    char row[128];
    for (int r = 0; r < sz; r++) {
        if (std::scanf("%127s", row) != 1) return false;
        for (int c = 0; c < sz && row[c]; c++) {
            if (row[c] == 'X' || row[c] == 'x') board[r][c] = 1;
            else if (row[c] == 'O' || row[c] == 'o') board[r][c] = 2;
            else board[r][c] = 0;
        }
    }
    return true;
}

int genmove() {
    if (!read_board_stdin()) {
        std::fprintf(stderr, "invalid board input\n");
        return 2;
    }
    gomoku::Move m = gomoku::choose_ai_move(board, sz, 2, 4);
    std::printf("%d %d\n", m.r, m.c);
    return 0;
}

// Level-doubling self-match: the current engine at `hi` against the very same
// engine at `lo`, colours swapped over the shared openings.  This needs no
// third-party runtime, so it is the portable smoke test for "did the search
// get stronger", and it is the check that caught an entire class of
// bookkeeping bugs during development.
int self_match(int hi_level, int lo_level, int pairs, int max_threads) {
    static const std::vector<std::vector<gomoku::Move>> openings = {
        {{7, 7}, {7, 8}, {8, 7}, {6, 7}},
        {{7, 7}, {8, 8}, {6, 8}, {8, 6}, {6, 6}, {7, 8}},
        {{7, 7}, {6, 7}, {8, 8}, {8, 6}, {6, 8}, {7, 6}},
        {{7, 7}, {8, 7}, {6, 6}, {8, 8}, {6, 8}, {7, 6}},
        {{7, 7}, {7, 6}, {6, 8}, {8, 8}, {5, 9}, {9, 5}},
        {{7, 7}, {8, 6}, {6, 6}, {8, 8}, {6, 10}, {8, 10}},
        {{7, 7}, {6, 8}, {8, 6}, {5, 9}, {9, 5}, {6, 6}},
        {{7, 7}, {7, 8}, {6, 6}, {8, 8}, {6, 9}, {8, 6}},
    };
    int hi_wins = 0, lo_wins = 0, draws = 0;
    // Black (the first player) has a large structural advantage in Gomoku, so
    // the raw score of a colour-unsplit match mostly measures the opening, not
    // the level.  Results are therefore reported per colour as well.
    int hi_as_black = 0, hi_black_games = 0, hi_as_white = 0, hi_white_games = 0;
    for (int pair = 0; pair < pairs; ++pair) {
        for (int swap = 0; swap < 2; ++swap) {
            const int hi_player = swap == 0 ? 1 : 2;
            clear_board();
            std::vector<gomoku::Move> history;
            for (const gomoku::Move& move : openings[pair % openings.size()]) {
                board[move.r][move.c] = history.size() % 2 == 0 ? 1 : 2;
                history.push_back(move);
            }
            int result = 0;
            int plies = static_cast<int>(history.size());
            while (plies < sz * sz) {
                const int player = plies % 2 == 0 ? 1 : 2;
                const int level = player == hi_player ? hi_level : lo_level;
                gomoku::OpponentStyle style = gomoku::analyze_opponent_style(sz, history, 3 - player);
                gomoku::Move last{-1, -1};
                for (int i = static_cast<int>(history.size()) - 1; i >= 0; --i) {
                    if ((i % 2 == 0 ? 1 : 2) == player) { last = history[i]; break; }
                }
                const gomoku::Move move = gomoku::choose_ai_move(board, sz, player, level,
                                                                 &style, max_threads, last);
                if (!gomoku::is_legal_move(board, sz, player, move)) {
                    result = 3 - player;
                    break;
                }
                board[move.r][move.c] = player;
                history.push_back(move);
                ++plies;
                if (gomoku::check_win_at(board, sz, move.r, move.c, player)) {
                    result = player;
                    break;
                }
            }
            if (result == hi_player) ++hi_wins;
            else if (result == 0) ++draws;
            else ++lo_wins;
            if (hi_player == 1) { ++hi_black_games; if (result == 1) ++hi_as_black; }
            else { ++hi_white_games; if (result == 2) ++hi_as_white; }
            std::printf("game=%d hi=%s winner=%d plies=%d\n",
                        pair * 2 + swap + 1, hi_player == 1 ? "black" : "white",
                        result, plies);
            std::fflush(stdout);
        }
    }
    std::printf("summary hi_level=%d lo_level=%d hi=%d lo=%d draws=%d "
                "hi_as_black=%d/%d hi_as_white=%d/%d\n",
                hi_level, lo_level, hi_wins, lo_wins, draws,
                hi_as_black, hi_black_games, hi_as_white, hi_white_games);
    // Only a losing overall score is a failure: with 2-4 games per side this is
    // a smoke test, and a colour-dominated 2-2 says nothing about the level.
    return hi_wins < lo_wins ? 1 : 0;
}

int genmove_history(int search_depth, int max_threads) {
    clear_board();
    int ai_player = 2, count = 0;
    if (std::scanf("%d %d %d", &sz, &ai_player, &count) != 3 ||
        sz < 9 || sz > gomoku::kMaxBoard || (ai_player != 1 && ai_player != 2) || count < 0) {
        std::fprintf(stderr, "invalid history header\n");
        return 2;
    }
    for (int i = 0; i < count; ++i) {
        int r = -1, c = -1;
        if (std::scanf("%d %d", &r, &c) != 2 || !gomoku::ok(sz, r, c) || board[r][c] != 0) {
            std::fprintf(stderr, "invalid move history\n");
            return 2;
        }
        board[r][c] = (i % 2 == 0) ? 1 : 2;
        move_history.push_back({r, c});
    }
    gomoku::OpponentStyle style = gomoku::analyze_opponent_style(sz, move_history, 3 - ai_player);
    gomoku::Move move = gomoku::choose_ai_move(board, sz, ai_player, search_depth, &style, max_threads,
                                               last_history_move_for(ai_player));
    std::printf("%d %d\n", move.r, move.c);
    return 0;
}

int play() {
    clear_board();
    print_board();
    while (true) {
        int r = -1, c = -1;
        std::printf("your move r c> ");
        if (std::scanf("%d %d", &r, &c) != 2) return 0;
        if (!gomoku::ok(sz, r, c) || board[r][c] != 0) {
            std::printf("illegal coordinate\n");
            continue;
        }
        if (gomoku::is_forbidden_move(board, sz, r, c)) {
            std::printf("forbidden move\n");
            continue;
        }
        apply_move(r, c, 1);
        if (game_over) {
            print_board();
            std::printf("%s\n", winner == 1 ? "black wins" : "draw");
            return 0;
        }
        gomoku::OpponentStyle style = gomoku::analyze_opponent_style(sz, move_history, 1);
        gomoku::Move ai = gomoku::choose_ai_move(board, sz, 2, 4, &style, 0,
                                                 last_history_move_for(2));
        if (ai.r == -1) {
            std::printf("draw\n");
            return 0;
        }
        apply_move(ai.r, ai.c, 2);
        std::printf("ai %d %d\n", ai.r, ai.c);
        print_board();
        if (game_over) {
            std::printf("%s\n", winner == 2 ? "white wins" : "draw");
            return 0;
        }
    }
}

int agent() {
    agent_log = std::fopen("gomoku_agent.log", "w");
    clear_board();
    log_line("SESSION START");
    log_board_snapshot("INIT");
    print_state();
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;
        for (char& ch : cmd) ch = (char)std::tolower((unsigned char)ch);
        if (cmd.empty()) continue;
        log_line(std::string("> ") + line);
        if (cmd == "quit" || cmd == "exit") {
            log_line("BYE");
            if (agent_log) std::fclose(agent_log);
            agent_log = nullptr;
            std::printf("BYE\n");
            return 0;
        }
        if (cmd == "help") {
            print_help();
            continue;
        }
        if (cmd == "new") {
            int n = sz;
            if (iss >> n) {
                if (n < 9) n = 9;
                if (n > gomoku::kMaxBoard) n = gomoku::kMaxBoard;
                sz = n;
            }
            clear_board();
            log_board_snapshot("NEW");
            print_state();
            continue;
        }
        if (cmd == "depth") {
            int d = default_depth;
            if (iss >> d) default_depth = std::max(1, std::min(6, d));
            std::printf("DEPTH %d\n", default_depth);
            log_line(std::string("DEPTH ") + std::to_string(default_depth));
            continue;
        }
        if (cmd == "show") {
            print_board();
            log_board_snapshot("SHOW");
            continue;
        }
        if (cmd == "state") {
            print_state();
            log_board_snapshot("STATE");
            continue;
        }
        if (cmd == "legal") {
            int r = -1, c = -1;
            if (!(iss >> r >> c)) {
                std::printf("ERR bad args\n");
                log_line("ERR bad args");
                continue;
            }
            bool ok = gomoku::ok(sz, r, c) && board[r][c] == 0 && !(turn == 1 && gomoku::is_forbidden_move(board, sz, r, c));
            std::printf("LEGAL %s\n", ok ? "YES" : "NO");
            log_line(std::string("LEGAL ") + (ok ? "YES" : "NO"));
            continue;
        }
        if (cmd == "play") {
            int r = -1, c = -1;
            if (!(iss >> r >> c)) {
                std::printf("ERR bad args\n");
                log_line("ERR bad args");
                continue;
            }
            if (game_over) {
                std::printf("ERR game over\n");
                log_line("ERR game over");
                continue;
            }
            if (!gomoku::ok(sz, r, c) || board[r][c] != 0) {
                std::printf("ERR illegal\n");
                log_line("ERR illegal");
                continue;
            }
            if (turn != 1) {
                std::printf("ERR not black turn\n");
                log_line("ERR not black turn");
                continue;
            }
            if (gomoku::is_forbidden_move(board, sz, r, c)) {
                std::printf("FORBIDDEN %d %d\n", r, c);
                log_line("FORBIDDEN");
                continue;
            }
            apply_move(r, c, 1);
            std::printf("OK BLACK %d %d\n", r, c);
            log_board_snapshot("MOVE");
            if (game_over) {
                const char* result = winner == 1 ? "RESULT BLACK_WINS" : "RESULT DRAW";
                std::printf("%s\n", result);
                log_line(result);
                continue;
            }
            gomoku::OpponentStyle style = gomoku::analyze_opponent_style(sz, move_history, 1);
            gomoku::Move ai = gomoku::choose_ai_move(board, sz, 2, default_depth, &style, 0,
                                                     last_history_move_for(2));
            if (ai.r == -1) {
                game_over = true;
                winner = 0;
                std::printf("RESULT DRAW\n");
                log_line("RESULT DRAW");
                continue;
            }
            apply_move(ai.r, ai.c, 2);
            std::printf("AI %d %d\n", ai.r, ai.c);
            log_board_snapshot("AI");
            if (game_over) {
                const char* result = winner == 2 ? "RESULT WHITE_WINS" : "RESULT DRAW";
                std::printf("%s\n", result);
                log_line(result);
            }
            continue;
        }
        if (cmd == "ai") {
            if (game_over) {
                std::printf("ERR game over\n");
                log_line("ERR game over");
                continue;
            }
            if (turn != 2) {
                std::printf("ERR not white turn\n");
                log_line("ERR not white turn");
                continue;
            }
            gomoku::OpponentStyle style = gomoku::analyze_opponent_style(sz, move_history, 1);
            gomoku::Move ai = gomoku::choose_ai_move(board, sz, 2, default_depth, &style, 0,
                                                     last_history_move_for(2));
            if (ai.r == -1) {
                game_over = true;
                winner = 0;
                std::printf("RESULT DRAW\n");
                log_line("RESULT DRAW");
                continue;
            }
            apply_move(ai.r, ai.c, 2);
            std::printf("AI %d %d\n", ai.r, ai.c);
            log_board_snapshot("AI");
            if (game_over) {
                const char* result = winner == 2 ? "RESULT WHITE_WINS" : "RESULT DRAW";
                std::printf("%s\n", result);
                log_line(result);
            }
            continue;
        }
        std::printf("ERR unknown command\n");
        log_line("ERR unknown command");
    }
    if (agent_log) std::fclose(agent_log);
    agent_log = nullptr;
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    std::string cmd = argc >= 2 ? argv[1] : "help";
    if (cmd == "test") return run_tests() ? 0 : 1;
    if (cmd == "genmove") return genmove();
    if (cmd == "genmove-history") {
        int depth = argc >= 3 ? std::max(1, std::min(6, std::atoi(argv[2]))) : 4;
        int threads = argc >= 4 ? std::max(0, std::min(gomoku::kMaxSearchThreads, std::atoi(argv[3]))) : 0;
        return genmove_history(depth, threads);
    }
    if (cmd == "selfmatch") {
        const int hi = argc >= 3 ? std::max(1, std::min(5, std::atoi(argv[2]))) : 3;
        const int lo = argc >= 4 ? std::max(1, std::min(5, std::atoi(argv[3]))) : 2;
        const int pairs = argc >= 5 ? std::max(1, std::min(4, std::atoi(argv[4]))) : 1;
        const int threads = argc >= 6
            ? std::max(1, std::min(gomoku::kMaxSearchThreads, std::atoi(argv[5]))) : 2;
        if (hi == lo) {
            std::fprintf(stderr, "selfmatch needs two different levels\n");
            return 2;
        }
        return self_match(hi, lo, pairs, threads);
    }
    if (cmd == "play") return play();
    if (cmd == "agent") return agent();
    print_help();
    return cmd == "help" ? 0 : 2;
}
