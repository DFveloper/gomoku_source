#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
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
#include <sys/socket.h>
#include <unistd.h>
using Socket = int;
constexpr Socket kInvalidSocket = -1;
#endif

using namespace std;

namespace {

constexpr int kMaxBoard = gomoku::kMaxBoard;
constexpr int kMinBoard = 9;
constexpr int kDefaultBoard = 15;
constexpr int kAiDepth = 4;
constexpr int kMaxLevel = 5;
constexpr size_t kMaxRequest = 2 * 1024 * 1024;

struct MoveReview {
    int r = 0;
    int c = 0;
    int p = 0;
    double winrate = 50.0;
    string label = "Book";
};

struct Game {
    mutex lock;
    int size = kDefaultBoard;
    int board[kMaxBoard][kMaxBoard]{};
    bool heat = false;
    bool game_over = false;
    int winner = 0;
    vector<pair<int, int>> moves;
    vector<MoveReview> reviews;
    pair<int, int> last_move{-1, -1};
    gomoku::WinLine win_line{};
    bool review_mode = false;
    int review_ply = 0;
    bool ai_thinking = false;
    int ai_job = 0;
    shared_ptr<atomic<bool>> ai_cancel;
    int engine = static_cast<int>(gomoku::EngineVersion::CurrentV3);
    int level = kAiDepth;
    pair<int, int> forbidden{-1, -1};
    chrono::steady_clock::time_point forbidden_at{};
    unsigned long long revision = 1;
};

Game game;
atomic<bool> running{true};
atomic<int> active_ai_workers{0};
Socket listener_socket = kInvalidSocket;
filesystem::path web_root;
string public_origin;
int ai_threads = 0;
filesystem::path engine_runtime_root;

void close_socket(Socket s) {
#ifdef _WIN32
    closesocket(s);
#else
    close(s);
#endif
}

string json_escape(const string& value) {
    string out;
    out.reserve(value.size() + 8);
    for (unsigned char ch : value) {
        switch (ch) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (ch >= 0x20) out += static_cast<char>(ch);
        }
    }
    return out;
}

bool five_for(const int board[kMaxBoard][kMaxBoard], int size, int player) {
    return gomoku::has_five_for(board, size, player);
}

// Black's win chance with `mover` to move, from the shared Chess.com-style
// core model: forced fives and continuous-four chains pin 100/0, everything
// else runs through a logistic curve over a tempo-aware evaluation.
double black_winrate(int board[kMaxBoard][kMaxBoard], int size, int mover) {
    return gomoku::black_win_chance(board, size, mover);
}

double best_after(int board[kMaxBoard][kMaxBoard], int size, int player) {
    // Every considered reply hands the move to the opponent.
    const int responder = 3 - player;
    double best = player == 1 ? -1.0 : 101.0;
    auto candidates = gomoku::ranked_candidates(board, size, player, 10, nullptr);
    for (const auto& candidate : candidates) {
        auto move = candidate.second;
        board[move.r][move.c] = player;
        double wr = black_winrate(board, size, responder);
        board[move.r][move.c] = 0;
        best = player == 1 ? max(best, wr) : min(best, wr);
    }
    return best < 0.0 || best > 100.0 ? black_winrate(board, size, player) : best;
}

string classify_move(int player, double before, double after, double best,
                     bool had_win, bool made_win, int ply) {
    double own_delta = player == 1 ? after - before : before - after;
    double best_gap = player == 1 ? best - after : after - best;
    if (had_win && !made_win) return "Missed Win";
    if (made_win) return "Best";
    if (ply < 8 && best_gap < 6.0) return "Book";
    if (own_delta > 14.0 && best_gap < 4.0) return "Brilliant";
    if (own_delta > 8.0) return "Great";
    if (best_gap < 2.0) return "Best";
    if (best_gap < 5.0) return "Excellent";
    if (best_gap < 10.0) return "Good";
    if (best_gap < 18.0) return "Inaccuracy";
    if (best_gap < 32.0) return "Mistake";
    return "Blunder";
}

