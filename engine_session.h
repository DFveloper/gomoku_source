#pragma once

// A retained Gomocup process keeps the transposition table between moves and
// between verification passes. BOARD reconstructs absolute colours from the
// chronological history; it does not clear Rapfi's search table.
#include "engine_adapter.h"
#include <cerrno>
#include <mutex>
#include <sstream>
#ifndef _WIN32
#include <fcntl.h>
#endif

namespace gomoku {

struct SearchLine { Move move{-1,-1};int score=0,depth=0;bool has_score=false; };
struct AnalyzedMove {
    Move move{-1,-1};
    int score = 0; // Rapfi value, positive for the side to move.
    int depth = 0;
    bool has_score = false;
    bool mate = false;
    int elapsed_ms = 0;
    std::string error;
    std::vector<SearchLine> candidates;
    SearchLine pending_line;
    bool in_pv=false;
    bool final_value=false;
    // Preserve a complete AB MultiPV iteration. Per-move latest scores may
    // otherwise mix a partial deeper iteration with earlier shallow lines.
    std::vector<SearchLine> completed_candidates;
    int completed_depth=0;
    std::vector<SearchLine> iteration_lines;
    int pending_pv=-1,expected_pvs=0;
    bool iteration_valid=false,pending_root_depth=false;
};

inline bool parse_search_score(std::string token, int& score, bool& mate) {
    mate = false;
    if (token.empty()) return false;
    const auto m = token.find('M');
    if (m != std::string::npos) {
        if (m!=1 || (token.front()!='+' && token.front()!='-')) return false;
        const bool negative = token.front() == '-';
        int distance = 0;
        if (token.substr(m+1) != "*") {
            char* end = nullptr;
            const long value = std::strtol(token.c_str()+m+1, &end, 10);
            if (end == token.c_str()+m+1 || *end || value < 0 || value > 500) return false;
            distance = static_cast<int>(value);
        }
        mate = true;
        score = (negative ? -1 : 1) * (30000-distance);
        return true;
    }
    char* end = nullptr;
    const long value = std::strtol(token.c_str(), &end, 10);
    if (end == token.c_str() || *end || value < -30000 || value > 30000) return false;
    score = static_cast<int>(value);
    return true;
}

inline void parse_search_info(const std::string& line, AnalyzedMove& result) {
    int pv=0;
    if (std::sscanf(line.c_str(),"INFO PV %d",&pv)==1) {
        result.pending_line=SearchLine{};result.in_pv=true;
        result.pending_pv=pv;result.pending_root_depth=false;
        if(pv==0) {result.iteration_lines.clear();result.expected_pvs=0;result.iteration_valid=true;}
    }
    int count=0;
    if(result.in_pv&&std::sscanf(line.c_str(),"INFO NUMPV %d",&count)==1) {
        if(count<1||count>64)result.iteration_valid=false;
        else if(result.expected_pvs==0&&result.pending_pv==0) {
            result.expected_pvs=count;result.iteration_lines.resize(count);
        } else if(count!=result.expected_pvs)result.iteration_valid=false;
    }
    // Final MESSAGE summaries may come from a stronger helper thread than the
    // main thread's last INFO PV. Consume them too, in arrival order.
    auto offset = line.find("INFO EVAL ");
    size_t length = 10;
    if (offset == std::string::npos) { offset=line.find("Eval "); length=5; }
    if (offset != std::string::npos) {
        std::istringstream input(line.substr(offset+length));
        std::string token; input >> token;
        int score; bool mate;
        if (parse_search_score(token,score,mate)) {
            result.score=score; result.mate=mate; result.has_score=true;
            if(line.find("MESSAGE Speed")!=std::string::npos)result.final_value=true;
            if (result.in_pv) {result.pending_line.score=score;result.pending_line.has_score=true;}
        }
    }
    int depth=0;
    if (std::sscanf(line.c_str(),"INFO DEPTH %d",&depth)==1) {
        result.depth=depth;if(result.in_pv) {result.pending_line.depth=depth;result.pending_root_depth=true;}
    }
    if(result.in_pv && result.pending_line.depth==0 &&
        std::sscanf(line.c_str(),"INFO SELDEPTH %d",&depth)==1)result.pending_line.depth=depth;
    offset=line.find("Depth ");
    if (offset!=std::string::npos && std::sscanf(line.c_str()+offset,"Depth %d",&depth)==1)
        result.depth=depth;
    int x,y;
    if (result.in_pv && std::sscanf(line.c_str(),"INFO BESTLINE %d,%d",&x,&y)==2)
        result.pending_line.move={y,x};
    if (line.rfind("INFO PV DONE",0)==0) {
        auto candidate=result.pending_line;
        if (candidate.move.r>=0&&candidate.has_score) {
            auto found=std::find_if(result.candidates.begin(),result.candidates.end(),[&](const SearchLine& p){
                return p.move.r==candidate.move.r&&p.move.c==candidate.move.c;
            });
            if(found==result.candidates.end()) result.candidates.push_back(candidate);
            else if(candidate.depth>=found->depth) *found=candidate;
        }
        if(result.iteration_valid&&result.pending_root_depth&&candidate.has_score&&candidate.move.r>=0&&
           result.pending_pv>=0&&result.pending_pv<result.expected_pvs) {
            auto& slot=result.iteration_lines[result.pending_pv];
            if(slot.has_score)result.iteration_valid=false;
            else slot=candidate;
            const int common_depth=result.iteration_lines.front().depth;
            bool complete=result.iteration_valid&&common_depth>0;
            for(size_t i=0;i<result.iteration_lines.size();++i) {
                const auto& item=result.iteration_lines[i];
                complete=complete&&item.has_score&&item.depth==common_depth;
                for(size_t j=0;j<i;++j)complete=complete&&
                    (item.move.r!=result.iteration_lines[j].move.r||item.move.c!=result.iteration_lines[j].move.c);
            }
            if(complete&&common_depth>=result.completed_depth) {
                result.completed_depth=common_depth;result.completed_candidates=result.iteration_lines;
            }
        } else result.iteration_valid=false;
        result.in_pv=false;
    }
}

class RetainedEngine {
public:
    explicit RetainedEngine(std::string searcher={}) : searcher_(std::move(searcher)) {}
    RetainedEngine(const RetainedEngine&) = delete;
    RetainedEngine& operator=(const RetainedEngine&) = delete;
    ~RetainedEngine() { close_process(); }

