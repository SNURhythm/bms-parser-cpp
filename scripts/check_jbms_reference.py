#!/usr/bin/env python3
"""Compare the current C++ parser with a freshly compiled local jbms-parser.

Builds stay under --output. No changes are made to the reference repository.
The classpath supplies only the unused BMSON decoder dependency; every core
BMS model/decoder source is compiled from --reference.
"""

import argparse
import json
import math
import os
from pathlib import Path
import random
import shlex
import subprocess


ROOT = Path(__file__).resolve().parents[1]
HEADER = "#TITLE audit\n#BPM 120\n#WAV01 head.wav\n#WAV02 tail.wav\n"
CASES = {
    "volwav_integer": "#VOLWAV -123\n#00011:01\n",
    "volwav_invalid_preserves": "#VOLWAV 42\n#VOLWAV 1.5\n#VOLWAV 2147483648\n#VOLWAV junk\n",
    "volwav_lexical": "#VOLWAVx+17\n",
    "custom_values": "%key  value  \n@key overwritten\n%Key case-sensitive\n% empty-key\n%tab\tignored\n%empty \n%spaces   \n #TITLE ignored\n",
    "custom_skipped_branch": "#RANDOM 3\n#IF 2\n%key retained\n@other retained too\n#VOLWAV 12\n#ENDIF\n#ENDRANDOM\n",
    "random_multiple_branches": "#RANDOM 3\n#IF 1\n#TITLE branch1\n#00011:01\n#ENDIF\n#IF 2\n#TITLE branch2\n#00012:02\n#ENDIF\n#IF 3\n#TITLE branch3\n#00013:01\n#ENDIF\n#ENDRANDOM\n",
    "random_nested_and_sequential": "#RANDOM 3\n#IF 1\n#RANDOM 3\n#IF 2\n#TITLE nested12\n#00011:01\n#ENDIF\n#IF 3\n#TITLE nested13\n#00012:02\n#ENDIF\n#ENDRANDOM\n#ENDIF\n#IF 2\n#00013:01\n#ENDIF\n#ENDRANDOM\n#RANDOM 3\n#IF 3\n#00014:02\n#ENDIF\n#ENDRANDOM\n",
    "volwav_limits": "#VOLWAV -2147483648\n#VOLWAV -2147483649\n",
    "gcc_fma_rounding": "#00002:0.3\n#00102:0.3\n#00111:01010101010101010101\n",    "poor_single_image": "#BMP01 one.png\n#00006:01000100\n",
    "poor_multiple_images": "#BMP00 default.png\n#BMP01 one.png\n#BMP02 two.png\n#00006:01000200\n",
    "poor_zero_override": "#BMP00 default.png\n#BMP01 one.png\n#00006:0101\n#00006:0000\n",
    "bga_layers": "#BMP01 one.png\n#00004:0102\n#00007:0201\n",
    "sentinel_position_ln": "#00002:5e-324\n#00151:0101\n",
    "sentinel_position_unclosed": "#00002:5e-324\n#00151:01\n",
    "player_range": "#PLAYER 2\n#PLAYER 3\n#00011:01\n",
    "mine_damage": "#000D1:01ZZ\n",
    "mine_damage_base62": "#BASE 62\n#000D1:aZ\n",
    "rounded_control_positions": "#00002:5e-324\n#00003:0078\n#00003:F000\n#00011:0101\n",
    "rounded_ln_positions": "#00002:5e-324\n#00051:0101\n",
    "rounded_bar_positions": "#00002:1e16\n#00111:01\n#00211:02\n",
    "numeric_base62_lnobj_short": "#TITLE audit\n#BPM 120\n#WAV01 head.wav\n#BASE 62\n#LNOBJ Z\n#00011:01\n",
    "numeric_colon": "#TITLE audit\n#BPM 120\n#WAV01 head.wav\n#00011 :01\n",
    "numeric_position_rounded": "#TITLE audit\n#BPM 120\n#WAV01 head.wav\n#00002:1e16\n#00102:4\n#00111:0101010101010101\n",
    "numeric_position_underflow": "#TITLE audit\n#BPM 120\n#WAV01 head.wav\n#00002:5e-324\n#00011:0101\n",
    "numeric_short_bpm": "#TITLE audit\n#BPM 120\n#WAV01 head.wav\n#BPM\n#00011:01\n",
    "numeric_stop_finite_rounding": "#BPM 120\n#STOP01 73.032\n#00009:01\n#00011:0001\n",
    "numeric_stop_inf": "#TITLE audit\n#BPM 120\n#WAV01 head.wav\n#STOP01 Infinity\n#00009:01\n#00011:0001\n",
    "numeric_stop_nan": "#TITLE audit\n#BPM 120\n#WAV01 head.wav\n#STOP01 NaN\n#00009:01\n#00011:0001\n",
    "numeric_stop_rounding": "#TITLE audit\n#BPM 120\n#WAV01 head.wav\n#STOP01 1.92\n#00009:01\n#00011:0001\n",
    "numeric_unicode_int": "#TITLE audit\n#BPM 120\n#WAV01 head.wav\n#DIFFICULTY \uff14\n#00011:01\n",
    "saturated_cross_measure_lnobj": "#BPM 1e-20\n#LNOBJ ZZ\n#00111:01\n#00211:ZZ\n",
    "collision_backward_channel": "#TITLE audit\n#BPM 120\n#WAV01 head.wav\n#WAV02 tail.wav\n#00051:0001\n#00051:0200\n",
    "collision_detached_open": "#TITLE audit\n#BPM 120\n#WAV01 head.wav\n#WAV02 tail.wav\n#00051:01\n#00011:02\n#00151:01\n",
    "collision_equal_channel": "#TITLE audit\n#BPM 120\n#WAV01 head.wav\n#WAV02 tail.wav\n#00051:01\n#00051:02\n",
    "collision_lnobj_interiors": "#TITLE audit\n#BPM 120\n#WAV01 head.wav\n#WAV02 tail.wav\n#LNOBJ ZZ\n#00011:00010000\n#00011:010000ZZ\n",
    "collision_mixed_type": "#TITLE audit\n#BPM 120\n#WAV01 head.wav\n#WAV02 tail.wav\n#LNMODE 2\n#LNOBJ ZZ\n#00051:01\n#00111:ZZ\n",
    "collision_overwrite_head": "#TITLE audit\n#BPM 120\n#WAV01 head.wav\n#WAV02 tail.wav\n#00051:0101\n#00011:02\n",
    "collision_overwrite_tail": "#TITLE audit\n#BPM 120\n#WAV01 head.wav\n#WAV02 tail.wav\n#00051:0101\n#00011:0002\n",
    "collision_pending_endpoint": "#TITLE audit\n#BPM 120\n#WAV01 head.wav\n#WAV02 tail.wav\n#LNOBJ ZZ\n#00051:0001\n#00011:01ZZ\n",
    "java_double_suffix": "#BPM 240d\n#00011:0001\n",
    "java_double_hex": "#BPM 0x1.ep7\n#00011:0001\n",
    "java_double_underflow": "#SCROLL01 2\n#SCROLL01 1e-999\n#000SC:01\n#00011:0001\n",
    "java_double_nan": "#SCROLL01 NaN\n#000SC:01\n#00011:0001\n",
    "java_double_infinity": "#BPM Infinity\n#00011:0001\n",
    "java_time_saturation": "#BPM 1e-20\n#00011:0001\n",
    "java_lnobj_reset": "#LNOBJ ZZ\n#LNOBJ 00\n#00011:01ZZ\n",
    "java_lnobj_leading_zero": "#LNOBJ 0ZZ\n#00011:01ZZ\n",
    "java_lnobj_sign": "#LNOBJ +Z\n#00011:010Z\n",
    "java_lnobj_base62_suffix": "#BASE 62\n#LNOBJ ZZsuffix\n#00011:01ZZ\n",
    "java_inline_bpm": "#00003:1G\n#00011:0001\n",
    "java_late_inside_normal": "#00051:01000100\n#00011:00010000\n",
    "java_late_head_normal": "#00051:0101\n#00011:02\n",
    "java_difficulty_label": "#TITLE Easy Street [ANOTHER]\n#00011:01\n",
    "java_empty_resource": "#WAV01   \n#00011:01\n",
    "java_header_prefix": "#DIFFICULTYx 4\n#00011:01\n",
    "java_invalid_if": "#RANDOM 1\n#IF 1x\n#00011:01\n#ENDIF\n#ENDRANDOM\n",
    "java_prefix_if": "#RANDOM 1\n#IFx 2\n#00011:01\n#ENDIF\n#ENDRANDOM\n",
    "java_tab_bpm": "#BPM\t240\n#00011:0001\n",
    "java_tab_base": "#BASE\t62\n#WAVaa lower.wav\n#00011:aa\n",
    "base62": "#BASE 62\n#SCROLL01 2\n#000SC:01\n#00011:01\n#00021:01\n#00151:0101\n",
    "late_base62": "#WAVaa lower.wav\n#00001:aa\n#BASE 62\n",
    "undefined_bpm": "#00008:01\n#00011:0001\n#00111:01\n",
    "undefined_scroll": "#SCROLL01 2\n#000SC:0102\n#00111:01\n",
    "undefined_stop": "#STOP01 192\n#00009:01\n#00009:02\n#00111:01\n",
    "negative_bpm": "#BPM01 -120\n#00008:01\n#00111:01\n",
    "negative_scale": "#00002:-1\n#00111:01\n",
    "negative_stop": "#STOP01 -192\n#00009:01\n#00111:01\n",
    "empty_dp": "#00029:0000\n#00111:01\n",
    "ln_release": "#00051:0102\n",
    "orphan_lnobj": "#LNOBJ ZZ\n#00011:ZZ\n",
    "unclosed_ln": "#00051:01\n",
    "mixed_ln": "#LNOBJ ZZ\n#00051:01\n#00111:ZZ\n",
    "inside_ln": "#00011:00010000\n#00051:01000100\n",
    "mine_collision": "#00011:01\n#000D1:02\n",
    "duplicate_note": "#00011:01\n#00011:02\n",
    "charge_count": "#LNMODE 2\n#00051:0102\n",
    "hellcharge_count": "#LNMODE 3\n#00051:0102\n",
    "invalid_cells": "#00011:??01\n",
    "difficulty": "#DIFFICULTY 4\n#DIFFICULTY 2.5\n#00011:01\n",
    "same_position_lnobj": "#LNOBJ ZZ\n#00011:01\n#00011:ZZ\n",
    "backwards_lnobj": "#LNOBJ ZZ\n#00011:0001\n#00011:ZZ00\n",
    "resource_trim": "#WAV01  subdir\\note.wav  \n#00011:01\n",
    "reuse_source": "#LNOBJ ZZ\n#BPM01 240\n#SCROLL01 2\n#STOP01 192\n#00011:0100ZZ00\n",
    "reuse_target": "#00008:01\n#000SC:01\n#00009:01\n#00011:0100ZZ00\n",
}


