#include <iostream>
#include "gomoku_core.h"
int main() {
    using namespace gomoku;
    int failures=0;
    auto check=[&](bool value,const char* label){std::cout<<(value?"[PASS] ":"[FAIL] ")<<label<<'\n';if(!value)++failures;};
    int board[kMaxBoard][kMaxBoard]{};
    for(Move move:std::vector<Move>{{7,6},{7,8},{6,7},{8,7}})board[move.r][move.c]=2;
    int snapshot[kMaxBoard][kMaxBoard];std::memcpy(snapshot,board,sizeof(board));
    Move witness{-1,-1};
    bool proved=opponent_has_unavoidable_vcf_setup(board,15,2,
        std::chrono::steady_clock::now()+std::chrono::seconds(2),16,1200,&witness,10,2);
    check(proved&&witness.r==7&&witness.c==7,"double open-three setup has a concrete winning witness");
    check(std::memcmp(board,snapshot,sizeof(board))==0,"proof leaves the input position unchanged");
    for(int c:{3,4,5})board[3][c]=1;
    check(analyze_move(board,15,7,7,2).count[TH_OPEN_THREE]>=2,"counter-four fixture retains the apparent winning fork");
    check(!opponent_has_unavoidable_vcf_setup(board,15,2,
        std::chrono::steady_clock::now()+std::chrono::seconds(2),16,1200,nullptr,10,2),
        "a distant counter-four defense refutes the apparent double-three win");
    std::memset(board,0,sizeof(board));
    board[7][7]=1;board[7][8]=2;board[8][7]=1;board[6][7]=2;
    check(!opponent_has_unavoidable_vcf_setup(board,15,2,
        std::chrono::steady_clock::now()+std::chrono::milliseconds(300),16,1200,nullptr,10,2),
        "a quiet four-stone opening cannot manufacture a threat proof");
    witness={-1,-1};
    check(!opponent_has_unavoidable_vcf_setup(board,15,2,std::chrono::steady_clock::now(),16,1200,&witness,10,2)&&witness.r<0,
        "expired proof budget returns unknown without inventing a witness");
    std::memcpy(snapshot,board,sizeof(board));
    int nodes=22000;
    const auto expired=std::chrono::steady_clock::now();
    check(!forced_four_search(board,15,2,16,nodes,&witness,&expired)&&nodes==22000&&witness.r<0&&
        std::memcmp(board,snapshot,sizeof(board))==0,
        "expired VCF budget returns unknown without consuming nodes or modifying the board");
    const std::atomic<bool> cancelled{true};
    check(!forced_four_search(board,15,2,16,nodes,&witness,nullptr,&cancelled)&&nodes==22000&&witness.r<0&&
        std::memcmp(board,snapshot,sizeof(board))==0,
        "cancelled VCF search returns unknown without modifying the board");
    return failures?1:0;
}
