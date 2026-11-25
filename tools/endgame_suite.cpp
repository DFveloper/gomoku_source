#include <iostream>
#include <fstream>
#include <filesystem>
#include <sstream>
#include "engine_endgame.h"

using namespace gomoku;
int reference(int board[kMaxBoard][kMaxBoard],int side,int root) {
    bool empty=false,legal=false;int best=side==root?-1:1;
    for(int r=0;r<15;++r)for(int c=0;c<15;++c) {
        if(board[r][c])continue;
        empty=true;
        if(!is_legal_move(board,15,side,{r,c}))continue;
        legal=true;
        board[r][c]=side;
        const int value=check_win_at(board,15,r,c,side)?(side==root?1:-1):reference(board,3-side,root);
        board[r][c]=0;
        best=side==root?std::max(best,value):std::min(best,value);
    }
    return !empty?0:(!legal?-1:best);
}

int main() {
    int failures=0;
    auto check=[&](bool value,const char* label){
        std::cout<<(value?"[PASS] ":"[FAIL] ")<<label<<'\n';if(!value)++failures;
    };
    int board[kMaxBoard][kMaxBoard]{};
    auto fill=[&] {
        for(int r=0;r<15;++r)for(int c=0;c<15;++c)board[r][c]=(r+2*c)%4<2?1:2;
    };
    const std::vector<Move> holes{{0,0},{3,4},{7,9},{14,14}};
    bool agrees=true,restored=true;
    for(int count=0;count<=4;++count)for(int side:{1,2}) {
        fill();for(int i=0;i<count;++i)board[holes[i].r][holes[i].c]=0;
        int original[kMaxBoard][kMaxBoard];std::memcpy(original,board,sizeof(board));
        const int expected=reference(board,side,side);
        const auto actual=prove_nonloss_endgame(board,15,side,12,2000);
        agrees=agrees&&actual.value>=-1;
        if(expected<0)agrees=agrees&&actual.value==-1;
        else if(count) {
            agrees=agrees&&actual.value>=0&&is_legal_move(board,15,side,actual.move);
            if(is_legal_move(board,15,side,actual.move)) {
                board[actual.move.r][actual.move.c]=side;
                const int guaranteed=check_win_at(board,15,actual.move.r,actual.move.c,side)?1:reference(board,3-side,side);
                agrees=agrees&&guaranteed>=actual.value;
                board[actual.move.r][actual.move.c]=0;
            }
        } else agrees=agrees&&actual.value==0;
        restored=restored&&std::memcmp(original,board,sizeof(board))==0;
    }
    check(agrees,"endgame certificates agree with independent exhaustive play for both colours");
    check(restored,"all endgame proofs leave the input board unchanged");
    std::memset(board,0,sizeof(board));
    for(int c:{3,4,5,8})board[7][c]=1;
    const auto fillable=black_fillable_root_moves(board,15);
    check(!has_black_overline_hole(board,15)&&!fillable.empty()&&
        std::none_of(fillable.begin(),fillable.end(),[](Move m){return m.r==7&&m.c==7;}),
        "the draw heuristic can avoid creating a permanent overline hole before it is a forced loss");
    board[7][7]=1;
    check(has_black_overline_hole(board,15)&&black_fillable_root_moves(board,15).empty(),
        "an existing hole does not falsely eliminate every legal root");
    board[7][6]=2;
    check(!has_black_overline_hole(board,15),"White filling the reserved hole clears its geometric risk");
    board[7][6]=0;board[7][7]=0;
    for(int r:{3,4,5,6})board[r][7]=1;
    const auto winning=black_fillable_root_moves(board,15);
    check(!winning.empty()&&std::any_of(winning.begin(),winning.end(),[](Move m){return m.r==7&&m.c==7;}),
        "a legal exact-five win is not excluded because it creates a hole on another axis");
    fill();board[14][8]=0;
    for(int c:{5,6,7,9,10})board[14][c]=1;
    check(is_forbidden_move(board,15,14,8)&&prove_nonloss_endgame(board,15,1).value==-1,
        "a forbidden last intersection is a loss for Black under the existing referee");
    check(prove_nonloss_endgame(board,15,2).value==0,
        "White can legally fill the same last intersection for a board-full draw");
    check(black_overline_draw_trap(board,15,1)&&!black_overline_draw_trap(board,15,2),
        "the permanent-overline loss proof respects the last player's parity");
    board[0][0]=0;
    check(black_overline_draw_trap(board,15,2),
        "White can reserve a permanent overline hole when Black has no possible five");
    const auto avoid_invalid=prove_nonloss_endgame(board,15,2);
    check(avoid_invalid.value==0&&avoid_invalid.move.r==14&&avoid_invalid.move.c==8,
        "White fills the forbidden hole for a real draw instead of counting Black's missing reply as a win");
    board[0][0]=1;board[0][4]=0;board[0][5]=2;
    for(int c:{0,1,2,3})board[0][c]=1;
    check(has_black_five_potential(board,15)&&!black_overline_draw_trap(board,15,2),
        "a possible future exact five prevents a false overline-loss proof");
    check(white_can_seal_black_overline_trap(board,15),
        "White can prove a trap by closing Black's last exact-five window");
    fill();board[7][7]=0;board[7][2]=2;
    for(int c:{3,4,5,6})board[7][c]=1;
    check(prove_nonloss_endgame(board,15,1).value==1,
        "an immediate legal five takes priority over a draw");
    fill();for(Move move:holes)board[move.r][move.c]=0;
    check(prove_nonloss_endgame(board,15,1,12,0).value==-2,
        "an expired budget cannot manufacture a non-loss certificate");
    const std::atomic<bool> cancelled{true};
    check(prove_nonloss_endgame(board,15,1,12,500,&cancelled).value==-2,
        "a cancelled endgame search stays unknown");
    check(prove_nonloss_endgame(board,15,1,12,500,nullptr,1).value==-2,
        "an exhausted node budget stays unknown");
    check(prove_nonloss_endgame(board,15,1,2).value==-2,
        "positions outside the configured endgame size are not adjudicated");
    std::ifstream fixture(std::filesystem::path(GOMOKU_SOURCE_ROOT)/"tools/fixtures/endgame_trap.txt");
    if(!fixture)return 2;
    std::string line;
    while(std::getline(fixture,line))if(!line.empty()&&line.front()!='#')break;
    std::memset(board,0,sizeof(board));
    std::istringstream history(line);std::string token;int side=1,count=0;
    while(history>>token) {
        int r,c;if(std::sscanf(token.c_str(),"%d,%d",&r,&c)!=2||!ok(15,r,c))return 2;
        board[r][c]=side;side=3-side;++count;
    }
    check(count==202&&side==1&&is_legal_move(board,15,1,{14,7}),
        "historical ply-203 trap fixture has a legal Black candidate");
    board[14][7]=1;
    check(white_can_seal_black_overline_trap(board,15),
        "the eventual ply-225 failure is already provable after Black's ply 203");
    board[14][7]=0;bool safe_alternative=false;
    for(int r=0;r<15&&!safe_alternative;++r)for(int c=0;c<15;++c) {
        if(!is_legal_move(board,15,1,{r,c}))continue;
        board[r][c]=1;
        safe_alternative=!white_can_seal_black_overline_trap(board,15);
        board[r][c]=0;
        if(safe_alternative)break;
    }
    check(safe_alternative,"the historical trap can be avoided without changing the referee");
    std::ifstream creation(std::filesystem::path(GOMOKU_SOURCE_ROOT)/"tools/fixtures/black_hole_creation.txt");
    if(!creation)return 2;
    while(std::getline(creation,line))if(!line.empty()&&line.front()!='#')break;
    std::memset(board,0,sizeof(board));std::istringstream prefix(line);side=1;count=0;
    while(prefix>>token) {
        int r,c;if(std::sscanf(token.c_str(),"%d,%d",&r,&c)!=2||!is_legal_move(board,15,side,{r,c}))return 2;
        board[r][c]=side;side=3-side;++count;
    }
    const auto historical=black_fillable_root_moves(board,15);
    check(count==50&&side==1&&!has_black_overline_hole(board,15)&&!historical.empty()&&
        std::none_of(historical.begin(),historical.end(),[](Move m){return m.r==9&&m.c==9;}),
        "the historical ply-51 decision is caught before its invalid ply-225 ending");
    board[9][9]=1;
    check(has_black_overline_hole(board,15),"the historical discarded move really creates a reserved hole");
    return failures?1:0;
}