    AnalyzedMove think(const ExternalEngineRequest& request, const std::vector<Move>& allowed = {}, int multipv=1) {
        std::lock_guard<std::mutex> lock(mutex_);
        AnalyzedMove result;
        const auto started=std::chrono::steady_clock::now();
        auto cancelled=[&] { return request.cancel && request.cancel->load(); };
        if (cancelled()) return result;
        std::error_code ec;
        const auto program=std::filesystem::absolute(request.program,ec);
        const bool continuation = previous_.size() <= request.moves.size() &&
            std::equal(previous_.begin(),previous_.end(),request.moves.begin(),
                [](Move a,Move b){ return a.r==b.r && a.c==b.c; });
        if (ec || program.empty() || !std::filesystem::is_regular_file(program,ec)) {
            result.error="engine runtime binary not found"; return result;
        }
        if (!alive() || program_!=program || board_size_!=request.board_size || !continuation ||
            utility_player_!=request.player || prefer_nonloss_!=request.prefer_nonloss)
            close_process();
        if (!alive()) {
            program_=program; board_size_=request.board_size;
            utility_player_=request.player;prefer_nonloss_=request.prefer_nonloss;
            if (!open_process()) { result.error="persistent engine launch failed"; return result; }
            if (!send("START "+std::to_string(board_size_)+
                      "\nINFO rule 4\n"+(searcher_.empty()?"":"INFO search_type "+searcher_+"\n")+
                      "INFO show_detail 2\n")) {
                result.error="engine initialization failed"; close_process(); return result;
            }
        }
        // A move is flushed just before Rapfi clears its `thinking` flag.
        // ABOUT is an argument-free protocol barrier; retrying it cannot leave
        // option arguments in the command stream if the callback is still busy.
        bool idle=false;
        const auto ready_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        std::string readiness;
        while (!cancelled() && std::chrono::steady_clock::now()<ready_deadline) {
            if (!send("ABOUT\n")) break;
            const auto attempt=std::chrono::steady_clock::now()+std::chrono::milliseconds(30);
            while (std::chrono::steady_clock::now()<attempt && !cancelled()) {
                std::string chunk;
                const int state=read_available(chunk);
                if (state<0) break;
                readiness+=chunk;
                if (readiness.find("name=")!=std::string::npos) {idle=true;break;}
            }
            if (idle) break;
        }
        if (!idle) {
            result.error=cancelled()?"":"persistent engine did not become ready";
            close_process();return result;
        }
        const int budget = request.turn_time_ms > 0 ? request.turn_time_ms : 5000;
        std::string input="INFO timeout_turn "+std::to_string(budget)+
            "\nINFO timeout_match 3600000\nINFO time_left 3600000\nINFO thread_num "+
            std::to_string(std::max(1,request.threads))+"\nYXBLOCKRESET\n";
        if(request.prefer_nonloss)
            input+="INFO evaluator_draw_ratio 0\nINFO evaluator_draw_black_winrate "+
                std::to_string(request.player==1?1:0)+"\nINFO draw_result "+std::to_string(request.player)+"\n";
        input+=(allowed.empty()&&multipv==1?"BOARD\n":"YXBOARD\n");
        for (size_t i=0;i<request.moves.size();++i) {
            const Move m=request.moves[i];
            input+=std::to_string(m.c)+","+std::to_string(m.r)+","+
                (static_cast<int>(i%2+1)==request.player ? "1" : "2")+"\n";
        }
        input+="DONE\n";
        if (!allowed.empty()) {
            int board[kMaxBoard][kMaxBoard];external_request_board(request,board);
            input+="YXBLOCK\n";
            for (int r=0;r<request.board_size;++r) for (int c=0;c<request.board_size;++c) {
                if (board[r][c]) continue;
                if (std::any_of(allowed.begin(),allowed.end(),[&](Move m){return m.r==r&&m.c==c;})) continue;
                input+=std::to_string(c)+","+std::to_string(r)+"\n";
            }
            input+="DONE\nYXNBEST "+std::to_string(std::clamp(multipv,1,8))+"\n";
        } else if(multipv>1) input+="YXNBEST "+std::to_string(std::min(8,multipv))+"\n";
        if (!send(input)) { result.error="persistent engine write failed"; close_process(); return result; }
        const auto deadline=started+std::chrono::milliseconds(budget+10000);
        std::string pending;
        while (!cancelled() && std::chrono::steady_clock::now()<deadline) {
            std::string chunk;
            const int state=read_available(chunk);
            if (state<0) { result.error="persistent engine exited without a move"; break; }
            pending+=chunk;
            size_t newline;
            while ((newline=pending.find('\n'))!=std::string::npos) {
                std::string line=pending.substr(0,newline); pending.erase(0,newline+1);
                parse_search_info(line,result);
                Move candidate;
                if (parse_external_move_line(line,candidate)) {
                    if (!legal_external_move(request,candidate)) {
                        result.error="persistent engine returned an illegal move";
                        close_process(); return result;
                    }
                    if (!allowed.empty() && !std::any_of(allowed.begin(),allowed.end(),
                        [&](Move m){return m.r==candidate.r&&m.c==candidate.c;})) {
                        result.error="engine ignored the root restriction";
                        close_process();return result;
                    }
                    result.move=candidate;
                    if(!result.final_value) {
                        auto chosen=std::find_if(result.candidates.begin(),result.candidates.end(),[&](const SearchLine& pv){
                            return pv.move.r==candidate.r&&pv.move.c==candidate.c;
                        });
                        if(chosen!=result.candidates.end()) {
                            result.score=chosen->score;result.depth=chosen->depth;result.has_score=true;
                            result.mate=std::abs(chosen->score)>=29500;
                        }
                    }
                    previous_=request.moves;
                    result.elapsed_ms=static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now()-started).count());
                    return result;
                }
            }
            if (state==0) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        if (!cancelled() && result.error.empty()) result.error="persistent engine response timed out";
        close_process();
        return result;
    }

private:
    std::mutex mutex_;
    std::string searcher_;
    std::filesystem::path program_;
    int board_size_=0;
    int utility_player_=0;
    bool prefer_nonloss_=false;
    std::vector<Move> previous_;
#ifdef _WIN32
    HANDLE process_=nullptr, input_=nullptr, output_=nullptr;
    bool alive() const { return process_ && WaitForSingleObject(process_,0)==WAIT_TIMEOUT; }
    bool open_process() {
        SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES),nullptr,TRUE};
        HANDLE child_input=nullptr, child_output=nullptr;
        if (!CreatePipe(&child_input,&input_,&attributes,0)) return false;
        if (!CreatePipe(&output_,&child_output,&attributes,0)) {
            CloseHandle(child_input); CloseHandle(input_); input_=nullptr; return false;
        }
        SetHandleInformation(input_,HANDLE_FLAG_INHERIT,0);
        SetHandleInformation(output_,HANDLE_FLAG_INHERIT,0);
        STARTUPINFOA startup{};startup.cb=sizeof(startup);
        startup.dwFlags=STARTF_USESTDHANDLES;
        startup.hStdInput=child_input;startup.hStdOutput=startup.hStdError=child_output;
        PROCESS_INFORMATION process{};
        std::string command="\""+program_.string()+"\"";
        const auto directory=program_.parent_path().string();
        const bool ok=CreateProcessA(nullptr,command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,
                                    nullptr,directory.c_str(),&startup,&process)!=FALSE;
        CloseHandle(child_input); CloseHandle(child_output);
        if (!ok) { close_process(); return false; }
        CloseHandle(process.hThread);process_=process.hProcess;return true;
    }
    bool send(const std::string& text) {
        DWORD count=0;
        return input_ && WriteFile(input_,text.data(),static_cast<DWORD>(text.size()),&count,nullptr)
            && count==text.size();
    }
    int read_available(std::string& output) {
        DWORD available=0;
        if (!PeekNamedPipe(output_,nullptr,0,nullptr,&available,nullptr)) return -1;
        if (!available) return alive()?0:-1;
        char buffer[8192];DWORD count=0;
        if (!ReadFile(output_,buffer,std::min<DWORD>(available,sizeof(buffer)),&count,nullptr)) return -1;
        output.assign(buffer,count);return count?1:-1;
    }
    void close_process() {
        if (process_) {
            if (alive()) TerminateProcess(process_,1);
            WaitForSingleObject(process_,2000);CloseHandle(process_);process_=nullptr;
        }
        if (input_) {CloseHandle(input_);input_=nullptr;}
        if (output_) {CloseHandle(output_);output_=nullptr;}
        previous_.clear();
    }
