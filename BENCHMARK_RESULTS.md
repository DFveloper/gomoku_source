# Legacy vs strengthened engine benchmark

## Current v3 MT cross-engine audit (2026-08-27)

The repository now includes `engine_benchmark`, which uses the same in-process
search and external Gomocup adapter as the Match UI. Each row below uses paired
openings played twice with colors swapped, on 15x15 Renju with eight V3
threads. Alpha was checked with two opening pairs (four games); every other row
uses one pair. External failures are retried once, matching the UI.

| Opponent | Level | V3 result | V3 average move time | V3 maximum move time |
|---|---:|---:|---:|---:|
| Legacy v1 | 3 | 2-0 | 1.19-1.43 s | 2.49 s |
| Strengthened v2 | 2 | 2-0 | 2.44-2.95 s | 6.76 s |
| Carbon Classic | 3 | 2-0 | 3.38-3.49 s | 6.67 s |
| Rapfi 2025 AVX2 | 3 | 2-0 | 2.84-3.16 s | 6.71 s |
| XL / Niren | 3 | 2-0 | 3.03-4.24 s | 6.75 s |
| AlphaGomoku MK26 | 3 | 4-0 | 2.44-3.85 s | 6.81 s |

Current v3 is now an adaptive neural/VCT ensemble. On 15x15 and 20x20 it asks
the bundled trained 8x128 Renju convolutional policy for a candidate with a
level-3 four-second model budget, then applies Lumen's immediate tactics,
Renju legality, bounded VCF proof, and one-reply all-defense VCT audit. The
classical transposition-table alpha-beta expert remains the fallback and is
selected directly for Legacy, the sole matchup where the neural policy
regressed the previous result.

The table establishes wins in every measured game, including 4-0 against Alpha
over two openings, not general game-theoretic dominance. These small samples
are regression checks rather than statistically stable ratings; a formal
rating claim still requires many balanced opening pairs. Untrained random
MLP weights, deeper classical search, broad internal VCT, and selective
deepening were rejected because they either remained 0-2 against Alpha or
regressed Carbon/Legacy.

> Historical note: these four-game results were captured before the later root-multithreading, depth-4, conservative-style, and dead-continuation update. They remain the reproducible comparison for the preceding engine revision, not a fresh rating of the current working tree.

Date: 2026-08-26 (Asia/Seoul)

## Method

- Legacy engine: `gomoku_core.h` from commit `7252479` in a detached worktree.
- Strengthened engine: the current working-tree `gomoku_core.h`.
- Build: GCC 15.2.0, C++17, `-O2`.
- Board/rules: 15x15; black forbidden moves were checked externally after every move.
- Search: nominal depth 2 for both engines. The strengthened engine also uses its forcing-line quiescence search.
- Four games used paired openings: each of two positions was played twice with engine colors swapped.
  - Pair A starts after the center opening move `(7, 7)`.
  - Pair B starts from the first 20 plies of the saved 42-ply `gomoku_replay.txt` long game.
- Draw cap: 110 plies. No game reached the cap and no game was decided by an illegal/forbidden move.

## Results

| Game | Black | White | Winner | Total plies | Legacy time | New time |
|---:|---|---|---|---:|---:|---:|
| 1 | New | Legacy | New | 11 | 2,314 ms | 16,460 ms |
| 2 | Legacy | New | New | 12 | 3,036 ms | 17,661 ms |
| 3 | New | Legacy | New | 47 | 6,775 ms | 137,599 ms |
| 4 | Legacy | New | Legacy | 39 | 7,850 ms | 26,598 ms |

Strengthened engine: **3 wins, 1 loss, 0 draws; 75.0% win rate**. Average game length was 27.2 plies.

The paired long-game position split by color: black won both games (new in game 3, legacy in game 4). This does not show an engine-specific late-game collapse, but it does show that the strengthened engine is not unbeatable and that this particular saved position strongly favors black under the tested search budget.

The strengthened engine used about 198.3 seconds of search time versus 20.0 seconds for the legacy engine. The extra cost comes primarily from forcing-line extension, broader late-game candidate sets, long-term evaluation, and opponent-style analysis. Four games are a regression signal, not a statistically stable strength rating.
