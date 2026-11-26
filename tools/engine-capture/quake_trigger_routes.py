"""Install Quake brush-triggered jump pads and teleporters in a JK/MotS model scene.

The source coordinates share the native axes, with a single uniform unit scale.
This profile is for a single-sector scene with the standard LucasArts walker.
"""
from __future__ import annotations

import hashlib
import math
import re
import struct


def _lump(data, index, fmt):
    offset, length = struct.unpack_from("<II", data, 8 + index * 8)
    header_length = 8 + (18 if data[4] == 47 else 17) * 8
    if ((length and offset < header_length) or offset + length > len(data)
            or length % struct.calcsize(fmt)):
        raise ValueError(f"Invalid trigger geometry in BSP lump {index}")
    return memoryview(data)[offset:offset + length]


def _scalar(value):
    return f"{value:.8f}".rstrip("0").rstrip(".") if abs(value) >= 5e-9 else "0"


def _vec(values):
    return "'" + " ".join(map(_scalar, values)) + "'"


def _append_numbered(lev, section, values):
    text = lev[section].replace("\r\n", "\n")
    indices = [int(s) for s in re.findall(r"(?m)^\s*(\d+)\s*:", text)]
    first = max(indices, default=-1) + 1
    rows = [f"{k}: {row}" for k, row in enumerate(values, first)]
    text, n = re.subn(r"(?im)^[ \t]*end[ \t]*$", lambda _: "\n".join(rows + ["end"]), text)
    if n != 1:
        raise ValueError(f"Invalid JKL {section} ending")
    capacity = re.compile(r"(?im)^(world\s+\w+[ \t]+)(\d+)")
    lev[section], count = capacity.subn(
        lambda m: m[1] + str(max(int(m[2]), first + len(rows))), text, count=1)
    if count != 1:
        raise ValueError(f"Invalid JKL {section} capacity")
    return list(range(first, first + len(rows)))


def _native_launch(delta, gravity, air_drag, step=0.02):
    """Solve for the source apex with the native walker's drag and gravity.

    For each step the engine applies drag, gravity, and then displacement. Invert
    that recurrence for a vertical launch; the same step count gives the planar
    distance multiplier. A player can still steer or collide while airborne.
    """
    if (not all(math.isfinite(v) for v in [gravity, air_drag, *delta])
            or gravity <= 0 or air_drag < 0 or air_drag * step >= 1 or delta[2] <= 0):
        raise ValueError("A jump pad needs positive height and ordinary gravity")
    decay = 1 - step * air_drag
    def flight(vz):
        height = distance = 0.0
        planar = 1.0
        for count in range(1, 10001):
            vz = vz * decay - gravity * step
            if vz <= 0:
                return height, distance, (count - 1) * step
            planar *= decay
            height += vz * step
            distance += planar * step
        raise ValueError("Jump arc exceeds the supported simulation range")
    lo = 0.0
    hi = math.sqrt(2 * gravity * delta[2]) * 2
    while flight(hi)[0] < delta[2]:
        hi *= 2
    for _ in range(48):
        mid = (lo + hi) / 2
        if flight(mid)[0] < delta[2]:
            lo = mid
        else:
            hi = mid
    _, planar, time = flight(hi)
    return [delta[0] / planar, delta[1] / planar, hi], time


