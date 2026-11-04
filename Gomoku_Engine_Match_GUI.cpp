#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>

#include "engine_match_resources.h"
#include "gomoku_core.h"

namespace {

using gomoku::EngineVersion;
using gomoku::Move;

constexpr int kSize = 15;
constexpr int kMinWidth = 1180;
constexpr int kMinHeight = 800;
constexpr int kEngineCount = static_cast<int>(EngineVersion::Count);

struct Rect { int x = 0, y = 0, w = 0, h = 0; };
struct EngineConfig { EngineVersion version = EngineVersion::CurrentV3; int depth = 4; };
enum class OpeningMode { Random, Custom };

HWND main_window = nullptr;
int client_w = 1280, client_h = 800;
int board[gomoku::kMaxBoard][gomoku::kMaxBoard] = {};
std::vector<Move> history;
Move last_move{-1, -1};
gomoku::WinLine win_line;
EngineConfig black_engine{EngineVersion::LegacyV1, 3};
EngineConfig white_engine{EngineVersion::CurrentV3, 4};
OpeningMode opening_mode = OpeningMode::Random;
int random_opening_plies = 4;
bool match_running = false;
bool game_over = false;
int winner = 0;
bool engine_thinking = false;
bool move_ready = false;
Move pending_move{-1, -1};
std::string pending_error;
long long last_search_ms = 0;
double displayed_black_chance = 50.0;
std::chrono::steady_clock::time_point next_turn_at{};
std::atomic<int> job_sequence{0};
std::mutex result_mutex;
std::string engine_temp_root;
std::array<std::string, kEngineCount> external_engine_paths{};
std::vector<std::string> extracted_files;

Rect board_box, panel_box;
Rect black_version_box, black_minus_box, black_plus_box;
Rect white_version_box, white_minus_box, white_plus_box;
Rect opening_box, opening_minus_box, opening_plus_box;
Rect start_box, reset_box, undo_box;

bool inside(Rect r, int x, int y) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

int current_player() { return history.size() % 2 == 0 ? 1 : 2; }

EngineConfig config_for(int player) { return player == 1 ? black_engine : white_engine; }

const char* engine_name(EngineVersion version) { return gomoku::engine_version_name(version); }

void layout() {
    int panel_w = std::max(390, client_w / 3);
    panel_box = {client_w - panel_w, 0, panel_w, client_h};
    int available_w = panel_box.x - 50;
    int board_pixels = std::min(client_h - 60, available_w - 20);
    board_pixels = std::max(560, board_pixels);
    board_box = {25 + (available_w - board_pixels) / 2,
                 std::max(25, (client_h - board_pixels) / 2), board_pixels, board_pixels};

    int x = panel_box.x + 24;
    int width = panel_box.w - 48;
    black_version_box = {x, 92, width, 42};
    black_minus_box = {x, 151, 48, 40};
    black_plus_box = {x + width - 48, 151, 48, 40};
    white_version_box = {x, 232, width, 42};
    white_minus_box = {x, 291, 48, 40};
    white_plus_box = {x + width - 48, 291, 48, 40};
    opening_box = {x, 370, width, 42};
    opening_minus_box = {x, 429, 48, 40};
    opening_plus_box = {x + width - 48, 429, 48, 40};
    start_box = {x, 493, width, 48};
    reset_box = {x, 551, (width - 10) / 2, 44};
    undo_box = {reset_box.x + reset_box.w + 10, 551, reset_box.w, 44};
}

COLORREF color(int r, int g, int b) { return RGB(r, g, b); }

void fill(HDC dc, Rect r, COLORREF c) {
    HBRUSH brush = CreateSolidBrush(c);
    RECT native{r.x, r.y, r.x + r.w, r.y + r.h};
    FillRect(dc, &native, brush);
    DeleteObject(brush);
}

void draw_text(HDC dc, const std::string& value, int x, int y, int size,
               COLORREF c, bool bold = false) {
    HFONT font = CreateFontA(size, 0, 0, 0, bold ? FW_BOLD : FW_SEMIBOLD,
                             FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH | FF_SWISS, "Segoe UI");
    HFONT old = static_cast<HFONT>(SelectObject(dc, font));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, c);
    TextOutA(dc, x, y, value.c_str(), static_cast<int>(value.size()));
    SelectObject(dc, old);
    DeleteObject(font);
}