RAW_CASES = {
    "long_numeric_header": "#BPM " + "1" * 100000 + "\n#00011:01\n",
    "review_lnobjunicode": "#BPM 120\n#WAV01 a.wav\n#LNOBJ \u00df\n#00011:01SS\n",
    "review_originunderflow": "#WAV01 a.wav\n#00002:5e-324\n#00003:0078\n#00011:01\n",
    "review_unicodecells": "#BPM 120\n#00011:\u00e901\n",
    "review_mode27": "#BPM 120\n#00027:01\n",
    "review_mode47": "#BPM 120\n#00047:01\n",
    "review_mode67": "#BPM 120\n#00067:01\n",
    "review_modeE7": "#BPM 120\n#000E7:01\n",
    "empty": "",
    "missing_bpm_metadata": "#TITLE only metadata\n",
    "carriage_returns": "#BPM 120\r#WAV01 one.wav\r#00011:01\r",
}


BINARY_CASES = {
    "bom-" + encoding: bom + "#TITLE BOM\n#BPM 120\n#00011:01\n".encode(encoding)
    for encoding, bom in [("utf-8", b"\xef\xbb\xbf"),
                          ("utf-16-le", b"\xff\xfe"),
                          ("utf-16-be", b"\xfe\xff"),
                          ("utf-32-le", b"\xff\xfe\x00\x00"),
                          ("utf-32-be", b"\x00\x00\xfe\xff")]
}
BINARY_CASES["bom-first-bpm"] = b"\xef\xbb\xbf#BPM 120\n#00011:01\n"

