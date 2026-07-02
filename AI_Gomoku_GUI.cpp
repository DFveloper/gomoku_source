#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#else
#include <X11/keysym.h>
#include <X11/extensions/XInput2.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <unistd.h>
#endif

using namespace std;

namespace {

constexpr int kMaxBoard = 30;
constexpr int kMinBoard = 9;
constexpr int kDefaultBoard = 15;
constexpr int kMinW = 900;
constexpr int kMinH = 620;

int sz = kDefaultBoard;
int brd[kMaxBoard][kMaxBoard];
bool dbg = false;
bool game_over = false;
int winner = 0;
bool fullscreen = false;
double target_aspect = 0.0;
int monitor_index = 0;
int physical_w = kMinW;
int physical_h = kMinH;
vector<pair<int, int>> log_v;
pair<int, int> last_move = {-1, -1};
pair<int, int> win_line_start = {-1, -1};
pair<int, int> win_line_end = {-1, -1};
bool review_mode = false;
int review_ply = 0;

struct MoveReview {
    int r = 0;
    int c = 0;
    int p = 0;
    double winrate = 50.0;
    string label = "Book";
};

vector<MoveReview> reviews;

struct Rect {
    int x, y, w, h;
};

struct Ui {
    int w = 1120, h = 720;
    int cell = 32, margin = 24;
    Rect board{};
    array<Rect, 6> buttons{};
} ui;

bool ok(int r, int c) { return r >= 0 && r < sz && c >= 0 && c < sz; }

string display_label(const string& label) {
    return label == "Missed Win" ? "Miss" : label;
}

double parse_aspect_value(const char* value) {
    if (!value || !*value) return 0.0;
    const char* colon = strchr(value, ':');
    if (colon) {
        double w = atof(value);
        double h = atof(colon + 1);
        return h > 0.0 ? w / h : 0.0;
    }
    double v = atof(value);
    return v > 0.0 ? v : 0.0;
}

void parse_args(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--fullscreen") == 0 || strcmp(argv[i], "-f") == 0) {
            fullscreen = true;
        } else if ((strcmp(argv[i], "--aspect") == 0 || strcmp(argv[i], "-a") == 0) && i + 1 < argc) {
            target_aspect = parse_aspect_value(argv[++i]);
        } else if ((strcmp(argv[i], "--board") == 0 || strcmp(argv[i], "-b") == 0) && i + 1 < argc) {
            int n = atoi(argv[++i]);
            if (n >= kMinBoard && n <= kMaxBoard) sz = n;
        } else if ((strcmp(argv[i], "--monitor") == 0 || strcmp(argv[i], "-m") == 0) && i + 1 < argc) {
            monitor_index = max(0, atoi(argv[++i]));
        } else {
            int n = atoi(argv[i]);
            if (n >= kMinBoard && n <= kMaxBoard) sz = n;
        }
    }
    const char* env_aspect = getenv("GOMOKU_ASPECT");
    if (target_aspect <= 0.0 && env_aspect) target_aspect = parse_aspect_value(env_aspect);
}

void set_surface_size(int w, int h) {
    physical_w = max(1, w);
    physical_h = max(1, h);
    if (target_aspect > 0.0) {
        ui.h = physical_h;
        ui.w = max(1, static_cast<int>(physical_h * target_aspect + 0.5));
    } else {
        ui.w = physical_w;
        ui.h = physical_h;
    }
}

int to_logical_x(int x) {
    return target_aspect > 0.0 ? static_cast<int>(x * (double)ui.w / physical_w + 0.5) : x;
}

int to_logical_y(int y) {
    return target_aspect > 0.0 ? static_cast<int>(y * (double)ui.h / physical_h + 0.5) : y;
}

int cnt_lin(int r, int c, int dr, int dc, int p) {
    int cnt = 1;
    int dir[] = {1, -1};
    for (int i = 0; i < 2; i++) {
        int nr = r + dr * dir[i], nc = c + dc * dir[i];
        while (ok(nr, nc) && brd[nr][nc] == p) {
            cnt++;
            nr += dr * dir[i];
            nc += dc * dir[i];
        }
    }
    return cnt;
}

  int get_thr(int r, int c, int p) {
      // Immediate block: any 5‑cell segment containing (r,c) with 3+ stones of player p.
      int dr[] = {0, 1, 1, 1, 0, -1, -1, -1};
      int dc[] = {1, 1, 0, -1, -1, -1, 0, 1};
      for (int d = 0; d < 8; ++d) {
          int cnt = 0;
          for (int k = -2; k <= 2; ++k) {
              int nr = r + dr[d] * k;
              int nc = c + dc[d] * k;
              if (!ok(nr, nc)) continue;
              if (brd[nr][nc] == p) cnt++;
          }
          if (cnt >= 3) return 10000;
      }

      // Heuristic score: centre preference + strongest direction threat count.
      const int center = sz / 2;
      const int dist = abs(r - center) + abs(c - center);
      int base = (sz - dist) * 10;          // closer to centre gets higher base
      int maxThreat = 0;
      for (int d = 0; d < 8; ++d) {
          int cnt = 0;
          for (int k = -2; k <= 2; ++k) {
              int nr = r + dr[d] * k;
              int nc = c + dc[d] * k;
              if (!ok(nr, nc)) continue;
              if (brd[nr][nc] == p) cnt++;
          }
          maxThreat = max(maxThreat, cnt);
      }
      int sc = base + maxThreat * 50;
      return sc;
  }

pair<int, int> vcf(int p, int dep) {
    if (dep <= 0) return {-1, -1};
    for (int r = 0; r < sz; r++) {
        for (int c = 0; c < sz; c++) {
            if (brd[r][c] != 0) continue;
            if (cnt_lin(r, c, 0, 1, p) >= 5 || cnt_lin(r, c, 1, 0, p) >= 5 ||
                cnt_lin(r, c, 1, 1, p) >= 5 || cnt_lin(r, c, 1, -1, p) >= 5) {
                return {r, c};
            }
            if (get_thr(r, c, p) >= 500) {
                brd[r][c] = p;
                pair<int, int> res = vcf(p, dep - 1);
                brd[r][c] = 0;
                if (res.first != -1) return {r, c};
            }
        }
    }
    return {-1, -1};
}

bool check_win_at(int r, int c, int p) {
    int dr[] = {0, 1, 1, 1}, dc[] = {1, 0, 1, -1};
    for (int i = 0; i < 4; i++) {
        if (cnt_lin(r, c, dr[i], dc[i], p) >= 5) {
            int sr = r, sc = c;
            int er = r, ec = c;
            while (ok(sr - dr[i], sc - dc[i]) && brd[sr - dr[i]][sc - dc[i]] == p) {
                sr -= dr[i];
                sc -= dc[i];
            }
            while (ok(er + dr[i], ec + dc[i]) && brd[er + dr[i]][ec + dc[i]] == p) {
                er += dr[i];
                ec += dc[i];
            }
            win_line_start = {sr, sc};
            win_line_end = {er, ec};
            return true;
        }
    }
    return false;
}

