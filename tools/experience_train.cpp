#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include "engine_experience.h"

using namespace gomoku;
struct Observation { std::string state;Move move;int ply=0; };
struct Totals {int nonloss=0,games=0;};
int main(int argc,char** argv) {
    if(argc<3){std::cerr<<"usage: experience_train NEW_BOOK [--preserve-alpha FROZEN_BOOK] STAR_LOG...\n";return 2;}
    const std::filesystem::path output=argv[1];
    if(std::filesystem::exists(output)){std::cerr<<"refusing to overwrite a frozen book\n";return 2;}
    std::map<std::string,std::map<std::pair<int,int>,Totals>> observations;
    std::map<std::string,std::map<std::pair<int,int>,Totals>> direct_observations;
    std::map<std::string,std::map<std::pair<int,int>,std::map<std::string,Totals>>> reply_observations;
    std::map<std::string,std::vector<ExperienceChoice>> frozen_alpha;
    std::vector<std::string> logs;
    std::string alpha_path;
    for(int argument=2;argument<argc;++argument) {
        if(std::string(argv[argument])=="--preserve-alpha") {
            if(!alpha_path.empty()||argument+1>=argc)return 2;
            alpha_path=argv[++argument];
        } else logs.push_back(argv[argument]);
    }
    if(!alpha_path.empty()) {
        const auto prior=read_experience(alpha_path);
        bool positive=false;
        for(const auto& state:prior)if(state.first.rfind("5:",0)==0) {
            frozen_alpha.emplace(state.first,state.second);
            for(const auto& choice:state.second)positive=positive||choice.nonloss_weight>0;
        }
        if(!positive){std::cerr<<"preserved Alpha book has no positive Alpha proposals\n";return 2;}
    }
    std::set<std::string> seen;
    int games=0,skipped=0;
    std::ostringstream sources;
    for(const auto& filename:logs) {
        std::ifstream input(filename);std::string line,encoded;
        int index=-1,opponent=-1,sr=-1,sc=-1;
        while(std::getline(input,line)) {
            if(line.rfind("star_case=",0)==0)
                std::sscanf(line.c_str(),"star_case=%d opponent=%d start=%d,%d",&index,&opponent,&sr,&sc);
            if(line.rfind("replay_case=",0)==0) {
                std::sscanf(line.c_str(),"replay_case=%d opponent=%d",&index,&opponent);
                if(index>=0&&index<18){sr=3+4*(index/2/3);sc=3+4*(index/2%3);}
            }
            if(line.rfind("moves=",0)==0)encoded=line.substr(6);
        }
        if(index<0||index>=18||(opponent!=4&&opponent!=5)||
           sr!=3+4*(index/2/3)||sc!=3+4*(index/2%3)||encoded.empty()){++skipped;continue;}
        const int player=index%2+1;
        int board[kMaxBoard][kMaxBoard]{};int turn=1,winner=-1,plies=0;
        bool valid=true,invalid_terminal=false;std::vector<Observation> pending;
        std::istringstream history(encoded);std::string token,identity;
        while(history>>token) {
            int r,c,consumed=0;
            if(std::sscanf(token.c_str(),"%d,%d%n",&r,&c,&consumed)!=2||
               consumed!=static_cast<int>(token.size())||!ok(15,r,c)||board[r][c]||
               (plies==0&&(r!=sr||c!=sc))){valid=false;break;}
            identity+=token+" ";
            if(turn==1&&is_forbidden_move(board,15,r,c)){winner=2;break;}
            // The prescribed initial Black stone is not a learned decision.
            if(turn==player&&plies>0)pending.push_back({experience_state(board,15),{r,c},plies});
            board[r][c]=turn;++plies;
            if(check_win_at(board,15,r,c,turn)){winner=turn;break;}
            if(plies==225){winner=0;break;}
            turn=3-turn;
        }
        if(valid&&winner<0&&first_legal_move(board,15,turn).r<0) {
            winner=3-turn;invalid_terminal=true;
        }
        const std::string unique=std::to_string(opponent)+":"+std::to_string(player)+":"+identity;
        if(!valid||winner<0||!seen.insert(unique).second){++skipped;continue;}
        ++games;
        const bool goal_nonloss=!invalid_terminal&&(winner==0||winner==player);
        for(size_t observation=0;observation<pending.size();++observation) {
            const auto& item=pending[observation];
            const auto key=experience_key(opponent,player,item.state);
            const std::string next=observation+1<pending.size()?pending[observation+1].state:
                "terminal:"+std::to_string(goal_nonloss);
            auto& branch=reply_observations[key][{item.move.r,item.move.c}][next];
            ++branch.games;if(goal_nonloss)++branch.nonloss;
            {
                auto& direct=direct_observations[experience_key(opponent,player,item.state)][{item.move.r,item.move.c}];
                direct.games+=4;if(goal_nonloss)direct.nonloss+=4;
            }
            for(int symmetry=0;symmetry<8;++symmetry) {
                std::string transformed(225,'0');
                for(int r=0;r<15;++r)for(int c=0;c<15;++c) {
                    const auto point=symmetry_move({r,c},15,symmetry);
                    transformed[point.r*15+point.c]=item.state[r*15+c];
                }
                const auto move=symmetry_move(item.move,15,symmetry);
                const int weight=symmetry==0?4:1;
                auto& total=observations[experience_key(opponent,player,transformed)][{move.r,move.c}];
                total.games+=weight;if(goal_nonloss)total.nonloss+=weight;
            }
        }
        sources<<filename<<'\t'<<opponent<<'\t'<<index<<'\t'<<winner<<'\t'<<plies<<'\t'<<goal_nonloss<<'\n';
    }
    // Opponent move-order biases are not rotation-invariant. When a position
    // has an actual successful observation, prefer its directly played moves
    // to a more optimistic but unplayed symmetry of a losing continuation.
    for(const auto& direct:direct_observations)
        if(std::any_of(direct.second.begin(),direct.second.end(),[](const auto& move){return move.second.nonloss>0;}))
            observations[direct.first]=direct.second;
    if(!output.parent_path().empty())std::filesystem::create_directories(output.parent_path());
    std::ofstream book(output);if(!book)return 2;
    book<<"GOMOKU_EXPERIENCE_V3\n";int entries=0,fragile_actions=0;
    for(const auto& state:observations) {
        if(!frozen_alpha.empty()&&state.first.rfind("5:",0)==0)continue;
        for(const auto& choice:state.second) {
            const auto& key=state.first;const auto& score=choice.second;
            int uncovered=0;bool successful_reply=false;
            const auto direct=reply_observations.find(key);
            if(direct!=reply_observations.end()) {
                const auto action=direct->second.find(choice.first);
                if(action!=direct->second.end())for(const auto& branch:action->second) {
                    if(branch.second.nonloss>0)successful_reply=true;
                    else if(branch.second.games>=2)++uncovered;
                }
            }
            if(!successful_reply)uncovered=0;
            if(uncovered>0)++fragile_actions;
            book<<key[0]<<' '<<key[2]<<' '<<key.substr(4)<<' '<<choice.first.first<<' '<<choice.first.second<<' '
                <<score.nonloss<<' '<<score.games<<' '<<uncovered<<'\n';++entries;
        }
    }
    for(const auto& state:frozen_alpha) {
        const auto& key=state.first;
        for(const auto& choice:state.second) {
            book<<"5 "<<key[2]<<' '<<key.substr(4)<<' '<<choice.move.r<<' '<<choice.move.c<<' '
                <<choice.nonloss_weight<<' '<<choice.total_weight<<' '<<choice.uncovered_replies<<'\n';++entries;
        }
    }
    book.close();std::ofstream provenance(output.string()+".training.tsv");provenance<<sources.str();
    std::cout<<"training_games="<<games<<" skipped_or_duplicate="<<skipped
        <<" positions="<<observations.size()<<" actions="<<entries<<" preserved_alpha="<<frozen_alpha.size()
        <<" fragile_actions="<<fragile_actions<<'\n';
    return entries?0:1;
}
