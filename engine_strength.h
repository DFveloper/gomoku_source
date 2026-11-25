#pragma once
#include "engine_ensemble.h"
#include "engine_adaptive.h"
#include "engine_endgame.h"

namespace gomoku {
inline int strong_total_threads(int requested, unsigned hardware) {
    const int cores = hardware ? static_cast<int>(std::min<unsigned>(hardware, kMaxSearchThreads)) : 1;
    return std::max(1,requested>0?std::min(requested,cores):cores);
}
inline int strong_primary_threads(int requested, unsigned hardware) {
    return std::max(1,strong_total_threads(requested,hardware)-2);
}

// Shared by the native UI, both WebUIs and the benchmark: deployment uses the
// same verified expert mixture that development tools measure.
inline Move choose_strong_move_impl(
        int board[kMaxBoard][kMaxBoard], int size, const std::vector<Move>& history,
        int player, int level, int threads, const std::filesystem::path& runtime,
        std::string& error, const std::atomic<bool>* cancel = nullptr,
        EngineVersion opponent = EngineVersion::CurrentV3, EnsembleStats* report = nullptr) {
    (void)opponent;
    if (cancel && cancel->load()) return {-1,-1};
    const bool tangent_opening=std::getenv("GOMOKU_TANGENTIAL_OPENING") && history.size()==1 &&
        ((history.front().r==size/2)!=(history.front().c==size/2));
    if(player==2 && history.size()==1 && (tangent_opening||std::getenv("GOMOKU_OPENING_CENTER")||std::getenv("GOMOKU_OPENING_OFFSET"))) {
        int stones=0;
        for(int r=0;r<size;++r)for(int c=0;c<size;++c)stones+=board[r][c]!=0;
        if(stones==1) {
            Move move{size/2,size/2};
            if(board[move.r][move.c])--move.r;
            if(tangent_opening) {
                move=history.front();
                if(move.r==size/2)--move.r;
                else --move.c;
            }
            if(const char* offset=std::getenv("GOMOKU_OPENING_OFFSET")) {
                int dr=0,dc=0;
                if(std::sscanf(offset,"%d,%d",&dr,&dc)==2&&std::abs(dr)<size&&std::abs(dc)<size) {
                    Move alternative{history.front().r+dr,history.front().c+dc};
                    if(is_legal_move(board,size,player,alternative))move=alternative;
                }
            }
            if(is_legal_move(board,size,player,move)) {
                if(report){*report=EnsembleStats{};report->move=move;report->source=EnsembleSource::LocalSearch;}
                error.clear();
                if(std::getenv("GOMOKU_BENCH_TRACE"))std::fprintf(stderr,"opening-policy move=%d,%d\n",move.r,move.c);
                return move;
            }
        }
    }
    const bool alpha_legacy=opponent==EngineVersion::AlphaGomoku2026&&std::getenv("GOMOKU_ALPHA_LEGACY");
    const bool root_council=opponent==EngineVersion::Rapfi2025&&std::getenv("GOMOKU_ROOT_COUNCIL_MS");
    if (std::getenv("GOMOKU_RETAINED_SEARCH") || std::getenv("GOMOKU_EXPERIENCE_BOOK") || alpha_legacy ||
        std::getenv("GOMOKU_MCTS_SEARCH") || std::getenv("GOMOKU_RAPFI_BOOK_ONLY") || std::getenv("GOMOKU_BLACK_DRAW_SAFETY") || root_council ||
        (std::getenv("GOMOKU_SAFE_ROUTER") && player==2)) {
        AdaptiveSearchConfig config;
        config.primary_threads=strong_primary_threads(threads,std::thread::hardware_concurrency());
        config.total_threads=strong_total_threads(threads,std::thread::hardware_concurrency());
        if(root_council)config.council_ms=std::clamp(std::atoi(std::getenv("GOMOKU_ROOT_COUNCIL_MS")),0,4000);
        if(root_council)if(const char* ms=std::getenv("GOMOKU_COUNCIL_RESCUE_MS"))config.council_rescue_ms=std::clamp(std::atoi(ms),0,10000);
        if (const char* ms=std::getenv("GOMOKU_RECHECK_MS")) config.recheck_ms=std::max(0,std::atoi(ms));
        if (const char* ms=std::getenv("GOMOKU_ALTERNATIVE_MS")) config.alternative_ms=std::max(0,std::atoi(ms));
        if (const char* ms=std::getenv("GOMOKU_ROLLOUT_MS")) config.rollout_ms=std::max(0,std::atoi(ms));
        if (const char* ms=std::getenv("GOMOKU_JUDGE_MS")) config.judge_ms=std::max(0,std::atoi(ms));
        if (const char* count=std::getenv("GOMOKU_SYMMETRY_COUNCIL")) config.symmetry_workers=std::clamp(std::atoi(count),0,8);
        config.opponent=opponent;
        config.mcts=std::getenv("GOMOKU_MCTS_SEARCH")!=nullptr || std::getenv("GOMOKU_SAFE_ROUTER")!=nullptr;
        config.prefer_nonloss=std::getenv("GOMOKU_NONLOSS_OBJECTIVE")!=nullptr;
        config.legacy_experience=alpha_legacy;
        config.rapfi_book_only=std::getenv("GOMOKU_RAPFI_BOOK_ONLY")!=nullptr;
        config.protect_black_draws=std::getenv("GOMOKU_BLACK_DRAW_SAFETY")!=nullptr;
        auto result=choose_adaptive_move(board,size,history,player,level,runtime,config,cancel);
        if (report) *report=result;
        error.clear();return result.move;
    }
    EnsembleConfig config;
    config.nominal_budget_ms = 5000;
    config.prefer_nonloss=std::getenv("GOMOKU_NONLOSS_OBJECTIVE")!=nullptr;
    if(const char* program=std::getenv("GOMOKU_PRIMARY_PROGRAM"))config.expert_program=program;
    config.completion_grace_ms = 2000;
    config.expert_budget_fraction = 1.0;
    config.expert_startup_ms = 0;
    config.prefer_consensus = true; // Agreement nominates a move; tactical proof can still reject it.
    // Reserve one worker each for the local proof search and Alpha. Spend extra
    // workers on Rapfi's search rather than duplicating the weaker local search.
    config.local_threads = 1;
    config.primary_threads = strong_primary_threads(threads, std::thread::hardware_concurrency());
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

inline int canonical_history_symmetry(const std::vector<Move>& history,int size,bool minimize=false,bool column_first=false) {
    int best=0;
    for(int symmetry=1;symmetry<8;++symmetry) {
        for(Move move:history) {
            const Move a=symmetry_move(move,size,symmetry),b=symmetry_move(move,size,best);
            if(a.c==b.c&&a.r==b.r)continue;
            const bool row_first=minimize&&!column_first;
            const int a0=row_first?a.r:a.c,a1=row_first?a.c:a.r;
            const int b0=row_first?b.r:b.c,b1=row_first?b.c:b.r;
            if(minimize?(a0<b0||(a0==b0&&a1<b1)):(a0>b0||(a0==b0&&a1>b1)))best=symmetry;
            break;
        }
    }
    return best;
}

inline Move choose_strong_move(
        int board[kMaxBoard][kMaxBoard],int size,const std::vector<Move>& history,
        int player,int level,int threads,const std::filesystem::path& runtime,
        std::string& error,const std::atomic<bool>* cancel=nullptr,
        EngineVersion opponent=EngineVersion::CurrentV3,EnsembleStats* report=nullptr) {
    if(cancel&&cancel->load())return {-1,-1};
    const auto endgame=prove_nonloss_endgame(board,size,player,12,500,cancel);
    if(cancel&&cancel->load())return {-1,-1};
    if(endgame.value>=0&&is_legal_move(board,size,player,endgame.move)) {
        if(std::getenv("GOMOKU_BENCH_TRACE"))
            std::fprintf(stderr,"endgame-proof plies=%zu value=%d nodes=%d move=%d,%d\n",
                history.size(),endgame.value,endgame.nodes,endgame.move.r,endgame.move.c);
        if(report) {
            *report=EnsembleStats{};report->move=endgame.move;
            report->source=EnsembleSource::LocalEndgame;
            report->local_proved_win=endgame.value>0;report->total_ms=endgame.elapsed_ms;
        }
        error.clear();return endgame.move;
    }
    if(!std::getenv("GOMOKU_CANONICAL_SEARCH") && !std::getenv("GOMOKU_SAFE_ROUTER"))
        return choose_strong_move_impl(board,size,history,player,level,threads,runtime,error,cancel,opponent,report);
    const char* canonical=std::getenv("GOMOKU_CANONICAL_SEARCH");
    const bool left=canonical&&std::string(canonical)=="left";
    const bool center=history.empty()||(history.front().r==size/2&&history.front().c==size/2);
    const int symmetry=left&&center?0:canonical_history_symmetry(history,size,
        left||(player==2&&std::getenv("GOMOKU_SAFE_ROUTER")),left);
    int transformed[kMaxBoard][kMaxBoard]{};
    std::vector<Move> sequence;
    for(int r=0;r<size;++r)for(int c=0;c<size;++c) {
        const Move point=symmetry_move({r,c},size,symmetry);
        transformed[point.r][point.c]=board[r][c];
    }
    for(Move move:history)sequence.push_back(symmetry_move(move,size,symmetry));
    auto move=choose_strong_move_impl(transformed,size,sequence,player,level,threads,runtime,error,cancel,opponent,report);
    move=symmetry_move(move,size,symmetry,true);
    if(report)report->move=move;
    if(std::getenv("GOMOKU_BENCH_TRACE"))
        std::fprintf(stderr,"canonical plies=%zu symmetry=%d move=%d,%d\n",history.size(),symmetry,move.r,move.c);
    return move;
}
}