void round_box(HDC dc, Rect r, COLORREF fill_color, COLORREF border_color) {
    HBRUSH brush = CreateSolidBrush(fill_color);
    HPEN pen = CreatePen(PS_SOLID, 1, border_color);
    HBRUSH old_brush = static_cast<HBRUSH>(SelectObject(dc, brush));
    HPEN old_pen = static_cast<HPEN>(SelectObject(dc, pen));
    RoundRect(dc, r.x, r.y, r.x + r.w, r.y + r.h, 12, 12);
    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    DeleteObject(brush);
    DeleteObject(pen);
}

void circle(HDC dc, int cx, int cy, int radius, COLORREF fill_color, COLORREF border_color,
            int border_width = 2) {
    HBRUSH brush = CreateSolidBrush(fill_color);
    HPEN pen = CreatePen(PS_SOLID, border_width, border_color);
    HBRUSH old_brush = static_cast<HBRUSH>(SelectObject(dc, brush));
    HPEN old_pen = static_cast<HPEN>(SelectObject(dc, pen));
    Ellipse(dc, cx - radius, cy - radius, cx + radius, cy + radius);
    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    DeleteObject(brush);
    DeleteObject(pen);
}

void button(HDC dc, Rect r, const std::string& label, bool enabled = true,
            COLORREF accent = color(238, 238, 228)) {
    COLORREF bg = enabled ? accent : color(122, 133, 121);
    round_box(dc, r, bg, bg);
    int text_width = static_cast<int>(label.size()) * 9;
    draw_text(dc, label, r.x + std::max(10, (r.w - text_width) / 2), r.y + 10,
              20, enabled ? color(38, 49, 38) : color(71, 79, 70), true);
}

void clear_position() {
    std::memset(board, 0, sizeof(board));
    history.clear();
    last_move = {-1, -1};
    win_line = {};
    winner = 0;
    game_over = false;
    pending_error.clear();
    displayed_black_chance = 50.0;
}

bool board_full() { return history.size() >= kSize * kSize; }

void refresh_evaluation() {
    int local[gomoku::kMaxBoard][gomoku::kMaxBoard] = {};
    for (int r = 0; r < kSize; ++r)
        for (int c = 0; c < kSize; ++c) local[r][c] = board[r][c];
    displayed_black_chance = 50.0 +
        std::tanh(gomoku::static_eval(local, kSize, 1) / 350000.0) * 49.0;
}

bool legal_opening_move(Move move, int player) {
    if (!gomoku::ok(kSize, move.r, move.c) || board[move.r][move.c] != 0) return false;
    if (player == 1 && gomoku::is_forbidden_move(board, kSize, move.r, move.c)) return false;
    board[move.r][move.c] = player;
    bool wins = gomoku::check_win_at(board, kSize, move.r, move.c, player);
    board[move.r][move.c] = 0;
    return !wins;
}

void generate_random_opening() {
    clear_position();
    std::mt19937 rng(static_cast<unsigned int>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count()));
    for (int ply = 0; ply < random_opening_plies; ++ply) {
        int player = current_player();
        std::vector<Move> candidates;
        if (history.empty()) {
            for (int r = kSize / 2 - 2; r <= kSize / 2 + 2; ++r)
                for (int c = kSize / 2 - 2; c <= kSize / 2 + 2; ++c)
                    candidates.push_back({r, c});
        } else {
            candidates = gomoku::generate_candidates(board, kSize, 2);
        }
        std::shuffle(candidates.begin(), candidates.end(), rng);
        bool placed = false;
        for (Move move : candidates) {
            if (!legal_opening_move(move, player)) continue;
            board[move.r][move.c] = player;
            history.push_back(move);
            last_move = move;
            placed = true;
            break;
        }
        if (!placed) break;
    }
    refresh_evaluation();
}

Move last_move_for(const std::vector<Move>& moves, int player) {
    for (int i = static_cast<int>(moves.size()) - 1; i >= 0; --i) {
        if (((i % 2 == 0) ? 1 : 2) == player) return moves[i];
    }
    return {-1, -1};
}

bool extract_resource_file(int resource_id, const std::string& relative_path) {
    HRSRC resource = FindResourceA(GetModuleHandleA(nullptr),
                                   MAKEINTRESOURCEA(resource_id), RT_RCDATA);
    if (!resource) return false;
    HGLOBAL loaded = LoadResource(GetModuleHandleA(nullptr), resource);
    if (!loaded) return false;
    void* data = LockResource(loaded);
    DWORD size = SizeofResource(GetModuleHandleA(nullptr), resource);
    std::string path = engine_temp_root + "\\" + relative_path;
    HANDLE file = CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    BOOL ok = WriteFile(file, data, size, &written, nullptr);
    CloseHandle(file);
    if (ok && written == size) extracted_files.push_back(path);
    return ok && written == size;
}