void compute_reviews_locked() {
    int replay[kMaxBoard][kMaxBoard]{};
    game.reviews.clear();
    for (size_t i = 0; i < game.moves.size(); ++i) {
        int player = i % 2 == 0 ? 1 : 2;
        int r = game.moves[i].first, c = game.moves[i].second;
        double before = black_winrate(replay, game.size, player);
        bool had_win = gomoku::has_forced_four_win(replay, game.size, player);
        double best = best_after(replay, game.size, player);
        replay[r][c] = player;
        bool made_win = five_for(replay, game.size, player);
        double after = black_winrate(replay, game.size, 3 - player);
        game.reviews.push_back({r, c, player, after,
            classify_move(player, before, after, best, had_win, made_win, static_cast<int>(i))});
    }
    game.review_ply = static_cast<int>(game.moves.size());
}

void cancel_pending_ai_locked() {
    ++game.ai_job;
    if (game.ai_cancel) game.ai_cancel->store(true);
    game.ai_thinking = false;
}

void reset_locked() {
    cancel_pending_ai_locked();
    memset(game.board, 0, sizeof(game.board));
    game.moves.clear();
    game.reviews.clear();
    game.last_move = {-1, -1};
    game.win_line = {};
    game.game_over = false;
    game.winner = 0;
    game.review_mode = false;
    game.review_ply = 0;
    game.forbidden = {-1, -1};
    ++game.revision;
}

gomoku::Move last_move_for_locked(int player) {
    for (int i = static_cast<int>(game.moves.size()) - 1; i >= 0; --i) {
        if ((i % 2 == 0 ? 1 : 2) == player) return {game.moves[i].first, game.moves[i].second};
    }
    return {-1, -1};
}

bool board_full_locked() {
    return game.moves.size() >= static_cast<size_t>(game.size * game.size);
}

void start_ai_locked() {
    if (game.ai_thinking || game.game_over || game.review_mode) return;
    array<int, kMaxBoard * kMaxBoard> snapshot{};
    for (int r = 0; r < kMaxBoard; ++r) for (int c = 0; c < kMaxBoard; ++c)
        snapshot[r * kMaxBoard + c] = game.board[r][c];
    vector<gomoku::Move> history;
    for (auto move : game.moves) history.push_back({move.first, move.second});
    int size = game.size;
    int ply = static_cast<int>(game.moves.size());
    int job = ++game.ai_job;
    gomoku::OpponentStyle style = gomoku::analyze_opponent_style(size, history, 1);
    gomoku::Move last_ai = last_move_for_locked(2);
    int engine = game.engine;
    int level = game.level;
    auto cancel = make_shared<atomic<bool>>(false);
    game.ai_cancel = cancel;
    game.ai_thinking = true;
    ++game.revision;

    ++active_ai_workers;
    thread([snapshot, size, ply, job, style, last_ai, engine, level, cancel, history]() {
        struct WorkerGuard {
            ~WorkerGuard() { --active_ai_workers; }
        } worker_guard;
        gomoku::Move move{-1, -1};
        int local_board[kMaxBoard][kMaxBoard]{};
        for (int r = 0; r < kMaxBoard; ++r) for (int c = 0; c < kMaxBoard; ++c)
            local_board[r][c] = snapshot[r * kMaxBoard + c];
        if (engine >= static_cast<int>(gomoku::EngineVersion::CarbonClassic)) {
            string error;
            gomoku::ExternalEngineRequest request;
            request.version = static_cast<gomoku::EngineVersion>(engine);
            request.moves = history;
            request.player = 2;
            request.level = level;
            request.board_size = size;
            request.program = gomoku::external_runtime_path(engine_runtime_root,
                                                            request.version);
            request.cancel = cancel.get();
            for (int attempt = 0; attempt < 2; ++attempt) {
                move = gomoku::run_external_engine(request, error);
                if (cancel->load()) return;
                if (gomoku::is_legal_move(local_board, size, 2, move)) break;
                cerr << "[webui] external engine attempt " << attempt + 1
                     << " failed (" << error << ")\n";
            }
        } else {
            move = gomoku::choose_versioned_move(local_board, size, 2, level,
                                                 static_cast<gomoku::EngineVersion>(engine),
                                                 &style, ai_threads, last_ai, cancel.get());
        }
        if (cancel->load()) return;
        if (!gomoku::is_legal_move(local_board, size, 2, move)) {
            // Never strand the human after a transient or malformed engine response.
            move = gomoku::choose_ai_move(local_board, size, 2, max(2, min(level, kAiDepth)),
                                          &style, ai_threads, last_ai, cancel.get());
            if (cancel->load()) return;
            if (!gomoku::is_legal_move(local_board, size, 2, move))
                move = gomoku::first_legal_move(local_board, size, 2);
        }

        lock_guard<mutex> guard(game.lock);
        if (job != game.ai_job || ply != static_cast<int>(game.moves.size()) ||
            game.game_over || game.review_mode) return;
        game.ai_thinking = false;
        if (gomoku::is_legal_move(game.board, game.size, 2, move)) {
            game.board[move.r][move.c] = 2;
            game.moves.push_back({move.r, move.c});
            game.last_move = {move.r, move.c};
            gomoku::WinLine line;
            if (gomoku::check_win_at(game.board, game.size, move.r, move.c, 2, &line)) {
                game.game_over = true;
                game.winner = 2;
                game.win_line = line;
                compute_reviews_locked();
                game.review_mode = true;
            } else if (board_full_locked()) {
                game.game_over = true;
                game.winner = 0;
                compute_reviews_locked();
                game.review_mode = true;
            }
        } else if (board_full_locked()) {
            game.game_over = true;
            game.winner = 0;
            compute_reviews_locked();
            game.review_mode = true;
        }
        ++game.revision;
    }).detach();
}