// Detects forbidden moves for black: 3‑in‑a‑row with both ends open (33),
// 4‑in‑a‑row with both ends open (44), or any 5‑in‑a‑row (장목).
// The function temporarily places a stone at (r, c), checks the patterns,
// and then restores the board. It returns true if the move would be
// forbidden.
bool is_forbidden_move(int r, int c) {
    // Place temporary stone.
    brd[r][c] = 1;
    bool forbidden = false;
    // Directions: horizontal, vertical, two diagonals.
    const int dr[] = {0, 1, 1, 1};
    const int dc[] = {1, 0, 1, -1};
    for (int d = 0; d < 4; ++d) {
        int left = 0, right = 0;
        // Count contiguous black stones to the left.
        int nr = r - dr[d];
        int nc = c - dc[d];
        while (ok(nr, nc) && brd[nr][nc] == 1) {
            left++;
            nr -= dr[d];
            nc -= dc[d];
        }
        // Count contiguous black stones to the right.
        nr = r + dr[d];
        nc = c + dc[d];
        while (ok(nr, nc) && brd[nr][nc] == 1) {
            right++;
            nr += dr[d];
            nc += dc[d];
        }
        int total = 1 + left + right;
        // Check for 5+ line (장목) – always forbidden.
        if (total >= 5) {
            forbidden = true;
            break;
        }
        // Check 4‑in‑a‑row with both ends open (44).
        if (total == 4) {
            bool left_open = ok(r - dr[d] * (left + 1), c - dc[d] * (left + 1)) && brd[r - dr[d] * (left + 1)][c - dc[d] * (left + 1)] == 0;
            bool right_open = ok(r + dr[d] * (right + 1), c + dc[d] * (right + 1)) && brd[r + dr[d] * (right + 1)][c + dc[d] * (right + 1)] == 0;
            if (left_open && right_open) {
                forbidden = true;
                break;
            }
        }
        // Check 3‑in‑a‑row with both ends open (33).
        if (total == 3) {
            bool left_open = ok(r - dr[d] * (left + 1), c - dc[d] * (left + 1)) && brd[r - dr[d] * (left + 1)][c - dc[d] * (left + 1)] == 0;
            bool right_open = ok(r + dr[d] * (right + 1), c + dc[d] * (right + 1)) && brd[r + dr[d] * (right + 1)][c + dc[d] * (right + 1)] == 0;
            if (left_open && right_open) {
                forbidden = true;
                break;
            }
        }
    }
    // Restore original state.
    brd[r][c] = 0;
    return forbidden;
}

pair<int, int> choose_ai_move() {
    pair<int, int> ap = vcf(2, 50);
    if (ap.first == -1) ap = vcf(1, 4);
    if (ap.first != -1) return ap;

    double bst = -1;
    ap = {sz / 2, sz / 2};
    for (int r = 0; r < sz; r++) {
        for (int c = 0; c < sz; c++) {
            if (brd[r][c] == 0) {
                double s = get_thr(r, c, 2) * 1.2 + get_thr(r, c, 1);
                int dst = abs(r - sz / 2) + abs(c - sz / 2);
                s += (sz - dst) * 0.1;
                if (s > bst) {
                    bst = s;
                    ap = {r, c};
                }
            }
        }
    }
    return ap;
}

bool has_five_for(int p) {
    int dr[] = {0, 1, 1, 1}, dc[] = {1, 0, 1, -1};
    for (int r = 0; r < sz; r++) {
        for (int c = 0; c < sz; c++) {
            if (brd[r][c] != p) continue;
            for (int i = 0; i < 4; i++) {
                if (cnt_lin(r, c, dr[i], dc[i], p) >= 5) return true;
            }
        }
    }
    return false;
}

int position_score_for(int p) {
    if (has_five_for(p)) return 1000000;
    int op = 3 - p;
    int score = 0;
    for (int r = 0; r < sz; r++) {
        for (int c = 0; c < sz; c++) {
            if (brd[r][c] == p) {
                score += 3;
            } else if (brd[r][c] == 0) {
                score += get_thr(r, c, p);
                score += get_thr(r, c, op) / 2;
                int dst = abs(r - sz / 2) + abs(c - sz / 2);
                score += max(0, sz - dst) / 4;
            }
        }
    }
    return score;
}

double estimate_black_winrate() {
    if (has_five_for(1)) return 100.0;
    if (has_five_for(2)) return 0.0;
    int black = position_score_for(1);
    int white = position_score_for(2);
    double diff = static_cast<double>(black - white);
    double wr = 50.0 + tanh(diff / 900.0) * 48.0;
    return min(99.0, max(1.0, wr));
}

string classify_move(int p, double before, double after, double best_after, bool had_win, bool made_win, int ply) {
    double own_delta = (p == 1) ? (after - before) : (before - after);
    double best_gap = (p == 1) ? (best_after - after) : (after - best_after);
    if (had_win && !made_win) return "Missed Win";
    if (made_win) return "Best";
    if (ply < 8 && best_gap < 8.0) return "Book";
    if (own_delta > 18.0 && best_gap < 4.0) return "Brilliant";
    if (own_delta > 10.0) return "Great";
    if (best_gap < 2.0) return "Best";
    if (best_gap < 5.0) return "Excellent";
    if (best_gap < 10.0) return "Good";
    if (best_gap < 18.0) return "Inaccuracy";
    if (best_gap < 32.0) return "Mistake";
    return "Blunder";
}

double best_possible_after_for(int p) {
    double best = (p == 1) ? -1.0 : 101.0;
    for (int r = 0; r < sz; r++) {
        for (int c = 0; c < sz; c++) {
            if (brd[r][c] != 0) continue;
            brd[r][c] = p;
            double wr = estimate_black_winrate();
            brd[r][c] = 0;
            if (p == 1) best = max(best, wr);
            else best = min(best, wr);
        }
    }
    return best < 0.0 || best > 100.0 ? estimate_black_winrate() : best;
}

void build_board_to_ply(int ply, int out[kMaxBoard][kMaxBoard]) {
    memset(out, 0, sizeof(int) * kMaxBoard * kMaxBoard);
    int limit = min<int>(ply, log_v.size());
    for (int i = 0; i < limit; i++) {
        out[log_v[i].first][log_v[i].second] = (i % 2 == 0) ? 1 : 2;
    }
}

void compute_reviews() {
    int saved[kMaxBoard][kMaxBoard];
    memcpy(saved, brd, sizeof(brd));
    reviews.clear();
    memset(brd, 0, sizeof(brd));
    for (size_t i = 0; i < log_v.size(); i++) {
        int p = (i % 2 == 0) ? 1 : 2;
        int r = log_v[i].first, c = log_v[i].second;
        double before = estimate_black_winrate();
        bool had_win = vcf(p, 1).first != -1;
        double best_after = best_possible_after_for(p);
        brd[r][c] = p;
        bool made_win = has_five_for(p);
        double after = estimate_black_winrate();
        reviews.push_back({r, c, p, after, classify_move(p, before, after, best_after, had_win, made_win, (int)i)});
    }
    memcpy(brd, saved, sizeof(brd));
    review_ply = (int)log_v.size();
}

void enter_review_after_game() {
    if (review_mode) return;
    compute_reviews();
    review_mode = true;
    review_ply = (int)log_v.size();
}

void new_game();
void save_replay(const char* filename);
bool load_replay(const char* filename);

