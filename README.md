# Gomoku GUI build

This version keeps the original AI logic and adds a single native GUI source.
Windows uses Win32/GDI and Linux uses X11, so no heavy GUI framework is needed.

## WebUI

The WebUI uses the same C++ AI, Renju forbidden-move rules, heatmap, undo,
board-size cycle, replay files, and post-game review graph as the native GUI.
It has no web framework or runtime dependency.

The AI opponent is selectable in-game: seven engines (Legacy v1, Strengthened
v2, Current v3 MT, Carbon Classic, Rapfi 2025 AVX2, AlphaGomoku MK26,
XL / Niren) at search levels 1-5. Legacy and v2 use the level as fixed search
depth; Current v3's classical fallback uses one extra ply at levels 1-2 and
fixed wall-clock budgets, while its trained neural policy is time-managed.
External engines receive the level as their depth/time control. External
runtimes are auto-detected under `third_party/runtime/` and marked
`NOT FOUND` in the dropdown when missing; if an external engine fails or
returns an illegal move mid-game, the built-in engine answers instead so a
human never gets stranded. Switching engines while the AI thinks cancels the
pending search and immediately re-asks the newly selected engine.

```bash
scripts/build_all.sh
./build/gomoku_webui --host 127.0.0.1 --port 8080 -t 32
```

The script configures CMake and builds every default project target. Use
`--debug`, `--build-dir DIR`, or the environment variables printed by
`scripts/build_all.sh --help` to customize the build. External Linux engine
runtimes remain available through the separate `third_party_engines` target.

Open `http://127.0.0.1:8080`. To make it reachable from other devices on the
network, use `--host 0.0.0.0`. `--port` accepts values from 1 to 65535, and the
initial board can be selected with `--board 9..30`.
Use `-t N` or `--threads N` (`N` = 1-32) to set the AI search thread limit. If omitted, the
engine selects the worker count automatically (up to its internal maximum).

### aeonthic.com deployment

Build and start both loopback-only WebUIs with:

```bash
./start_webuis.sh start
```

The default ports are `18899` for `/gomoku/` and `18900` for
`/gomoku_match/`. Use `status`, `stop`, `restart`, or `logs` in place of
`start` for service management. Environment variables such as
`GOMOKU_THREADS`, `GOMOKU_PORT`, and `GOMOKU_MATCH_PORT` override the defaults.
The matching Apache reverse-proxy directives are in
`deploy/apache-gomoku-paths.conf`.

The main Gomoku process also routes the dedicated Sangin page itself at
`/gomoku/sangin/`; no Sangin-specific Apache rule is needed. Sangin is fixed
to a 15x15 Renju game against Rapfi at level 5. It has no engine/level or
board-size selector, heatmap, undo, save/load, or keyboard shortcuts. A short
Renju notice is shown before each new game, including black's overline,
double-three, and double-four restrictions. Post-game move review remains
available. On both public game pages, a valid tap paints the black stone before
the AI request begins.

To build, register both WebUIs for boot, and start them immediately under
systemd, run:

```bash
sudo ./deploy/install_webui_autostart.sh
```

Defaults can be adjusted later in `/etc/default/aeonthic-gomoku`; restart
`aeonthic-gomoku.service` and `aeonthic-gomoku-match.service` after edits.

The deployment script starts both services with `--public-mode`. In that mode,
each browser keeps its game and model-match records in browser IndexedDB (with
a localStorage fallback and a cookie that marks browser-only storage). The C++
servers expose stateless move-calculation endpoints only: shared stateful APIs,
server replay loading, and server log/replay writes return `404`.

### Model Match WebUI

Build and run the engine-versus-engine spectator UI separately:

```bash
cmake --build build --target gomoku_match_webui
./build/gomoku_match_webui --host 127.0.0.1 --port 8081 -t 32
```

The included source tree runs Legacy v1 and Strengthened v2 in-process. Current
v3 MT uses its in-process tactical solver plus the bundled AlphaGomoku Renju
network as a child-process neural policy when available. The four standalone
external engines run as child processes through Gomocup
protocol adapters when their native Linux ports exist under
`third_party/runtime/` — build them with
`cmake --build build --target third_party_engines` or
`scripts/build_third_party_engines.sh`. Missing runtimes are reported in the
UI instead of being silently unavailable.
Random openings support 0-12 plies. Custom openings use left-click to place an
alternating stone and right-click or `UNDO OPENING` to undo.
The `-t N`/`--threads N` setting applies to Current v3 MT searches; the legacy
and strengthened engines retain their original single-threaded search paths.
External engine threading is configured per runtime: Rapfi follows its
`config.toml` (`default_thread_num = 0` = every core), AlphaGomoku uses its
`config.json` MCTS thread count and CPU batch size; Carbon and XL are
single-threaded engines by design.