BINARY_CASES["utf8-ambiguous-euc-first"] = "#BPM 120\n#TITLE éé\n#00011:01\n".encode("utf-8")
for name, suffix in {
    "invalid-lead": b"\xff", "overlong": b"\xc0\xaf", "truncated2": b"\xc2",
    "truncated3": b"\xe2\x82", "truncated4": b"\xf0\x90\x80", "surrogate": b"\xed\xa0\x80",
    "bad-third": b"\xe2\x82x", "bad-fourth": b"\xf0\x90\x80x", "out-of-range": b"\xf4\x90\x80\x80",
}.items():
    BINARY_CASES["malformed-utf8-" + name] = b"\xef\xbb\xbf\n#BPM 120\n#TITLE a" + suffix
BINARY_CASES["charset-sampling"] = b"#TITLE \xa1\xa1\n*" + b"x" * 65536 + b"\n*\x83\x65\n#BPM 120\n"
BINARY_CASES["ms932-extensions"] = b"#BPM 120\n#TITLE \xfa\x40\xf0\x40\x81\x60\n#00011:01\n"
BINARY_CASES["euckr-unmapped-lead"] = b"#BPM 120\n#TITLE \xa1\xa1" + b"x" * 65536 + b"\xad\x80\xa2\xe9\n"
for encoding in ["utf-16-le", "utf-16-be", "utf-32-le", "utf-32-be"]:
    BINARY_CASES["nobom-" + encoding] = "#BPM 120\n#TITLE ƀ𝠀\n#00011:01\n".encode(encoding)

