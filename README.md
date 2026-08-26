# Gomoku GUI build

This version keeps the original AI logic and adds a single native GUI source.
Windows uses Win32/GDI and Linux uses X11, so no heavy GUI framework is needed.

## WebUI

The WebUI uses the same C++ AI, Renju forbidden-move rules, heatmap, undo,
board-size cycle, replay files, and post-game review graph as the native GUI.
It has no web framework or runtime dependency.

```bash
cmake -S . -B build
cmake --build build --target gomoku_webui
./build/gomoku_webui --host 127.0.0.1 --port 8080 -t 32
```

Open `http://127.0.0.1:8080`. To make it reachable from other devices on the
network, use `--host 0.0.0.0`. `--port` accepts values from 1 to 65535, and the
initial board can be selected with `--board 9..30`.
Use `-t N` or `--threads N` (`N` = 1-32) to set the AI search thread limit. If omitted, the
engine selects the worker count automatically (up to its internal maximum).

### Model Match WebUI

Build and run the engine-versus-engine spectator UI separately:

```bash
cmake --build build --target gomoku_match_webui
./build/gomoku_match_webui --host 127.0.0.1 --port 8081 -t 32
```

The included source tree runs Legacy v1, Strengthened v2, and Current v3 MT
in-process. The four external engines run as child processes through Gomocup
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
- Review buttons: `PREV`, `NEXT`, `END`, `SAVE`, `LOAD`, `NEW`.
- Review files are saved to and loaded from `gomoku_replay.txt`.
- Keyboard shortcuts: `N` new game, `U` undo, `D` debug heatmap, `S` save log, `L` load replay, `Q` quit.
- Review shortcuts: `A` previous move, `D` next move, `E` final move, `S` save replay, `L` load replay.
- Optional board size argument: `gomoku_gui 19`.
- Fullscreen/aspect options: `--fullscreen --aspect 4:3 --board 15`.
- Windows monitor option: `--monitor 1`.

## Strengthened AI

The AI now keeps late-game evaluation stable by scoring the strongest durable opportunities instead of summing every empty intersection. It also extends forcing four-threat lines past the normal horizon, widens selective search as the board fills, and safely returns a draw when no legal move remains.

The default search depth is 4. Root candidates are evaluated in parallel on up to 32 CPU threads, each with an independent board copy. The useful worker count is also bounded by the available CPU concurrency and the number of root candidates. On the included one-stone 15x15 benchmark at depth 3, eight workers reduced search time from 7.4 seconds to 2.1 seconds (3.45x on the development machine).

Human moves are analyzed online for attack, defense, locality, center, and directional preferences. After neutral opening priors, the recent style profile changes which opponent replies are retained and searched first; tactical replies are still evaluated with worst-case negamax rather than assuming the prediction is always correct. At least 75% of each selective reply set comes from the style-free baseline, so a surprising human move is not discarded merely because it does not match the learned profile.

Search values now take precedence over root move-order heuristics, and low-value continuations whose useful line is blocked at both ends receive an abandonment penalty. This prevents the engine from carrying a dead plan forward after deeper search has rejected it.

After a game, review mode opens automatically with a Chess.com-style black/white win-chance graph. It includes 25/50/75% guides, move-quality markers, and a current-move win-rate tooltip. The graph uses the engine's antisymmetric position evaluation instead of the old empty-point sum.

Run the tactical, long-evaluation, draw, and style regressions with:

```powershell
.\gomoku_cli.exe test
```

The reproducible legacy-versus-strengthened match results and limitations are in [BENCHMARK_RESULTS.md](BENCHMARK_RESULTS.md).

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
