#!/usr/bin/env python3
"""Compare semantic chart fingerprints from two performance harness binaries.

Build test/performance.cpp against the baseline and candidate parser separately.
Inputs are read-only; synthetic edge cases and snapshots live under --output.
"""
import argparse
import random
import subprocess
from pathlib import Path


def synthetic_cases(output):
    rng = random.Random(20261001)
    result = []
    for index in range(300):
        lines = ["#BPM 137", "#WAV01 note.wav", "#WAV02 other.wav",
                 "#BMP01 image.png", "#BMP00 miss.png", "#BPM01 173",
                 "#STOP01 48", "#STOP02 -24", "#SCROLL01 -0.5",
                 "#SCROLL02 1", "#SPEED01 0.75", "#LNOBJ ZZ",
                 "#TITLE Case " + str(index), "#LNTYPE 2"]
        if index % 3 == 0:
            lines += ["#RANDOM 3", "#IF 1", "#4K", "#ELSEIF 2", "#8K", "#ELSE", "#6K", "#ENDIF", "#ENDRANDOM"]
        if index % 5 == 0:
            lines += ["#BASE 62"]
        for measure in range(5):
            if rng.randrange(3) == 0:
                lines += [f"#{measure:03d}02:" + rng.choice(["0.75", "1", "1.5", "2"])]
            # Independent lanes avoid the legacy parser's ownership issues with
            # overlapping note objects. Timing channels deliberately overwrite.
            for channel in ["11", "12", "31", "54", "D9", "01", "01", "03", "08", "09", "09", "SC", "SC", "SP", "04", "06", "07", "ZZ"]:
                length = rng.choice([1, 2, 3, 4, 6, 8, 12, 24])
                cells = [rng.choice(["00", "00", "01", "02", "ZZ"]) for _ in range(length)]
                if channel in ["03", "08"]:
                    cells = [rng.choice(["00", "01"]) for _ in range(length)]
                if channel == "ZZ":
                    cells = [rng.choice(["+1", " 1", "-1", "!1", "0?", "00"]) for _ in range(length)]
                lines += [f"#{measure:03d}{channel}:" + ''.join(cells) + ('0' if index % 7 == 0 else '')]
        if index % 2:
            lines = [line[:7].lower() + line[7:] for line in lines]
        path = output / f"case-{index}.bms"
        path.write_bytes(('\r\n' if index % 3 else '\n').join(lines).encode())
        result.append(path.resolve())
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('baseline', type=Path)
    p.add_argument('candidate', type=Path)
    p.add_argument('--manifest', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--workers', type=int, default=8)
    p.add_argument('--scan', action='store_true')
    args = p.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    synthetic = args.output / 'synthetic'
    synthetic.mkdir(exist_ok=True)
    paths = args.manifest.read_text().splitlines() + list(map(str, synthetic_cases(synthetic)))
    manifest = args.output / 'manifest.txt'
    manifest.write_text('\n'.join(paths) + '\n')
    for seed in [12345, 20261001]:
        modes = [('full', 'full'), ('metadata', 'metadata'), ('ready', 'ready'), ('file', 'file')]
        if args.scan:
            modes += [('scan-reference', 'scan')]
        for before_mode, after_mode in modes:
            snapshots = []
            for name, binary, mode in [('baseline', args.baseline, before_mode), ('candidate', args.candidate, after_mode)]:
                dest = args.output / f'{name}-{mode}-{seed}.txt'
                with (args.output / f'{name}-{mode}-{seed}.log').open('w') as log:
                    subprocess.run([str(binary.resolve()), str(manifest.resolve()), mode,
                                    str(args.workers), '1', str(dest.resolve()), str(seed)],
                                   stdout=log, stderr=log, check=True)
                snapshots.append(dest.read_text().splitlines())
            if snapshots[0] != snapshots[1]:
                differences = [(a, b) for a, b in zip(*snapshots) if a != b]
                for a, b in differences[:10]: print('BEFORE', a, '\nAFTER ', b)
                raise SystemExit(f'FAIL {after_mode}, seed {seed}: {len(differences)} differing charts')
            print(f'PASS {after_mode}, seed {seed}: {len(paths)} charts', flush=True)


if __name__ == '__main__':
    main()
