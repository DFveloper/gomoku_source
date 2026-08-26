#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
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
        {".XX.", TH_OPEN_TWO, 780},
        {".X.X.", TH_BROKEN_TWO, 560},
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

inline ThreatInfo evaluate_line(const char line[16]) {
    ThreatInfo info;
    int run = 0;
    for (int i = 0; i < 15; i++) {
        if (line[i] == 'X') info.longest = std::max(info.longest, ++run);
        else run = 0;
    }
    int best[TH_KIND_COUNT] = {};
    for (const PatternDef& p : pattern_table()) {
        int len = (int)std::strlen(p.pat);
        for (int i = 0; i + len <= 15; i++) {
            if (i > 7 || i + len <= 7) continue;
            if (!match_at(line, i, p.pat)) continue;
            info.count[p.kind]++;
            best[p.kind] = std::max(best[p.kind], p.score);
        }
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
        if (!(p == 1 && own >= 18000 && is_forbidden_move(board, sz, m.r, m.c))) own_scores.push_back(own);
        if (!(op == 1 && opponent >= 18000 && is_forbidden_move(board, sz, m.r, m.c))) opponent_scores.push_back(opponent);
    }
    const int potential = weighted_potential(own_scores) - weighted_potential(opponent_scores);
    return potential + board_shape_score(board, sz, p) - board_shape_score(board, sz, op);
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
        int base = attack * 12 + defend * 15 + center;
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

inline int negamax(int board[kMaxBoard][kMaxBoard], int sz, int p, int depth, int alpha, int beta,
                   Move last, int last_p, const OpponentStyle* style = nullptr, int quiet_depth = 2) {
    int op = 3 - p;
    if (last.r != -1 && check_win_at(board, sz, last.r, last.c, last_p)) {
        return last_p == p ? 200000000 + depth : -200000000 - depth;
    }
    if (depth == 0) {
        if (quiet_depth == 0) return static_eval(board, sz, p);
        auto forcing = forcing_candidates(board, sz, p, 4, style);
        if (forcing.empty()) return static_eval(board, sz, p);
        int best = -210000000;
        for (const auto& item : forcing) {
            Move m = item.second;
            board[m.r][m.c] = p;
            int val = -negamax(board, sz, op, 0, -beta, -alpha, m, p, style, quiet_depth - 1);
            board[m.r][m.c] = 0;
            best = std::max(best, val);
            alpha = std::max(alpha, val);
            if (alpha >= beta) break;
        }
        return best;
    }
    const int stones = stone_count(board, sz);
    int width = depth >= 4 ? 8 : (depth == 3 ? 9 : (depth == 2 ? 11 : 8));
    if (stones >= 24) width += 1;
    if (stones >= 40) width += 1;
    auto moves = ranked_candidates(board, sz, p, width, style);
    if (moves.empty()) return 0;
    int best = -210000000;
    for (auto item : moves) {
        Move m = item.second;
        board[m.r][m.c] = p;
        int val = -negamax(board, sz, op, depth - 1, -beta, -alpha, m, p, style, quiet_depth);
        board[m.r][m.c] = 0;
        best = std::max(best, val);
        alpha = std::max(alpha, val);
        if (alpha >= beta) break;
    }
    return best;
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

inline Move choose_ai_move(int board[kMaxBoard][kMaxBoard], int sz, int p = 2, int depth = 2,
                           const OpponentStyle* style = nullptr, int max_threads = 0,
                           Move last_own = {-1, -1}) {
    int op = 3 - p;
    Move win = find_immediate_win(board, sz, p);
    if (win.r != -1) return win;
    Move block = find_immediate_win(board, sz, op);
    if (block.r != -1 && !(p == 1 && is_forbidden_move(board, sz, block.r, block.c))) return block;

    int root_width = stone_count(board, sz) >= 30 ? 20 : 18;
    auto roots = ranked_candidates(board, sz, p, root_width, style, last_own);
    if (roots.empty()) return {-1, -1};
    std::vector<int> values(roots.size(), -210000000);
    std::atomic<size_t> next{0};
    unsigned int available = std::max(1u, std::thread::hardware_concurrency());
    int worker_count = max_threads > 0 ? max_threads : static_cast<int>(available);
    worker_count = std::max(1, std::min<int>({worker_count, kMaxSearchThreads,
                                              static_cast<int>(roots.size())}));

    auto worker = [&]() {
        while (true) {
            size_t index = next.fetch_add(1);
            if (index >= roots.size()) return;
            int local[kMaxBoard][kMaxBoard];
            std::memcpy(local, board, sizeof(local));
            Move m = roots[index].second;
            local[m.r][m.c] = p;
            if (check_win_at(local, sz, m.r, m.c, p)) {
                values[index] = 200000000 + depth;
            } else {
                values[index] = -negamax(local, sz, op, depth, -210000000, 210000000,
                                         m, p, style, 2);
            }
        }
    };
    std::vector<std::thread> workers;
    workers.reserve(worker_count);
    for (int i = 0; i < worker_count; ++i) workers.emplace_back(worker);
    for (auto& thread : workers) thread.join();

    int best_index = 0;
    for (size_t i = 1; i < roots.size(); ++i) {
        // Search value is authoritative.  Move-order heuristics only resolve
        // exact ties, so a stale plan can no longer override deeper evidence.
        if (values[i] > values[best_index] ||
            (values[i] == values[best_index] && roots[i].first > roots[best_index].first)) {
            best_index = static_cast<int>(i);
        }
    }
    return roots[best_index].second;
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
                          int alpha, int beta, Move last, int last_p) {
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
        int value = -legacy_negamax(board, sz, op, depth - 1, -beta, -alpha, m, p);
        board[m.r][m.c] = 0;
        best = std::max(best, value);
        alpha = std::max(alpha, value);
        if (alpha >= beta) break;
    }
    return best;
}

inline Move legacy_choose_move(int board[kMaxBoard][kMaxBoard], int sz, int p, int depth) {
    int op = 3 - p;
    Move win = find_immediate_win(board, sz, p);
    if (win.r != -1) return win;
    Move block = find_immediate_win(board, sz, op);
    if (block.r != -1 && !(p == 1 && is_forbidden_move(board, sz, block.r, block.c))) return block;
    int best_score = -210000000;
    Move best{sz / 2, sz / 2};
    for (const auto& item : legacy_ranked_candidates(board, sz, p, 18)) {
        Move m = item.second;
        board[m.r][m.c] = p;
        int value = -legacy_negamax(board, sz, op, depth, -210000000, 210000000, m, p);
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
                      const OpponentStyle* style, int quiet_depth = 2) {
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
            int value = -v2_negamax(board, sz, op, 0, -beta, -alpha, m, p, style, quiet_depth - 1);
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
                                m, p, style, quiet_depth);
        board[m.r][m.c] = 0;
        best = std::max(best, value);
        alpha = std::max(alpha, value);
        if (alpha >= beta) break;
    }
    return best;
}

