"""Prepare matched ordinary-input runs for a target Thing and a neighbouring control."""
import argparse
import hashlib
import json
import re
from pathlib import Path

import gameplay_capture
from verify_episode_pair import archive_index


OBSERVER = """# RED trial observer. The selected Things have no event links to this COG.
symbols
int target=@TARGET@ local
int control=@CONTROL@ local
int signalbin=@SIGNAL@ local
int targetSig=-1 local
int controlSig=-1 local
int status=0 local
thing player local
message startup
message pulse
end
code
startup:
    SetPulse(0.05);
    return;
pulse:
    player = GetLocalPlayerThing();
    if (player < 0) return;
    if (status == 0) {
        targetSig = GetThingSignature(target);
        controlSig = GetThingSignature(control);
        if ((targetSig <= 0) || (controlSig <= 0) ||
            (GetCollideType(target) != 3) || (GetCollideType(control) != 3)) {
            status = 4;
        } else {
            status = 1;
        }
    }
    if ((status == 1) || (status == 2)) {
        if ((GetThingSignature(control) != controlSig) || (GetCollideType(control) != 3)) {
            status = 3;
        } else if (GetThingType(target) <= 0) {
            status = 2;
        } else if ((status == 2) || (GetThingSignature(target) != targetSig) ||
                   (GetCollideType(target) != 3)) {
            status = 5;
        }
    }
    SetInv(player, signalbin, status);
    return;
end
"""


def profile(game, edition, episode, map_name, target, control, signal_bin, ammo_bin, ammo):
    if game not in ("jk", "mots") or edition not in ("indexed8", "rgb16"):
        raise ValueError("Choose the installed game and texture edition.")
    if not re.fullmatch(r"[A-Za-z0-9_-]{1,48}", episode) or not re.fullmatch(r"[A-Za-z0-9_-]{1,48}\.jkl", map_name):
        raise ValueError("Expected a simple episode name and a .jkl map name.")
    if any(type(n) is not int or not 0 <= n < 32768 for n in (target, control)) or target == control:
        raise ValueError("Choose two distinct nonnegative Thing indices below 32768.")
    if any(type(n) is not int or not 0 <= n < 200 for n in (signal_bin, ammo_bin)) or signal_bin == ammo_bin:
        raise ValueError("Choose separate registered inventory bins in the range 0..199.")
    if type(ammo) is not int or not 1 <= ammo <= 100000:
        raise ValueError("Starting ammunition must be positive.")
    return {"game":game, "edition":edition, "episode":episode, "map":map_name, "target":target,
            "control":control, "signalBin":signal_bin, "ammoBin":ammo_bin, "startingAmmo":ammo}


def recipe(p, fire):
    launch = {k:p[k] for k in ("game","episode","map")}
    signal = f"player.inventory.{p['signalBin']}"
    ammo = f"player.inventory.{p['ammoBin']}"
    initial = {"levelActive":True, signal:1, ammo:p["startingAmmo"]}
    ended = {"levelActive":True, signal:2, ammo:{"max":p["startingAmmo"]-1, "min":0}} if fire else initial
    steps = [{"op":"wait","name":"two-intact-panes","expect":initial,"timeout":35,
              "retryTap":{"key":"return","seconds":0.08,"every":1}},
             {"op":"checkpoint","name":"before"}]
    if fire:
        steps.extend([{"op":"command","value":"button 1 1"},
                      {"op":"sleep","seconds":0.7},
                      {"op":"command","value":"button 1 0"},
                      {"op":"wait","name":"target-cleared","expect":ended,"timeout":4}])
    else:
        steps.append({"op":"observe","name":"withhold-fire","seconds":0.7,"maxGapSeconds":0.5,"expect":initial})
    steps.extend([{"op":"observe","name":"control-survives","expect":ended,"seconds":0.8,"maxGapSeconds":0.5},
                  {"op":"checkpoint","name":"after"}])
    return {"schemaVersion":1, "name":f"pane-{p['game']}-{p['edition']}-{'fire' if fire else 'hold'}",
            "launch":launch, "steps":steps}


def observer(p):
    script = OBSERVER
    for marker,key in (("@TARGET@","target"),("@CONTROL@","control"),("@SIGNAL@","signalBin")):
        script = script.replace(marker, str(p[key]))
    return script


def prepare(destination, p):
    files = {"pane-profile.json":p}
    for fire, filename in ((False,"no-fire.json"),(True,"fire.json")):
        files[filename] = recipe(p, fire)
        gameplay_capture.validate_scenario(files[filename])
    destination.mkdir(exist_ok=False)
    for filename, value in files.items():
        (destination / filename).write_text(json.dumps(value, indent=2) + "\n",encoding="utf-8")
    (destination / "redpane.cog").write_text(observer(p), encoding="ascii")


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8"))


def identity(path):
    with path.open("rb") as stream:
        digest = hashlib.sha256()
        for chunk in iter(lambda: stream.read(65536), b""):
            digest.update(chunk)
        return digest.hexdigest()


