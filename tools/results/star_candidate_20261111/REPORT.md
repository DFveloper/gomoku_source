# Nine-star-point acceptance audit

Completed: 36/36. Acceptance: FAIL. Accepted: 25/36.

15x15 Renju; only Black's first stone is fixed. Coordinates in this table are one-based. Both sides receive a nominal 5000 ms protocol budget; logs include actual response times.

| Opponent | Wins | Losses | Full-board draws | Invalid |
|---|---:|---:|---:|---:|
| rapfi | 11 | 5 | 2 | 0 |
| alpha | 12 | 5 | 0 | 1 |

| Black start (row,column) | Rapfi, candidate Black | Rapfi, candidate White | Alpha, candidate Black | Alpha, candidate White |
|---|---|---|---|---|
| (4, 4) | [L, 76 plies](rapfi_00.log) | [W, 112 plies](rapfi_01.log) | [L, 80 plies](alpha_00.log) | [W, 50 plies](alpha_01.log) |
| (4, 8) | [W, 35 plies](rapfi_02.log) | [L, 57 plies](rapfi_03.log) | [W, 31 plies](alpha_02.log) | [L, 59 plies](alpha_03.log) |
| (4, 12) | [D, 225 plies](rapfi_04.log) | [L, 91 plies](rapfi_05.log) | [W, 59 plies](alpha_04.log) | [W, 70 plies](alpha_05.log) |
| (8, 4) | [W, 33 plies](rapfi_06.log) | [L, 51 plies](rapfi_07.log) | [W, 43 plies](alpha_06.log) | [INVALID, 136 plies](alpha_07.log) |
| (8, 8) | [W, 31 plies](rapfi_08.log) | [W, 80 plies](rapfi_09.log) | [W, 39 plies](alpha_08.log) | [L, 57 plies](alpha_09.log) |
| (8, 12) | [W, 23 plies](rapfi_10.log) | [W, 92 plies](rapfi_11.log) | [W, 33 plies](alpha_10.log) | [W, 62 plies](alpha_11.log) |
| (12, 4) | [D, 225 plies](rapfi_12.log) | [W, 102 plies](rapfi_13.log) | [W, 49 plies](alpha_12.log) | [L, 57 plies](alpha_13.log) |
| (12, 8) | [W, 43 plies](rapfi_14.log) | [L, 57 plies](rapfi_15.log) | [W, 33 plies](alpha_14.log) | [L, 37 plies](alpha_15.log) |
| (12, 12) | [W, 71 plies](rapfi_16.log) | [W, 102 plies](rapfi_17.log) | [W, 71 plies](alpha_16.log) | [W, 80 plies](alpha_17.log) |

A loss, invalid game, missing result or incomplete board fails acceptance. Opponent illegal moves are excluded from strength wins.

The baseline and candidate suites overlapped on a 36-CPU host; the candidate resumed on the following day. These are acceptance audits, not controlled strength or Elo estimates. See `manifest.json` for hashes/settings and the candidate `audit_notes.json` for snapshot migration and source-version details.

The candidate requested six workers: four for Rapfi, one for Alpha and one for local search. The source also now supports hardware-capped automatic allocation; explicit six-worker allocation with both experts present is unchanged.

[Additional 24-worker diagnostic](diagnostic_white_edge_requested24threads.log): Rapfi, case 3, candidate White, loss in 51 plies. This game is excluded from the 36-game score.

[Bounded VCF replay audit](diagnostic_lost_game_vcf_audit.log): ten valid lost games, 307 positions, 16 attacks and 22,000 nodes per position; no winning VCF proof detected. This does not prove no stronger move exists.
