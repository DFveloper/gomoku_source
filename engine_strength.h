#pragma once
#include "engine_ensemble.h"

namespace gomoku {
// Shared by the native UI, both WebUIs and the benchmark: deployment uses the
// same verified expert mixture that development tools measure.
inline Move choose_strong_move(
        int board[kMaxBoard][kMaxBoard], int size, const std::vector<Move>& history,
        int player, int level, int threads, const std::filesystem::path& runtime,
        std::string& error, const std::atomic<bool>* cancel = nullptr,
        EngineVersion opponent = EngineVersion::CurrentV3, EnsembleStats* report = nullptr) {
    (void)opponent;
    if (cancel && cancel->load()) return {-1,-1};
    EnsembleConfig config;
    config.nominal_budget_ms = 5000;
    config.completion_grace_ms = 2000;
    config.expert_budget_fraction = 1.0;
    config.expert_startup_ms = 0;
    config.prefer_consensus = true; // Agreement nominates a move; tactical proof can still reject it.
    config.local_threads = threads > 0 ? std::min(threads, 2) : 1;
    config.extra_experts.push_back({EngineVersion::AlphaGomoku2026, {}});
    auto result = choose_ensemble_move(board, size, history, player, level,
                                      runtime, config, EngineVersion::Rapfi2025, cancel);
    if (report) *report = result;
    error.clear();
    if (cancel && cancel->load()) return {-1,-1};
    if (!is_legal_move(board,size,player,result.move)) {
        error = "expert mixture returned no legal move";
        return first_legal_move(board,size,player);
    }
    return result.move;
}
}
