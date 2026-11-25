"""The learner must use actual terminal positions, never trust a PASS label."""
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

TRAINER = Path(sys.argv.pop(1)).resolve()


class TrainingTests(unittest.TestCase):
    def test_repeated_failure_against_another_reply_marks_a_fragile_action(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            header = "star_case=1 opponent=4 start=3,3\n"
            histories = (
                "3,3 4,4 5,5 4,5 6,6 4,6 7,7 4,7 8,9 4,8",
                "3,3 4,4 3,4 0,0 3,5 0,2 3,6 0,4 3,7",
                "3,3 4,4 3,4 0,1 3,5 0,2 3,6 0,4 3,7")
            logs = []
            for index, history in enumerate(histories):
                log = root / f"game-{index}.log"
                log.write_text(header + "moves=" + history + "\n")
                logs.append(log)
            state = ["0"] * 225
            state[3 * 15 + 3] = "1"
            for count, expected in ((2, 0), (3, 1)):
                book = root / f"book-{count}.txt"
                process = subprocess.run([str(TRAINER), str(book), *map(str, logs[:count]), str(logs[1])],
                                         capture_output=True, text=True)
                self.assertEqual(process.returncode, 0, process.stderr)
                row = next(row.split() for row in book.read_text().splitlines()[1:]
                           if row.split()[2] == "".join(state))
                self.assertEqual(int(row[7]), expected)
                self.assertEqual(int(row[5]), 4)  # Actual episode weights remain unchanged.
                self.assertEqual(int(row[6]), 4 * count)

    def test_direct_midgame_success_overrides_unplayed_symmetry(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            log, book = root / "midgame.log", root / "book.txt"
            log.write_text("star_case=0 opponent=4 start=3,3\n"
                           "moves=3,3 0,0 4,4 1,1 3,4 0,2 3,5 0,4 3,6 0,6 3,7\n")
            process = subprocess.run([str(TRAINER), str(book), str(log)], capture_output=True, text=True)
            self.assertEqual(process.returncode, 0, process.stderr)
            state = ["0"] * 225
            for r, c, side in ((3, 3, 1), (0, 0, 2), (4, 4, 1), (1, 1, 2)):
                state[r * 15 + c] = str(side)
            actions = [row.split()[3:5] for row in book.read_text().splitlines()[1:]
                       if row.split()[2] == "".join(state)]
            self.assertEqual(actions, [["3", "4"]])

    def test_outcomes_duplicates_and_invalid_games(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            header = "star_case=0 opponent=4 start=3,3 lumen=black winner=1 plies=9 status=PASS error=none\n"
            win, loss, invalid = [root / name for name in ("win.log", "loss.log", "invalid.log")]
            win.write_text(header + "moves=3,3 0,0 3,4 0,2 3,5 0,4 3,6 0,6 3,7\n")
            loss.write_text(header + "moves=3,3 0,0 3,7 0,1 7,3 0,2 11,11 0,3 6,10 0,4\n")
            invalid.write_text(header + "moves=3,3 99,99\n")
            book = root / "book.txt"
            process = subprocess.run([str(TRAINER), str(book), str(win), str(win), str(loss), str(invalid)],
                                     capture_output=True, text=True)
            self.assertEqual(process.returncode, 0, process.stderr)
            self.assertIn("training_games=2 skipped_or_duplicate=2", process.stdout)
            provenance = Path(str(book) + ".training.tsv").read_text().splitlines()
            self.assertEqual({int(row.split("\t")[3]) for row in provenance}, {1, 2})
            state = ["0"] * 225
            state[3 * 15 + 3], state[0] = "1", "2"
            selected = [row.split() for row in book.read_text().splitlines()[1:]
                        if row.split()[2] == "".join(state)]
            actions = {tuple(row[3:5]): tuple(map(int, row[5:7])) for row in selected}
            self.assertGreater(actions["3", "4"][0], 0)
            self.assertEqual(actions["3", "7"][0], 0)
            self.assertGreater(actions["3", "7"][1], 0)
            self.assertNotIn(("4", "3"), actions)
            self.assertNotIn(("7", "3"), actions)
            checksum = hashlib.sha256(book.read_bytes()).hexdigest()
            repeated = subprocess.run([str(TRAINER), str(book), str(win)], capture_output=True)
            self.assertEqual(repeated.returncode, 2)
            self.assertEqual(hashlib.sha256(book.read_bytes()).hexdigest(), checksum)

    def test_incomplete_game_cannot_supply_draw_experience(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            log, book = root / "unfinished.log", root / "book.txt"
            log.write_text("star_case=0 opponent=4 start=3,3 lumen=black winner=0 plies=225 status=PASS error=none\n"
                           "moves=3,3 4,4 5,5\n")
            process = subprocess.run([str(TRAINER), str(book), str(log)], capture_output=True, text=True)
            self.assertEqual(process.returncode, 1)
            self.assertIn("training_games=0", process.stdout)
            self.assertEqual(book.read_text().splitlines(), ["GOMOKU_EXPERIENCE_V3"])

    def test_corrected_black_foul_is_adjudicated_before_old_continuation(self):
        source = Path(__file__).parent / "fixtures/renju_legality.txt"
        line = next(line for line in source.read_text().splitlines() if line.startswith("alpha_13.log:50 "))
        history = line.split("|", 1)[0].split(" ", 1)[1].strip()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            log, book = root / "old-rules.log", root / "book.txt"
            log.write_text("star_case=13 opponent=5 start=11,3 lumen=white winner=1 plies=57 status=FAIL error=none\n"
                           "moves=" + history + " 11,11\n")
            process = subprocess.run([str(TRAINER), str(book), str(log)], capture_output=True, text=True)
            self.assertEqual(process.returncode, 0, process.stderr)
            row = Path(str(book) + ".training.tsv").read_text().strip().split("\t")
            self.assertEqual(row[1:], ["5", "13", "2", "50", "1"])

    def test_an_opponents_missing_legal_reply_is_not_learned_as_a_success(self):
        source = Path(__file__).parent / "fixtures/endgame_no_move.txt"
        history = next(line for line in source.read_text().splitlines() if line and not line.startswith("#"))
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            log, book = root / "invalid-ending.log", root / "book.txt"
            log.write_text("star_case=3 opponent=4 start=3,7 lumen=white winner=2 plies=224 status=FAIL error=illegal move\n"
                           "moves=" + history + "\n")
            process = subprocess.run([str(TRAINER), str(book), str(log)], capture_output=True, text=True)
            self.assertEqual(process.returncode, 0, process.stderr)
            row = Path(str(book) + ".training.tsv").read_text().strip().split("\t")
            self.assertEqual(row[1:], ["4", "3", "2", "224", "0"])
            self.assertTrue(all(int(line.split()[5]) == 0 for line in book.read_text().splitlines()[1:]))

    def test_preserved_alpha_proposals_are_not_replaced_by_new_training(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            state = ["0"] * 225
            state[3 * 15 + 3] = "1"
            prior, log, book = root / "prior.txt", root / "new.log", root / "book.txt"
            prior.write_text("GOMOKU_EXPERIENCE_V1\n5 2 " + "".join(state) + " 4 4 3 4\n")
            log.write_text("star_case=1 opponent=5 start=3,3 lumen=white winner=1 plies=9 status=FAIL error=none\n"
                           "moves=3,3 0,0 3,4 0,2 3,5 0,4 3,6 0,6 3,7\n")
            process = subprocess.run([str(TRAINER), str(book), "--preserve-alpha", str(prior), str(log)],
                                     capture_output=True, text=True)
            self.assertEqual(process.returncode, 0, process.stderr)
            self.assertEqual(book.read_text().splitlines()[1:], ["5 2 " + "".join(state) + " 4 4 3 4 0"])

    def test_preserved_modern_alpha_profile_keeps_all_actions_and_branch_flags(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            state = ["0"] * 225
            state[3 * 15 + 3] = "1"
            rows = ["5 2 " + "".join(state) + tail for tail in (" 4 4 3 4 0", " 4 5 0 8 0", " 4 6 1 9 1")]
            prior, log, book = root / "prior.txt", root / "new.log", root / "book.txt"
            prior.write_text("GOMOKU_EXPERIENCE_V3\n" + "\n".join(rows) + "\n")
            log.write_text("star_case=1 opponent=5 start=3,3\n"
                           "moves=3,3 0,0 3,4 0,2 3,5 0,4 3,6 0,6 3,7\n")
            process = subprocess.run([str(TRAINER), str(book), "--preserve-alpha", str(prior), str(log)],
                                     capture_output=True, text=True)
            self.assertEqual(process.returncode, 0, process.stderr)
            self.assertEqual(book.read_text().splitlines()[1:], rows)


if __name__ == "__main__":
    unittest.main()