string status_locked() {
    if (game.review_mode) return "GAME REVIEW";
    if (game.game_over) return game.winner == 0 ? "DRAW" : game.winner == 1 ? "YOU WIN!" : "AI WINS!";
    if (game.ai_thinking) return "CALCULATING...";
    return "YOUR TURN";
}

int engine_count() { return static_cast<int>(gomoku::EngineVersion::Count); }

bool engine_supports_size(int engine, int size) {
    using gomoku::EngineVersion;
    switch (static_cast<EngineVersion>(engine)) {
        case EngineVersion::Rapfi2025: return size >= 5 && size <= 22;
        case EngineVersion::AlphaGomoku2026: return size == 15 || size == 20;
        case EngineVersion::XLNiren: return size == 15;
        default: return size >= kMinBoard && size <= kMaxBoard;
    }
}

bool engine_available(int engine) {
    if (engine < 0 || engine >= engine_count()) return false;
    if (!engine_supports_size(engine, game.size)) return false;
    if (engine < static_cast<int>(gomoku::EngineVersion::CarbonClassic)) return true;
    return filesystem::exists(gomoku::external_runtime_path(
        engine_runtime_root, static_cast<gomoku::EngineVersion>(engine)));
}

string state_json_locked() {
    ostringstream out;
    out << "{\"size\":" << game.size << ",\"board\":[";
    int shown_ply = game.review_mode ? game.review_ply : static_cast<int>(game.moves.size());
    int shown[kMaxBoard][kMaxBoard]{};
    for (int i = 0; i < shown_ply && i < static_cast<int>(game.moves.size()); ++i)
        shown[game.moves[i].first][game.moves[i].second] = i % 2 == 0 ? 1 : 2;
    for (int r = 0; r < game.size; ++r) {
        if (r) out << ',';
        out << '[';
        for (int c = 0; c < game.size; ++c) {
            if (c) out << ',';
            out << shown[r][c];
        }
        out << ']';
    }
    out << "],\"moves\":[";
    for (size_t i = 0; i < game.moves.size(); ++i) {
        if (i) out << ',';
        out << '[' << game.moves[i].first << ',' << game.moves[i].second << ']';
    }
    out << "],\"heat\":" << (game.heat ? "true" : "false")
        << ",\"heatmap\":[";
    if (game.heat && !game.review_mode) {
        bool first = true;
        for (int r = 0; r < game.size; ++r) for (int c = 0; c < game.size; ++c) {
            if (game.board[r][c] != 0) continue;
            if (!first) out << ',';
            first = false;
            int score = min(255, gomoku::get_thr(game.board, game.size, r, c, 2) / 2 +
                                 gomoku::get_thr(game.board, game.size, r, c, 1));
            out << '[' << r << ',' << c << ',' << score << ']';
        }
    }
    out << "],\"gameOver\":" << (game.game_over ? "true" : "false")
        << ",\"winner\":" << game.winner
        << ",\"engines\":[";
    for (int i = 0; i < engine_count(); ++i) {
        if (i) out << ',';
        out << "{\"id\":" << i << ",\"name\":\""
            << json_escape(gomoku::engine_version_name(static_cast<gomoku::EngineVersion>(i)))
            << "\",\"available\":" << (engine_available(i) ? "true" : "false") << '}';
    }
    out << "],\"engine\":" << game.engine
        << ",\"level\":" << game.level
        << ",\"maxLevel\":" << kMaxLevel
        << ",\"reviewMode\":" << (game.review_mode ? "true" : "false")
        << ",\"reviewPly\":" << game.review_ply
        << ",\"aiThinking\":" << (game.ai_thinking ? "true" : "false")
        << ",\"status\":\"" << json_escape(status_locked()) << "\""
        << ",\"revision\":" << game.revision;
    pair<int, int> last = game.last_move;
    if (game.review_mode && game.review_ply > 0 && game.review_ply <= static_cast<int>(game.moves.size()))
        last = game.moves[game.review_ply - 1];
    out << ",\"last\":[" << last.first << ',' << last.second << ']';
    bool show_win = game.win_line.start.r >= 0 && (!game.review_mode || game.review_ply == static_cast<int>(game.moves.size()));
    if (show_win) out << ",\"winLine\":[" << game.win_line.start.r << ',' << game.win_line.start.c << ','
                      << game.win_line.end.r << ',' << game.win_line.end.c << ']';
    else out << ",\"winLine\":null";
    double forbidden_age = game.forbidden.first < 0 ? 1000.0 :
        chrono::duration<double, milli>(chrono::steady_clock::now() - game.forbidden_at).count();
    if (forbidden_age < 700.0)
        out << ",\"forbidden\":[" << game.forbidden.first << ',' << game.forbidden.second << ']';
    else out << ",\"forbidden\":null";
    out << ",\"reviews\":[";
    for (size_t i = 0; i < game.reviews.size(); ++i) {
        if (i) out << ',';
        const auto& item = game.reviews[i];
        out << "{\"r\":" << item.r << ",\"c\":" << item.c << ",\"p\":" << item.p
            << ",\"winrate\":" << item.winrate << ",\"label\":\"" << json_escape(item.label) << "\"}";
    }
    out << "]}";
    return out.str();
}

