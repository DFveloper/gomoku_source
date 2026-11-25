#!/usr/bin/env python3
"""Train fixed experience versions and require a fresh 36-game pass.

Past successes are training data, never substituted for new validation games.
Counterfactual tail replays may train a version, but cannot pass its acceptance
gate. Failed versions may stop after a loss; incomplete runs never pass.
Every attempted version remains in its own immutable results directory.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
from report_star_points import write_report


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--after-run", type=Path, required=True)
    parser.add_argument("--seed-run", type=Path, action="append", default=[], help="also train from completed games in this run")
    parser.add_argument("--preserve-alpha", type=Path, help="retain a frozen Alpha profile until a new Alpha failure; V1 uses legacy routing")
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--trainer", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--results", type=Path, required=True)
    parser.add_argument("--rounds", type=int, default=2)
    parser.add_argument("--jobs", type=int, default=3)
    parser.add_argument("--threads", type=int, default=6)
    parser.add_argument("--fallback", choices=("ab", "mcts"), default="mcts")
    parser.add_argument("--rapfi-book-only", action="store_true", help="retain positive Rapfi continuations without empirical bans, penalties or expanded PVs")
    parser.add_argument("--black-draw-safety", action="store_true", help="enable the experimental Rapfi Black hole-risk heuristic")
    parser.add_argument("--root-council-ms", type=int, default=0, help="reserve up to 4000 ms for common Rapfi AB/MCTS/Alpha root arbitration")
    parser.add_argument("--council-rescue-ms", type=int, default=0, help="optional additional deepening in selected uncertain positions; explicitly exceeds the base clock")
    parser.add_argument("--full-audits", action="store_true", help="finish all games even after a version has already failed")
    parser.add_argument("--full-audit-every", type=int, default=0,
                        help="finish all 36 games on the first and every Nth attempt; 0 disables periodic audits")
    args = parser.parse_args()
    if args.rounds < 1 or args.full_audit_every < 0 or not 0 <= args.root_council_ms <= 4000 or not 0 <= args.council_rescue_ms <= 10000 or (args.council_rescue_ms and not args.root_council_ms) or not 1 <= args.jobs <= 36 or not 1 <= args.threads <= 32:
        parser.error("positive rounds, 1..36 jobs and 1..32 threads required")
    args.work.mkdir(parents=True, exist_ok=True)
    args.results.mkdir(parents=True, exist_ok=True)
    if any(args.work.iterdir()) or any(args.results.iterdir()):
        parser.error("work and results directories must be empty")
    binary, trainer = args.work.resolve() / "benchmark.snapshot", args.work.resolve() / "trainer.snapshot"
    shutil.copy2(args.binary, binary)
    shutil.copy2(args.trainer, trainer)
    alpha_policy = None
    if args.preserve_alpha is not None:
        alpha_policy = args.work.resolve() / "alpha-policy.snapshot"
        shutil.copy2(args.preserve_alpha, alpha_policy)
    alpha_checksum = digest(alpha_policy) if alpha_policy is not None else None
    alpha_legacy = False
    if alpha_policy is not None:
        with alpha_policy.open() as alpha_input:
            alpha_legacy = alpha_input.readline().strip() == "GOMOKU_EXPERIENCE_V1"
    keep_alpha = alpha_policy is not None
    initial_manifest = json.loads((args.after_run / "manifest.json").read_text())
    runtime = Path(initial_manifest["runtime_root"])
    runtime_hashes = {name: checksum for name, checksum in initial_manifest["sha256"].items()
                      if runtime in Path(name).parents}

    def state(phase, **details):
        value = {"phase": phase, "goal_met": phase == "passed", **details}
        temporary = args.work / "status.tmp"
        temporary.write_text(json.dumps(value, indent=2) + "\n")
        temporary.replace(args.work / "status.json")
        print(json.dumps(value), flush=True)

    def wait_for_audit(folder):
        last = None
        while True:
            summary_path = folder / "summary.json"
            summary = json.loads(summary_path.read_text()) if summary_path.is_file() else {"games": []}
            count = len(summary["games"])
            if count != last:
                state("waiting", audit=str(folder), completed=count)
                if summary_path.is_file():
                    write_report(folder, summary)
                last = count
            if summary.get("complete"):
                identities = {(game["opponent"], game["case"]) for game in summary["games"]}
                expected = {(opponent, case) for opponent in ("rapfi", "alpha") for case in range(18)}
                if count != 36 or identities != expected:
                    raise ValueError(f"completed audit does not contain the required 36 distinct games: {folder}")
                summary["passed"] = bool(summary.get("passed") and all(game["passed"] for game in summary["games"]))
                return summary
            if summary.get("aborted_after_failure"):
                if not summary["games"] or all(game["passed"] for game in summary["games"]):
                    raise ValueError(f"aborted audit has no recorded failure: {folder}")
                summary["passed"] = False
                return summary
            if (args.work / "STOP").exists():
                return None
            time.sleep(5)

    previous = args.after_run
    for iteration in range(2, args.rounds + 2):
        summary = wait_for_audit(previous)
        if summary is None:
            state("stopped", audit=str(previous))
            return 1
        if summary.get("passed"):
            state("passed", audit=str(previous), games=36)
            return 0
        if (args.work / "STOP").exists():
            state("stopped", audit=str(previous))
            return 1
        if any(game["opponent"] == "alpha" and not game["passed"] for game in summary["games"]):
            keep_alpha = False
        if keep_alpha and (not alpha_policy.is_file() or digest(alpha_policy) != alpha_checksum):
            state("alpha_policy_changed", path=str(alpha_policy))
            return 2
        for name, checksum in runtime_hashes.items():
            if not Path(name).is_file() or digest(Path(name)) != checksum:
                state("runtime_changed", path=name)
                return 2
        model_folder = args.work / f"model-{iteration:02d}"
        model_folder.mkdir()
        full_audit = args.full_audits or (args.full_audit_every > 0 and (iteration - 2) % args.full_audit_every == 0)
        book = model_folder.resolve() / "experience.txt"
        logs = set()
        for folder in (root / "tools/results/star_baseline_20261111",
                       root / "tools/results/star_candidate_20261111",
                       root / "tools/results/star_nonloss_20261112",
                       args.after_run, *args.seed_run):
            logs.update(folder.glob("*_??.log"))
        logs.update((root / ".run/analysis-v4").glob("*.log"))
        logs.update(args.results.glob("round-*/*_??.log"))
        state("training", iteration=iteration, input_logs=len(logs))
        training_command = [str(trainer), str(book)]
        if keep_alpha:
            training_command += ["--preserve-alpha", str(alpha_policy)]
        training_command += list(map(str, sorted(logs)))
        with (model_folder / "training.log").open("w") as output:
            process = subprocess.run(training_command,
                                     stdout=output, stderr=subprocess.STDOUT)
        if process.returncode:
            state("training_failed", iteration=iteration)
            return process.returncode
        source_manifest = []
        for line in Path(str(book) + ".training.tsv").read_text().splitlines():
            fields = line.split("\t")
            filename, opponent, case, winner, plies = fields[:5]
            source_manifest.append({"path": filename, "sha256": digest(Path(filename)),
                                    "opponent": int(opponent), "case": int(case),
                                    "replayed_winner": int(winner), "terminal_plies": int(plies),
                                    "goal_nonloss": bool(int(fields[5])) if len(fields)>5 else None})
        (model_folder / "manifest.json").write_text(json.dumps({
            "training_sources": source_manifest, "book_sha256": digest(book),
            "trainer_sha256": digest(trainer), "benchmark_sha256": digest(binary),
            "fallback": args.fallback, "fail_fast": not full_audit,
            "rapfi_book_only": args.rapfi_book_only, "black_draw_safety": args.black_draw_safety,
            "root_council_ms": args.root_council_ms,
            "council_rescue_ms": args.council_rescue_ms,
            "preserved_alpha_sha256": digest(alpha_policy) if keep_alpha else None,
            "preserved_alpha_legacy": bool(keep_alpha and alpha_legacy)}, indent=2) + "\n")
        environment = {name: value for name, value in os.environ.items() if not name.startswith("GOMOKU_")}
        environment.update(GOMOKU_EXPERIENCE_BOOK=str(book), GOMOKU_NONLOSS_OBJECTIVE="1", GOMOKU_BENCH_TRACE="1")
        if args.fallback == "mcts":
            environment["GOMOKU_MCTS_SEARCH"] = "1"
        if args.rapfi_book_only:
            environment["GOMOKU_RAPFI_BOOK_ONLY"] = "1"
        if args.black_draw_safety:
            environment["GOMOKU_BLACK_DRAW_SAFETY"] = "1"
        if args.root_council_ms:
            environment["GOMOKU_ROOT_COUNCIL_MS"] = str(args.root_council_ms)
        if args.council_rescue_ms:
            environment["GOMOKU_COUNCIL_RESCUE_MS"] = str(args.council_rescue_ms)
        if keep_alpha and alpha_legacy:
            environment["GOMOKU_ALPHA_LEGACY"] = "1"
        previous = args.results.resolve() / f"round-{iteration:02d}"
        state("validation", iteration=iteration, audit=str(previous), book_sha256=digest(book), full_audit=full_audit)
        command = [sys.executable, str(root / "scripts/validate_star_points.py"),
                   "--binary", str(binary), "--runtime", str(runtime), "--output", str(previous),
                   "--jobs", str(args.jobs), "--threads", str(args.threads), "--order", "hard-first"]
        if not full_audit:
            command.append("--fail-fast")
        with (model_folder / "validation.log").open("w") as output:
            result = subprocess.run(command, env=environment,
                                    stdout=output, stderr=subprocess.STDOUT)
        if result.returncode not in (0, 1):
            state("validation_failed", iteration=iteration, exit_code=result.returncode)
            return result.returncode
        summary_path = previous / "summary.json"
        saved = json.loads(summary_path.read_text()) if summary_path.is_file() else {}
        if not saved.get("complete") and not saved.get("aborted_after_failure"):
            state("validation_incomplete", iteration=iteration)
            return 2
    final = wait_for_audit(previous)
    state("passed" if final.get("passed") else "rounds_exhausted", audit=str(previous),
          completed=len(final["games"]), accepted=sum(game["passed"] for game in final["games"]))
    return 0 if final.get("passed") else 1


if __name__ == "__main__":
    sys.exit(main())