bool extract_all_engines() {
    char temp_dir[MAX_PATH] = {};
    if (!GetTempPathA(MAX_PATH, temp_dir)) return false;
    engine_temp_root = std::string(temp_dir) + "gomoku_match_engines_" +
                       std::to_string(GetCurrentProcessId());
    CreateDirectoryA(engine_temp_root.c_str(), nullptr);
    for (const char* folder : {"carbon", "rapfi", "alpha", "xl"})
        CreateDirectoryA((engine_temp_root + "\\" + folder).c_str(), nullptr);
    CreateDirectoryA((engine_temp_root + "\\alpha\\networks").c_str(), nullptr);

    bool ok = true;
    ok &= extract_resource_file(IDR_CARBON_ENGINE, "carbon\\pbrain-carbon.exe");
    external_engine_paths[static_cast<int>(EngineVersion::CarbonClassic)] =
        engine_temp_root + "\\carbon\\pbrain-carbon.exe";

    ok &= extract_resource_file(IDR_RAPFI_ENGINE, "rapfi\\pbrain-rapfi.exe");
    ok &= extract_resource_file(IDR_RAPFI_CONFIG, "rapfi\\config.toml");
    ok &= extract_resource_file(IDR_RAPFI_MODEL, "rapfi\\model210901.bin");
    ok &= extract_resource_file(IDR_RAPFI_FREESTYLE, "rapfi\\mix9svqfreestyle_bsmix.bin.lz4");
    ok &= extract_resource_file(IDR_RAPFI_STANDARD, "rapfi\\mix9svqstandard_bs15.bin.lz4");
    ok &= extract_resource_file(IDR_RAPFI_RENJU_BLACK, "rapfi\\mix9svqrenju_bs15_black.bin.lz4");
    ok &= extract_resource_file(IDR_RAPFI_RENJU_WHITE, "rapfi\\mix9svqrenju_bs15_white.bin.lz4");
    ok &= extract_resource_file(IDR_RAPFI_AUTHORS, "rapfi\\AUTHORS");
    external_engine_paths[static_cast<int>(EngineVersion::Rapfi2025)] =
        engine_temp_root + "\\rapfi\\pbrain-rapfi.exe";

    ok &= extract_resource_file(IDR_ALPHA_ENGINE, "alpha\\pbrain-AlphaGomoku.exe");
    ok &= extract_resource_file(IDR_ALPHA_CONFIG, "alpha\\config.json");
    ok &= extract_resource_file(IDR_ALPHA_SWAP2, "alpha\\swap2_openings.json");
    ok &= extract_resource_file(IDR_ALPHA_RENJU_NETWORK, "alpha\\networks\\renju_conv_8x128.bin");
    ok &= extract_resource_file(IDR_ALPHA_LIBDL, "alpha\\libdl.dll");
    ok &= extract_resource_file(IDR_ALPHA_LIBGCC, "alpha\\libgcc_s_seh-1.dll");
    ok &= extract_resource_file(IDR_ALPHA_LIBGOMP, "alpha\\libgomp-1.dll");
    ok &= extract_resource_file(IDR_ALPHA_LIBSTDCPP, "alpha\\libstdc++-6.dll");
    ok &= extract_resource_file(IDR_ALPHA_LIBWINPTHREAD, "alpha\\libwinpthread-1.dll");
    external_engine_paths[static_cast<int>(EngineVersion::AlphaGomoku2026)] =
        engine_temp_root + "\\alpha\\pbrain-AlphaGomoku.exe";

    ok &= extract_resource_file(IDR_XL_ENGINE, "xl\\pbrain-xl.exe");
    external_engine_paths[static_cast<int>(EngineVersion::XLNiren)] =
        engine_temp_root + "\\xl\\pbrain-xl.exe";
    return ok;
}

void cleanup_extracted_engines() {
    for (auto it = extracted_files.rbegin(); it != extracted_files.rend(); ++it) DeleteFileA(it->c_str());
    RemoveDirectoryA((engine_temp_root + "\\alpha\\networks").c_str());
    for (const char* folder : {"carbon", "rapfi", "alpha", "xl"})
        RemoveDirectoryA((engine_temp_root + "\\" + folder).c_str());
    if (!engine_temp_root.empty()) RemoveDirectoryA(engine_temp_root.c_str());
}