string replay_locked() {
    ostringstream out;
    out << "GOMOKU_REPLAY_V1 " << game.size << ' ' << game.winner << " OFFLINE\n";
    for (auto move : game.moves) out << move.first << ' ' << move.second << '\n';
    return out.str();
}

string log_locked() {
    ostringstream out;
    int replay[kMaxBoard][kMaxBoard]{};
    out << "--- Gomoku review ---\n";
    for (size_t i = 0; i < game.moves.size(); ++i) {
        int r = game.moves[i].first, c = game.moves[i].second;
        replay[r][c] = i % 2 == 0 ? 1 : 2;
        out << "\n[" << i + 1 << "] " << (i % 2 == 0 ? "Human" : "AI") << " (" << r << ", " << c << ")\n";
        for (int row = 0; row < game.size; ++row) {
            for (int col = 0; col < game.size; ++col)
                out << (replay[row][col] == 1 ? 'X' : replay[row][col] == 2 ? 'O' : '.') << ' ';
            out << '\n';
        }
    }
    return out.str();
}

bool load_replay_locked(const string& text) {
    istringstream in(text);
    string magic, name;
    int size = 0, winner = 0;
    if (!(in >> magic >> size >> winner >> name) || magic != "GOMOKU_REPLAY_V1" ||
        size < kMinBoard || size > kMaxBoard || winner < 0 || winner > 2) return false;
    vector<pair<int, int>> loaded;
    int occupied[kMaxBoard][kMaxBoard]{};
    int r = 0, c = 0;
    while (in >> r >> c) {
        if (r < 0 || r >= size || c < 0 || c >= size || occupied[r][c]) return false;
        occupied[r][c] = 1;
        loaded.push_back({r, c});
        if (loaded.size() > static_cast<size_t>(size * size)) return false;
    }
    reset_locked();
    game.size = size;
    game.moves = move(loaded);
    for (size_t i = 0; i < game.moves.size(); ++i)
        game.board[game.moves[i].first][game.moves[i].second] = i % 2 == 0 ? 1 : 2;
    game.winner = winner;
    game.game_over = true;
    if (winner && !game.moves.empty()) {
        auto last = game.moves.back();
        gomoku::check_win_at(game.board, game.size, last.first, last.second,
                             game.moves.size() % 2 == 1 ? 1 : 2, &game.win_line);
    }
    compute_reviews_locked();
    game.review_mode = true;
    game.last_move = game.moves.empty() ? pair<int, int>{-1, -1} : game.moves.back();
    ++game.revision;
    return true;
}

