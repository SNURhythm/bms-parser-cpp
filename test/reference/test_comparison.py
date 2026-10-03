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


if __name__ == "__main__":
    unittest.main()
