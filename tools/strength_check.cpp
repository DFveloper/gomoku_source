#include <cstdio>
#include "engine_strength.h"

int main(int argc, char** argv) {
    using namespace gomoku;
    if (argc < 2) { std::fprintf(stderr,"usage: strength_check RUNTIME_ROOT\n"); return 2; }
    int board[kMaxBoard][kMaxBoard]{};
    std::vector<Move> history{{7,7}};
    board[7][7]=1;
    std::string error;
    EnsembleStats report;
    Move move=choose_strong_move(board,15,history,2,4,1,argv[1],error,nullptr,
                                EngineVersion::CurrentV3,&report);
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
