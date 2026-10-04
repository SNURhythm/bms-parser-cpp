import importlib.util
from pathlib import Path
import unittest


path = Path(__file__).resolve().parents[2] / "scripts/check_jbms_reference.py"
spec = importlib.util.spec_from_file_location("check_jbms_reference", path)
comparison = importlib.util.module_from_spec(spec)
spec.loader.exec_module(comparison)


def chart(pair_time=0, stop=0):
    return comparison.parse_output(
        'FILE fixture.bms\nMETA bpm=120\nTEXT title="audit"\n'
        f'TL pos=0 time=0 bpm=120 scroll=1 stop={stop}\n'
        'NOTE lane=0 kind=LongNote wav="a.wav" pair=1 type=1 end=0 '
        f'pairtime={pair_time}\n'
    )["fixture.bms"]


class IntegerMicroseconds(unittest.TestCase):
    def test_detached_partner_one_microsecond_difference_above_double_precision(self):
        self.assertTrue(comparison.differences(
            chart(pair_time=9007199254740992),
            chart(pair_time=9007199254740993)))

    def test_stop_one_microsecond_difference_above_double_precision(self):
        self.assertTrue(comparison.differences(
            chart(stop=9007199254740992),
            chart(stop=9007199254740993)))


class MalformedLongNotePolicy(unittest.TestCase):
    def fixture(self, pair, attached, end=0, kind=1, time=2000000):
        return comparison.parse_output(
            'FILE fixture.bms\nMETA bpm=120 notes=1\nTEXT title="audit"\n'
            f'TL pos=0 time={time} bpm=120 scroll=1 stop=0\n'
            f'NOTE lane=0 kind=LongNote wav="a.wav" pair={pair} type={kind} end={end}'
            + (f' pairattached={attached} pairtime={time + 1000000}' if pair != "null" else '')
            + '\n')["fixture.bms"]

    def test_valid_pair_unchanged(self):
        data = self.fixture(1, 1)
        self.assertEqual(comparison.demote_malformed_long_notes(data), 0)
        self.assertEqual(data["notes"][0]["kind"], "LongNote")

    def test_missing_and_detached_endpoints(self):
        for pair, attached in [("null", None), (1, 0)]:
            for end in (0, 1):
                for kind in (0, 1, 2, 3):
                    data = self.fixture(pair, attached, end, kind)
                    demotes = pair == "null" or end or kind in (2, 3)
                    self.assertEqual(comparison.demote_malformed_long_notes(data), int(bool(demotes)))
                    if not demotes:
                        self.assertEqual(data["notes"][0]["kind"], "LongNote")
                        continue
                    note = data["notes"][0]
                    self.assertEqual(note["kind"], "NormalNote")
                    self.assertEqual(note["wav"], "a.wav")
                    self.assertNotIn("pair", note)
                    self.assertEqual(data["meta"]["notes"], 1 + (end and kind < 2))
                    altered = self.fixture(pair, attached, end, kind)
                    comparison.demote_malformed_long_notes(altered)
                    altered["notes"][0]["time"] += 1
                    self.assertTrue(comparison.differences(altered, data))

    def test_startup_shift_breaks_retained_classic_tail(self):
        data = self.fixture(1, 0, time=0)
        self.assertEqual(comparison.demote_malformed_long_notes(data), 1)
        self.assertEqual(data["notes"][0]["kind"], "NormalNote")

    def test_count_window_wrap_and_negative(self):
        for time in (-1000, 2147483647000, 4294967296000):
            data = self.fixture(1, 0, end=1, time=time)
            comparison.demote_malformed_long_notes(data)
            self.assertEqual(data["meta"]["notes"], 2 if time == 4294967296000 else 1)


if __name__ == "__main__":
    unittest.main()
