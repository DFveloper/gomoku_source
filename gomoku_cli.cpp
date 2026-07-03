#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "gomoku_core.h"

namespace {

int board[gomoku::kMaxBoard][gomoku::kMaxBoard];
int sz = 15;

void clear_board() {
    std::memset(board, 0, sizeof(board));
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

    return pass;
}

void print_help() {
    std::printf("Commands:\n");
    std::printf("  play              interactive human black vs AI white\n");
    std::printf("  genmove           read current board from stdin, print AI move\n");
    std::printf("  test              run tactical regression tests\n");
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
    gomoku::Move m = gomoku::choose_ai_move(board, sz, 2, 3);
    std::printf("%d %d\n", m.r, m.c);
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
        board[r][c] = 1;
        if (gomoku::check_win_at(board, sz, r, c, 1)) {
            print_board();
            std::printf("black wins\n");
            return 0;
        }
        gomoku::Move ai = gomoku::choose_ai_move(board, sz, 2, 3);
        if (ai.r == -1) {
            std::printf("draw\n");
            return 0;
        }
        board[ai.r][ai.c] = 2;
        std::printf("ai %d %d\n", ai.r, ai.c);
        print_board();
        if (gomoku::check_win_at(board, sz, ai.r, ai.c, 2)) {
            std::printf("white wins\n");
            return 0;
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    std::string cmd = argc >= 2 ? argv[1] : "help";
    if (cmd == "test") return run_tests() ? 0 : 1;
    if (cmd == "genmove") return genmove();
    if (cmd == "play") return play();
    print_help();
    return cmd == "help" ? 0 : 2;
}