def _read_triggers(entities, models, source):
    if (source[:8] not in (b"IBSP.\0\0\0", b"IBSP/\0\0\0")
            or len(source) < 8 + (18 if source[4] == 47 else 17) * 8):
        raise ValueError("Expected Quake III/Live IBSP v46 or v47")
    planes, brushes, sides = (_lump(source, i, fmt) for i, fmt in
                              [(2, "<4f"), (8, "<3i"), (9, "<2i")])
    by_name = {}
    for i, ent in enumerate(entities):
        if "targetname" in ent:
            by_name.setdefault(ent["targetname"], []).append((i, ent))
    for i, ent in enumerate(entities):
        kind = ent.get("classname", "").lower()
        if kind not in ("trigger_push", "trigger_teleport"):
            continue
        if int(ent.get("spawnflags", "0"), 0):
            raise ValueError(f"Trigger {i} uses unsupported spawn flags")
        model_name = ent.get("model", "")
        if not re.fullmatch(r"\*[1-9]\d*", model_name):
            raise ValueError(f"Trigger {i} requires a brush model")
        model = int(model_name[1:])
        if model >= len(models):
            raise ValueError(f"Trigger {i} model is missing")
        fields = models[model]
        lo, hi = list(fields[:3]), list(fields[3:6])
        if any(not math.isfinite(v) for v in lo + hi) or any(a > b for a,b in zip(lo,hi)):
            raise ValueError(f"Invalid trigger bounds for {model_name}")
        first, count = fields[8:10]
        if first < 0 or count <= 0 or first + count > len(brushes) // 12:
            raise ValueError(f"Invalid trigger brush range for {model_name}")
        hulls = []
        for index in range(first, first + count):
            start, size, _ = struct.unpack_from("<3i", brushes, index * 12)
            if start < 0 or size < 4 or start + size > len(sides) // 8:
                raise ValueError(f"Invalid brush sides for model {model_name}")
            refs = [pi for pi, _ in struct.iter_unpack("<2i",sides[start*8:(start+size)*8])]
            if any(pi < 0 or pi >= len(planes) // 16 for pi in refs):
                raise ValueError(f"Invalid plane index for model {model_name}")
            hull = [list(struct.unpack_from("<4f", planes, pi * 16)) for pi in refs]
            if not all(math.isfinite(v) for p in hull for v in p):
                raise ValueError(f"Non-finite trigger plane for model {model_name}")
            hulls.append({"brush":index, "planes":hull})
        dests = by_name.get(ent.get("target"), [])
        if len(dests) != 1:
            raise ValueError(f"Trigger {i} does not identify one destination")
        di, dest = dests[0]
        point = [float(v) for v in dest["origin"].split()]
        if len(point) != 3 or not all(math.isfinite(v) for v in point):
            raise ValueError(f"Invalid trigger destination for {i}")
        yaw = float(dest.get("angle", 0))
        if not math.isfinite(yaw) or "angles" in dest:
            raise ValueError(f"Unsupported trigger destination orientation for {i}")
        yield {"entity":i, "type":kind, "model":model, "sourceBounds":[lo, hi],
               "sourceHulls":hulls, "targetname":ent["target"], "targetEntity":di,
               "sourceTarget":point, "sourceAngle":yaw}


def add_routes(lev, files, entities, models, scale, *, source_bsp):
    # Work on a shallow copy of immutable section strings until all input and JKL
    # bindings have been checked. An invalid late route must not leave ghosts.
    original_sections = lev
    lev = dict(lev)
    scale = 1 / scale if scale > 1 else scale
    if not math.isfinite(scale) or not 0 < scale <= 1:
        raise ValueError("Expected a positive world-unit scale")
    routes = list(_read_triggers(entities, models, source_bsp))
    if not routes:
        return {"routes":[]}
    sectors = lev.get("sectors", "")
    count = re.findall(r"(?im)^world[ \t]+sectors[ \t]+(\d+)\s*$", sectors)
    indices = re.findall(r"(?im)^sector[ \t]+(\d+)\s*$", sectors)
    if count != ["1"] or indices != ["0"]:
        raise ValueError("Quake routes require a single-sector scene with sector 0")
    player = re.search(r"(?im)^walkplayer\s+[^\n]+", lev["templates"])
    if not player:
        raise ValueError("Missing walkplayer template")
    def parameter(key):
        match = re.search(r"(?i)\b" + key + r"=([^\s]+)",player[0])
        if not match:
            raise ValueError(f"Missing walkplayer {key}")
        return float(match[1])
    radius = parameter("movesize")
    if not math.isfinite(radius) or radius <= 0:
        raise ValueError("Expected positive walkplayer movesize")
    # Both stock walker models (JK ky.3do, MotS kk.3do) have this insert Z.
    # The native floor probe takes the larger of insert Z and movesize + .005.
    foot = max(radius + .005, .120114)
    drag = parameter("airdrag")
    gravity = float(re.search(r"(?im)^world\s+gravity\s+([\d.]+)", lev["header"])[1])
    mots = any("archlight" in key.replace(" ", "") for key in lev)
    script_name = "qlroutes.cog"
    if any(key.replace("\\", "/").casefold() == "cog/" + script_name for key in files):
        raise ValueError("Route COG already present")
    symbols = ["message startup", "message pulse", "thing player local",
               "vector pos local", "flex x local", "flex y local", "flex z local",
               "int current=0 local", "int previous=0 local", "int hit=0 local"]
    binds = []
    blocks = []
    for route_id, route in enumerate(routes, 1):
        lo, hi = route["sourceBounds"]
        center = [(a + b) * .5 for a, b in zip(lo, hi)]
        target = route["sourceTarget"]
        route["nativeTriggerCenter"] = [c * scale for c in center]
        gate_lo = [lo[0] * scale - radius, lo[1] * scale - radius, lo[2] * scale - radius]
        gate_hi = [hi[0] * scale + radius, hi[1] * scale + radius, hi[2] * scale + foot]
        bounds = [f"({a} >= {_scalar(l)}) && ({a} <= {_scalar(h)})"
                  for a, l, h in zip("xyz", gate_lo, gate_hi)]
        block = ["  if (" + " && ".join(bounds) + ")", "  {"]
        for hull in route["sourceHulls"]:
            block.append("    hit = 1;")
            for nx, ny, nz, dist in hull["planes"]:
                # Minimum signed plane distance across the bottom-heavy native
                # upright actor. The six model AABB tests above are only a gate.
                support = math.hypot(nx,ny)*radius + max(0,nz)*foot + max(0,-nz)*radius
                block.append(f"    if (VectorDot(pos, {_vec([nx,ny,nz])}) > {_scalar(dist*scale + support)}) hit = 0;")
            block.append(f"    if (hit) current = {route_id};")
        block += [f"    if ((current == {route_id}) && (previous != {route_id}))", "    {"]
        if route["type"] == "trigger_push":
            delta = [(v - c) * scale for v,c in zip(target,center)]
            velocity, apex_time = _native_launch(delta, gravity, drag)
            block += ["      DetachThing(player);", f"      SetThingVel(player, {_vec(velocity)});"]
            route.update(launchVelocity=velocity, nominalApexSeconds=apex_time,
                         nativeSourceApex=[v*scale for v in target])
        else:
            if not re.search(r"(?im)^ghost\s+\S+\s+.*\btype=ghost\b", lev["templates"]):
                raise ValueError("Missing native ghost destination template")
            ghost = "exit" + str(route_id)
            symbols.append("thing " + ghost + " nolink")
            yaw = (route["sourceAngle"] - 90) % 360
            dest = [target[0]*scale, target[1]*scale, (target[2]+1)*scale]
            row = f"ghost qlexit{route_id:02d} {' '.join(map(_scalar, dest))} 0 {_scalar(yaw)} 0 0"
            gid, = _append_numbered(lev, "things", [row + (" -1" if mots else "")])
            binds.append(str(gid))
            heading = math.radians(route["sourceAngle"])
            exit_vel = [400*scale*math.cos(heading), 400*scale*math.sin(heading), 0]
            block += ["      StopThing(player);", f"      TeleportThing(player, {ghost});",
                      "      DetachThing(player);", f"      SetThingVel(player, {_vec(exit_vel)});"]
            route.update(ghostThing=gid, nativeGhostPosition=dest, nativeGhostYaw=yaw,
                         exitVelocity=exit_vel)
        block += ["    }", "  }"]
        blocks.extend(block)
    script = ["# Quake brush-triggered routes for the owning player's local simulation.",
              "flags=0x240", "", "symbols", *symbols, "end", "", "code", "startup:",
              "  SetPulse(0.025);", "  return;", "", "pulse:", "  player = GetLocalPlayerThing();",
              "  if (player < 0) return;", "  if (GetThingHealth(player) <= 0)",
              "  {", "    previous = 0;", "    return;", "  }", "  pos = GetThingPos(player);",
              "  x = VectorX(pos);", "  y = VectorY(pos);", "  z = VectorZ(pos);",
              "  current = 0;", *blocks, "  previous = current;", "  return;", "end", ""]
    raw = "\r\n".join(script).encode("ascii")
    _append_numbered(lev, "cogscripts", [script_name])
    instance, = _append_numbered(lev, "cogs", [" ".join([script_name] + binds)])
    original_sections.update({key:lev[key] for key in ("cogs", "cogscripts", "things")})
    files["cog/" + script_name] = raw
    return {"routes": routes, "sourceBspSha256":hashlib.sha256(source_bsp).hexdigest(),
            "nativeCogSha256":hashlib.sha256(raw).hexdigest(), "instance":instance,
            "inputScale":scale, "gravity":gravity, "airDrag":drag, "walkerFoot":foot,
            "walkerRadius":radius, "pulseSeconds":.025, "solverStepSeconds":.02,
            "authority":"Local walker's native position/velocity; each peer runs flags=0x240"}