#else
    pid_t pid_=-1;
    int input_=-1, output_=-1;
    bool alive() const { return pid_>0; }
    bool open_process() {
        int in[2],out[2];
        if (!create_external_pipe(in)) return false;
        if (!create_external_pipe(out)) {::close(in[0]);::close(in[1]);return false;}
        const std::string program=program_.string(),directory=program_.parent_path().string();
        pid_=fork();
        if (pid_==0) {
            dup2(in[0],STDIN_FILENO);dup2(out[1],STDOUT_FILENO);dup2(out[1],STDERR_FILENO);
            for (int fd:{in[0],in[1],out[0],out[1]}) ::close(fd);
            if (chdir(directory.c_str())!=0) _exit(126);
            execl(program.c_str(),program.c_str(),static_cast<char*>(nullptr));_exit(127);
        }
        ::close(in[0]);::close(out[1]);
        if (pid_<0) {::close(in[1]);::close(out[0]);return false;}
        input_=in[1];output_=out[0];return true;
    }
    bool send(const std::string& text) {
        return write_external_pipe(input_, text.data(), text.size());
    }
    int read_available(std::string& output) {
        pollfd descriptor{output_,POLLIN|POLLHUP,0};
        const int ready=poll(&descriptor,1,5);
        if (ready<0) return errno==EINTR?0:-1;
        if (!ready) return 0;
        char buffer[8192];const auto count=::read(output_,buffer,sizeof(buffer));
        if (count<=0) return -1;
        output.assign(buffer,static_cast<size_t>(count));return 1;
    }
    void close_process() {
        if (input_>=0) {::close(input_);input_=-1;}
        if (output_>=0) {::close(output_);output_=-1;}
        if (pid_>0) {
            kill(pid_,SIGKILL);
            while (waitpid(pid_,nullptr,0)<0 && errno==EINTR) {}
            pid_=-1;
        }
        previous_.clear();
    }
#endif
};

} // namespace gomoku
