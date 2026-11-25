# Nine-star-point acceptance audit

Completed: 36/36. Acceptance: FAIL. Accepted: 19/36.

15x15 Renju; only Black's first stone is fixed. Coordinates in this table are one-based. Both sides receive a nominal 5000 ms protocol budget; logs include actual response times.

| Opponent | Wins | Losses | Full-board draws | Invalid |
|---|---:|---:|---:|---:|
| rapfi | 9 | 7 | 2 | 0 |
| alpha | 8 | 9 | 0 | 1 |

| Black start (row,column) | Rapfi, candidate Black | Rapfi, candidate White | Alpha, candidate Black | Alpha, candidate White |
|---|---|---|---|---|
| (4, 4) | [W, 95 plies](rapfi_00.log) | [W, 60 plies](rapfi_01.log) | [L, 70 plies](alpha_00.log) | [INVALID, 56 plies](alpha_01.log) |
| (4, 8) | [W, 47 plies](rapfi_02.log) | [L, 57 plies](rapfi_03.log) | [W, 35 plies](alpha_02.log) | [L, 67 plies](alpha_03.log) |
| (4, 12) | [L, 72 plies](rapfi_04.log) | [W, 104 plies](rapfi_05.log) | [W, 63 plies](alpha_04.log) | [W, 36 plies](alpha_05.log) |
| (8, 4) | [D, 225 plies](rapfi_06.log) | [L, 47 plies](rapfi_07.log) | [W, 57 plies](alpha_06.log) | [L, 35 plies](alpha_07.log) |
| (8, 8) | [W, 53 plies](rapfi_08.log) | [L, 31 plies](rapfi_09.log) | [L, 72 plies](alpha_08.log) | [L, 39 plies](alpha_09.log) |
| (8, 12) | [W, 35 plies](rapfi_10.log) | [L, 33 plies](rapfi_11.log) | [W, 37 plies](alpha_10.log) | [L, 39 plies](alpha_11.log) |
| (12, 4) | [D, 225 plies](rapfi_12.log) | [W, 90 plies](rapfi_13.log) | [W, 51 plies](alpha_12.log) | [L, 83 plies](alpha_13.log) |
| (12, 8) | [W, 51 plies](rapfi_14.log) | [L, 61 plies](rapfi_15.log) | [L, 112 plies](alpha_14.log) | [L, 47 plies](alpha_15.log) |
| (12, 12) | [L, 80 plies](rapfi_16.log) | [W, 50 plies](rapfi_17.log) | [W, 63 plies](alpha_16.log) | [W, 36 plies](alpha_17.log) |

A loss, invalid game, missing result or incomplete board fails acceptance. Opponent illegal moves are excluded from strength wins.

The baseline and candidate suites overlapped on a 36-CPU host; the candidate resumed on the following day. These are acceptance audits, not controlled strength or Elo estimates. See `manifest.json` for hashes/settings and the candidate `audit_notes.json` for snapshot migration and source-version details.