# UTF-16 makes the intended characters independent of encoding detection.
for name, text in {
    "unicode-volwav": "#VOLWAV +１２３\n",
    "unicode-short-channel": "#001😀\n#00011:01\n",
    "unicode-header": "#TITLEéabc\n",
    "unicode-header-empty": "#TITLE old\n#TITLEé\n",
    "unicode-header-surrogate": "#TITLE😀abc\n",
    "unicode-if": "#RANDOM 1\n#IFé2\n#00011:01\n#ENDIF\n",
    "unicode-resource": "#WAV01éa.wav\n#00011:01\n",
    "unicode-bpm-short": "#BPM😀\n",
    "unicode-controls": "#BPM01é240\n#STOP01é96\n#SCROLL01é2\n#00008:01\n#00009:01\n#000SC:01\n#00011:0001\n",
}.items():
    BINARY_CASES[name] = b"\xff\xfe" + ("\n#BPM 120\n" + text).encode("utf-16-le")
for name, suffix in {
    "odd": b"a",
    "high-nonlow": b"\x00\xd8a\x00",
    "high-odd": b"\x00\xd8a",
    "low": b"\x00\xdc",
}.items():
    BINARY_CASES["malformed-utf16-" + name] = b"\xff\xfe" + "\n#BPM 120\n#TITLE a".encode("utf-16-le") + suffix


def collision_generated(rng):
    rows = [HEADER, "#LNOBJ ZZ", f"#LNMODE {rng.randrange(4)}"]
    for _ in range(rng.randint(4, 24)):
        measure = rng.randrange(4)
        channel = rng.choice(["11", "51", "D1", "12", "52", "D2"])
        cells = "".join(rng.choice(["00", "00", "01", "02", "ZZ"])
                        for _ in range(rng.choice([1, 2, 4, 8])))
        rows.append(f"#{measure:03}{channel}:{cells}")
    return "\n".join(rows) + "\n"


