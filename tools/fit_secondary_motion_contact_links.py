"""骨に付ける衣服接触形状を、開発用のbind座標記録から作る。"""
from __future__ import annotations

import argparse
from collections import defaultdict
import json
import math
from pathlib import Path
import re


def _finite_number(value: object, field: str) -> float:
    """有限な数値fieldを読み取り、boolや非数値を拒否する。"""
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(f"{field} must be a finite number")
    try:
        number = float(value)
    except (OverflowError, ValueError):
        raise ValueError(f"{field} must be a finite number") from None
    if not math.isfinite(number):
        raise ValueError(f"{field} must be a finite number")
    return number


def _validate_point_record(record: dict) -> list[dict]:
    """skin binding pointのID、binding、座標、weightを検証する。"""
    position_id = record.get("positionId")
    if isinstance(position_id, bool) or not isinstance(position_id, int) or position_id < 0 or position_id > 0xFFFFFFFF:
        raise ValueError("positionId must be a uint32 value")
    bindings = record.get("bindings")
    if not isinstance(bindings, list):
        raise ValueError(f"positionId {position_id}: bindings must be an array")
    total_weight = 0.0
    for index, binding in enumerate(bindings):
        prefix = f"positionId {position_id} binding {index}"
        if not isinstance(binding, dict):
            raise ValueError(f"{prefix} must be an object")
        bone = binding.get("bone")
        if isinstance(bone, bool) or not isinstance(bone, int) or bone < 0 or bone > 0xFFFFFFFF:
            raise ValueError(f"{prefix}: bone must be a uint32 value")
        if not isinstance(binding.get("name"), str) or not binding["name"]:
            raise ValueError(f"{prefix}: name must be a nonempty string")
        if not isinstance(binding.get("group"), str):
            raise ValueError(f"{prefix}: group must be a string")
        weight = _finite_number(binding.get("weight"), f"{prefix} weight")
        if weight < 0.0 or weight > 1.0:
            raise ValueError(f"{prefix}: weight must be between zero and one")
        local_position = binding.get("localPosition")
        if not isinstance(local_position, list) or len(local_position) != 3:
            raise ValueError(f"{prefix}: localPosition must contain three coordinates")
        for axis, coordinate in enumerate(local_position):
            _finite_number(coordinate, f"{prefix} localPosition[{axis}]")
        total_weight += weight
        if not math.isfinite(total_weight) or total_weight > 1.000001:
            raise ValueError(f"positionId {position_id}: binding weights exceed one")
    return bindings


