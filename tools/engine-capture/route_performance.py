"""Compare native presentation timings for fixed JK/MotS campaigns and traversal routes."""
import argparse
import csv
import hashlib
import io
import json
import math
from pathlib import Path
import re
import statistics


class ProfileError(ValueError):
    pass


def require(condition, message):
    if not condition:
        raise ProfileError(message)


def unique(pairs):
    out = {}
    for key, value in pairs:
        require(key not in out, f"Duplicate JSON key: {key}")
        out[key] = value
    return out


def decode(data):
    return json.loads(data, object_pairs_hook=unique, parse_constant=lambda value: require(False, "Nonfinite JSON value"))


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def identity(value):
    require(isinstance(value, str) and re.fullmatch("[0-9a-f]{64}", value), "Expected a lowercase SHA-256")
    return value


class Evidence:
    def __init__(self, root):
        self.root = Path(root).resolve()
        self.remaining = 256 * 1024 * 1024

    def read(self, ref, limit):
        raw = ref["path"]
        require(isinstance(raw, str) and "\\" not in raw and not Path(raw).is_absolute()
                and all(v not in ("", ".", "..") for v in raw.split("/")), "Evidence path must be project-relative")
        path = self.root / raw
        require(path.resolve().is_relative_to(self.root) and not path.is_symlink() and path.is_file(),
                f"Evidence is not a regular local file: {raw}")
        limit = min(limit, self.remaining)
        require(path.stat().st_size <= limit, "Evidence exceeds its read budget")
        with path.open("rb") as stream:
            data = stream.read(limit + 1)
        require(len(data) <= limit and sha256(data) == identity(ref["sha256"]), f"Evidence changed: {raw}")
        self.remaining -= len(data)
        return data


def percentile(values, quantile):
    ordered = sorted(values)
    at = quantile * (len(ordered) - 1)
    lo = math.floor(at)
    return ordered[lo] + (ordered[math.ceil(at)] - ordered[lo]) * (at - lo)


ROUTE_TOLERANCE = .075  # JKU: the paired-route end tolerance, applied along the whole route
ROUTE_POINTS = 1000


def _segment(p, a, b):
    """Distance from p to segment a-b, and the position along it from 0 to 1."""
    ab = [b[i] - a[i] for i in range(3)]
    length = sum(v * v for v in ab)
    s = 0.0 if length == 0 else max(0.0, min(1.0, sum((p[i] - a[i]) * ab[i] for i in range(3)) / length))
    return math.dist(p, [a[i] + s * ab[i] for i in range(3)]), s


def nearest_on_route(p, route):
    """Distance from p to a polyline, and the arc length along the polyline of the nearest point."""
    if len(route) == 1:
        return math.dist(p, route[0]), 0.0
    best, at, travelled = math.inf, 0.0, 0.0
    for a, b in zip(route, route[1:]):
        distance, s = _segment(p, a, b)
        if distance < best:
            best, at = distance, travelled + s * math.dist(a, b)
        travelled += math.dist(a, b)
    return best, at


def route_deviation(first, second):
    """Largest distance from a point of either route to the other route (the symmetric Hausdorff distance).
    It ignores clocks, so lag between independent observers is tolerated, but a detour is not."""
    return max(max(nearest_on_route(p, second)[0] for p in first), max(nearest_on_route(p, first)[0] for p in second))


def thin_route(positions, limit=ROUTE_POINTS):
    """Keep a point whenever the player has moved `spacing` from the last kept one, plus the last point. Every
    dropped point lies within `spacing` of the kept route, so deviations are resolved to that spacing."""
    length = sum(math.dist(a, b) for a, b in zip(positions, positions[1:]))
    spacing = max(.02, length / (limit - 1))
    kept = [positions[0]]
    for p in positions[1:]:
        if math.dist(p, kept[-1]) >= spacing:
            kept.append(p)
    if kept[-1] != positions[-1]:
        kept.append(positions[-1])
    return kept, spacing