int json_int(const string& body, const string& key, int fallback = -1) {
    string needle = "\"" + key + "\"";
    size_t pos = body.find(needle);
    if (pos == string::npos || (pos = body.find(':', pos + needle.size())) == string::npos) return fallback;
    try { return stoi(body.substr(pos + 1)); } catch (...) { return fallback; }
}

string json_string(const string& body, const string& key) {
    string needle = "\"" + key + "\"";
    size_t pos = body.find(needle);
    if (pos == string::npos || (pos = body.find(':', pos + needle.size())) == string::npos) return {};
    pos = body.find('"', pos + 1);
    if (pos == string::npos) return {};
    size_t end = body.find('"', pos + 1);
    return end == string::npos ? string{} : body.substr(pos + 1, end - pos - 1);
}

struct Request { string method, path, body; };

bool read_request(Socket socket, Request& request) {
    string data;
    array<char, 8192> buffer{};
    size_t header_end = string::npos, content_length = 0;
    while (data.size() < kMaxRequest) {
        int got = recv(socket, buffer.data(), static_cast<int>(buffer.size()), 0);
        if (got <= 0) return false;
        data.append(buffer.data(), got);
        header_end = data.find("\r\n\r\n");
        if (header_end != string::npos) {
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
        if (count <= 0) break;
        sent += static_cast<size_t>(count);
    }
}

void respond(Socket socket, int status, const string& type, const string& body,
             const string& extra = {}) {
    string reason = status == 200 ? "OK" : status == 204 ? "No Content" :
                    status == 400 ? "Bad Request" : status == 404 ? "Not Found" : "Error";
    ostringstream head;
    head << "HTTP/1.1 " << status << ' ' << reason << "\r\nContent-Type: " << type
         << "\r\nContent-Length: " << body.size()
         << "\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n"
         << extra << "Connection: close\r\n\r\n";
    send_all(socket, head.str());
    send_all(socket, body);
}

string mime_type(const filesystem::path& path) {
    string ext = path.extension().string();
    if (ext == ".html") return "text/html; charset=utf-8";
    if (ext == ".css") return "text/css; charset=utf-8";
    if (ext == ".js") return "text/javascript; charset=utf-8";
    if (ext == ".png") return "image/png";
    return "application/octet-stream";
}

bool serve_file(Socket socket, const string& path) {
    string name = path == "/" ? "index.html" : path.substr(1);
    if (name.find("..") != string::npos || name.find('\\') != string::npos) return false;
    filesystem::path file = web_root / name;
    ifstream input(file, ios::binary);
    if (!input) return false;
    string body((istreambuf_iterator<char>(input)), istreambuf_iterator<char>());
    if (name == "index.html") {
        const string marker = "__GOMOKU_ORIGIN__";
        size_t at = 0;
        while ((at = body.find(marker, at)) != string::npos) {
            body.replace(at, marker.size(), public_origin);
            at += public_origin.size();
        }
    }
    respond(socket, 200, mime_type(file), body);
    return true;
}

void save_named_file(const string& name, const string& body) {
    ofstream output(name, ios::binary);
    output << body;
}

void handle_connection(Socket socket) {
    Request request;
    if (!read_request(socket, request)) { close_socket(socket); return; }
    if (request.method == "GET" && request.path == "/api/state") {
        lock_guard<mutex> guard(game.lock);
        respond(socket, 200, "application/json; charset=utf-8", state_json_locked());
    } else if (request.method == "POST" && request.path == "/api/move") {
        int r = json_int(request.body, "r"), c = json_int(request.body, "c");
        lock_guard<mutex> guard(game.lock);
        if (game.ai_thinking || game.review_mode || game.game_over || r < 0 || r >= game.size ||
            c < 0 || c >= game.size || game.board[r][c] != 0) {
            respond(socket, 400, "application/json", "{\"error\":\"illegal move\"}");
        } else if (gomoku::is_forbidden_move(game.board, game.size, r, c)) {
            game.forbidden = {r, c};
            game.forbidden_at = chrono::steady_clock::now();
            ++game.revision;
            respond(socket, 400, "application/json", "{\"error\":\"forbidden move\"}");
        } else {
            game.forbidden = {-1, -1};
            game.board[r][c] = 1;
            game.moves.push_back({r, c});
            game.last_move = {r, c};
            gomoku::WinLine line;
            if (gomoku::check_win_at(game.board, game.size, r, c, 1, &line)) {
                game.game_over = true;
                game.winner = 1;
                game.win_line = line;
                compute_reviews_locked();
                game.review_mode = true;
            } else if (board_full_locked()) {
                game.game_over = true;
                game.winner = 0;
                compute_reviews_locked();
                game.review_mode = true;
            } else {
                start_ai_locked();
            }
            ++game.revision;
            respond(socket, 200, "application/json", state_json_locked());
        }
    } else if (request.method == "POST" && request.path == "/api/action") {
        string action = json_string(request.body, "action");
        lock_guard<mutex> guard(game.lock);
        bool valid = true;
        if (action == "new") reset_locked();
        else if (action == "undo") {
            if (!game.moves.empty() && !game.game_over) {
                cancel_pending_ai_locked();
                int count = min<int>(2, game.moves.size());
                while (count--) {
                    auto move = game.moves.back(); game.moves.pop_back();
                    game.board[move.first][move.second] = 0;
                }
                game.last_move = game.moves.empty() ? pair<int, int>{-1, -1} : game.moves.back();
                ++game.revision;
            }
        } else if (action == "heat") { game.heat = !game.heat; ++game.revision; }
        else if (action == "size") {
            if (game.moves.empty()) {
                game.size = game.size == 15 ? 19 : game.size == 19 ? 13 : 15;
                reset_locked();
                if (!engine_available(game.engine))
                    game.engine = static_cast<int>(gomoku::EngineVersion::CurrentV3);
            }
        } else if (action == "prev") game.review_ply = max(0, game.review_ply - 1);
        else if (action == "next") game.review_ply = min<int>(game.moves.size(), game.review_ply + 1);
        else if (action == "end") game.review_ply = static_cast<int>(game.moves.size());
        else valid = false;
        if (action == "prev" || action == "next" || action == "end") ++game.revision;
        respond(socket, valid ? 200 : 400, "application/json", valid ? state_json_locked() : "{\"error\":\"unknown action\"}");
    } else if (request.method == "POST" && request.path == "/api/config") {
        lock_guard<mutex> guard(game.lock);
        int engine = json_int(request.body, "engine", game.engine);
        int level = json_int(request.body, "level", game.level);
        if (!engine_available(engine) || level < 1 || level > kMaxLevel) {
            respond(socket, 400, "application/json", "{\"error\":\"invalid AI settings\"}");
        } else {
            bool restart = game.ai_thinking && !game.game_over && !game.review_mode;
            cancel_pending_ai_locked();
            game.engine = engine;
            game.level = level;
            // A mid-search switch re-asks the newly selected engine at once;
            // otherwise the choice simply applies from the next AI turn.
            if (restart) start_ai_locked();
            ++game.revision;
            respond(socket, 200, "application/json", state_json_locked());
        }
    } else if (request.method == "GET" && request.path == "/api/log") {
        lock_guard<mutex> guard(game.lock);
        string body = log_locked();
        save_named_file("gomoku_log.txt", body);
        respond(socket, 200, "text/plain; charset=utf-8", body,
                "Content-Disposition: attachment; filename=gomoku_log.txt\r\n");
    } else if (request.method == "GET" && request.path == "/api/replay") {
        lock_guard<mutex> guard(game.lock);
        string body = replay_locked();
        save_named_file("gomoku_replay.txt", body);
        respond(socket, 200, "text/plain; charset=utf-8", body,
                "Content-Disposition: attachment; filename=gomoku_replay.txt\r\n");
    } else if (request.method == "POST" && request.path == "/api/load") {
        lock_guard<mutex> guard(game.lock);
        if (!load_replay_locked(request.body)) respond(socket, 400, "application/json", "{\"error\":\"invalid replay\"}");
        else {
            save_named_file("gomoku_replay.txt", request.body);
            respond(socket, 200, "application/json", state_json_locked());
        }
    } else if (request.method == "POST" && request.path == "/api/quit") {
        respond(socket, 200, "application/json", "{\"ok\":true}");
        {
            lock_guard<mutex> guard(game.lock);
            cancel_pending_ai_locked();
        }
        running = false;
#ifdef _WIN32
        shutdown(listener_socket, SD_BOTH);
#else
        shutdown(listener_socket, SHUT_RDWR);
#endif
    } else if (request.method == "GET" && serve_file(socket, request.path)) {
        // Served above.
    } else {
        respond(socket, 404, "text/plain; charset=utf-8", "Not found\n");
    }
    close_socket(socket);
}

void print_usage(const char* exe) {
    cerr << "Usage: " << exe << " [--host ADDRESS] [--port PORT] [--board SIZE] [-t THREADS]\n"
         << "       " << exe << " --help\n";
}

bool parse_args(int argc, char** argv, string& host, int& port) {
    for (int i = 1; i < argc; ++i) {
        string arg = argv[i];
        if ((arg == "--host" || arg == "-H") && i + 1 < argc) host = argv[++i];
        else if ((arg == "--port" || arg == "-p") && i + 1 < argc) {
            try { port = stoi(argv[++i]); } catch (...) { return false; }
            if (port < 1 || port > 65535) return false;
        } else if ((arg == "--board" || arg == "-b") && i + 1 < argc) {
            try { game.size = stoi(argv[++i]); } catch (...) { return false; }
            if (game.size < kMinBoard || game.size > kMaxBoard) return false;
        } else if ((arg == "--threads" || arg == "-t") && i + 1 < argc) {
            try { ai_threads = stoi(argv[++i]); } catch (...) { return false; }
            if (ai_threads < 1 || ai_threads > gomoku::kMaxSearchThreads) return false;
        } else if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            exit(0);
        } else return false;
    }
    return true;
}