void handle_review_key(char ch) {
    if (ch == 'a' || ch == 'A') review_ply = max(0, review_ply - 1);
    else if (ch == 'd' || ch == 'D') review_ply = min<int>(log_v.size(), review_ply + 1);
    else if (ch == 'e' || ch == 'E') review_ply = (int)log_v.size();
    else if (ch == 's' || ch == 'S') save_replay("gomoku_replay.txt");
    else if (ch == 'l' || ch == 'L') load_replay("gomoku_replay.txt");
    else if (ch == 'n' || ch == 'N') new_game();
}

int displayed_cell(int r, int c) {
    if (!review_mode) return brd[r][c];
    int board[kMaxBoard][kMaxBoard];
    build_board_to_ply(review_ply, board);
    return board[r][c];
}

void save_replay(const char* filename) {
    FILE* fp = fopen(filename, "w");
    if (!fp) return;
    fprintf(fp, "GOMOKU_REPLAY_V1 %d %d OFFLINE\n", sz, winner);
    for (auto mv : log_v) fprintf(fp, "%d %d\n", mv.first, mv.second);
    fclose(fp);
}

bool load_replay(const char* filename) {
    FILE* fp = fopen(filename, "r");
    if (!fp) return false;
    char magic[32] = {}, name[64] = {};
    int file_sz = 15, file_winner = 0;
    if (fscanf(fp, "%31s %d %d %63s", magic, &file_sz, &file_winner, name) != 4 ||
        strcmp(magic, "GOMOKU_REPLAY_V1") != 0) {
        fclose(fp);
        return false;
    }
    vector<pair<int, int>> loaded;
    int r = 0, c = 0;
    while (fscanf(fp, "%d %d", &r, &c) == 2) {
        if (r >= 0 && r < file_sz && c >= 0 && c < file_sz) loaded.push_back({r, c});
    }
    fclose(fp);
    sz = max(kMinBoard, min(kMaxBoard, file_sz));
    memset(brd, 0, sizeof(brd));
    log_v = loaded;
    for (size_t i = 0; i < log_v.size(); i++) brd[log_v[i].first][log_v[i].second] = (i % 2 == 0) ? 1 : 2;
    winner = file_winner;
    game_over = true;
    win_line_start = {-1, -1};
    win_line_end = {-1, -1};
    if (winner != 0 && !log_v.empty()) {
        auto last = log_v.back();
        check_win_at(last.first, last.second, (log_v.size() % 2 == 1) ? 1 : 2);
    }
    compute_reviews();
    review_mode = true;
    last_move = log_v.empty() ? pair<int, int>{-1, -1} : log_v.back();
    return true;
}

void layout() {
    int panel = max(260, ui.w / 4);
    int board_space_w = ui.w - panel - 64;
    int board_space_h = ui.h - 64;
    int board_px = min(board_space_w, board_space_h);
    ui.cell = max(24, board_px / sz);
    int full_px = ui.cell * (sz - 1) + ui.margin * 2;
    ui.board = {32, max(24, (ui.h - full_px) / 2), full_px, full_px};
    int px = ui.board.x + ui.board.w + 36;
    // Position the first button below the board but make sure the entire button
    // column stays above the lower‑text area ("Touch or click…" and
    // "N/U/D/S/Q shortcuts supported.").  The bottom text is drawn at
    // `ui.h - 48`, so the last button (index 5) must finish before that.
    // Each button is 50px high and spaced 60px apart, so the bottom of the
    // last button is at `py + 5 * 60 + 50`.  We enforce `py` such that
    // `py + 350 <= ui.h - 48`.  This is equivalent to `py <= ui.h - 408`.
    // Use a max to keep the buttons below the board and at least 32px from
    // the top of the window.
    // Compute the start Y position for the first button.
    int py = max(32, ui.board.y + 200);
    // Ensure the entire button column fits above the bottom‑text area.
    // The last button (index 5) ends at py + 350.  The bottom‑text is drawn
    // at ui.h - 76 (first line) and ui.h - 48 (second line).  We keep a
    // margin by requiring the button column to finish before ui.h - 76.
    if (py + 350 > ui.h - 48) {
        py = ui.h - 48 - 350;
        if (py < 32) py = 32;  // keep a small margin from top
    }
    int bw = max(220, ui.w - px - 32);
    for (int i = 0; i < 6; i++) ui.buttons[i] = {px, py + i * 60, bw, 50};
}

bool hit(Rect r, int x, int y) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

void new_game() {
    memset(brd, 0, sizeof(brd));
    log_v.clear();
    reviews.clear();
    last_move = {-1, -1};
    win_line_start = {-1, -1};
    win_line_end = {-1, -1};
    game_over = false;
    winner = 0;
    review_mode = false;
    review_ply = 0;
}

void undo_turn() {
    if (log_v.empty() || game_over) return;
    int count = min<int>(2, log_v.size());
    while (count--) {
        auto [r, c] = log_v.back();
        log_v.pop_back();
        brd[r][c] = 0;
    }
    last_move = log_v.empty() ? pair<int, int>{-1, -1} : log_v.back();
}

void save_log(const char* filename) {
    FILE* fp = fopen(filename, "w");
    if (!fp) return;
    int replay[kMaxBoard][kMaxBoard] = {};
    fprintf(fp, "--- Gomoku review ---\n");
    for (size_t i = 0; i < log_v.size(); i++) {
        int r = log_v[i].first, c = log_v[i].second;
        replay[r][c] = (i % 2 == 0) ? 1 : 2;
        fprintf(fp, "\n[%zu] %s (%d, %d)\n", i + 1, (i % 2 == 0) ? "Human" : "AI", r, c);
        for (int row = 0; row < sz; row++) {
            for (int col = 0; col < sz; col++) {
                fprintf(fp, "%c ", replay[row][col] == 1 ? 'X' : (replay[row][col] == 2 ? 'O' : '.'));
            }
            fprintf(fp, "\n");
        }
    }
    fclose(fp);
}

bool board_from_point(int x, int y, int& r, int& c) {
    int gx0 = ui.board.x + ui.margin;
    int gy0 = ui.board.y + ui.margin;
    int gx1 = gx0 + ui.cell * (sz - 1);
    int gy1 = gy0 + ui.cell * (sz - 1);
    int touch = max(18, ui.cell / 2);
    if (x < gx0 - touch || x > gx1 + touch || y < gy0 - touch || y > gy1 + touch) return false;
    c = (x - gx0 + ui.cell / 2) / ui.cell;
    r = (y - gy0 + ui.cell / 2) / ui.cell;
    return ok(r, c);
}

void play_human_move(int r, int c) {
      // Reject forbidden moves (33, 44, 장목) for black.
      if (review_mode || game_over || !ok(r, c) || brd[r][c] != 0) return;
      if (is_forbidden_move(r, c)) {
          // Move is illegal under the forbidden rule; ignore it.
          return;
      }
      brd[r][c] = 1;
      log_v.push_back({r, c});
      last_move = {r, c};
      if (check_win_at(r, c, 1)) {
          game_over = true;
          winner = 1;
          enter_review_after_game();
          return;
      }

    pair<int, int> ap = choose_ai_move();
    if (ap.first != -1 && brd[ap.first][ap.second] == 0) {
        brd[ap.first][ap.second] = 2;
        log_v.push_back(ap);
        last_move = ap;
        if (check_win_at(ap.first, ap.second, 2)) {
            game_over = true;
            winner = 2;
            enter_review_after_game();
        }
    }
}

