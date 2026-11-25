"""Orchestration tests with fake children in temporary directories, not games."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


class ReinforcementLoopTests(unittest.TestCase):
    def run_loop(self, changed_runtime=False, fail_validation=False, preserve_alpha=False, fail_alpha=False, full_audit_every=0, modern_alpha=False, book_only=False, draw_safety=False, council_ms=0, rescue_ms=0):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            initial = root / "initial"
            initial.mkdir()
            runtime = root / "runtime"
            config = runtime / "rapfi/config.toml"
            config.parent.mkdir(parents=True)
            config.write_text("unchanged opponent")
            (initial / "manifest.json").write_text(json.dumps({
                "runtime_root": str(runtime),
                "sha256": {str(config): hashlib.sha256(config.read_bytes()).hexdigest()}}))
            (initial / "summary.json").write_text(json.dumps({
                "complete": True, "passed": False,
                "games": [{"opponent": opponent, "case": case, "passed": False,
                           "player": case % 2 + 1, "winner": 2 - case % 2,
                           "plies": 35, "error": "none", "exit_code": 1}
                          for opponent in ("rapfi", "alpha") for case in range(18)]}))
            alpha_policy = root / "alpha.txt"
            if preserve_alpha:
                summary_path = initial / "summary.json"
                summary = json.loads(summary_path.read_text())
                for game in summary["games"]:
                    if game["opponent"] == "alpha":
                        game.update(passed=True, winner=game["player"], exit_code=0)
                summary_path.write_text(json.dumps(summary))
                state = "0" * 112 + "1" + "0" * 112
                alpha_policy.write_text("GOMOKU_EXPERIENCE_V1\n5 2 " + state + " 8 8 3 4\n")
                if modern_alpha:
                    alpha_policy.write_text("GOMOKU_EXPERIENCE_V3\n5 2 " + state + " 8 8 3 4 0\n")
            benchmark, trainer = root / "benchmark", root / "trainer"
            benchmark.write_text("""#!/usr/bin/env python3
import os,sys,time
from pathlib import Path
opponent,case=map(int,sys.argv[2:4])
point=case//2
if os.environ.get('LOOP_UNIT_EXPECT_ALPHA') and not os.environ.get('LOOP_UNIT_FAIL_ALPHA') and opponent==5 and os.environ.get('GOMOKU_ALPHA_LEGACY')!='1':sys.exit(9)
if os.environ.get('LOOP_UNIT_EXPECT_MODERN') and opponent==5 and os.environ.get('GOMOKU_ALPHA_LEGACY'):sys.exit(9)
if os.environ.get('LOOP_UNIT_EXPECT_BOOK_ONLY') and os.environ.get('GOMOKU_RAPFI_BOOK_ONLY')!='1':sys.exit(9)
if os.environ.get('LOOP_UNIT_EXPECT_DRAW_SAFETY') and os.environ.get('GOMOKU_BLACK_DRAW_SAFETY')!='1':sys.exit(9)
if os.environ.get('LOOP_UNIT_EXPECT_COUNCIL') and os.environ.get('GOMOKU_ROOT_COUNCIL_MS')!=os.environ['LOOP_UNIT_EXPECT_COUNCIL']:sys.exit(9)
if os.environ.get('LOOP_UNIT_EXPECT_RESCUE') and os.environ.get('GOMOKU_COUNCIL_RESCUE_MS')!=os.environ['LOOP_UNIT_EXPECT_RESCUE']:sys.exit(9)
with Path(os.environ['LOOP_UNIT_COUNTER']).open('a') as out:out.write('game\\n')
fail=os.environ.get('LOOP_UNIT_FAIL') and opponent==4 and case==3
fail=fail or (os.environ.get('LOOP_UNIT_FAIL_ALPHA') and opponent==5 and case==3 and os.environ.get('GOMOKU_ALPHA_LEGACY')=='1')
if os.environ.get('LOOP_UNIT_FAIL') and not fail:time.sleep(0.05)
print(f'star_case={case} opponent={opponent} start={3+4*(point//3)},{3+4*(point%3)} lumen={"black" if case%2==0 else "white"} winner={1 if fail else 0} plies={35 if fail else 225} status={"FAIL" if fail else "PASS"} error=none')
sys.exit(1 if fail else 0)
""")
            trainer.write_text("""#!/usr/bin/env python3
