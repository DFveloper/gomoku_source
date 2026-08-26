#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "gomoku_core.h"
#include "engine_adapter.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
using Socket = SOCKET;
constexpr Socket kInvalidSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
using Socket = int;
constexpr Socket kInvalidSocket = -1;
#endif

using namespace std;

namespace {

constexpr int kBoardSize = 15;
constexpr int kMaxBoard = gomoku::kMaxBoard;
constexpr size_t kMaxRequest = 1024 * 1024;

const array<const char*, 7> kEngineNames = {
    "Legacy v1", "Strengthened v2", "Current v3 MT", "Carbon Classic",
    "Rapfi 2025 AVX2", "AlphaGomoku MK26 CPU", "XL / Niren"
};

struct PlayedMove {
    int r = 0, c = 0, p = 0, elapsed_ms = 0;
    bool opening = false;
};

struct MatchState {
    mutex lock;
    int board[kMaxBoard][kMaxBoard]{};
    vector<PlayedMove> moves;
    int black_engine = 2;
    int white_engine = 1;
    int black_level = 2;
    int white_level = 2;
    string opening_mode = "random";
    int random_plies = 4;
    string phase = "setup";
    string message = "CONFIGURE THE MATCH";
    int winner = 0;
    int thinking_player = 0;
    gomoku::WinLine win_line{};
    int job = 0;
    shared_ptr<atomic<bool>> search_cancel;
    unsigned long long revision = 1;
};

MatchState match_state;
atomic<bool> server_running{true};
atomic<int> active_match_workers{0};
Socket listener_socket = kInvalidSocket;
filesystem::path match_web_root;
filesystem::path common_web_root;
filesystem::path engine_runtime_root;
string public_origin;
int search_threads = 0;

void close_socket(Socket socket) {
#ifdef _WIN32
    closesocket(socket);
#else
    close(socket);
#endif
}

int json_int(const string& body, const string& key, int fallback = -1) {
    size_t at = body.find("\"" + key + "\"");
    if (at == string::npos || (at = body.find(':', at)) == string::npos) return fallback;
    try { return stoi(body.substr(at + 1)); } catch (...) { return fallback; }
}

string json_string(const string& body, const string& key) {
    size_t at = body.find("\"" + key + "\"");
    if (at == string::npos || (at = body.find(':', at)) == string::npos ||
        (at = body.find('"', at)) == string::npos) return {};
    size_t end = body.find('"', at + 1);
    return end == string::npos ? string{} : body.substr(at + 1, end - at - 1);
}

// Relative paths (inside third_party/runtime) of the native engine ports.
// The Windows GUI embeds PE runtimes as resources; on POSIX the engines are
// built from third_party/*-src into these directories instead.
const array<pair<int, const char*>, 4> kExternalEngines = {{
    {static_cast<int>(gomoku::EngineVersion::CarbonClassic), "carbon/pbrain-carbon"},
    {static_cast<int>(gomoku::EngineVersion::Rapfi2025), "rapfi/pbrain-rapfi"},
    {static_cast<int>(gomoku::EngineVersion::AlphaGomoku2026), "alphagomoku/pbrain-agomoku"},
    {static_cast<int>(gomoku::EngineVersion::XLNiren), "xl/pbrain-xl"},
}};

filesystem::path external_engine_path(int engine) {
    for (const auto& [id, relative] : kExternalEngines) {
        if (id != engine) continue;
        filesystem::path path = engine_runtime_root / relative;
#ifdef _WIN32
        if (!filesystem::exists(path)) path += ".exe";
#endif
        return path;
    }
    return {};
}

bool engine_available(int engine) {
    if (engine < 0 || engine > 2) {
#ifndef _WIN32
        return filesystem::exists(external_engine_path(engine));
#else
        return false;
#endif
    }
    return true;
}

string state_json_locked() {
    ostringstream out;
    out << "{\"size\":" << kBoardSize << ",\"board\":[";
    for (int r = 0; r < kBoardSize; ++r) {
        if (r) out << ',';
        out << '[';
        for (int c = 0; c < kBoardSize; ++c) {
            if (c) out << ',';
            out << match_state.board[r][c];
        }
        out << ']';
    }
    out << "],\"moves\":[";
    for (size_t i = 0; i < match_state.moves.size(); ++i) {
        if (i) out << ',';
        const auto& move = match_state.moves[i];
        out << "{\"r\":" << move.r << ",\"c\":" << move.c << ",\"p\":" << move.p
            << ",\"ms\":" << move.elapsed_ms << ",\"opening\":" << (move.opening ? "true" : "false") << '}';
    }
    out << "],\"engines\":[";
    for (int i = 0; i < static_cast<int>(kEngineNames.size()); ++i) {
        if (i) out << ',';
        out << "{\"id\":" << i << ",\"name\":\"" << kEngineNames[i]
            << "\",\"available\":" << (engine_available(i) ? "true" : "false") << '}';
    }
    out << "],\"blackEngine\":" << match_state.black_engine
        << ",\"whiteEngine\":" << match_state.white_engine
        << ",\"blackLevel\":" << match_state.black_level
        << ",\"whiteLevel\":" << match_state.white_level
        << ",\"openingMode\":\"" << match_state.opening_mode << "\""
        << ",\"randomPlies\":" << match_state.random_plies
        << ",\"phase\":\"" << match_state.phase << "\""
        << ",\"message\":\"" << match_state.message << "\""
        << ",\"winner\":" << match_state.winner
        << ",\"thinkingPlayer\":" << match_state.thinking_player
        << ",\"threads\":" << search_threads
        << ",\"revision\":" << match_state.revision;
    if (match_state.moves.empty()) out << ",\"last\":null";
    else {
        const auto& move = match_state.moves.back();
        out << ",\"last\":[" << move.r << ',' << move.c << ']';
    }
    if (match_state.win_line.start.r < 0) out << ",\"winLine\":null";
    else out << ",\"winLine\":[" << match_state.win_line.start.r << ',' << match_state.win_line.start.c
             << ',' << match_state.win_line.end.r << ',' << match_state.win_line.end.c << ']';
    out << '}';
    return out.str();
}

void clear_board_locked() {
    ++match_state.job;
    if (match_state.search_cancel) match_state.search_cancel->store(true);
    match_state.search_cancel.reset();
    memset(match_state.board, 0, sizeof(match_state.board));
    match_state.moves.clear();
    match_state.phase = "setup";
    match_state.message = "CONFIGURE THE MATCH";
    match_state.winner = 0;
    match_state.thinking_player = 0;
    match_state.win_line = {};
    ++match_state.revision;
}

vector<gomoku::Move> history_locked() {
    vector<gomoku::Move> history;
    history.reserve(match_state.moves.size());
    for (const auto& move : match_state.moves) history.push_back({move.r, move.c});
    return history;
}

gomoku::Move last_for_locked(int player) {
    for (int i = static_cast<int>(match_state.moves.size()) - 1; i >= 0; --i)
        if (match_state.moves[i].p == player) return {match_state.moves[i].r, match_state.moves[i].c};
    return {-1, -1};
}

bool board_full_locked() {
    for (int r = 0; r < kBoardSize; ++r) for (int c = 0; c < kBoardSize; ++c)
        if (match_state.board[r][c] == 0) return false;
    return true;
}

int position_winner_locked(gomoku::WinLine* line = nullptr) {
    for (auto it = match_state.moves.rbegin(); it != match_state.moves.rend(); ++it) {
        gomoku::WinLine found;
        if (gomoku::check_win_at(match_state.board, kBoardSize, it->r, it->c, it->p, &found)) {
            if (line) *line = found;
            return it->p;
        }
    }
    return 0;
}

void finish_locked(int winner, const string& message) {
    match_state.phase = "finished";
    match_state.winner = winner;
    match_state.thinking_player = 0;
    match_state.message = message;
    ++match_state.revision;
}

void make_random_opening_locked() {
    mt19937 random(static_cast<unsigned int>(chrono::steady_clock::now().time_since_epoch().count()));
    for (int ply = 0; ply < match_state.random_plies; ++ply) {
        int player = ply % 2 == 0 ? 1 : 2;
        auto candidates = gomoku::generate_candidates(match_state.board, kBoardSize, 2);
        candidates.erase(remove_if(candidates.begin(), candidates.end(), [&](gomoku::Move move) {
            return player == 1 && gomoku::is_forbidden_move(match_state.board, kBoardSize, move.r, move.c);
        }), candidates.end());
        if (candidates.empty()) break;
        shuffle(candidates.begin(), candidates.end(), random);
        gomoku::Move move = candidates.front();
        match_state.board[move.r][move.c] = player;
        match_state.moves.push_back({move.r, move.c, player, 0, true});
        if (gomoku::check_win_at(match_state.board, kBoardSize, move.r, move.c, player)) break;
    }
}

void run_match(int job) {
    struct WorkerGuard {
        ~WorkerGuard() { --active_match_workers; }
    } worker_guard;
    while (true) {
        int snapshot[kMaxBoard][kMaxBoard]{};
        vector<gomoku::Move> history;
        int player = 0, engine = 0, depth = 0;
        gomoku::OpponentStyle style;
        gomoku::Move last_own;
        shared_ptr<atomic<bool>> cancel;
        {
            lock_guard<mutex> guard(match_state.lock);
            if (job != match_state.job || match_state.phase != "running") return;
            player = match_state.moves.size() % 2 == 0 ? 1 : 2;
            engine = player == 1 ? match_state.black_engine : match_state.white_engine;
            depth = player == 1 ? match_state.black_level : match_state.white_level;
            memcpy(snapshot, match_state.board, sizeof(snapshot));
            history = history_locked();
            style = gomoku::analyze_opponent_style(kBoardSize, history, 3 - player);
            last_own = last_for_locked(player);
            cancel = make_shared<atomic<bool>>(false);
            match_state.search_cancel = cancel;
            match_state.thinking_player = player;
            match_state.message = string(player == 1 ? "BLACK" : "WHITE") + " CALCULATING";
            ++match_state.revision;
        }

        auto started = chrono::steady_clock::now();
        gomoku::Move move{-1, -1};
        string engine_error;
        bool recovered = false;
        if (engine >= static_cast<int>(gomoku::EngineVersion::CarbonClassic)) {
            gomoku::ExternalEngineRequest request;
            request.version = static_cast<gomoku::EngineVersion>(engine);
            request.moves = history;
            request.player = player;
            request.level = depth;
            request.board_size = kBoardSize;
            request.program = external_engine_path(engine);
            request.cancel = cancel.get();
            for (int attempt = 0; attempt < 2; ++attempt) {
                move = gomoku::run_external_engine(request, engine_error);
                if (cancel->load()) return;
                if (gomoku::is_legal_move(snapshot, kBoardSize, player, move)) break;
                if (attempt == 0) {
                    lock_guard<mutex> guard(match_state.lock);
                    if (job != match_state.job || match_state.phase != "running") return;
                    match_state.message = string(player == 1 ? "BLACK" : "WHITE") +
                                          " RETRYING " + kEngineNames[engine];
                    ++match_state.revision;
                }
            }
        } else {
            move = gomoku::choose_versioned_move(
                snapshot, kBoardSize, player, depth, static_cast<gomoku::EngineVersion>(engine),
                &style, search_threads, last_own, cancel.get());
        }
        if (cancel->load()) return;
        if (!gomoku::is_legal_move(snapshot, kBoardSize, player, move)) {
            recovered = true;
            {
                lock_guard<mutex> guard(match_state.lock);
                if (job != match_state.job || match_state.phase != "running") return;
                match_state.message = string(player == 1 ? "BLACK" : "WHITE") + " USING SAFE FALLBACK";
                ++match_state.revision;
            }
            move = gomoku::choose_ai_move(snapshot, kBoardSize, player,
                                          max(2, min(depth, 3)), &style, search_threads, last_own,
                                          cancel.get());
            if (cancel->load()) return;
            if (!gomoku::is_legal_move(snapshot, kBoardSize, player, move))
                move = gomoku::first_legal_move(snapshot, kBoardSize, player);
        }
        int elapsed = static_cast<int>(chrono::duration_cast<chrono::milliseconds>(
            chrono::steady_clock::now() - started).count());

        {
            lock_guard<mutex> guard(match_state.lock);
            if (job != match_state.job || match_state.phase != "running") return;
            if (!gomoku::is_legal_move(match_state.board, kBoardSize, player, move)) {
                finish_locked(0, board_full_locked() ? "DRAW" : "DRAW · NO LEGAL MOVES");
                return;
            }
            match_state.board[move.r][move.c] = player;
            match_state.moves.push_back({move.r, move.c, player, elapsed, false});
            gomoku::WinLine line;
            if (gomoku::check_win_at(match_state.board, kBoardSize, move.r, move.c, player, &line)) {
                match_state.win_line = line;
                finish_locked(player, string(player == 1 ? "BLACK" : "WHITE") + " WINS");
                return;
            }
            if (board_full_locked()) {
                finish_locked(0, "DRAW");
                return;
            }
            match_state.thinking_player = 0;
            match_state.message = "MOVE " + to_string(match_state.moves.size()) +
                                  (recovered ? " · ENGINE RECOVERED" : "");
            ++match_state.revision;
        }
        this_thread::sleep_for(chrono::milliseconds(240));
    }
}

struct Request { string method, path, body; };

bool read_request(Socket socket, Request& request) {
    string data;
    array<char, 8192> buffer{};
    size_t header_end = string::npos, content_length = 0;
    while (data.size() < kMaxRequest) {
        int count = recv(socket, buffer.data(), static_cast<int>(buffer.size()), 0);
        if (count <= 0) return false;
        data.append(buffer.data(), count);
        header_end = data.find("\r\n\r\n");
        if (header_end == string::npos) continue;
        string headers = data.substr(0, header_end);
        size_t at = headers.find("Content-Length:");
        if (at == string::npos) at = headers.find("content-length:");
        if (at != string::npos) {
            at = headers.find(':', at);
            try { content_length = stoul(headers.substr(at + 1)); } catch (...) { return false; }
        }
        if (content_length > kMaxRequest || data.size() - header_end - 4 < content_length) continue;
        break;
    }
    if (header_end == string::npos) return false;
    istringstream first(data.substr(0, data.find("\r\n")));
    string version;
    if (!(first >> request.method >> request.path >> version)) return false;
    size_t query = request.path.find('?');
    if (query != string::npos) request.path.resize(query);
    request.body = data.substr(header_end + 4, content_length);
    return true;
}

void send_all(Socket socket, const string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
        int count = send(socket, data.data() + sent, static_cast<int>(data.size() - sent), 0);
        if (count <= 0) return;
        sent += static_cast<size_t>(count);
    }
}

