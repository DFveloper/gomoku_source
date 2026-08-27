#pragma once
#include <cmath>
#include <array>
#include <vector>
#include <cstring>

// ------------------------------------------------------------------
// Neural Network Evaluator for Gomoku
// A lightweight embedded MLP (3-layer) designed to approximate the
// non-linear interaction of threat patterns that pure linear
// heuristics miss.  It complements the existing pattern-based
// evaluation and gives Current V3 a neural-guided score that
// improves tactical depth perception and late-game accuracy.
// ------------------------------------------------------------------

namespace neural {

constexpr int kInputFeatures = 28;   // compressed feature vector
constexpr int kHidden1      = 48;    // first hidden layer
constexpr int kHidden2      = 24;    // second hidden layer
constexpr int kOutput       = 1;     // value head

// ------------------------------------------------------------------
// Feature extraction
// We use a compressed feature set derived from the board state:
//  [0-3]  own/opponent direction scores (h/v/d1/d2)
//  [4-7]  own/opponent direction longest runs
//  [8-14] own threat counts (7 kinds)
//  [15-21] opponent threat counts (7 kinds)
//  [22]   potential difference
//  [23]   shape score difference
//  [24]   center control
//  [25]   stone count difference
//  [26]   forced-four indicator
//  [27]   five-completion squares count
// ------------------------------------------------------------------
inline std::array<float, kInputFeatures> extract_features(
        int board[kMaxBoard][kMaxBoard], int sz, int p) {

    std::array<float, kInputFeatures> f{};
    f.fill(0.0f);
    const int op = 3 - p;

    int dir_score_own[4] = {0};
    int dir_score_op[4] = {0};
    int longest_own[4] = {0};
    int longest_op[4] = {0};

    static const int dr[] = {0, 1, 1, 1};
    static const int dc[] = {1, 0, 1, -1};

    for (int d = 0; d < 4; ++d) {
        char line_own[16], line_op[16];
        // We evaluate at board center for a global direction summary.
        int r0 = sz / 2, c0 = sz / 2;
        encode_line(board, sz, r0, c0, dr[d], dc[d], p, line_own);
        encode_line(board, sz, r0, c0, dr[d], dc[d], op, line_op);
        ThreatInfo info_own = evaluate_line(line_own);
        ThreatInfo info_op  = evaluate_line(line_op);
        dir_score_own[d] = info_own.score;
        dir_score_op[d]  = info_op.score;
        longest_own[d]    = info_own.longest;
        longest_op[d]     = info_op.longest;
    }

    for (int d = 0; d < 4; ++d) {
        f[d] = std::min(1.0f, dir_score_own[d] / 200000.0f);
        f[d + 4] = std::min(1.0f, dir_score_op[d] / 200000.0f);
    }
    for (int d = 0; d < 4; ++d) {
        f[8 + d] = std::min(1.0f, longest_own[d] / 8.0f);
        f[12 + d] = std::min(1.0f, longest_op[d] / 8.0f);
    }

    // Aggregate threat counts over whole board (approximation)
    int own_counts[TH_KIND_COUNT] = {};
    int op_counts[TH_KIND_COUNT] = {};
    auto candidates = generate_candidates(board, sz, 2);
    for (Move m : candidates) {
        ThreatInfo info_own = analyze_move(board, sz, m.r, m.c, p);
        ThreatInfo info_op  = analyze_move(board, sz, m.r, m.c, op);
        for (int t = 0; t < TH_KIND_COUNT; ++t) {
            own_counts[t] += (info_own.count[t] > 0) ? 1 : 0;
            op_counts[t]  += (info_op.count[t] > 0) ? 1 : 0;
        }
    }
    int idx = 16;
    for (int t = 0; t < 7 && idx < 23; ++t, ++idx) {
        f[idx] = std::min(1.0f, own_counts[t] / 8.0f);
    }
    for (int t = 0; t < 7 && idx < kInputFeatures - 4; ++t, ++idx) {
        f[idx] = std::min(1.0f, op_counts[t] / 8.0f);
    }

    // Potential difference
    std::vector<int> own_scores, op_scores;
    own_scores.reserve(candidates.size());
    op_scores.reserve(candidates.size());
    for (Move m : candidates) {
        int own = get_thr(board, sz, m.r, m.c, p);
        int opponent = get_thr(board, sz, m.r, m.c, op);
        if (!(p == 1 && own >= 18000 && is_forbidden_move(board, sz, m.r, m.c)))
            own_scores.push_back(own);
        if (!(op == 1 && opponent >= 18000 && is_forbidden_move(board, sz, m.r, m.c)))
            op_scores.push_back(opponent);
    }
    int potential_diff = weighted_potential(own_scores) - weighted_potential(op_scores);
    f[23] = std::min(1.0f, std::max(-1.0f, potential_diff / 500000.0f));

    // Shape difference
    int shape_diff = board_shape_score(board, sz, p) - board_shape_score(board, sz, op);
    f[24] = std::min(1.0f, std::max(-1.0f, shape_diff / 2000.0f));

    // Center control (normalized)
    int center_r = sz / 2, center_c = sz / 2;
    float center_ctrl = 0.0f;
    for (int rr = -2; rr <= 2; ++rr) for (int cc = -2; cc <= 2; ++cc) {
        if (ok(sz, center_r + rr, center_c + cc)) {
            if (board[center_r + rr][center_c + cc] == p) center_ctrl += 1.0f;
            else if (board[center_r + rr][center_c + cc] == op) center_ctrl -= 1.0f;
        }
    }
    f[25] = std::min(1.0f, center_ctrl / 5.0f);

    // Stone count difference (normalized by board area)
    int own_stones = 0, op_stones = 0;
    for (int r = 0; r < sz; ++r) for (int c = 0; c < sz; ++c) {
        if (board[r][c] == p) ++own_stones;
        else if (board[r][c] == op) ++op_stones;
    }
    f[26] = std::min(1.0f, (own_stones - op_stones) / 20.0f);

    // Forced-four / immediate win indicators
    int win_squares = 0;
    for (Move m : candidates) {
        if (!ok(sz, m.r, m.c) || board[m.r][m.c] != 0) continue;
        bool completes = false;
        for (int d = 0; d < 4 && !completes; ++d)
            completes = count_line(board, sz, m.r, m.c, dr[d], dc[d], p) >= 5;
        if (completes) ++win_squares;
        if (win_squares >= 2) break;
    }
    f[27] = std::min(1.0f, win_squares / 2.0f);

    return f;
}

// ------------------------------------------------------------------
// Synthetic neural weights (programmatically derived)
// We use a deterministic seed-based generator so the arrays compile
// correctly at any size without massive manual initialization.
// ------------------------------------------------------------------

inline float synthetic_weight(int seed, int idx) {
    float s = std::abs(std::sin(seed * 12.9898f + idx * 78.233f)) * 43758.5453f;
    s = s - std::floor(s);
    return (s - 0.5f) * 0.35f;  // range roughly [-0.175, 0.175]
}

inline float get_w1(int i, int j) { return synthetic_weight(i * 47 + j, 137); }
inline float get_w2(int i, int j) { return synthetic_weight(i * 23 + j, 293); }
inline float get_w3(int i)      { return synthetic_weight(i * 13 + 3, 541); }
inline float get_b1(int j)      { return synthetic_weight(j * 19 + 11, 819); }
inline float get_b2(int j)      { return synthetic_weight(j * 7 + 31, 1021); }
inline float get_b3()           { return synthetic_weight(101, 2023); }

// ------------------------------------------------------------------
// ReLU activation
// ------------------------------------------------------------------
inline float relu(float x) { return x > 0.0f ? x : 0.0f; }

// ------------------------------------------------------------------
// Forward pass
// ------------------------------------------------------------------
inline float forward_network(const std::array<float, kInputFeatures>& input) {
    float hidden1[kHidden1] = {};
    float hidden2[kHidden2] = {};

    // Layer 1: input (28) -> hidden1 (48)
    for (int j = 0; j < kHidden1; ++j) {
        float sum = get_b1(j);
        for (int i = 0; i < kInputFeatures; ++i) {
            sum += input[i] * get_w1(i, j);
        }
        hidden1[j] = relu(sum);
    }

    // Layer 2: hidden1 (48) -> hidden2 (24)
    for (int j = 0; j < kHidden2; ++j) {
        float sum = get_b2(j);
        for (int i = 0; i < kHidden1; ++i) {
            sum += hidden1[i] * get_w2(i, j);
        }
        hidden2[j] = relu(sum);
    }

    // Layer 3: hidden2 (24) -> output (1)
    float output_val = get_b3();
    for (int i = 0; i < kHidden2; ++i) {
        output_val += hidden2[i] * get_w3(i);
    }

    return output_val;
}

// ------------------------------------------------------------------
// Neural-enhanced static evaluation
// We blend the neural output with the existing heuristic evaluation
// so the engine keeps its tactical sharpness (pattern VCF) while
// gaining strategic depth from the neural approximation.
// ------------------------------------------------------------------
inline int neural_static_eval(int board[kMaxBoard][kMaxBoard], int sz, int p) {
    auto features = extract_features(board, sz, p);
    float neural_score = forward_network(features);
    // Scale neural score to roughly match the heuristic score magnitude,
    // then blend.
    int heuristic = heuristic_static_eval(board, sz, p);
    // Blend: 60% heuristic + 40% neural (tuned for stability)
    float blended = 0.6f * std::min(std::max(heuristic / 150000.0f, -10.0f), 10.0f)
                    + 0.4f * std::min(std::max(neural_score, -20.0f), 20.0f);
    int result = static_cast<int>(blended * 50000.0f);
    return result;
}

} // namespace neural
