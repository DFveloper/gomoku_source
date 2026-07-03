#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <iostream>
#include <string>
#include <sstream>

#include "gomoku_core.h"

namespace {

int board[gomoku::kMaxBoard][gomoku::kMaxBoard];
int sz = 15;
int turn = 1;
bool game_over = false;
int winner = 0;
int default_depth = 3;
FILE* agent_log = nullptr;

void clear_board() {
    std::memset(board, 0, sizeof(board));
    turn = 1;
    game_over = false;
    winner = 0;
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
    if (gomoku::check_win_at(board, sz, r, c, p)) {
        game_over = true;
        winner = p;
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

    return pass;
}

void print_help() {
    std::printf("Commands:\n");
    std::printf("  play              interactive human black vs AI white\n");
    std::printf("  agent             text protocol for Codex / other LLMs\n");
    std::printf("  genmove           read current board from stdin, print AI move\n");
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
            if (iss >> d) default_depth = std::max(1, std::min(5, d));
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
                std::printf("RESULT BLACK_WINS\n");
                log_line("RESULT BLACK_WINS");
                continue;
            }
            gomoku::Move ai = gomoku::choose_ai_move(board, sz, 2, default_depth);
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
                std::printf("RESULT WHITE_WINS\n");
                log_line("RESULT WHITE_WINS");
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
            gomoku::Move ai = gomoku::choose_ai_move(board, sz, 2, default_depth);
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
                std::printf("RESULT WHITE_WINS\n");
                log_line("RESULT WHITE_WINS");
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
    if (cmd == "play") return play();
    if (cmd == "agent") return agent();
    print_help();
    return cmd == "help" ? 0 : 2;
}