void respond(Socket socket, int status, const string& type, const string& body) {
    const char* reason = status == 200 ? "OK" : status == 400 ? "Bad Request" : "Not Found";
    ostringstream head;
    head << "HTTP/1.1 " << status << ' ' << reason << "\r\nContent-Type: " << type
         << "\r\nContent-Length: " << body.size()
         << "\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nConnection: close\r\n\r\n";
    send_all(socket, head.str());
    send_all(socket, body);
}

string mime_type(const filesystem::path& path) {
    string extension = path.extension().string();
    if (extension == ".html") return "text/html; charset=utf-8";
    if (extension == ".css") return "text/css; charset=utf-8";
    if (extension == ".js") return "text/javascript; charset=utf-8";
    if (extension == ".png") return "image/png";
    return "application/octet-stream";
}

bool serve_file(Socket socket, const string& request_path) {
    string name = request_path == "/" ? "index.html" : request_path.substr(1);
    if (name.find("..") != string::npos || name.find('\\') != string::npos) return false;
    filesystem::path path = name == "og.png" ? common_web_root / name : match_web_root / name;
    ifstream input(path, ios::binary);
    if (!input) return false;
    string body((istreambuf_iterator<char>(input)), istreambuf_iterator<char>());
    if (name == "index.html") {
        string marker = "__GOMOKU_ORIGIN__";
        size_t at = 0;
        while ((at = body.find(marker, at)) != string::npos) {
            body.replace(at, marker.size(), public_origin);
            at += public_origin.size();
        }
    }
    respond(socket, 200, mime_type(path), body);
    return true;
}

