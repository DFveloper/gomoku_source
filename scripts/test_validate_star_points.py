"""Acceptance-runner regressions using a fake benchmark, never engine ratings."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


class AcceptanceTests(unittest.TestCase):
    def run_gate(self, mode, resume=None):
        with tempfile.TemporaryDirectory() as directory:
            temporary = Path(directory)
            binary = temporary / "benchmark"
            binary.write_text("""#!/usr/bin/env python3
import os, sys, time
from pathlib import Path
opponent, case = map(int, sys.argv[2:4])
mode = os.environ['FAKE_STAR_MODE']
if mode == 'frozen_book':
    book = Path(os.environ['GOMOKU_EXPERIENCE_BOOK'])
    if book.name != 'experience.snapshot' or book.read_text() != 'initial book':
        sys.exit(4)
with open(os.environ['FAKE_CASE_COUNTER'], 'a') as counter:
    counter.write(f'{opponent},{case}\\n')
if mode == 'crash' and opponent == 5 and case == 17:
    sys.exit(3)
winner, plies = 0, 225
if mode == 'short_draw': plies = 30
if mode == 'loss' and opponent == 4 and case == 1: winner, plies = 1, 35
if mode == 'loss_fast':
    if opponent == 4 and case == 3: winner, plies = 1, 35
    else: time.sleep(0.05)