void cycle_size() {
    if (!log_v.empty()) return;
    sz = (sz == 15) ? 19 : ((sz == 19) ? 13 : 15);
    new_game();
}

void handle_click(int x, int y) {
    int r = 0, c = 0;
    if (review_mode) {
        if (hit(ui.buttons[0], x, y)) review_ply = max(0, review_ply - 1);
        else if (hit(ui.buttons[1], x, y)) review_ply = min<int>(log_v.size(), review_ply + 1);
        else if (hit(ui.buttons[2], x, y)) review_ply = (int)log_v.size();
        else if (hit(ui.buttons[3], x, y)) save_replay("gomoku_replay.txt");
        else if (hit(ui.buttons[4], x, y)) load_replay("gomoku_replay.txt");
        else if (hit(ui.buttons[5], x, y)) new_game();
    } else if (board_from_point(x, y, r, c)) {
        play_human_move(r, c);
    } else if (hit(ui.buttons[0], x, y)) {
        new_game();
    } else if (hit(ui.buttons[1], x, y)) {
        undo_turn();
    } else if (hit(ui.buttons[2], x, y)) {
        dbg = !dbg;
    } else if (hit(ui.buttons[3], x, y)) {
        save_log("gomoku_log.txt");
    } else if (hit(ui.buttons[4], x, y)) {
        cycle_size();
    }
}

string status_text() {
    if (review_mode) return "GAME REVIEW";
    if (game_over) return winner == 1 ? "YOU WIN!" : "AI WINS!";
    return "YOUR TURN";
}

}  // namespace

#ifdef _WIN32

HWND g_hwnd = nullptr;

COLORREF rgb(int r, int g, int b) { return RGB(r, g, b); }

struct MonitorChoice {
    int wanted = 0;
    int seen = 0;
    RECT rect{0, 0, kMinW, kMinH};
    bool found = false;
};

BOOL CALLBACK enum_monitor_proc(HMONITOR monitor, HDC, LPRECT, LPARAM data) {
    auto* choice = reinterpret_cast<MonitorChoice*>(data);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfo(monitor, &info)) return TRUE;
    if (choice->seen == choice->wanted) {
        choice->rect = info.rcMonitor;
        choice->found = true;
        return FALSE;
    }
    choice->seen++;
    return TRUE;
}

RECT selected_monitor_rect() {
    MonitorChoice choice{};
    choice.wanted = monitor_index;
    EnumDisplayMonitors(nullptr, nullptr, enum_monitor_proc, reinterpret_cast<LPARAM>(&choice));
    if (choice.found) return choice.rect;
    choice.wanted = 0;
    choice.seen = 0;
    EnumDisplayMonitors(nullptr, nullptr, enum_monitor_proc, reinterpret_cast<LPARAM>(&choice));
    return choice.rect;
}

void fill(HDC hdc, Rect r, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    RECT wr{r.x, r.y, r.x + r.w, r.y + r.h};
    FillRect(hdc, &wr, brush);
    DeleteObject(brush);
}

void text(HDC hdc, const string& s, int x, int y, int size, COLORREF color, bool bold = false) {
    HFONT font = CreateFontA(size, 0, 0, 0, bold ? FW_BOLD : FW_SEMIBOLD, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");
    HFONT old = (HFONT)SelectObject(hdc, font);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, color);
    TextOutA(hdc, x, y, s.c_str(), (int)s.size());
    SelectObject(hdc, old);
    DeleteObject(font);
}

void ellipse(HDC hdc, int cx, int cy, int rad, COLORREF fill_color, COLORREF outline_color) {
    HBRUSH brush = CreateSolidBrush(fill_color);
    HPEN pen = CreatePen(PS_SOLID, 2, outline_color);
    HBRUSH oldb = (HBRUSH)SelectObject(hdc, brush);
    HPEN oldp = (HPEN)SelectObject(hdc, pen);
    Ellipse(hdc, cx - rad, cy - rad, cx + rad, cy + rad);
    SelectObject(hdc, oldb);
    SelectObject(hdc, oldp);
    DeleteObject(brush);
    DeleteObject(pen);
}

void round_rect(HDC hdc, Rect r, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HBRUSH oldb = (HBRUSH)SelectObject(hdc, brush);
    HPEN oldp = (HPEN)SelectObject(hdc, pen);
    RoundRect(hdc, r.x, r.y, r.x + r.w, r.y + r.h, 14, 14);
    SelectObject(hdc, oldb);
    SelectObject(hdc, oldp);
    DeleteObject(brush);
    DeleteObject(pen);
}

COLORREF review_color(const string& label) {
    if (label == "Brilliant") return rgb(31, 196, 228);
    if (label == "Great") return rgb(20, 118, 58);
    if (label == "Best" || label == "Excellent") return rgb(129, 206, 76);
    if (label == "Good") return rgb(238, 238, 228);
    if (label == "Book") return rgb(151, 93, 45);
    if (label == "Inaccuracy") return rgb(235, 204, 69);
    if (label == "Mistake") return rgb(232, 142, 42);
    if (label == "Blunder") return rgb(224, 38, 38);
    return rgb(207, 75, 166);
}

void draw_star(HDC hdc, int cx, int cy, int r, COLORREF color) {
    POINT pts[10];
    for (int i = 0; i < 10; i++) {
        double a = -3.14159265358979323846 / 2.0 + i * 3.14159265358979323846 / 5.0;
        double rr = (i % 2 == 0) ? r : r * 0.45;
        pts[i] = {cx + (LONG)(cos(a) * rr), cy + (LONG)(sin(a) * rr)};
    }
    HBRUSH brush = CreateSolidBrush(color);
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HBRUSH oldb = (HBRUSH)SelectObject(hdc, brush);
    HPEN oldp = (HPEN)SelectObject(hdc, pen);
    Polygon(hdc, pts, 10);
    SelectObject(hdc, oldb);
    SelectObject(hdc, oldp);
    DeleteObject(brush);
    DeleteObject(pen);
}

void draw_review_badge(HDC hdc, const string& label, int x, int y) {
    COLORREF c = review_color(label);
    Rect box{x, y, 42, 34};
    round_rect(hdc, box, c);
    COLORREF ink = (label == "Good" || label == "Inaccuracy") ? rgb(38, 49, 38) : rgb(255, 255, 255);
    if (label == "Best") {
        draw_star(hdc, x + 21, y + 17, 13, rgb(255, 255, 255));
    } else if (label == "Excellent") {
        HPEN pen = CreatePen(PS_SOLID, 4, rgb(255, 255, 255));
        HPEN old = (HPEN)SelectObject(hdc, pen);
        MoveToEx(hdc, x + 11, y + 18, nullptr);
        LineTo(hdc, x + 18, y + 25);
        LineTo(hdc, x + 31, y + 9);
        SelectObject(hdc, old);
        DeleteObject(pen);
    } else if (label == "Good") {
        HPEN pen = CreatePen(PS_SOLID, 3, rgb(38, 49, 38));
        HPEN old = (HPEN)SelectObject(hdc, pen);
        HBRUSH oldb = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Ellipse(hdc, x + 11, y + 8, x + 31, y + 28);
        SelectObject(hdc, oldb);
        SelectObject(hdc, old);
        DeleteObject(pen);
    } else if (label == "Book") {
        fill(hdc, {x + 10, y + 8, 9, 18}, rgb(255, 243, 210));
        fill(hdc, {x + 22, y + 8, 9, 18}, rgb(255, 243, 210));
        HPEN pen = CreatePen(PS_SOLID, 2, rgb(255, 255, 255));
        HPEN old = (HPEN)SelectObject(hdc, pen);
        MoveToEx(hdc, x + 20, y + 8, nullptr);
        LineTo(hdc, x + 20, y + 27);
        SelectObject(hdc, old);
        DeleteObject(pen);
    } else {
        string symbol = "X";
        if (label == "Brilliant") symbol = "!!";
        else if (label == "Great") symbol = "!";
        else if (label == "Inaccuracy") symbol = "?!";
        else if (label == "Mistake") symbol = "?";
        else if (label == "Blunder") symbol = "??";
        text(hdc, symbol, x + 9, y + 6, 21, ink, true);
    }
}