def trace(data):
    result = []
    reader = csv.DictReader(io.StringIO(data.decode("ascii")))
    required = {"frame", "ns", "sector", "x", "y", "z"}
    require(reader.fieldnames and required <= set(reader.fieldnames)
            and len(set(reader.fieldnames)) == len(reader.fieldnames), "Incomplete or duplicate native trace columns")
    for row in reader:
        require(len(result) < 30000, "Frame trace exceeds 30000 rows")
        sample = {key: int(row[key]) for key in ("frame", "ns", "sector")}
        for key in ("x", "y", "z"):
            sample[key] = float(row[key])
            require(math.isfinite(sample[key]), "Nonfinite player coordinate")
        for key in ("visibleSectors", "sectorFrustums", "alphaAdjoins", "attachmentFlags", "visibleModels", "visibleModelFaces",
                    "thingCollisionQueries", "floorCacheHits"):
            sample[key] = int(row[key]) if row.get(key) not in (None, "") else None
            require(sample[key] is None or sample[key] >= 0, "Invalid native counter")
        require(sample["frame"] >= 0 and sample["ns"] >= 0 and sample["sector"] >= 0, "Invalid native frame")
        require(not result or (sample["frame"] > result[-1]["frame"] and sample["ns"] > result[-1]["ns"]),
                "Native presentation clocks must increase")
        result.append(sample)
    return result


def checkpoints(scenario):
    result = {}
    for step in scenario.get("steps", []):
        if step.get("op") == "checkpoint" and step.get("status") == "passed":
            name = step["name"]
            require(name not in result, "Repeated checkpoint")
            result[name] = step["state"]
    return result


def sample_run(run, evidence):
    launcher = decode(evidence.read(run["launcher"], 4 << 20))
    scenario = decode(evidence.read(run["scenario"], 4 << 20))
    staged = decode(evidence.read(run["staged"], 4 << 20))
    raw = evidence.read(run["frames"], 8 << 20)
    engine = identity(run["engineSha256"])
    package = identity(run["packageSha256"])
    recipe = identity(run["recipeSha256"])
    meta = dict(line.split("=", 1) for line in evidence.read(run["nativeTarget"], 4096).decode().splitlines())
    target = run["target"]
    require([int(meta["width"]), int(meta["height"])] == target["resolution"]
            and int(meta["swapInterval"]) == target["vsync"] and int(meta["fpsCap"]) == target["fpsCap"]
            and meta["renderer"] == target["renderer"] and meta["driver"] == target["driver"],
            "Declared display conditions differ from the native observation")
    require(evidence.read(run["observedCache"], 64).decode().strip() == run["cache"], "Floor-cache activation differs")
    require([v["sha256"] for v in staged if v.get("role") == "engine"] == [engine], "Staged engine differs")
    result = {"repeat": run["repeat"], "packageSha256": package, "engineSha256": engine, "recipeSha256": recipe,
              "traceSha256": run["frames"]["sha256"], "launcherSha256": run["launcher"]["sha256"],
              "config": sorted((v.get("destination"), v["sha256"]) for v in staged if v.get("role") == "config"),
              "resources": sorted((v["destination"], v["sha256"]) for v in staged if v.get("role") == "resource"),
              "cpuTimeMs": None, "gpuTimeMs": None, "collisionQueryCount": None, "accepted": False}
    if launcher.get("status") != "completed" or scenario.get("status") != "passed":
        result["reason"] = "The observed gameplay scenario failed or was incomplete."
        return result
    proof = launcher.get("gameplay", {})
    require(launcher.get("captureMode") == "gameplay" and launcher.get("immutableInputsUnchanged") is True
            and launcher.get("processExitCode") == 0 and proof.get("status") == "passed", "Unverified gameplay outcome")
    require(launcher["episodeArchive"].get("archiveSha256") == package
            and launcher["episodeArchive"].get("looseEpisodeFilesStaged") is False, "Packaged level differs")
    require(proof.get("scenarioSha256") == run["scenario"]["sha256"]
            and proof.get("scenarioInputSha256") == recipe, "Scenario evidence differs from the launcher")
    states = checkpoints(scenario)
    points = {name:state["renderedFrame"] for name,state in states.items()}
    require({"warmup", "begin", "end"} <= points.keys() and points["warmup"] < points["begin"] < points["end"],
            "Timing needs ordered warmup, begin and end checkpoints")
    frames = trace(raw)
    measured = [v for v in frames if points["begin"] <= v["frame"] <= points["end"]]
    warmup = next((v for v in frames if v["frame"] == points["warmup"]), None)
    require(warmup and len(measured) >= 21 and len(measured) == points["end"] - points["begin"] + 1,
            "Need at least 20 consecutive presentation intervals plus observed warmup")
    intervals = [(b["ns"] - a["ns"])/1_000_000 for a, b in zip(measured, measured[1:])]
    positions = [[p[k] for k in ("x","y","z")] for p in measured]
    headings = []
    for name, position in (("begin", positions[0]), ("end", positions[-1])):
        player = states[name]["player"]
        require(math.dist(position, player["position"]) <= .003, "Checkpoint and trace disagree on player position")
        heading = player["headPYR"]
        require(len(heading) == 3 and all(math.isfinite(v) for v in heading), "Invalid player view angles")
        headings.append(heading)
    distance = sum(math.dist(a, b) for a, b in zip(positions, positions[1:]))
    if (run["route"] == "stationary" and distance > .025) or (run["route"] == "walk" and distance < .05):
        result["reason"] = "Observed player movement differs from the declared stationary or traversal condition."
        return result
    overlay = measured[::max(1, math.ceil(len(measured)/255))]
    if overlay[-1] != measured[-1]:
        overlay.append(measured[-1])
    route, spacing = thin_route(positions)
    result.update(accepted=True, samples=len(intervals), start=positions[0], end=positions[-1], headPYR=headings,
                  route=route, routeSpacing=spacing,
                  warmupMs=(measured[0]["ns"]-warmup["ns"])/1e6,
                  measuredMs=sum(intervals), medianMs=statistics.median(intervals), p95Ms=percentile(intervals,.95),
                  p99Ms=percentile(intervals,.99), distance=distance, sectors=list(dict.fromkeys(x["sector"] for x in measured)),
                  overlay=[{k:p[k] for k in ("x","y","z","sector","visibleSectors","sectorFrustums","attachmentFlags",
                                              "thingCollisionQueries","visibleModels","visibleModelFaces")} for p in overlay],
                  counterMax={k:max((p[k] for p in measured if p[k] is not None),default=None) for k in
                              ("visibleSectors","sectorFrustums","alphaAdjoins","visibleModels","visibleModelFaces","thingCollisionQueries","floorCacheHits")})
    return result


