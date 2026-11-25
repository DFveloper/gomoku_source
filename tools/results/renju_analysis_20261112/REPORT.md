# Renju legality and non-loss search investigation

The 36-game undefeated goal has **not** been achieved. Selected diagnostic
draws below do not constitute a passed acceptance suite. The unchanged Rapfi
and Alpha opponents still receive their original nominal 5000 ms budgets.

## Current strongest fixed audit and council research

The best completed fixed version accepted **34/36**:
[positive-policy round 08](../star_reinforcement_positive_20261113/round-08/LIVE_REPORT.md).
Alpha passed 18/18; Rapfi White case 3 lost and Black case 12 had an invalid
no-legal-move ending. The new [expert council](../../../docs/moe-council.md)
passed all 15 software checks but has not passed a fresh strength audit.
Native goal-risk probes still sometimes prefer the risky move, so the Black
endgame cause remains unresolved. Standard-clock and extra-compute rescue
profiles are evaluated separately, never pooled.

## Confirmed causes

- The previous heuristic black-forbidden classifier differed from native
  Rapfi on 90 of 1264 replay positions: 13 false-forbidden and 83 missed
  coordinates. The geometric implementation agrees on all 1264 masks.
  Tests retain the 90 independently recorded reference masks.
- In `star_candidate_20261111/rapfi_00.log`, before ply 59, `(9,9)` was
  incorrectly forbidden. The corrected VCF prover finds it in two nodes.
  Unchanged Rapfi independently returns `(9,9)` and `+M5`; see
  [the raw reference analysis](rapfi00_ply59_reference.json). The old move
  `(7,9)` lost. This is a concrete missed win, not an inferred rating gain.
- Replaying all 1334 recorded black moves found an actual missed black foul
  in Alpha case 13 at ply 51, `(11,11)`. The historical suite is preserved
  unchanged. New refereeing records an attempted black foul explicitly as a
  White rule win; occupied cells, malformed replies, crashes and timeouts
  remain invalid games.
- An additional constructed position distinguishes a legal five-making
  extension from a genuine three: an extension that completes five on another
  axis cannot define a three. This is covered by a separate regression and
  independently checked against native Rapfi. The previous 1264-mask sample
  did not contain this edge case.
- An additional 2500 synthetic occupancy positions, with seed `20261112`,
  agree with native Rapfi on all 225 cells per position; see
  [the out-of-sample check](random_rules_even.log). These are classifier checks,
  not played games or strength results.

