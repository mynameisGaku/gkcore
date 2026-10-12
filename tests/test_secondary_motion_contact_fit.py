"""衣服接触リンク生成ツールの開発入力と包絡を検査する。"""
from __future__ import annotations

import importlib.util
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
TOOL_PATH = ROOT / "tools" / "fit_secondary_motion_contact_links.py"
SPEC = importlib.util.spec_from_file_location("fit_secondary_motion_contact_links", TOOL_PATH)
assert SPEC is not None and SPEC.loader is not None
FIT_TOOL = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(FIT_TOOL)


def binding(bone: int, name: str, weight: float, position: list[float], group: str = "skirtChild") -> dict:
    return {"bone": bone, "name": name, "weight": weight, "localPosition": position, "group": group}


def point(position_id: int, bindings: list[dict]) -> dict:
    return {"type": "gkcore_skin_binding_point", "positionId": position_id, "bindings": bindings}


class SecondaryMotionContactFitTests(unittest.TestCase):
    def test_partition_envelopes_every_sample_and_is_order_deterministic(self) -> None:
        records = [
            point(10, [binding(1, "SkirtA.001", 0.5, [0.0, 0.0, 0.0]), binding(2, "SkirtB.001", 0.5, [1.0, 0.0, 0.0])]),
            point(11, [binding(1, "SkirtA.001", 0.5, [0.01, 0.0, 0.0]), binding(2, "SkirtB.001", 0.5, [1.0, 0.01, 0.0])]),
            point(12, [binding(1, "SkirtA.001", 0.5, [0.10, 0.0, 0.0]), binding(2, "SkirtB.001", 0.5, [1.10, 0.0, 0.0])]),
            point(13, [binding(1, "SkirtA.001", 0.5, [0.11, 0.0, 0.0]), binding(2, "SkirtB.001", 0.5, [1.10, 0.01, 0.0])]),
            point(14, [binding(1, "SkirtA.001", 0.6, [9.0, 0.0, 0.0]), binding(3, "Leg.001", 0.4, [9.0, 1.0, 0.0], "bodyShapeAncestor")]),
            point(15, [binding(3, "Leg.001", 1.0, [10.0, 0.0, 0.0], "bodyShapeAncestor")]),
        ]

        links, report = FIT_TOOL.fit_links(records, max_spread=0.02, minimum_radius=0.001)
        explicit_capsules, explicit_report = FIT_TOOL.fit_links(records, max_spread=0.02, minimum_radius=0.001, shape="capsule")
        reversed_links, reversed_report = FIT_TOOL.fit_links(list(reversed(records)), max_spread=0.02, minimum_radius=0.001)

        self.assertEqual(links, explicit_capsules)
        self.assertEqual(report, explicit_report)
        self.assertEqual(links, reversed_links)
        self.assertEqual(report, reversed_report)
        self.assertEqual(report["selectedPoints"], 4)
        self.assertEqual(report["excludedFixedOrMixedPoints"], 2)
        sample_by_id = {record["positionId"]: record for record in records if record["type"] == "gkcore_skin_binding_point"}
        for link in links:
            self.assertLessEqual(link["observedSpread"], 0.02)
            self.assertGreaterEqual(link["radius"], link["observedSpread"])
            for position_id in link["positionIds"]:
                row = sample_by_id[position_id]
                endpoints = {item["name"]: item["localPosition"] for item in row["bindings"]}
                for endpoint_key, bone_name in (("firstOffset", link["bones"][0]), ("secondOffset", link["bones"][1])):
                    self.assertLessEqual(math.dist(link[endpoint_key], endpoints[bone_name]), link["radius"] + 1.0e-12)

    def test_same_bone_single_endpoint_does_not_count_weight_twice(self) -> None:
        records = [point(3, [binding(7, "SkirtOnly.001", 1.0, [0.25, -0.5, 0.75])])]

        links, report = FIT_TOOL.fit_links(records, max_spread=0.01, minimum_radius=0.002)

        self.assertEqual(report["selectedPoints"], 1)
        self.assertEqual(report["excludedFixedOrMixedPoints"], 0)
        self.assertEqual(len(links), 1)
        self.assertEqual(links[0]["bones"], ("SkirtOnly.001", "SkirtOnly.001"))
        self.assertEqual(links[0]["maximumExtraWeight"], 0.0)
        self.assertEqual(links[0]["firstOffset"], [0.25, -0.5, 0.75])
        self.assertEqual(links[0]["secondOffset"], [0.25, -0.5, 0.75])

    def test_sphere_mode_uses_primary_weight_without_faking_it(self) -> None:
        records = [
            point(20, [binding(1, "SkirtA.001", 0.6, [0.0, 0.0, 0.0]), binding(2, "SkirtB.001", 0.4, [1.0, 0.0, 0.0])]),
            point(21, [binding(1, "SkirtA.001", 0.55, [0.02, 0.0, 0.0]), binding(2, "SkirtB.001", 0.45, [1.0, 0.0, 0.0])]),
            point(22, [binding(3, "SkirtC.001", 0.6, [4.0, 0.0, 0.0]), binding(4, "Leg.001", 0.4, [4.0, 1.0, 0.0], "bodyShapeAncestor")]),
            point(23, [binding(5, "Leg.001", 1.0, [5.0, 0.0, 0.0], "bodyShapeAncestor")]),
            point(24, [binding(6, "SkirtD.001", 0.495, [6.0, 0.0, 0.0]), binding(7, "SkirtE.001", 0.495, [7.0, 0.0, 0.0]), binding(8, "Leg.001", 0.01, [8.0, 0.0, 0.0], "bodyShapeAncestor")]),
        ]

        links, report = FIT_TOOL.fit_links(records, max_spread=0.05, minimum_radius=0.001, shape="sphere")

        self.assertEqual(report["shape"], "sphere")
        self.assertEqual(report["selectedPoints"], 2)
        self.assertEqual(report["excludedFixedOrMixedPoints"], 3)
        self.assertEqual(report["coverageLimit"], "a primary-bone sphere does not guarantee coverage of remaining bone influences or pose changes; validate rendered skin independently")
        self.assertEqual(len(links), 1)
        link = links[0]
        self.assertEqual(link["bones"], ("SkirtA.001", "SkirtA.001"))
        self.assertEqual(link["firstOffset"], link["secondOffset"])
        self.assertEqual(link["minimumEndpointWeight"], 0.55)
        self.assertEqual(link["maximumEndpointWeight"], 0.6)
        self.assertAlmostEqual(link["maximumExtraWeight"], 0.45)
        self.assertGreaterEqual(link["radius"], link["observedSpread"])
        by_id = {record["positionId"]: record["bindings"] for record in records}
        for position_id in link["positionIds"]:
            primary = next(item for item in by_id[position_id] if item["bone"] == 1)
            self.assertLessEqual(math.dist(link["firstOffset"], primary["localPosition"]), link["radius"] + 1.0e-12)

    def test_sphere_mode_is_order_deterministic_for_tied_weights_and_duplicate_bone_entries(self) -> None:
        records = [
            point(30, [binding(8, "SkirtB.001", 0.5, [0.0, 0.0, 0.0]), binding(7, "SkirtA.001", 0.25, [1.0, 0.0, 0.0]), binding(7, "SkirtA.001", 0.25, [1.2, 0.0, 0.0])]),
            point(31, [binding(8, "SkirtB.001", 0.5, [0.1, 0.0, 0.0]), binding(7, "SkirtA.001", 0.25, [1.1, 0.0, 0.0]), binding(7, "SkirtA.001", 0.25, [1.3, 0.0, 0.0])]),
        ]

        links, report = FIT_TOOL.fit_links(records, max_spread=1.0, minimum_radius=0.001, shape="sphere")
        reversed_links, reversed_report = FIT_TOOL.fit_links(list(reversed(records)), max_spread=1.0, minimum_radius=0.001, shape="sphere")

        self.assertEqual(links, reversed_links)
        self.assertEqual(report, reversed_report)
        self.assertEqual(links[0]["bones"], ("SkirtA.001", "SkirtA.001"))
        self.assertAlmostEqual(links[0]["minimumEndpointWeight"], 0.5)
        self.assertAlmostEqual(links[0]["maximumExtraWeight"], 0.5)
        for record in records:
            positions = [item["localPosition"][0] for item in record["bindings"] if item["bone"] == 7]
            expected_centroid = sum(position * 0.25 for position in positions) / 0.5
            self.assertLessEqual(abs(links[0]["firstOffset"][0] - expected_centroid), links[0]["radius"] + 1.0e-12)

    def test_cli_accepts_sphere_shape_option(self) -> None:
        records = [point(3, [binding(7, "SkirtOnly.001", 1.0, [0.25, -0.5, 0.75])])]
        with tempfile.TemporaryDirectory() as temporary:
            input_path = Path(temporary) / "bindings.json"
            output_path = Path(temporary) / "links.txt"
            input_path.write_text(json.dumps(records), encoding="utf-8")

            result = subprocess.run([sys.executable, str(TOOL_PATH), str(input_path), str(output_path), "--shape", "sphere"], capture_output=True, text=True, check=False)

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('"shape": "sphere"', output_path.with_suffix(".fit.json").read_text(encoding="utf-8"))

    def test_rejects_invalid_record_shapes_and_values(self) -> None:
        valid = point(1, [binding(1, "SkirtA.001", 1.0, [0.0, 0.0, 0.0])])
        invalid_records = [
            {"type": "gkcore_skin_binding_point", "positionId": -1, "bindings": valid["bindings"]},
            {"type": "gkcore_skin_binding_point", "positionId": 1, "bindings": {}},
            point(1, [binding(1, "SkirtA.001", 1.0, [0.0, 1.0])]),
            point(1, [binding(1, "SkirtA.001", math.nan, [0.0, 0.0, 0.0])]),
            point(1, [binding(1, "SkirtA.001", -0.1, [0.0, 0.0, 0.0])]),
            point(1, [binding(1, "SkirtA.001", 1.1, [0.0, 0.0, 0.0])]),
            point(1, [binding(1, "SkirtA.001", 1.0, [0.0, math.inf, 0.0])]),
            point(1, [{"bone": 1, "weight": 1.0, "localPosition": [0.0, 0.0, 0.0], "group": "skirtChild"}]),
        ]
        for record in invalid_records:
            with self.subTest(record=record):
                with self.assertRaises((TypeError, ValueError, KeyError)):
                    FIT_TOOL.fit_links([record], max_spread=0.01, minimum_radius=0.0)

        for records in ({"not": "a list"}, [None], ["bad row"]):
            with self.subTest(records=records):
                with self.assertRaises((TypeError, ValueError)):
                    FIT_TOOL.fit_links(records, max_spread=0.01, minimum_radius=0.0)

    def test_rejects_invalid_fit_parameters(self) -> None:
        records = [point(1, [binding(1, "SkirtA.001", 1.0, [0.0, 0.0, 0.0])])]
        for max_spread, minimum_radius in ((0.0, 0.0), (-1.0, 0.0), (math.inf, 0.0), (0.01, -0.1), (0.01, math.nan)):
            with self.subTest(max_spread=max_spread, minimum_radius=minimum_radius):
                with self.assertRaises(ValueError):
                    FIT_TOOL.fit_links(records, max_spread=max_spread, minimum_radius=minimum_radius)
        for shape in ("box", "", None):
            with self.subTest(shape=shape):
                with self.assertRaises(ValueError):
                    FIT_TOOL.fit_links(records, max_spread=0.01, minimum_radius=0.0, shape=shape)

    def test_cli_rejects_more_than_api_link_limit(self) -> None:
        records = []
        for index in range(1025):
            records.append(point(index, [binding(index * 2, f"SkirtA{index:04d}.001", 0.5, [0.0, 0.0, 0.0]), binding(index * 2 + 1, f"SkirtB{index:04d}.001", 0.5, [1.0, 0.0, 0.0])]))
        with tempfile.TemporaryDirectory() as temporary:
            input_path = Path(temporary) / "bindings.json"
            output_path = Path(temporary) / "links.txt"
            input_path.write_text(json.dumps(records), encoding="utf-8")
            result = subprocess.run([sys.executable, str(TOOL_PATH), str(input_path), str(output_path)], capture_output=True, text=True, check=False)
            self.assertEqual(result.returncode, 2)
            self.assertIn("fitted links exceed the API limit", result.stderr)
            self.assertFalse(output_path.exists())


if __name__ == "__main__":
    unittest.main()