void handle_connection(Socket socket) {
    Request request;
    if (!read_request(socket, request)) { close_socket(socket); return; }
    if (request.method == "GET" && request.path == "/api/match/state") {
        lock_guard<mutex> guard(match_state.lock);
        respond(socket, 200, "application/json", state_json_locked());
    } else if (request.method == "POST" && request.path == "/api/match/config") {
        lock_guard<mutex> guard(match_state.lock);
        if (match_state.phase != "setup") {
            respond(socket, 400, "application/json", "{\"error\":\"settings are locked\"}");
        } else {
            int black = json_int(request.body, "blackEngine", match_state.black_engine);
            int white = json_int(request.body, "whiteEngine", match_state.white_engine);
            int black_level = json_int(request.body, "blackLevel", match_state.black_level);
            int white_level = json_int(request.body, "whiteLevel", match_state.white_level);
            int plies = json_int(request.body, "randomPlies", match_state.random_plies);
            string mode = json_string(request.body, "openingMode");
            if (!engine_available(black) || !engine_available(white) || black_level < 1 || black_level > 5 ||
                white_level < 1 || white_level > 5 || plies < 0 || plies > 12 ||
                (mode != "random" && mode != "custom")) {
                respond(socket, 400, "application/json", "{\"error\":\"invalid match settings\"}");
            } else {
                if (mode != match_state.opening_mode) clear_board_locked();
                match_state.black_engine = black;
                match_state.white_engine = white;
                match_state.black_level = black_level;
                match_state.white_level = white_level;
                match_state.random_plies = plies;
                match_state.opening_mode = mode;
                ++match_state.revision;
                respond(socket, 200, "application/json", state_json_locked());
            }
        }
    } else if (request.method == "POST" && request.path == "/api/match/opening") {
        int r = json_int(request.body, "r"), c = json_int(request.body, "c");
        lock_guard<mutex> guard(match_state.lock);
        int player = match_state.moves.size() % 2 == 0 ? 1 : 2;
        if (match_state.phase != "setup" || match_state.opening_mode != "custom" || r < 0 || c < 0 ||
            r >= kBoardSize || c >= kBoardSize || match_state.board[r][c] ||
            position_winner_locked() != 0 ||
            (player == 1 && gomoku::is_forbidden_move(match_state.board, kBoardSize, r, c))) {
            respond(socket, 400, "application/json", "{\"error\":\"illegal opening move\"}");
        } else {
            match_state.board[r][c] = player;
            match_state.moves.push_back({r, c, player, 0, true});
            bool terminal = gomoku::check_win_at(match_state.board, kBoardSize, r, c, player);
            match_state.message = terminal ? "CUSTOM OPENING COMPLETE" :
                                  "CUSTOM OPENING · " + to_string(match_state.moves.size()) + " PLIES";
            ++match_state.revision;
            respond(socket, 200, "application/json", state_json_locked());
        }
    } else if (request.method == "POST" && request.path == "/api/match/undo-opening") {
        lock_guard<mutex> guard(match_state.lock);
        if (match_state.phase == "setup" && match_state.opening_mode == "custom" && !match_state.moves.empty()) {
            auto move = match_state.moves.back();
            match_state.moves.pop_back();
            match_state.board[move.r][move.c] = 0;
            match_state.message = match_state.moves.empty() ? "BUILD A CUSTOM OPENING" :
                                  "CUSTOM OPENING · " + to_string(match_state.moves.size()) + " PLIES";
            ++match_state.revision;
        }
        respond(socket, 200, "application/json", state_json_locked());
    } else if (request.method == "POST" && request.path == "/api/match/start") {
        lock_guard<mutex> guard(match_state.lock);
        if (match_state.phase != "setup") {
            respond(socket, 400, "application/json", "{\"error\":\"match already started\"}");
        } else {
            if (match_state.opening_mode == "random") {
                memset(match_state.board, 0, sizeof(match_state.board));
                match_state.moves.clear();
                make_random_opening_locked();
            }
            match_state.winner = 0;
            match_state.win_line = {};
            int job = ++match_state.job;
            gomoku::WinLine opening_line;
            int opening_winner = position_winner_locked(&opening_line);
            if (opening_winner) {
                match_state.win_line = opening_line;
                finish_locked(opening_winner,
                              string(opening_winner == 1 ? "BLACK" : "WHITE") + " WINS IN OPENING");
            } else if (board_full_locked()) {
                finish_locked(0, "DRAW · OPENING FILLED THE BOARD");
            } else {
                match_state.phase = "running";
                match_state.message = "MATCH STARTED";
                ++match_state.revision;
                ++active_match_workers;
                thread(run_match, job).detach();
            }
            respond(socket, 200, "application/json", state_json_locked());
        }
    } else if (request.method == "POST" && request.path == "/api/match/reset") {
        lock_guard<mutex> guard(match_state.lock);
        clear_board_locked();
        respond(socket, 200, "application/json", state_json_locked());
    } else if (request.method == "POST" && request.path == "/api/match/quit") {
        respond(socket, 200, "application/json", "{\"ok\":true}");
        {
            lock_guard<mutex> guard(match_state.lock);
            ++match_state.job;
            if (match_state.search_cancel) match_state.search_cancel->store(true);
            match_state.phase = "stopped";
        }
        server_running = false;
#ifdef _WIN32
        shutdown(listener_socket, SD_BOTH);
#else
        shutdown(listener_socket, SHUT_RDWR);
#endif
    } else if (request.method == "GET" && serve_file(socket, request.path)) {
    } else {
        respond(socket, 404, "text/plain; charset=utf-8", "Not found\n");
    }
    close_socket(socket);
}

