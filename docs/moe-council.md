# Search expert council

The candidate already executes original Rapfi alpha-beta/NNUE and AlphaGomoku
search. The optional council adds a real Rapfi MCTS proposal and compares their
root moves with one common alpha-beta search. This is a search-expert router,
not a trained sparse neural MoE layer. Opponent executables are unchanged.

Enable `GOMOKU_ROOT_COUNCIL_MS=3500` for the Rapfi matchup. Six candidate workers
are divided into four AB workers, one MCTS worker and one Alpha worker. They
join before common AB arbitration uses all six workers. Proposal and arbitration
share the existing nominal 5000 ms budget; prior tactical checks also consume
that budget. Small worker budgets omit experts. The Alpha matchup retains its
existing route.

Proven immediate tactics precede the council. Untrusted book moves become
proposals; heavily supported covered book moves can still use the fast path.
Support counts are empirical weights, not calibrated win probabilities. Duplicate
roots are folded only when the entire colored position has the corresponding
exact board symmetry.

Arbitration requires every distinct candidate at one completed depth at least
as deep as the primary AB result. Partial deeper iterations do not replace the
last complete iteration. Raw scores from different engines are never averaged.
Incomplete or shallow arbitration preserves the primary move. A score improvement
greater than 20 can change the move. For Black, an additional bounded specialist
flags permanent overline holes and forced-block setups that create a hole. This
is a heuristic risk feature, not a loss proof. A safe alternative can break a
near tie only when its common AB score is no lower than the primary score;
positive mate results are protected. Unknown risk does not qualify as safe.

`GOMOKU_COUNCIL_RESCUE_MS=5000` optionally allows one extra common search after
selected shallow results (a Black hole-risk alternative, or all negative White
alternatives). This explicitly grants the candidate up to 5000 additional ms in
those positions. Opponents keep their original 5000 ms. Such a run is an
asymmetric-compute experiment and must be reported separately from the standard
clock profile. Coverage and depth requirements stay unchanged.

Source review used Rapfi commit `3c94c2a976f24a0dd1c5517623e9ab6fffe66bd7`
and AlphaGomoku commit `1f8184044930e6b76ebf21b93643b65669c85ff6`.
These identify inspected source, not the provenance of frozen runtime binaries;
audit manifests separately record executable/model hashes. Upstream sources
remain in `third_party/rapfi-src` and `third_party/alphagomoku-src`, with their
GPLv3 notices. This change adds independently written routing/protocol code and
uses upstream executables; it does not transplant upstream search code into the
Apache-licensed router. A direct source integration needs an explicit compatible
licensing arrangement as well as the original model/search dependencies.

All 15 CTest checks passed after implementation. Native probes verified actual
restricted MultiPV and AB/MCTS/Alpha routing. In a historical forced-hole position,
the risky move still sometimes receives the higher common AB score. This is an
unresolved strength issue, not a repaired game. Diagnostic probes and passed
software tests cannot establish the undefeated objective. Acceptance requires
36 fresh games using one fixed executable, book and configuration.