def fit_links(records: list[dict], max_spread: float, minimum_radius: float, shape: str = "capsule") -> tuple[list[dict], dict]:
    """同じ骨対または主weight骨の座標を小領域へ分割し、観測範囲を包む。"""
    max_spread = _finite_number(max_spread, "max_spread")
    minimum_radius = _finite_number(minimum_radius, "minimum_radius")
    if max_spread <= 0.0 or minimum_radius < 0.0:
        raise ValueError("max_spread must be positive and minimum_radius must be nonnegative")
    if not isinstance(shape, str) or shape not in ("capsule", "sphere"):
        raise ValueError("shape must be capsule or sphere")
    if not isinstance(records, list):
        raise ValueError("binding records must be an array")
    groups: dict[tuple[str, str], list[dict]] = defaultdict(list)
    excluded = 0
    for record in records:
        if not isinstance(record, dict):
            raise ValueError("each binding record must be an object")
        if record.get("type") != "gkcore_skin_binding_point":
            continue
        bindings = _validate_point_record(record)
        candidates = [binding for binding in bindings if binding["group"] == "skirtChild" and re.search(r"\.00[123]$", binding["name"])]
        if sum(binding["weight"] for binding in candidates) < 0.99:
            excluded += 1
            continue
        if shape == "sphere":
            # 同じ骨に複数clusterがある場合はweight付き重心へまとめてから主骨を選ぶ。
            by_bone: dict[int, dict] = {}
            for binding in candidates:
                bone = binding["bone"]
                entry = by_bone.get(bone)
                if entry is None:
                    entry = {"name": binding["name"], "weight": 0.0, "positionSum": [0.0, 0.0, 0.0]}
                    by_bone[bone] = entry
                elif entry["name"] != binding["name"]:
                    raise ValueError(f"positionId {record['positionId']}: one bone has inconsistent names")
                weight = binding["weight"]
                entry["weight"] += weight
                for axis in range(3):
                    entry["positionSum"][axis] += binding["localPosition"][axis] * weight
            primary = min(by_bone.items(), key=lambda item: (-item[1]["weight"], item[0], item[1]["name"]))[1]
            if primary["weight"] < 0.5:
                excluded += 1
                continue
            endpoint_weight = primary["weight"]
            position = [value / endpoint_weight for value in primary["positionSum"]]
            coordinates = position + position
            bones = (primary["name"], primary["name"])
        else:
            dominant: dict[str, dict] = {}
            for binding in candidates:
                family = binding["name"].split(".")[0]
                if family not in dominant or binding["weight"] > dominant[family]["weight"]:
                    dominant[family] = binding
            endpoints = sorted(dominant.values(), key=lambda binding: (-binding["weight"], binding["bone"], binding["name"]))[:2]
            if not endpoints:
                excluded += 1
                continue
            if len(endpoints) == 1:
                endpoints *= 2
            endpoints.sort(key=lambda binding: (binding["bone"], binding["name"]))
            endpoint_weight = endpoints[0]["weight"] if endpoints[0]["bone"] == endpoints[1]["bone"] else endpoints[0]["weight"] + endpoints[1]["weight"]
            coordinates = endpoints[0]["localPosition"] + endpoints[1]["localPosition"]
            bones = (endpoints[0]["name"], endpoints[1]["name"])
        groups[bones].append({"positionId": record["positionId"], "coordinates": coordinates, "endpointWeight": endpoint_weight, "extraWeight": 1.0 - endpoint_weight})

    links: list[dict] = []
    def divide(bones: tuple[str, str], samples: list[dict]) -> None:
        # 元データの端点偏差を半径で包み、過大な一つの球にはまとめない。
        centre = [sum(sample["coordinates"][axis] for sample in samples) / len(samples) for axis in range(6)]
        spread = max(math.dist(sample["coordinates"][endpoint * 3:endpoint * 3 + 3], centre[endpoint * 3:endpoint * 3 + 3]) for sample in samples for endpoint in range(2))
        if spread > max_spread and len(samples) > 1:
            axis = max(range(6), key=lambda value: max(sample["coordinates"][value] for sample in samples) - min(sample["coordinates"][value] for sample in samples))
            ordered = sorted(samples, key=lambda sample: (sample["coordinates"][axis], sample["positionId"]))
            middle = len(ordered) // 2
            divide(bones, ordered[:middle])
            divide(bones, ordered[middle:])
            return
        link = {"bones": bones, "firstOffset": centre[:3], "secondOffset": centre[3:], "radius": max(minimum_radius, spread), "observedSpread": spread, "positionIds": [sample["positionId"] for sample in samples], "maximumExtraWeight": max(sample["extraWeight"] for sample in samples)}
        if shape == "sphere":
            link["minimumEndpointWeight"] = min(sample["endpointWeight"] for sample in samples)
            link["maximumEndpointWeight"] = max(sample["endpointWeight"] for sample in samples)
        links.append(link)
    for bones, samples in sorted(groups.items()):
        samples.sort(key=lambda sample: (sample["positionId"], tuple(sample["coordinates"]), sample["endpointWeight"]))
        divide(bones, samples)
    coverage_limit = "additional bone influences and pose changes require independent rendered-skin validation"
    if shape == "sphere":
        coverage_limit = "a primary-bone sphere does not guarantee coverage of remaining bone influences or pose changes; validate rendered skin independently"
    return links, {"shape": shape, "selectedPoints": sum(len(values) for values in groups.values()), "excludedFixedOrMixedPoints": excluded, "groups": len(groups), "links": len(links), "radiusMeaning": "observed local binding spread, assuming unit bone scale", "coverageLimit": coverage_limit}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bindings", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--max-spread", type=float, default=0.012)
    parser.add_argument("--minimum-radius", type=float, default=0.002)
    parser.add_argument("--shape", choices=("capsule", "sphere"), default="capsule", help="capsule endpoint pair or one sphere on the primary-weight bone")
    args = parser.parse_args()
    if not math.isfinite(args.max_spread) or args.max_spread <= 0 or not math.isfinite(args.minimum_radius) or args.minimum_radius < 0:
        parser.error("spread must be positive and radius must be nonnegative")
    links, report = fit_links(json.loads(args.bindings.read_text(encoding="utf-8-sig")), args.max_spread, args.minimum_radius, args.shape)
    if len(links) > 1024:
        parser.error("fitted links exceed the API limit")
    lines = ["# 半径 firstX firstY firstZ secondX secondY secondZ | 2個のbone名", "# 骨ローカル接触点の広がりから作る衣服側の補助形状。頂点を実行中に補正しない。"]
    for link in links:
        values = [link["radius"], *link["firstOffset"], *link["secondOffset"]]
        lines.append(" ".join(format(value, ".9g") for value in values) + " | " + " ".join(link["bones"]))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(b"\xef\xbb\xbf" + ("\r\n".join(lines) + "\r\n").encode("utf-8"))
    args.output.with_suffix(".fit.json").write_text(json.dumps({"report": report, "links": links}, indent=2), encoding="utf-8")
    print(json.dumps(report))


if __name__ == "__main__":
    main()
