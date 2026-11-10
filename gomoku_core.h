#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <thread>
#include <utility>
#include <vector>

namespace gomoku {

constexpr int kMaxBoard = 30;
constexpr int kMaxSearchThreads = 32;

enum ThreatKind {
    TH_FIVE,
    TH_OPEN_FOUR,
    TH_CLOSED_FOUR,
    TH_BROKEN_FOUR,
    TH_OPEN_THREE,
    TH_BROKEN_THREE,
    TH_CLOSED_THREE,
    TH_OPEN_TWO,
    TH_BROKEN_TWO,
    TH_CLOSED_TWO,
    TH_KIND_COUNT
};

struct ThreatInfo {
    int score = 0;
    int count[TH_KIND_COUNT] = {};
    int longest = 0;
};

struct PatternDef {
    const char* pat;
    ThreatKind kind;
    int score;
};

struct Move {
    int r = -1;
    int c = -1;
};

struct WinLine {
    Move start{-1, -1};
    Move end{-1, -1};
};

// A deliberately small, online model of the human player.  The neutral priors
// keep the engine conservative during the opening; confidence grows only after
// several observed moves.  Direction order is horizontal, vertical, diagonal
// down-right, diagonal down-left.
struct OpponentStyle {
    int player = 1;
    int observations = 0;
    double attack_preference = 0.5;
    double defense_preference = 0.5;
    double locality_preference = 0.5;
    double center_preference = 0.5;
    std::array<double, 4> direction_preference{{0.25, 0.25, 0.25, 0.25}};

