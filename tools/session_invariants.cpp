#include <fstream>
#include <iostream>
#include <tuple>
#include "engine_session.h"

using namespace gomoku;

// Run this executable under a different filename to provide a deterministic
// child process. This exercises the same pipes/lifecycle on Linux and Windows.
int fake_engine(const std::string& mode) {
    int size=15,requests=0,nbest=1;
    std::vector<std::tuple<int,int,int>> stones;
    std::vector<Move> blocked;
    auto answer=[&] {
        ++requests;
        if (mode.find("exit")!=std::string::npos) std::exit(0);
        if (mode.find("stall")!=std::string::npos) std::this_thread::sleep_for(std::chrono::seconds(30));
        if (mode.find("illegal")!=std::string::npos) {std::cout<<"99,99"<<std::endl;return;}
        if (mode.find("foul")!=std::string::npos) {std::cout<<"7,7"<<std::endl;return;}
        int board[kMaxBoard][kMaxBoard]{};
        const int self=stones.empty()||std::get<2>(stones.front())==1?1:2;
        for (auto [r,c,side]:stones) board[r][c]=side==1?self:3-self;
        Move move{-1,-1};
        for (int r=0;r<size&&move.r<0;++r) for (int c=0;c<size;++c) {
            if (std::any_of(blocked.begin(),blocked.end(),[&](Move m){return m.r==r&&m.c==c;})) continue;
            if (is_legal_move(board,size,self,{r,c})) {move={r,c};break;}
        }
        if(mode.find("council")!=std::string::npos) {
            for(int i=0;i<std::min(nbest,2);++i)
                std::cout<<"INFO PV "<<i<<"\nINFO NUMPV "<<std::min(nbest,2)
                    <<"\nINFO DEPTH 8\nINFO EVAL "<<100+100*i<<"\nINFO BESTLINE "<<i<<",0\nINFO PV DONE\n";
            std::cout<<"0,0"<<std::endl;return;
        }
        if(mode.find("multipv")!=std::string::npos) {
            std::cout<<"INFO PV 0\nINFO NUMPV 2\nINFO EVAL 135\nINFO SELDEPTH 42\nINFO BESTLINE "
                <<move.c<<","<<move.r<<"\nINFO PV DONE\n"
                <<"INFO PV 1\nINFO NUMPV 2\nINFO EVAL -900\nINFO SELDEPTH 88\nINFO BESTLINE 1,0\nINFO PV DONE\n"
                <<move.c<<","<<move.r<<std::endl;
            return;
        }
        std::cout<<"INFO DEPTH "<<requests<<"\nINFO EVAL "<<100+requests<<"\n"
                 <<move.c<<","<<move.r<<std::endl;
    };
    std::string line;
    while (std::getline(std::cin,line)) {
        if (line.rfind("START ",0)==0) {size=std::stoi(line.substr(6));std::cout<<"OK"<<std::endl;}
        else if (line=="ABOUT") std::cout<<"name=\"fake\""<<std::endl;
        else if (line=="YXBLOCKRESET") blocked.clear();
        else if (line=="BOARD"||line=="YXBOARD") {
            const bool search=line=="BOARD";stones.clear();
            while (std::getline(std::cin,line)&&line!="DONE") {
                int r,c,s;if(std::sscanf(line.c_str(),"%d,%d,%d",&c,&r,&s)==3) stones.emplace_back(r,c,s);
            }
            if (search) answer();
        } else if(line=="YXBLOCK") {
            while(std::getline(std::cin,line)&&line!="DONE") {
                int r,c;if(std::sscanf(line.c_str(),"%d,%d",&c,&r)==2) blocked.push_back({r,c});
            }
        } else if(line.rfind("YXNBEST",0)==0) {std::sscanf(line.c_str(),"YXNBEST %d",&nbest);answer();}
        else if(line=="END") return 0;
    }
    return 0;
}

