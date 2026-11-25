#include <cstdio>
#include "engine_strength.h"

int main(int argc, char** argv) {
    using namespace gomoku;
    const bool allocation = strong_primary_threads(6,36)==4 &&
        strong_primary_threads(0,8)==6 && strong_primary_threads(32,4)==2 &&
        strong_primary_threads(32,2)==1 && strong_primary_threads(0,0)==1 &&
        strong_primary_threads(0,128)==30;
    std::printf("[%s] explicit, automatic and hardware-limited expert thread allocation\n",
                allocation?"PASS":"FAIL");
    if (!allocation) return 1;
    bool symmetry=true;
    for(int r=0;r<15;++r)for(int c=0;c<15;++c)for(int s=0;s<8;++s) {
        const auto moved=symmetry_move({r,c},15,s);
        const auto restored=symmetry_move(moved,15,s,true);
        symmetry=symmetry&&ok(15,moved.r,moved.c)&&restored.r==r&&restored.c==c;
    }
    std::vector<Move> axis{{3,7},{3,6},{4,8}};
    for(int s=0;s<8;++s) {
        std::vector<Move> rotated;
        for(auto move:axis)rotated.push_back(symmetry_move(move,15,s));
        const int canonical=canonical_history_symmetry(rotated,15,true,true);
        const int reference=canonical_history_symmetry(axis,15,true,true);
        for(size_t i=0;i<axis.size();++i) {
            const auto a=symmetry_move(rotated[i],15,canonical);
            const auto b=symmetry_move(axis[i],15,reference);
            symmetry=symmetry&&a.r==b.r&&a.c==b.c;
        }
    }
    std::printf("[%s] eight board symmetries preserve coordinates and canonical histories\n",symmetry?"PASS":"FAIL");
    if(!symmetry)return 1;
    if (argc == 2 && std::string(argv[1]) == "--thread-config-only") return 0;
    if (argc < 2) { std::fprintf(stderr,"usage: strength_check RUNTIME_ROOT\n"); return 2; }
    int board[kMaxBoard][kMaxBoard]{};
    std::vector<Move> history{{7,7}};
    board[7][7]=1;
    std::string error;
    EnsembleStats report;
    // Availability is an independent integration check: a legitimate learned
    // move can return before either expert is needed by the production router.
    EnsembleConfig availability;
    availability.nominal_budget_ms=5000;availability.completion_grace_ms=2000;
    availability.expert_budget_fraction=1.0;availability.expert_startup_ms=0;
    availability.primary_threads=1;availability.local_threads=1;
    availability.extra_experts.push_back({EngineVersion::AlphaGomoku2026,{}});
    report=choose_ensemble_move(board,15,history,2,4,argv[1],availability,EngineVersion::Rapfi2025);
    Move move=report.move;
    bool legal=is_legal_move(board,15,2,move);
    bool both=report.experts_launched==2 && report.experts_answered==2;
    std::printf("[%s] Rapfi + Alpha: launched=%d answered=%d move=(%d,%d) source=%s depth=%d time_ms=%d\n",
        legal&&both?"PASS":"FAIL",report.experts_launched,report.experts_answered,
        move.r,move.c,ensemble_source_name(report.source),report.local_depth,report.total_ms);
    bool pass=legal&&both;
    std::atomic<bool> cancel{true};
    move=choose_strong_move(board,15,history,2,4,1,argv[1],error,&cancel);
    pass=pass&&move.r<0;
    // A forced win must outrank every external expert, without spawning it.
    for(int col=3;col<7;++col) board[5][col]=2;
    move=choose_strong_move(board,15,history,2,4,1,argv[1],error,nullptr,
                            EngineVersion::CurrentV3,&report);
    pass=pass&&report.source==EnsembleSource::LocalTactics&&move.r==5;
    std::printf("[%s] cancellation and immediate-win precedence\n",pass?"PASS":"FAIL");
    return pass?0:1;
}