void draw_scene(HDC hdc) {
    layout();
    fill(hdc, {0, 0, ui.w, ui.h}, rgb(42, 82, 50));
    round_rect(hdc, ui.board, rgb(235, 187, 114));

    HPEN grid = CreatePen(PS_SOLID, 1, rgb(86, 62, 40));
    HPEN oldp = (HPEN)SelectObject(hdc, grid);
    int gx0 = ui.board.x + ui.margin, gy0 = ui.board.y + ui.margin;
    int gx1 = gx0 + ui.cell * (sz - 1), gy1 = gy0 + ui.cell * (sz - 1);
    for (int i = 0; i < sz; i++) {
        MoveToEx(hdc, gx0, gy0 + i * ui.cell, nullptr);
        LineTo(hdc, gx1, gy0 + i * ui.cell);
        MoveToEx(hdc, gx0 + i * ui.cell, gy0, nullptr);
        LineTo(hdc, gx0 + i * ui.cell, gy1);
    }
    SelectObject(hdc, oldp);
    DeleteObject(grid);

    if (dbg) {
        for (int r = 0; r < sz; r++) for (int c = 0; c < sz; c++) if (brd[r][c] == 0) {
            int s = min(255, get_thr(r, c, 2) / 2 + get_thr(r, c, 1));
            ellipse(hdc, gx0 + c * ui.cell, gy0 + r * ui.cell, ui.cell / 4, rgb(230 - s / 2, 170 + s / 3, 35), rgb(230 - s / 2, 170 + s / 3, 35));
        }
    }

    if (sz >= 13) {
        int pts[] = {3, sz / 2, sz - 4};
        for (int rr : pts) for (int cc : pts) ellipse(hdc, gx0 + cc * ui.cell, gy0 + rr * ui.cell, 4, rgb(77, 59, 37), rgb(77, 59, 37));
    }

    for (int r = 0; r < sz; r++) for (int c = 0; c < sz; c++) if (displayed_cell(r, c)) {
        int cx = gx0 + c * ui.cell, cy = gy0 + r * ui.cell;
        int rad = max(10, ui.cell / 2 - 3);
        if (displayed_cell(r, c) == 1) ellipse(hdc, cx, cy, rad, rgb(20, 21, 20), rgb(9, 10, 9));
        else ellipse(hdc, cx, cy, rad, rgb(242, 238, 225), rgb(160, 154, 140));
    }
    pair<int, int> shown_last = last_move;
    if (review_mode && review_ply > 0 && review_ply <= (int)log_v.size()) shown_last = log_v[review_ply - 1];
    if (shown_last.first != -1) {
        HPEN pen = CreatePen(PS_SOLID, 3, rgb(129, 182, 76));
        HPEN old = (HPEN)SelectObject(hdc, pen);
        HBRUSH oldb = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
        int cx = gx0 + shown_last.second * ui.cell, cy = gy0 + shown_last.first * ui.cell;
        int rad = max(12, ui.cell / 2);
        Ellipse(hdc, cx - rad, cy - rad, cx + rad, cy + rad);
        SelectObject(hdc, oldb);
        SelectObject(hdc, old);
        DeleteObject(pen);
    }
    if (win_line_start.first != -1 && (!review_mode || review_ply == (int)log_v.size())) {
        HPEN pen = CreatePen(PS_SOLID, max(5, ui.cell / 6), rgb(224, 38, 38));
        HPEN old = (HPEN)SelectObject(hdc, pen);
        int x1 = gx0 + win_line_start.second * ui.cell;
        int y1 = gy0 + win_line_start.first * ui.cell;
        int x2 = gx0 + win_line_end.second * ui.cell;
        int y2 = gy0 + win_line_end.first * ui.cell;
        MoveToEx(hdc, x1, y1, nullptr);
        LineTo(hdc, x2, y2);
        SelectObject(hdc, old);
        DeleteObject(pen);
    }

    Rect panel{ui.board.x + ui.board.w + 28, 0, ui.w - ui.board.x - ui.board.w - 28, ui.h};
    fill(hdc, panel, rgb(37, 72, 45));
    text(hdc, "GOMOKU", panel.x + 24, 38, 40, rgb(245, 244, 232), true);
    text(hdc, "Chess.com style board UI", panel.x + 28, 86, 18, rgb(214, 224, 200));
    text(hdc, "YOU: BLACK    AI: WHITE", panel.x + 28, 122, 20, rgb(245, 244, 232), true);
    text(hdc, status_text(), panel.x + 28, 162, 30, rgb(129, 182, 76), true);
    text(hdc, "MOVES: " + to_string(log_v.size()), panel.x + 28, 202, 20, rgb(245, 244, 232), true);

    const string labels[5] = {
        review_mode ? "PREV" : "NEW",
        review_mode ? "NEXT" : "UNDO",
        review_mode ? "END" : (dbg ? "HEAT ON" : "HEAT OFF"),
        review_mode ? "SAVE" : "SAVE",
        review_mode ? "LOAD" : ("SIZE " + to_string(sz))
    };
    const string review_new_label = "NEW";
    int button_count = review_mode ? 6 : 5;
    for (int i = 0; i < button_count; i++) {
        round_rect(hdc, ui.buttons[i], i == 2 && dbg ? rgb(129, 182, 76) : rgb(238, 238, 228));
        text(hdc, (i == 5) ? review_new_label : labels[i], ui.buttons[i].x + 22, ui.buttons[i].y + 12, 22, rgb(38, 49, 38), true);
    }
    text(hdc, "Touch or click an intersection.", panel.x + 28, ui.h - 76, 18, rgb(214, 224, 200));
    text(hdc, "N/U/D/S/Q shortcuts supported.", panel.x + 28, ui.h - 48, 18, rgb(214, 224, 200));
    if (review_mode) {
        int y = ui.buttons[5].y + 64;
        text(hdc, "PLY: " + to_string(review_ply) + "/" + to_string(log_v.size()), panel.x + 28, y, 18, rgb(245, 244, 232), true);
        if (review_ply > 0 && review_ply <= (int)reviews.size()) {
            const MoveReview& mv = reviews[review_ply - 1];
            draw_review_badge(hdc, mv.label, panel.x + 28, y + 30);
            text(hdc, display_label(mv.label), panel.x + 78, y + 36, 22, review_color(mv.label), true);
            text(hdc, "WINRATE: " + to_string((int)(mv.winrate + 0.5)) + "%", panel.x + 28, y + 66, 18, rgb(245, 244, 232), true);
        }
        Rect graph{panel.x + 28, y + 100, max(120, panel.w - 60), 112};
        fill(hdc, graph, rgb(31, 54, 36));
        HPEN axis = CreatePen(PS_SOLID, 1, rgb(214, 224, 200));
        HPEN old = (HPEN)SelectObject(hdc, axis);
        MoveToEx(hdc, graph.x, graph.y + graph.h / 2, nullptr);
        LineTo(hdc, graph.x + graph.w, graph.y + graph.h / 2);
        SelectObject(hdc, old);
        DeleteObject(axis);
        if (reviews.size() > 1) {
            HPEN pen = CreatePen(PS_SOLID, 3, rgb(129, 182, 76));
            old = (HPEN)SelectObject(hdc, pen);
            for (size_t i = 1; i < reviews.size(); i++) {
                int x1 = graph.x + (int)((i - 1) * graph.w / (reviews.size() - 1));
                int x2 = graph.x + (int)(i * graph.w / (reviews.size() - 1));
                int y1 = graph.y + graph.h - (int)(reviews[i - 1].winrate * graph.h / 100.0);
                int y2 = graph.y + graph.h - (int)(reviews[i].winrate * graph.h / 100.0);
                MoveToEx(hdc, x1, y1, nullptr);
                LineTo(hdc, x2, y2);
            }
            SelectObject(hdc, old);
            DeleteObject(pen);
        }
        if (!reviews.empty() && review_ply > 0) {
            int idx = min<int>(review_ply - 1, reviews.size() - 1);
            int cx = graph.x + (reviews.size() == 1 ? graph.w : (int)(idx * graph.w / (reviews.size() - 1)));
            HPEN pen = CreatePen(PS_SOLID, 2, rgb(255, 255, 255));
            HPEN old = (HPEN)SelectObject(hdc, pen);
            MoveToEx(hdc, cx, graph.y - 4, nullptr);
            LineTo(hdc, cx, graph.y + graph.h + 4);
            SelectObject(hdc, old);
            DeleteObject(pen);
            draw_review_badge(hdc, reviews[idx].label, cx - 21, graph.y - 42);
        }
        text(hdc, "SAVE/LOAD REPLAY ONLY", panel.x + 28, graph.y + graph.h + 24, 18, rgb(214, 224, 200), true);
    }
}

