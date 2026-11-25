# Direct-evidence reinforcement audits

Goal not yet achieved. The best completed frozen audit accepted 29/36; Alpha
finished 14 wins and 4 draws, while Rapfi still had seven failed conditions.

This revision retains demonstrated continuations even if other observed
continuations failed. Actual successful moves take precedence over unplayed
symmetry augmentation in every recorded position. Earlier confidence filtering
had discarded a known positive White response after `3,7 2,7 4,5` and fresh
games lost after the replacement. This correction is empirical, not a proof.

Alpha proposals and its AB non-loss policy are preserved from the previous
18/18 run until a new Alpha failure. Every new round must still complete all
36 games from its own frozen binary and book without any failure. Earlier
results and diagnostic tail repairs never substitute for fresh games.

Current status: [active controller](../../../.run/active-star-reinforcement/status.json).
Per-round results are in `round-*/LIVE_REPORT.md`. A failed partial round cannot
pass. The latest full build and all 13 CTest groups passed, including direct
midgame evidence and low-weight positive-continuation regressions.