filesystem::path locate_web_root(const char* argv0) {
    vector<filesystem::path> candidates;
#ifdef GOMOKU_WEB_ROOT
    candidates.push_back(GOMOKU_WEB_ROOT);
#endif
    candidates.push_back(filesystem::current_path() / "webui");
    error_code ec;
    filesystem::path exe = filesystem::absolute(argv0, ec).parent_path();
    candidates.push_back(exe / "webui");
    candidates.push_back(exe / "../webui");
    for (const auto& path : candidates) if (filesystem::exists(path / "index.html")) return filesystem::canonical(path);
    return {};
}

void locate_engine_runtime_root(const char* argv0) {
    vector<filesystem::path> candidates;
#ifdef GOMOKU_SOURCE_ROOT
    candidates.push_back(filesystem::path(GOMOKU_SOURCE_ROOT) / "third_party" / "runtime");
#endif
    error_code ec;
    filesystem::path exe = filesystem::absolute(argv0, ec).parent_path();
    candidates.push_back(filesystem::current_path() / "third_party" / "runtime");
    candidates.push_back(exe / "third_party" / "runtime");
    candidates.push_back(exe / ".." / "third_party" / "runtime");
    for (const auto& path : candidates)
        if (filesystem::exists(path)) {
            engine_runtime_root = filesystem::canonical(path);
            return;
        }
}

} // namespace