void repaint() { InvalidateRect(g_hwnd, nullptr, FALSE); }

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_SIZE:
            set_surface_size(LOWORD(lp), HIWORD(lp));
            repaint();
            return 0;
        case WM_LBUTTONUP:
            handle_click(to_logical_x(GET_X_LPARAM(lp)), to_logical_y(GET_Y_LPARAM(lp)));
            repaint();
            return 0;
        case WM_KEYUP:
            if (wp == 'Q' || wp == VK_ESCAPE) PostQuitMessage(0);
            else if (review_mode) handle_review_key(static_cast<char>(wp));
            else if (wp == 'N') new_game();
            else if (wp == 'U') undo_turn();
            else if (wp == 'D') dbg = !dbg;
            else if (wp == 'S') save_log("gomoku_log.txt");
            else if (wp == 'L') load_replay("gomoku_replay.txt");
            repaint();
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);
            int pw = max(1L, rc.right - rc.left);
            int ph = max(1L, rc.bottom - rc.top);
            set_surface_size(pw, ph);
            HDC mem = CreateCompatibleDC(hdc);
            HBITMAP bmp = CreateCompatibleBitmap(hdc, ui.w, ui.h);
            HBITMAP old = (HBITMAP)SelectObject(mem, bmp);
            draw_scene(mem);
            SetStretchBltMode(hdc, HALFTONE);
            StretchBlt(hdc, 0, 0, pw, ph, mem, 0, 0, ui.w, ui.h, SRCCOPY);
            SelectObject(mem, old);
            DeleteObject(bmp);
            DeleteDC(mem);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

int main(int argc, char** argv) {
    parse_args(argc, argv);
    new_game();
    HINSTANCE inst = GetModuleHandle(nullptr);
    WNDCLASSA wc{};
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = inst;
    wc.lpszClassName = "GomokuGuiWindow";
    wc.hCursor = LoadCursor(nullptr, IDC_HAND);
    RegisterClassA(&wc);
    int x = CW_USEDEFAULT, y = CW_USEDEFAULT, w = kMinW, h = kMinH;
    DWORD style = WS_OVERLAPPEDWINDOW;
    if (fullscreen) {
        RECT mon = selected_monitor_rect();
        x = mon.left;
        y = mon.top;
        w = mon.right - mon.left;
        h = mon.bottom - mon.top;
        style = WS_POPUP;
    }
    set_surface_size(w, h);
    g_hwnd = CreateWindowA(wc.lpszClassName, "Gomoku GUI", style,
                           x, y, w, h,
                           nullptr, nullptr, inst, nullptr);
    ShowWindow(g_hwnd, fullscreen ? SW_SHOWMAXIMIZED : SW_SHOW);
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}

#else

Display* dpy = nullptr;
Window win = 0;
GC gc = 0;
int screen_id = 0;
int xi_opcode = -1;

int sx(int x) { return target_aspect > 0.0 ? static_cast<int>(x * (double)physical_w / ui.w + 0.5) : x; }
int sy(int y) { return target_aspect > 0.0 ? static_cast<int>(y * (double)physical_h / ui.h + 0.5) : y; }
int sw(int w) { return max(1, sx(w) - sx(0)); }
int sh(int h) { return max(1, sy(h) - sy(0)); }

unsigned long color(int r, int g, int b) {
    Colormap cmap = DefaultColormap(dpy, screen_id);
    XColor xc{};
    xc.red = r * 257;
    xc.green = g * 257;
    xc.blue = b * 257;
    XAllocColor(dpy, cmap, &xc);
    return xc.pixel;
}

void fill(Rect r, unsigned long c) {
    XSetForeground(dpy, gc, c);
    XFillRectangle(dpy, win, gc, sx(r.x), sy(r.y), sw(r.w), sh(r.h));
}

void text(const string& s, int x, int y, int size, unsigned long c) {
    (void)size;
    XSetForeground(dpy, gc, c);
    XDrawString(dpy, win, gc, sx(x), sy(y + 14), s.c_str(), (int)s.size());
}

void ellipse(int cx, int cy, int rad, unsigned long fill_c, unsigned long outline_c) {
    XSetForeground(dpy, gc, fill_c);
    XFillArc(dpy, win, gc, sx(cx - rad), sy(cy - rad), sw(rad * 2), sh(rad * 2), 0, 360 * 64);
    XSetForeground(dpy, gc, outline_c);
    XDrawArc(dpy, win, gc, sx(cx - rad), sy(cy - rad), sw(rad * 2), sh(rad * 2), 0, 360 * 64);
}

void line(int x1, int y1, int x2, int y2, unsigned long c) {
    XSetForeground(dpy, gc, c);
    XDrawLine(dpy, win, gc, sx(x1), sy(y1), sx(x2), sy(y2));
}