The classifier follows the five-precedence, three-definition and recursive
extension provisions in [the official RIF rules](https://gomoku.renju.net/rifrules/),
sections 3 and 9. Only the first Black stone is fixed for this user-requested
suite; tournament opening and swap procedures are not imposed.

## Changes under investigation

- Persistent expert processes preserve transposition tables between turns.
  Cancellation, EOF, root restrictions and utility changes have regression
  coverage. Linux expert pipes use atomic close-on-exec and local SIGPIPE
  handling, so concurrent launches cannot retain other experts' pipes.
- `GOMOKU_RETAINED_SEARCH=1 GOMOKU_NONLOSS_OBJECTIVE=1` gives the candidate's
  Rapfi search equal internal utility for draws and wins. This does not change
  the referee or either opponent. A draw still requires all 225 intersections.
- Complete-defense, bounded threat proofs can reject demonstrably losing
  moves. Node, time and cancellation limits return unknown, never a win proof.
- Opening, symmetry, MCTS and extra-compute variants remain opt-in experiments.
  Neither more time nor a newer expert/model alone solved the difficult cases.

## Selected completed diagnostics

| Opponent | Case | Candidate | Search | Actual result |
|---|---:|---|---|---|
| Alpha | 3 | White | Retained AB, non-loss utility | [225-ply draw](nonloss_ab_alpha3.log) |
| Alpha | 9 | White | Retained AB, non-loss utility | [225-ply draw](nonloss_ab_alpha9.log) |
| Rapfi | 7 | White | Same, first reply `(-1,0)` relative to Black | [225-ply draw](edge_rapfi7_-1_0.log) |
| Rapfi | 7 | White | Same, first reply `(+1,0)` relative to Black | [225-ply draw](edge_rapfi7_1_0.log) |

These are selected successes from many failed experiments, not all games from
one strategy. They used `.run/analysis-v4/benchmark-nonloss`, before later
deadline and rare-three fixes. The recorded current-source hashes describe
the audit source, not a reconstruction of that older diagnostic executable.

[The completed fixed-executable 36-game run](../star_nonloss_20261112/LIVE_REPORT.md)
used retained AB, non-loss utility and a tangential response at axis openings.
It accepted **27/36** games and failed the goal:

| Opponent | Wins | Losses | Full-board draws | Invalid |
|---|---:|---:|---:|---:|
| Rapfi | 1 | 5 | 10 | 2 |
| Alpha | 10 | 2 | 6 | 0 |

This snapshot predates the rare-three extension and exact-endgame fixes. It
cannot certify the current source. The two invalid Rapfi endings occurred when
Black had no legal placement left; they are not draws or accepted wins.

## Endgame repair and learned policy

The case-2 endgame failure is already detectable after Black ply 203. A new
proof guard rejects creating an overline hole when White can close Black's
last possible exact-five window and reserve that hole for Black's final turn.
The [counterfactual tail replay](corrected_tail_rapfi2.log) imports the original
202 legal plies, then plays both engines normally. The repair finishes in an
actual 225-ply draw. This is a diagnostic tail repair, not a new star-case pass.

A bounded, collision-free ternary endgame solver independently checks positions
with up to 12 empty points. Its root utility also rejects invalid matches: it
does not turn an opponent's missing legal reply into a certified strength win.
It can instead fill the forbidden hole as White to obtain a real board-full
draw. The referee's placement-only rules and error criteria are unchanged.

The first frozen experience version learned 27,978 states from 144 recorded
training episodes. Its [completed 36-game audit](../star_experience_20261112/LIVE_REPORT.md)
accepted **29/36**, still failing the goal:

| Opponent | Wins | Losses | Full-board draws | Invalid |
|---|---:|---:|---:|---:|
| Rapfi | 6 | 6 | 5 | 1 |
| Alpha | 14 | 0 | 4 | 0 |

Seven Rapfi conditions remain: White cases 3, 7, 9, 11 and 15 lost; Black
case 8 lost and case 16 ended invalidly with no legal final placement.
Alpha's 18 accepted games belong to this frozen version only. Later profiles
retain its positive Alpha book and AB non-loss search until a new Alpha failure;
they still require a fresh full 36-game audit.

The next learner retains successful and failed actions. An initially large
empirical loss penalty degraded openings and was reduced to 40 score units.
An adjacent first reply cannot be replaced by a distant untested reply during
that adjustment. Direct successful evidence at every recorded position takes precedence
over unplayed symmetry-augmented alternatives. A later audit exposed a confidence
filter discarding the known positive White response `(3,8)` after the zero-based
prefix `(3,7) (2,7) (4,5)`: the learned weight was 4/16, so the old 50% filter
rejected it and an empirically adjusted alternative lost. The new policy retains
a positive continuation regardless of the fraction; this remains an empirical
proposal subject to tactical checks, never a win certificate. These empirical weights are not
proofs or counts of fresh validation games. MCTS variants also produced fresh
losses; their partial successes do not establish improvement over the AB policy.

The stable controller entry point is
[`.run/active-star-reinforcement/status.json`](../../../.run/active-star-reinforcement/status.json).
It freezes executable, book and opponent hashes and tests difficult White games
first. Failed versions may stop after a loss; **only 36 fresh completed games
from one unchanged version can pass**. Imported tail replays are training data,
never new opening passes. All failed attempts remain in their own result folders.

## Verification

The full build and 13 CTest groups passed, with targeted additions for the
later goal-reward alignment. Checks include recursive rules, the historical
missed win, complete-defense threats, exact endgames, MCTS PV parsing, pipe
cleanup, all coordinate symmetries, learning from losses and frozen-book
validation. The acceptance runner also rejects tail-only replays and cannot
pass a failed partial training round. Isolated fake-child tests exercise
orchestration; they never appear in the real strength results.

## Reply-branch investigation (2026-11-13)

All 12 direct-evidence versions finished their attempted games and failed the
acceptance gate. Rapfi White case 7 drew in the last 11 versions; case 9 drew in
the last three. These are repeatability observations, not a combined score or a
complete 36-game pass. White case 3 remains unresolved.

[Round 3](../star_reinforcement_direct_20261113/round-03/LIVE_REPORT.md) drew case 3,
but later losses reveal alternative opponent replies. After the zero-based
prefix `3,7 2,7 4,5 3,8 4,9`, the remembered White move `4,6` allowed Black
`5,6` on a drawn line, and `5,8` on several losing lines. Simply retaining every
positive continuation caused the learner to repeat this fragile move. A second
fork occurs after White `8,8` at ply 12: Black's `7,4` line drew, while `7,9`
repeatedly won. The single MCTS win starting White `4,6` similarly masked
repeated failures after its remembered third-ply response.

The V3 book retains the actual weighted wins/losses and separately records
uncovered opponent replies: at least two distinct completed losing histories
against a reply, no successful history against that reply, and a successful
history against another reply after the same candidate action. Duplicate
histories cannot satisfy the two-history threshold. Such a proposal no longer
plays automatically; normal search explores other legal roots. If every legal
root would be excluded, restrictions are dropped. Immediate wins and tactical
proofs retain priority. These flags describe empirical risk, never a proof of
forced loss. V1 and V2 books remain readable, and the preserved Alpha book
retains zero flags.

The 261-episode diagnostic model flags exactly three Rapfi White proposals:
`6,9` after 3 plies, `4,6` after 5 plies and `8,8` after 11 plies. Alpha's
preserved proposals are untouched. The new build passed all 13 CTest groups,
including repeated-branch detection, duplicate exclusion, unchanged episode
weights, legal-root exploration and preservation of an available legal move.

A full 36-game audit of the prior binary is finishing to expose conditions that
fail-fast rounds never reached. The new controller waits for it, then trains
and audits the branch-aware binary. `--full-audit-every 3` runs all 36 games on
the first and every third attempt, even after a loss; only a real unchanged
36/36 pass satisfies the gate. The stable active-status link above identifies
the current controller. A diagnostic replay from the five-ply prefix is separate
training evidence and cannot count as an opening pass.

The first branch-aware diagnostic did not solve case 3:
[the fresh game](branch_star_rapfi3.log) lost in 35 plies after trying White
`5,6`, while [the five-ply-prefix replay](branch_tail_rapfi3_p5.log) lost in
65 plies after trying `4,8`. Both are retained as failed training observations.
Their fixed executable/book hashes are in
[the diagnostic manifest](branch_diagnostic_manifest.json). Neither is a pass.

The latest exploration variant also excludes an alternative that has no
positive episode and at least two direct failed episodes at a flagged fork.
This applies only where a reply-branch flag already triggered exploration;
ordinary V1/V2 states remain unrestricted, and eliminating every legal move
falls back to unrestricted search. Tests cover retaining a singly observed
alternative and moving beyond a repeatedly failed one. The rebuilt interfaces
and all 13 CTest groups passed again. The active controller freezes this latest
binary, waits for the ongoing full prior-version audit, then starts new audits.

## Completed 33/36 audits and draw-safety experiment

The 12 branch-exploration versions finished. Four full audits accepted 32, 32,
33 and 33 of 36 games; the remaining partial versions also failed.
[The last full audit](../star_reinforcement_branch_explore_20261113/round-11/LIVE_REPORT.md)
had Alpha 15 wins/3 draws and Rapfi 7 wins/2 losses/8 draws/1 invalid ending.
Rapfi White cases 3 and 15 lost; Black case 4 could not legally fill its last
intersection. The earlier 33/36 version instead failed Black case 12. No scores
are combined across these versions.

In the case-4 failure, Black ply 51 (`9,9`) already created the permanent
geometric overline hole at `7,7`. In the case-12 failure, the first such hole
appeared at Black ply 175 after a forced defensive move. The exact late-game
proof can be too late to retain a successful earlier continuation.

The experimental Rapfi-Black non-loss profile now prefers legal roots that do
not create such a hole, while allowing immediate exact-five wins. Immediate
blocks and local win proofs retain precedence. If an overline hole already
exists, the candidate searches with ordinary win utility instead of treating
draws and wins as equal, and does not automatically replay empirical proposals.
When every root would be excluded, search is unrestricted. This is a heuristic
risk policy, not a proof that a hole always causes a loss, and it does not alter
the referee or either opponent. Alpha is outside this profile adjustment.

A separate candidate-comparison fix requires AB alternatives to reach at least
the selected line's depth. A White case-15 diagnostic had promoted a shallower
optimistic alternative; this is no longer permitted. The outward-opening
case-15 diagnostic still lost in 33 plies, and its strict-depth tail repair also
lost in 37. Neither was a pass; both are training observations.

The Alpha-preservation path now copies complete modern books, including all
negative actions and uncovered-reply flags, without enabling legacy routing.
The first new model's 41,913 Alpha rows have the same SHA-256 as the successful
33/36 version's Alpha rows. The old V1 preservation behavior remains covered.
Modern-book merge, routing, hole geometry, the historical ply-51 fixture and
same-depth candidate comparisons passed with all 13 CTest groups and the full
interface build. Tests of orchestration remain isolated from real strength data.

The new controller runs a complete first audit and periodic full audits, even
after a loss, with frozen binary/book/opponents. Its live location is the active
status alias. Black tail diagnostics separately test avoiding the ply-51 hole
and seeking a win from an already-created hole; they cannot count as fresh
opening passes. The undefeated gate remains unmet until a single version
finishes all 36 fresh games without a failure.

The first proactive-hole tail from case-4 prefix 50 also lost (112 plies),
despite avoiding `9,9`. At ply 111 the retained expert exited without a move;
the candidate used a legal fallback, and White subsequently made five. This is
a valid candidate loss, not a passed repair. The separate prefix-52 win-seeking
tail was still running when this note was written. Avoiding one geometric risk
does not establish a successful strategy; the new full audit is the acceptance
check. Completed additional diagnostics are retained as
[outward opening](outward_star_rapfi15.log),
[equal-depth tail](equaldepth_tail_rapfi15_p11.log) and
[hole-avoidance tail](fillable_tail_rapfi4_p50.log).

The prefix-52 win-seeking diagnostic eventually ended invalidly at 224 stones:
Black still could not place on the reserved hole. Its
[raw history](seekwin_tail_rapfi4_p52.log) is a failed repair, not a draw. Changing
utility after creating a hole was insufficient in this fixed prefix.

For additional training diversity, `--teacher-alpha-tail` lets the unchanged
Alpha runtime play the candidate side of a diagnostic continuation against the
unchanged opponent. It always emits `replay_case` with
`diagnostic=teacher-alpha`, never an acceptance `star_case`. It is unavailable
in the production star-case route. Teacher moves are traced with engine ID 5.
Completed legal teacher histories may train a book; only the strengthened
production engine's own fresh 36-game audit can pass. Separate Alpha-teacher
games from the first fixed Black stone are running for Rapfi White cases 3 and
15. The full build and all 13 CTest groups passed after adding this mode;
a dedicated gate test rejects teacher diagnostics as acceptance games.

The active draw-safety controller retains its frozen pre-teacher executable.
The later teacher-only CLI addition does not modify that frozen binary or its
ongoing scores. Current repository source hashes include the diagnostic CLI;
as with earlier snapshots, those hashes alone do not reconstruct an older
executable. The executable and book hashes in each run remain authoritative.

## Regression isolation and unplayed-opening research

The completed draw-safety experiment accepted **30/36**, below the prior best
33/36. Alpha passed 17/18 and Rapfi 13/18. Its
[full report](../star_reinforcement_draw_safe_20261113/round-02/LIVE_REPORT.md)
retains all failures, including another invalid Black ending. The geometric
hole heuristic remains an explicit opt-in experiment through
`GOMOKU_BLACK_DRAW_SAFETY=1`; it is not part of the next control profile.

A concrete learning regression occurs in Rapfi White case 9. The first 13 moves
match the 33/36 version, but the next policy blocks its successful White `4,7`
(`16/20`, no uncovered replies in the frozen reference) after learning two
failed histories against a different Black reply (`16/24`, one uncovered reply).
It chooses `7,10` instead and loses in 39 plies. The successful continuation
against Black `5,9` and the failed continuation against Black `3,7` are distinct
branches. A failed descendant is not a proof that the parent move cannot draw.

The new `GOMOKU_RAPFI_BOOK_ONLY=1` control retains positive continuations as
empirical proposals and uses ordinary single-PV search for unknown positions.
It disables empirical root bans, negative ordering penalties and loss-triggered
8-PV expansion for Rapfi only. Alpha's branch-aware selection remains unchanged.
Tactical and endgame proofs still take precedence. Both policy flags are frozen
in acceptance manifests and cannot change on resume.

Branch-specific replays remain negative: raw AB lost case 3 from prefix 7 in
73 plies; Alpha lost in 35 and genuine MCTS in 25. For case 9's bad Black `3,7`
reply, AB and Alpha both lost from prefix 15 in 53 plies. No repair is claimed.
An initially MCTS-labelled raw diagnostic actually used the default ensemble
because the standalone MCTS flag did not activate adaptive routing. Its log was
preserved as `raw_ensemble_rapfi3_p7.log` (77-ply loss); a separate real MCTS run
used explicit retained routing. Standalone MCTS routing is now corrected and
independently verified with the actual MCTS, AB and Alpha workers: 3 launched,
3 answered, legal move. Earlier book-based MCTS runs already used adaptive
routing and are not relabelled.

A fixed-reference audit now reuses the 33/36 book without retraining and enables
Rapfi positive-only selection. In parallel, an opening explorer tests **77
previously unplayed first White replies** across Rapfi cases 3 and 15 (34 and
43 respectively). It uses unchanged real opponents and the production candidate
with a fixed first-response setting per trial. A promising reply must win or
draw twice consecutively before being marked a training candidate. Even then,
`goal_met` remains false: these different trial configurations cannot be combined
into a 36-game acceptance result. Trial records and confirmations are separate
from validation scores. Historical played replies are excluded, changed runtime
files abort the experiment, and malformed, crashed or replay-only outputs cannot
be accepted as production trials.

After both stages finish, failed fixed-audit and exploratory games feed new
positive-only training versions. Each such version must independently complete
all 36 fresh games to pass. The active status alias identifies the current
stage. All interfaces built, all **14 CTest groups** passed, and the standalone
native MCTS check passed. Synthetic orchestration tests remain isolated in
temporary directories and never enter real strength data.

An early result from the new fixed-book control re-confirms Rapfi White case 9
as a real 225-stone draw: it retains White `4,7`, and the opponent chooses the
previously successful Black `5,9` branch. This does not resolve the separate
Black `3,7` branch. Cases 3 and 7 already lost in this new audit, so this version
has not passed the gate. The opening explorer's first completed trials were
also losses; no confirmed opening is claimed at this point.