int main(int argc, char** argv) {
    string host = "127.0.0.1";
    int port = 8080;
    if (!parse_args(argc, argv, host, port)) {
        print_usage(argv[0]);
        return 2;
    }
    web_root = locate_web_root(argv[0]);
    if (web_root.empty()) {
        cerr << "WebUI assets not found. Run from the project directory or rebuild with CMake.\n";
        return 1;
    }
    locate_engine_runtime_root(argv[0]);
    for (int i = static_cast<int>(gomoku::EngineVersion::CarbonClassic); i < engine_count(); ++i) {
        filesystem::path runtime = gomoku::external_runtime_path(
            engine_runtime_root, static_cast<gomoku::EngineVersion>(i));
        cout << "External engine "
             << gomoku::engine_version_name(static_cast<gomoku::EngineVersion>(i)) << ": "
             << (filesystem::exists(runtime) ? runtime.string() : string("not found")) << '\n';
    }
#ifdef _WIN32
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return 1;
#else
    signal(SIGPIPE, SIG_IGN);
#endif
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    addrinfo* addresses = nullptr;
    string port_text = to_string(port);
    int lookup = getaddrinfo(host == "*" ? nullptr : host.c_str(), port_text.c_str(), &hints, &addresses);
    if (lookup != 0) {
        cerr << "Cannot resolve host " << host << "\n";
        return 1;
    }
    for (addrinfo* address = addresses; address; address = address->ai_next) {
        listener_socket = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (listener_socket == kInvalidSocket) continue;
        int reuse = 1;
        setsockopt(listener_socket, SOL_SOCKET, SO_REUSEADDR,
                   reinterpret_cast<const char*>(&reuse), sizeof(reuse));
        if (bind(listener_socket, address->ai_addr, static_cast<int>(address->ai_addrlen)) == 0 &&
            listen(listener_socket, 16) == 0) break;
        close_socket(listener_socket);
        listener_socket = kInvalidSocket;
    }
    freeaddrinfo(addresses);
    if (listener_socket == kInvalidSocket) {
        cerr << "Cannot listen on " << host << ':' << port << "\n";
        return 1;
    }
    cout << "Gomoku WebUI: http://" << (host == "0.0.0.0" || host == "*" ? "127.0.0.1" : host)
         << ':' << port << "\nPress Q in the WebUI or Ctrl+C to stop.\n";
    public_origin = "http://" + (host == "0.0.0.0" || host == "*" ? string("127.0.0.1") : host) +
                    ":" + to_string(port);
    while (running) {
        Socket client = accept(listener_socket, nullptr, nullptr);
        if (client == kInvalidSocket) {
            if (!running) break;
            continue;
        }
        thread(handle_connection, client).detach();
    }
    close_socket(listener_socket);
    while (active_ai_workers.load() > 0) this_thread::sleep_for(chrono::milliseconds(10));
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
