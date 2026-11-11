#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include "gomoku_core.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace gomoku {

// Relative paths (inside third_party/runtime) of the native engine ports.
// The Windows match GUI embeds PE runtimes as resources; everywhere else the
// engines are built from third_party/*-src into these directories.
inline std::filesystem::path external_runtime_relative_path(EngineVersion version) {
    switch (version) {
#ifdef _WIN32
        case EngineVersion::CarbonClassic: return "carbon/pbrain-carbon.exe";
        case EngineVersion::Rapfi2025: return "rapfi/pbrain-rapfi_avx2.exe";
        case EngineVersion::AlphaGomoku2026: return "alphagomoku/pbrain-AlphaGomoku.exe";
        case EngineVersion::XLNiren: return "xl/pbrain-xl.exe";
#else
        case EngineVersion::CarbonClassic: return "carbon/pbrain-carbon";
        case EngineVersion::Rapfi2025: return "rapfi/pbrain-rapfi";
        case EngineVersion::AlphaGomoku2026: return "alphagomoku/pbrain-agomoku";
        case EngineVersion::XLNiren: return "xl/pbrain-xl";
#endif
        default: return {};
    }
}

inline std::filesystem::path external_runtime_path(const std::filesystem::path& runtime_root,
                                                   EngineVersion version) {
    std::filesystem::path relative = external_runtime_relative_path(version);
    if (relative.empty() || runtime_root.empty()) return {};
    return runtime_root / relative;
}

struct ExternalEngineRequest {
    EngineVersion version = EngineVersion::CurrentV3;
    std::vector<Move> moves;
    int player = 2;
    int level = 3;
    int board_size = 15;
    // `max_depth` is a Gomocup hint. Sending our level's number as a depth cap
    // silently crippled every expert: Rapfi then answered in a quarter of the
    // clock it was given and the mixture was measurably weaker than its own
    // member. Zero means "no depth hint", which is what a time-controlled
    // opponent should always send.
    int max_depth_hint = 0;
    std::filesystem::path program;
    const std::atomic<bool>* cancel = nullptr;
};

inline void external_request_board(const ExternalEngineRequest& request,
                                   int board[kMaxBoard][kMaxBoard]) {
    std::memset(board, 0, sizeof(int) * kMaxBoard * kMaxBoard);
    for (size_t i = 0; i < request.moves.size(); ++i) {
        Move move = request.moves[i];
        if (ok(request.board_size, move.r, move.c) && board[move.r][move.c] == 0)
            board[move.r][move.c] = i % 2 == 0 ? 1 : 2;
    }
}

inline bool parse_external_move_line(const std::string& line, Move& move) {
    int x = -1, y = -1;
    char trailing = 0;
#ifdef _WIN32
    if (sscanf_s(line.c_str(), " %d , %d %c", &x, &y, &trailing, 1) != 2) return false;
#else
    if (std::sscanf(line.c_str(), " %d , %d %c", &x, &y, &trailing) != 2) return false;
#endif
    move = {y, x};
    return true;
}

inline bool legal_external_move(const ExternalEngineRequest& request, Move move) {
    int board[kMaxBoard][kMaxBoard];
    external_request_board(request, board);
    return is_legal_move(board, request.board_size, request.player, move);
}

inline int external_turn_time_ms(EngineVersion version, int level) {
    int turn_time_ms = std::min(16000, 250 * (1 << std::max(0, level - 1)));
    if (version == EngineVersion::CarbonClassic) turn_time_ms = 180000;
    return turn_time_ms;
}

#ifdef _WIN32

