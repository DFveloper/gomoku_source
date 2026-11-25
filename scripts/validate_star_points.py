#!/usr/bin/env python3
"""Run all 36 real games; a crash, missing game or loss fails acceptance.

Each process runs the production choose_strong_move path, including its parallel
experts. Games are isolated processes to keep clocks and search state separate.
"""
import argparse
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time
from report_star_points import write_report


def main():
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=root / "build/tools/engine_benchmark")
    parser.add_argument("--runtime", type=Path, default=root / "third_party/runtime")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--resume", action="store_true", help="resume a saved, unchanged runtime and executable snapshot")
    parser.add_argument("--jobs", type=int, help="parallel games; default fits the requested worker budget")
    parser.add_argument("--threads", type=int)
    parser.add_argument("--game-timeout", type=int, default=3600)
    parser.add_argument("--order", choices=("standard", "hard-first"), default="standard")
    parser.add_argument("--fail-fast", action="store_true", help="stop starting games after a loss; a partial run can never pass")
    args = parser.parse_args()
    manifest_path = args.output / "manifest.json"
    results = []
    if args.resume:
        if not manifest_path.is_file() or not (args.output / "summary.json").is_file():
            parser.error("resume requires manifest.json and summary.json")
        manifest = json.loads(manifest_path.read_text())
        saved_threads = manifest.get("requested_threads", manifest.get("local_threads", 1))
        if args.threads is not None and args.threads != saved_threads:
            parser.error("resume must use the original thread allocation")
        args.threads = saved_threads
        results = json.loads((args.output / "summary.json").read_text())["games"]
    elif args.threads is None:
        args.threads = 1
    if args.jobs is None:
        args.jobs = max(1, min(8, (os.cpu_count() or 1) // max(3, args.threads)))
    if not 1 <= args.jobs <= 36 or not 1 <= args.threads <= 32 or args.game_timeout < 1:
        parser.error("jobs must be 1..36, threads 1..32, timeout positive")
    binary, runtime = args.binary.resolve(), args.runtime.resolve()
    settings={name:os.environ.get(name) for name in (
        "GOMOKU_SAFE_ROUTER", "GOMOKU_RETAINED_SEARCH", "GOMOKU_MCTS_SEARCH",
        "GOMOKU_RECHECK_MS", "GOMOKU_ALTERNATIVE_MS", "GOMOKU_ROLLOUT_MS",
        "GOMOKU_CANONICAL_SEARCH", "GOMOKU_OPENING_CENTER", "GOMOKU_OPENING_OFFSET",
        "GOMOKU_NONLOSS_OBJECTIVE", "GOMOKU_JUDGE_MS", "GOMOKU_PRIMARY_PROGRAM", "GOMOKU_ENSEMBLE_EXPERT",
        "GOMOKU_TANGENTIAL_OPENING", "GOMOKU_SYMMETRY_COUNCIL", "GOMOKU_EXPERIENCE_BOOK",
        "GOMOKU_ALPHA_LEGACY", "GOMOKU_RAPFI_BOOK_ONLY", "GOMOKU_BLACK_DRAW_SAFETY", "GOMOKU_ROOT_COUNCIL_MS", "GOMOKU_COUNCIL_RESCUE_MS")}
    try:
        council_ms=int(settings.get('GOMOKU_ROOT_COUNCIL_MS') or 0)
        extra_ms=int(settings.get('GOMOKU_COUNCIL_RESCUE_MS') or 0)
    except ValueError:
        parser.error('council budgets must be integer milliseconds')
    if not 0 <= council_ms <= 4000 or not 0 <= extra_ms <= 10000 or (extra_ms and not council_ms):
        parser.error('invalid council budget or rescue without a root council')
    if not args.resume and not binary.is_file():
        parser.error(f"build the benchmark first: {binary}")
    args.output.mkdir(parents=True, exist_ok=True)
    # Refuse to silently overwrite evidence from an earlier engine revision.
    if not args.resume and any(args.output.iterdir()):
        parser.error("output directory must be empty")
    # A rebuild during a long run must not change later games' engine code.
    snapshot_binary = args.output.resolve() / "benchmark.snapshot"
    book_snapshot = args.output.resolve() / "experience.snapshot"
    book_source = Path(settings["GOMOKU_EXPERIENCE_BOOK"]).resolve() if settings["GOMOKU_EXPERIENCE_BOOK"] else None
    if args.resume:
        saved_settings = manifest.get("engine_settings", {})
        if any(saved_settings.get(name) != value for name, value in settings.items()) or any(
                value is not None for name, value in saved_settings.items() if name not in settings):
            parser.error("resume must use the original engine algorithm settings")
        saved_runtime = manifest.get("runtime_root")
        if saved_runtime is None:
            saved_runtime = next((str(Path(p).parent.parent) for p in manifest["sha256"]
                                  if p.endswith("/rapfi/config.toml")), None)
        if saved_runtime is None or runtime != Path(saved_runtime).resolve():
            parser.error("resume must use the original runtime directory")
        expected = manifest["sha256"].get(str(snapshot_binary))
        if not expected or not snapshot_binary.is_file() or hashlib.sha256(snapshot_binary.read_bytes()).hexdigest() != expected:
            parser.error("the frozen benchmark snapshot is missing or changed")
        if book_source is not None:
            expected_book = manifest["sha256"].get(str(book_snapshot))
            if not expected_book or not book_snapshot.is_file() or hashlib.sha256(book_snapshot.read_bytes()).hexdigest() != expected_book:
                parser.error("the frozen experience book is missing or changed")
        for filename, checksum in manifest["sha256"].items():
            path = Path(filename)
            primary = settings.get("GOMOKU_PRIMARY_PROGRAM")
            primary_folder = Path(primary).absolute().parent if primary else None
            is_engine_input = runtime in path.parents or (primary_folder is not None and primary_folder in path.parents)
            if is_engine_input and (not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest() != checksum):
                parser.error(f"runtime changed since the original run: {path}")
        identities = [(g["opponent"], g["case"]) for g in results]
        if len(set(identities)) != len(identities) or any(name not in ("rapfi", "alpha") or case not in range(18) for name, case in identities):
            parser.error("saved summary has duplicate or unknown games")
        manifest.setdefault("resumptions", []).append({
            "started_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
            "jobs": args.jobs, "completed_before_resume": len(results),
            "order": args.order, "fail_fast": args.fail_fast})
        manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
    else:
        shutil.copy2(binary, snapshot_binary)
        if book_source is not None:
            if not book_source.is_file():
                parser.error("experience book does not exist")
            shutil.copy2(book_source, book_snapshot)
    if not args.resume:
        provenance = {}
        inputs = [snapshot_binary, root / "engine_strength.h", root / "engine_ensemble.h",
                     root / "engine_adaptive.h", root / "engine_session.h", root / "engine_endgame.h",
                     root / "engine_experience.h", root / "engine_council.h", root / "tools/experience_train.cpp",
                     root / "gomoku_core.h", root / "engine_adapter.h",
                     root / "engine_benchmark.cpp",
                     runtime / "rapfi/pbrain-rapfi", runtime / "rapfi/config.toml",
                     runtime / "alphagomoku/pbrain-agomoku", runtime / "alphagomoku/config.json"]
        inputs += sorted((runtime / "rapfi").glob("*.bin*"))
        inputs += sorted((runtime / "alphagomoku/networks").glob("*"))
        if book_source is not None:
            inputs += [book_source, book_snapshot, Path(str(book_source)+".training.tsv")]
        if settings.get("GOMOKU_PRIMARY_PROGRAM"):
            primary=Path(settings["GOMOKU_PRIMARY_PROGRAM"]).absolute()
            inputs += [primary,primary.parent/"config.toml"]
            inputs += sorted(primary.parent.glob("*.bin*"))
        for path in inputs:
            if path.is_file():
                provenance[str(path)] = hashlib.sha256(path.read_bytes()).hexdigest()
        manifest = {"started_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
                    "jobs": args.jobs, "requested_threads": args.threads,
                    "order": args.order, "fail_fast": args.fail_fast,
                "hardware_threads": os.cpu_count(), "local_threads": 1,
                "rapfi_threads": max(1, min(args.threads, os.cpu_count() or 1, 32) - 2),
                    "alpha_threads": 1, "nominal_ms": 5000,
                    "candidate_extra_council_ms": extra_ms,
                    "runtime_root": str(runtime),
                    "engine_settings": settings,
                    "experience_snapshot": str(book_snapshot) if book_source is not None else None,
                    "rule": "15x15 Renju", "star_coordinates_zero_based": [3, 7, 11],
                    "expected_games": 36, "sha256": provenance}
        manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")

    def play(opponent, case):
        command = [str(snapshot_binary), "--star-case", str(opponent), str(case),
                   str(args.threads), str(runtime)]
        started = time.monotonic()
        try:
            # A process group allows timeout cleanup to include expert children.
            environment = dict(os.environ)
            if book_source is not None:
                environment["GOMOKU_EXPERIENCE_BOOK"] = str(book_snapshot)
            process = subprocess.Popen(command, stdout=subprocess.PIPE, env=environment,
                                       stderr=subprocess.STDOUT, start_new_session=True)
            try:
                raw, _ = process.communicate(timeout=args.game_timeout)
            except subprocess.TimeoutExpired:
                import signal
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                raw, _ = process.communicate()
                raw += b"\nGAME_TIMEOUT\n"
            output = raw.decode(errors="replace")
            code = process.returncode
        except OSError as error:
            output, code = str(error), -1
        name = "rapfi" if opponent == 4 else "alpha"
        (args.output / f"{name}_{case:02d}.log").write_text(output)
        match = re.search(r"^star_case=(\d+) opponent=(\d+) start=(\d+),(\d+) lumen=(black|white) "
                          r"winner=(\d+) plies=(\d+) .*?status=(PASS|FAIL) error=(.*)$",
                          output, re.MULTILINE)
        point = case // 2
        expected_start = (3 + 4 * (point // 3), 3 + 4 * (point % 3))
        valid = bool(match and int(match[1]) == case and int(match[2]) == opponent
                     and (int(match[3]), int(match[4])) == expected_start
                     and match[5] == ("black" if case % 2 == 0 else "white"))
        winner = int(match[6]) if valid else None
        player = case % 2 + 1
        plies = int(match[7]) if valid else 0
        complete = valid and (winner in (1, 2) or (winner == 0 and plies == 225))
        passed = (code == 0 and complete and match[8] == "PASS" and match[9] == "none"
                  and (winner == 0 or winner == player))
        result = {"opponent": name, "case": case, "player": player, "winner": winner,
                  "plies": plies, "passed": passed, "exit_code": code,
                  "error": match[9] if valid else "missing or mismatched result",
                  "seconds": round(time.monotonic() - started, 2)}
        print(f"{name} case={case:02d} {'PASS' if passed else 'FAIL'} "
              f"winner={winner} plies={plies}", flush=True)
        return result

    done = {(g["opponent"], g["case"]) for g in results}
    tasks = [(opponent, case) for opponent in (4, 5) for case in range(18)
             if (("rapfi" if opponent == 4 else "alpha"), case) not in done]
    if args.order == "hard-first":
        white_priority = {case: index for index, case in enumerate((3, 7, 9, 11, 13, 15, 1, 5, 17))}
        tasks.sort(key=lambda item: (item[1] % 2 == 0, item[0], white_priority.get(item[1], item[1])))
    remaining = iter(tasks)
    halted = args.fail_fast and any(not game["passed"] for game in results)

    def persist():
        snapshot = {"complete": len(results) == 36,
                    "passed": len(results) == 36 and all(r["passed"] for r in results),
                    "aborted_after_failure": halted and len(results) != 36,
                    "games": sorted(results, key=lambda r: (r["opponent"], r["case"]))}
        temporary = args.output / "summary.tmp"
        temporary.write_text(json.dumps(snapshot, indent=2) + "\n")
        temporary.replace(args.output / "summary.json")
        write_report(args.output, snapshot)

    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        active = set()
        def submit_next():
            if halted:
                return
            try:
                opponent, case = next(remaining)
            except StopIteration:
                return
            active.add(pool.submit(play, opponent, case))
        for _ in range(args.jobs):
            submit_next()
        while active:
            completed, _ = concurrent.futures.wait(active, return_when=concurrent.futures.FIRST_COMPLETED)
            for future in completed:
                result = future.result()
                results.append(result)
                active.remove(future)
                if args.fail_fast and not result["passed"]:
                    halted = True
            persist()
            for _ in completed:
                submit_next()
    persist()
    passed = len(results) == 36 and all(r["passed"] for r in results)
    print(f"{'PASS' if passed else 'FAIL'}: {sum(r['passed'] for r in results)}/36 games accepted")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
