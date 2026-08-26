# Legacy vs strengthened engine benchmark

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
