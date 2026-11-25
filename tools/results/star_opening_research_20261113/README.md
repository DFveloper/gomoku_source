# Training-only opening exploration

This experiment tests 77 unplayed White replies across Rapfi cases 3 and 15.
It uses the unchanged real opponent and a frozen production benchmark. Each
trial's first response is a separate configuration. A response is only marked
promising after two consecutive non-losses. Those confirmations are training
evidence, never a 36-game pass, and goal_met remains false throughout.

See manifest.json for prior exclusions and hashes, trials.json for actual game
results, and status.json for progress. The candidate must subsequently pass a
fresh full 36-game audit with a single fixed book and executable.
