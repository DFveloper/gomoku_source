# Fixed-version nine-star-point audit

Completed: 36/36. Accepted: 27/36. Gate: **FAIL**.

15×15, Black starts at each combination of rows/columns 4, 8 and 12 (one-based). The candidate plays both colours against Rapfi and Alpha. Only the first Black stone is fixed. Draws require 225 played stones. Missing, invalid and lost games cannot pass.

| Opponent | Wins | Losses | Full-board draws | Invalid | Pending |
|---|---:|---:|---:|---:|---:|
| rapfi | 1 | 5 | 10 | 2 | 0 |
| alpha | 10 | 2 | 6 | 0 | 0 |

| Black start | Rapfi, candidate Black | Rapfi, candidate White | Alpha, candidate Black | Alpha, candidate White |
|---|---|---|---|---|
| (4, 4) | [D, 225 plies](rapfi_00.log) | [D, 225 plies](rapfi_01.log) | [D, 225 plies](alpha_00.log) | [W, 55 plies](alpha_01.log) |
| (4, 8) | [INVALID, 224 plies](rapfi_02.log) | [L, 39 plies](rapfi_03.log) | [W, 31 plies](alpha_02.log) | [D, 225 plies](alpha_03.log) |
| (4, 12) | [D, 225 plies](rapfi_04.log) | [D, 225 plies](rapfi_05.log) | [W, 93 plies](alpha_04.log) | [D, 225 plies](alpha_05.log) |
| (8, 4) | [D, 225 plies](rapfi_06.log) | [L, 37 plies](rapfi_07.log) | [W, 49 plies](alpha_06.log) | [D, 225 plies](alpha_07.log) |
| (8, 8) | [D, 225 plies](rapfi_08.log) | [L, 57 plies](rapfi_09.log) | [W, 33 plies](alpha_08.log) | [L, 35 plies](alpha_09.log) |
| (8, 12) | [W, 47 plies](rapfi_10.log) | [L, 45 plies](rapfi_11.log) | [W, 33 plies](alpha_10.log) | [D, 225 plies](alpha_11.log) |
| (12, 4) | [D, 225 plies](rapfi_12.log) | [D, 225 plies](rapfi_13.log) | [D, 225 plies](alpha_12.log) | [W, 41 plies](alpha_13.log) |
| (12, 8) | [INVALID, 224 plies](rapfi_14.log) | [L, 57 plies](rapfi_15.log) | [W, 35 plies](alpha_14.log) | [L, 35 plies](alpha_15.log) |
| (12, 12) | [D, 225 plies](rapfi_16.log) | [D, 225 plies](rapfi_17.log) | [W, 41 plies](alpha_16.log) | [W, 41 plies](alpha_17.log) |

This report contains one fixed executable/model version. Training episodes and counterfactual tail replays are excluded. Opponent processes play every subsequent move; the nominal protocol budget is 5000 ms. Actual response times appear in the raw logs. Hashes and settings are in [manifest.json](manifest.json).
