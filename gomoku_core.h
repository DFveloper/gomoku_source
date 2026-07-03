#pragma once

#include <algorithm>
#include <array>
#include <cstring>
#include <utility>
#include <vector>

namespace gomoku {

constexpr int kMaxBoard = 30;

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

inline std::vector<Move> generate_candidates(const int board[kMaxBoard][kMaxBoard], int sz, int radius = 2) {
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

inline int static_eval(int board[kMaxBoard][kMaxBoard], int sz, int p) {
    int op = 3 - p;
    int score = 0;
    for (Move m : generate_candidates(board, sz, 2)) {
        score += get_thr(board, sz, m.r, m.c, p);
        score -= get_thr(board, sz, m.r, m.c, op) * 13 / 10;
    }
    return score;
}

inline std::vector<std::pair<int, Move>> ranked_candidates(int board[kMaxBoard][kMaxBoard], int sz, int p, int limit) {
    int op = 3 - p;
    std::vector<std::pair<int, Move>> ranked;
    ranked.reserve(sz * sz / 3);
    for (Move m : generate_candidates(board, sz, 2)) {
        if (p == 1 && is_forbidden_move(board, sz, m.r, m.c)) continue;
        int attack = get_thr(board, sz, m.r, m.c, p);
        int defend = get_thr(board, sz, m.r, m.c, op);
        int center = std::max(0, sz - (std::abs(m.r - sz / 2) + std::abs(m.c - sz / 2)));
        ranked.push_back({attack * 12 + defend * 15 + center, m});
    }
    std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    if ((int)ranked.size() > limit) ranked.resize(limit);
    return ranked;
}

inline int negamax(int board[kMaxBoard][kMaxBoard], int sz, int p, int depth, int alpha, int beta, Move last, int last_p) {
    int op = 3 - p;
    if (last.r != -1 && check_win_at(board, sz, last.r, last.c, last_p)) {
        return last_p == p ? 200000000 + depth : -200000000 - depth;
    }
    if (depth == 0) return static_eval(board, sz, p);
    auto moves = ranked_candidates(board, sz, p, depth >= 2 ? 12 : 8);
    if (moves.empty()) return static_eval(board, sz, p);
    int best = -210000000;
    for (auto item : moves) {
        Move m = item.second;
        board[m.r][m.c] = p;
        int val = -negamax(board, sz, op, depth - 1, -beta, -alpha, m, p);
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

inline Move choose_ai_move(int board[kMaxBoard][kMaxBoard], int sz, int p = 2, int depth = 2) {
    int op = 3 - p;
    Move win = find_immediate_win(board, sz, p);
    if (win.r != -1) return win;
    Move block = find_immediate_win(board, sz, op);
    if (block.r != -1 && !(p == 1 && is_forbidden_move(board, sz, block.r, block.c))) return block;

    int best_score = -210000000;
    Move best{sz / 2, sz / 2};
    for (auto item : ranked_candidates(board, sz, p, 18)) {
        Move m = item.second;
        board[m.r][m.c] = p;
        if (check_win_at(board, sz, m.r, m.c, p)) {
            board[m.r][m.c] = 0;
            return m;
        }
        int val = -negamax(board, sz, op, depth, -210000000, 210000000, m, p);
        board[m.r][m.c] = 0;
        val += item.first / 20;
        if (val > best_score) {
            best_score = val;
            best = m;
        }
    }
    return best;
}

}  // namespace gomoku