from pathlib import Path
import sys
book=Path(sys.argv[1]);book.write_text('GOMOKU_EXPERIENCE_V2\\n')
Path(str(book)+'.training.tsv').write_text('')
""")
            benchmark.chmod(0o755)
            trainer.chmod(0o755)
            if changed_runtime:
                config.write_text("different opponent")
            work, results = root / "work", root / "results"
            counter = root / "counter"
            command = [
                sys.executable, str(Path(__file__).with_name("reinforce_star_experience.py")),
                "--after-run", str(initial), "--binary", str(benchmark), "--trainer", str(trainer),
                "--work", str(work), "--results", str(results), "--rounds", "1" if fail_validation else "2", "--jobs", "3", "--threads", "1"]
            if preserve_alpha:
                command += ["--preserve-alpha", str(alpha_policy)]
            if full_audit_every:
                command += ["--full-audit-every", str(full_audit_every)]
            if book_only:
                command.append("--rapfi-book-only")
            if draw_safety:
                command.append("--black-draw-safety")
            if council_ms:
                command += ['--root-council-ms', str(council_ms)]
            if rescue_ms:
                command += ['--council-rescue-ms', str(rescue_ms)]
            process = subprocess.run(command,
                env={**os.environ, "LOOP_UNIT_COUNTER": str(counter), "LOOP_UNIT_FAIL": "1" if fail_validation else "",
                     "LOOP_UNIT_EXPECT_ALPHA": "1" if preserve_alpha and not modern_alpha else "",
                     "LOOP_UNIT_EXPECT_BOOK_ONLY": "1" if book_only else "",
                     "LOOP_UNIT_EXPECT_DRAW_SAFETY": "1" if draw_safety else "",
                     "LOOP_UNIT_EXPECT_COUNCIL": str(council_ms) if council_ms else "",
                     "LOOP_UNIT_EXPECT_RESCUE": str(rescue_ms) if rescue_ms else "",
                     "LOOP_UNIT_EXPECT_MODERN": "1" if modern_alpha else "", "LOOP_UNIT_FAIL_ALPHA": "1" if fail_alpha else ""},
                capture_output=True, text=True)
            status = json.loads((work / "status.json").read_text())
            count = len(counter.read_text().splitlines()) if counter.exists() else 0
            return process.returncode, status, count, (work / "model-03").exists(), process.stderr

    def test_each_version_plays_all_36_fresh_games_and_stops_on_success(self):
        code, status, count, third_round, error = self.run_loop()
        self.assertEqual(code, 0, error)
        self.assertTrue(status["goal_met"])
        self.assertEqual(count, 36)
        self.assertFalse(third_round)

    def test_changed_opponent_aborts_before_training_or_validation(self):
        code, status, count, _, error = self.run_loop(changed_runtime=True)
        self.assertEqual(code, 2, error)
        self.assertFalse(status["goal_met"])
        self.assertEqual(status["phase"], "runtime_changed")
        self.assertEqual(count, 0)

    def test_failed_partial_versions_retrain_without_claiming_36_game_success(self):
        code, status, count, _, error = self.run_loop(fail_validation=True)
        self.assertEqual(code, 1, error)
        self.assertFalse(status["goal_met"])
        self.assertEqual(status["phase"], "rounds_exhausted")
        self.assertGreater(count, 0)
        self.assertLess(count, 36)

    def test_a_successful_alpha_policy_is_preserved_in_fresh_validation(self):
        code, status, count, third_round, error = self.run_loop(preserve_alpha=True)
        self.assertEqual(code, 0, error)
        self.assertTrue(status["goal_met"])
        self.assertEqual(count, 36)
        self.assertFalse(third_round)

    def test_modern_alpha_profile_does_not_enable_legacy_search_routing(self):
        code, status, count, third_round, error = self.run_loop(preserve_alpha=True, modern_alpha=True)
        self.assertEqual(code, 0, error)
        self.assertTrue(status["goal_met"])
        self.assertEqual(count, 36)
        self.assertFalse(third_round)

    def test_experimental_policies_are_forwarded_to_each_frozen_game(self):
        code, status, count, third_round, error = self.run_loop(book_only=True, draw_safety=True)
        self.assertEqual(code, 0, error)
        self.assertTrue(status["goal_met"])
        self.assertEqual(count, 36)
        self.assertFalse(third_round)

    def test_common_arbitration_budget_is_forwarded(self):
        code, status, count, third_round, error = self.run_loop(council_ms=1500)
        self.assertEqual(code, 0, error)
        self.assertTrue(status['goal_met'])
        self.assertEqual(count, 36)
        self.assertFalse(third_round)

    def test_optional_rescue_budget_is_explicit_in_frozen_games(self):
        code, status, count, _, error = self.run_loop(council_ms=3500,rescue_ms=5000)
        self.assertEqual(code,0,error)
        self.assertTrue(status['goal_met'])
        self.assertEqual(count,36)

    def test_periodic_full_audit_finishes_all_games_after_an_early_loss(self):
        code, status, count, _, error = self.run_loop(fail_validation=True, full_audit_every=3)
        self.assertEqual(code, 1, error)
        self.assertFalse(status["goal_met"])
        self.assertEqual(status["phase"], "rounds_exhausted")
        self.assertEqual(count, 36)

    def test_a_new_alpha_failure_releases_the_frozen_policy_for_learning(self):
        code, status, count, third_round, error = self.run_loop(preserve_alpha=True, fail_alpha=True)
        self.assertEqual(code, 0, error)
        self.assertTrue(status["goal_met"])
        self.assertGreater(count, 36)
        self.assertTrue(third_round)


if __name__ == "__main__":
    unittest.main()