bool write_pipe(HANDLE pipe, const std::string& text) {
    DWORD written = 0;
    return WriteFile(pipe, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) &&
           written == text.size();
}

Move external_best_move(EngineVersion version,
                      const std::array<int, gomoku::kMaxBoard * gomoku::kMaxBoard>& snapshot,
                      const std::vector<Move>& moves, int player, int depth,
                      int job, std::string& error) {
    const std::string& engine_path = external_engine_paths[static_cast<int>(version)];
    if (engine_path.empty()) {
        error = std::string(engine_name(version)) + " resource extraction failed";
        return {-1, -1};
    }
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE child_stdout_read = nullptr, child_stdout_write = nullptr;
    HANDLE child_stdin_read = nullptr, child_stdin_write = nullptr;
    if (!CreatePipe(&child_stdout_read, &child_stdout_write, &security, 0) ||
        !CreatePipe(&child_stdin_read, &child_stdin_write, &security, 0)) {
        error = std::string(engine_name(version)) + " pipe creation failed";
        return {-1, -1};
    }
    SetHandleInformation(child_stdout_read, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(child_stdin_write, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdOutput = child_stdout_write;
    startup.hStdError = child_stdout_write;
    startup.hStdInput = child_stdin_read;
    PROCESS_INFORMATION process{};
    std::vector<char> command(engine_path.begin(), engine_path.end());
    command.push_back('\0');
    std::string working_dir = engine_path.substr(0, engine_path.find_last_of("\\/"));
    BOOL created = CreateProcessA(nullptr, command.data(), nullptr, nullptr, TRUE,
                                  CREATE_NO_WINDOW, nullptr, working_dir.c_str(), &startup, &process);
    CloseHandle(child_stdout_write);
    CloseHandle(child_stdin_read);
    if (!created) {
        CloseHandle(child_stdout_read);
        CloseHandle(child_stdin_write);
        error = std::string(engine_name(version)) + " process launch failed";
        return {-1, -1};
    }

    int turn_time_ms = std::min(16000, 250 * (1 << std::max(0, depth - 1)));
    if (version == EngineVersion::CarbonClassic) turn_time_ms = 180000;
    std::string input = "START 15\nINFO timeout_turn " + std::to_string(turn_time_ms) +
                        "\nINFO timeout_match 1800000\n";
    input += "INFO rule 4\nINFO max_depth " + std::to_string(depth) + "\nBOARD\n";
    for (size_t i = 0; i < moves.size(); ++i) {
        int absolute_stone = (i % 2 == 0) ? 1 : 2;
        int stone = absolute_stone == player ? 1 : 2;  // Gomocup: 1=engine, 2=opponent.
        input += std::to_string(moves[i].c) + "," + std::to_string(moves[i].r) +
                 "," + std::to_string(stone) + "\n";
    }
    input += "DONE\nEND\n";
    if (!write_pipe(child_stdin_write, input))
        error = std::string(engine_name(version)) + " command write failed";
    CloseHandle(child_stdin_write);

    std::string output;
    Move result{-1, -1};
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(std::max(30000, turn_time_ms + 15000));
    while (std::chrono::steady_clock::now() < deadline && result.r == -1) {
        if (job_sequence.load() != job) {
            TerminateProcess(process.hProcess, 2);
            error = std::string(engine_name(version)) + " search cancelled";
            break;
        }
        DWORD available = 0;
        if (PeekNamedPipe(child_stdout_read, nullptr, 0, nullptr, &available, nullptr) && available) {
            char buffer[512];
            DWORD read = 0;
            if (ReadFile(child_stdout_read, buffer, std::min<DWORD>(available, sizeof(buffer)),
                         &read, nullptr) && read) {
                output.append(buffer, buffer + read);
                size_t line_end = 0;
                while ((line_end = output.find('\n')) != std::string::npos) {
                    std::string line = output.substr(0, line_end);
                    output.erase(0, line_end + 1);
                    int x = -1, y = -1;
                    if (std::sscanf(line.c_str(), "%d,%d", &x, &y) == 2 &&
                        gomoku::ok(kSize, y, x)) result = {y, x};
                }
            }
        }
        if (WaitForSingleObject(process.hProcess, 0) == WAIT_OBJECT_0 && result.r == -1) break;
        Sleep(10);
    }
    if (result.r == -1 && error.empty()) {
        error = std::string(engine_name(version)) + " returned no move";
        TerminateProcess(process.hProcess, 3);
    }
    WaitForSingleObject(process.hProcess, 1000);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    CloseHandle(child_stdout_read);
    (void)snapshot;
    (void)player;
    return result;
}

void start_engine_turn() {
    if (!match_running || game_over || engine_thinking) return;
    int player = current_player();
    EngineConfig config = config_for(player);
    EngineConfig opponent_config = config_for(3 - player);
    std::array<int, gomoku::kMaxBoard * gomoku::kMaxBoard> snapshot{};
    for (int r = 0; r < kSize; ++r)
        for (int c = 0; c < kSize; ++c) snapshot[r * gomoku::kMaxBoard + c] = board[r][c];
    std::vector<Move> moves = history;
    int job = ++job_sequence;
    {
        std::lock_guard<std::mutex> lock(result_mutex);
        engine_thinking = true;
        move_ready = false;
        pending_error.clear();
    }
    std::thread([snapshot, moves, player, config, opponent_config, job]() {
        auto started = std::chrono::steady_clock::now();
        Move move{-1, -1};
        std::string error;
        if (config.version == EngineVersion::CurrentV3 &&
            opponent_config.version != EngineVersion::LegacyV1) {
            int local[gomoku::kMaxBoard][gomoku::kMaxBoard] = {};
            for (int r = 0; r < kSize; ++r)
                for (int c = 0; c < kSize; ++c)
                    local[r][c] = snapshot[r * gomoku::kMaxBoard + c];
            Move win = gomoku::find_immediate_win(local, kSize, player);
            Move block = gomoku::find_immediate_win(local, kSize, 3 - player);
            if (win.r != -1) move = win;
            else if (block.r != -1 &&
                     !(player == 1 && gomoku::is_forbidden_move(local, kSize, block.r, block.c)))
                move = block;
            else {
                move = external_best_move(EngineVersion::AlphaGomoku2026, snapshot, moves,
                                          player, std::min(5, config.depth + 2), job, error);
                if (gomoku::is_legal_move(local, kSize, player, move)) {
                    local[move.r][move.c] = player;
                    const auto deadline = std::chrono::steady_clock::now() +
                                          std::chrono::milliseconds(350);
                    const bool unsafe = gomoku::has_forced_four_win(local, kSize, 3 - player, 5, 320) ||
                        gomoku::opponent_has_unavoidable_vcf_setup(
                            local, kSize, 3 - player, deadline, 12, 48);
                    local[move.r][move.c] = 0;
                    if (unsafe) move = {-1, -1};
                }
                if (!gomoku::is_legal_move(local, kSize, player, move)) {
                    gomoku::OpponentStyle style =
                        gomoku::analyze_opponent_style(kSize, moves, 3 - player);
                    move = gomoku::choose_ai_move(local, kSize, player,
                        std::min(2, config.depth), &style, 0,
                        last_move_for(moves, player));
                }
            }
        } else if (static_cast<int>(config.version) >= static_cast<int>(EngineVersion::CarbonClassic)) {
            move = external_best_move(config.version, snapshot, moves, player,
                                      config.depth, job, error);
        } else {
            int local[gomoku::kMaxBoard][gomoku::kMaxBoard] = {};
            for (int r = 0; r < kSize; ++r)
                for (int c = 0; c < kSize; ++c)
                    local[r][c] = snapshot[r * gomoku::kMaxBoard + c];
            gomoku::OpponentStyle style = gomoku::analyze_opponent_style(kSize, moves, 3 - player);
            move = gomoku::choose_versioned_move(local, kSize, player, config.depth,
                                                  config.version, &style, 0,
                                                  last_move_for(moves, player));
        }
        long long elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count();
        std::lock_guard<std::mutex> lock(result_mutex);
        if (job_sequence.load() != job) return;
        pending_move = move;
        pending_error = error;
        last_search_ms = elapsed;
        move_ready = true;
    }).detach();
}

void finish_with_winner(int player, const std::string& reason = {}) {
    match_running = false;
    game_over = true;
    engine_thinking = false;
    winner = player;
    displayed_black_chance = player == 1 ? 100.0 : (player == 2 ? 0.0 : 50.0);
    if (!reason.empty()) pending_error = reason;
}

void apply_engine_result() {
    Move move;
    std::string error;
    {
        std::lock_guard<std::mutex> lock(result_mutex);
        if (!move_ready) return;
        move = pending_move;
        error = pending_error;
        move_ready = false;
    }
    int player = current_player();
    bool legal = gomoku::ok(kSize, move.r, move.c) && board[move.r][move.c] == 0;
    if (legal && player == 1) legal = !gomoku::is_forbidden_move(board, kSize, move.r, move.c);
    if (!legal) {
        finish_with_winner(3 - player, error.empty() ? "illegal engine move" : error);
        return;
    }
    board[move.r][move.c] = player;
    history.push_back(move);
    last_move = move;
    engine_thinking = false;
    if (gomoku::check_win_at(board, kSize, move.r, move.c, player, &win_line)) {
        finish_with_winner(player);
    } else if (board_full()) {
        match_running = false;
        game_over = true;
        winner = 0;
        refresh_evaluation();
    } else {
        refresh_evaluation();
        next_turn_at = std::chrono::steady_clock::now() + std::chrono::milliseconds(450);
    }
}

void begin_match() {
    ++job_sequence;
    engine_thinking = false;
    move_ready = false;
    if (opening_mode == OpeningMode::Random) generate_random_opening();
    winner = 0;
    game_over = false;
    match_running = true;
    pending_error.clear();
    next_turn_at = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
}

void reset_match() {
    ++job_sequence;
    match_running = false;
    engine_thinking = false;
    move_ready = false;
    clear_position();
}

void cycle_version(EngineConfig& config) {
    int value = (static_cast<int>(config.version) + 1) % kEngineCount;
    config.version = static_cast<EngineVersion>(value);
}

std::string status_text() {
    if (game_over) {
        if (winner == 0) return "DRAW";
        return std::string(winner == 1 ? "BLACK WINS - " : "WHITE WINS - ") +
               engine_name(config_for(winner).version);
    }
    if (!match_running) {
        return opening_mode == OpeningMode::Custom ? "SET OPENING, THEN START" : "READY";
    }
    int player = current_player();
    if (engine_thinking) return std::string(player == 1 ? "BLACK THINKING - " : "WHITE THINKING - ") +
                                 engine_name(config_for(player).version);
    return "NEXT MOVE";
}

void draw_scene(HDC dc) {
    layout();
    fill(dc, {0, 0, client_w, client_h}, color(42, 82, 50));
    fill(dc, panel_box, color(34, 66, 41));
    round_box(dc, board_box, color(235, 187, 114), color(235, 187, 114));
    int margin = 40;
    int cell = (board_box.w - margin * 2) / (kSize - 1);
    int grid_x = board_box.x + margin;
    int grid_y = board_box.y + margin;
    int grid_end_x = grid_x + cell * (kSize - 1);
    int grid_end_y = grid_y + cell * (kSize - 1);
    HPEN grid_pen = CreatePen(PS_SOLID, 1, color(86, 62, 40));
    HPEN old_pen = static_cast<HPEN>(SelectObject(dc, grid_pen));
    for (int i = 0; i < kSize; ++i) {
        MoveToEx(dc, grid_x, grid_y + i * cell, nullptr);
        LineTo(dc, grid_end_x, grid_y + i * cell);
        MoveToEx(dc, grid_x + i * cell, grid_y, nullptr);
        LineTo(dc, grid_x + i * cell, grid_end_y);
    }
    SelectObject(dc, old_pen);
    DeleteObject(grid_pen);
    for (int point : {3, 7, 11})
        for (int point2 : {3, 7, 11}) circle(dc, grid_x + point2 * cell, grid_y + point * cell,
                                             4, color(86, 62, 40), color(86, 62, 40), 1);
    for (int r = 0; r < kSize; ++r) for (int c = 0; c < kSize; ++c) {
        if (!board[r][c]) continue;
        int cx = grid_x + c * cell, cy = grid_y + r * cell;
        int radius = std::max(10, cell / 2 - 3);
        if (board[r][c] == 1) circle(dc, cx, cy, radius, color(20, 21, 20), color(7, 7, 7));
        else circle(dc, cx, cy, radius, color(242, 238, 225), color(150, 145, 134));
    }
    if (last_move.r != -1) {
        int cx = grid_x + last_move.c * cell, cy = grid_y + last_move.r * cell;
        HPEN pen = CreatePen(PS_SOLID, 3, color(129, 206, 76));
        old_pen = static_cast<HPEN>(SelectObject(dc, pen));
        HBRUSH old_brush = static_cast<HBRUSH>(SelectObject(dc, GetStockObject(NULL_BRUSH)));
        int radius = std::max(12, cell / 2);
        Ellipse(dc, cx - radius, cy - radius, cx + radius, cy + radius);
        SelectObject(dc, old_brush);
        SelectObject(dc, old_pen);
        DeleteObject(pen);
    }
    if (win_line.start.r != -1) {
        HPEN pen = CreatePen(PS_SOLID, 6, color(224, 38, 38));
        old_pen = static_cast<HPEN>(SelectObject(dc, pen));
        MoveToEx(dc, grid_x + win_line.start.c * cell, grid_y + win_line.start.r * cell, nullptr);
        LineTo(dc, grid_x + win_line.end.c * cell, grid_y + win_line.end.r * cell);
        SelectObject(dc, old_pen);
        DeleteObject(pen);
    }

    int x = panel_box.x + 24;
    COLORREF cream = color(245, 244, 232), green = color(129, 206, 76);
    draw_text(dc, "ENGINE MATCH", x, 28, 34, cream, true);
    draw_text(dc, "BLACK ENGINE", x, 70, 16, color(214, 224, 200), true);
    button(dc, black_version_box, engine_name(black_engine.version), !match_running);
    button(dc, black_minus_box, "-", !match_running);
    draw_text(dc, "DEPTH " + std::to_string(black_engine.depth), x + 72, 159, 21, cream, true);
    button(dc, black_plus_box, "+", !match_running);
    draw_text(dc, "WHITE ENGINE", x, 210, 16, color(214, 224, 200), true);
    button(dc, white_version_box, engine_name(white_engine.version), !match_running);
    button(dc, white_minus_box, "-", !match_running);
    draw_text(dc, "DEPTH " + std::to_string(white_engine.depth), x + 72, 299, 21, cream, true);
    button(dc, white_plus_box, "+", !match_running);
    draw_text(dc, "OPENING", x, 348, 16, color(214, 224, 200), true);
    button(dc, opening_box, opening_mode == OpeningMode::Random ? "RANDOM" : "CUSTOM BOARD",
           !match_running, opening_mode == OpeningMode::Custom ? green : color(238, 238, 228));
    button(dc, opening_minus_box, "-", !match_running && opening_mode == OpeningMode::Random);
    std::string opening_count = opening_mode == OpeningMode::Random
        ? "START PLIES " + std::to_string(random_opening_plies)
        : "CLICK BOARD / RIGHT-CLICK UNDO";
    draw_text(dc, opening_count, x + 62, 437, opening_mode == OpeningMode::Random ? 19 : 15, cream, true);
    button(dc, opening_plus_box, "+", !match_running && opening_mode == OpeningMode::Random);
    button(dc, start_box, match_running ? "MATCH RUNNING" : "START MATCH", !match_running,
           match_running ? color(122, 133, 121) : green);
    button(dc, reset_box, "RESET", true);
    button(dc, undo_box, "UNDO OPENING", !match_running && opening_mode == OpeningMode::Custom);
    draw_text(dc, status_text(), x, 615, 21, game_over ? color(240, 181, 54) : green, true);
    draw_text(dc, "MOVES " + std::to_string(history.size()) + "   LAST " +
              std::to_string(last_search_ms) + " ms", x, 648, 17, cream, true);
    if (!pending_error.empty()) draw_text(dc, pending_error, x, 676, 15, color(255, 150, 140), true);

    Rect eval_bar{x, 708, panel_box.w - 48, 20};
    fill(dc, eval_bar, color(225, 224, 214));
    fill(dc, {eval_bar.x, eval_bar.y, static_cast<int>(eval_bar.w * displayed_black_chance / 100.0), eval_bar.h},
         color(47, 49, 47));
    draw_text(dc, "BLACK " + std::to_string(static_cast<int>(displayed_black_chance + 0.5)) + "%", x, 733, 15, cream, true);
    if (!match_running && opening_mode == OpeningMode::Custom)
        draw_text(dc, "Opening editor enabled. Board input locks after START.",
                  board_box.x + 12, board_box.y + board_box.h - 28, 16, color(86, 62, 40), true);
}

bool point_to_board(int x, int y, Move& move) {
    int margin = 40;
    int cell = (board_box.w - margin * 2) / (kSize - 1);
    int grid_x = board_box.x + margin, grid_y = board_box.y + margin;
    int c = (x - grid_x + cell / 2) / cell;
    int r = (y - grid_y + cell / 2) / cell;
    if (!gomoku::ok(kSize, r, c)) return false;
    int cx = grid_x + c * cell, cy = grid_y + r * cell;
    if (std::abs(x - cx) > cell / 2 || std::abs(y - cy) > cell / 2) return false;
    move = {r, c};
    return true;
}

void handle_left_click(int x, int y) {
    if (inside(reset_box, x, y)) {
        reset_match();
        return;
    }
    if (match_running) return;
    if (inside(black_version_box, x, y)) cycle_version(black_engine);
    else if (inside(white_version_box, x, y)) cycle_version(white_engine);
    else if (inside(black_minus_box, x, y)) black_engine.depth = std::max(1, black_engine.depth - 1);
    else if (inside(black_plus_box, x, y)) black_engine.depth = std::min(6, black_engine.depth + 1);
    else if (inside(white_minus_box, x, y)) white_engine.depth = std::max(1, white_engine.depth - 1);
    else if (inside(white_plus_box, x, y)) white_engine.depth = std::min(6, white_engine.depth + 1);
    else if (inside(opening_box, x, y)) {
        opening_mode = opening_mode == OpeningMode::Random ? OpeningMode::Custom : OpeningMode::Random;
        clear_position();
    } else if (inside(opening_minus_box, x, y) && opening_mode == OpeningMode::Random)
        random_opening_plies = std::max(0, random_opening_plies - 1);
    else if (inside(opening_plus_box, x, y) && opening_mode == OpeningMode::Random)
        random_opening_plies = std::min(12, random_opening_plies + 1);
    else if (inside(undo_box, x, y) && opening_mode == OpeningMode::Custom) {
        if (!history.empty()) {
            Move move = history.back();
            history.pop_back();
            board[move.r][move.c] = 0;
            last_move = history.empty() ? Move{-1, -1} : history.back();
            refresh_evaluation();
        }
    } else if (inside(start_box, x, y)) begin_match();
    else if (opening_mode == OpeningMode::Custom) {
        Move move;
        if (point_to_board(x, y, move) && legal_opening_move(move, current_player())) {
            board[move.r][move.c] = current_player();
            history.push_back(move);
            last_move = move;
            refresh_evaluation();
        }
    }
}

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
            info->ptMinTrackSize = {kMinWidth, kMinHeight};
            return 0;
        }
        case WM_SIZE:
            client_w = std::max(1, static_cast<int>(LOWORD(lparam)));
            client_h = std::max(1, static_cast<int>(HIWORD(lparam)));
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        case WM_LBUTTONUP:
            handle_left_click(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        case WM_RBUTTONUP:
            if (!match_running && opening_mode == OpeningMode::Custom && !history.empty()) {
                Move move = history.back();
                history.pop_back();
                board[move.r][move.c] = 0;
                last_move = history.empty() ? Move{-1, -1} : history.back();
                refresh_evaluation();
                InvalidateRect(window, nullptr, FALSE);
            }
            return 0;
        case WM_TIMER:
            apply_engine_result();
            if (match_running && !engine_thinking &&
                std::chrono::steady_clock::now() >= next_turn_at) start_engine_turn();
            {
                std::string title = "Gomoku Engine Match - " + status_text() + " - " +
                                    std::to_string(history.size()) + " moves";
                SetWindowTextA(window, title.c_str());
            }
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(window, &paint);
            RECT rect{};
            GetClientRect(window, &rect);
            HDC memory = CreateCompatibleDC(dc);
            HBITMAP bitmap = CreateCompatibleBitmap(dc, rect.right, rect.bottom);
            HBITMAP old = static_cast<HBITMAP>(SelectObject(memory, bitmap));
            draw_scene(memory);
            BitBlt(dc, 0, 0, rect.right, rect.bottom, memory, 0, 0, SRCCOPY);
            SelectObject(memory, old);
            DeleteObject(bitmap);
            DeleteDC(memory);
            EndPaint(window, &paint);
            return 0;
        }
        case WM_DESTROY:
            ++job_sequence;
            cleanup_extracted_engines();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcA(window, message, wparam, lparam);
}

}  // namespace

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int show_command) {
    extract_all_engines();
    WNDCLASSA window_class{};
    window_class.lpfnWndProc = window_proc;
    window_class.hInstance = instance;
    window_class.lpszClassName = "GomokuEngineMatchWindow";
    window_class.hCursor = LoadCursor(nullptr, IDC_HAND);
    window_class.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    RegisterClassA(&window_class);
    main_window = CreateWindowA(window_class.lpszClassName, "Gomoku Engine Match",
                                WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                1280, 840, nullptr, nullptr, instance, nullptr);
    ShowWindow(main_window, show_command);
    UpdateWindow(main_window);
    SetTimer(main_window, 1, 50, nullptr);
    MSG message{};
    while (GetMessageA(&message, nullptr, 0, 0)) {
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }
    return static_cast<int>(message.wParam);
}
