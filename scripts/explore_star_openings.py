#!/usr/bin/env python3
"""Explore unplayed White replies as training data, never a 36-game pass."""
import argparse
from collections import deque
from concurrent.futures import ThreadPoolExecutor, wait, FIRST_COMPLETED
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def start_point(case):
    return 3 + 4 * (case // 2 // 3), 3 + 4 * (case // 2 % 3)


def observed_replies(tables, cases):
    seen = {case: set() for case in cases}
    for table in tables:
        for row in table.read_text().splitlines():
            fields = row.split('\t')
            if len(fields) < 6 or fields[1] != '4' or int(fields[2]) not in seen:
                continue
            case = int(fields[2])
            lines = Path(fields[0]).read_text().splitlines()
            encoded = next((line[6:] for line in lines if line.startswith('moves=')), '')
            moves = encoded.split()
            if len(moves) < 2:
                continue
            first, reply = (tuple(map(int, point.split(','))) for point in moves[:2])
            if first == start_point(case):
                seen[case].add(reply)
    return seen


def accepted_trial(output, code, case, offset):
    """Validate the actual referee result and prescribed first two stones."""
    header = next((line for line in output.splitlines() if line.startswith('star_case=')), '')
    fields = dict(re.findall(r'(\w+)=(\S+)', header))
    try:
        plies, winner = int(fields['plies']), int(fields['winner'])
        if code != 0 or fields['error'] != 'none' or int(fields['star_case']) != case or fields['opponent'] != '4':
            return False
        if fields['lumen'] != 'white' or winner not in (0, 2) or not 1 <= plies <= 225:
            return False
        if winner == 0 and plies != 225:
            return False
        if winner == 2 and plies < (9 if fields.get('termination') == 'black-forbidden' else 10):
            return False
        encoded = next(line[6:] for line in output.splitlines() if line.startswith('moves='))
        moves = [tuple(map(int, point.split(','))) for point in encoded.split()]
        point = start_point(case)
        if fields['start'] != f'{point[0]},{point[1]}' or len(moves) != plies or len(set(moves)) != plies:
            return False
        if any(len(m) != 2 or not all(0 <= coordinate < 15 for coordinate in m) for m in moves):
            return False
        return moves[:2] == [point, (point[0] + offset[0], point[1] + offset[1])]
    except (KeyError, ValueError, StopIteration, IndexError):
        return False


def main():
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--reference-audit', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--prior-training', type=Path, action='append', default=[])
    parser.add_argument('--cases', default='3,15')
    parser.add_argument('--radius', type=int, default=3)
    parser.add_argument('--jobs', type=int, default=3)
    parser.add_argument('--threads', type=int, default=6)
    parser.add_argument('--confirm', type=int, default=2)
    parser.add_argument('--game-timeout', type=int, default=3600)
    args = parser.parse_args()
    try:
        cases = list(dict.fromkeys(map(int, args.cases.split(','))))
    except ValueError:
        parser.error('cases must be comma-separated odd case numbers')
    if not cases or any(case not in range(1, 18, 2) for case in cases):
        parser.error('opening exploration requires White cases 1..17')
    if not 1 <= args.radius <= 6 or not 1 <= args.jobs <= 12 or not 1 <= args.threads <= 32 or not 1 <= args.confirm <= 10 or args.game_timeout < 1:
        parser.error('invalid radius, jobs, threads, confirmations or timeout')
    args.output.mkdir(parents=True, exist_ok=True)
    if any(args.output.iterdir()):
        parser.error('output directory must be empty')
    snapshot = args.output.resolve() / 'benchmark.snapshot'
    shutil.copy2(args.binary, snapshot)
    checksum = digest(snapshot)
    reference = json.loads((args.reference_audit / 'manifest.json').read_text())
    runtime = Path(reference['runtime_root']).resolve()
    runtime_hashes = {name: value for name, value in reference['sha256'].items() if runtime in Path(name).parents}
    trials, found, streaks = [], {}, {}

    def status(phase, **details):
        value = dict(phase=phase, goal_met=False, training_only=True,
                     completed=len(trials), found_cases=found, **details)
        temporary = args.output / 'status.tmp'
        temporary.write_text(json.dumps(value, indent=2) + '\n')
        temporary.replace(args.output / 'status.json')
        print(json.dumps(value), flush=True)

    def unchanged_runtime():
        return bool(runtime_hashes) and all(Path(name).is_file() and digest(Path(name)) == value
                                           for name, value in runtime_hashes.items())

    if not unchanged_runtime():
        status('runtime_changed')
        return 2
    seen = observed_replies(args.prior_training, cases)
    offsets = sorted(((r, c) for r in range(-args.radius, args.radius + 1)
                      for c in range(-args.radius, args.radius + 1) if r or c),
                     key=lambda p: (max(abs(p[0]), abs(p[1])), abs(p[0]) + abs(p[1]), p))
    per_case = {}
    for case in cases:
        sr, sc = start_point(case)
        candidates = [*offsets]
        center = (7 - sr, 7 - sc)
        if center != (0, 0) and center not in candidates:
            candidates.append(center)
        per_case[case] = [offset for offset in candidates
                          if 0 <= sr + offset[0] < 15 and 0 <= sc + offset[1] < 15
                          and (sr + offset[0], sc + offset[1]) not in seen[case]]
    queue = deque((case, per_case[case][index], 0)
                  for index in range(max(map(len, per_case.values()), default=0))
                  for case in cases if index < len(per_case[case]))
    (args.output / 'manifest.json').write_text(json.dumps(dict(
        kind='training-only opening exploration; configurations differ by trial',
        benchmark_sha256=checksum, runtime_root=str(runtime), runtime_sha256=runtime_hashes,
        cases=cases, radius=args.radius, jobs=args.jobs, threads=args.threads,
        confirmations=args.confirm, prior_training_sha256={str(p.resolve()): digest(p) for p in args.prior_training},
        unplayed_offsets=per_case, excluded_known_replies={c: sorted(seen[c]) for c in cases}), indent=2) + '\n')
    base_environment = {name: value for name, value in os.environ.items() if not name.startswith('GOMOKU_')}
    base_environment.update(GOMOKU_RETAINED_SEARCH='1', GOMOKU_NONLOSS_OBJECTIVE='1', GOMOKU_BENCH_TRACE='1')

    def play(case, offset, repeat, number):
        environment = {**base_environment, 'GOMOKU_OPENING_OFFSET': f'{offset[0]},{offset[1]}'}
        command = [str(snapshot), '--star-case', '4', str(case), str(args.threads), str(runtime)]
        try:
            game = subprocess.run(command, env=environment, capture_output=True, text=True, timeout=args.game_timeout)
            output, code = game.stdout + game.stderr, game.returncode
        except subprocess.TimeoutExpired as error:
            output = error.stdout or b''
            if isinstance(output, bytes):
                output = output.decode(errors='replace')
            output += '\nopening_trial_timeout\n'
            code = 124
        except OSError as error:
            output, code = f'opening_trial_error={error}\n', 125
        filename = f'rapfi_opening_{case:02d}_{number:05d}_{repeat:02d}.log'
        (args.output / filename).write_text(output)
        return dict(case=case, offset=list(offset), repeat=repeat, exit_code=code, log=filename,
                    accepted=accepted_trial(output, code, case, offset), training_only=True)

    status('searching', queued=len(queue))
    stopped, number = False, 0
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        running = {}
        while queue or running:
            stopped = stopped or (args.output / 'STOP').exists()
            while queue and len(running) < args.jobs and not stopped:
                case, offset, repeat = queue.popleft()
                if str(case) in found:
                    continue
                if digest(snapshot) != checksum:
                    stopped = True
                    status('benchmark_changed')
                    break
                number += 1
                running[pool.submit(play, case, offset, repeat, number)] = (case, offset, repeat)
            if not running:
                break
            complete, _ = wait(running, timeout=1, return_when=FIRST_COMPLETED)
            for future in complete:
                case, offset, repeat = running.pop(future)
                result = future.result()
                trials.append(result)
                key = (case, offset)
                streaks[key] = streaks.get(key, 0) + 1 if result['accepted'] else 0
                if result['accepted'] and str(case) not in found:
                    if streaks[key] >= args.confirm:
                        found[str(case)] = dict(offset=list(offset), consecutive_nonloss=streaks[key])
                    else:
                        queue.appendleft((case, offset, repeat + 1))
                (args.output / 'trials.json').write_text(json.dumps(trials, indent=2) + '\n')
                status('searching', queued=len(queue), active=len(running))
    if not unchanged_runtime():
        status('runtime_changed')
        return 2
    if digest(snapshot) != checksum:
        status('benchmark_changed')
        return 2
    status('stopped' if stopped else 'finished')
    return 1 if stopped else 0


if __name__ == '__main__':
    sys.exit(main())