point = case // 2
row, col = 3 + 4 * (point // 3), 3 + 4 * (point % 3)
if mode == 'wrong_start': row = 0
tag = 'replay_case' if mode in ('replay_only', 'teacher_only') else 'star_case'
print(f'{tag}={case} opponent={opponent} start={row},{col} lumen={"black" if case % 2 == 0 else "white"} winner={winner} plies={plies} lumen_avg_ms=1 lumen_max_ms=1 opponent_avg_ms=1 opponent_max_ms=1 status=PASS error=none')
if mode == 'teacher_only': print('diagnostic=teacher-alpha')
""")
            binary.chmod(0o755)
            output = temporary / "results"
            runtime = temporary / "runtime"
            for relative in ("rapfi/config.toml", "alphagomoku/config.json"):
                config = runtime / relative
                config.parent.mkdir(parents=True, exist_ok=True)
                config.write_text("original configuration")
            counter = temporary / "counter"
            environment = {**os.environ, "FAKE_STAR_MODE": mode, "FAKE_CASE_COUNTER": str(counter)}
            if resume == 'changed_rescue':
                environment['GOMOKU_ROOT_COUNCIL_MS']='1500'
            book_source = temporary / "training-book.txt"
            if mode == "frozen_book":
                book_source.write_text("initial book")
                environment["GOMOKU_EXPERIENCE_BOOK"] = str(book_source)
            primary_config = temporary / "alternate-expert/config.toml"
            if resume == "changed_primary_file":
                primary_config.parent.mkdir()
                primary_config.write_text("original expert configuration")
                primary = primary_config.parent / "pbrain-rapfi"
                primary.write_text("original expert binary")
                environment["GOMOKU_PRIMARY_PROGRAM"] = str(primary)
            runner = str(Path(__file__).with_name("validate_star_points.py"))
            command = [sys.executable, runner,
                 "--binary", str(binary), "--runtime", str(runtime),
                 "--output", str(output)]
            if mode == "loss_fast":
                command += ["--jobs", "3", "--order", "hard-first", "--fail-fast"]
            process = subprocess.run(command, env=environment, capture_output=True, text=True)
            summary = json.loads((output / "summary.json").read_text())
            if mode == "loss_fast":
                self.assertLess(len(summary["games"]), 36)
                self.assertFalse(summary["complete"])
                self.assertTrue(summary["aborted_after_failure"])
            else:
                self.assertEqual(len(summary["games"]), 36)
                self.assertTrue(summary["complete"])
            if resume is not None:
                if resume == "missing_game":
                    summary["games"].pop()
                    summary["complete"] = summary["passed"] = False
                    (output / "summary.json").write_text(json.dumps(summary))
                elif resume == "changed_snapshot":
                    with (output / "benchmark.snapshot").open("a") as changed:
                        changed.write("\n# altered snapshot\n")
                elif resume == "changed_runtime_file":
                    (runtime / "rapfi/config.toml").write_text("different configuration")
                elif resume == "changed_primary_file":
                    primary_config.write_text("different expert configuration")
                elif resume == "changed_algorithm":
                    environment["GOMOKU_NONLOSS_OBJECTIVE"] = "1"
                elif resume == "changed_book_policy":
                    environment["GOMOKU_RAPFI_BOOK_ONLY"] = "1"
                elif resume == "changed_draw_safety":
                    environment["GOMOKU_BLACK_DRAW_SAFETY"] = "1"
                elif resume == "changed_council":
                    environment["GOMOKU_ROOT_COUNCIL_MS"] = "1500"
                elif resume == "changed_rescue":
                    environment["GOMOKU_COUNCIL_RESCUE_MS"] = "5000"
                elif resume == "new_unset_algorithm_option":
                    manifest_path = output / "manifest.json"
                    manifest = json.loads(manifest_path.read_text())
                    manifest["engine_settings"].pop("GOMOKU_SYMMETRY_COUNCIL")
                    manifest_path.write_text(json.dumps(manifest))
                elif resume == "changed_book_snapshot":
                    (output / "experience.snapshot").write_text("altered book")
                elif resume == "changed_source_book":
                    book_source.write_text("new training data")
                    summary["games"].pop()
                    summary["complete"] = summary["passed"] = False
                    (output / "summary.json").write_text(json.dumps(summary))
                command = [sys.executable, runner, "--output", str(output),
                           "--runtime", str(runtime), "--resume", "--jobs", "4"]
                if resume == "changed_threads":
                    command += ["--threads", "2"]
                if resume == "changed_runtime":
                    command += ["--runtime", str(temporary)]
                process = subprocess.run(command, env=environment, capture_output=True, text=True)
                summary = json.loads((output / "summary.json").read_text())
                self.assertEqual(len(counter.read_text().splitlines()),
                                 37 if resume in ("missing_game", "changed_source_book") else 36)
            return process.returncode, summary["passed"]

    def test_full_board_draws_pass(self):
        self.assertEqual(self.run_gate("draw"), (0, True))

    def test_single_loss_rejects_even_if_child_reports_pass(self):
        self.assertEqual(self.run_gate("loss"), (1, False))

    def test_failure_first_training_round_stops_without_claiming_a_complete_audit(self):
        self.assertEqual(self.run_gate("loss_fast"), (1, False))

    def test_unfinished_games_are_not_draws(self):
        self.assertEqual(self.run_gate("short_draw"), (1, False))

    def test_missing_result_rejects(self):
        self.assertEqual(self.run_gate("crash"), (1, False))

    def test_wrong_start_rejects(self):
        self.assertEqual(self.run_gate("wrong_start"), (1, False))

    def test_counterfactual_tail_replays_cannot_pass_fresh_game_acceptance(self):
        self.assertEqual(self.run_gate("replay_only"), (1, False))

    def test_teacher_diagnostics_cannot_pass_fresh_game_acceptance(self):
        self.assertEqual(self.run_gate("teacher_only"), (1, False))

    def test_resume_rejects_changed_experience_policy(self):
        self.assertEqual(self.run_gate("draw", "changed_book_policy")[0], 2)

    def test_resume_rejects_changed_draw_safety_policy(self):
        self.assertEqual(self.run_gate("draw", "changed_draw_safety")[0], 2)

    def test_resume_rejects_changed_common_root_arbitration(self):
        self.assertEqual(self.run_gate("draw", "changed_council")[0], 2)

    def test_resume_rejects_changed_adaptive_deepening_budget(self):
        self.assertEqual(self.run_gate('draw','changed_rescue')[0],2)

    def test_resume_only_runs_missing_game(self):
        self.assertEqual(self.run_gate("draw", "missing_game"), (0, True))

    def test_resume_rejects_changed_executable(self):
        self.assertEqual(self.run_gate("draw", "changed_snapshot")[0], 2)

    def test_resume_rejects_changed_threads(self):
        self.assertEqual(self.run_gate("draw", "changed_threads")[0], 2)

    def test_resume_rejects_changed_runtime_directory(self):
        self.assertEqual(self.run_gate("draw", "changed_runtime")[0], 2)

    def test_resume_rejects_changed_runtime_contents(self):
        self.assertEqual(self.run_gate("draw", "changed_runtime_file")[0], 2)

    def test_resume_rejects_changed_primary_expert_contents(self):
        self.assertEqual(self.run_gate("draw", "changed_primary_file")[0], 2)

    def test_resume_rejects_changed_algorithm_settings(self):
        self.assertEqual(self.run_gate("draw", "changed_algorithm")[0], 2)

    def test_resume_accepts_a_new_unset_option(self):
        self.assertEqual(self.run_gate("draw", "new_unset_algorithm_option"), (0, True))

    def test_resume_rejects_changed_experience_snapshot(self):
        self.assertEqual(self.run_gate("frozen_book", "changed_book_snapshot")[0], 2)

    def test_training_changes_cannot_change_the_frozen_audit_book(self):
        self.assertEqual(self.run_gate("frozen_book", "changed_source_book"), (0, True))


if __name__ == "__main__":
    unittest.main()