## Ubuntu / Debian / ARM Linux

```bash
sudo apt update
sudo apt install -y build-essential libx11-dev libxi-dev
g++ -std=c++17 -O2 AI_Gomoku_GUI.cpp -o gomoku_gui -lX11 -lXi
./gomoku_gui
```

This should also work on ARM Linux boards with an X11 desktop session.

Fullscreen with aspect correction:

```bash
./gomoku_gui --fullscreen --aspect 4:3
```

Boot autostart:

```bash
chmod +x linux_install_autostart.sh linux_run_fullscreen.sh
./linux_install_autostart.sh
```

Edit `linux_run_fullscreen.sh` to change `ASPECT_RATIO` or `BOARD_SIZE`.
The Linux account must auto-login into an X11 desktop session for the desktop
autostart file to run immediately after boot.

## Windows

```powershell
g++ -std=c++17 -O2 AI_Gomoku_GUI.cpp -o gomoku_gui.exe -lgdi32 -luser32
.\gomoku_gui.exe
```

Fullscreen aspect test:

```powershell
.\windows_aspect_test.ps1
```

Edit `$AspectRatio` in `windows_aspect_test.ps1` to compare values such as
`4:3`, `5:4`, or `1.3333`. Edit `$MonitorIndex` to choose the fullscreen
monitor. `0` is the first monitor, `1` is the second monitor, and so on.

If you have CMake installed, this also works on both platforms:

```bash
cmake -S . -B build
cmake --build build
```

## Controls

- Touch or click an empty intersection to place a black stone.
- `NEW`, `UNDO`, `HEAT OFF/ON`, `SAVE`, and `SIZE` are large touch-friendly buttons.
- The native GUI adds `ENGINE` and `LEVEL` cycle buttons (one tap per step,
  unavailable runtimes skipped), mirroring the WebUI dropdowns.
- Review buttons: `PREV`, `NEXT`, `END`, `SAVE`, `LOAD`, `NEW`.
- Review files are saved to and loaded from `gomoku_replay.txt`.
- Keyboard shortcuts: `N` new game, `U` undo, `D` debug heatmap, `E` engine, `V` level, `S` save log, `L` load replay, `Q` quit.
- Review shortcuts: `A` previous move, `D` next move, `E` final move, `S` save replay, `L` load replay.
- Optional board size argument: `gomoku_gui 19`.
- Fullscreen/aspect options: `--fullscreen --aspect 4:3 --board 15`.
- Windows monitor option: `--monitor 1`.

## Strengthened AI

The AI now keeps late-game evaluation stable by scoring the strongest durable opportunities instead of summing every empty intersection. It also extends forcing four-threat lines past the normal horizon, widens selective search as the board fills, and safely returns a draw when no legal move remains.

The default search depth is 4. Root candidates are evaluated in parallel on up to 32 CPU threads, each with an independent board copy. The useful worker count is also bounded by the available CPU concurrency and the number of root candidates. On the included one-stone 15x15 benchmark at depth 3, eight workers reduced search time from 7.4 seconds to 2.1 seconds (3.45x on the development machine).

