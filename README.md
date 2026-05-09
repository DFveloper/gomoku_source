# Gomoku GUI build

This version keeps the original AI logic and adds a single native GUI source.
Windows uses Win32/GDI and Linux uses X11, so no heavy GUI framework is needed.

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