def generated(rng, index):
    pms = index % 7 == 0
    base62 = rng.choice([True, False])
    ids = ["01", "02", "0A", "Aa", "aa"] if base62 else ["01", "02", "0A", "AZ", "XY"]
    lines = ["#TITLE generated", f"#BPM {rng.choice([120, 137, 180.5, 240])}",
             f"#DIFFICULTY {rng.randrange(6)}", f"#LNMODE {rng.randrange(4)}", "#LNOBJ ZZ"]
    lines += [f"#WAV{obj} note-{obj}.wav" for obj in ids]
    lines += ["#BASE 62" if base62 else "#BASE 36", "#BPM01 90", "#BPM02 173.25",
              "#STOP01 48", "#STOP02 7.5", "#SCROLL01 -0.5", "#SCROLL02 2"]
    measure_count = rng.randint(2, 6)
    rows = [[] for _ in range(measure_count)]
    channels = [11, 12, 13, 14, 15, 22, 23, 24, 25] if pms else \
               rng.choice([[11, 12, 16], [11, 18, 19, 16], [11, 16, 21, 26],
                           [11, 18, 19, 16, 21, 28, 29, 26]])
    for channel in channels:
        cells = ["00"] * (measure_count * 16)
        kind = rng.choice(["normal", "lnobj", "channel_ln"])
        positions = sorted(rng.sample(range(len(cells)), rng.randint(2, min(14, len(cells)))))
        if kind != "normal":
            positions = positions[:len(positions) // 2 * 2]
        for j, position in enumerate(positions):
            cells[position] = "ZZ" if kind == "lnobj" and j % 2 else rng.choice(ids)
        note_channel = channel + 40 if kind == "channel_ln" else channel
        for measure in range(measure_count):
            data = "".join(cells[measure * 16:(measure + 1) * 16])
            rows[measure].append(f"#{measure:03}{note_channel:02}:{data}")
    for measure in range(measure_count):
        rows[measure].append(f"#{measure:03}02:{rng.choice([0.5, 0.75, 1, 1.25, 1.5, 2])}")
        for channel, objects in [("03", ["78", "F0"]), ("08", ["01", "02"]),
                                 ("09", ["01", "02"]), ("SC", ["01", "02"]),
                                 ("01", ids), ("31", ids)]:
            cells = ["00"] * 16
            for pos in rng.sample(range(16), rng.randint(0, 4)):
                cells[pos] = rng.choice(objects)
            rows[measure].append(f"#{measure:03}{channel}:{''.join(cells)}")
        rng.shuffle(rows[measure])
        lines.extend(rows[measure])
    return "\n".join(lines) + "\n", ".pms" if pms else ".bms"


def fields(line):
    return dict(part.split("=", 1) for part in shlex.split(line)[1:])


def parse_output(output):
    charts = {}
    chart = None
    timeline = None
    previous = None
    for line in output.splitlines():
        if line.startswith("FILE "):
            chart = {"notes": [], "hidden": [], "background": [], "controls": [], "bga": [], "poor": [], "timelines": [], "values": {}}
            charts[line[5:]] = chart
            previous = None
        elif line == "NULL":
            chart["null"] = True
        elif line.startswith("META "):
            chart["meta"] = {key: float(value) for key, value in fields(line).items()}
            previous = (chart["meta"]["bpm"], 1)
        elif line.startswith("TEXT "):
            chart["text"] = fields(line)
        elif line.startswith("VALUE "):
            entry = fields(line)
            chart["values"][entry["key"]] = entry["value"]
        elif line.startswith("TL "):
            timeline = {key: int(value) if key in ("time", "stop") else float(value) for key, value in fields(line).items()}
            chart["timelines"].append(timeline)
            state = (timeline["bpm"], timeline["scroll"])
            if state != previous or timeline["stop"] != 0:
                control = dict(timeline)
                chart["controls"].append(control)
            previous = state
        elif line.startswith(("NOTE ", "HIDDEN ", "BG ", "BGA ", "POOR ")):
            event = fields(line)
            for key in ("lane", "pair", "type", "end", "damage", "frame", "pairtype", "pairend", "pairtime", "pairattached"):
                if key in event:
                    event[key] = (int(event[key]) if key == "pairtime" else float(event[key])) if event[key] != "null" else None
            event.update({key: timeline[key] for key in ("pos", "time", "bpm", "scroll")})
            category = "notes" if line.startswith("NOTE ") else \
                       "hidden" if line.startswith("HIDDEN ") else \
                       "bga" if line.startswith("BGA ") else \
                       "poor" if line.startswith("POOR ") else "background"
            chart[category].append(event)
    for chart in charts.values():
        for key in ("notes", "hidden", "background"):
            chart[key].sort(key=lambda item: (item["pos"], item.get("lane", -1),
                                             item.get("kind", ""), item["wav"]))
    return charts


def demote_malformed_long_notes(chart):
    """Apply the explicit parser policy to raw Java output, never fixture names.

    Preserve raw logs. Only unrenderable/unfinishable endpoints change, plus the total
    count delta for formerly uncounted classic tails in Java's time window.
    """
    changed = 0
    active_positions = {note["pos"] for note in chart.get("notes", [])}
    shifts_start = False
    for timeline in chart.get("timelines", []):
        if timeline["time"] >= 1000000:
            break
        if timeline["pos"] in active_positions:
            shifts_start = True
            break
    for note in chart.get("notes", []):
        if note["kind"] != "LongNote":
            continue
        missing = note.get("pair") is None
        unusable_detached = note.get("pairattached") == 0 and (
            note["end"] or note["type"] in (2, 3) or shifts_start or
            note["pair"] <= note["pos"] or note["pairtime"] <= note["time"])
        if not missing and not unusable_detached:
            continue
        millis = abs(note["time"]) // 1000 * (-1 if note["time"] < 0 else 1)
        counted_time = (millis & 0xffffffff) < 0x7fffffff
        if counted_time and note["end"] and note["type"] not in (2, 3):
            chart["meta"]["notes"] += 1
        note["kind"] = "NormalNote"
        for key in ("pair", "type", "end", "pairwav", "pairtype", "pairend",
                    "pairtime", "pairattached"):
            note.pop(key, None)
        changed += 1
    return changed


def differences(actual, expected):
    if actual.get("null") != expected.get("null"):
        return ["parse acceptance differs"]
    if actual.get("null"):
        return []
    errors = []
    for key, value in expected["meta"].items():
        if not equal_number(actual["meta"].get(key), value):
            errors.append(f"metadata {key}: C++={actual['meta'].get(key)} Java={value}")
    for key, value in expected["text"].items():
        if actual["text"][key] != value:
            errors.append(f"text {key}: C++={actual['text'][key]!r} Java={value!r}")
    if actual["values"] != expected["values"]:
        errors.append(f"custom values: C++={actual['values']!r} Java={expected['values']!r}")
    for category in ("notes", "hidden", "background", "controls", "bga", "poor", "timelines"):
        a, b = actual[category], expected[category]
        if len(a) != len(b):
            errors.append(f"{category} count: C++={len(a)} Java={len(b)}")
            continue
        for i, (left, right) in enumerate(zip(a, b)):
            if left.keys() != right.keys():
                errors.append(f"{category}[{i}] fields differ")
                break
            for key, value in right.items():
                if isinstance(value, (float, int)):
                    equal = equal_number(left[key], value)
                else:
                    equal = left[key] == value
                if not equal:
                    errors.append(f"{category}[{i}] {key}: C++={left[key]} Java={value}")
                    break
            if errors and errors[-1].startswith(f"{category}[{i}]"):
                break
    return errors


def equal_number(left, right):
    return left == right or (isinstance(left, float) and isinstance(right, float)
                             and math.isnan(left) and math.isnan(right))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", type=Path, required=True)
    parser.add_argument("--bmson-classpath", type=Path, required=True,
                        help="jar supplying the unused BMSONDecoder class")
    parser.add_argument("--output", type=Path, default=ROOT / "build/jbms-review")
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--cases", type=int, default=200)
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--collision-cases", type=int, default=100)
    parser.add_argument("--random-values", action="append",
                        help="repeat a comma-separated explicit selection sequence; defaults: 1, 2, 3, 1,2,3 (does not compare PRNGs)")
    args = parser.parse_args()
    selections = args.random_values or ["1", "2", "3", "1,2,3"]
    for selection in selections:
        if not selection or any(not value.isdecimal() for value in selection.split(",")):
            parser.error("--random-values requires comma-separated nonnegative integers")
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    fixtures = out / "fixtures"
    fixtures.mkdir(exist_ok=True)
    classes = out / "classes"
    classes.mkdir(exist_ok=True)
    reference = args.reference.expanduser().resolve()
    jar = args.bmson_classpath.expanduser().resolve()
    sources = sorted((reference / "src/bms/model").glob("*.java"))
    sources = [str(path) for path in sources if path.name != "BMSONDecoder.java"]
    subprocess.run(["javac", "-cp", str(jar), "-d", str(classes), *sources,
                    str(ROOT / "test/reference/DumpReference.java")], check=True)
    command = [os.environ.get("CXX", "c++"), "-std=c++17", "-O1", "-g", "-pthread",
               "-DBMS_PARSER_VERBOSE=0", f"-I{ROOT / 'src'}", f"-I{ROOT / 'test'}"]
    if args.sanitize:
        command += ["-fsanitize=address,undefined,float-cast-overflow", "-fno-sanitize-recover=all"]
    binary = out / "dump-cpp"
    subprocess.run(command + [str(ROOT / "test/reference/DumpCpp.cpp"),
                             *map(str, sorted((ROOT / "src").glob("*.cpp"))),
                             "-o", str(binary)], check=True)
    assert not (CASES.keys() & RAW_CASES.keys() or CASES.keys() & BINARY_CASES.keys()
                or RAW_CASES.keys() & BINARY_CASES.keys()), "fixture names must be unique"
    paths = []
    for name, body in CASES.items():
        path = fixtures / (name + ".bms")
        path.write_text(HEADER + body)
        paths.append(path)
    for name, content in RAW_CASES.items():
        path = fixtures / (name + ".bms")
        path.write_text(content)
        paths.append(path)
    for name, content in BINARY_CASES.items():
        path = fixtures / (name + ".bms")
        path.write_bytes(content)
        paths.append(path)
    rng = random.Random(args.seed)
    for index in range(args.collision_cases):
        path = fixtures / (f"collision-generated-{index:04}.bms")
        path.write_text(collision_generated(rng))
        paths.append(path)
    for index in range(args.cases):
        content, extension = generated(rng, index)
        path = fixtures / (f"generated-{index:04}" + extension)
        path.write_text(content)
        paths.append(path)
    paths.extend(sorted((ROOT / "test/testcases/metadata").glob("*.bme")))
    paths.extend(sorted((ROOT / "test/testcases/beat-guess").glob("*.bm*")))
    paths.append(ROOT / "test/testcases/parser/popn.pms")
    results = []
    for selection in selections:
        decoded = {}
        for language, command in [
            ("cpp", [str(binary)]),
            ("java", ["java", "-cp", str(classes) + os.pathsep + str(jar), "DumpReference"]),
        ]:
            env = dict(os.environ, BMS_REFERENCE_RANDOM_VALUES=selection)
            run = subprocess.run(command + list(map(str, paths)), capture_output=True, text=True,
                                 errors="backslashreplace", env=env)
            label = language + "-random-" + selection.replace(",", "_")
            (out / (label + ".txt")).write_text(run.stdout)
            (out / (label + ".stderr.txt")).write_text(run.stderr)
            if run.returncode:
                raise SystemExit(f"{language} dumper failed ({run.returncode}); see {out}")
            decoded[language] = parse_output(run.stdout)
        for path in paths:
            cpp, java = decoded["cpp"].get(path.name), decoded["java"].get(path.name)
            demoted = demote_malformed_long_notes(java) if java is not None else 0
            errors = differences(cpp, java) if cpp is not None and java is not None else ["missing output"]
            results.append({"chart": str(path), "random_values": selection, "demoted_endpoints": demoted, "errors": errors})
    failures = [item for item in results if item.get("errors")]
    report = {"seed": args.seed, "generated_cases": args.cases, "collision_cases": args.collision_cases,
              "reference_revision": subprocess.check_output(["git", "-C", str(reference), "rev-parse", "HEAD"], text=True).strip(),
              "demoted_endpoints": sum(item["demoted_endpoints"] for item in results),
              "random_selections": selections, "sanitized": args.sanitize, "failures": len(failures), "results": results}
    (out / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"Compared {len(paths)} charts x {len(selections)} explicit random selections: {len(failures)} unexpected differences; report: {out / 'results.json'}")
    for result in failures[:12]:
        print(Path(result["chart"]).name, "random=" + result["random_values"], "; ".join(result["errors"]))
    raise SystemExit(bool(failures))


if __name__ == "__main__":
    main()
