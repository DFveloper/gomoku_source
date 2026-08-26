# Third-party engines embedded in Engine Match GUI

`gomoku_engine_match.exe` embeds the runtime files below as Windows RCDATA.
At startup they are extracted into a process-specific temporary directory and
communicated with through the Gomocup/Piskvork text protocol. Corresponding
source repositories are vendored under `third_party/*-src`.

| GUI name | Runtime | Evaluation/search | License/source |
|---|---|---|---|
| Carbon Classic | Carbon 2.3 | Classical expert patterns, minimax/alpha-beta, transposition table | Source-visible upstream; no explicit license file. Personal research only. `third_party/carbon-src` |
| Rapfi 2025 AVX2 | Rapfi 0.43.01 | CPU Mix9-SVQ NNUE, alpha-beta, TT, tactical search | GPL-3.0. `third_party/rapfi-src` |
| AlphaGomoku MK26 | AlphaGomoku 5.10.0 | CPU convolutional network, MCTS plus alpha-beta tactical solver | GPL-3.0. `third_party/alphagomoku-src` |
| XL / Niren | XL 2016 | Classical Renju engine, handcrafted evaluation/search | GPL-2.0. `third_party/xl-src` |

Only the laptop-compatible runtime subset is embedded:

- Rapfi uses the AVX2 executable, `model210901.bin`, and the required evaluator
  files including the Renju black/white weights.
- AlphaGomoku uses the CPU runtime DLLs and only the Renju network.
- Carbon and XL are compact standalone executables.

The GUI sends both `INFO max_depth` and a calibrated turn-time budget. Built-in
engines and the locally patched Carbon profile use an exact fixed depth. Rapfi
honors its own depth/time controls. AlphaGomoku is MCTS-based and XL is
time-managed, so for those two the displayed depth acts as a search level whose
time budget doubles at each step (250 ms at level 1 through 8 s at level 6).

## Local Carbon integration patch

Carbon's existing fixed-depth `AICarbon::yourTurn` API is exposed through a
small Gomocup extension, `INFO max_depth N`. No evaluation/search logic was
changed.