Current v3 also uses a per-worker Zobrist transposition table, so positions
reached through different move orders are reused without sharing mutable board
state between threads. Search levels are wall-clock budgets of 1, 2, 4, 8, and
16 seconds, and the level number doubles as a depth guarantee (levels 1 and 2
search one extra ply). Iterative deepening then spends whatever the budget has
left: see [Search strategy](#search-strategy) below.

On 15x15 and 20x20, Current v3 is an adaptive neural/VCT ensemble. It obtains a
candidate from the bundled trained AlphaGomoku 8x128 Renju convolutional
network with a policy level two steps above the selected level (capped at 5),
then applies Lumen's immediate-win, mandatory-defense, forbidden-move, VCF and
VCT checks. Missing model runtimes fall back automatically to the standalone
alpha-beta engine. Engine-match mode selects that classical expert directly
against Legacy, where it is measurably stronger than the neural expert.

Before the normal root search, each candidate also receives a 160-node bounded
VCF safety probe. A move that hands the opponent a proven continuous-four win
is discarded without expanding its full tree. Neural policy moves additionally
receive a one-reply VCT audit: an opponent setup is rejected only if every legal
Renju defense still leaves a bounded VCF. Timeout is treated as unknown, never
as a loss. Broad VCT inside every alpha-beta node remains disabled because it
reduced strength against Rapfi and Carbon.

Human moves are analyzed online for attack, defense, locality, center, and directional preferences. After neutral opening priors, the recent style profile changes which opponent replies are retained and searched first; tactical replies are still evaluated with worst-case negamax rather than assuming the prediction is always correct. At least 75% of each selective reply set comes from the style-free baseline, so a surprising human move is not discarded merely because it does not match the learned profile.

Search values now take precedence over root move-order heuristics, and low-value continuations whose useful line is blocked at both ends receive an abandonment penalty. This prevents the engine from carrying a dead plan forward after deeper search has rejected it.

After a game, review mode opens automatically with a Chess.com-style black/white win-chance graph. It includes 25/50/75% guides, move-quality markers, and a current-move win-rate tooltip.

The win-chance model mirrors how Chess.com builds its graph. Forced results pin the value at 100/0: a five available right now, an open four (or any double five-point) on the board, and continuous-four (VCF) chains found by a bounded forcing search. Everything else runs through the Chess.com logistic curve, fed by a tempo-aware evaluation — the side to move converts threats first, so an open three answered in time reads ~60% for its owner while the identical threat left unanswered pins the game. The curve never dips to 0% or 100% without a forced result, exactly like mate-in-N behaviour on Chess.com.

## Search strategy

The engine is a selective alpha-beta searcher whose evaluation is built from
pattern matches, so its strength is governed by three things: how fast it can
score a position, how deep it can look inside the level's budget, and whether
the value it reports for a move is trustworthy. The current revision attacks
all three.

**Pattern matching is table-driven.** Every threat score the engine has is a
function of one 15-cell line seen from one empty intersection. That line is
base-4 information, so `evaluate_line` packs it into one 32-bit word (two bits
per cell) and matches it against a pre-shifted table of every
`(pattern, alignment)` pair that can cover the centre cell. A match is one AND
plus one compare instead of a per-character loop over 44 pattern rows. This
was the single hottest routine in the profile (about 90% of search time); the
table form measured 808 ns -> 198 ns per line on the development machine,
which is the same 3-4x speedup end to end on real searches. The semantics are
identical, including the fact that `.` is a literal empty cell (not a
wildcard) and that a match only counts when its window covers the centre. The
regression suite compares the packed matcher against a literal transcription of
the old string matcher on every line of a test position, and the two agree
exactly.

**Iterative deepening spends the whole budget.** A single fixed-depth pass left
1 s of a level-1 budget and 16 s of a level-5 budget unused: on a mid-game
position the engine used 6-65% of its allowance and then returned. The search
now runs a depth ladder (`1, 3, .., nominal` plus whatever extra plies still
fit), keeps the best move of the last pass that *finished*, and reorders the
next pass by the values it just found. Two consequences matter:

- Before starting a pass, the previous pass's measured (sequential) cost is
  projected forward with a calibrated branching factor. The engine therefore
  does not start work it cannot finish, and it stops climbing when the next
  depth will not fit.
- An iteration that is already running may overrun the soft deadline by at most
  `min(1000 ms, budget/4 + 250 ms)` so its result stays usable. Anything still
  unfinished at that stop is discarded, and the last complete answer stands.
  The level number is a guarantee: the nominal depth is always attempted.

Measured on two cores with the depth the engine actually reached in brackets:

| Position | L1 | L2 | L3 | L4 | L5 |
|---|---|---|---|---|---|
| opening (8 plies) | 0.7 s [3] | 2.0 s [3] | 4.0 s [4] | 7.3 s [5] | 16.0 s [5] |
| midgame (16 plies) | 1.0 s [3] | 2.0 s [4] | 2.9 s [5] | 7.1 s [6] | 16.0 s [6] |

The same positions under the previous fixed-depth pass reached depth 2, 3, 3,
4, 5 and returned having spent only 6-65% of the same budgets.

**Root selection only trusts real values.** With aspiration the first move is
searched with a full window and later moves are compared against the leader, so
every move except the leader carries a bound rather than an exact score. Those
bounds are recorded, fail-low results are never allowed to win a tie, and the
answer is kept in a durable record that a half-finished pass cannot overwrite.
That last detail is what the A/B referee was built to catch: an earlier draft
had the reordering permute the move order but not the per-root values, so
scores were attributed to the wrong move.

### Measuring a strength change

Cross-engine tables answer "does v3 beat engine X", which is not the same
question as "did this change make the search better": a lower opponent level
can make any edit look good. Two tools answer that question directly.

```bash
scripts/build_tools.sh            # build/tools/{ab_match,id_soundness}
build/tools/ab_match 2 3 2        # LEVEL PAIRS THREADS
build/tools/id_soundness 2 8      # LEVEL POSITIONS
./build/gomoku_cli selfmatch 3 2 2 2
```

`ab_match` plays the working tree against a frozen copy of the previous
revision (`tools/fixtures/baseline_core.h`) on shared openings with colours
swapped. `id_soundness` recomputes the pass the engine says it answered from,
independently and single-threaded, and requires the returned move to be that
pass's plain argmax. `gomoku_cli selfmatch` is the portable version of the
first check (needs no fixture) and is the smoke test for "a higher level still
beats a lower one".

Results for this revision, paired openings with colours swapped, two cores,
eight V3 threads:

| Match | Level | Result |
|---|---|---|
| working tree vs frozen baseline | 2 | **5-0-1** |
| working tree vs frozen baseline | 3 | **3-1-0** |
| combined | 2-3 | **8-1-1** |

The same development matches also produced one clear negative result, which is
why the pattern table's dead rows are still in it. Compressing the unreachable
duplicate rows in `pattern_table()` (only `max score per kind` survives into
`ThreatInfo.score`) roughly halves the matcher's work, and measured 2-2 against
the baseline, versus 5-0-1 for the bit-exact build. Cheap-but-different scoring
was not a strength gain, so the scoring semantics stayed exactly as they were.

Every level's budget is still a hard cap in the sense that matters for a UI:
the engine answers within the level budget plus the bounded overrun above, and
never runs for a minute because one move looked hard.

One deliberate consequence: the engine is no longer move-for-move deterministic
across thread counts. A single fixed-depth pass always returned the same move
regardless of how many workers it had; spending the leftover budget on extra
plies means a faster machine or a shorter search legitimately reaches a
different depth and can therefore choose differently. What is guaranteed is
that the returned move is the exact argmax of the depth the engine reports, and
that the level's depth is always searched. The regression suite checks both at
1, 2, 4 and 32 threads.

Run the tactical, long-evaluation, draw, and style regressions with:

```powershell
.\gomoku_cli.exe test
```

For reproducible paired engine matches (same opening with colors swapped), use
the benchmark target. External engines are retried exactly like the Match UI:

```bash
cmake --build build --target engine_benchmark
# opponent id: 0 Legacy, 1 v2, 3 Carbon, 4 Rapfi, 5 AlphaGomoku, 6 XL
./build/engine_benchmark 4 3 1 8
# optional fifth argument sets Current v3's level independently
./build/engine_benchmark 5 3 1 8 3
```

The reproducible legacy-versus-strengthened match results and limitations are in [BENCHMARK_RESULTS.md](BENCHMARK_RESULTS.md).
The design review of where the engine's remaining strength comes from, the
roadmap for the next changes, and the tools used to measure them are in
[ENGINE_STRENGTH_PLAN.md](ENGINE_STRENGTH_PLAN.md).
The current neural/VCT profile won all measured paired games: 2-0 against
Legacy, v2, Carbon, Rapfi and XL, plus 4-0 against Alpha over two openings.
This is a regression suite, not a solved-game or statistically stable rating
claim. Level-3 Current v3 peaked at 6.81 seconds per move in that suite.

For scripted style-aware analysis, optional depth and thread limits can be supplied:

```powershell
# size/player/move-count and move history are read from stdin
.\gomoku_cli.exe genmove-history 4 8
```

## Engine vs engine GUI

`gomoku_engine_match.exe` is a self-contained spectator program. It provides
seven selectable engines:

- Legacy v1
- Strengthened v2
- Current v3 MT
- Carbon Classic
- Rapfi 2025 AVX2
- AlphaGomoku MK26 CPU
- XL / Niren

Black and white engines and search levels are independent. Opening mode can be
random (0-12 starting plies) or custom. In custom mode, left-click intersections
to place an alternating opening and right-click or press `UNDO OPENING` to undo.
After `START MATCH`, all board input and engine-setting controls are locked; only
`RESET` can terminate the match.

The external runtimes, model files and dependent DLLs are embedded in the one
GUI executable and extracted to a process-specific temporary directory at
startup. See [THIRD_PARTY_ENGINES.md](THIRD_PARTY_ENGINES.md) for algorithms,
runtime subsets, source locations and license notes.