inline Move v2_choose_move(int board[kMaxBoard][kMaxBoard], int sz, int p, int depth,
                           const OpponentStyle* style) {
    int op = 3 - p;
    Move win = find_immediate_win(board, sz, p);
    if (win.r != -1) return win;
    Move block = find_immediate_win(board, sz, op);
    if (block.r != -1 && !(p == 1 && is_forbidden_move(board, sz, block.r, block.c))) return block;
    int best_score = -210000000;
    Move best{-1, -1};
    int root_width = stone_count(board, sz) >= 30 ? 20 : 18;
    for (const auto& item : v2_ranked_candidates(board, sz, p, root_width, style)) {
        Move m = item.second;
        board[m.r][m.c] = p;
        int value = -v2_negamax(board, sz, op, depth, -210000000, 210000000, m, p, style, 2);
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
                                  int max_threads = 0, Move last_own = {-1, -1}) {
    switch (version) {
        case EngineVersion::LegacyV1:
            return legacy_choose_move(board, sz, p, depth);
        case EngineVersion::StrengthenedV2:
            return v2_choose_move(board, sz, p, depth, style);
        case EngineVersion::CurrentV3:
            return choose_ai_move(board, sz, p, depth, style, max_threads, last_own);
        case EngineVersion::CarbonClassic:
        case EngineVersion::Rapfi2025:
        case EngineVersion::AlphaGomoku2026:
        case EngineVersion::XLNiren:
        default:
            return {-1, -1};  // External protocol adapter owns third-party execution.
    }
}

}  // namespace gomoku
