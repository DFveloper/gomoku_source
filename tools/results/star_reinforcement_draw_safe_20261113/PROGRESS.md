# Draw-safety experiment

Goal remains unmet. The latest best complete audit accepted 33/36: Alpha 18/18;
Rapfi 15/18. Rapfi White cases 3 and 15 lost, and Black case 4 ended invalidly.

This experimental version avoids creating permanent overline holes as Black
against Rapfi and seeks a win if a hole already exists. It also requires equal
or greater AB depth when comparing an alternative. These are search choices,
not changes to the referee or opponents, and the risk filter is not a proof.

The complete Alpha model from the successful 18/18 run is preserved, including
negative actions and reply-branch flags. All 41,913 Alpha rows have identical
SHA-256 checksums. Modern search routing remains enabled. An Alpha failure
releases preservation for subsequent attempts.

All interfaces built and all 13 CTest groups passed. A full fresh 36-game audit
is running. Only one unchanged version passing all 36 can meet the goal.
[Active controller](../../../.run/active-star-reinforcement/status.json)
