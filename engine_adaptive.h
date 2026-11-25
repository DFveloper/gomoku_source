#pragma once
#include "engine_session.h"
#include "engine_ensemble.h"
#include "engine_experience.h"
#include "engine_council.h"
#include <array>

namespace gomoku {

struct AdaptiveSearchConfig {
    int base_ms=5000;
    int recheck_ms=0; // Additional search only in adverse/unstable positions.
    int alternative_ms=0;
    int primary_threads=4;
    int rollout_ms=0;
    EngineVersion opponent=EngineVersion::Rapfi2025;
    bool mcts=false;
    bool prefer_nonloss=false;
    int judge_ms=0;
    int symmetry_workers=0;
    bool legacy_experience=false;
    bool rapfi_book_only=false;
    bool protect_black_draws=false;
    int council_ms=0,council_rescue_ms=0,total_threads=6;
};

inline bool adjust_experience_search(AnalyzedMove& result,
        const std::vector<ExperienceChoice>& experience,
        int board[kMaxBoard][kMaxBoard],int size,int player,bool mcts,Move opening_stone={-1,-1}) {
    if(!result.has_score||(result.mate&&result.score>0)||experience.empty())return false;
    const Move original=result.move;
    const bool local_opening=ok(size,opening_stone.r,opening_stone.c)&&
        std::max(std::abs(original.r-opening_stone.r),std::abs(original.c-opening_stone.c))<=1;
    // Shallower alternatives can have optimistic scores before the opponent's
    // forcing continuation appears. Only compare equally deep AB results.
    const int minimum_depth=result.depth;
    int best=result.score-experience_loss_penalty(experience,result.move);
    for(const auto& candidate:result.candidates) {
        if(!candidate.has_score||!is_legal_move(board,size,player,candidate.move)||
           (candidate.move.r==original.r&&candidate.move.c==original.c)||
           (!mcts&&candidate.depth<minimum_depth))continue;
        if(local_opening&&std::max(std::abs(candidate.move.r-opening_stone.r),
            std::abs(candidate.move.c-opening_stone.c))>1)continue;
        const int adjusted=candidate.score-experience_loss_penalty(experience,candidate.move);
        if(adjusted>best) {
            result.move=candidate.move;result.score=candidate.score;result.depth=candidate.depth;
            result.mate=std::abs(candidate.score)>=29500;best=adjusted;
        }
    }
    return result.move.r!=original.r||result.move.c!=original.c;
}

inline EnsembleStats choose_adaptive_move(
        int board[kMaxBoard][kMaxBoard],int size,const std::vector<Move>& history,
        int player,int level,const std::filesystem::path& runtime,
        AdaptiveSearchConfig config,const std::atomic<bool>* cancel=nullptr) {
    EnsembleStats stats;
    if(config.legacy_experience){config.mcts=false;config.prefer_nonloss=true;}
    const bool book_only=config.rapfi_book_only&&config.opponent==EngineVersion::Rapfi2025;
    const bool council=config.council_ms>0;
    const auto started=std::chrono::steady_clock::now();
    auto finish=[&](Move move,EnsembleSource source) {
        stats.move=move;stats.source=source;
        stats.total_ms=static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now()-started).count());return stats;
    };
    if ((cancel&&cancel->load()) || first_legal_move(board,size,player).r<0) return stats;
    Move tactical=find_immediate_win(board,size,player);
    if (tactical.r>=0) return finish(tactical,EnsembleSource::LocalTactics);
    tactical=find_immediate_win(board,size,3-player);
    if (is_legal_move(board,size,player,tactical)) return finish(tactical,EnsembleSource::LocalTactics);
    int local[kMaxBoard][kMaxBoard];std::memcpy(local,board,sizeof(local));
    int nodes=22000;Move proven{-1,-1};
    const auto proof_deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(500);
    if (forced_four_search(local,size,player,16,nodes,&proven,&proof_deadline,cancel) && is_legal_move(board,size,player,proven)) {
        stats.local_proved_win=true;return finish(proven,EnsembleSource::LocalVcf);
    }
    if(opponent_has_unavoidable_vcf_setup(board,size,player,
        std::chrono::steady_clock::now()+std::chrono::milliseconds(500),16,1200,&proven,10,2)) {
        stats.local_proved_win=true;return finish(proven,EnsembleSource::LocalTactics);
    }

    // Against Rapfi, invalid last-hole endings persisted despite a high
    // non-loss score. Preserve the independently validated Alpha profile.
    const bool black_draw_safety=config.protect_black_draws&&player==1&&config.prefer_nonloss&&
        config.opponent==EngineVersion::Rapfi2025;
    const bool must_seek_win=black_draw_safety&&has_black_overline_hole(board,size);
    const auto fillable=black_draw_safety&&!must_seek_win?black_fillable_root_moves(board,size):std::vector<Move>{};
    if(must_seek_win)config.prefer_nonloss=false;
    if(black_draw_safety&&std::getenv("GOMOKU_BENCH_TRACE")&&(must_seek_win||!fillable.empty()))
        std::fprintf(stderr,"black-draw-safety plies=%zu seek_win=%d allowed=%zu\n",history.size(),must_seek_win,fillable.size());

    std::vector<ExperienceChoice> experience;
    Move book_nominee{-1,-1};
    if(const char* book=std::getenv("GOMOKU_EXPERIENCE_BOOK")) {
        experience=experience_actions(book,board,size,player,config.opponent);
        const auto remembered=best_experience_choice(experience,!(book_only||council));
        // These are augmented episode weights, not calibrated probabilities.
        // Retain a demonstrated continuation even when other continuations
        // from its descendants failed. Immediate threats still take priority.
        if(!must_seek_win&&remembered.nonloss_weight>0&&
           (fillable.empty()||std::any_of(fillable.begin(),fillable.end(),[&](Move move){
               return move.r==remembered.move.r&&move.c==remembered.move.c;
           }))&&
           is_legal_move(board,size,player,remembered.move)&&
           !detail::loses_to_endgame_trap(board,size,player,remembered.move)&&
           !detail::loses_to_vcf(board,size,player,remembered.move,6000)&&
           !detail::loses_to_vct(board,size,player,remembered.move,350)) {
            bool hole_risk=false;
            if(council&&player==1) {
                int after[kMaxBoard][kMaxBoard];std::memcpy(after,board,sizeof(after));
                after[remembered.move.r][remembered.move.c]=1;hole_risk=has_black_overline_hole(after,size);
            }
            const bool trusted=!council||(!hole_risk&&remembered.nonloss_weight>=8&&remembered.uncovered_replies==0&&
                4LL*remembered.nonloss_weight>=3LL*remembered.total_weight);
            if(trusted&&std::getenv("GOMOKU_BENCH_TRACE"))
                std::fprintf(stderr,"experience plies=%zu move=%d,%d nonloss_weight=%d total_weight=%d\n",
                    history.size(),remembered.move.r,remembered.move.c,remembered.nonloss_weight,remembered.total_weight);
            if(cancel&&cancel->load())return finish({-1,-1},EnsembleSource::LocalSearch);
            if(trusted)return finish(remembered.move,EnsembleSource::ExperienceBook);
            book_nominee=remembered.move;
        }
    }

    ExternalEngineRequest request;
    request.version=EngineVersion::Rapfi2025;request.moves=history;
    request.player=player;request.level=level;request.board_size=size;
    request.turn_time_ms=config.base_ms;request.threads=config.primary_threads;
    request.program=external_runtime_path(runtime,request.version);request.cancel=cancel;
    if(const char* program=std::getenv("GOMOKU_PRIMARY_PROGRAM"))request.program=program;
    request.prefer_nonloss=config.prefer_nonloss;
    const bool alpha_available=std::filesystem::is_regular_file(external_runtime_path(runtime,EngineVersion::AlphaGomoku2026));
    const auto allocation=council_allocation(config.total_threads,alpha_available);
    if(council) {
        const int remaining=std::max(100,config.base_ms-static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count()));
        const auto timing=council_time_budget(remaining,config.council_ms);
        request.turn_time_ms=timing.proposal_ms;
        request.threads=allocation.primary;
    }
    // A shared owner is necessary because UI worker threads change each turn.
    // RetainedEngine serializes its process; a divergent/new history resets it.
    static RetainedEngine classical;
    static RetainedEngine monte_carlo("mcts");
    RetainedEngine& primary=config.mcts?monte_carlo:classical;
    AnalyzedMove classical_proposal;
    AnalyzedMove diversity_proposal;
    std::thread classical_worker;
    if(config.mcts&&(!council||allocation.diversity>0)) {
        ExternalEngineRequest classical_request=request;classical_request.threads=1;
        ++stats.experts_launched;
        classical_worker=std::thread([&,classical_request]{classical_proposal=classical.think(classical_request);});
    } else if(council&&!config.mcts&&allocation.diversity>0&&std::filesystem::is_regular_file(request.program)) {
        ExternalEngineRequest diverse_request=request;diverse_request.threads=1;
        ++stats.experts_launched;
        classical_worker=std::thread([&,diverse_request]{diversity_proposal=monte_carlo.think(diverse_request);});
    }
    ExternalEngineRequest alpha=request;
    alpha.version=EngineVersion::AlphaGomoku2026;alpha.threads=1;
    alpha.program=external_runtime_path(runtime,alpha.version);
    Move alpha_move{-1,-1};std::string alpha_error;
    const bool have_alpha=alpha_available&&(!council||allocation.alpha>0);
    std::thread alpha_worker;
    if (have_alpha) {
        ++stats.experts_launched;
        alpha_worker=std::thread([&]{alpha_move=run_external_engine(alpha,alpha_error);});
    }
    if (std::filesystem::is_regular_file(request.program)) ++stats.experts_launched;
    AnalyzedMove result;
    if(config.symmetry_workers>1&&!config.mcts&&!council) {
        const int count=std::min({8,config.symmetry_workers,config.primary_threads});
        static std::array<RetainedEngine,7> oriented;
        std::vector<AnalyzedMove> proposals(count);
        std::vector<std::thread> workers;
        for(int symmetry=0;symmetry<count;++symmetry)workers.emplace_back([&,symmetry] {
            ExternalEngineRequest rotated=request;
            rotated.threads=std::max(1,config.primary_threads/count);
            for(auto& move:rotated.moves)move=symmetry_move(move,size,symmetry);
            RetainedEngine& engine=symmetry==0?primary:oriented[symmetry-1];
            proposals[symmetry]=engine.think(rotated);
            proposals[symmetry].move=symmetry_move(proposals[symmetry].move,size,symmetry,true);
        });
        for(auto& worker:workers)worker.join();
        if(std::filesystem::is_regular_file(request.program))stats.experts_launched+=count-1;
        int best=0,best_votes=0,answered=0;
        for(int i=0;i<count;++i) {
            const auto move=proposals[i].move;
            if(!is_legal_move(board,size,player,move))continue;
            ++answered;
            int votes=0;
            for(const auto& proposal:proposals)
                votes+=proposal.move.r==move.r&&proposal.move.c==move.c;
            if(votes>best_votes){best=i;best_votes=votes;}
            if(std::getenv("GOMOKU_BENCH_TRACE"))
                std::fprintf(stderr,"symmetry-search plies=%zu orientation=%d move=%d,%d eval=%d depth=%d votes=%d\n",
                    history.size(),i,move.r,move.c,proposals[i].score,proposals[i].depth,votes);
        }
        stats.experts_answered+=std::max(0,answered-1); // Selected reply is counted below.
        result=proposals[best];stats.chosen_votes=best_votes;stats.consensus=best_votes>=2;
    } else {
        const bool failures=!council&&!book_only&&!config.legacy_experience&&std::any_of(experience.begin(),experience.end(),[](const ExperienceChoice& choice){
            return choice.total_weight>2LL*choice.nonloss_weight;
        });
        const int multipv=failures&&!config.mcts?8:(config.rollout_ms>0?3:1);
        auto allowed=council||book_only||config.legacy_experience?std::vector<Move>{}:
            experience_root_moves(experience,board,size,player);
        if(!fillable.empty()) {
            if(allowed.empty())allowed=fillable;
            else {
                std::vector<Move> both;
                for(Move move:allowed)if(std::any_of(fillable.begin(),fillable.end(),[&](Move safe){
                    return safe.r==move.r&&safe.c==move.c;
                }))both.push_back(move);
                // Empirical exploration cannot erase the structural alternatives.
                allowed=both.empty()?fillable:std::move(both);
            }
        }
        if(!allowed.empty()&&std::getenv("GOMOKU_BENCH_TRACE"))
            std::fprintf(stderr,"experience-explore plies=%zu allowed=%zu\n",history.size(),allowed.size());
        result=primary.think(request,allowed,multipv);
    }
    if (alpha_worker.joinable()) alpha_worker.join();
    if (classical_worker.joinable()) classical_worker.join();
    const bool alpha_legal=is_legal_move(board,size,player,alpha_move);
    if (alpha_legal) ++stats.experts_answered;
    if (is_legal_move(board,size,player,result.move)) ++stats.experts_answered;
    if (is_legal_move(board,size,player,classical_proposal.move)) ++stats.experts_answered;
    if (is_legal_move(board,size,player,diversity_proposal.move)) ++stats.experts_answered;
    stats.expert_available=stats.experts_answered>0;
    const Move initial=result.move;
    const int initial_score=result.score;
    bool council_book=false;
    if(council&&!(cancel&&cancel->load())) {
        const int reference_depth=config.mcts?classical_proposal.depth:result.depth;
        AnalyzedMove baseline=result;
        if(is_legal_move(board,size,player,book_nominee)&&!(result.has_score&&result.mate&&result.score>0)) {
            baseline.move=book_nominee;baseline.score=0;baseline.has_score=false;baseline.mate=false;council_book=true;
        }
        std::vector<Move> roots;
        auto nominate=[&](Move move) {
            if(!is_legal_move(board,size,player,move))return;
            if(std::none_of(roots.begin(),roots.end(),[&](Move p){return council_equivalent_moves(board,size,p,move);}))roots.push_back(move);
        };
        nominate(baseline.move);nominate(result.move);
        nominate(config.mcts?classical_proposal.move:diversity_proposal.move);nominate(alpha_move);
        std::vector<int> risks;
        if(player==1) {
            const auto risk_deadline=std::min(started+std::chrono::milliseconds(config.base_ms),
                std::chrono::steady_clock::now()+std::chrono::milliseconds(100));
            for(Move move:roots)risks.push_back(council_black_risk(board,size,move,risk_deadline));
            if(!risks.empty()&&risks.front()>0&&std::none_of(risks.begin(),risks.end(),[](int risk){return risk==0;})&&
               !has_black_overline_hole(board,size)) {
                // A local defensive specialist contributes a root, not a vote
                // or a forced choice. The common AB search still has to accept it.
                Move attack{-1,-1},block{-1,-1};
                council_forced_hole_reply(board,size,risk_deadline,&attack,&block);
                std::vector<Move> defences;
                if(is_legal_move(board,size,player,attack))defences.push_back(attack);
                if(is_legal_move(board,size,player,block))defences.push_back(block);
                for(const auto& item:build_root_candidates(board,size,player,nullptr))defences.push_back(item.second);
                for(Move defence:defences) {
                    if(std::chrono::steady_clock::now()>=risk_deadline)break;
                    if(council_black_risk(board,size,defence,risk_deadline)!=0)continue;
                    const auto before=roots.size();nominate(defence);
                    if(roots.size()>before)risks.push_back(0);
                    break;
                }
            }
        }
        const int remaining=config.base_ms-static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now()-started).count());
        stats.council_roots=static_cast<int>(roots.size());
        CouncilDecision decision;decision.move=baseline.move;decision.reason="budget";
        AnalyzedMove assessed;
        if(!roots.empty()&&remaining>=50) {
            ExternalEngineRequest common=request;
            common.turn_time_ms=std::min(config.council_ms,remaining);common.threads=allocation.arbiter;
            const auto phase_started=std::chrono::steady_clock::now();
            assessed=classical.think(common,roots,static_cast<int>(roots.size()));
            stats.council_ms=static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now()-phase_started).count());
            decision=council_select(baseline,assessed,roots,board,size,player,reference_depth,20,risks);
            const bool risky_black=player==1&&!risks.empty()&&risks.front()>0&&
                std::any_of(risks.begin(),risks.end(),[](int risk){return risk==0;});
            const bool adverse_white=player==2&&roots.size()>1&&!assessed.completed_candidates.empty()&&
                std::all_of(assessed.completed_candidates.begin(),assessed.completed_candidates.end(),[](const SearchLine& line){
                    return line.has_score&&line.score<=-100;
                });
            if(config.council_rescue_ms>0&&std::strcmp(decision.reason,"shallow")==0&&
               (risky_black||adverse_white)&&!(cancel&&cancel->load())) {
                if(std::getenv("GOMOKU_BENCH_TRACE"))
                    std::fprintf(stderr,"root-council-deepen plies=%zu requested_ms=%d previous_depth=%d floor=%d\n",
                        history.size(),config.council_rescue_ms,assessed.completed_depth,reference_depth);
                common.turn_time_ms=config.council_rescue_ms;
                const auto rescue_started=std::chrono::steady_clock::now();
                assessed=classical.think(common,roots,static_cast<int>(roots.size()));
                stats.council_rescue_ms=static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now()-rescue_started).count());
                stats.council_ms+=stats.council_rescue_ms;
                decision=council_select(baseline,assessed,roots,board,size,player,reference_depth,20,risks);
            }
        }
        if(is_legal_move(board,size,player,baseline.move))result=baseline;
        if(decision.complete) {
            result.move=decision.move;result.score=decision.score;result.depth=decision.depth;result.has_score=true;
            result.mate=std::abs(decision.score)>=29500;
            stats.council_complete=true;stats.council_promoted=decision.promoted;stats.council_depth=decision.depth;
            if(decision.promoted)council_book=false;
        }
        if(std::getenv("GOMOKU_BENCH_TRACE")) {
            std::fprintf(stderr,"root-council plies=%zu roots=%zu complete=%d promoted=%d depth=%d floor=%d reason=%s move=%d,%d phase_ms=%d\n",
                history.size(),roots.size(),decision.complete,decision.promoted,decision.depth,reference_depth,
                decision.reason,result.move.r,result.move.c,stats.council_ms);
            for(size_t i=0;i<roots.size();++i) {
                const auto line=std::find_if(assessed.completed_candidates.begin(),assessed.completed_candidates.end(),[&](const SearchLine& item){
                    return item.move.r==roots[i].r&&item.move.c==roots[i].c;
                });
                std::fprintf(stderr,"root-council-candidate plies=%zu move=%d,%d scored=%d eval=%d depth=%d goal_risk=%d\n",
                    history.size(),roots[i].r,roots[i].c,line!=assessed.completed_candidates.end(),
                    line==assessed.completed_candidates.end()?0:line->score,
                    line==assessed.completed_candidates.end()?0:line->depth,risks.empty()?-1:risks[i]);
            }
        }
    }
    if (!council&&config.recheck_ms>0 && result.has_score && result.score < -100 && !(cancel&&cancel->load())) {
        request.turn_time_ms=config.recheck_ms;
        const auto deeper=primary.think(request);
        if (is_legal_move(board,size,player,deeper.move)) result=deeper;
    }
    bool alternate_promoted=false;
    if(!council&&config.judge_ms>0 && config.mcts && is_legal_move(board,size,player,result.move) &&
       !(cancel&&cancel->load())) {
        std::vector<Move> choices{result.move};
        auto add=[&](Move move) {
            if(!is_legal_move(board,size,player,move))return;
            if(std::none_of(choices.begin(),choices.end(),[&](Move p){return p.r==move.r&&p.c==move.c;}))choices.push_back(move);
        };
        add(classical_proposal.move);
        auto candidates=result.candidates;
        std::stable_sort(candidates.begin(),candidates.end(),[](const SearchLine& a,const SearchLine& b){
            return a.score>b.score;
        });
        for(const auto& candidate:candidates){if(choices.size()>=3)break;add(candidate.move);}
        static std::array<RetainedEngine,3> root_judges;
        std::vector<AnalyzedMove> evaluations(choices.size());
        std::vector<std::thread> workers;
        for(size_t i=0;i<choices.size();++i)workers.emplace_back([&,i] {
            ExternalEngineRequest judge=request;
            judge.turn_time_ms=config.judge_ms;
            judge.threads=std::max(1,config.primary_threads/2);
            evaluations[i]=root_judges[i].think(judge,{choices[i]});
        });
        for(auto& worker:workers)worker.join();
        if(evaluations[0].has_score&&evaluations[0].move.r>=0) {
            size_t best=0;
            for(size_t i=1;i<choices.size();++i)if(evaluations[i].has_score&&evaluations[i].move.r>=0&&
                evaluations[i].score>evaluations[best].score+20)best=i;
            if(best>0){result=evaluations[best];alternate_promoted=true;}
        }
        if(std::getenv("GOMOKU_BENCH_TRACE"))for(size_t i=0;i<choices.size();++i)
            std::fprintf(stderr,"root-judge plies=%zu move=%d,%d eval=%d depth=%d known=%d\n",
                history.size(),choices[i].r,choices[i].c,evaluations[i].score,evaluations[i].depth,evaluations[i].has_score);
    }
    if (!council&&config.alternative_ms>0 && alpha_legal && result.has_score && result.score<0 &&
        (alpha_move.r!=result.move.r || alpha_move.c!=result.move.c) && !(cancel&&cancel->load())) {
        request.turn_time_ms=config.alternative_ms;
        const auto alternative=primary.think(request,{alpha_move});
        if (alternative.has_score && alternative.depth>=result.depth &&
            alternative.score > result.score+40 && is_legal_move(board,size,player,alternative.move)) {
            result=alternative;alternate_promoted=true;
        }
    }
    if (!council&&config.rollout_ms>0 && result.has_score && result.score<200 &&
        is_legal_move(board,size,player,result.move) && !(cancel&&cancel->load())) {
        std::vector<Move> candidates{result.move};
        auto add=[&](Move move) {
            if(!is_legal_move(board,size,player,move))return;
            if(std::none_of(candidates.begin(),candidates.end(),[&](Move p){return p.r==move.r&&p.c==move.c;}))
                candidates.push_back(move);
        };
        auto lines=result.candidates;
        std::stable_sort(lines.begin(),lines.end(),[](const SearchLine& a,const SearchLine& b){
            return a.depth!=b.depth?a.depth>b.depth:a.score>b.score;
        });
        for(const auto& line:lines) {
            if(candidates.size()>=3)break;
            if(line.depth>=result.depth-3)add(line.move);
        }
        if(alpha_legal && candidates.size()<4)add(alpha_move);
        struct Outcome {bool known=false;int score=0;Move reply{-1,-1};};
        std::vector<Outcome> outcomes(candidates.size());
        static std::array<RetainedEngine,4> judges;
        std::vector<std::thread> workers;
        for(size_t index=0;index<candidates.size();++index) workers.emplace_back([&,index] {
            ExternalEngineRequest model=request;
            model.version=config.opponent>=EngineVersion::CarbonClassic?config.opponent:EngineVersion::Rapfi2025;
            model.program=external_runtime_path(runtime,model.version);
            model.player=3-player;model.threads=1;model.turn_time_ms=5000;
            model.referee_accept_black_foul=true;
            model.prefer_nonloss=false;
            model.moves.push_back(candidates[index]);
            if(model.moves.size()==static_cast<size_t>(size*size)) {
                outcomes[index].known=true;outcomes[index].score=0;return;
            }
            std::string model_error;
            const Move reply=run_external_engine(model,model_error);
            int after[kMaxBoard][kMaxBoard];external_request_board(model,after);
            if(black_forbidden_reply(after,size,model.player,reply)) {
                outcomes[index].known=true;outcomes[index].score=30000;outcomes[index].reply=reply;return;
            }
            if(!legal_external_move(model,reply))return;
            outcomes[index].reply=reply;
            after[reply.r][reply.c]=3-player;
            if(check_win_at(after,size,reply.r,reply.c,3-player)) {
                outcomes[index].known=true;outcomes[index].score=-30000;return;
            }
            ExternalEngineRequest judge=request;
            judge.moves=model.moves;judge.moves.push_back(reply);
            judge.turn_time_ms=config.rollout_ms;
            const auto assessed=judges[index].think(judge);
            if(assessed.has_score&&is_legal_move(after,size,player,assessed.move)) {
                outcomes[index].known=true;outcomes[index].score=assessed.score;
            }
        });
        for(auto& worker:workers)worker.join();
        if(outcomes[0].known) {
            size_t best=0;
            for(size_t index=1;index<outcomes.size();++index)
                if(outcomes[index].known&&outcomes[index].score>outcomes[best].score+35)best=index;
            if(best>0) {result.move=candidates[best];result.score=outcomes[best].score;alternate_promoted=true;}
        }
        if(std::getenv("GOMOKU_BENCH_TRACE")) for(size_t i=0;i<candidates.size();++i)
            std::fprintf(stderr,"rollout plies=%zu candidate=%d,%d reply=%d,%d known=%d eval=%d\n",
                history.size(),candidates[i].r,candidates[i].c,outcomes[i].reply.r,outcomes[i].reply.c,
                outcomes[i].known,outcomes[i].score);
    }
    const Move opening_stone=player==2&&history.size()==1?history.front():Move{-1,-1};
    const bool experience_adjusted=!council&&!book_only&&!config.legacy_experience&&adjust_experience_search(result,experience,board,size,player,config.mcts,opening_stone);
    if (std::getenv("GOMOKU_BENCH_TRACE")) {
        std::fprintf(stderr,"adaptive plies=%zu initial=%d,%d eval=%d final=%d,%d eval=%d depth=%d scored=%d alternate=%d learned=%d error=%s\n",
            history.size(),initial.r,initial.c,initial_score,result.move.r,result.move.c,
            result.score,result.depth,result.has_score,alternate_promoted,experience_adjusted,result.error.c_str());
        std::fflush(stderr);
    }
    if (cancel&&cancel->load()) return finish({-1,-1},EnsembleSource::LocalSearch);
    if (is_legal_move(board,size,player,result.move)) {
        stats.local_depth=result.depth;
        stats.expert_ms=static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now()-started).count());
        stats.agreed=alpha_legal&&alpha_move.r==result.move.r&&alpha_move.c==result.move.c;
        if(council) {
            for(Move proposal:{initial,classical_proposal.move,diversity_proposal.move,alpha_move})
                stats.chosen_votes+=proposal.r==result.move.r&&proposal.c==result.move.c;
            stats.consensus=stats.chosen_votes>=2;
        }
        // A proven losing move can be replaced; an unproved opinion cannot.
        auto losing=[&](Move move) {
            return detail::loses_to_endgame_trap(board,size,player,move)||
                   detail::loses_to_vcf(board,size,player,move,6000)||
                   detail::loses_to_vct(board,size,player,move,350);
        };
        if(losing(result.move)) {
            std::vector<Move> alternatives;
            if(is_legal_move(board,size,player,classical_proposal.move))alternatives.push_back(classical_proposal.move);
            if(alpha_legal)alternatives.push_back(alpha_move);
            auto defense=ranked_candidates(board,size,player,12,nullptr);
            for(const auto& item:defense)alternatives.push_back(item.second);
            for(Move move:alternatives) {
                if(move.r==result.move.r&&move.c==result.move.c)continue;
                if(!losing(move)) {
                    if(std::getenv("GOMOKU_BENCH_TRACE"))
                        std::fprintf(stderr,"proof-override plies=%zu old=%d,%d new=%d,%d\n",
                            history.size(),result.move.r,result.move.c,move.r,move.c);
                    stats.proof_overruled=true;return finish(move,EnsembleSource::ExpertCorrected);
                }
            }
        }
        return finish(result.move,stats.council_promoted?EnsembleSource::ExpertArbitrated:
            (council_book?EnsembleSource::ExperienceBook:
            (experience_adjusted?EnsembleSource::ExperienceAdjusted:EnsembleSource::ExpertDelegated)));
    }
    if (alpha_legal) return finish(alpha_move,EnsembleSource::ExpertDelegated);
    OpponentStyle style=analyze_opponent_style(size,history,3-player);
    Move fallback=choose_ai_move(board,size,player,level,&style,config.primary_threads,{-1,-1},cancel);
    return finish(fallback,EnsembleSource::LocalSearch);
}
} // namespace gomoku
