# Fixed-version nine-star-point audit

Completed: 3/36. Accepted: 0/36. Gate: **FAIL**.

15×15, Black starts at each combination of rows/columns 4, 8 and 12 (one-based). The candidate plays both colours against Rapfi and Alpha. Only the first Black stone is fixed. Draws require 225 played stones. Missing, invalid and lost games cannot pass.

| Opponent | Wins | Losses | Full-board draws | Invalid | Pending |
|---|---:|---:|---:|---:|---:|
| rapfi | 0 | 3 | 0 | 0 | 15 |
| alpha | 0 | 0 | 0 | 0 | 18 |

| Black start | Rapfi, candidate Black | Rapfi, candidate White | Alpha, candidate Black | Alpha, candidate White |
|---|---|---|---|---|
| (4, 4) | pending | pending | pending | pending |
| (4, 8) | pending | [L, 45 plies](rapfi_03.log) | pending | pending |
| (4, 12) | pending | pending | pending | pending |
| (8, 4) | pending | [L, 21 plies](rapfi_07.log) | pending | pending |
| (8, 8) | pending | [L, 35 plies](rapfi_09.log) | pending | pending |
| (8, 12) | pending | pending | pending | pending |
| (12, 4) | pending | pending | pending | pending |
| (12, 8) | pending | pending | pending | pending |
| (12, 12) | pending | pending | pending | pending |

This report contains one fixed executable/model version. Training episodes and counterfactual tail replays are excluded. Opponent processes play every subsequent move; the nominal protocol budget is 5000 ms. Actual response times appear in the raw logs. Hashes and settings are in [manifest.json](manifest.json).
