#!/usr/bin/env python3
"""Prevent guessed HBT pitches and boundary centers in input adaptation."""
import importlib.util
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from h3d_aes_experiment import center_site_counts

spec = importlib.util.spec_from_file_location("aes_converter", ROOT / "benchmarks/h3d_aes_convert.py")
converter = importlib.util.module_from_spec(spec)
spec.loader.exec_module(converter)

HEADER = "UNITS DISTANCE MICRONS 2000 ;\nDIEAREA ( 0 0 ) ( 250000 250000 ) ;"
PLACER = "DieSize 0 0 12500 12500\nTerminalSize 100 100\nTerminalSpacing 100\n"


class ProvenanceChecks(unittest.TestCase):
    def test_no_implicit_pitch(self):
        with self.assertRaisesRegex(ValueError, "no implicit 3 um"):
            converter.hbt_grid_spec(HEADER)

    def test_actual_placer_spec(self):
        result = converter.hbt_grid_spec(HEADER, PLACER)
        self.assertEqual(result["pitch_dbu"], 4000)
        self.assertEqual(result["origin_dbu"], [3000, 3000])
        self.assertEqual(result["provenance"]["placer_terminal_model"]["terminal_size_dbu"], [2000, 2000])

    def test_explicit_override_is_labeled(self):
        result = converter.hbt_grid_spec(HEADER, PLACER, "3.0")
        self.assertEqual(result["pitch_dbu"], 6000)
        self.assertEqual(result["provenance"]["kind"], "explicit_research_pitch_override")
        self.assertEqual(result["provenance"]["placer_terminal_model"]["minimum_center_spacing_dbu"], 4000)

    def test_units_bounds_and_spacing(self):
        with self.assertRaises(ValueError):
            converter.hbt_grid_spec(HEADER.replace("2000", "1000"), PLACER)
        with self.assertRaises(ValueError):
            converter.hbt_grid_spec(HEADER, PLACER.replace("12500", "13000"))
        with self.assertRaises(ValueError):
            converter.hbt_grid_spec(HEADER, PLACER, "1.5")

    def test_row_core_can_be_smaller_than_def_outline(self):
        result = converter.hbt_grid_spec(HEADER, PLACER.replace("12500 12500", "12483 12460"))
        self.assertEqual(result["diearea_dbu"], [0, 0, 250000, 250000])
        self.assertEqual(result["provenance"]["placer_terminal_model"]["placement_core_bounds_dbu"], [0, 0, 249660, 249200])

    def test_interior_centers(self):
        self.assertEqual(center_site_counts([0, 0, 250000, 250000], [3000, 3000], [6000, 6000]), [42, 42])
        self.assertEqual(center_site_counts([0, 0, 250000, 250000], [2000, 2000], [4000, 4000]), [62, 62])
        self.assertEqual(center_site_counts([0, 0, 250000, 250000], [2000, 2000], [4000, 4000], [2000, 2000]), [62, 62])


if __name__ == "__main__":
    unittest.main()
