#pragma once
#include "engine_session.h"
#include "engine_endgame.h"

namespace gomoku {
struct CouncilAllocation {
    int primary=1,diversity=0,alpha=0,arbiter=1;
};
struct CouncilTimeAllocation { int proposal_ms=0,arbitration_ms=0; };
inline CouncilTimeAllocation council_time_budget(int remaining,int requested) {
    remaining=std::max(100,remaining);
    const int arbitration=std::min(std::max(0,requested),std::max(0,remaining-200));
    return {remaining-arbitration,arbitration};
}
inline CouncilAllocation council_allocation(int total,bool alpha_available) {
    total=std::clamp(total,1,kMaxSearchThreads);
    CouncilAllocation result;
    result.arbiter=total;
    result.alpha=alpha_available&&total>=2?1:0;
    result.diversity=total-result.alpha>=2?1:0;
    result.primary=total-result.alpha-result.diversity;
    return result;
}

struct CouncilDecision {
    Move move{-1,-1};
    int score=0,depth=0,primary_score=0;
    bool promoted=false,complete=false;
    const char* reason="no-evidence";
};

// Rapfi folds symmetry-equivalent roots before MultiPV. Match that position
// symmetry explicitly so a duplicated proposal is not mistaken for a missing
// root. Preserve the first proposal's orientation (often the book baseline).
inline bool council_equivalent_moves(const int board[kMaxBoard][kMaxBoard],int size,Move a,Move b) {
    if(a.r==b.r&&a.c==b.c)return true;
    for(int symmetry=1;symmetry<8;++symmetry) {
        const auto mapped=symmetry_move(a,size,symmetry);
        if(mapped.r!=b.r||mapped.c!=b.c)continue;
        bool invariant=true;
        for(int r=0;r<size&&invariant;++r)for(int c=0;c<size;++c) {
            const auto point=symmetry_move({r,c},size,symmetry);
            if(board[r][c]!=board[point.r][point.c]){invariant=false;break;}
        }
        if(invariant)return true;
    }
    return false;
}

// A bounded goal-risk feature, NOT a proof of game loss. It also sees a
// closed-four reply whose forced Black block creates an overline hole.
// -1 means the feature budget expired; unknown must not be treated as safe.
inline int council_forced_hole_reply(const int input[kMaxBoard][kMaxBoard],int size,
        const std::chrono::steady_clock::time_point& deadline,Move* attack_witness=nullptr,Move* block_witness=nullptr) {
    if(attack_witness)*attack_witness={-1,-1};
    if(block_witness)*block_witness={-1,-1};
    int board[kMaxBoard][kMaxBoard];std::memcpy(board,input,sizeof(board));
    std::vector<Move> empty;
    for(int r=0;r<size;++r)for(int c=0;c<size;++c)if(!board[r][c])empty.push_back({r,c});
    if(empty.size()>64)return 0; // Only the direct-hole feature is active here.
    for(Move attack:empty) {
        if(std::chrono::steady_clock::now()>=deadline)return -1;
        board[attack.r][attack.c]=2;
        const Move block=find_immediate_win(board,size,2);
        bool risk=false;
        if(!check_win_at(board,size,attack.r,attack.c,2)&&block.r>=0&&
           five_completion_squares(board,size,2,2)==1&&find_immediate_win(board,size,1).r<0) {
            if(!is_legal_move(board,size,1,block))risk=true;
            else {
                board[block.r][block.c]=1;
                risk=has_black_overline_hole(board,size);
                board[block.r][block.c]=0;
            }
        }
        board[attack.r][attack.c]=0;
        if(risk) {
            if(attack_witness)*attack_witness=attack;
            if(block_witness)*block_witness=block;
            return 1;
        }
    }
    return std::chrono::steady_clock::now()>=deadline?-1:0;
}

inline int council_black_risk(const int input[kMaxBoard][kMaxBoard],int size,Move move,
        const std::chrono::steady_clock::time_point& deadline) {
    int board[kMaxBoard][kMaxBoard];std::memcpy(board,input,sizeof(board));
    if(!is_legal_move(board,size,1,move))return -1;
    board[move.r][move.c]=1;
    if(check_win_at(board,size,move.r,move.c,1))return 0;
    if(has_black_overline_hole(board,size))return 1;
    return council_forced_hole_reply(board,size,deadline);
}

// All scores below come from ONE AB search of the same board, side, roots
// and completed depth. Neural/MCTS opinions only nominate moves; their raw
// scores are never averaged or compared against AB values.
inline CouncilDecision council_select(const AnalyzedMove& primary,const AnalyzedMove& arbiter,
        const std::vector<Move>& roots,int board[kMaxBoard][kMaxBoard],int size,int player,
        int minimum_depth,int margin=20,const std::vector<int>& risks={}) {
    CouncilDecision result;result.move=primary.move;result.score=primary.score;result.depth=primary.depth;
    if(!is_legal_move(board,size,player,primary.move)){result.reason="missing-primary";return result;}
    if(primary.has_score&&primary.mate&&primary.score>0){result.reason="positive-mate";return result;}
    if(!arbiter.error.empty()||!is_legal_move(board,size,player,arbiter.move))return result;
    if(roots.empty()||arbiter.completed_candidates.size()!=roots.size()) {
        result.reason="incomplete-roots";return result;
    }
    const SearchLine* reference=nullptr;const SearchLine* best=nullptr;size_t reference_index=0;
    for(size_t i=0;i<roots.size();++i) {
        const Move root=roots[i];
        if(!is_legal_move(board,size,player,root)){result.reason="illegal-root";return result;}
        for(size_t j=0;j<i;++j)if(roots[j].r==root.r&&roots[j].c==root.c) {
            result.reason="duplicate-root";return result;
        }
        const SearchLine* found=nullptr;
        for(const auto& line:arbiter.completed_candidates)if(line.move.r==root.r&&line.move.c==root.c) {
            if(found){result.reason="duplicate-line";return result;}found=&line;
        }
        if(!found||!found->has_score||found->depth!=arbiter.completed_depth) {
            result.reason="incomplete-depth";return result;
        }
        if(root.r==primary.move.r&&root.c==primary.move.c){reference=found;reference_index=i;}
        if(!best||found->score>best->score)best=found;
    }
    if(!reference){result.reason="missing-primary-root";return result;}
    result.depth=arbiter.completed_depth;
    if(arbiter.completed_depth<std::max(1,minimum_depth)){result.reason="shallow";return result;}
    result.complete=true;result.primary_score=reference->score;
    if(best->score>reference->score+margin) {
        result.move=best->move;result.score=best->score;result.promoted=true;result.reason="common-search";
    } else {
        result.score=reference->score;result.reason="keep-primary";
        if(player==1&&risks.size()==roots.size()&&risks[reference_index]>0&&std::abs(reference->score)<29500) {
            for(size_t i=0;i<roots.size();++i)if(risks[i]==0) {
                const auto found=std::find_if(arbiter.completed_candidates.begin(),arbiter.completed_candidates.end(),[&](const SearchLine& line){
                    return line.move.r==roots[i].r&&line.move.c==roots[i].c;
                });
                // No hard ban, and never trade a better AB score for the feature.
                if(found!=arbiter.completed_candidates.end()&&found->score>=reference->score) {
                    result.move=found->move;result.score=found->score;result.promoted=true;result.reason="goal-risk-tie";break;
                }
            }
        }
    }
    return result;
}
} // namespace gomoku
