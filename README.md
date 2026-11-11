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

Build all targets with CMake, Ninja and w64devkit (automatically detected in
`%USERPROFILE%/Downloads/w64devkit` or `C:/w64devkit`):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build_windows.ps1 -Test
.\.run\build-windows\gomoku_sangin.exe
.\.run\build-windows\gomoku_engine_match.exe
```

Use `-ToolchainBin C:/path/to/bin` for another MinGW toolchain. The script uses
`.run/build-windows` to avoid reusing Linux CMake caches in `build/`.
Sangin is a native Win32/GDI window with 15x15 Renju, Rapfi level 5, a rule
notice before each game, and post-game review. Engine/level/size controls,
undo, heatmap, file controls and gameplay shortcuts are disabled there.
Keep this source tree's `third_party/runtime` directory for Sangin's Rapfi
runtime. Its bundled AVX2 executable requires an AVX2-capable CPU; engine
failures use the existing built-in fallback. The native match app embeds
the bundled external engines and can run independently of a browser.

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
state between threads. Search levels have hard wall-clock budgets of 1, 2, 4,
8, and 16 seconds; only fully completed root results are eligible for
selection when the budget expires. Levels 1 and 2 automatically search one
extra ply because the transposition table keeps them within the same practical
time envelope. This prevents a difficult move from running for a minute while
preserving deterministic completed searches.

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
