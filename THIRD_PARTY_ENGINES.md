# Third-party engines embedded in Engine Match GUI

`gomoku_engine_match.exe` embeds the runtime files below as Windows RCDATA.
At startup they are extracted into a process-specific temporary directory and
communicated with through the Gomocup/Piskvork text protocol. Corresponding
source repositories are vendored under `third_party/*-src`.

## Linux / WebUI runtimes

`gomoku_match_webui` launches the same protocol over POSIX pipes against native
Linux binaries built from the vendored sources into `third_party/runtime/`:

| Engine | Runtime | Notes |
|---|---|---|
| Carbon Classic | `runtime/carbon/pbrain-carbon` | Built with the `third_party/compat/windows.h` shim plus the documented `INFO max_depth` patch. |
| Rapfi 2025 AVX2 | `runtime/rapfi/pbrain-rapfi` | Built from the vendored source (AVX2 + BMI2). The classic `model210901.bin` predates the current table layout and is unused by the mix9svq evaluator, so `config.toml` omits it. |
| AlphaGomoku MK26 CPU | `runtime/alphagomoku/pbrain-agomoku` | Requires the MinML CPU backend (`git clone https://github.com/MaciejKozarzewski/MinML`). |
| XL / Niren | `runtime/xl/pbrain-xl` | The compat shim redefines `clock()` to wall-clock milliseconds; upstream's time management assumed the Windows semantics and aborted every search otherwise. |

Build everything with `scripts/build_third_party_engines.sh` or
`cmake --build build --target third_party_engines`. The WebUI reports each
engine as unavailable when its runtime binary is missing.

| GUI name | Runtime | Evaluation/search | License/source |
|---|---|---|---|
| Carbon Classic | Carbon 2.3 | Classical expert patterns, minimax/alpha-beta, transposition table | Source-visible upstream; no explicit license file. Personal research only. `third_party/carbon-src` |
| Rapfi 2025 AVX2 | Rapfi 0.43.01 | CPU Mix9-SVQ NNUE, alpha-beta, TT, tactical search | GPL-3.0. `third_party/rapfi-src` |
| AlphaGomoku MK26 | AlphaGomoku 5.10.0 | CPU convolutional network, MCTS plus alpha-beta tactical solver | GPL-3.0. `third_party/alphagomoku-src` |
| XL / Niren | XL 2016 | Classical Renju engine, handcrafted evaluation/search | GPL-2.0. `third_party/xl-src` |

Only the laptop-compatible runtime subset is embedded:

- Rapfi uses the AVX2 executable, `model210901.bin`, and the required evaluator
  files including the Renju black/white weights.
- AlphaGomoku uses the CPU runtime DLLs and only the Renju network. Current v3
  also uses this trained 8x128 policy as its optional neural expert, followed
  by Lumen's independent Renju/VCF/VCT validation; the classical engine remains
  the automatic fallback when the runtime is absent.
- Carbon and XL are compact standalone executables.

The GUI sends both `INFO max_depth` and a calibrated turn-time budget. Legacy,
v2, and the locally patched Carbon profile use an exact fixed depth. Current v3
adds one ply at levels 1-2 under hard time budgets; Rapfi honors its own
depth/time controls. AlphaGomoku is MCTS-based and XL is
time-managed, so for those two the displayed depth acts as a search level whose
time budget doubles at each step (250 ms at level 1 through 8 s at level 6).

## Local Carbon integration patch

Carbon's existing fixed-depth `AICarbon::yourTurn` API is exposed through a
small Gomocup extension, `INFO max_depth N`. No evaluation/search logic was
changed.
