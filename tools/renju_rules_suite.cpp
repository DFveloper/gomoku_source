#include <fstream>
#include <filesystem>
#include <iostream>
#include <sstream>
#include "gomoku_core.h"

int main() {
    using namespace gomoku;
    int failures=0,positions=0;
    bool recovered_win=false;
    auto check=[&](bool value,const char* label) {
        std::cout<<(value?"[PASS] ":"[FAIL] ")<<label<<'\n';if(!value)++failures;
    };
    int board[kMaxBoard][kMaxBoard]{};
    for(int c:{4,6,8,10})board[7][c]=1;
    check(is_forbidden_move(board,15,7,7),"two different fours on one axis are forbidden");
    std::memset(board,0,sizeof(board));
    for(int c:{4,5,6})board[7][c]=1;
    check(!is_forbidden_move(board,15,7,7),"an open four's two endpoints count as one four");
    std::memset(board,0,sizeof(board));
    for(int c:{3,4,5,6,8})board[7][c]=1;
    for(int r:{3,4,5,6})board[r][7]=1;
    check(!is_forbidden_move(board,15,7,7),"five takes precedence over simultaneous forbidden shapes");
    std::memset(board,0,sizeof(board));
    for(int col:{5,9})for(int row:{3,4,5,6})board[row][col]=1;
    for(Move move:std::vector<Move>{{7,6},{7,8},{6,7},{8,7}})board[move.r][move.c]=1;
    check(!is_forbidden_move(board,15,7,7),
        "a three cannot extend via a move that simultaneously completes five on another axis");
    board[3][5]=board[4][5]=0;
    check(is_forbidden_move(board,15,7,7),
        "one non-winning legal extension restores the genuine double-three");
    const auto file=std::filesystem::path(GOMOKU_SOURCE_ROOT)/"tools/fixtures/renju_legality.txt";
    std::ifstream input(file);
    if(!input){std::cerr<<"missing fixture "<<file<<'\n';return 2;}
    std::string line;
    while(std::getline(input,line)) {
        if(line.empty()||line.front()=='#')continue;
        const auto separator=line.find('|');
        if(separator==std::string::npos)return 2;
        std::istringstream history(line.substr(0,separator)),expected(line.substr(separator+1));
        std::string name,coordinate;history>>name;
        std::memset(board,0,sizeof(board));int turn=1;
        while(history>>coordinate) {
            int r,c;if(std::sscanf(coordinate.c_str(),"%d,%d",&r,&c)!=2||!ok(15,r,c))return 2;
            board[r][c]=turn;turn=3-turn;
        }
        bool forbidden[15][15]{};
        while(expected>>coordinate) {
            int r,c;if(std::sscanf(coordinate.c_str(),"%d,%d",&r,&c)!=2||!ok(15,r,c))return 2;
            forbidden[r][c]=true;
        }
        int snapshot[kMaxBoard][kMaxBoard];std::memcpy(snapshot,board,sizeof(board));
        for(int r=0;r<15;++r)for(int c=0;c<15;++c) {
            if(is_forbidden_move(board,15,r,c)!=forbidden[r][c]) {
                std::cerr<<"mismatch "<<name<<" at "<<r<<","<<c<<'\n';++failures;
            }
        }
        if(name=="rapfi_00.log:58") {
            Move winning{-1,-1};int nodes=22000;
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(500);
            recovered_win=forced_four_search(board,15,1,16,nodes,&winning,&deadline)&&
                winning.r==9&&winning.c==9;
            check(recovered_win,"previously rejected legal move recovers Rapfi-confirmed mate at ply 59");
        }
        if(std::memcmp(snapshot,board,sizeof(board))!=0){std::cerr<<"board mutated: "<<name<<'\n';++failures;}
        ++positions;
    }
    check(positions==90,"90 independently recorded full-board legality masks");
    check(recovered_win,"the historical missed-win regression was exercised");
    std::cout<<"positions="<<positions<<" failures="<<failures<<'\n';
    return failures?1:0;
}
