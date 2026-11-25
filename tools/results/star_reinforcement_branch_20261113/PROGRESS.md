# Reply-branch reinforcement

Goal remains unmet. The best complete fixed-version audit accepted 29/36.
Twelve later partial versions did not pass; they exposed repeatable draws in
Rapfi White cases 7 and 9 and unresolved failures in case 3.

This V3 learner identifies positive actions with repeated failures against
another observed opponent reply. It preserves actual episode outcomes and
searches alternative legal roots in those positions. Exactly three Rapfi White
proposals were flagged in the initial 261-episode diagnostic dataset.

All 13 CTest groups passed. The current controller first waits for the prior
full 36-game audit, then freezes and tests the new binary and learned book.
Full audits recur every third attempt. Only one fresh unchanged 36/36 pass
can meet the goal. No partial run or tail replay counts as a complete pass.

[Current controller status](../../../.run/active-star-reinforcement/status.json)