unsigned long review_color(const string& label) {
    if (label == "Brilliant") return color(31, 196, 228);
    if (label == "Great") return color(20, 118, 58);
    if (label == "Best" || label == "Excellent") return color(129, 206, 76);
    if (label == "Good") return color(238, 238, 228);
    if (label == "Book") return color(151, 93, 45);
    if (label == "Inaccuracy") return color(235, 204, 69);
    if (label == "Mistake") return color(232, 142, 42);
    if (label == "Blunder") return color(224, 38, 38);
    return color(207, 75, 166);
}

void draw_star(int cx, int cy, int r, unsigned long c) {
    XPoint pts[10];
    for (int i = 0; i < 10; i++) {
        double a = -3.14159265358979323846 / 2.0 + i * 3.14159265358979323846 / 5.0;
        double rr = (i % 2 == 0) ? r : r * 0.45;
        pts[i].x = (short)sx(cx + (int)(cos(a) * rr));
        pts[i].y = (short)sy(cy + (int)(sin(a) * rr));
    }
    XSetForeground(dpy, gc, c);
    XFillPolygon(dpy, win, gc, pts, 10, Complex, CoordModeOrigin);
}

void draw_review_badge(const string& label, int x, int y) {
    unsigned long c = review_color(label);
    fill({x, y, 42, 34}, c);
    unsigned long ink = (label == "Good" || label == "Inaccuracy") ? color(38, 49, 38) : color(255, 255, 255);
    if (label == "Best") {
        draw_star(x + 21, y + 17, 13, color(255, 255, 255));
    } else if (label == "Excellent") {
        XSetLineAttributes(dpy, gc, 4, LineSolid, CapRound, JoinRound);
        line(x + 11, y + 18, x + 18, y + 25, color(255, 255, 255));
        line(x + 18, y + 25, x + 31, y + 9, color(255, 255, 255));
        XSetLineAttributes(dpy, gc, 1, LineSolid, CapButt, JoinMiter);
    } else if (label == "Good") {
        XSetLineAttributes(dpy, gc, 3, LineSolid, CapRound, JoinRound);
        XSetForeground(dpy, gc, color(38, 49, 38));
        XDrawArc(dpy, win, gc, sx(x + 11), sy(y + 8), sw(20), sh(20), 0, 360 * 64);
        XSetLineAttributes(dpy, gc, 1, LineSolid, CapButt, JoinMiter);
    } else if (label == "Book") {
        fill({x + 10, y + 8, 9, 18}, color(255, 243, 210));
        fill({x + 22, y + 8, 9, 18}, color(255, 243, 210));
        XSetLineAttributes(dpy, gc, 2, LineSolid, CapButt, JoinMiter);
        line(x + 20, y + 8, x + 20, y + 27, color(255, 255, 255));
        XSetLineAttributes(dpy, gc, 1, LineSolid, CapButt, JoinMiter);
    } else {
        string symbol = "X";
        if (label == "Brilliant") symbol = "!!";
        else if (label == "Great") symbol = "!";
        else if (label == "Inaccuracy") symbol = "?!";
        else if (label == "Mistake") symbol = "?";
        else if (label == "Blunder") symbol = "??";
        text(symbol, x + 10, y + 7, 21, ink);
    }
}

void draw_scene() {
    layout();
    auto bg = color(42, 82, 50), panel_c = color(37, 72, 45), board_c = color(235, 187, 114);
    auto ink = color(86, 62, 40), cream = color(245, 244, 232), green = color(129, 182, 76);
    fill({0, 0, ui.w, ui.h}, bg);
    fill(ui.board, board_c);

    int gx0 = ui.board.x + ui.margin, gy0 = ui.board.y + ui.margin;
    int gx1 = gx0 + ui.cell * (sz - 1), gy1 = gy0 + ui.cell * (sz - 1);
    XSetForeground(dpy, gc, ink);
    for (int i = 0; i < sz; i++) {
        XDrawLine(dpy, win, gc, sx(gx0), sy(gy0 + i * ui.cell), sx(gx1), sy(gy0 + i * ui.cell));
        XDrawLine(dpy, win, gc, sx(gx0 + i * ui.cell), sy(gy0), sx(gx0 + i * ui.cell), sy(gy1));
    }
    if (dbg) {
        for (int r = 0; r < sz; r++) for (int c = 0; c < sz; c++) if (brd[r][c] == 0) {
            int s = min(255, get_thr(r, c, 2) / 2 + get_thr(r, c, 1));
            ellipse(gx0 + c * ui.cell, gy0 + r * ui.cell, ui.cell / 4, color(230 - s / 2, 170 + s / 3, 35), color(230 - s / 2, 170 + s / 3, 35));
        }
    }
    if (sz >= 13) {
        int pts[] = {3, sz / 2, sz - 4};
        for (int rr : pts) for (int cc : pts) ellipse(gx0 + cc * ui.cell, gy0 + rr * ui.cell, 4, ink, ink);
    }
    for (int r = 0; r < sz; r++) for (int c = 0; c < sz; c++) if (displayed_cell(r, c)) {
        int cx = gx0 + c * ui.cell, cy = gy0 + r * ui.cell;
        int rad = max(10, ui.cell / 2 - 3);
        if (displayed_cell(r, c) == 1) ellipse(cx, cy, rad, color(20, 21, 20), color(8, 8, 8));
        else ellipse(cx, cy, rad, color(242, 238, 225), color(160, 154, 140));
    }
    pair<int, int> shown_last = last_move;
    if (review_mode && review_ply > 0 && review_ply <= (int)log_v.size()) shown_last = log_v[review_ply - 1];
    if (shown_last.first != -1) {
        XSetForeground(dpy, gc, green);
        int cx = gx0 + shown_last.second * ui.cell, cy = gy0 + shown_last.first * ui.cell;
        int rad = max(12, ui.cell / 2);
        XDrawArc(dpy, win, gc, sx(cx - rad), sy(cy - rad), sw(rad * 2), sh(rad * 2), 0, 360 * 64);
    }
    if (win_line_start.first != -1 && (!review_mode || review_ply == (int)log_v.size())) {
        XSetForeground(dpy, gc, color(224, 38, 38));
        XSetLineAttributes(dpy, gc, max(5, ui.cell / 6), LineSolid, CapRound, JoinRound);
        int x1 = gx0 + win_line_start.second * ui.cell;
        int y1 = gy0 + win_line_start.first * ui.cell;
        int x2 = gx0 + win_line_end.second * ui.cell;
        int y2 = gy0 + win_line_end.first * ui.cell;
        XDrawLine(dpy, win, gc, sx(x1), sy(y1), sx(x2), sy(y2));
        XSetLineAttributes(dpy, gc, 1, LineSolid, CapButt, JoinMiter);
    }

    Rect panel{ui.board.x + ui.board.w + 28, 0, ui.w - ui.board.x - ui.board.w - 28, ui.h};
    fill(panel, panel_c);
    text("GOMOKU", panel.x + 24, 38, 40, cream);
    text("YOU: BLACK    AI: WHITE", panel.x + 28, 122, 20, cream);
    text(status_text(), panel.x + 28, 162, 30, green);
    text("MOVES: " + to_string(log_v.size()), panel.x + 28, 202, 20, cream);
    const string labels[5] = {
        review_mode ? "PREV" : "NEW",
        review_mode ? "NEXT" : "UNDO",
        review_mode ? "END" : (dbg ? "HEAT ON" : "HEAT OFF"),
        review_mode ? "SAVE" : "SAVE",
        review_mode ? "LOAD" : ("SIZE " + to_string(sz))
    };
    const string review_new_label = "NEW";
    int button_count = review_mode ? 6 : 5;
    for (int i = 0; i < button_count; i++) {
        fill(ui.buttons[i], i == 2 && dbg ? green : color(238, 238, 228));
        text((i == 5) ? review_new_label : labels[i], ui.buttons[i].x + 22, ui.buttons[i].y + 15, 22, color(38, 49, 38));
    }
    text("Touch or click an intersection.", panel.x + 28, ui.h - 76, 18, color(214, 224, 200));
    if (review_mode) {
        int y = ui.buttons[5].y + 64;
        text("PLY: " + to_string(review_ply) + "/" + to_string(log_v.size()), panel.x + 28, y, 18, cream);
        if (review_ply > 0 && review_ply <= (int)reviews.size()) {
            const MoveReview& mv = reviews[review_ply - 1];
            draw_review_badge(mv.label, panel.x + 28, y + 28);
            text(display_label(mv.label), panel.x + 78, y + 34, 22, review_color(mv.label));
            text("WINRATE: " + to_string((int)(mv.winrate + 0.5)) + "%", panel.x + 28, y + 64, 18, cream);
        }
        Rect graph{panel.x + 28, y + 98, max(120, panel.w - 60), 104};
        fill(graph, color(31, 54, 36));
        line(graph.x, graph.y + graph.h / 2, graph.x + graph.w, graph.y + graph.h / 2, color(214, 224, 200));
        if (reviews.size() > 1) {
            XSetLineAttributes(dpy, gc, 3, LineSolid, CapRound, JoinRound);
            for (size_t i = 1; i < reviews.size(); i++) {
                int x1 = graph.x + (int)((i - 1) * graph.w / (reviews.size() - 1));
                int x2 = graph.x + (int)(i * graph.w / (reviews.size() - 1));
                int y1 = graph.y + graph.h - (int)(reviews[i - 1].winrate * graph.h / 100.0);
                int y2 = graph.y + graph.h - (int)(reviews[i].winrate * graph.h / 100.0);
                line(x1, y1, x2, y2, green);
            }
            XSetLineAttributes(dpy, gc, 1, LineSolid, CapButt, JoinMiter);
        }
        if (!reviews.empty() && review_ply > 0) {
            int idx = min<int>(review_ply - 1, reviews.size() - 1);
            int cx = graph.x + (reviews.size() == 1 ? graph.w : (int)(idx * graph.w / (reviews.size() - 1)));
            XSetLineAttributes(dpy, gc, 2, LineSolid, CapRound, JoinRound);
            line(cx, graph.y - 4, cx, graph.y + graph.h + 4, color(255, 255, 255));
            XSetLineAttributes(dpy, gc, 1, LineSolid, CapButt, JoinMiter);
            draw_review_badge(reviews[idx].label, cx - 21, graph.y - 42);
        }
        text("SAVE/LOAD REPLAY ONLY", panel.x + 28, graph.y + graph.h + 22, 18, color(214, 224, 200));
    }
    XFlush(dpy);
}