def verify_run(directory, p, fire):
    spec = gameplay_capture.validate_scenario(recipe(p, fire))
    receipt = read_json(directory / "launcher-receipt.json")
    report = read_json(directory / "scenario.json")
    inputs = receipt["gameplayInputs"]
    summary = receipt["gameplay"]
    if (receipt["status"] != "completed" or receipt.get("immutableInputsUnchanged") is not True
            or summary["status"] != "passed" or report["status"] != "passed"
            or report["inputs"] != inputs or report["physicsOverrides"] is not False):
        raise ValueError(f"{directory}: the engine trial did not complete with unchanged inputs")
    if (read_json(directory / "scenario-input.json") != spec
            or identity(directory / "scenario-input.json") != inputs["scenarioInputSha256"]
            or inputs["scenarioInputSha256"] != report["scenarioSha256"]
            or identity(directory / "scenario.json") != summary["scenarioSha256"]
            or identity(directory / "agent/commands.jsonl") != summary["journal"]["sha256"]):
        raise ValueError(f"{directory}: retained recipe, report or journal differs from its receipt")
    if [(step["op"], step["name"], step["status"]) for step in report["steps"]] != [
            (step["op"], step["name"], "passed") for step in spec["steps"]]:
        raise ValueError(f"{directory}: incomplete or unexpected scenario steps")
    for step in report["steps"]:
        if step["op"] == "checkpoint":
            image = step["capture"]
            if (not re.fullmatch(r"frame-[1-9][0-9]*\.ppm", image["file"])
                    or identity(directory / "agent" / image["file"]) != image["sha256"]):
                raise ValueError(f"{directory}: checkpoint frame changed or missing")
    last = report["finalState"]
    expected = next(s["expect"] for s in spec["steps"] if s["name"] == "control-survives")
    if gameplay_capture.mismatches(last, expected, None):
        raise ValueError(f"{directory}: final native inventory does not match the selected trial")
    if any(not re.fullmatch("[0-9a-f]{64}", inputs[key] or "") for key in ("engineSha256", "packageSha256")):
        raise ValueError(f"{directory}: missing package or engine identity")
    archive = receipt["episodeArchive"]
    expected_paths = {f"episode/{p['episode']}.{ext}" for ext in ("gob","goo")}
    if archive["path"] not in expected_paths or archive["mapEntry"] != "jkl\\" + p["map"].lower():
        raise ValueError(f"{directory}: the captured episode and map differ from the selected profile")
    package = directory / archive["path"]
    if identity(package) != inputs["packageSha256"] or inputs["packageSha256"] != archive["archiveSha256"]:
        raise ValueError(f"{directory}: staged episode differs from the capture receipt")
    with package.open("rb") as stream:
        entry = archive_index(stream).get("cog/redpane.cog")
        if entry is None or entry[1] > 32 * 1024:
            raise ValueError(f"{directory}: missing expected pane observer")
        stream.seek(entry[0])
        actual = stream.read(entry[1]).decode("ascii").replace("\r\n", "\n")
        if actual != observer(p):
            raise ValueError(f"{directory}: packaged observer differs from the selected Thing and signal bindings")
    return {"engineSha256":inputs["engineSha256"], "packageSha256":inputs["packageSha256"],
            "scenarioSha256":summary["scenarioSha256"],
            "remainingAmmo":gameplay_capture.lookup(last, f"player.inventory.{p['ammoBin']}")}


def compare(p, no_fire, fire):
    hold = verify_run(no_fire, p, False)
    shot = verify_run(fire, p, True)
    if any(hold[key] != shot[key] for key in ("engineSha256", "packageSha256")):
        raise ValueError("Use the same engine and packaged episode for the two runs.")
    return {"status":"passed", "profile":p, "noFire":hold, "fire":shot}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    actions = parser.add_subparsers(dest="action", required=True)
    create = actions.add_parser("prepare", help="write an observer and a matched pair of recipes")
    create.add_argument("out", type=Path)
    create.add_argument("--game", choices=["jk","mots"], required=True)
    create.add_argument("--edition", choices=["indexed8","rgb16"], required=True)
    create.add_argument("--episode", required=True)
    create.add_argument("--map", dest="map_name", required=True)
    for arg in ("target","control","signal-bin","ammo-bin","starting-ammo"):
        create.add_argument("--"+arg, type=int, required=True)
    check = actions.add_parser("compare", help="check completed gameplay sessions against the profile")
    check.add_argument("profile", type=Path)
    check.add_argument("no_fire", type=Path)
    check.add_argument("fire", type=Path)
    args = parser.parse_args()
    try:
        if args.action == "compare":
            raw = read_json(args.profile)
            p = profile(raw["game"], raw["edition"], raw["episode"], raw["map"], raw["target"],
                        raw["control"], raw["signalBin"], raw["ammoBin"], raw["startingAmmo"])
            print(json.dumps(compare(p, args.no_fire, args.fire), indent=2))
        else:
            p = profile(args.game, args.edition, args.episode, args.map_name, args.target, args.control,
                        args.signal_bin, args.ammo_bin, args.starting_ammo)
            prepare(args.out, p)
    except (ValueError, OSError, KeyError, TypeError, gameplay_capture.AgentError) as error:
        parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    main()
