#include <filesystem>
#include <iostream>
#include "engine_adaptive.h"

int main() {
    using namespace gomoku;
    int failures=0;
    auto check=[&](bool value,const char* label){
        std::cout<<(value?"[PASS] ":"[FAIL] ")<<label<<'\n';if(!value)++failures;
    };
    const auto folder=std::filesystem::temp_directory_path()/
        ("gomoku-experience-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(folder);
    int board[kMaxBoard][kMaxBoard]{};board[7][7]=1;
    const auto book=folder/"book.txt";
    {
        std::ofstream output(book);
        output<<"GOMOKU_EXPERIENCE_V1\n4 2 "<<experience_state(board,15)<<" 8 8 3 4\n";
    }
    const auto remembered=lookup_experience(book.string(),board,15,2,EngineVersion::Rapfi2025);
    check(remembered.move.r==8&&remembered.move.c==8&&remembered.nonloss_weight==3&&remembered.total_weight==4,
        "experience lookup retains the learned move and its evidence counts");
    check(lookup_experience(book.string(),board,15,2,EngineVersion::AlphaGomoku2026).move.r<0,
        "opponent profiles cannot silently share unrelated observations");
    board[0][0]=2;
    check(lookup_experience(book.string(),board,15,2,EngineVersion::Rapfi2025).move.r<0,
        "a different board cannot match a recorded position");board[0][0]=0;
    const auto malformed=folder/"malformed.txt";
    {
        std::ofstream output(malformed);
        output<<"GOMOKU_EXPERIENCE_V1\n4 2 "<<experience_state(board,15)<<" 8 8 3 4\n"
              <<"4 2 broken 99 99 1 1\n";
    }
    check(read_experience(malformed.string()).empty(),"a corrupt book fails closed instead of loading a partial table");
    const auto reinforced=folder/"reinforced.txt";
    {
        std::ofstream output(reinforced);
        output<<"GOMOKU_EXPERIENCE_V2\n4 2 "<<experience_state(board,15)<<" 8 8 0 8\n"
              <<"4 2 "<<experience_state(board,15)<<" 8 7 2 2\n";
    }
    const auto actions=experience_actions(reinforced.string(),board,15,2,EngineVersion::Rapfi2025);
    check(actions.size()==2&&lookup_experience(reinforced.string(),board,15,2,EngineVersion::Rapfi2025).move.c==7,
        "reinforcement retains both failed actions and successful alternatives");
    check(experience_loss_penalty(actions,{8,8})>0&&experience_loss_penalty(actions,{8,6})==0,
        "an unobserved continuation is not assigned a fabricated loss");
    const auto branched=folder/"branched.txt";
    {
        std::ofstream output(branched);
        output<<"GOMOKU_EXPERIENCE_V3\n4 2 "<<experience_state(board,15)<<" 8 8 4 12 1\n"
              <<"4 2 "<<experience_state(board,15)<<" 8 7 2 8 0\n"
              <<"5 2 "<<experience_state(board,15)<<" 8 8 4 12 1\n"
              <<"5 2 "<<experience_state(board,15)<<" 8 7 2 8 0\n";
    }
    const auto branches=experience_actions(branched.string(),board,15,2,EngineVersion::Rapfi2025);
    check(branches.size()==2&&branches[0].uncovered_replies==1&&best_experience_choice(branches).move.c==7,
        "a known successful line cannot hide repeated failures against another observed reply");
    check(best_experience_choice(branches,false).move.c==8,
        "positive-only selection can retain a demonstrated parent while its unresolved reply is researched");
    const auto allowed=experience_root_moves(branches,board,15,2);
    check(allowed.size()==223&&std::none_of(allowed.begin(),allowed.end(),[](Move m){return m.r==8&&m.c==8;})&&
        std::any_of(allowed.begin(),allowed.end(),[](Move m){return m.r==8&&m.c==7;}),
        "empirical branch exploration retains other legal alternatives");
    check(experience_root_moves(actions,board,15,2).empty(),
        "legacy experience does not silently restrict root search");
    auto explored=branches;explored.push_back({{8,6},0,8,0});explored.push_back({{7,6},0,4,0});
    const auto unexplored=experience_root_moves(explored,board,15,2);
    check(unexplored.size()==222&&std::none_of(unexplored.begin(),unexplored.end(),[](Move m){return m.r==8&&m.c==6;})&&
        std::any_of(unexplored.begin(),unexplored.end(),[](Move m){return m.r==7&&m.c==6;}),
        "fork exploration moves beyond repeated failed alternatives while retaining a singly observed one");
    std::vector<ExperienceChoice> all_fragile;
    for(int r=0;r<15;++r)for(int c=0;c<15;++c)if(!board[r][c])all_fragile.push_back({{r,c},1,2,1});
    check(experience_root_moves(all_fragile,board,15,2).empty(),
        "empirical exploration cannot eliminate every legal root move");
    AnalyzedMove search;search.move={8,8};search.score=100;search.depth=12;search.has_score=true;
    search.candidates={{{8,7},80,12,true},{{0,0},1000,1,true},{{7,7},2000,12,true},{{8,6},-29990,12,true}};
    check(adjust_experience_search(search,actions,board,15,2,false)&&search.move.r==8&&search.move.c==7&&search.score==80,
        "failed-action penalties choose a searched alternative while rejecting stale and illegal lines");
    search.move={8,8};search.score=100;search.depth=12;search.mate=false;
    search.candidates={{{8,7},99,11,true}};
    check(!adjust_experience_search(search,actions,board,15,2,false)&&search.move.c==8,
        "a shallower optimistic alternative cannot displace a completed deeper AB result");
    search.move={8,8};search.score=100;search.depth=12;search.mate=false;
    search.candidates={{{5,7},99,12,true},{{8,7},80,12,true}};
    check(adjust_experience_search(search,actions,board,15,2,false,{7,7})&&search.move.r==8&&search.move.c==7,
        "opening exploration does not abandon a local reply for a distant unobserved move");
    search.move={8,8};search.score=29999;search.mate=true;
    check(!adjust_experience_search(search,actions,board,15,2,false)&&search.move.c==8,
        "empirical failures cannot override a positive mate result");
    const char* prior=std::getenv("GOMOKU_EXPERIENCE_BOOK");
    const bool had_prior=prior!=nullptr;const std::string saved=prior?prior:"";
#ifdef _WIN32
    _putenv_s("GOMOKU_EXPERIENCE_BOOK",book.string().c_str());
#else
    setenv("GOMOKU_EXPERIENCE_BOOK",book.string().c_str(),1);
#endif
    AdaptiveSearchConfig config;config.opponent=EngineVersion::Rapfi2025;
    auto result=choose_adaptive_move(board,15,{{7,7}},2,1,folder,config);
    check(result.source==EnsembleSource::ExperienceBook&&result.move.r==8&&result.move.c==8&&
        result.experts_launched==0&&!result.local_proved_win,
        "a learned proposal is usable without runtimes and is never reported as a win proof");
#ifdef _WIN32
    _putenv_s("GOMOKU_EXPERIENCE_BOOK",branched.string().c_str());
#else
    setenv("GOMOKU_EXPERIENCE_BOOK",branched.string().c_str(),1);
#endif
    config.rapfi_book_only=true;
    const auto positive_parent=choose_adaptive_move(board,15,{{7,7}},2,1,folder,config);
    check(positive_parent.source==EnsembleSource::ExperienceBook&&positive_parent.move.c==8&&
        positive_parent.experts_launched==0&&!positive_parent.local_proved_win,
        "Rapfi positive-only routing keeps a successful proposal without claiming a win certificate");
    config.opponent=EngineVersion::AlphaGomoku2026;
    const auto unaffected_alpha=choose_adaptive_move(board,15,{{7,7}},2,1,folder,config);
    check(unaffected_alpha.source==EnsembleSource::ExperienceBook&&unaffected_alpha.move.c==7,
        "the Rapfi-only policy does not change Alpha's branch-aware selection");
    config.rapfi_book_only=false;config.opponent=EngineVersion::Rapfi2025;
    int alpha_board[kMaxBoard][kMaxBoard]{};
    alpha_board[7][7]=1;alpha_board[7][8]=2;alpha_board[6][7]=1;
    const auto alpha_book=folder/"alpha-preserved.txt";
    {
        std::ofstream output(alpha_book);
        output<<"GOMOKU_EXPERIENCE_V2\n5 2 "<<experience_state(alpha_board,15)<<" 8 8 1 8\n"
              <<"4 2 "<<experience_state(alpha_board,15)<<" 8 8 1 8\n";
    }
#ifdef _WIN32
    _putenv_s("GOMOKU_EXPERIENCE_BOOK",alpha_book.string().c_str());
#else
    setenv("GOMOKU_EXPERIENCE_BOOK",alpha_book.string().c_str(),1);
#endif
    config.legacy_experience=true;config.mcts=true;config.opponent=EngineVersion::AlphaGomoku2026;
    const auto preserved=choose_adaptive_move(alpha_board,15,{{7,7},{7,8},{6,7}},2,1,folder,config);
    check(preserved.source==EnsembleSource::ExperienceBook&&preserved.move.r==8&&preserved.move.c==8&&
        preserved.experts_launched==0,"the preserved Alpha policy retains its original positive-book routing");
    config.legacy_experience=false;config.mcts=false;config.opponent=EngineVersion::Rapfi2025;
    const auto demonstrated=choose_adaptive_move(alpha_board,15,{{7,7},{7,8},{6,7}},2,1,folder,config);
    check(demonstrated.source==EnsembleSource::ExperienceBook&&demonstrated.move.r==8&&demonstrated.move.c==8&&
        demonstrated.experts_launched==0&&!demonstrated.local_proved_win,
        "weighted failures do not discard a demonstrated midgame continuation or turn it into a proof");
#ifdef _WIN32
    _putenv_s("GOMOKU_EXPERIENCE_BOOK",book.string().c_str());
#else
    setenv("GOMOKU_EXPERIENCE_BOOK",book.string().c_str(),1);
#endif
    config.legacy_experience=false;config.mcts=false;config.opponent=EngineVersion::Rapfi2025;
    for(int c=3;c<7;++c)board[5][c]=2;
    result=choose_adaptive_move(board,15,{{7,7}},2,1,folder,config);
    check(result.source==EnsembleSource::LocalTactics&&result.move.r==5,
        "an immediate winning move outranks learned experience");
    const std::atomic<bool> cancelled{true};
    result=choose_adaptive_move(board,15,{{7,7}},2,1,folder,config,&cancelled);
    check(result.move.r<0,"cancellation also prevents learned moves");
#ifdef _WIN32
    _putenv_s("GOMOKU_EXPERIENCE_BOOK",had_prior?saved.c_str():"");
#else
    if(had_prior)setenv("GOMOKU_EXPERIENCE_BOOK",saved.c_str(),1);else unsetenv("GOMOKU_EXPERIENCE_BOOK");
#endif
    std::filesystem::remove_all(folder);
    return failures?1:0;
}
