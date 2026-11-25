#pragma once
#include "gomoku_core.h"
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <unordered_map>

namespace gomoku {
struct ExperienceChoice {
    Move move{-1,-1};
    // Weighted training observations, including symmetry augmentation. These
    // are not a count of fresh validation games or a forced-result certificate.
    int nonloss_weight=0,total_weight=0;
    // Distinct observed opponent replies with repeated failures and no
    // successful continuation. This is an exploration hint, never a proof.
    int uncovered_replies=0;
};
inline std::string experience_state(const int board[kMaxBoard][kMaxBoard],int size) {
    std::string state;state.reserve(size*size);
    for(int r=0;r<size;++r)for(int c=0;c<size;++c)state.push_back(static_cast<char>('0'+board[r][c]));
    return state;
}
inline std::string experience_key(int opponent,int player,const std::string& state) {
    return std::to_string(opponent)+":"+std::to_string(player)+":"+state;
}
using ExperienceTable=std::unordered_map<std::string,std::vector<ExperienceChoice>>;

inline ExperienceTable read_experience(const std::string& path) {
    std::ifstream input(path);std::string header;
    if(!(input>>header)||(header!="GOMOKU_EXPERIENCE_V1"&&header!="GOMOKU_EXPERIENCE_V2"&&header!="GOMOKU_EXPERIENCE_V3"))return {};
    const int minimum=header=="GOMOKU_EXPERIENCE_V1"?1:0;
    ExperienceTable table;
    std::string line;std::getline(input,line);
    while(std::getline(input,line)) {
        if(line.empty())continue;
        std::istringstream row(line);int opponent,player,r,c,nonlosses,games,uncovered=0;std::string state,extra;
        if(!(row>>opponent>>player>>state>>r>>c>>nonlosses>>games)||
           (header=="GOMOKU_EXPERIENCE_V3"&&!(row>>uncovered))||(row>>extra)||
           (opponent!=4&&opponent!=5)||(player!=1&&player!=2)||state.size()!=225||
           state.find_first_not_of("012")!=std::string::npos||!ok(15,r,c)||
           state[r*15+c]!='0'||nonlosses<minimum||games<1||games<nonlosses||uncovered<0||uncovered>games)return {};
        const auto key=experience_key(opponent,player,state);
        auto& choices=table[key];
        if(std::any_of(choices.begin(),choices.end(),[&](const ExperienceChoice& item){
            return item.move.r==r&&item.move.c==c;
        }))return {};
        choices.push_back({{r,c},nonlosses,games,uncovered});
    }
    return table;
}

// The book is a fixed training artifact, never a live results reader. Each
// version is loaded once; the acceptance runner supplies an immutable copy.
inline std::vector<ExperienceChoice> experience_actions(const std::string& path,
        const int board[kMaxBoard][kMaxBoard],int size,int player,EngineVersion opponent) {
    if(path.empty()||size!=15)return {};
    static std::mutex mutex;
    static std::unordered_map<std::string,std::shared_ptr<const ExperienceTable>> versions;
    std::shared_ptr<const ExperienceTable> table;
    {
        std::lock_guard<std::mutex> lock(mutex);
        auto found=versions.find(path);
        if(found==versions.end())found=versions.emplace(path,
            std::make_shared<const ExperienceTable>(read_experience(path))).first;
        table=found->second;
    }
    const auto found=table->find(experience_key(static_cast<int>(opponent),player,experience_state(board,size)));
    return found==table->end()?std::vector<ExperienceChoice>{}:found->second;
}

inline ExperienceChoice best_experience_choice(const std::vector<ExperienceChoice>& choices,bool require_covered=true) {
    ExperienceChoice best;
    for(const auto& choice:choices) {
        if(!choice.nonloss_weight||(require_covered&&choice.uncovered_replies>0))continue;
        const long long left=(choice.nonloss_weight+1LL)*(best.total_weight+2LL);
        const long long right=(best.nonloss_weight+1LL)*(choice.total_weight+2LL);
        if(best.move.r<0||left>right||(left==right&&choice.total_weight>best.total_weight))best=choice;
    }
    return best;
}

// Explore alternatives to empirical proposals that repeatedly failed against
// another observed reply. An empty list means unrestricted search, including
// when excluding every legal move would leave no alternative.
inline std::vector<Move> experience_root_moves(const std::vector<ExperienceChoice>& choices,
        int board[kMaxBoard][kMaxBoard],int size,int player) {
    if(std::none_of(choices.begin(),choices.end(),[](const ExperienceChoice& c){return c.uncovered_replies>0;}))return {};
    std::vector<Move> allowed;bool excluded=false;
    for(int r=0;r<size;++r)for(int c=0;c<size;++c) {
        if(!is_legal_move(board,size,player,{r,c}))continue;
        const bool fragile=std::any_of(choices.begin(),choices.end(),[&](const ExperienceChoice& choice){
            // At a fork under exploration, also move beyond alternatives
            // that accumulated repeated failures without a single success.
            // Direct evidence in these states has weight four per episode.
            return (choice.uncovered_replies>0||
                (choice.nonloss_weight==0&&choice.total_weight>=8))&&choice.move.r==r&&choice.move.c==c;
        });
        if(fragile)excluded=true;else allowed.push_back({r,c});
    }
    return excluded?allowed:std::vector<Move>{};
}

inline ExperienceChoice lookup_experience(const std::string& path,
        const int board[kMaxBoard][kMaxBoard],int size,int player,EngineVersion opponent) {
    return best_experience_choice(experience_actions(path,board,size,player,opponent));
}

// A bounded empirical ordering penalty, not a proof that the move loses. An
// unobserved action receives zero penalty, allowing search to explore beyond
// a previously failed continuation instead of replaying it indefinitely.
inline int experience_loss_penalty(const std::vector<ExperienceChoice>& choices,Move move) {
    for(const auto& choice:choices)if(choice.move.r==move.r&&choice.move.c==move.c) {
        const long long excess=choice.total_weight-2LL*choice.nonloss_weight;
        return excess>0?static_cast<int>(40LL*excess/(choice.total_weight+2LL)):0;
    }
    return 0;
}
} // namespace gomoku