    double confidence() const {
        return std::min(1.0, observations / 8.0);
    }
};

inline bool ok(int sz, int r, int c) { return r >= 0 && r < sz && c >= 0 && c < sz; }

inline const std::vector<PatternDef>& pattern_table() {
    static const std::vector<PatternDef> patterns = {
        {"XXXXX", TH_FIVE, 1000000},
        {".XXXX.", TH_OPEN_FOUR, 220000},
        {".XXXXO", TH_CLOSED_FOUR, 52000}, {"OXXXX.", TH_CLOSED_FOUR, 52000},
        {"#XXXX.", TH_CLOSED_FOUR, 50000}, {".XXXX#", TH_CLOSED_FOUR, 50000},
        {".XXX.X.", TH_BROKEN_FOUR, 130000}, {".XX.XX.", TH_BROKEN_FOUR, 130000}, {".X.XXX.", TH_BROKEN_FOUR, 130000},
        {"OXXX.X.", TH_BROKEN_FOUR, 48000}, {".XXX.XO", TH_BROKEN_FOUR, 48000},
        {"OXX.XX.", TH_BROKEN_FOUR, 48000}, {".XX.XXO", TH_BROKEN_FOUR, 48000},
        {"OX.XXX.", TH_BROKEN_FOUR, 48000}, {".X.XXXO", TH_BROKEN_FOUR, 48000},
        {"#XXX.X.", TH_BROKEN_FOUR, 45000}, {".XXX.X#", TH_BROKEN_FOUR, 45000},
        {"#XX.XX.", TH_BROKEN_FOUR, 45000}, {".XX.XX#", TH_BROKEN_FOUR, 45000},
        {"#X.XXX.", TH_BROKEN_FOUR, 45000}, {".X.XXX#", TH_BROKEN_FOUR, 45000},
        {".XXX.", TH_OPEN_THREE, 18000},
        {".XX.X.", TH_BROKEN_THREE, 12500}, {".X.XX.", TH_BROKEN_THREE, 12500},
        {"OXXX.", TH_CLOSED_THREE, 2600}, {".XXXO", TH_CLOSED_THREE, 2600},
        {"#XXX.", TH_CLOSED_THREE, 2400}, {".XXX#", TH_CLOSED_THREE, 2400},
        {"OXX.X.", TH_CLOSED_THREE, 2100}, {".XX.XO", TH_CLOSED_THREE, 2100},
        {"OX.XX.", TH_CLOSED_THREE, 2100}, {".X.XXO", TH_CLOSED_THREE, 2100},
        // Enhanced: stronger broken-three defense (Carbon-style defensive priority)
        {".X.XX.", TH_BROKEN_THREE, 13800}, {".XX.X.", TH_BROKEN_THREE, 13800},
        {"O.XXX.", TH_CLOSED_THREE, 3200}, {".XXX.O", TH_CLOSED_THREE, 3200},
        // Enhanced: open-two with defensive value
        {".XX.", TH_OPEN_TWO, 780}, {".X.X.", TH_BROKEN_TWO, 560},
        {"OXX.", TH_CLOSED_TWO, 150}, {".XXO", TH_CLOSED_TWO, 150},
        {"#XX.", TH_CLOSED_TWO, 130}, {".XX#", TH_CLOSED_TWO, 130},
        {"OX.X.", TH_CLOSED_TWO, 110}, {".X.XO", TH_CLOSED_TWO, 110},
    };
    return patterns;
}

inline int count_line(const int board[kMaxBoard][kMaxBoard], int sz, int r, int c, int dr, int dc, int p) {
    int cnt = 1;
    const int dir[] = {1, -1};
    for (int i = 0; i < 2; i++) {
        int nr = r + dr * dir[i], nc = c + dc * dir[i];
        while (ok(sz, nr, nc) && board[nr][nc] == p) {
            cnt++;
            nr += dr * dir[i];
            nc += dc * dir[i];
        }
    }
    return cnt;
}

inline void encode_line(const int board[kMaxBoard][kMaxBoard], int sz, int r, int c, int dr, int dc, int p, char out[16]) {
    int op = 3 - p;
    for (int i = 0; i < 15; i++) {
        int k = i - 7;
        int nr = r + dr * k, nc = c + dc * k;
        if (!ok(sz, nr, nc)) out[i] = '#';
        else if (k == 0) out[i] = 'X';
        else if (board[nr][nc] == p) out[i] = 'X';
        else if (board[nr][nc] == op) out[i] = 'O';
        else out[i] = '.';
    }
    out[15] = '\0';
}

inline bool match_at(const char line[16], int pos, const char* pat) {
    for (int i = 0; pat[i]; i++) {
        if (pos + i >= 15 || line[pos + i] != pat[i]) return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Pattern matching, packed.
//
// The reference matcher walks every pattern row at every alignment in the
// 15-cell window with a per-character compare, which is the single hottest
// routine in the whole engine (about 90% of search time).  Each line is base-4
// information, so we pack it into one 32-bit word, two bits per cell, and
// collect every (pattern, alignment) pair that can cover the centre cell into
// a flat table of pre-shifted masks.  A match is then one AND plus one compare.
//
// Semantics are identical to the reference: '.' is a literal empty cell (not a
// wildcard), '#' is off-board, and a match is counted only when its window
// covers the centre position, matching the original `i > 7 || i + len <= 7`
// skip.  `gomoku_cli test` pins this equivalence with a differential check.
// ---------------------------------------------------------------------------

inline int cell_code(char ch) {
    switch (ch) {
        case 'X': return 1;
        case 'O': return 2;
        case '#': return 3;
        default: return 0;  // '.'
    }
}

inline std::uint32_t pack_line(const char line[16]) {
    std::uint32_t key = 0;
    for (int i = 0; i < 15; ++i) key |= static_cast<std::uint32_t>(cell_code(line[i])) << (2 * i);
    return key;
}

// Pre-shifted (pattern, alignment) pair.  `value` has zero in the wildcard
// positions where `mask` has zero, so (key & mask) == value means exact match.
struct PatternArm {
    std::uint32_t mask;
    std::uint32_t value;
    unsigned char kind;
    int score;
};

inline const std::vector<PatternArm>& pattern_arms() {
    static const std::vector<PatternArm> arms = [] {
        std::vector<PatternArm> built;
        for (const PatternDef& p : pattern_table()) {
            const int len = static_cast<int>(std::strlen(p.pat));
            std::uint32_t local_mask = 0, local_value = 0;
            for (int i = 0; i < len; ++i) {
                local_mask |= 3u << (2 * i);
                local_value |= static_cast<std::uint32_t>(cell_code(p.pat[i])) << (2 * i);
            }
            for (int offset = 0; offset + len <= 15; ++offset) {
                if (offset > 7 || offset + len <= 7) continue;
                built.push_back({local_mask << (2 * offset), local_value << (2 * offset),
                                 static_cast<unsigned char>(p.kind), p.score});
            }
        }
        return built;
    }();
    return arms;
}

inline ThreatInfo evaluate_line(const char line[16]) {
    ThreatInfo info;
    int run = 0;
    for (int i = 0; i < 15; i++) {
        if (line[i] == 'X') info.longest = std::max(info.longest, ++run);
        else run = 0;
    }
    int best[TH_KIND_COUNT] = {};
    const std::uint32_t key = pack_line(line);
    for (const PatternArm& arm : pattern_arms()) {
        if ((key & arm.mask) != arm.value) continue;
        info.count[arm.kind]++;
        if (arm.score > best[arm.kind]) best[arm.kind] = arm.score;
    }
    for (int i = 0; i < TH_KIND_COUNT; i++) info.score += best[i];
    return info;
}

inline ThreatInfo evaluate_direction(const int board[kMaxBoard][kMaxBoard], int sz, int r, int c, int dr, int dc, int p) {
    char line[16];
    encode_line(board, sz, r, c, dr, dc, p, line);
    return evaluate_line(line);
}

inline ThreatInfo analyze_move(const int board[kMaxBoard][kMaxBoard], int sz, int r, int c, int p) {
    static const int dr[] = {0, 1, 1, 1};
    static const int dc[] = {1, 0, 1, -1};
    ThreatInfo total;
    int active_fours = 0, active_threes = 0;
    for (int d = 0; d < 4; d++) {
        ThreatInfo dir = evaluate_direction(board, sz, r, c, dr[d], dc[d], p);
        total.score += dir.score;
        total.longest = std::max(total.longest, dir.longest);
        for (int i = 0; i < TH_KIND_COUNT; i++) total.count[i] += dir.count[i];
        if (dir.count[TH_OPEN_FOUR] || dir.count[TH_BROKEN_FOUR] || dir.count[TH_CLOSED_FOUR]) active_fours++;
        if (dir.count[TH_OPEN_THREE] || dir.count[TH_BROKEN_THREE]) active_threes++;
    }
    if (total.count[TH_FIVE]) total.score += 1000000;
    if (total.count[TH_OPEN_FOUR]) total.score += 260000;
    if (active_fours >= 2) total.score += 240000;
    if (total.count[TH_BROKEN_FOUR]) total.score += 100000;
    if (total.count[TH_CLOSED_FOUR]) total.score += 50000;
    if (active_fours >= 1 && active_threes >= 1) total.score += 180000;
    if (total.count[TH_OPEN_THREE]) total.score += 20000;
    if (active_threes >= 2 || total.count[TH_OPEN_THREE] >= 2) total.score += 65000;
    if (total.count[TH_BROKEN_THREE]) total.score += 9000;
    if (total.count[TH_CLOSED_THREE]) total.score += 1300;
    if (total.count[TH_OPEN_TWO]) total.score += 450;
    if (total.count[TH_BROKEN_TWO]) total.score += 260;
    if (total.count[TH_CLOSED_TWO]) total.score += 60;
    int dst = std::abs(r - sz / 2) + std::abs(c - sz / 2);
    total.score += std::max(0, sz - dst);
    return total;
}

inline int get_thr(const int board[kMaxBoard][kMaxBoard], int sz, int r, int c, int p) {
    if (!ok(sz, r, c) || board[r][c] != 0) return 0;
    return analyze_move(board, sz, r, c, p).score;
}

inline bool is_forbidden_move(int board[kMaxBoard][kMaxBoard], int sz, int r, int c);
inline std::vector<Move> generate_candidates(const int board[kMaxBoard][kMaxBoard], int sz,
                                             int radius = 2);

inline double clamp01(double v) { return std::max(0.0, std::min(1.0, v)); }

inline double relative_choice(int chosen, int best) {
    if (best < 200) return 0.5;
    return clamp01(static_cast<double>(chosen) / static_cast<double>(best));
}

inline void observe_opponent_move(OpponentStyle& style,
                                  int board[kMaxBoard][kMaxBoard], int sz, Move move) {
    if (!ok(sz, move.r, move.c) || board[move.r][move.c] != 0) return;
    const int p = style.player;
    const int op = 3 - p;
    int best_attack = 0, best_defense = 0;
    for (Move candidate : generate_candidates(board, sz, 2)) {
        best_attack = std::max(best_attack, get_thr(board, sz, candidate.r, candidate.c, p));
        best_defense = std::max(best_defense, get_thr(board, sz, candidate.r, candidate.c, op));
    }

    const double attack_evidence = relative_choice(get_thr(board, sz, move.r, move.c, p), best_attack);
    const double defense_evidence = relative_choice(get_thr(board, sz, move.r, move.c, op), best_defense);
    int nearby = 0;
    for (int dr = -2; dr <= 2; ++dr) for (int dc = -2; dc <= 2; ++dc) {
        if ((dr != 0 || dc != 0) && ok(sz, move.r + dr, move.c + dc) && board[move.r + dr][move.c + dc]) nearby++;
    }
    const double locality_evidence = clamp01(nearby / 6.0);
    const int max_center_distance = std::max(1, sz - 1);
    const int center_distance = std::abs(move.r - sz / 2) + std::abs(move.c - sz / 2);
    const double center_evidence = clamp01(1.0 - static_cast<double>(center_distance) / max_center_distance);

    static const int dr[] = {0, 1, 1, 1};
    static const int dc[] = {1, 0, 1, -1};
    std::array<double, 4> direction_evidence{};
    double direction_total = 0.0;
    for (int d = 0; d < 4; ++d) {
        ThreatInfo info = evaluate_direction(board, sz, move.r, move.c, dr[d], dc[d], p);
        direction_evidence[d] = 1.0 + info.longest * 80.0 + std::min(info.score, 100000) / 100.0;
        direction_total += direction_evidence[d];
    }
    for (double& value : direction_evidence) value /= direction_total;

    // Two neutral pseudo-observations prevent one unusual opening move from
    // changing the selective search too aggressively.
    const double prior = 2.0;
    const double old_weight = prior + style.observations;
    const double new_weight = old_weight + 1.0;
    style.attack_preference = (style.attack_preference * old_weight + attack_evidence) / new_weight;
    style.defense_preference = (style.defense_preference * old_weight + defense_evidence) / new_weight;
    style.locality_preference = (style.locality_preference * old_weight + locality_evidence) / new_weight;
    style.center_preference = (style.center_preference * old_weight + center_evidence) / new_weight;
    for (int d = 0; d < 4; ++d) {
        style.direction_preference[d] =
            (style.direction_preference[d] * old_weight + direction_evidence[d]) / new_weight;
    }
    style.observations++;
}

inline OpponentStyle analyze_opponent_style(int sz, const std::vector<Move>& history,
                                            int opponent_player = 1) {
    OpponentStyle style;
    style.player = opponent_player;
    int replay[kMaxBoard][kMaxBoard] = {};
    const size_t observation_start = history.size() > 24 ? history.size() - 24 : 0;
    for (size_t i = 0; i < history.size(); ++i) {
        Move move = history[i];
        int p = (i % 2 == 0) ? 1 : 2;
        if (!ok(sz, move.r, move.c) || replay[move.r][move.c] != 0) continue;
        if (p == opponent_player && i >= observation_start) observe_opponent_move(style, replay, sz, move);
        replay[move.r][move.c] = p;
    }
    return style;
}

inline bool check_win_at(const int board[kMaxBoard][kMaxBoard], int sz, int r, int c, int p, WinLine* line = nullptr) {
    static const int dr[] = {0, 1, 1, 1};
    static const int dc[] = {1, 0, 1, -1};
    for (int i = 0; i < 4; i++) {
        if (count_line(board, sz, r, c, dr[i], dc[i], p) >= 5) {
            if (line) {
                int sr = r, sc = c, er = r, ec = c;
                while (ok(sz, sr - dr[i], sc - dc[i]) && board[sr - dr[i]][sc - dc[i]] == p) {
                    sr -= dr[i]; sc -= dc[i];
                }
                while (ok(sz, er + dr[i], ec + dc[i]) && board[er + dr[i]][ec + dc[i]] == p) {
                    er += dr[i]; ec += dc[i];
                }
                line->start = {sr, sc};
                line->end = {er, ec};
            }
            return true;
        }
    }
    return false;
}

inline bool has_five_for(const int board[kMaxBoard][kMaxBoard], int sz, int p) {
    static const int dr[] = {0, 1, 1, 1};
    static const int dc[] = {1, 0, 1, -1};
    for (int r = 0; r < sz; r++) for (int c = 0; c < sz; c++) {
        if (board[r][c] != p) continue;
        for (int d = 0; d < 4; d++) {
            int pr = r - dr[d], pc = c - dc[d];
            if (ok(sz, pr, pc) && board[pr][pc] == p) continue;
            int n = 0, nr = r, nc = c;
            while (ok(sz, nr, nc) && board[nr][nc] == p) {
                n++;
                nr += dr[d]; nc += dc[d];
            }
            if (n >= 5) return true;
        }
    }
    return false;
}

inline bool is_forbidden_move(int board[kMaxBoard][kMaxBoard], int sz, int r, int c) {
    if (!ok(sz, r, c) || board[r][c] != 0) return false;
    board[r][c] = 1;
    static const int dr[] = {0, 1, 1, 1};
    static const int dc[] = {1, 0, 1, -1};
    int open_threes = 0, fours = 0;
    bool exact_five = false, overline = false;
    for (int d = 0; d < 4; d++) {
        int n = count_line(board, sz, r, c, dr[d], dc[d], 1);
        exact_five = exact_five || n == 5;
        overline = overline || n > 5;
        ThreatInfo info = evaluate_direction(board, sz, r, c, dr[d], dc[d], 1);
        if (info.count[TH_OPEN_THREE]) open_threes++;
        if (info.count[TH_OPEN_FOUR] || info.count[TH_BROKEN_FOUR] || info.count[TH_CLOSED_FOUR]) fours++;
    }
    board[r][c] = 0;
    if (exact_five && !overline) return false;
    return overline || open_threes >= 2 || fours >= 2;
}

inline bool is_legal_move(int board[kMaxBoard][kMaxBoard], int sz, int p, Move move) {
    return ok(sz, move.r, move.c) && board[move.r][move.c] == 0 &&
           (p != 1 || !is_forbidden_move(board, sz, move.r, move.c));
}

// Last-resort move selection for adapters and UI recovery paths. A full scan
// is intentional: candidate generators may omit isolated empty intersections.
inline Move first_legal_move(int board[kMaxBoard][kMaxBoard], int sz, int p) {
    const int center = sz / 2;
    for (int distance = 0; distance <= sz * 2; ++distance) {
        for (int r = 0; r < sz; ++r) for (int c = 0; c < sz; ++c) {
            if (std::abs(r - center) + std::abs(c - center) != distance) continue;
            if (is_legal_move(board, sz, p, {r, c})) return {r, c};
        }
    }
    return {-1, -1};
}

inline std::vector<Move> generate_candidates(const int board[kMaxBoard][kMaxBoard], int sz, int radius) {
    bool any = false;
    bool mark[kMaxBoard][kMaxBoard] = {};
    for (int r = 0; r < sz; r++) for (int c = 0; c < sz; c++) if (board[r][c]) {
        any = true;
        for (int dr = -radius; dr <= radius; dr++) for (int dc = -radius; dc <= radius; dc++) {
            int nr = r + dr, nc = c + dc;
            if (ok(sz, nr, nc) && board[nr][nc] == 0) mark[nr][nc] = true;
        }
    }
    if (!any) return {{sz / 2, sz / 2}};
    std::vector<Move> out;
    out.reserve(sz * sz / 3);
    for (int r = 0; r < sz; r++) for (int c = 0; c < sz; c++) if (mark[r][c]) out.push_back({r, c});
    return out;
}

inline int board_shape_score(const int board[kMaxBoard][kMaxBoard], int sz, int p) {
    static const int dr[] = {0, 1, 1, 1};
    static const int dc[] = {1, 0, 1, -1};
    int score = 0;
    for (int r = 0; r < sz; ++r) for (int c = 0; c < sz; ++c) {
        if (board[r][c] != p) continue;
        int freedom = 0;
        for (int rr = -1; rr <= 1; ++rr) for (int cc = -1; cc <= 1; ++cc) {
            if ((rr != 0 || cc != 0) && ok(sz, r + rr, c + cc) && board[r + rr][c + cc] == 0) freedom++;
        }
        score += freedom * 3;
        for (int d = 0; d < 4; ++d) {
            if (ok(sz, r + dr[d], c + dc[d]) && board[r + dr[d]][c + dc[d]] == p) score += 18;
        }
    }
    return score;
}

inline int weighted_potential(std::vector<int>& scores) {
    static const int weight[] = {100, 72, 52, 38, 28, 20, 14, 10, 7, 5};
    std::sort(scores.begin(), scores.end(), std::greater<int>());
    long long total = 0;
    const int count = std::min<int>(scores.size(), static_cast<int>(sizeof(weight) / sizeof(weight[0])));
    for (int i = 0; i < count; ++i) total += static_cast<long long>(scores[i]) * weight[i] / 100;
    return static_cast<int>(std::min<long long>(total, 150000000));
}

// Use the strongest durable opportunities, rather than summing every empty
// intersection.  The old sum grew with board clutter and made late-game values
// noisy.  This evaluation is antisymmetric, which is important for negamax.
inline int static_eval(int board[kMaxBoard][kMaxBoard], int sz, int p) {
    const int op = 3 - p;
    std::vector<int> own_scores, opponent_scores;
    auto candidates = generate_candidates(board, sz, 2);
    own_scores.reserve(candidates.size());
    opponent_scores.reserve(candidates.size());
    for (Move m : candidates) {
        int own = get_thr(board, sz, m.r, m.c, p);
        int opponent = get_thr(board, sz, m.r, m.c, op);
        // Enhanced defensive filtering: block opponent's open fours more aggressively (Rapfi-style tactical priority)
        if (!(p == 1 && own >= 18000 && is_forbidden_move(board, sz, m.r, m.c))) own_scores.push_back(own);
        // Stronger defensive weight: opponent threats above 95000 should trigger defensive priorities
        if (op == 1 && opponent >= 95000 && is_forbidden_move(board, sz, m.r, m.c)) opponent_scores.push_back(opponent * 3 / 2);
        else if (!(op == 1 && opponent >= 18000 && is_forbidden_move(board, sz, m.r, m.c))) opponent_scores.push_back(opponent);
    }
    const int potential = weighted_potential(own_scores) - weighted_potential(opponent_scores);
    // Enhanced defensive shape score (XL-style): defensive connectivity difference, antisymmetric
    int defensive_shape = 0;
    for (int r = 0; r < sz; ++r) for (int c = 0; c < sz; ++c) {
        if (board[r][c] == p) {
            int connections = 0;
            for (int d = 0; d < 4; ++d) {
                static const int dr[] = {0, 1, 1, 1};
                static const int dc[] = {1, 0, 1, -1};
                int nr = r + dr[d], nc = c + dc[d];
                if (ok(sz, nr, nc) && board[nr][nc] == p) connections++;
            }
            defensive_shape += connections * 15;
        } else if (board[r][c] == op) {
            int connections = 0;
            for (int d = 0; d < 4; ++d) {
                static const int dr[] = {0, 1, 1, 1};
                static const int dc[] = {1, 0, 1, -1};
                int nr = r + dr[d], nc = c + dc[d];
                if (ok(sz, nr, nc) && board[nr][nc] == op) connections++;
            }
            defensive_shape -= connections * 15;
        }
    }
    return potential + board_shape_score(board, sz, p) - board_shape_score(board, sz, op) + defensive_shape / 3;
}

inline int style_move_bonus(int board[kMaxBoard][kMaxBoard], int sz, int p, Move move,
                            const OpponentStyle* style) {
    if (!style || style->player != p || style->observations == 0) return 0;
    const int op = 3 - p;
    const double confidence = style->confidence();
    const int attack = get_thr(board, sz, move.r, move.c, p);
    const int defend = get_thr(board, sz, move.r, move.c, op);
    int nearby = 0;
    for (int rr = -2; rr <= 2; ++rr) for (int cc = -2; cc <= 2; ++cc) {
        if ((rr != 0 || cc != 0) && ok(sz, move.r + rr, move.c + cc) && board[move.r + rr][move.c + cc]) nearby++;
    }
    const int center_distance = std::abs(move.r - sz / 2) + std::abs(move.c - sz / 2);
    const double center = clamp01(1.0 - static_cast<double>(center_distance) / std::max(1, sz - 1));

    static const int dr[] = {0, 1, 1, 1};
    static const int dc[] = {1, 0, 1, -1};
    double direction_match = 0.0, direction_total = 0.0;
    for (int d = 0; d < 4; ++d) {
        ThreatInfo info = evaluate_direction(board, sz, move.r, move.c, dr[d], dc[d], p);
        double strength = 1.0 + info.longest * 80.0 + std::min(info.score, 100000) / 100.0;
        direction_total += strength;
        direction_match += strength * style->direction_preference[d];
    }
    direction_match = direction_total > 0.0 ? clamp01(direction_match * 4.0 / direction_total) : 0.5;

    double bonus = 0.0;
    bonus += attack * (style->attack_preference - 0.5) * 4.0;
    bonus += defend * (style->defense_preference - 0.5) * 4.0;
    bonus += (nearby / 6.0) * (style->locality_preference - 0.5) * 2400.0;
    bonus += center * (style->center_preference - 0.5) * 1800.0;
    bonus += (direction_match - 0.5) * 1600.0;
    return static_cast<int>(bonus * confidence);
}

inline int stale_continuation_penalty(int board[kMaxBoard][kMaxBoard], int sz, int p,
                                      Move move, Move last_own) {
    if (!ok(sz, last_own.r, last_own.c) || board[last_own.r][last_own.c] != p) return 0;
    if (std::max(std::abs(move.r - last_own.r), std::abs(move.c - last_own.c)) > 3) return 0;
    const int op = 3 - p;
    ThreatInfo info = analyze_move(board, sz, move.r, move.c, p);
    int defend = get_thr(board, sz, move.r, move.c, op);
    if (info.count[TH_FIVE] || info.count[TH_OPEN_FOUR] || info.count[TH_BROKEN_FOUR] ||
        info.count[TH_CLOSED_FOUR] || info.count[TH_OPEN_THREE] ||
        info.count[TH_BROKEN_THREE] || defend >= 18000) return 0;

    static const int dr[] = {0, 1, 1, 1};
    static const int dc[] = {1, 0, 1, -1};
    int dead_axes = 0;
    for (int d = 0; d < 4; ++d) {
        int own_neighbors = 0;
        int ends_blocked = 0;
        for (int sign : {-1, 1}) {
            int r = move.r + dr[d] * sign, c = move.c + dc[d] * sign;
            while (ok(sz, r, c) && board[r][c] == p) {
                own_neighbors++;
                r += dr[d] * sign;
                c += dc[d] * sign;
            }
            if (!ok(sz, r, c) || board[r][c] == op) ends_blocked++;
        }
        if (own_neighbors > 0 && ends_blocked == 2) dead_axes++;
    }
    int nearby_own = 0;
    for (int rr = -2; rr <= 2; ++rr) for (int cc = -2; cc <= 2; ++cc) {
        if ((rr != 0 || cc != 0) && ok(sz, move.r + rr, move.c + cc) &&
            board[move.r + rr][move.c + cc] == p) nearby_own++;
    }
    int penalty = dead_axes * 60000;
    if (info.score < 1200 && nearby_own >= 2) penalty += (nearby_own - 1) * 6000;
    return penalty;
}

inline std::vector<std::pair<int, Move>> ranked_candidates(int board[kMaxBoard][kMaxBoard], int sz,
                                                           int p, int limit,
                                                           const OpponentStyle* style = nullptr,
                                                           Move last_own = {-1, -1}) {
    int op = 3 - p;
    struct CandidateRank {
        int base = 0;
        int styled = 0;
        Move move;
    };
    std::vector<CandidateRank> all;
    all.reserve(sz * sz / 3);
    for (Move m : generate_candidates(board, sz, 2)) {
        if (p == 1 && is_forbidden_move(board, sz, m.r, m.c)) continue;
        int attack = get_thr(board, sz, m.r, m.c, p);
        int defend = get_thr(board, sz, m.r, m.c, op);
        int center = std::max(0, sz - (std::abs(m.r - sz / 2) + std::abs(m.c - sz / 2)));
        // Enhanced defensive priority: when opponent has high threats, defensive weight increases (Rapfi/Carbon hybrid)
        int defensive_priority = 0;
        if (defend >= 95000) defensive_priority = defend * 2;  // Must block forced wins
        else if (defend >= 18000) defensive_priority = defend;  // Strong defensive priority
        else if (defend >= 5000) defensive_priority = defend / 2; // Moderate defense
        int base = attack * 12 + defend * 15 + defensive_priority + center;
        base -= stale_continuation_penalty(board, sz, p, m, last_own);
        // Style may promote likely human replies, but never demote a sound
        // baseline reply.  This prevents overfitting to a few human moves.
        int styled = base + std::max(0, style_move_bonus(board, sz, p, m, style));
        all.push_back({base, styled, m});
    }
    auto by_base = all;
    auto by_styled = all;
    auto tie = [](const CandidateRank& a, const CandidateRank& b, bool styled) {
        int av = styled ? a.styled : a.base;
        int bv = styled ? b.styled : b.base;
        if (av != bv) return av > bv;
        if (a.move.r != b.move.r) return a.move.r < b.move.r;
        return a.move.c < b.move.c;
    };
    std::sort(by_base.begin(), by_base.end(), [&](const auto& a, const auto& b) { return tie(a, b, false); });
    std::sort(by_styled.begin(), by_styled.end(), [&](const auto& a, const auto& b) { return tie(a, b, true); });

    std::vector<CandidateRank> selected;
    selected.reserve(std::min<int>(limit, all.size()));
    auto add_unique = [&](const CandidateRank& candidate) {
        for (const auto& existing : selected) {
            if (existing.move.r == candidate.move.r && existing.move.c == candidate.move.c) return;
        }
        if (static_cast<int>(selected.size()) < limit) selected.push_back(candidate);
    };
    int baseline_keep = style && style->observations > 0 ? std::max(1, limit * 3 / 4) : limit;
    for (int i = 0; i < std::min<int>(baseline_keep, by_base.size()); ++i) add_unique(by_base[i]);
    for (const auto& candidate : by_styled) add_unique(candidate);
    std::sort(selected.begin(), selected.end(), [&](const auto& a, const auto& b) { return tie(a, b, true); });

    std::vector<std::pair<int, Move>> ranked;
    ranked.reserve(selected.size());
    for (const auto& candidate : selected) ranked.push_back({candidate.styled, candidate.move});
    return ranked;
}

inline int stone_count(const int board[kMaxBoard][kMaxBoard], int sz) {
    int count = 0;
    for (int r = 0; r < sz; ++r) for (int c = 0; c < sz; ++c) count += board[r][c] != 0;
    return count;
}

inline std::vector<std::pair<int, Move>> forcing_candidates(int board[kMaxBoard][kMaxBoard], int sz,
                                                            int p, int limit,
                                                            const OpponentStyle* style) {
    const int op = 3 - p;
    std::vector<std::pair<int, Move>> forcing;
    for (Move m : generate_candidates(board, sz, 2)) {
        if (p == 1 && is_forbidden_move(board, sz, m.r, m.c)) continue;
        int attack = get_thr(board, sz, m.r, m.c, p);
        int defend = get_thr(board, sz, m.r, m.c, op);
        // Closed/broken fours and stronger threats must be resolved before a
        // quiet evaluation, avoiding the old fixed-depth horizon failure.
        if (attack < 95000 && defend < 95000) continue;
        int priority = attack * 12 + defend * 15 + std::max(0, style_move_bonus(board, sz, p, m, style));
        forcing.push_back({priority, m});
    }
    std::sort(forcing.begin(), forcing.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    if (static_cast<int>(forcing.size()) > limit) forcing.resize(limit);
    return forcing;
}

struct TranspositionEntry {
    std::uint64_t key = 0;
    int value = 0;
    short depth = -1;
    unsigned char bound = 0;  // 1 exact, 2 lower, 3 upper.
};

struct TranspositionTable {
    explicit TranspositionTable(size_t size = 1u << 16) : entries(size) {}
    std::vector<TranspositionEntry> entries;
};

inline std::uint64_t mix_hash(std::uint64_t value) {
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}

inline std::uint64_t stone_hash(int r, int c, int p) {
    return mix_hash(static_cast<std::uint64_t>((r * kMaxBoard + c) * 3 + p));
}

inline std::uint64_t side_hash(int p) {
    return mix_hash(0x5a17ULL + static_cast<std::uint64_t>(p));
}

inline std::uint64_t position_hash(const int board[kMaxBoard][kMaxBoard], int sz, int p) {
    std::uint64_t hash = side_hash(p);
    for (int r = 0; r < sz; ++r) for (int c = 0; c < sz; ++c) {
        if (board[r][c]) hash ^= stone_hash(r, c, board[r][c]);
    }
    return hash;
}

inline bool has_forced_four_win(const int board[kMaxBoard][kMaxBoard], int sz, int p,
                                int max_attacks, int node_budget);

inline int negamax(int board[kMaxBoard][kMaxBoard], int sz, int p, int depth, int alpha, int beta,
                   Move last, int last_p, const OpponentStyle* style = nullptr, int quiet_depth = 2,
                   const std::atomic<bool>* cancel = nullptr,
                   const std::chrono::steady_clock::time_point* deadline = nullptr,
                   bool* timed_out = nullptr, std::uint64_t hash = 0,
                   TranspositionTable* table = nullptr) {
    if ((cancel && cancel->load()) ||
        (deadline && std::chrono::steady_clock::now() >= *deadline)) {
        if (timed_out && !(cancel && cancel->load())) *timed_out = true;
        return 0;
    }
    int op = 3 - p;
    if (last.r != -1 && check_win_at(board, sz, last.r, last.c, last_p)) {
        return last_p == p ? 200000000 + depth : -200000000 - depth;
    }
    if (hash == 0) hash = position_hash(board, sz, p);
    const int table_depth = depth * 4 + std::max(0, quiet_depth);
    const int original_alpha = alpha;
    const int original_beta = beta;
    TranspositionEntry* table_entry = nullptr;
    if (table && !table->entries.empty()) {
        table_entry = &table->entries[hash & (table->entries.size() - 1)];
        if (table_entry->key == hash && table_entry->depth >= table_depth) {
            if (table_entry->bound == 1) return table_entry->value;
            if (table_entry->bound == 2) alpha = std::max(alpha, table_entry->value);
            else if (table_entry->bound == 3) beta = std::min(beta, table_entry->value);
            if (alpha >= beta) return table_entry->value;
        }
    }
    auto save_table = [&](int value) {
        if (!table_entry || (timed_out && *timed_out) || (cancel && cancel->load())) return value;
        unsigned char bound = 1;
        if (value <= original_alpha) bound = 3;
        else if (value >= original_beta) bound = 2;
        if (table_entry->key != hash || table_entry->depth <= table_depth || bound == 1) {
            *table_entry = {hash, value, static_cast<short>(table_depth), bound};
        }
        return value;
    };
    if (depth == 0) {
        if (quiet_depth == 0) return save_table(static_eval(board, sz, p));
        auto forcing = forcing_candidates(board, sz, p, 4, style);
        if (forcing.empty()) return save_table(static_eval(board, sz, p));
        int best = -210000000;
        for (const auto& item : forcing) {
            Move m = item.second;
            board[m.r][m.c] = p;
            const std::uint64_t child_hash = hash ^ stone_hash(m.r, m.c, p) ^
                                             side_hash(p) ^ side_hash(op);
            int val = -negamax(board, sz, op, 0, -beta, -alpha, m, p, style,
                               quiet_depth - 1, cancel, deadline, timed_out,
                               child_hash, table);
            board[m.r][m.c] = 0;
            best = std::max(best, val);
            alpha = std::max(alpha, val);
            if (alpha >= beta) break;
        }
        return save_table(best);
    }
    const int stones = stone_count(board, sz);
    int width = depth >= 4 ? 8 : (depth == 3 ? 9 : (depth == 2 ? 11 : 8));
    if (stones >= 24) width += 1;
    if (stones >= 40) width += 1;
    auto moves = ranked_candidates(board, sz, p, width, style);
    if (moves.empty()) return save_table(0);
    int best = -210000000;
    for (auto item : moves) {
        if (deadline && std::chrono::steady_clock::now() >= *deadline) {
            if (timed_out) *timed_out = true;
            return 0;
        }
        Move m = item.second;
        board[m.r][m.c] = p;
        const std::uint64_t child_hash = hash ^ stone_hash(m.r, m.c, p) ^
                                         side_hash(p) ^ side_hash(op);
        int val = -negamax(board, sz, op, depth - 1, -beta, -alpha, m, p, style,
                           quiet_depth, cancel, deadline, timed_out, child_hash, table);
        board[m.r][m.c] = 0;
        best = std::max(best, val);
        alpha = std::max(alpha, val);
        if (alpha >= beta) break;
    }
    return save_table(best);
}

inline Move find_immediate_win(int board[kMaxBoard][kMaxBoard], int sz, int p) {
    for (Move m : generate_candidates(board, sz, 1)) {
        if (p == 1 && is_forbidden_move(board, sz, m.r, m.c)) continue;
        board[m.r][m.c] = p;
        bool win = check_win_at(board, sz, m.r, m.c, p);
        board[m.r][m.c] = 0;
        if (win) return m;
    }
    return {-1, -1};
}

inline bool has_forced_four_win(const int board[kMaxBoard][kMaxBoard], int sz, int p,
                                int max_attacks = 6, int node_budget = 900);

// Looks one ordinary reply beyond the VCF horizon.  A setup counts as proven
// only when one opponent reply leaves VCF after every legal answer, including
// counter-wins and Renju-specific legal defenses.  Timeout means "unknown",
// never a loss, so this guard cannot reject a move on incomplete work.
inline bool opponent_has_unavoidable_vcf_setup(
        const int board[kMaxBoard][kMaxBoard], int sz, int opponent,
        const std::chrono::steady_clock::time_point& deadline,
        int reply_width = 16, int vcf_node_budget = 80) {
    const int defender = 3 - opponent;
    int after_reply[kMaxBoard][kMaxBoard];
    std::memcpy(after_reply, board, sizeof(after_reply));
    auto replies = ranked_candidates(after_reply, sz, opponent, reply_width, nullptr);
    for (const auto& item : replies) {
        if (std::chrono::steady_clock::now() >= deadline) return false;
        const Move reply = item.second;
        after_reply[reply.r][reply.c] = opponent;
        if (check_win_at(after_reply, sz, reply.r, reply.c, opponent)) {
            after_reply[reply.r][reply.c] = 0;
            return true;
        }

        bool has_legal_answer = false;
        bool every_answer_loses = true;
        for (int r = 0; r < sz && every_answer_loses; ++r) {
            for (int c = 0; c < sz; ++c) {
                if (after_reply[r][c] != 0 ||
                    (defender == 1 && is_forbidden_move(after_reply, sz, r, c))) continue;
                has_legal_answer = true;
                after_reply[r][c] = defender;
                const bool survives = check_win_at(after_reply, sz, r, c, defender) ||
                    !has_forced_four_win(after_reply, sz, opponent, 6, vcf_node_budget);
                after_reply[r][c] = 0;
                if (survives) {
                    every_answer_loses = false;
                    break;
                }
                if (std::chrono::steady_clock::now() >= deadline) {
                    after_reply[reply.r][reply.c] = 0;
                    return false;
                }
            }
        }
        after_reply[reply.r][reply.c] = 0;
        if (has_legal_answer && every_answer_loses) return true;
    }
    return false;
}

// Optional search instrumentation.  Exposed so benchmarks and the CLI can
// report what the engine actually reached instead of guessing from wall time.
struct SearchStats {
    int budget_ms = 0;
    int elapsed_ms = 0;
    int completed_depth = 0;
    int nominal_depth = 0;
    int search_depth = 0;
    bool search_depth_complete = false;
    int roots = 0;
    int roots_completed = 0;
    bool nominal_depth_completed = false;
};

inline Move choose_ai_move(int board[kMaxBoard][kMaxBoard], int sz, int p = 2, int depth = 2,
                           const OpponentStyle* style = nullptr, int max_threads = 0,
                           Move last_own = {-1, -1}, const std::atomic<bool>* cancel = nullptr,
                           SearchStats* stats = nullptr) {
    if (cancel && cancel->load()) return {-1, -1};
    if (stats) *stats = SearchStats{};
    const auto started = std::chrono::steady_clock::now();
    auto elapsed_ms = [&]() {
        return static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count());
    };
    const int level = std::max(1, std::min(depth, 5));
    const int time_budget_ms = 1000 * (1 << (level - 1));
    const auto deadline = started + std::chrono::milliseconds(time_budget_ms);
    // Iterations that are already running may finish slightly past the soft
    // deadline; anything still unfinished at the hard stop is discarded.  This
    // converts "the budget expired mid-search" into "the last complete answer
    // stands", which is what makes iterative deepening safe here.
    // Overrun allowance for an iteration that is already running.  Capped so a
    // documented level budget stays close to its published value.
    const int overrun_ms = std::min(1000, time_budget_ms / 4 + 250);
    const auto hard_stop = deadline + std::chrono::milliseconds(overrun_ms);
    const int op = 3 - p;
    if (stats) stats->budget_ms = time_budget_ms;

    Move win = find_immediate_win(board, sz, p);
    if (win.r != -1) return win;
    Move block = find_immediate_win(board, sz, op);
    if (block.r != -1 && !(p == 1 && is_forbidden_move(board, sz, block.r, block.c))) return block;

    const int root_width = stone_count(board, sz) >= 30 ? 20 : 18;
    const int nominal_depth = depth <= 2 ? depth + 1 : depth;
    auto roots = ranked_candidates(board, sz, p, root_width, style, last_own);
    if (roots.empty()) return {-1, -1};
    const int root_count = static_cast<int>(roots.size());

    unsigned int available = std::max(1u, std::thread::hardware_concurrency());
    int worker_count = max_threads > 0 ? max_threads : static_cast<int>(available);
    worker_count = std::max(1, std::min<int>({worker_count, kMaxSearchThreads, root_count}));

    // `order` is the search order for the next iteration and is reshuffled by
    // every completed iteration, so deeper passes visit the promising moves
    // first.  `root_key` keeps the static ordering key for deterministic ties.
    std::vector<Move> order;
    std::vector<int> root_key;
    order.reserve(root_count);
    root_key.reserve(root_count);
    for (const auto& item : roots) {
        order.push_back(item.second);
        root_key.push_back(item.first);
    }

    // Results of the pass currently running.
    std::vector<int> depth_value;
    std::vector<unsigned char> depth_done;
    // 1 = exact score, 2 = lower bound (a better move was already found),
    // 3 = upper bound (fail low).  Only exact and lower-bound results carry an
    // ordering value high enough to be trusted at the root.
    std::vector<unsigned char> depth_bound;
    // Durable best-so-far, kept separately so a pass that stops halfway can
    // never erase the answer of the last pass that actually finished.  Values
    // are only comparable within one depth, so `best_value` is depth-tagged.
    Move best_move{-1, -1};
    int best_value = -210000000;
    int best_depth = 0;
    bool best_is_complete = false;
    int completed_depth = 0;
    int completed_count = 0;
    bool nominal_completed = false;
    // Sequential (single-worker) cost of the last completed iteration, used to
    // project what the next depth costs before committing to it.
    double sequential_ms = 0.0;

    // Depth ladder: every two plies plus a cheap ordering pass.  Odd and even
    // depths are kept on the same parity as the target because threat chains
    // alternate sides, so a same-parity ladder reuses the previous ordering.
    std::vector<int> ladder;
    for (int d = 1; d <= nominal_depth; ++d) {
        if (d == nominal_depth || d == 1 || (d % 2) == (nominal_depth % 2)) ladder.push_back(d);
    }

    auto ms_left = [&](const std::chrono::steady_clock::time_point& until) {
        return std::chrono::duration<double, std::milli>(
            until - std::chrono::steady_clock::now()).count();
    };

    // Analyzes one root move at `search_depth`.  Returns false when the search
    // hit the stop point, in which case `value_out` is meaningless.
    auto analyze_root = [&](int index, int search_depth, int alpha, int beta,
                            const std::chrono::steady_clock::time_point& stop,
                            TranspositionTable& table, int& value_out) {
        int local[kMaxBoard][kMaxBoard];
        std::memcpy(local, board, sizeof(local));
        const Move m = order[index];
        local[m.r][m.c] = p;
        if (check_win_at(local, sz, m.r, m.c, p)) {
            value_out = 200000000 + search_depth;
            return true;
        }
        if (has_forced_four_win(local, sz, op, 6, 320)) {
            value_out = -190000000 - search_depth;
            return true;
        }
        bool root_timed_out = false;
        const std::uint64_t hash = position_hash(local, sz, op);
        value_out = -negamax(local, sz, op, search_depth, -beta, -alpha, m, p, style, 2,
                             cancel, &stop, &root_timed_out, hash, &table);
        return !root_timed_out && !(cancel && cancel->load()) &&
               std::chrono::steady_clock::now() < stop;
    };

    // One iteration over all roots.  `aspirate` searches the first move with a
    // full window and every later move against the best value found so far; a
    // later move only replaces the leader when it proves strictly better, which
    // is exactly the comparison the root needs and much cheaper than a full
    // window.  A partial, aspirated pass is therefore still decisive.
    auto run_iteration = [&](int search_depth, bool aspirate,
                             const std::chrono::steady_clock::time_point& stop,
                             bool require_complete, double& sequential_out,
                             bool& fully_completed) {
        depth_value.assign(root_count, -210000000);
        depth_done.assign(root_count, 0);
        depth_bound.assign(root_count, 0);
        sequential_out = 0.0;
        fully_completed = false;
        std::atomic<size_t> next{0};
        std::atomic<int> leader{-210000000};
        std::atomic<long long> sequential_us{0};
        std::atomic<bool> aborted{false};
        auto worker = [&]() {
            // One table per worker, kept across roots so positions reached by
            // different move orders are reused (same policy as before).
            TranspositionTable table;
            while (true) {
                if (cancel && cancel->load()) { aborted = true; return; }
                if (aborted.load()) return;
                if (aspirate && std::chrono::steady_clock::now() >= stop) return;
                const size_t index = next.fetch_add(1);
                if (index >= static_cast<size_t>(root_count)) return;
                const int alpha = aspirate ? leader.load() : -210000000;
                const auto root_started = std::chrono::steady_clock::now();
                int value = 0;
                const bool ok = analyze_root(static_cast<int>(index), search_depth,
                                             alpha, 210000000, stop, table, value);
                sequential_us += static_cast<long long>(
                    std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now() - root_started).count());
                if (!ok) {
                    if (require_complete) aborted = true;
                    return;
                }
                depth_value[index] = value;
                depth_done[index] = 1;
                depth_bound[index] = aspirate && value <= alpha ? 3 : (alpha > -210000000 ? 2 : 1);
                if (aspirate) {
                    int current = leader.load();
                    while (value > current && !leader.compare_exchange_weak(current, value)) {}
                }
            }
        };
        std::vector<std::thread> workers;
        workers.reserve(worker_count);
        for (int i = 0; i < worker_count; ++i) workers.emplace_back(worker);
        for (auto& thread : workers) thread.join();
        sequential_out = static_cast<double>(sequential_us.load()) / 1000.0;
        if (aborted.load()) {
            depth_done.assign(root_count, 0);
            depth_bound.assign(root_count, 0);
            return false;
        }
        fully_completed = true;
        for (unsigned char flag : depth_done) fully_completed = fully_completed && flag;
        return fully_completed;
    };

    // Reorders the next iteration by the values just found (PV-first search).
    // Only `order` and the static keys are permuted: the per-root value arrays
    // belong to the pass that produced them and are consumed by absorb_pass
    // before the next pass reuses those buffers.
    auto adopt_ordering = [&](bool require_complete) {
        std::vector<int> index_order(root_count);
        for (int i = 0; i < root_count; ++i) index_order[i] = i;
        std::stable_sort(index_order.begin(), index_order.end(), [&](int a, int b) {
            if (!require_complete) {
                const bool da = depth_done[a] != 0, db = depth_done[b] != 0;
                if (da != db) return da;
            }
            if (depth_value[a] != depth_value[b]) return depth_value[a] > depth_value[b];
            return root_key[a] > root_key[b];
        });
        std::vector<Move> reordered;
        std::vector<int> reordered_key;
        reordered.reserve(root_count);
        reordered_key.reserve(root_count);
        for (int i : index_order) {
            reordered.push_back(order[i]);
            reordered_key.push_back(root_key[i]);
        }
        order.swap(reordered);
        root_key.swap(reordered_key);
    };

    // Iterative deepening.  The selected level is a guarantee: every pass up
    // to the nominal depth is attempted (a pass that starts may overrun the
    // soft deadline slightly so its result stays usable).  Anything left over
    // is spent on extra plies, one level at a time, until a pass no longer
    // fits.  Before starting a pass, the previous pass's measured cost is
    // projected forward, so the engine does not begin work it cannot finish.
    constexpr double kNodeGrowthPerPly = 2.4;  // calibrated on this search
    constexpr double kFitFraction = 0.75;      // leave headroom for jitter
    constexpr int kMaxExtraDepth = 2;
    std::vector<int> plan = ladder;
    for (int extra = 1; extra <= kMaxExtraDepth && nominal_depth + extra <= 12; ++extra)
        plan.push_back(nominal_depth + extra);

    // Folds the finished pass into the durable record.  A complete pass always
    // wins; a partial pass only ever replaces a lower-depth (or equally deep but
    // incomplete) record, and only with a move that proved a strictly better
    // value than the standing answer.
    auto absorb_pass = [&](int pass_depth, bool complete) {
        if (complete) {
            int best_index = -1;
            for (int i = 0; i < root_count; ++i) {
                if (!depth_done[i]) continue;
                if (depth_bound[i] == 3) continue;  // fail low: no usable score
                if (best_index == -1 || depth_value[i] > depth_value[best_index] ||
                    (depth_value[i] == depth_value[best_index] &&
                     root_key[i] > root_key[best_index])) {
                    best_index = i;
                }
            }
            if (best_index == -1) {  // every root failed low; fall back to any result
                for (int i = 0; i < root_count; ++i) {
                    if (!depth_done[i]) continue;
                    if (best_index == -1 || depth_value[i] > depth_value[best_index])
                        best_index = i;
                }
            }
            if (best_index >= 0) {
                best_move = order[best_index];
                best_value = depth_value[best_index];
                best_depth = pass_depth;
                best_is_complete = true;
            }
            return;
        }
        // Partial pass: only trust a strictly better, non-fail-low result at a
        // depth that is at least as deep as the standing record, and keep the
        // best of them rather than whichever happened to finish last.
        int improved = -1;
        for (int i = 0; i < root_count; ++i) {
            if (!depth_done[i] || depth_bound[i] == 3) continue;
            if (pass_depth < best_depth) continue;
            if (best_value != -210000000 && depth_value[i] <= best_value) continue;
            if (improved == -1 || depth_value[i] > depth_value[improved] ||
                (depth_value[i] == depth_value[improved] && root_key[i] > root_key[improved]))
                improved = i;
        }
        if (improved >= 0) {
            best_move = order[improved];
            best_value = depth_value[improved];
            best_depth = pass_depth;
            best_is_complete = false;
        }
    };

    for (size_t step = 0; step < plan.size() && !(cancel && cancel->load()); ++step) {
        const int candidate_depth = plan[step];
        const bool is_nominal = candidate_depth == nominal_depth;
        const bool is_extra = candidate_depth > nominal_depth;
        const bool have_estimate = completed_depth > 0;
        const double per_root_ms = have_estimate
            ? sequential_ms / std::max(1, completed_count) : 0.0;
        const double growth = have_estimate && candidate_depth > completed_depth
            ? std::pow(kNodeGrowthPerPly, candidate_depth - completed_depth) : 1.0;
        const double projected_wall_ms =
            per_root_ms * root_count * growth / std::max(1, worker_count);
        const bool fits_complete = !have_estimate ||
            projected_wall_ms <= kFitFraction * ms_left(deadline);
        // The nominal depth is promised by the level, and the very first pass
        // has no estimate to go on, so both are attempted regardless.
        const bool must_try = is_nominal || !have_estimate;

        if (fits_complete) {
            double measured = 0.0;
            bool complete = false;
            run_iteration(candidate_depth, false, hard_stop, true, measured, complete);
            if (!complete) break;
            absorb_pass(candidate_depth, true);
            adopt_ordering(true);
            sequential_ms = measured;
            completed_count = root_count;
            completed_depth = candidate_depth;
            if (is_nominal || is_extra) nominal_completed = true;
        } else if (must_try) {
            // Out of comfortable time but this depth was promised.  Try a clean
            // pass inside the hard stop; otherwise fall back to an aspirated
            // pass bounded by the soft deadline, then stop climbing.
            double measured = 0.0;
            bool complete = false;
            run_iteration(candidate_depth, false, hard_stop, true, measured, complete);
            if (complete) {
                absorb_pass(candidate_depth, true);
                adopt_ordering(true);
                sequential_ms = measured;
                completed_count = root_count;
                completed_depth = candidate_depth;
                if (is_nominal) nominal_completed = true;
            } else {
                // The full pass was discarded, so the ordering still reflects
                // the previous pass; keep it and search with aspiration.
                complete = false;
                run_iteration(candidate_depth, true, deadline, false, measured, complete);
                absorb_pass(candidate_depth, false);
                if (complete) {
                    adopt_ordering(false);
                    completed_count = 0;
                    for (unsigned char flag : depth_done) completed_count += flag ? 1 : 0;
                }
                break;
            }
        } else if (is_extra) {
            // Only a partial pass fits.  Searching the ordered candidates with
            // aspiration is safe: a move displaces the leader only by proving a
            // strictly better value, and the last complete answer is the floor.
            double measured = 0.0;
            bool complete = false;
            run_iteration(candidate_depth, true, deadline, false, measured, complete);
            absorb_pass(candidate_depth, false);
            if (complete) {
                adopt_ordering(false);
                completed_depth = candidate_depth;
                completed_count = 0;
                for (unsigned char flag : depth_done) completed_count += flag ? 1 : 0;
            }
            break;
        } else {
            // Pruned intermediate pass that does not fit; skip it and let the
            // next (deeper, promised) pass use the time.
            continue;
        }
    }

    if (stats) {
        stats->completed_depth = completed_depth;  // last pass that fully finished
        stats->nominal_depth = nominal_depth;
        stats->roots = root_count;
        stats->roots_completed = completed_count;
        stats->nominal_depth_completed = nominal_completed;
        stats->elapsed_ms = elapsed_ms();
    }
    // Fall back to the static ordering only if no pass produced any result
    // (every pass was cancelled), so a move is always returned.
    if (best_move.r == -1) {
        for (int i = 0; i < root_count; ++i) {
            if (!depth_done[i]) continue;
            best_move = order[i];
            break;
        }
    }
    if (best_move.r == -1) best_move = order[0];
    if (stats) {
        stats->search_depth = best_depth;       // depth that produced the answer
        stats->search_depth_complete = best_is_complete;
    }

    // Final defensive audit: if the selected move concedes an unavoidable VCF
    // setup, fall back to the next-best move that survives the same audit.
    int audit_board[kMaxBoard][kMaxBoard];
    std::memcpy(audit_board, board, sizeof(audit_board));
    audit_board[best_move.r][best_move.c] = p;
    const auto audit_budget = std::chrono::milliseconds(5000);
    if (opponent_has_unavoidable_vcf_setup(audit_board, sz, op,
                                           std::chrono::steady_clock::now() + audit_budget,
                                           12, 48)) {
        for (int i = 0; i < root_count; ++i) {
            const Move audit_move = order[i];
            if (audit_move.r == best_move.r && audit_move.c == best_move.c) continue;
            std::memcpy(audit_board, board, sizeof(audit_board));
            audit_board[audit_move.r][audit_move.c] = p;
            if (!opponent_has_unavoidable_vcf_setup(audit_board, sz, op,
                                                    std::chrono::steady_clock::now() + audit_budget,
                                                    12, 48)) {
                return audit_move;
            }
        }
    }
    return best_move;
}

// ---------------------------------------------------------------------------
// Chess.com-style win-chance model for the review graph.
//
// The graph answers one question per position: how likely is Black to win
// with this exact board and this player to move?  Forced results pin the
// value at 100/0, everything else runs through the Chess.com logistic curve
// applied to a tempo-aware evaluation.  The old tanh mapping ignored whose
// turn it was, so identical boards got identical chances even when the
// initiative flipped the outcome.
// ---------------------------------------------------------------------------

// Distinct empty squares where p would complete five immediately.  Two or
// more cannot be covered by a single block, which decides the game outright.
inline int five_completion_squares(int board[kMaxBoard][kMaxBoard], int sz,
                                   int p, int wanted = 2) {
    static const int dr[] = {0, 1, 1, 1};
    static const int dc[] = {1, 0, 1, -1};
    int found = 0;
    for (Move m : generate_candidates(board, sz, 2)) {
        if (p == 1 && is_forbidden_move(board, sz, m.r, m.c)) continue;
        bool completes = false;
        for (int d = 0; d < 4 && !completes; ++d)
            completes = count_line(board, sz, m.r, m.c, dr[d], dc[d], p) >= 5;
        if (completes && ++found >= wanted) return found;
    }
    return found;
}

// Continuous-four (VCF) probe.  The attacker only plays four threats, every
// one forcing the reply, so the branching factor stays tiny.  The defender
// model always blocks, which is exact unless the defender holds a five point
// of their own; callers guarantee that never happens.  Attack and node
// budgets bound the worst case for post-game replay loops.
inline bool forced_four_search(int board[kMaxBoard][kMaxBoard], int sz, int p,
                               int attacks_left, int& budget) {
    if (attacks_left <= 0 || budget <= 0) return false;
    --budget;
    const int op = 3 - p;
    // The defender would simply convert their own five instead of blocking.
    if (find_immediate_win(board, sz, op).r != -1) return false;

    struct FourCandidate { Move move; int score; };
    std::vector<FourCandidate> fours;
    for (Move m : generate_candidates(board, sz, 1)) {
        if (p == 1 && is_forbidden_move(board, sz, m.r, m.c)) continue;
        ThreatInfo info = analyze_move(board, sz, m.r, m.c, p);
        if (info.count[TH_FIVE]) return true;  // Five available right now.
        if (!info.count[TH_OPEN_FOUR] && !info.count[TH_BROKEN_FOUR] &&
            !info.count[TH_CLOSED_FOUR]) continue;
        fours.push_back({m, info.score});
    }
    std::sort(fours.begin(), fours.end(),
              [](const FourCandidate& a, const FourCandidate& b) { return a.score > b.score; });
    if (fours.size() > 10) fours.resize(10);

    static const int dr[] = {0, 1, 1, 1};
    static const int dc[] = {1, 0, 1, -1};
    for (const auto& item : fours) {
        Move f = item.move;
        board[f.r][f.c] = p;
        // Squares that would finish five after this four; two at once is
        // unstoppable, one must be blocked before anything else matters.
        Move block{-1, -1};
        bool double_threat = false;
        for (int rr = std::max(0, f.r - 4); rr <= std::min(sz - 1, f.r + 4) && !double_threat; ++rr) {
            for (int cc = std::max(0, f.c - 4); cc <= std::min(sz - 1, f.c + 4); ++cc) {
                if (board[rr][cc] != 0) continue;
                if (p == 1 && is_forbidden_move(board, sz, rr, cc)) continue;
                bool completes = false;
                for (int d = 0; d < 4 && !completes; ++d)
                    completes = count_line(board, sz, rr, cc, dr[d], dc[d], p) >= 5;
                if (!completes) continue;
                if (block.r == -1) block = {rr, cc};
                else { double_threat = true; break; }
            }
        }
        bool won = false;
        if (double_threat) {
            won = true;  // One block cannot cover two completion squares.
        } else if (block.r != -1) {
            if (op == 1 && is_forbidden_move(board, sz, block.r, block.c)) {
                won = true;  // The forced block is itself illegal for Black.
            } else {
                board[block.r][block.c] = op;
                won = forced_four_search(board, sz, p, attacks_left - 1, budget);
                board[block.r][block.c] = 0;
            }
        }
        board[f.r][f.c] = 0;
        if (won) return true;
        if (budget <= 0) break;
    }
    return false;
}

inline bool has_forced_four_win(const int board[kMaxBoard][kMaxBoard], int sz, int p,
                                int max_attacks, int node_budget) {
    int local[kMaxBoard][kMaxBoard];
    std::memcpy(local, board, sizeof(local));
    int budget = node_budget;
    return forced_four_search(local, sz, p, max_attacks, budget);
}

// static_eval ingredients recombined for tempo.  Potential entries here are
// place-a-stone scores, which run one conversion ahead of reality (an open
// three already reads as a four-class number), so neither side counts at face
// value.  The player to move still converts first and earns the larger share,
// while the idle side's plan is discounted hardest because the mover answers
// it before it matures.  Positive favours the mover.
inline int mover_tempo_eval(int board[kMaxBoard][kMaxBoard], int sz, int p) {
    const int op = 3 - p;
    std::vector<int> own_scores, opponent_scores;
    auto candidates = generate_candidates(board, sz, 2);
    own_scores.reserve(candidates.size());
    opponent_scores.reserve(candidates.size());
    for (Move m : candidates) {
        int own = get_thr(board, sz, m.r, m.c, p);
        int opponent = get_thr(board, sz, m.r, m.c, op);
        // Five-completion squares never enter the potential: they are either
        // pinned outright or a mandatory block, both handled structurally.
        if (!analyze_move(board, sz, m.r, m.c, p).count[TH_FIVE]) {
            if (!(p == 1 && own >= 18000 && is_forbidden_move(board, sz, m.r, m.c)))
                own_scores.push_back(own);
        }
        if (!analyze_move(board, sz, m.r, m.c, op).count[TH_FIVE]) {
            if (!(op == 1 && opponent >= 18000 && is_forbidden_move(board, sz, m.r, m.c)))
                opponent_scores.push_back(opponent);
        }
    }
    const int mine = weighted_potential(own_scores);
    const int theirs = weighted_potential(opponent_scores);
    const int shape = board_shape_score(board, sz, p) - board_shape_score(board, sz, op);
    // Enhanced defensive urgency: when opponent holds multiple high threats,
    // the idle side's potential is discounted more aggressively (Rapfi-style tactical urgency)
    int defensive_urgency = 0;
    int opponent_high_threats = 0;
    for (int score : opponent_scores) {
        if (score >= 18000) opponent_high_threats++;
    }
    if (opponent_high_threats >= 2) defensive_urgency = opponent_high_threats * 8000;
    else if (opponent_high_threats >= 1) defensive_urgency = 4000;
    return mine * 52 / 100 - theirs * 35 / 100 + shape - defensive_urgency;
}

// Logistic slope calibrated against tempo-weighted outputs: one net open
// three with the move lands near 62%, answering one leaves the injured side
// about 58%, double threats push past 70%, and forced lines pin at the
// rails.  Same sigmoid family Chess.com applies to centipawns.
constexpr double kWinChanceScale = 1.15e-6;

inline double chance_from_mover_score(double score) {
    double logistic = 2.0 / (1.0 + std::exp(-score * kWinChanceScale));  // 0..2
    return 50.0 * logistic;  // 0..100, 50 at equality.
}

// Black's winning percentage (0-100) for the review graph.  `side_to_move`
// is 1 for Black or 2 for White and drives both forced-line probes and the
// tempo weighting, so the graph finally reacts to initiative swings.
inline double black_win_chance(int board[kMaxBoard][kMaxBoard], int sz, int side_to_move) {
    if (has_five_for(board, sz, 1)) return 100.0;
    if (has_five_for(board, sz, 2)) return 0.0;
    const int mover = side_to_move == 2 ? 2 : 1;
    const int idle = 3 - mover;
    if (find_immediate_win(board, sz, mover).r != -1) return mover == 1 ? 100.0 : 0.0;
    const int completions = five_completion_squares(board, sz, idle, 2);
    if (completions >= 2) return idle == 1 ? 100.0 : 0.0;
    // While the idle side holds even one five point they convert it instead
    // of blocking, so a forcing chain only proves anything without it.
    if (completions == 0 && has_forced_four_win(board, sz, mover)) return mover == 1 ? 100.0 : 0.0;
    double score = static_cast<double>(mover_tempo_eval(board, sz, mover));
    // A lone five point on the idle side is a mandatory block: the mover
    // spends this turn answering it and forfeits the initiative.
    if (completions == 1) score -= 150000.0;
    double chance = chance_from_mover_score(score);
    chance = std::min(99.0, std::max(1.0, chance));
    return mover == 1 ? chance : 100.0 - chance;
}

enum class EngineVersion {
    LegacyV1 = 0,
    StrengthenedV2 = 1,
    CurrentV3 = 2,
    CarbonClassic = 3,
    Rapfi2025 = 4,
    AlphaGomoku2026 = 5,
    XLNiren = 6,
    Count = 7
};

inline const char* engine_version_name(EngineVersion version) {
    switch (version) {
        case EngineVersion::LegacyV1: return "Legacy v1";
        case EngineVersion::StrengthenedV2: return "Strengthened v2";
        case EngineVersion::CurrentV3: return "Current v3 MT";
        case EngineVersion::CarbonClassic: return "Carbon Classic";
        case EngineVersion::Rapfi2025: return "Rapfi 2025 AVX2";
        case EngineVersion::AlphaGomoku2026: return "AlphaGomoku MK26";
        case EngineVersion::XLNiren: return "XL / Niren";
        default: return "Unknown";
    }
}

// Exact search profile used by the original July engine.  Pattern recognition
// and Renju legality are shared because those routines did not change.
inline int legacy_static_eval(int board[kMaxBoard][kMaxBoard], int sz, int p) {
    int op = 3 - p;
    int score = 0;
    for (Move m : generate_candidates(board, sz, 2)) {
        score += get_thr(board, sz, m.r, m.c, p);
        score -= get_thr(board, sz, m.r, m.c, op) * 13 / 10;
    }
    return score;
}

inline std::vector<std::pair<int, Move>> legacy_ranked_candidates(
        int board[kMaxBoard][kMaxBoard], int sz, int p, int limit) {
    int op = 3 - p;
    std::vector<std::pair<int, Move>> ranked;
    for (Move m : generate_candidates(board, sz, 2)) {
        if (p == 1 && is_forbidden_move(board, sz, m.r, m.c)) continue;
        int attack = get_thr(board, sz, m.r, m.c, p);
        int defend = get_thr(board, sz, m.r, m.c, op);
        int center = std::max(0, sz - (std::abs(m.r - sz / 2) + std::abs(m.c - sz / 2)));
        ranked.push_back({attack * 12 + defend * 15 + center, m});
    }
    std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    if (static_cast<int>(ranked.size()) > limit) ranked.resize(limit);
    return ranked;
}

inline int legacy_negamax(int board[kMaxBoard][kMaxBoard], int sz, int p, int depth,
                          int alpha, int beta, Move last, int last_p,
                          const std::atomic<bool>* cancel = nullptr) {
    if (cancel && cancel->load()) return 0;
    int op = 3 - p;
    if (last.r != -1 && check_win_at(board, sz, last.r, last.c, last_p))
        return last_p == p ? 200000000 + depth : -200000000 - depth;
    if (depth == 0) return legacy_static_eval(board, sz, p);
    auto moves = legacy_ranked_candidates(board, sz, p, depth >= 2 ? 12 : 8);
    if (moves.empty()) return legacy_static_eval(board, sz, p);
    int best = -210000000;
    for (const auto& item : moves) {
        Move m = item.second;
        board[m.r][m.c] = p;
        int value = -legacy_negamax(board, sz, op, depth - 1, -beta, -alpha, m, p, cancel);
        board[m.r][m.c] = 0;
        best = std::max(best, value);
        alpha = std::max(alpha, value);
        if (alpha >= beta) break;
    }
    return best;
}

inline Move legacy_choose_move(int board[kMaxBoard][kMaxBoard], int sz, int p, int depth,
                               const std::atomic<bool>* cancel = nullptr) {
    if (cancel && cancel->load()) return {-1, -1};
    int op = 3 - p;
    Move win = find_immediate_win(board, sz, p);
    if (win.r != -1) return win;
    Move block = find_immediate_win(board, sz, op);
    if (block.r != -1 && !(p == 1 && is_forbidden_move(board, sz, block.r, block.c))) return block;
    int best_score = -210000000;
    Move best{-1, -1};
    for (const auto& item : legacy_ranked_candidates(board, sz, p, 18)) {
        if (cancel && cancel->load()) return {-1, -1};
        Move m = item.second;
        board[m.r][m.c] = p;
        int value = -legacy_negamax(board, sz, op, depth, -210000000, 210000000, m, p, cancel);
        board[m.r][m.c] = 0;
        value += item.first / 20;
        if (value > best_score) {
            best_score = value;
            best = m;
        }
    }
    return best;
}

// August v2 profile: stable long-game evaluation, forcing extension and the
// first opponent model, before conservative style retention/root MT was added.
inline std::vector<std::pair<int, Move>> v2_ranked_candidates(
        int board[kMaxBoard][kMaxBoard], int sz, int p, int limit, const OpponentStyle* style) {
    int op = 3 - p;
    std::vector<std::pair<int, Move>> ranked;
    for (Move m : generate_candidates(board, sz, 2)) {
        if (p == 1 && is_forbidden_move(board, sz, m.r, m.c)) continue;
        int attack = get_thr(board, sz, m.r, m.c, p);
        int defend = get_thr(board, sz, m.r, m.c, op);
        int center = std::max(0, sz - (std::abs(m.r - sz / 2) + std::abs(m.c - sz / 2)));
        ranked.push_back({attack * 12 + defend * 15 + center +
                          style_move_bonus(board, sz, p, m, style), m});
    }
    std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    if (static_cast<int>(ranked.size()) > limit) ranked.resize(limit);
    return ranked;
}

inline std::vector<std::pair<int, Move>> v2_forcing_candidates(
        int board[kMaxBoard][kMaxBoard], int sz, int p, int limit, const OpponentStyle* style) {
    int op = 3 - p;
    std::vector<std::pair<int, Move>> forcing;
    for (Move m : generate_candidates(board, sz, 2)) {
        if (p == 1 && is_forbidden_move(board, sz, m.r, m.c)) continue;
        int attack = get_thr(board, sz, m.r, m.c, p);
        int defend = get_thr(board, sz, m.r, m.c, op);
        if (attack < 95000 && defend < 95000) continue;
        forcing.push_back({attack * 12 + defend * 15 + style_move_bonus(board, sz, p, m, style), m});
    }
    std::sort(forcing.begin(), forcing.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    if (static_cast<int>(forcing.size()) > limit) forcing.resize(limit);
    return forcing;
}

inline int v2_negamax(int board[kMaxBoard][kMaxBoard], int sz, int p, int depth,
                      int alpha, int beta, Move last, int last_p,
                      const OpponentStyle* style, int quiet_depth = 2,
                      const std::atomic<bool>* cancel = nullptr) {
    if (cancel && cancel->load()) return 0;
    int op = 3 - p;
    if (last.r != -1 && check_win_at(board, sz, last.r, last.c, last_p))
        return last_p == p ? 200000000 + depth : -200000000 - depth;
    if (depth == 0) {
        if (quiet_depth == 0) return static_eval(board, sz, p);
        auto moves = v2_forcing_candidates(board, sz, p, 4, style);
        if (moves.empty()) return static_eval(board, sz, p);
        int best = -210000000;
        for (const auto& item : moves) {
            Move m = item.second;
            board[m.r][m.c] = p;
            int value = -v2_negamax(board, sz, op, 0, -beta, -alpha, m, p, style,
                                    quiet_depth - 1, cancel);
            board[m.r][m.c] = 0;
            best = std::max(best, value);
            alpha = std::max(alpha, value);
            if (alpha >= beta) break;
        }
        return best;
    }
    int width = depth >= 2 ? 12 : 8;
    int stones = stone_count(board, sz);
    if (stones >= 24) width += 2;
    if (stones >= 40) width += 2;
    auto moves = v2_ranked_candidates(board, sz, p, width, style);
    if (moves.empty()) return 0;
    int best = -210000000;
    for (const auto& item : moves) {
        Move m = item.second;
        board[m.r][m.c] = p;
        int value = -v2_negamax(board, sz, op, depth - 1, -beta, -alpha,
                                m, p, style, quiet_depth, cancel);
        board[m.r][m.c] = 0;
        best = std::max(best, value);
        alpha = std::max(alpha, value);
        if (alpha >= beta) break;
    }
    return best;
}

inline Move v2_choose_move(int board[kMaxBoard][kMaxBoard], int sz, int p, int depth,
                           const OpponentStyle* style,
                           const std::atomic<bool>* cancel = nullptr) {
    if (cancel && cancel->load()) return {-1, -1};
    int op = 3 - p;
    Move win = find_immediate_win(board, sz, p);
    if (win.r != -1) return win;
    Move block = find_immediate_win(board, sz, op);
    if (block.r != -1 && !(p == 1 && is_forbidden_move(board, sz, block.r, block.c))) return block;
    int best_score = -210000000;
    Move best{-1, -1};
    int root_width = stone_count(board, sz) >= 30 ? 20 : 18;
    for (const auto& item : v2_ranked_candidates(board, sz, p, root_width, style)) {
        if (cancel && cancel->load()) return {-1, -1};
        Move m = item.second;
        board[m.r][m.c] = p;
        int value = -v2_negamax(board, sz, op, depth, -210000000, 210000000, m, p,
                                style, 2, cancel);
        board[m.r][m.c] = 0;
        value += item.first / 20;
        if (value > best_score) {
            best_score = value;
            best = m;
        }
    }
    return best;
}

inline Move choose_versioned_move(int board[kMaxBoard][kMaxBoard], int sz, int p, int depth,
                                  EngineVersion version, const OpponentStyle* style = nullptr,
                                  int max_threads = 0, Move last_own = {-1, -1},
                                  const std::atomic<bool>* cancel = nullptr) {
    switch (version) {
        case EngineVersion::LegacyV1:
            return legacy_choose_move(board, sz, p, depth, cancel);
        case EngineVersion::StrengthenedV2:
            return v2_choose_move(board, sz, p, depth, style, cancel);
        case EngineVersion::CurrentV3:
            return choose_ai_move(board, sz, p, depth, style, max_threads, last_own, cancel);
        case EngineVersion::CarbonClassic:
        case EngineVersion::Rapfi2025:
        case EngineVersion::AlphaGomoku2026:
        case EngineVersion::XLNiren:
        default:
            return {-1, -1};  // External protocol adapter owns third-party execution.
    }
}

}  // namespace gomoku
