#!/usr/bin/env python3
"""Render a live report for one frozen 36-game audit, never aggregate versions."""
import json
from pathlib import Path
import sys
import tempfile


def write_report(folder, summary=None):
    folder = Path(folder)
    if summary is None:
        summary = json.loads((folder / "summary.json").read_text())
    games = summary["games"]
    by_case = {(game["opponent"], game["case"]): game for game in games}

    def label(game):
        if not game:
            return "pending"
        if game.get("error") != "none" or game.get("exit_code") not in (0, 1):
            return "INVALID"
        winner, plies = game.get("winner"), game.get("plies", 0)
        if winner == 0:
            return "D" if plies == 225 else "INVALID"
        if winner not in (1, 2):
            return "INVALID"
        return "W" if winner == game["player"] else "L"

    accepted = sum(bool(game["passed"]) for game in games)
    passed = len(games) == 36 and summary.get("passed") and all(game["passed"] for game in games)
    verdict = "PASS" if passed else ("FAIL" if any(not game["passed"] for game in games) else "IN PROGRESS")
    lines = ["# Fixed-version nine-star-point audit", "",
             f"Completed: {len(games)}/36. Accepted: {accepted}/36. Gate: **{verdict}**.", "",
             "15×15, Black starts at each combination of rows/columns 4, 8 and 12 (one-based). "
             "The candidate plays both colours against Rapfi and Alpha. Only the first Black stone is fixed. "
             "Draws require 225 played stones. Missing, invalid and lost games cannot pass.", "",
             "| Opponent | Wins | Losses | Full-board draws | Invalid | Pending |",
             "|---|---:|---:|---:|---:|---:|"]
    for opponent in ("rapfi", "alpha"):
        labels = [label(by_case.get((opponent, case))) for case in range(18)]
        lines.append(f"| {opponent} | " + " | ".join(str(labels.count(item)) for item in ("W", "L", "D", "INVALID", "pending")) + " |")
    lines += ["", "| Black start | Rapfi, candidate Black | Rapfi, candidate White | Alpha, candidate Black | Alpha, candidate White |",
              "|---|---|---|---|---|"]
    for point in range(9):
        cells = []
        for opponent in ("rapfi", "alpha"):
            for colour in (0, 1):
                case = 2 * point + colour
                game = by_case.get((opponent, case))
                cells.append(f"[{label(game)}, {game.get('plies', 0)} plies]({opponent}_{case:02d}.log)" if game else "pending")
        lines.append(f"| ({4+4*(point//3)}, {4+4*(point%3)}) | " + " | ".join(cells) + " |")
    lines += ["", "This report contains one fixed executable/model version. Training episodes and "
              "counterfactual tail replays are excluded. Opponent processes play every subsequent move; "
              "the nominal protocol budget is 5000 ms. Actual response times appear in the raw logs. "
              "Hashes and settings are in [manifest.json](manifest.json).", ""]
    manifest_path=folder / 'manifest.json'
    manifest=json.loads(manifest_path.read_text()) if manifest_path.is_file() else {}
    extra=manifest.get('candidate_extra_council_ms',0)
    if extra:
        lines += [f"Against Rapfi, the candidate may spend up to {extra} additional ms deepening an uncertain common-root "
                  "evaluation in selected adverse or goal-risk positions. Opponents retain their original "
                  "5000 ms budget. This is an asymmetric compute experiment, not an equal-time strength comparison.", ""]
    with tempfile.NamedTemporaryFile(mode="w", dir=folder, prefix=".live-report-", suffix=".tmp", delete=False) as stream:
        stream.write("\n".join(lines))
        temporary = Path(stream.name)
    temporary.replace(folder / "LIVE_REPORT.md")


if __name__ == "__main__":
    write_report(Path(sys.argv[1]))
