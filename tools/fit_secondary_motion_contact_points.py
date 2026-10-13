"""複数boneに結び付ける接触球を、開発用bind座標記録から作る。"""
from __future__ import annotations

import argparse
from collections import defaultdict
import json
import math
from pathlib import Path

from fit_secondary_motion_contact_links import _finite_number, _validate_point_record


def fit_contact_points(records: list[dict], max_spread: float, minimum_radius: float, minimum_moving_weight: float = 0.99, maximum_bone_scale: float = 1.0, selected_ids: set[int] | None = None) -> tuple[list[dict], dict]:
    """同じboneと実weightの点だけをまとめ、各boneの偏差の重み付き上界を半径にする。"""
    max_spread = _finite_number(max_spread, "max_spread")
    minimum_radius = _finite_number(minimum_radius, "minimum_radius")
    minimum_moving_weight = _finite_number(minimum_moving_weight, "minimum_moving_weight")
    maximum_bone_scale = _finite_number(maximum_bone_scale, "maximum_bone_scale")
    if max_spread <= 0 or minimum_radius < 0 or not 0 <= minimum_moving_weight <= 1 or maximum_bone_scale <= 0:
        raise ValueError("invalid fit limits")
    if not isinstance(records, list):
        raise ValueError("binding records must be an array")
    groups = defaultdict(list)
    seen_ids = set()
    excluded = []
    names = {}
    for record in records:
        if not isinstance(record, dict):
            raise ValueError("each binding record must be an object")
        if record.get("type") != "gkcore_skin_binding_point":
            continue
        bindings = _validate_point_record(record)
        identifier = record["positionId"]
        if identifier in seen_ids:
            raise ValueError("duplicate positionId")
        seen_ids.add(identifier)
        total = sum(b["weight"] for b in bindings)
        if abs(total - 1.0) > 1e-6:
            raise ValueError("binding weights must sum to one")
        moving = sum(b["weight"] for b in bindings if b["group"] == "skirtChild")
        if moving < minimum_moving_weight or selected_ids is not None and identifier not in selected_ids:
            excluded.append(identifier)
            continue
        # 同じboneの複数clusterは、実weightを足して局所位置の重心を求める。
        merged = {}
        for b in bindings:
            if b["weight"] == 0:
                continue
            bone = b["bone"]
            if bone in names and names[bone] != b["name"]:
                raise ValueError("one bone has inconsistent names")
            names[bone] = b["name"]
            item = merged.setdefault(bone, {"weight": 0.0, "sum": [0.0] * 3})
            item["weight"] += b["weight"]
            for axis in range(3):
                item["sum"][axis] += b["localPosition"][axis] * b["weight"]
        if not 1 <= len(merged) <= 64:
            raise ValueError("one contact requires one to 64 positive-weight anchors")
        signature = tuple((bone, merged[bone]["weight"]) for bone in sorted(merged))
        offsets = [[value / merged[bone]["weight"] for value in merged[bone]["sum"]] for bone, _ in signature]
        if any(not math.isfinite(value) for row in offsets for value in row):
            raise ValueError("merged binding offset overflowed")
        groups[signature].append({"positionId": identifier, "offsets": offsets})
    points = []
    def divide(signature, samples):
        centres = [[sum(row["offsets"][i][axis] for row in samples) / len(samples) for axis in range(3)] for i in range(len(signature))]
        spreads = [max(math.dist(row["offsets"][i], centres[i]) for row in samples) for i in range(len(signature))]
        bound = sum(weight * spread for (_, weight), spread in zip(signature, spreads)) * maximum_bone_scale
        if not math.isfinite(bound):
            raise ValueError("contact spread overflowed")
        if bound > max_spread and len(samples) > 1:
            i, axis = max(((i, axis) for i in range(len(signature)) for axis in range(3)), key=lambda item: signature[item[0]][1] * (max(row["offsets"][item[0]][item[1]] for row in samples) - min(row["offsets"][item[0]][item[1]] for row in samples)))
            ordered = sorted(samples, key=lambda row: (row["offsets"][i][axis], row["positionId"]))
            middle = len(ordered) // 2
            divide(signature, ordered[:middle]); divide(signature, ordered[middle:])
            return
        points.append({"radius": max(minimum_radius, bound), "observedWeightedBound": bound, "anchors": [{"bone": bone, "name": names[bone], "weight": weight, "offset": centre, "observedSpread": spread} for (bone, weight), centre, spread in zip(signature, centres, spreads)], "positionIds": [row["positionId"] for row in samples]})
    for signature, samples in sorted(groups.items()):
        divide(signature, sorted(samples, key=lambda row: row["positionId"]))
    return points, {"selectedPoints": sum(len(rows) for rows in groups.values()), "excludedPoints": len(excluded), "excludedPositionIds": sorted(excluded), "contactPoints": len(points), "anchorCount": sum(len(row["anchors"]) for row in points), "maximumBoneScale": maximum_bone_scale, "coverageLimit": "equal bone/weight signatures only; the radius bound requires each bone linear transform to stretch by no more than maximumBoneScale; triangle interiors and unselected samples are not covered"}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bindings", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--max-spread", type=float, default=0.012)
    parser.add_argument("--minimum-radius", type=float, default=0.002)
    parser.add_argument("--minimum-moving-weight", type=float, default=0.99)
    parser.add_argument("--maximum-bone-scale", type=float, default=1.00002)
    parser.add_argument("--point-ids", type=Path)
    args = parser.parse_args()
    try:
        ids = set(json.loads(args.point_ids.read_text(encoding="utf-8-sig"))) if args.point_ids else None
        points, report = fit_contact_points(json.loads(args.bindings.read_text(encoding="utf-8-sig")), args.max_spread, args.minimum_radius, args.minimum_moving_weight, args.maximum_bone_scale, ids)
        if not points or len(points) > 1024 or report["anchorCount"] > 16384:
            raise ValueError("contact point count is empty or exceeds API limits")
    except (ValueError, OSError, TypeError) as error:
        parser.error(str(error))
    lines = ["# 半径 | bone名 weight localX localY localZ | ...", "# 複数boneの実weightで動く接触球。頂点位置は書き換えない。"]
    for contact in points:
        anchors = [f"{a['name']} {a['weight']:.9g} " + " ".join(f"{v:.9g}" for v in a["offset"]) for a in contact["anchors"]]
        lines.append(f"{contact['radius']:.9g} | " + " | ".join(anchors))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(b"\xef\xbb\xbf" + ("\r\n".join(lines) + "\r\n").encode("utf-8"))
    args.output.with_suffix(".fit.json").write_text(json.dumps({"report": report, "points": points}, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({k: v for k, v in report.items() if k != "excludedPositionIds"}))


if __name__ == "__main__":
    main()