void enable_touch_events() {
    int event = 0, error = 0;
    if (!XQueryExtension(dpy, "XInputExtension", &xi_opcode, &event, &error)) return;
    int major = 2, minor = 2;
    if (XIQueryVersion(dpy, &major, &minor) != Success) return;

    unsigned char mask[(XI_LASTEVENT + 7) / 8] = {};
    XISetMask(mask, XI_TouchBegin);
    XISetMask(mask, XI_TouchUpdate);
    XISetMask(mask, XI_TouchEnd);
    XISetMask(mask, XI_ButtonRelease);

    XIEventMask evmask{};
    evmask.deviceid = XIAllMasterDevices;
    evmask.mask_len = sizeof(mask);
    evmask.mask = mask;
    XISelectEvents(dpy, win, &evmask, 1);
    XFlush(dpy);
}

int main(int argc, char** argv) {
    parse_args(argc, argv);
    new_game();
    dpy = XOpenDisplay(nullptr);
    if (!dpy) {
        fprintf(stderr, "Cannot open X display.\n");
        return 1;
    }
    screen_id = DefaultScreen(dpy);
    int start_w = fullscreen ? DisplayWidth(dpy, screen_id) : kMinW;
    int start_h = fullscreen ? DisplayHeight(dpy, screen_id) : kMinH;
    int start_x = fullscreen ? 0 : 80;
    int start_y = fullscreen ? 0 : 80;
    set_surface_size(start_w, start_h);
    win = XCreateSimpleWindow(dpy, RootWindow(dpy, screen_id), start_x, start_y, start_w, start_h, 0,
                              BlackPixel(dpy, screen_id), color(42, 82, 50));
    XStoreName(dpy, win, "Gomoku GUI");
    XSelectInput(dpy, win, ExposureMask | ButtonReleaseMask | KeyReleaseMask | StructureNotifyMask);
    gc = XCreateGC(dpy, win, 0, nullptr);
    if (fullscreen) {
        Atom wm_state = XInternAtom(dpy, "_NET_WM_STATE", False);
        Atom fs_atom = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
        XChangeProperty(dpy, win, wm_state, XA_ATOM, 32, PropModeReplace,
                        reinterpret_cast<unsigned char*>(&fs_atom), 1);
    }
    XMapWindow(dpy, win);
    enable_touch_events();

    bool running = true;
    while (running) {
        while (XPending(dpy)) {
            XEvent e;
            XNextEvent(dpy, &e);
            if (e.xcookie.type == GenericEvent && e.xcookie.extension == xi_opcode &&
                XGetEventData(dpy, &e.xcookie)) {
                if (e.xcookie.evtype == XI_TouchEnd || e.xcookie.evtype == XI_ButtonRelease) {
                    auto* dev = static_cast<XIDeviceEvent*>(e.xcookie.data);
                    handle_click(to_logical_x((int)(dev->event_x + 0.5)), to_logical_y((int)(dev->event_y + 0.5)));
                    draw_scene();
                }
                XFreeEventData(dpy, &e.xcookie);
            } else if (e.type == Expose) draw_scene();
            else if (e.type == ConfigureNotify) {
                set_surface_size(e.xconfigure.width, e.xconfigure.height);
                draw_scene();
            } else if (e.type == ButtonRelease) {
                handle_click(to_logical_x(e.xbutton.x), to_logical_y(e.xbutton.y));
                draw_scene();
            } else if (e.type == KeyRelease) {
                char buf[8] = {};
                KeySym ks = 0;
                XLookupString(&e.xkey, buf, sizeof(buf), &ks, nullptr);
                char ch = buf[0];
                if (ch == 'q' || ch == 'Q' || ks == XK_Escape) running = false;
                else if (review_mode) handle_review_key(ch);
                else if (ch == 'n' || ch == 'N') new_game();
                else if (ch == 'u' || ch == 'U') undo_turn();
                else if (ch == 'd' || ch == 'D') dbg = !dbg;
                else if (ch == 's' || ch == 'S') save_log("gomoku_log.txt");
                else if (ch == 'l' || ch == 'L') load_replay("gomoku_replay.txt");
                draw_scene();
            }
        }
        usleep(16000);
    }
    XFreeGC(dpy, gc);
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
    return 0;
}

#endif
