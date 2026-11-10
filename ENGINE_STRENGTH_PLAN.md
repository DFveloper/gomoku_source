# Making the Gomoku engine stronger

This document answers one question: **where does the remaining strength of this
engine actually come from, and in what order should it be bought?**

It is split into two parts.

1. [Implemented in this revision](#part-1--implemented-in-this-revision) — the
   three changes that were measured to help, with the evidence.
2. [Roadmap](#part-2--roadmap) — the remaining opportunities, ranked by
   expected strength per unit of risk, each with a concrete design, a way to
   verify it, and the reason it is ranked where it is.

Everything below is grounded in measurements taken on this checkout
(2 vCPU, `g++ -O2`, 15x15 Renju), not in general engine folklore. Numbers that
were not measured are labelled as estimates.

---

## Where the time went

A `gprof` profile of one level-3 search, before any change:

| Function | Share of search time |
|---|---:|
| `evaluate_line` (pattern matching) | **89.9%** |
| `analyze_move` | 6.3% |
| `static_eval` | 0.9% |
| `is_forbidden_move` | 0.5% |

And a separate measurement of how much of the level budget the search actually
used:

| Level | Budget | Time used (before) |
|---|---:|---:|
| 1 | 1 s | 57 ms (6%) |
| 2 | 2 s | 352 ms (18%) |
| 3 | 4 s | 875 ms (22%) |
| 4 | 8 s | 3.2 s (41%) |
| 5 | 16 s | 10.4 s (65%) |

Those two facts set the whole agenda. The engine was spending 90% of its time in
one string-matching loop, and then throwing away between a third and nineteen
twentieths of the time it had been given.

---

## Part 1 — Implemented in this revision

### 1.1 Table-driven pattern matching (≈3–4× faster evaluation, bit-exact)

Every threat score in the engine reduces to one question about one line: looking
along a direction from one empty intersection, which patterns around that
intersection match? The old code answered it by walking 44 pattern rows at every
alignment with a per-character compare.

The line is base-4 information (`'.'`, `'X'`, `'O'`, `'#'`), so it packs into one
32-bit word at two bits per cell. Each `(pattern, alignment)` pair that can cover
the centre cell becomes one pre-shifted `(mask, value)` pair, and a match is then

```cpp
if ((key & arm.mask) != arm.value) continue;   // one AND, one compare
```

Two details make this safe rather than merely fast:

- `.` is a **literal empty cell, not a wildcard**. Treating it as a wildcard is
  the obvious "improvement" and it silently changes every score. The first
  prototype did exactly that and the differential test caught it immediately.
- A match only counts when its window covers the centre, reproducing the
  original `i > 7 || i + len <= 7` skip.

Measured: **808 ns → 198 ns per line**, bit-exact on ~1M randomly generated
lines. End-to-end, a level-3 search on the same position went from 1.96 s to
0.88 s.

The regression suite (`gomoku_cli test`) now contains this check permanently: it
recomputes both algorithms on every line of a test position and requires
identical `score`, `longest` and per-kind counts.

### 1.2 Iterative deepening that spends the budget

The old search ran exactly one pass at a fixed depth and returned. The new
search runs a depth ladder, keeps the best move of the last pass that *finished*,
and reorders the next pass by the values it just found.

Two mechanisms keep this honest:

- **Projected stop.** Before starting a pass, the previous pass's measured
  sequential cost is projected forward with a calibrated branching factor
  (≈2.4 per ply). Passes that cannot fit are not started, and the ladder stops
  climbing when the next depth will not fit.
- **Bounded overrun.** A pass that is already running may overrun the soft
  deadline by at most `min(1000 ms, budget/4 + 250 ms)` so its result stays
  usable. Anything unfinished at that stop is discarded. The level is a
  *guarantee* — the nominal depth is always attempted — but not a ceiling.

Measured depth actually reached (2 cores, eight threads):

| Position | L1 | L2 | L3 | L4 | L5 |
|---|---|---|---|---|---|
| opening (8 plies) | 0.7 s [3] | 2.0 s [3] | 4.0 s [4] | 7.3 s [5] | 16.0 s [5] |
| midgame (16 plies) | 1.0 s [3] | 2.0 s [4] | 2.9 s [5] | 7.1 s [6] | 16.0 s [6] |

Previous fixed-depth pass, same positions: depth 2, 3, 3, 4, 5, using 6–65% of
the budget. Effectively the engine gained **+1 ply at levels 2, 3 and 4**, and
level 1 gained a whole ply over its old depth.

### 1.3 Root bookkeeping that only trusts real values

With aspiration, the first move is searched with a full window and later moves
are compared against the leader, so every non-leading move carries a bound
rather than an exact score. If that distinction is lost, the engine happily
believes a "proven worse" score and returns the wrong move.

The root therefore records per-move bound types, never lets a fail-low bound win
a tie, and keeps the answer in a durable record so a half-finished pass cannot
overwrite the last complete answer. This is the bug class that the A/B referee
was built to catch: an earlier draft permuted the move order but not the
per-root value arrays, so scores were attributed to the wrong move and the
engine played worse than the revision it replaced (**0-3-1**). Once fixed, the
same build won **8-1-1**.

### 1.4 Evidence

Paired openings, colours swapped, working tree vs the frozen previous revision
(`tools/fixtures/baseline_core.h`):

| Match | Level | Result |
|---|---|---|
| vs frozen baseline | 2 | **5-0-1** |
| vs frozen baseline | 3 | **3-1-0** |
| combined | 2-3 | **8-1-1** |

Cross-engine check, new build vs Legacy v1 via the existing `engine_benchmark`:
2-0 at level 2, 1-0 with one board-full draw at level 3.

**Iterative deepening trades determinism for depth, by design.** The old search
returned the same move for any thread count and any machine speed, because it
always searched one fixed depth. That is no longer true and cannot be: the
whole point is to buy extra plies with whatever time is left, and time differs.
What still holds is the property that matters — the move is the argmax of the
pass the engine says it answered from, and the level's depth is always
delivered. `gomoku_cli test` therefore replaced "same move for every thread
count" with "the reported depth's true argmax, checked at 1, 2, 4 and 32
threads", plus a separate check that the level guarantee and the budget bound
hold. If reproducible move-for-move play is ever needed (a regression fixture, a
tournament with fixed output), pin one level and one thread count and compare
those, not two different thread counts.

**Read every Gomoku match result per colour.** Black (the first player) has a
large structural advantage, so a colour-split pair where one side wins both
games says much less than it looks like. A level-doubling self-match at L2 vs
L1 produced four games and black won all four — a genuinely useless ranking
signal. Both `ab_match` and `gomoku_cli selfmatch` therefore report
`as_black` / `as_white` counts next to the score, and `selfmatch` only fails on
a losing *overall* score. When a result looks surprising, check whether the
games were colour-dominated before believing the level changed anything.

**One negative result is part of this section on purpose.** `pattern_table()`
contains 44 rows, but only the highest score per threat kind survives into
`ThreatInfo.score`, so many rows are dead. Compressing them roughly halves the
matcher's work. It measured **2-2** against the baseline, versus 5-0-1 for the
bit-exact build — so the scoring semantics were left exactly as they were. Speed
only helps when the search can convert it into depth; different scoring is a
different engine.

### 1.5 The tools that make the next change measurable

A cross-engine table answers "does v3 beat engine X", which is not the same
question as "did this edit make the search better" — a weaker opponent setting
can make any edit look good. This revision adds three ways to answer the second
question:

| Tool | What it does |
|---|---|
| `tools/ab_match.cpp` | Plays the working tree against the frozen baseline revision, paired openings, colours swapped. Both cores in one binary. |
| `tools/id_soundness.cpp` | Recomputes, independently and single-threaded, the pass the engine says it answered from, and requires the returned move to be that pass's plain argmax. |
| `gomoku_cli selfmatch HI LO` | Portable level-doubling self-match; the smoke test for "a higher level still beats a lower one". |

```bash
scripts/build_tools.sh
build/tools/ab_match 2 3 2        # LEVEL PAIRS THREADS
build/tools/id_soundness 2 8      # LEVEL POSITIONS
./build/gomoku_cli selfmatch 3 2 2 2
```

`id_soundness` is wired into `ctest`. `ab_match` is deliberately not a ctest
entry: a ten-game match takes tens of minutes on two cores and its job is to
gate a *change*, not to gate every build.

---

## Part 1b — Equal-time results against Rapfi (2026-11-09)

The goal for this round was explicit: beat Rapfi, Alpha and the rest **inside
the same per-move time budget**, bit-exactness not required. This section records
what was actually measured, because the honest answer is that the goal was not
reached, and the measurement itself is the most useful thing produced.

### Getting a measuring stick

The bundled external runtimes cannot run on this host: they are linked against
`GLIBC_2.38`/`GLIBCXX_3.4.32` and the sandbox has glibc 2.36, so every one of
them exits at load time. That alone invalidates any "beats Rapfi" claim made
from this checkout — the benchmark would silently report the built-in engine
playing against nothing.

`tools/build_native_engines.sh` (new) builds the engines for the host instead:
Rapfi from source with `-DNO_COMMAND_MODULES=ON` and `-march=native`, XL from
source with the repo's compat header. The native binaries are written beside the
bundled ones (`.native-<glibc>` suffix, git-ignored) and `tools/duel` prefers
them. Carbon and AlphaGomoku are not buildable here (Carbon's sources are an
empty submodule; AlphaGomoku's tree did not build), so **Rapfi is the only
external opponent with a working, equal-time measuring stick on this machine.**

`tools/duel` (new) is the referee: one persistent engine session per game, both
sides on the same per-move clock, Renju rules for both, colour-swapped openings,
and per-colour reporting. Games the opponent forfeits to a crash, timeout or
protocol error are reported as `VOID` and excluded from the score rather than
counted as wins — an early run of this tool "won" 2-0 against XL purely because
XL was mis-parsing the board.

### Results

Per-move budget 1000 ms for both sides, 2 threads, 15x15 Renju:

| Match | Core budget | Result |
|---|---|---:|
| vs native Rapfi | 1000 ms | **0-6** |
| vs native Rapfi | 4000 ms (4x) | **0-2** |
| vs native Rapfi (after the tactical changes below) | 1000 ms | **0-6** |

Rapfi searches at roughly 130,000 nodes/second to depth 16-31 inside one second.
Giving the in-tree engine **four times** Rapfi's clock did not win a game, which
is the important number: the gap is structural, not a tuning deficit. No amount
of extra time closes it, so "beat Rapfi in equal time" is not reachable by
speed work.

### What was changed anyway, and what it bought

Two real weaknesses were found by analysing the losses, and both are fixed:

1. **The search had no reserved time for tactics.** The alpha-beta pass ran until
   the deadline and the safety audits ran *after* it, so their cost was paid out
   of the *next* move's budget. `choose_ai_move` now reserves
   `min(400 ms, budget/4)` up front, ends the tree search at
   `deadline - reserve`, and runs the tactical checks inside the reserve.
2. **Critical defensive points were not guaranteed to be at the root.** The
   static ranking scores a square by what it builds and blocks *at that point*,
   so the squares that stop the opponent from *building* a four look quiet. In
   the analysed loss the engine attacked while Rapfi completed an open-four
   diagonal — and an open four cannot be blocked. Every empty point where the
   opponent would make a four, broken four or open three is now forced into the
   root candidate list.

A third change was attempted and **removed**: a VCT (three-threat) prover. Its
first version returned "forced win" in a quiet four-stone position. The cause is
fundamental to the way it was written: with a bounded node budget a VCT search
can only approximate the defender's set of replies, and an approximation of "the
defender has no answer" is unsound in the direction that loses games. A sound
version needs a defender-reply generator that is complete over the relevant
threats, plus a proof-number argument, which is a bigger project than this
round — it is written up in Part 2.3 with that requirement stated. Shipping the
unsound version would have made the engine worse, not better.

The changes are verified not to regress what already existed: against the frozen
previous revision, L1 is **4-0** and L2 is **5-0-1** (paired openings, colours
swapped). The tactical suite (`tools/tactics_suite`, wired into `ctest`) pins the
four positions that matter — including the exact open-four position from the
lost Rapfi game — and all four are answered correctly in under a millisecond,
inside the level budget.

### What this means for the goal

Equal-time victory over Rapfi 2025 is not a tuning target. Rapfi is a mature
NNUE-guided engine with an opening database and a threat-space search; the
in-tree engine is a pattern-evaluation alpha-beta searcher whose evaluation
cannot see a forced win five plies away. The ordering below is therefore
deliberately different from a "make it 10% faster" list:

1. **A sound threat-space prover** (Part 2.3) — the single biggest gap. It is
   also what converts "cannot see the loss coming" into "sees it and blocks".
2. **Completeness of the defender's reply set** — the reason VCT was withdrawn.
   Without it, every forcing search is a source of fake wins.
3. **Parallel tree search** (Part 2.1) — still the cheapest real strength, and
   the only change that closes part of the 4x-time result above.
4. Everything else in Part 2, in the order given there.

Until item 1 exists, the honest statement about this engine is the narrow one:
it beats its own previous revision, it answers every tactical test correctly, and
it loses to Rapfi at equal time.


---

## Part 2 — Roadmap

Ranked by expected strength per unit of risk. The first item is by far the
largest remaining win and needs no new algorithms.

### 2.1 Parallel search (expected: large — the biggest single item)

**Situation.** The root is parallel, the tree is not. Every root candidate gets
its own worker and its own board, and a worker finishes a root completely before
taking the next. Two consequences:

1. **A single hard root is a hard floor on move time.** Measured per-root costs
   at depth 4 on one position ranged from 1.97 s to 8.17 s. With eight workers
   the whole search cannot finish before its slowest root, so the wall time is
   set by one branch.
2. **There is no cross-root information.** Two workers exploring sibling moves
   cannot prune each other, and the same position reached by different move
   orders in different subtrees is recomputed.

**Design.** Two levels, in this order:

- *Young-brothers wait* (small, low risk). In the root loop, process roots in
  order and make later roots wait while earlier ones are still running, then
  search them with the bound the earlier roots have already established.
  In Gomoku the first root is right very often, so most later roots fail low
  almost immediately. This is the classic "young brothers wait concept" and it
  typically removes most of the redundant work at the root.
- *Shared transposition table with a lockless probe* (medium risk). Today each
  worker owns a table, so a position found by one worker is invisible to the
  others. A shared table with atomic key/value entries and the existing
  depth/bound fields would let workers reuse each other's completed subtrees.
  The risk is not correctness of the search (a table hit is only a shortcut) but
  convincing ourselves of it: entries must be validated against the full Zobrist
  key, and a torn read must be detected (store key and value in one 64-bit word,
  or use a seqlock).

**Verify.** `ab_match` at fixed level with 1, 2, 4 and 8 threads must be
non-decreasing; the per-root cost distribution should show a lower maximum. This
is measurable today with the instrumentation already in `SearchStats`.

**Why first.** It is the only change that attacks the dominant cost — parallel
efficiency — rather than shaving constants. On the 2-vCPU sandbox this shows up
as throughput; on the 32-thread deployment it is the difference between the tree
being 1x and 8-16x faster.

### 2.2 Better move ordering: a real threat-space move generator

**Situation.** Candidate generation is "empty cells within Chebyshev radius 2 of
a stone", scored by the static pattern evaluation. Ordering quality is what
alpha-beta lives on, and this generator has two weaknesses: it has no notion of
"this move creates a double threat" (the classic Gomoku killer), and the width
is a fixed number per depth rather than a response to the position.

**Design.**

- Generate candidates by *threat class*, not by proximity: for each empty cell
  compute its per-direction threat kinds once (already available from
  `analyze_move`) and bucket into "makes five / makes four / makes open three /
  makes broken three / blocks opponent four / quiet". Search buckets in that
  order and let the width fall out of the bucket, e.g. always search all
  four-making moves, then as many three-making moves as the budget allows.
- Add a proper killer/history heuristic inside the tree: two slots per ply for
  quiet moves that caused a cutoff, plus a history table keyed by
  `(move, threat kind)`. The style model already does something in this
  direction at the root; this brings it inside the tree.
- Late move reductions: after the first N ordered moves, search the remainder at
  `depth - 1` and re-search only if the reduced value beats alpha. This is the
  standard way to turn better ordering into extra depth, and it is where the
  ladder's spare budget will go once the ordering is good enough.

**Verify.** Count nodes to complete a fixed-depth search before and after (a
lower node count at equal result is a pure win). `id_soundness` must keep
passing, because reductions are a heuristic — a reduced move that fails to
re-search would break the argmax invariant, so the re-search condition needs a
test.

**Why second.** It multiplies with 2.1: better ordering makes aspiration and
the projection more effective, and it is what lets the existing per-kind threat
scores actually steer the search.

### 2.3 Threat-space proof search (VCF/VCT) with transposition

**Situation.** The engine has a bounded `forced_four_search` (VCF) and a
one-reply VCT audit, both with small node budgets (320 and 48). They are used as
*safety filters* — "do not play this move if it concedes a forced win" — rather
than as a way to find a win. In Gomoku, most games between strong engines are
decided by exactly this: a VCF or VCT that the attacker proves and the defender
cannot see.

**Design.**

- Make the forcing search a first-class node type with its own transposition
  table and a "threat count" budget rather than a wall-clock budget, so its
  result is deterministic and cheap to test.
- Run it at the root *before* the general search: if a VCF exists, play it;
  if the opponent has one, take the unique defence. This is cheap (the
  branching factor of a four-only search is tiny) and removes the most common
  way the engine currently loses.
- Extend from VCF (fours only) to VCT (fours plus open threes that force a
  unique reply), keeping a node budget. The README notes broad in-tree VCT was
  tried and regressed against Rapfi and Carbon; the lesson is to keep VCT at the
  root and out of every node, not to abandon it.

**Verify.** A tactical test suite: positions with a known VCF/VCT win, and
positions where a specific defence is the only non-losing move.
`tools/tactics_suite` is now that place, and it is wired into `ctest`.

**Soundness is the whole difficulty.** An attempt at this during the Rapfi round
produced a VCT search that announced a forced win in a quiet four-stone position,
because it bounded the defender's reply set to the 14 most relevant squares. An
approximation of "the defender has no answer" fails in the direction that loses
games, so it was withdrawn rather than shipped. A sound version needs:

- a defender-reply generator that is **complete** over one-turn threats (fours,
  and threes that make a four), not merely "the likely blocks";
- proof-number or threat-sequence accounting so that a bounded search returns
  "unknown", never "win";
- the same Renju legality rules used in play for both sides at every node.

The existing `forced_four_search` already meets all three for fours only, which
is why it is used and the three-threat version is not.

**Why third.** With 2.1 and 2.2 the general search gets deeper, and deeper
search finds waiting moves but is still poor at *proofs*. Also: a deterministic,
node-budgeted prover is testable in a way a deeper alpha-beta search is not.

### 2.4 Transposition table quality

**Situation.** The table is 2¹⁶ entries per worker (about 65k, roughly 1 MB),
direct-mapped, replaced whenever the new entry is deeper or exact. On a 15x15
game the useful position count in a 4-8 s search can exceed that, and the
replacement rule throws away deep entries for shallow exact ones.

**Design.** Two-generation (aging) replacement, bucket size 2-4 with
depth-preferred replacement inside the bucket, and size it from the level
(≈1 MB at L1 up to 32 MB at L5, still per worker). Also store the move that
caused the cutoff and try it first when the entry is reused — today a hit saves
the subtree, but the ordering hint is discarded.

**Verify.** Node count at fixed depth, and hash-hit rate reported through
`SearchStats`. Pure win if the hit rate rises at equal or better quality.

**Why fourth.** Real but modest: it is a constant-factor improvement over a
search that 2.1–2.3 already make much more efficient.

### 2.5 Evaluation overhaul: scores from line features, not the pattern table

**Situation.** The pattern table is a hand-written, redundant, partly dead list
(44 rows, several unreachable — see 1.4). It is also *score-per-direction*, then
combined with ad-hoc bonuses. It has no concept of "this stone is part of two
three-threats in crossing directions", which is the actual currency of Gomoku.

**Design.** Replace it with a line-feature evaluator: for each of the 4
directions, classify the stone group into (length, open ends, single/double
gapped) tuples, and score the *combination* of directions:

| Shape | Meaning |
|---|---|
| `_XXXX_` | open four — wins next move |
| `X_XXX_X`, `XX_XX` | broken four — wins in two |
| `_XXX_` + `_XXX_` crossing | double open three — wins |
| `_XX_X_` etc. | broken three |

This is what Rapfi and Carbon do, and it is the reference for what the current
table is approximating. It also makes the per-direction information reusable by
a threat-space search (2.3), which is the real reason to do it: one classifier
serving search, evaluation and proof.

**Verify.** This is a *replacement*, so it must be gated on `ab_match` at fixed
level, and on the tactical suite. Expect it to need tuning; expect the pattern
table to be hard to beat on the first attempt, exactly as 1.4 showed.

**Why fifth despite its size.** It is the most invasive change in the list and
the one most likely to regress in the short term. Do it after 2.1–2.3 make the
search cheap enough that evaluation quality is the binding constraint, and after
the tools exist to measure it (they now do).

### 2.6 Opening book and opponent modelling, honestly

**Situation.** The engine has a style model for human opponents (attack,
defence, locality, centre, direction preferences) and no opening book at all —
games start from one centre stone and the engine's own evaluation.

**Design.**

- **Opening book**: small, curated, and only for the first 4–8 plies, on the
  three openings that actually matter. It converts the weakest part of the
  engine's knowledge (which it searches least deeply) into free strength and
  saves the clock for the middlegame. Keep it as data with a test that every
  entry is legal Renju.
- **Style model**: keep the current conservative design (style may promote
  replies, never demote sound ones). It is a *fun* feature more than a strength
  feature, and the 75% baseline floor is the right guard. Do not let it grow
  into a search ordering heuristic without `ab_match` evidence — it currently
  participates in ordering, so a bad style model directly costs depth.

**Verify.** `ab_match` against the same revision with the book/without it, from
the same openings.

**Why last.** Real but bounded gains, and the opening book is the only item here
that can be done by someone who does not want to touch the search at all.

### 2.7 What not to do

- **Do not chase throughput alone.** 1.4 is the cautionary tale: a change that
  halved matcher work scored 2-2. Throughput is only strength if the search
  converts it into depth, and only `ab_match` tells you whether it did.
- **Do not ship the neural path as the primary expert.** The ensemble picks the
  classical engine against Legacy because the neural policy regressed that
  matchup. The bundled network is a policy prior, not a proof. Use it to order
  moves (cheap, safe) rather than to choose them.
- **Do not trust single games.** The current evidence base is ten paired games.
  Before any claim of "stronger" that will be quoted, run at least 30 paired
  openings per level; the tooling makes that a background job, not a blocker.
- **Do not put broad VCT inside every node.** Already tried, already measured as
  a regression against Rapfi and Carbon. Keep proofs at the root.

---

## Summary

| Change | State | Evidence |
|---|---|---|
| Packed pattern matcher | done, bit-exact | 3–4× faster evaluation; 0.88 s vs 1.96 s at L3 |
| Iterative deepening with projected stop | done | +1 ply at L2/L3/L4; uses the whole budget |
| Root bound/value bookkeeping | done | fixed a 3-game regression; now 8-1-1 |
| Determinism contract restated | done | argmax-per-reported-depth test replaces "same move for any thread count" |
| A/B referee, soundness test, self-match | done | `ab_match`, `id_soundness`, `selfmatch` |
| Parallel tree / young-brothers-wait | next | — |
| Threat-class move ordering + LMR | proposed | — |
| Root VCF/VCT prover with node budgets | proposed | — |
| Two-generation transposition table | proposed | — |
| Line-feature evaluation | proposed | — |
| Opening book | proposed | — |

### Addendum — the same class of bug, caught again

The tactical audit added in this round initially indexed the search order with
the *previous* pass's value arrays. `order` is re-permuted between passes, so the
audit looked up the wrong moves and could replace a correct answer with a worse
one. `tools/id_soundness` caught it immediately (2 mismatches in 4 positions) —
the third time this exact index-space mistake has appeared in this engine.

The structural fix is now in place: `absorb_pass` records the ranked **moves**
that produced the current answer (`best_ranked`), not indices into a mutable
array, and the audit consumes that list. Any future stage that needs "the moves
this answer beat" must take the same route.

The lesson worth carrying: when per-root state is spread across several parallel
arrays, every reordering must move all of them, and anything that outlives an
iteration must store the entity (the move), never its position.