void usage(const char* executable) {
    cerr << "Usage: " << executable << " [--host ADDRESS] [--port PORT] [-t THREADS]\n";
}

bool parse_args(int argc, char** argv, string& host, int& port) {
    for (int i = 1; i < argc; ++i) {
        string argument = argv[i];
        if ((argument == "--host" || argument == "-H") && i + 1 < argc) host = argv[++i];
        else if ((argument == "--port" || argument == "-p") && i + 1 < argc) {
            try { port = stoi(argv[++i]); } catch (...) { return false; }
            if (port < 1 || port > 65535) return false;
        } else if ((argument == "--threads" || argument == "-t") && i + 1 < argc) {
            try { search_threads = stoi(argv[++i]); } catch (...) { return false; }
            if (search_threads < 1 || search_threads > gomoku::kMaxSearchThreads) return false;
        } else if (argument == "--help" || argument == "-h") { usage(argv[0]); exit(0); }
        else return false;
    }
    return true;
}

filesystem::path locate_root(const char* argv0, const string& folder) {
    vector<filesystem::path> candidates;
#ifdef GOMOKU_SOURCE_ROOT
    candidates.push_back(filesystem::path(GOMOKU_SOURCE_ROOT) / folder);
#endif
    candidates.push_back(filesystem::current_path() / folder);
    error_code error;
    filesystem::path executable = filesystem::absolute(argv0, error).parent_path();
    candidates.push_back(executable / folder);
    candidates.push_back(executable / ".." / folder);
    for (const auto& candidate : candidates)
        if (filesystem::exists(candidate)) return filesystem::canonical(candidate);
    return {};
}

} // namespace