int main(int argc,char** argv) {
    const auto executable=std::filesystem::absolute(argv[0]);
    const std::string name=executable.filename().string();
    if (name.find("fake-retained")!=std::string::npos) return fake_engine(name);
    int failures=0;
    auto check=[&](bool value,const char* message) {
        std::printf("[%s] %s\n",value?"PASS":"FAIL",message);if(!value)++failures;
    };
#ifndef _WIN32
    int disconnected[2];
    if (!create_external_pipe(disconnected)) return 2;
    check((fcntl(disconnected[0], F_GETFD) & FD_CLOEXEC) &&
          (fcntl(disconnected[1], F_GETFD) & FD_CLOEXEC),
          "expert pipe descriptors cannot leak through a concurrent exec");
    close(disconnected[0]);
    check(!write_external_pipe(disconnected[1], "END\n", 4),
          "a disconnected expert pipe returns failure without terminating the host");
    close(disconnected[1]);
#endif
    int score;bool mate;
    check(parse_search_score("-216",score,mate)&&score==-216&&!mate,"numeric search values");
    check(parse_search_score("+M13",score,mate)&&score==29987&&mate,"positive mate distance");
    check(parse_search_score("-M*",score,mate)&&score==-30000&&mate,"negative database mate");
    check(!parse_search_score("X_M*",score,mate)&&!parse_search_score("VAL_NONE",score,mate),"unknown/malformed values remain unknown");
    AnalyzedMove info;
    parse_search_info("INFO EVAL -250",info);
    parse_search_info("MESSAGE Speed 200K | Depth 23-30 | Eval -97 | Time 1s",info);
    check(info.has_score&&info.score==-97&&info.depth==23,"final best-thread summary supersedes earlier main-thread information");
    const auto unique=std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto folder=std::filesystem::temp_directory_path()/("gomoku-session-"+unique);
    std::filesystem::create_directory(folder);
    auto program=[&](const std::string& suffix) {
        auto path=folder/("fake-retained-"+suffix+
#ifdef _WIN32
            ".exe"
#else
            ""
#endif
        );
        std::filesystem::copy_file(executable,path);
        std::filesystem::permissions(path,std::filesystem::status(executable).permissions());
        return path;
    };
    {
        RetainedEngine engine;
        ExternalEngineRequest request;
        request.program=program("normal");request.moves={{7,7}};request.player=2;
        request.turn_time_ms=200;request.threads=1;
        auto first=engine.think(request);
        check(first.move.r==0&&first.move.c==0&&first.score==101,"first query returns legal analyzed move");
        request.moves.push_back(first.move);request.moves.push_back({0,1});
        auto second=engine.think(request);
        check(second.score==102,"chronological continuation reuses the same process");
        auto repeated=engine.think(request,{{3,4}});
        check(repeated.score==103&&repeated.move.r==3&&repeated.move.c==4,"root-restricted verification keeps the table and respects its candidate");
        request.moves={{3,3}};
        auto reset=engine.think(request);
        check(reset.score==101,"new or divergent game resets the process");
        request.prefer_nonloss=true;
        auto objective=engine.think(request);
        check(objective.score==101,"changed draw utility resets incompatible transposition bounds");
        check(engine.think(request).score==102,"unchanged non-loss utility retains search state");
        request.program=program("multipv");
        const auto multipv=engine.think(request);
        check(multipv.score==135&&multipv.depth==42&&multipv.candidates.size()==2,
            "MCTS analysis uses the returned move's PV rather than the last printed alternative");
        request.program=program("council");
        const auto council=engine.think(request,{{0,0},{0,1}},2);
        check(council.error.empty()&&council.completed_depth==8&&council.completed_candidates.size()==2&&
            council.completed_candidates[1].score==200,
            "root restrictions retain the requested MultiPV count and complete-depth evidence");
        request.program=program("illegal");
        auto illegal=engine.think(request);
        check(illegal.move.r<0&&!illegal.error.empty(),"illegal child replies cannot become engine moves");
        request.program=program("exit");
        auto exited=engine.think(request);
        check(exited.move.r<0&&!exited.error.empty(),"child exit is a recoverable error");
        request.program=program("stall");
        std::atomic<bool> stop{false};request.cancel=&stop;
        std::thread cancel([&]{std::this_thread::sleep_for(std::chrono::milliseconds(100));stop=true;});
        const auto started=std::chrono::steady_clock::now();
        auto cancelled=engine.think(request);cancel.join();
        const auto duration=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count();
        check(cancelled.move.r<0&&cancelled.error.empty()&&duration<2000,"cancellation stops and reaps a stalled child");
    }
    {
        ExternalEngineRequest request;
        request.program=program("foul");request.player=1;request.turn_time_ms=200;
        request.moves={{7,4},{0,0},{7,6},{0,2},{7,8},{0,4},{7,10},{0,6}};
        std::string error;
        auto strict=run_external_engine(request,error);
        check(strict.r<0&&!error.empty(),"engine selection rejects a forbidden black move");
        request.referee_accept_black_foul=true;
        auto attempted=run_external_engine(request,error);
        check(attempted.r==7&&attempted.c==7&&error.empty()&&!legal_external_move(request,attempted),
              "referee receives the attempted foul without making it a legal move");
        check(!accepted_external_reply(request,{0,0})&&!accepted_external_reply(request,{-1,-1}),
              "occupied cells and protocol failures remain invalid, never rule wins");
    }
    std::filesystem::remove_all(folder);
    return failures?1:0;
}
