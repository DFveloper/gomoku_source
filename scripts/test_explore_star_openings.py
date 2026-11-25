"""Temporary fake-child orchestration checks; never actual strength games."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


class OpeningResearchTests(unittest.TestCase):
    def run_search(self, mode='wins', changed_runtime=False, exclude_known=False):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            runtime = root / 'runtime'
            config = runtime / 'rapfi/config.toml'
            config.parent.mkdir(parents=True)
            config.write_text('original opponent')
            reference = root / 'reference'
            reference.mkdir()
            (reference / 'manifest.json').write_text(json.dumps(dict(
                runtime_root=str(runtime), sha256={str(config): hashlib.sha256(config.read_bytes()).hexdigest()})))
            if changed_runtime:
                config.write_text('changed opponent')
            binary = root / 'fake-benchmark'
            binary.write_text('''#!/usr/bin/env python3
from pathlib import Path
import os,sys
case=int(sys.argv[3]);dr,dc=map(int,os.environ['GOMOKU_OPENING_OFFSET'].split(','))
sr,sc=3+4*(case//2//3),3+4*(case//2%3)
r,c=sr+dr,sc+dc
counter=Path(os.environ['OPENING_TEST_COUNTER'])
key=f'{case},{dr},{dc}'
prior=counter.read_text().splitlines() if counter.exists() else []
with counter.open('a') as out:out.write(key+'\\n')
mode=os.environ['OPENING_TEST_MODE']
if mode=='stop':Path(os.environ['OPENING_TEST_STOP']).write_text('stop after this trial')
win=mode not in ('loss',) and not (mode=='confirm_loss' and key in prior)
moves=[(sr,sc),(r,c),(14,0),(r,c+1),(14,2),(r,c+2),(13,0),(r,c+3),(13,2),(r,c+4)]
tag='replay_case' if mode=='teacher' else 'star_case'
print(f'{tag}={case} opponent=4 start={sr},{sc} lumen=white winner={2 if win else 1} plies=10 status=PASS error=none')
print('moves='+' '.join(f'{x},{y}' for x,y in moves))
sys.exit(0 if win else 1)
''')
            binary.chmod(0o755)
            output, counter = root / 'research', root / 'counter'
            command = [sys.executable, str(Path(__file__).with_name('explore_star_openings.py')),
                       '--binary', str(binary), '--reference-audit', str(reference),
                       '--output', str(output), '--cases', '3,15', '--radius', '1',
                       '--jobs', '1', '--threads', '1', '--confirm', '2']
            if exclude_known:
                log, table = root / 'known.log', root / 'training.tsv'
                log.write_text('moves=3,7 2,7 14,0 2,8 14,2 2,9 13,0 2,10 13,2 2,11\n')
                table.write_text(f'{log}\t4\t3\t2\t10\t1\n')
                command += ['--prior-training', str(table)]
            process = subprocess.run(command, env={**os.environ, 'OPENING_TEST_COUNTER': str(counter),
                                     'OPENING_TEST_MODE': mode, 'OPENING_TEST_STOP': str(output / 'STOP')},
                                     capture_output=True, text=True)
            status = json.loads((output / 'status.json').read_text())
            trials_path = output / 'trials.json'
            trials = json.loads(trials_path.read_text()) if trials_path.exists() else []
            return process.returncode, status, trials, process.stderr

    def test_confirmed_candidates_are_training_data_not_a_completed_goal(self):
        code, status, trials, error = self.run_search()
        self.assertEqual(code, 0, error)
        self.assertEqual(set(status['found_cases']), {'3', '15'})
        self.assertEqual(len(trials), 4)
        self.assertTrue(all(t['accepted'] for t in trials))
        self.assertFalse(status['goal_met'])
        self.assertTrue(status['training_only'])

    def test_failed_confirmations_do_not_produce_a_stable_candidate(self):
        code, status, trials, error = self.run_search('confirm_loss')
        self.assertEqual(code, 0, error)
        self.assertFalse(status['found_cases'])
        self.assertTrue(any(t['accepted'] for t in trials))
        self.assertFalse(status['goal_met'])

    def test_teacher_headers_cannot_be_accepted_as_production_trials(self):
        code, status, trials, error = self.run_search('teacher')
        self.assertEqual(code, 0, error)
        self.assertFalse(status['found_cases'])
        self.assertTrue(trials)
        self.assertFalse(any(t['accepted'] for t in trials))

    def test_changed_opponent_prevents_any_trial(self):
        code, status, trials, error = self.run_search(changed_runtime=True)
        self.assertEqual(code, 2, error)
        self.assertEqual(status['phase'], 'runtime_changed')
        self.assertFalse(trials)
        self.assertFalse(status['goal_met'])

    def test_previously_played_first_replies_are_not_explored_again(self):
        code, status, trials, error = self.run_search(exclude_known=True)
        self.assertEqual(code, 0, error)
        self.assertFalse(any(t['case'] == 3 and t['offset'] == [-1, 0] for t in trials))
        self.assertIn('3', status['found_cases'])

    def test_stop_finishes_the_current_trial_without_claiming_confirmation(self):
        code, status, trials, error = self.run_search('stop')
        self.assertEqual(code, 1, error)
        self.assertEqual(status['phase'], 'stopped')
        self.assertEqual(len(trials), 1)
        self.assertFalse(status['found_cases'])
        self.assertFalse(status['goal_met'])


if __name__ == '__main__':
    unittest.main()
