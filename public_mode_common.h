#pragma once

#include <cctype>
#include <cstdlib>
#include <string>
#include <vector>

#include "gomoku_core.h"

namespace gomoku_public {

inline bool parse_move_pairs(const std::string& json, std::vector<gomoku::Move>& moves) {
    const std::string key = "\"moves\"";
    std::size_t pos = json.find(key);
    if (pos == std::string::npos || (pos = json.find('[', pos + key.size())) == std::string::npos)
        return false;
    ++pos;
    while (pos < json.size()) {
        while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) ++pos;
        if (pos >= json.size()) return false;
        if (json[pos] == ']') return true;
        if (json[pos] == ',') { ++pos; continue; }
        if (json[pos] != '[') return false;
        ++pos;
        char* end = nullptr;
        long row = std::strtol(json.c_str() + pos, &end, 10);
        if (end == json.c_str() + pos) return false;
        pos = static_cast<std::size_t>(end - json.c_str());
        while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) ++pos;
        if (pos >= json.size() || json[pos++] != ',') return false;
        long col = std::strtol(json.c_str() + pos, &end, 10);
        if (end == json.c_str() + pos) return false;
        pos = static_cast<std::size_t>(end - json.c_str());
        while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) ++pos;
        if (pos >= json.size() || json[pos++] != ']') return false;
        moves.push_back({static_cast<int>(row), static_cast<int>(col)});
    }
    return false;
}

template <std::size_t Rows, std::size_t Cols>
inline bool rebuild_board(const std::vector<gomoku::Move>& moves, int size,
                          int (&board)[Rows][Cols], bool enforce_renju = true) {
    static_assert(Rows >= static_cast<std::size_t>(gomoku::kMaxBoard));
    static_assert(Cols >= static_cast<std::size_t>(gomoku::kMaxBoard));
    if (size < 1 || size > gomoku::kMaxBoard || moves.size() > static_cast<std::size_t>(size * size))
        return false;
    for (std::size_t index = 0; index < moves.size(); ++index) {
        const auto move = moves[index];
        const int player = index % 2 == 0 ? 1 : 2;
        if (move.r < 0 || move.r >= size || move.c < 0 || move.c >= size || board[move.r][move.c] != 0)
            return false;
        if (enforce_renju && player == 1 && gomoku::is_forbidden_move(board, size, move.r, move.c))
            return false;
        board[move.r][move.c] = player;
        if (index + 1 < moves.size() && gomoku::check_win_at(board, size, move.r, move.c, player))
            return false;
    }
    return true;
}

inline gomoku::Move last_for(const std::vector<gomoku::Move>& moves, int player) {
    for (int index = static_cast<int>(moves.size()) - 1; index >= 0; --index)
        if ((index % 2 == 0 ? 1 : 2) == player) return moves[static_cast<std::size_t>(index)];
    return {-1, -1};
}

inline std::string line_json(const gomoku::WinLine& line) {
    if (line.start.r < 0) return "null";
    return "[" + std::to_string(line.start.r) + "," + std::to_string(line.start.c) + "," +
           std::to_string(line.end.r) + "," + std::to_string(line.end.c) + "]";
}

} // namespace gomoku_public
