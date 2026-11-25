#include <iostream>
#include <fstream>
#include <sstream>
#include "engine_adaptive.h"
using namespace gomoku;
int main() {
    int failures=0;
    auto check=[&](bool value,const char* name){std::cout<<(value?"[PASS] ":"[FAIL] ")<<name<<'\n';if(!value)++failures;};
    bool budgets=true;
    for(int total=1;total<=40;++total)for(bool alpha:{false,true}) {
        const auto a=council_allocation(total,alpha);const int cap=std::min(total,32);
        budgets=budgets&&a.primary>=1&&a.primary+a.diversity+a.alpha==cap&&a.arbiter==cap&&(!a.alpha||alpha);
    }
    check(budgets,"proposal phases respect 1..32 worker budgets and arbitration reuses the full budget");
    bool time_budgets=true;
    for(int remaining:{100,200,1000,3000,5000})for(int requested:{0,1500,3500,4000}) {
        const auto timing=council_time_budget(remaining,requested);
        time_budgets=time_budgets&&timing.proposal_ms>=100&&timing.proposal_ms+timing.arbitration_ms==remaining;
    }
    check(time_budgets&&council_time_budget(5000,3500).proposal_ms==1500,
        "proposal generation and deeper common arbitration split one nominal search budget");
    auto pv=[](AnalyzedMove& out,int index,int count,int depth,int value,Move move,bool root_depth=true){
        parse_search_info("INFO PV "+std::to_string(index),out);
        parse_search_info("INFO NUMPV "+std::to_string(count),out);
        parse_search_info(std::string(root_depth?"INFO DEPTH ":"INFO SELDEPTH ")+std::to_string(depth),out);
        parse_search_info("INFO EVAL "+std::to_string(value),out);
        parse_search_info("INFO BESTLINE "+std::to_string(move.c)+","+std::to_string(move.r),out);
        parse_search_info("INFO PV DONE",out);
    };
    int board[kMaxBoard][kMaxBoard]{};board[7][7]=1;
    check(council_equivalent_moves(board,15,{6,6},{8,8})&&
        !council_equivalent_moves(board,15,{6,6},{6,7}),
        "position-symmetric roots are folded without confusing diagonal and axial replies");
    board[2][3]=2;
    check(!council_equivalent_moves(board,15,{6,6},{8,8}),
        "asymmetric stones prevent invalid root folding");board[2][3]=0;
    const Move first{8,8},second{8,7};const std::vector<Move> roots{first,second};
    AnalyzedMove primary;primary.move=first;primary.score=10000;primary.depth=12;primary.has_score=true;
    AnalyzedMove search;search.move=second;
    pv(search,0,2,12,0,first);pv(search,1,2,12,80,second);
    auto selected=council_select(primary,search,roots,board,15,2,12);
    check(selected.complete&&selected.promoted&&selected.move.c==7&&selected.score==80,
        "one common AB iteration selects candidates without comparing foreign expert score scales");
    pv(search,0,2,13,9000,first);
    selected=council_select(primary,search,roots,board,15,2,12);
    check(search.completed_depth==12&&selected.score==80&&selected.promoted,
        "a partial deeper iteration cannot overwrite a completed common-depth comparison");
    check(!council_select(primary,search,roots,board,15,2,13).complete,
        "a shallow committee cannot displace a deeper baseline");
    search.error="timeout";
    check(!council_select(primary,search,roots,board,15,2,12).promoted,
        "a failed arbitration abstains even if it printed earlier PV information");search.error.clear();
    primary.mate=true;primary.score=29990;
    check(!council_select(primary,search,roots,board,15,2,12).promoted,
        "common arbitration cannot discard a positive baseline mate");primary.mate=false;primary.score=100;
    auto incomplete=search;incomplete.completed_candidates.pop_back();
    check(!council_select(primary,incomplete,roots,board,15,2,12).complete,
        "missing root coverage cannot become a promotion");
    check(!council_select(primary,search,{first,first},board,15,2,12).complete,
        "duplicated roots cannot fake complete coverage");
    auto illegal=search;illegal.completed_candidates[1].move={7,7};
    check(!council_select(primary,illegal,{first,{7,7}},board,15,2,12).complete,
        "occupied candidate roots are rejected");
    AnalyzedMove mcts;mcts.move=second;pv(mcts,0,2,40,0,first,false);pv(mcts,1,2,40,100,second,false);
    check(mcts.completed_candidates.empty(),"MCTS selective depth cannot masquerade as completed AB depth");
    AnalyzedMove mismatch;pv(mismatch,0,2,12,0,first);pv(mismatch,1,3,12,100,second);
    check(mismatch.completed_candidates.empty(),"inconsistent PV counts do not create a complete iteration");
    AnalyzedMove tie;tie.move=second;pv(tie,0,2,12,100,first);pv(tie,1,2,12,100,second);
    const auto stable=council_select(primary,tie,roots,board,15,2,12);
    check(stable.complete&&!stable.promoted&&stable.move.c==8,"ties keep the baseline rather than oscillating experts");
    int poison[kMaxBoard][kMaxBoard]{};
    for(int c:{3,4,5,8})poison[7][c]=1;
    const Move risky{7,7},safe{0,0};
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(1);
    check(council_black_risk(poison,15,risky,deadline)==1&&council_black_risk(poison,15,safe,deadline)==0,
        "the goal expert distinguishes a newly created overline hole from a fillable alternative");
    AnalyzedMove flat;flat.move=risky;flat.depth=8;flat.has_score=true;
    AnalyzedMove flat_search;flat_search.move=safe;
    pv(flat_search,0,2,8,0,risky);pv(flat_search,1,2,8,0,safe);
    auto safer=council_select(flat,flat_search,{risky,safe},poison,15,1,8,20,{1,0});
    check(safer.complete&&safer.promoted&&safer.move.r==0&&std::string(safer.reason)=="goal-risk-tie",
        "equal-depth equal-score roots may prefer a fillable board without treating the feature as a proof");
    flat_search.completed_candidates[0].score=1226;flat_search.completed_candidates[1].score=1244;
    const auto positive_band=council_select(flat,flat_search,{risky,safe},poison,15,1,8,20,{1,0});
    check(positive_band.promoted&&positive_band.move.r==0,
        "the goal expert resolves a small positive-score margin without requiring zero-valued draw scores");
    flat_search.completed_candidates[0].score=0;
    flat_search.completed_candidates[1].score=-1;
    check(!council_select(flat,flat_search,{risky,safe},poison,15,1,8,20,{1,0}).promoted,
        "the goal feature cannot force an inferior AB-scored alternative");
    flat_search.completed_candidates[1].score=0;
    check(!council_select(flat,flat_search,{risky,safe},poison,15,1,8,20,{1,-1}).promoted,
        "an unknown feature result is not labelled safe");
    std::ifstream fixture(std::filesystem::path(GOMOKU_SOURCE_ROOT)/"tools/fixtures/black_forced_hole.txt");
    if(!fixture)return 2;
    std::string line;
    while(std::getline(fixture,line))if(!line.empty()&&line.front()!='#')break;
    std::memset(poison,0,sizeof(poison));std::istringstream moves(line);std::string token;int side=1,count=0;
    while(moves>>token) {
        int r,c;if(std::sscanf(token.c_str(),"%d,%d",&r,&c)!=2||!is_legal_move(poison,15,side,{r,c}))return 2;
        poison[r][c]=side;side=3-side;++count;
    }
    int original[kMaxBoard][kMaxBoard];std::memcpy(original,poison,sizeof(poison));
    deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    check(count==172&&side==1&&!has_black_overline_hole(poison,15)&&
        council_black_risk(poison,15,{1,3},deadline)==1,
        "the historical forced-block hole is visible before the opponent creates its four");
    check(std::memcmp(original,poison,sizeof(poison))==0,"goal-risk analysis restores the imported position");
    for(int c=3;c<7;++c)board[5][c]=2;
    AdaptiveSearchConfig config;config.council_ms=1500;
    auto tactics=choose_adaptive_move(board,15,{{7,7}},2,1,"/nonexistent-council-runtime",config);
    check(tactics.source==EnsembleSource::LocalTactics&&tactics.experts_launched==0&&tactics.move.r==5,
        "an immediate win precedes all committee work");
    return failures?1:0;
}