int main(int argc, char** argv) {
    string host = "127.0.0.1";
    int port = 8081;
    if (!parse_args(argc, argv, host, port)) { usage(argv[0]); return 2; }
    match_web_root = locate_root(argv[0], "match_webui");
    common_web_root = locate_root(argv[0], "webui");
    if (match_web_root.empty() || common_web_root.empty()) {
        cerr << "Model Match WebUI assets not found.\n";
        return 1;
    }
    filesystem::path third_party_root = locate_root(argv[0], "third_party");
    if (!third_party_root.empty()) engine_runtime_root = third_party_root / "runtime";
#ifdef _WIN32
    WSADATA socket_data{};
    if (WSAStartup(MAKEWORD(2, 2), &socket_data) != 0) return 1;
#else
    signal(SIGPIPE, SIG_IGN);
#endif
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    addrinfo* addresses = nullptr;
    string port_string = to_string(port);
    if (getaddrinfo(host == "*" ? nullptr : host.c_str(), port_string.c_str(), &hints, &addresses) != 0) {
        cerr << "Cannot resolve host " << host << '\n';
        return 1;
    }
    for (addrinfo* address = addresses; address; address = address->ai_next) {
        listener_socket = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (listener_socket == kInvalidSocket) continue;
        int reuse = 1;
        setsockopt(listener_socket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
        if (bind(listener_socket, address->ai_addr, static_cast<int>(address->ai_addrlen)) == 0 &&
            listen(listener_socket, 16) == 0) break;
        close_socket(listener_socket);
        listener_socket = kInvalidSocket;
    }
    freeaddrinfo(addresses);
    if (listener_socket == kInvalidSocket) {
        cerr << "Cannot listen on " << host << ':' << port << '\n';
        return 1;
    }
    string display_host = host == "0.0.0.0" || host == "*" ? "127.0.0.1" : host;
    public_origin = "http://" + display_host + ':' + to_string(port);
    cout << "Gomoku Model Match WebUI: " << public_origin << "\nSearch threads: "
         << (search_threads > 0 ? to_string(search_threads) : string("auto")) << '\n';
    for (int i = 3; i < static_cast<int>(kEngineNames.size()); ++i)
        cout << "External engine " << kEngineNames[i] << ": "
             << (engine_available(i) ? external_engine_path(i).string() : string("not found"))
             << '\n';
    cout << "Press Q in the WebUI or Ctrl+C to stop.\n";
    while (server_running) {
        Socket client = accept(listener_socket, nullptr, nullptr);
        if (client == kInvalidSocket) { if (!server_running) break; else continue; }
        thread(handle_connection, client).detach();
    }
    close_socket(listener_socket);
    while (active_match_workers.load() > 0) this_thread::sleep_for(chrono::milliseconds(10));
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
