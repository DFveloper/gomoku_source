#pragma once
#include "gomoku_core.h"

namespace gomoku {
inline constexpr Move kEndgameDirections[]={{0,1},{1,0},{1,1},{1,-1}};

// White may leave this intersection for Black's last turn. The geometric
// overline cannot disappear while the intersection remains empty; Black can
// still escape by winning an exact five elsewhere (or at the same point).
inline bool has_black_overline_hole(const int board[kMaxBoard][kMaxBoard],int size) {
    for(int r=0;r<size;++r)for(int c=0;c<size;++c)if(!board[r][c])
        for(Move direction:kEndgameDirections)
            if(count_line(board,size,r,c,direction.r,direction.c,1)>5)return true;
    return false;
}

// A heuristic for a placement-only non-loss policy, not a loss certificate.
// Prefer roots that retain a fillable board while such roots still exist.
// Immediate legal fives remain allowed. No legal alternative means unrestricted.
inline std::vector<Move> black_fillable_root_moves(const int input[kMaxBoard][kMaxBoard],int size) {
    if(size%2==0||has_black_overline_hole(input,size))return {};
    int board[kMaxBoard][kMaxBoard];std::memcpy(board,input,sizeof(board));
    std::vector<Move> allowed;bool excluded=false;
    for(int r=0;r<size;++r)for(int c=0;c<size;++c) {
        if(!is_legal_move(board,size,1,{r,c}))continue;
        board[r][c]=1;
        const bool risky=!check_win_at(board,size,r,c,1)&&has_black_overline_hole(board,size);
        board[r][c]=0;
        if(risky)excluded=true;else allowed.push_back({r,c});
    }
    return excluded?allowed:std::vector<Move>{};
}

// Conservative reachability test: if every exact-five window contains White
// or is permanently bounded by Black, Black can never win by placing stones.
inline bool has_black_five_potential(const int board[kMaxBoard][kMaxBoard],int size) {
    for(int r=0;r<size;++r)for(int c=0;c<size;++c)
        for(Move direction:kEndgameDirections) {
            const int er=r+4*direction.r,ec=c+4*direction.c;
            if(!ok(size,er,ec))continue;
            if(ok(size,r-direction.r,c-direction.c)&&board[r-direction.r][c-direction.c]==1)continue;
            if(ok(size,er+direction.r,ec+direction.c)&&board[er+direction.r][ec+direction.c]==1)continue;
            bool possible=true;
            for(int i=0;i<5;++i)if(board[r+i*direction.r][c+i*direction.c]==2){possible=false;break;}
            if(possible)return true;
        }
    return false;
}

// Under the placement-only referee, White can reserve a permanent Black
// overline hole for Black's last turn. This proves a loss only when no future
// exact five can let Black win or legalize that hole. Parity is essential.
inline bool black_overline_draw_trap(const int board[kMaxBoard][kMaxBoard],int size,int next_player) {
    if(size%2==0)return false;
    int empty=0;bool hole=false;
    for(int r=0;r<size;++r)for(int c=0;c<size;++c)if(!board[r][c]) {
        ++empty;
        for(Move direction:kEndgameDirections)
            if(count_line(board,size,r,c,direction.r,direction.c,1)>5)hole=true;
    }
    return hole&&empty>0&&(empty%2?1:2)==next_player&&!has_black_five_potential(board,size);
}

inline bool white_can_seal_black_overline_trap(const int input[kMaxBoard][kMaxBoard],int size) {
    if(size%2==0)return false;
    std::vector<Move> empty;bool hole=false;
    for(int r=0;r<size;++r)for(int c=0;c<size;++c)if(!input[r][c]) {
        empty.push_back({r,c});
        for(Move direction:kEndgameDirections)
            if(count_line(input,size,r,c,direction.r,direction.c,1)>5)hole=true;
    }
    if(!hole||empty.empty()||empty.size()%2)return false; // White must be next.
    if(!has_black_five_potential(input,size))return true;
    if(empty.size()>32)return false;
    int board[kMaxBoard][kMaxBoard];std::memcpy(board,input,sizeof(board));
    for(Move reply:empty) {
        board[reply.r][reply.c]=2;
        const bool sealed=black_overline_draw_trap(board,size,1);
        board[reply.r][reply.c]=0;
        if(sealed)return true;
    }
    return false;
}

struct EndgameProof {
    Move move{-1,-1};
    int value=-2; // +1 valid win, 0 guaranteed non-loss, -1 goal failure; -2 unknown.
    int nodes=0;
    int elapsed_ms=0;
};

// Exact placement-only endgame, matching the existing referee: a full board
// draws; a no-legal-move reply is an invalid match and cannot certify a win
// for either candidate colour. Utility is always from the root candidate's
// perspective, rather than negating an opponent's error into a claimed win.
// No passes or draw adjudications are introduced. A timeout stays unknown.
inline EndgameProof prove_nonloss_endgame(
        const int input[kMaxBoard][kMaxBoard],int size,int player,
        int max_empty=12,int time_ms=500,const std::atomic<bool>* cancel=nullptr,
        int node_limit=200000) {
    EndgameProof result;
    const auto started=std::chrono::steady_clock::now();
    const auto deadline=started+std::chrono::milliseconds(std::max(0,time_ms));
    std::vector<Move> empty;
    for(int r=0;r<size;++r)for(int c=0;c<size;++c)
        if(!input[r][c])empty.push_back({r,c});
    if(empty.size()>static_cast<size_t>(std::clamp(max_empty,0,14)))return result;
    int board[kMaxBoard][kMaxBoard];std::memcpy(board,input,sizeof(board));
    auto stopped=[&] {
        return result.nodes>=node_limit||(cancel&&cancel->load())||
            std::chrono::steady_clock::now()>=deadline;
    };
    auto finish=[&](int value,Move move) {
        result.value=value;result.move=move;
        result.elapsed_ms=static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now()-started).count());return result;
    };
    if(stopped())return finish(-2,{-1,-1});
    if(empty.empty())return finish(0,{-1,-1});
    std::vector<Move> legal;
    for(Move move:empty) {
        if(stopped())return finish(-2,{-1,-1});
        if(!is_legal_move(board,size,player,move))continue;
        board[move.r][move.c]=player;
        const bool win=check_win_at(board,size,move.r,move.c,player);
        board[move.r][move.c]=0;
        if(win)return finish(1,move);
        legal.push_back(move);
    }
    if(legal.empty())return finish(-1,{-1,-1});

    // A ternary code is collision-free for the root's empty intersections.
    // Fixed stones never change, and ply parity determines the side to move.
    std::vector<unsigned> powers(empty.size()+1,1);
    for(size_t i=0;i<empty.size();++i)powers[i+1]=powers[i]*3;
    std::vector<std::int8_t> memo(powers.back(),2);
    auto search=[&](auto&& self,int side,unsigned code,int remaining)->int {
        if(stopped())return 2;
        ++result.nodes;
        if(remaining==0)return 0;
        if(memo[code]!=2)return memo[code];
        const bool own_turn=side==player;
        int best=own_turn?-1:1;bool incomplete=false,has_legal=false;
        for(size_t i=0;i<empty.size();++i) {
            const Move move=empty[i];
            if(board[move.r][move.c]||!is_legal_move(board,size,side,move))continue;
            has_legal=true;
            board[move.r][move.c]=side;
            const int child=check_win_at(board,size,move.r,move.c,side)?(own_turn?1:-1):
                self(self,3-side,code+side*powers[i],remaining-1);
            board[move.r][move.c]=0;
            if(child==2)incomplete=true;
            else {
                best=own_turn?std::max(best,child):std::min(best,child);
                if((own_turn&&best==1)||(!own_turn&&best==-1)) {
                    memo[code]=static_cast<std::int8_t>(best);return best;
                }
            }
            if(stopped()){incomplete=true;break;}
        }
        if(incomplete)return 2;
        if(!has_legal)best=-1;
        memo[code]=static_cast<std::int8_t>(best);return best;
    };
    for(Move move:legal) {
        const size_t index=static_cast<size_t>(std::find_if(empty.begin(),empty.end(),[&](Move item){
            return item.r==move.r&&item.c==move.c;
        })-empty.begin());
        board[move.r][move.c]=player;
        const int child=search(search,3-player,player*powers[index],static_cast<int>(empty.size())-1);
        board[move.r][move.c]=0;
        if(child==0||child==1)return finish(child,move);
        if(child==2)return finish(-2,{-1,-1});
    }
    return finish(-1,legal.front());
}
} // namespace gomoku