// Runs one Gomocup/Piskvork protocol turn in a child process and returns its
// move.  On failure the move is {-1,-1} and `error` explains why; a cancelled
// run leaves `error` empty.
inline Move run_external_engine(const ExternalEngineRequest& request, std::string& error) {
    error.clear();
    auto fail = [&](const char* reason) {
        error = reason;
        return Move{-1, -1};
    };
    if (request.program.empty()) return fail("engine runtime binary not found");

    SECURITY_ATTRIBUTES inherit{};
    inherit.nLength = sizeof(inherit);
    inherit.bInheritHandle = TRUE;
    HANDLE to_child_read = nullptr, to_child_write = nullptr;
    HANDLE from_child_read = nullptr, from_child_write = nullptr;
    if (!CreatePipe(&to_child_read, &to_child_write, &inherit, 0))
        return fail("pipe creation failed");
    if (!CreatePipe(&from_child_read, &from_child_write, &inherit, 0)) {
        CloseHandle(to_child_read);
        CloseHandle(to_child_write);
        return fail("pipe creation failed");
    }
    SetHandleInformation(to_child_write, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(from_child_read, HANDLE_FLAG_INHERIT, 0);
    DWORD pipe_mode = PIPE_READMODE_BYTE | PIPE_NOWAIT;
    SetNamedPipeHandleState(from_child_read, &pipe_mode, nullptr, nullptr);

    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = to_child_read;
    startup.hStdOutput = from_child_write;
    startup.hStdError = from_child_write;
    PROCESS_INFORMATION process{};

    std::string program = request.program.string();
    std::string command = "\"" + program + "\"";
    std::string working_dir = request.program.parent_path().string();
    BOOL created = CreateProcessA(nullptr, command.data(), nullptr, nullptr, TRUE,
                                  CREATE_NO_WINDOW, nullptr,
                                  working_dir.empty() ? nullptr : working_dir.c_str(),
                                  &startup, &process);
    CloseHandle(to_child_read);
    CloseHandle(from_child_write);
    if (!created) {
        CloseHandle(to_child_write);
        CloseHandle(from_child_read);
        return fail("process launch failed");
    }

    int turn_time_ms = external_turn_time_ms(request.version, request.level);
    std::string input = "START " + std::to_string(request.board_size) +
                        "\nINFO timeout_turn " + std::to_string(turn_time_ms) +
                        "\nINFO timeout_match 1800000\n";
    input += "INFO rule 4\n";
    if (request.max_depth_hint > 0)
        input += "INFO max_depth " + std::to_string(request.max_depth_hint) + "\n";
    input += "BOARD\n";
    for (size_t i = 0; i < request.moves.size(); ++i) {
        Move m = request.moves[i];
        int absolute_stone = static_cast<int>(i) % 2 == 0 ? 1 : 2;
        int stone = absolute_stone == request.player ? 1 : 2;  // Gomocup: 1=engine, 2=opponent.
        input += std::to_string(m.c) + "," + std::to_string(m.r) + "," + std::to_string(stone) + "\n";
    }
    input += "DONE\n";

    auto cancelled = [&]() { return request.cancel && request.cancel->load(); };
    std::atomic<bool> write_done{false}, write_succeeded{false};
    std::thread writer([&]() {
        DWORD written = 0;
        write_succeeded = WriteFile(to_child_write, input.data(), static_cast<DWORD>(input.size()),
                                    &written, nullptr) && written == input.size();
        write_done = true;
    });
    Move result{-1, -1};
    std::string output;
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(std::max(30000, turn_time_ms + 15000));
    bool timed_out = false;
    bool illegal_move = false;
    auto consume_lines = [&](bool include_partial) {
        size_t line_end = 0;
        while ((line_end = output.find('\n')) != std::string::npos || (include_partial && !output.empty())) {
            std::string line = line_end == std::string::npos ? output : output.substr(0, line_end);
            if (line_end == std::string::npos) output.clear();
            else output.erase(0, line_end + 1);
            Move candidate;
            if (!parse_external_move_line(line, candidate)) continue;
            if (legal_external_move(request, candidate)) result = candidate;
            else illegal_move = true;
            break;
        }
    };
    while (result.r == -1) {
        if (cancelled()) break;
        if (std::chrono::steady_clock::now() >= deadline) { timed_out = true; break; }
        if (write_done.load() && !write_succeeded.load()) break;
        char buffer[4096];
        DWORD got = 0;
        while (PeekNamedPipe(from_child_read, buffer, sizeof(buffer), &got, nullptr, nullptr) && got > 0) {
            DWORD read_now = 0;
            if (!ReadFile(from_child_read, buffer, sizeof(buffer), &read_now, nullptr) || read_now == 0) break;
            output.append(buffer, buffer + read_now);
            consume_lines(false);
            if (result.r != -1 || illegal_move) break;
        }
        if (result.r != -1 || illegal_move) break;
        if (WaitForSingleObject(process.hProcess, 0) == WAIT_OBJECT_0) {
            consume_lines(true);
            break;
        }
        Sleep(10);
    }
    bool was_cancelled = cancelled();
    if (result.r == -1 || !write_done.load()) TerminateProcess(process.hProcess, 1);
    writer.join();
    if (result.r != -1) {
        DWORD ignored = 0;
        const char end[] = "END\n";
        WriteFile(to_child_write, end, sizeof(end) - 1, &ignored, nullptr);
    }
    CloseHandle(to_child_write);
    CloseHandle(from_child_read);
    if (was_cancelled) {
        error.clear();
    } else if (result.r == -1) {
        error = !write_succeeded.load() ? "command write failed" :
                illegal_move ? "returned no legal move" :
                timed_out ? "did not answer in time" : "returned no move";
    }
    if (WaitForSingleObject(process.hProcess, 2000) == WAIT_TIMEOUT) {
        TerminateProcess(process.hProcess, 1);
        WaitForSingleObject(process.hProcess, 2000);
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return result;
}

#else

// POSIX twin of the Win32 runner above; mirrors external_best_move() of the
// match WebUI/GUI so every surface drives engines identically.
inline Move run_external_engine(const ExternalEngineRequest& request, std::string& error) {
    using std::chrono::steady_clock;
    error.clear();
    auto fail = [&](const char* reason) {
        error = reason;
        return Move{-1, -1};
    };
    if (request.program.empty()) return fail("engine runtime binary not found");

    int to_child[2] = {-1, -1}, from_child[2] = {-1, -1};
    if (pipe(to_child) != 0 || pipe(from_child) != 0) {
        for (int fd : {to_child[0], to_child[1], from_child[0], from_child[1]})
            if (fd != -1) close(fd);
        return fail("pipe creation failed");
    }

    // Resolve before the fork: the child changes into the engine's own directory
    // (so it finds its config and networks), which would make a relative path
    // unusable by the time `execl` runs.
    std::error_code path_error;
    std::filesystem::path absolute_program = std::filesystem::absolute(request.program, path_error);
    if (path_error) absolute_program = request.program;
    std::string program_string = absolute_program.string();
    std::string working_dir = absolute_program.parent_path().string();
    pid_t pid = fork();
    if (pid < 0) {
        for (int fd : {to_child[0], to_child[1], from_child[0], from_child[1]}) close(fd);
        return fail("process launch failed");
    }
    if (pid == 0) {
        dup2(to_child[0], STDIN_FILENO);
        dup2(from_child[1], STDOUT_FILENO);
        dup2(from_child[1], STDERR_FILENO);
        for (int fd : {to_child[0], to_child[1], from_child[0], from_child[1]})
            close(fd);
        if (!working_dir.empty()) {
            int changed_directory = chdir(working_dir.c_str());
            (void)changed_directory;
        }
        execl(program_string.c_str(), request.program.filename().c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }
    close(to_child[0]);
    close(from_child[1]);

    int turn_time_ms = external_turn_time_ms(request.version, request.level);
    std::string input = "START " + std::to_string(request.board_size) +
                        "\nINFO timeout_turn " + std::to_string(turn_time_ms) +
                        "\nINFO timeout_match 1800000\n";
    input += "INFO rule 4\n";
    if (request.max_depth_hint > 0)
        input += "INFO max_depth " + std::to_string(request.max_depth_hint) + "\n";
    input += "BOARD\n";
    for (size_t i = 0; i < request.moves.size(); ++i) {
        Move m = request.moves[i];
        int absolute_stone = static_cast<int>(i) % 2 == 0 ? 1 : 2;
        int stone = absolute_stone == request.player ? 1 : 2;
        input += std::to_string(m.c) + "," + std::to_string(m.r) + "," + std::to_string(stone) + "\n";
    }
    input += "DONE\n";
    auto cancelled = [&]() { return request.cancel && request.cancel->load(); };
    std::atomic<bool> write_done{false}, write_succeeded{false};
    std::thread writer([&]() {
        size_t sent = 0;
        while (sent < input.size()) {
            ssize_t count = write(to_child[1], input.data() + sent, input.size() - sent);
            if (count <= 0) break;
            sent += static_cast<size_t>(count);
        }
        write_succeeded = sent == input.size();
        write_done = true;
    });
    Move result{-1, -1};
    std::string output;
    auto deadline = steady_clock::now() +
                     std::chrono::milliseconds(std::max(30000, turn_time_ms + 15000));
    bool timed_out = false;
    bool illegal_move = false;
    auto consume_lines = [&](bool include_partial) {
        size_t line_end = 0;
        while ((line_end = output.find('\n')) != std::string::npos || (include_partial && !output.empty())) {
            std::string line = line_end == std::string::npos ? output : output.substr(0, line_end);
            if (line_end == std::string::npos) output.clear();
            else output.erase(0, line_end + 1);
            Move candidate;
            if (!parse_external_move_line(line, candidate)) continue;
            if (legal_external_move(request, candidate)) result = candidate;
            else illegal_move = true;
            break;
        }
    };
    while (result.r == -1) {
        if (cancelled()) break;
        if (steady_clock::now() >= deadline) { timed_out = true; break; }
        if (write_done.load() && !write_succeeded.load()) break;
        pollfd fds{from_child[0], POLLIN | POLLHUP, 0};
        int ready = poll(&fds, 1, 10);
        if (ready <= 0) continue;
        char buffer[4096];
        ssize_t count = read(from_child[0], buffer, sizeof(buffer));
        if (count <= 0) {
            consume_lines(true);
            break;
        }
        output.append(buffer, buffer + count);
        consume_lines(false);
        if (illegal_move) break;
    }
    bool was_cancelled = cancelled();
    if (result.r == -1 || !write_done.load()) kill(pid, SIGKILL);
    writer.join();
    if (result.r != -1) {
        const char end[] = "END\n";
        ssize_t end_written = write(to_child[1], end, sizeof(end) - 1);
        (void)end_written;
    }
    close(to_child[1]);
    close(from_child[0]);
    if (was_cancelled) {
        error.clear();
    } else if (result.r == -1) {
        error = !write_succeeded.load() ? "command write failed" :
                illegal_move ? "returned no legal move" :
                timed_out ? "did not answer in time" : "returned no move";
    }
    int status = 0;
    bool reaped = false;
    for (int attempt = 0; attempt < 100; ++attempt) {
        pid_t done = waitpid(pid, &status, WNOHANG);
        if (done == pid || done < 0) { reaped = true; break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!reaped) {
        kill(pid, SIGKILL);
        waitpid(pid, &status, 0);
    }
    return result;
}

#endif

// Current V3 neural/VCT hybrid. The bundled AlphaGomoku runtime supplies the
// trained 8x128 Renju policy, while Lumen remains responsible for immediate
// tactics, Renju legality and rejecting policy moves that hand over a proven
// VCF or one-reply VCF setup. If the model is unavailable, every surface falls
// back to the self-contained alpha-beta engine.
inline Move choose_current_neural_vct_move(
        int board[kMaxBoard][kMaxBoard], int sz, const std::vector<Move>& history,
        int player, int level, int max_threads, const std::filesystem::path& runtime_root,
        std::string& error, const std::atomic<bool>* cancel = nullptr,
        EngineVersion opponent_engine = EngineVersion::Count) {
    error.clear();
    if (cancel && cancel->load()) return {-1, -1};

    const int opponent = 3 - player;
    Move immediate = find_immediate_win(board, sz, player);
    if (immediate.r != -1) return immediate;
    Move mandatory_block = find_immediate_win(board, sz, opponent);
    if (mandatory_block.r != -1 &&
        !(player == 1 && is_forbidden_move(board, sz, mandatory_block.r, mandatory_block.c)))
        return mandatory_block;

    // Legacy's narrow fixed-depth style is the one matchup where the trained
    // policy over-generalizes. The original Lumen alpha-beta profile already
    // wins that pairing, so engine-match mode retains it as an explicit expert
    // in the ensemble. Human/unknown opponents still use the neural expert.
    if (opponent_engine == EngineVersion::LegacyV1) {
        OpponentStyle style = analyze_opponent_style(sz, history, opponent);
        Move last_own{-1, -1};
        for (int i = static_cast<int>(history.size()) - 1; i >= 0; --i) {
            if ((i % 2 == 0 ? 1 : 2) == player) {
                last_own = history[i];
                break;
            }
        }
        return choose_ai_move(board, sz, player, level, &style,
                              max_threads, last_own, cancel);
    }

    Move neural_move{-1, -1};
    const std::filesystem::path program = external_runtime_path(
        runtime_root, EngineVersion::AlphaGomoku2026);
    if ((sz == 15 || sz == 20) && !program.empty() && std::filesystem::exists(program)) {
        ExternalEngineRequest request;
        request.version = EngineVersion::AlphaGomoku2026;
        request.moves = history;
        request.player = player;
        // Level 3 gets the model's four-second policy budget. Process startup
        // makes measured wall time about 2.5-6.5 s, still close to bundled
        // engines and far below the one-minute limit.
        request.level = std::min(5, std::max(1, level) + 2);
        request.board_size = sz;
        request.program = program;
        request.cancel = cancel;
        neural_move = run_external_engine(request, error);
    }

    if (cancel && cancel->load()) return {-1, -1};
    if (is_legal_move(board, sz, player, neural_move)) {
        int local[kMaxBoard][kMaxBoard];
        std::memcpy(local, board, sizeof(local));
        local[neural_move.r][neural_move.c] = player;
        const auto audit_deadline = std::chrono::steady_clock::now() +
                                    std::chrono::milliseconds(350);
        const bool unsafe = has_forced_four_win(local, sz, opponent, 5, 320) ||
            opponent_has_unavoidable_vcf_setup(local, sz, opponent, audit_deadline, 12, 48);
        if (!unsafe) {
            error.clear();
            return neural_move;
        }
    }

    OpponentStyle style = analyze_opponent_style(sz, history, opponent);
    Move last_own{-1, -1};
    for (int i = static_cast<int>(history.size()) - 1; i >= 0; --i) {
        if ((i % 2 == 0 ? 1 : 2) == player) {
            last_own = history[i];
            break;
        }
    }
    // The neural process already spent most of the turn budget. A level-2
    // alpha-beta fallback still searches three regular plies and is capped at
    // two seconds.
    return choose_ai_move(board, sz, player, std::min(2, std::max(1, level)),
                          &style, max_threads, last_own, cancel);
}

}  // namespace gomoku
