# Fresh AB reinforcement audits

The goal remains unmet. The completed prior experience version accepted 29/36:
Alpha 14 wins and 4 draws; Rapfi 6 wins, 6 losses, 5 draws and 1 invalid game.

These runs retain the earlier Alpha book and AB policy while learning Rapfi
continuations. Alpha preservation is released if a new Alpha failure occurs.
Opponents are unchanged. Each round freezes its executable and experience book.
Only a fresh 36-game pass in one round can satisfy the acceptance gate.

Current orchestration: [.run/active-star-reinforcement/status.json](../../../.run/active-star-reinforcement/status.json).
Each `round-*/LIVE_REPORT.md` records that version alone. A failed partial round
never passes and cannot contribute games to another version's score.

The full build and all 13 CTest groups passed after introducing the preserved
Alpha profile and direct opening evidence. Tests of orchestration use isolated
temporary fake engines; those tests never count as played strength games.