def compare(plan, root):
    require(plan.get("schemaVersion") == 1 and isinstance(plan.get("runs"), list)
            and 1 <= len(plan["runs"]) <= 128, "A profile accepts 1 to 128 recorded trials")
    evidence = Evidence(root)
    groups = {}
    target = plan["runs"][0]["target"]
    fields = ("renderer","driver","hardwareClass","cpu","resolution","vsync","fpsCap","physics","clock")
    require(isinstance(target, dict) and all(k in target for k in fields)
            and target["hardwareClass"] in ("hardware","software","synthetic","unavailable"),
            "Declare the target, display, timing and physics conditions")
    require(target["clock"] == "native pre-swap monotonic nanoseconds", "A native presentation timestamp is required")
    if any(label in str(target["renderer"]).lower() for label in ("llvmpipe", "softpipe", "software rasterizer", "swiftshader")):
        require(target["hardwareClass"] == "software", "The observed renderer is a software device")
    identities = {}
    recipe_identifiers = {}
    config = None
    resources = None
    engine = None
    for r in plan["runs"]:
        require(r["target"] == target, "Trials use different hardware, rendering, physics, caps or resolutions")
        route, geometry, cache, repeat = (r[k] for k in ("route","geometry","cache","repeat"))
        require(route in ("stationary","walk") and geometry in ("model-scene","hybrid-sectors")
                and cache in ("off","on") and type(repeat) is int and 1 <= repeat <= 32, "Invalid trial coordinates")
        group = groups.setdefault((route,geometry,cache), {})
        require(repeat not in group, "Duplicate repeat in the same condition")
        result = sample_run(r,evidence)
        require(identities.setdefault(geometry,result["packageSha256"]) == result["packageSha256"],
                "Geometry variant changed package within the experiment")
        require(recipe_identifiers.setdefault(route,result["recipeSha256"]) == result["recipeSha256"],
                "A route must use the same input recipe across conditions")
        if config is None:
            config, engine, resources = result["config"], result["engineSha256"], result["resources"]
        require(config == result["config"] and engine == result["engineSha256"] and resources == result["resources"],
                "Engine, stock resources or configuration changed")
        group[repeat] = result
    conditions = []
    for (route,geometry,cache), runs in sorted(groups.items()):
        conditions.append({"route":route,"geometry":geometry,"cache":cache,"runs":[runs[k] for k in sorted(runs)]})
    comparisons = []
    reviews = {}
    for ref in plan.get("optimizationReviews", []):
        review = decode(evidence.read(ref, 65536))
        pair = (identity(review["baselinePackageSha256"]), identity(review["candidatePackageSha256"]))
        require(pair not in reviews, "Duplicate optimization review for the same packages")
        statuses = {}
        for name in ("silhouettes","transparentOpenings","sourceCoverage","gameplayCollision"):
            check = review["checks"][name]
            require(check["result"] in ("passed","failed","unobserved"), "Invalid optimization check")
            if check["result"] != "unobserved":
                require(bool(evidence.read(check["evidence"], 4<<20)), "Missing optimization evidence")
            statuses[name] = check["result"]
        reviews[pair] = statuses
    for route in ("stationary","walk"):
        for first, second in [(("model-scene","off"),("hybrid-sectors","off")),
                              (("model-scene","on"),("hybrid-sectors","on")),
                              (("model-scene","off"),("model-scene","on")),
                              (("hybrid-sectors","off"),("hybrid-sectors","on"))]:
            a, b = groups.get((route,*first),{}), groups.get((route,*second),{})
            row = {"route":route,"baseline":"/".join(first),"candidate":"/".join(second),"exceedsRepeatSpread":False}
            checks = reviews.get((identities.get(first[0]), identities.get(second[0])), {})
            row["semanticChecks"] = checks
            if len(a)<3 or a.keys()!=b.keys() or not all(x["accepted"] for x in [*a.values(),*b.values()]):
                row["reason"]="Needs at least three paired repeats and successful behavior in both conditions."
            elif any(math.dist(a[i]["start"], b[i]["start"]) > .005 or math.dist(a[i]["end"], b[i]["end"]) > .075
                     or a[i]["headPYR"] != b[i]["headPYR"] for i in a):
                row["reason"]="Observed start, end or player view angles diverge between the paired routes."
            elif max(v["routeSpacing"] for v in (*a.values(), *b.values())) > ROUTE_TOLERANCE / 3:
                row["routeAgreement"]="unverified"
                row["reason"]="A route is too long to compare along its length at the route tolerance."
            elif (deviation := max(route_deviation(a[i]["route"], b[i]["route"]) for i in a)) > ROUTE_TOLERANCE:
                row.update(routeAgreement="diverges", routeDeviation=deviation, routeTolerance=ROUTE_TOLERANCE)
                row["reason"]=("The paired runs share their endpoints but pass through different places, so they "
                               "may not have met the same geometry.")
            else:
                row.update(routeAgreement="agrees", routeDeviation=deviation, routeTolerance=ROUTE_TOLERANCE)
                summary = {}
                for metric in ("medianMs","p95Ms","p99Ms"):
                    before, after = [v[metric] for v in a.values()], [v[metric] for v in b.values()]
                    delta = [b[i][metric]-a[i][metric] for i in sorted(a)]
                    summary[metric] = {"baselineRange":[min(before),max(before)],"candidateRange":[min(after),max(after)],
                                       "pairedMedianDelta":statistics.median(delta),"allCandidateBelow":max(after)<min(before)}
                row["metrics"]=summary
                row["exceedsRepeatSpread"]=summary["medianMs"]["allCandidateBelow"] and summary["p95Ms"]["allCandidateBelow"]
            if "failed" in checks.values() or "reason" in row:
                row["optimizationStatus"] = "rejected or incomplete"
            elif not checks or "unobserved" in checks.values():
                row["optimizationStatus"] = "requires independent semantic review"
            elif row["exceedsRepeatSpread"]:
                row["optimizationStatus"] = "faster on this target; supplied semantic review passed"
            else:
                row["optimizationStatus"] = "improvement not demonstrated"
            comparisons.append(row)
    for condition in conditions:
        for run in condition["runs"]:
            run.pop("route", None)
    changes = plan.get("semanticChanges", [])
    require(isinstance(changes, list) and len(changes) <= 64 and all(isinstance(v,str) and len(v) <= 2048 for v in changes),
            "Expected a bounded list of declared changes")
    return {"schemaVersion":1,"title":str(plan.get("title","Route performance"))[:160],"target":target,
            "hardwareBenchmark":target["hardwareClass"]=="hardware","conditions":conditions,"comparisons":comparisons,
            "semanticChanges":changes,
            "limits":"Timing covers native presentation intervals. CPU/GPU stage times and total collision queries are unavailable. Thing collision queries cover one native intersection routine. A player origin and head angles do not establish pixel equivalence. Semantic review records report the supplied independent evidence."}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest",type=Path)
    parser.add_argument("--html",type=Path,help="create a new standalone visual report")
    args=parser.parse_args()
    try:
        require(args.manifest.stat().st_size <= 1<<20, "Manifest exceeds 1 MiB")
        plan=decode(args.manifest.read_bytes())
        result=compare(plan,args.manifest.parent)
        if args.html:
            from route_performance_html import render
            with args.html.open("x",encoding="utf-8") as stream:
                stream.write(render(result))
        print(json.dumps(result,allow_nan=False))
    except (OSError, ValueError, KeyError, TypeError) as ex:
        parser.exit(2,f"{ex}\n")


if __name__ == "__main__":
    main()
