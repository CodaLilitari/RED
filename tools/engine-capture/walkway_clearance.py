"""Measure sampled player clearance across a flat walkway from a RED-exported JKL."""
import argparse
from collections import deque
import json
import math
from pathlib import Path
import re
import sys

from walkway_geometry import load_level, native_mesh, require, slab


def dependencies():
    try:
        import numpy as np
        from PIL import Image, ImageDraw
        from scipy import ndimage
    except ImportError as error:
        raise ValueError("Install the analysis extras with: python -m pip install numpy scipy Pillow") from error
    return np, Image, ImageDraw, ndimage


def raster(floors, walls, things, args):
    np, Image, ImageDraw, ndimage = dependencies()
    step = args.step
    floor_xy = [(x,y) for face in floors for x,y,_ in face]
    left = min(x for x,y in floor_xy) - 3*step
    top = max(y for x,y in floor_xy) + 3*step
    w = math.ceil((max(x for x,y in floor_xy)-left)/step) + 4
    h = math.ceil((top-min(y for x,y in floor_xy))/step) + 4
    require(w*h <= 4000000 and max(w,h) <= 8192, "Grid is too large; use a larger --step or a smaller sector")

    def pixel(x,y):
        return round((x-left)/step), round((top-y)/step)

    def polygon(draw, face, value):
        pts = [pixel(x,y) for x,y,_ in face]
        if len(pts) >= 3:
            draw.polygon(pts, fill=value)
        elif pts:
            draw.line([pts[0],pts[-1]], fill=value)

    floor = Image.new("L", (w,h))
    draw = ImageDraw.Draw(floor)
    for face in floors:
        polygon(draw, face, 255)
    low, high = args.floor_z+args.foot_offset, args.floor_z+args.height
    for face in walls:
        polygon(draw, slab(face, low, high), 0)
    free = np.asarray(floor) != 0

    centers = {}
    ignored = []
    solids = []
    fixed_path_poses = []
    models = {}
    work = 0
    allocated_pixels = 0
    model_dir = {}
    if args.models:
        for entry in args.models.iterdir():
            if entry.suffix.lower() == '.3do':
                model_dir.setdefault(entry.name.casefold(), []).append(entry)
        require(len(model_dir) <= 4096, 'Model directory exceeds 4096 distinct 3DO names')
    for name, pose, props in things:
        x,y,z,pitch,yaw,roll = pose
        if name.casefold() in args.anchor_keys:
            require(name.casefold() not in centers, f"Ambiguous anchor name {name}")
            require(args.floor_z-1e-5 <= z <= high, f"Anchor {name} is outside the selected floor height range")
            gx,gy = pixel(x,y)
            require(0 <= gx < w and 0 <= gy < h, f"Anchor {name} falls outside the selected floor bounds")
            centers[name.casefold()] = (gx,gy)
        collide = int(props.get("collide", "0"), 0)
        if collide == 0:
            continue
        if props.get("type") in {"player","actor","item","weapon","explosion","particle"}:
            ignored.append(name)
            continue
        move = props.get("move", "none")
        require("frame" not in props and int(props.get("numframes","0"),0) == 0,
                f"Authored movement frames on {name}; a static walkway check cannot evaluate their path")
        if move == "path" and args.stationary_paths:
            fixed_path_poses.append(name)
        else:
            require(move == "none", f"Cannot assume move={move} Thing {name} is stationary"
                    + ("; review its placement and pass --stationary-paths for frameless architecture" if move == "path" else ""))
        allocated_pixels += w*h
        require(allocated_pixels <= 256000000, "Too many placed objects for this sampling resolution")
        solid = Image.new("L", (w,h))
        pen = ImageDraw.Draw(solid)
        if collide == 1:
            size = max(float(props.get("size","0")), float(props.get("movesize","0")))
            require(0 < size <= 1e5, f"Unresolved sphere radius on {name}")
            if z+size < low or z-size > high:
                continue
            pen.ellipse([pixel(x-size,y+size), pixel(x+size,y-size)], fill=255)
        else:
            require(collide == 3, f"Unsupported collide={collide} on {name}")
            model = props.get("model3d","")
            require(re.fullmatch(r"[A-Za-z0-9_+. -]+\.3do", model, re.I), f"Missing or unsupported model name on {name}")
            if model not in models:
                require(len(models) < 128, "More than 128 distinct collidable models")
                require(args.models is not None, f"Pass --models with the extracted 3DO directory for {name}: {model}")
                matches = model_dir.get(model.casefold(), [])
                require(len(matches) == 1 and matches[0].is_file(), f"Missing or ambiguous model {model}")
                path = matches[0]
                models[model] = native_mesh(path)
            faces, _ = models[model]
            work += sum(len(face) for face in faces)
            require(work <= 2000000, "Excessive placed-model corner count")
            require(abs(pitch) <= 1e-6 and abs(roll) <= 1e-6, f"Unsupported pitch or roll on collidable Thing {name}")
            c,s = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
            for face in faces:
                # All transforms are rigid yaw plus translation. Clip before projecting.
                local = slab(face, low-z, high-z)
                polygon(pen, [(x+c*a-s*b,y+s*a+c*b,z+d) for a,b,d in local], 255)
        box = solid.getbbox()
        if box:
            x1,y1,x2,y2 = box
            filled = ndimage.binary_fill_holes(np.asarray(solid.crop(box)) != 0)
            free[y1:y2,x1:x2] &= ~filled
            solids.append(name)
    require(centers.keys() == args.anchor_keys, "Missing selected anchors: " + ", ".join(sorted(args.anchor_keys-centers.keys())))
    return free, centers, solids, ignored, fixed_path_poses, {m:d for m,(_,d) in models.items()}, left, top


def find_paths(mask, points):
    """Recover actual four-neighbour paths on a padded grid at the chosen radius."""
    np, _, _, _ = dependencies()
    width = mask.shape[1]
    allowed = mask.ravel()
    cells = [y*width+x for x,y in points]
    remain = set(cells[1:]) - {cells[0]}
    previous = bytearray(allowed.size)
    previous[cells[0]] = 5
    queue = deque([cells[0]])
    steps = (1,-1,width,-width)
    while queue and remain:
        base = queue.popleft()
        for direction, delta in enumerate(steps, 1):
            target = base + delta
            # Three pixels of zero border surround the footprint.
            if allowed[target] and not previous[target]:
                previous[target] = direction
                queue.append(target)
                remain.discard(target)
    require(not remain, "Could not trace a connected route")
    visited = {cells[0]}
    for pos in cells[1:]:
        while pos not in visited:
            visited.add(pos)
            pos -= steps[previous[pos]-1]
    return sorted(visited)


def analyze(args):
    np, Image, ImageDraw, ndimage = dependencies()
    keys = [n.casefold() for n in args.anchor]
    require(2 <= len(set(keys)) == len(keys) <= 64, "Select 2 to 64 distinct named Things")
    args.anchor_keys = set(keys)
    values = [args.floor_z,args.radius,args.step,args.height,args.foot_offset]
    require(all(math.isfinite(v) for v in values) and .001 <= args.step <= 1 and
            0 < args.radius <= 100 and 0 < args.foot_offset < args.height <= 100,
            "Invalid floor, radius, sampling step or vertical slab")
    floors,walls,things,jkl_sha = load_level(args.level,args.sector,args.floor_z)
    free,centers,solids,ignored,fixed_path_poses,hashes,left,top = raster(floors,walls,things,args)
    points = [centers[key] for key in keys]
    # The guard covers rounding both the boundary and the sampled player centre.
    distance = np.maximum(ndimage.distance_transform_edt(free)*args.step - 2*math.sqrt(2)*args.step, 0)
    hole = None
    if args.around is not None:
        hx,hy = args.around
        require(math.isfinite(hx) and math.isfinite(hy), "Invalid --around coordinates")
        cx,cy = round((hx-left)/args.step), round((top-hy)/args.step)
        h,w = free.shape
        require(0 <= cx < w and 0 <= cy < h and not free[cy,cx],
                "--around must mark an excluded point inside the walkway plan bounds")
        hole = (cy,cx)

    def component(radius):
        labels, _ = ndimage.label(free & (distance >= radius))
        ids = [int(labels[y,x]) for x,y in points]
        selected = labels == ids[0] if ids[0] else labels < 0
        connected = ids[0] > 0 and all(i == ids[0] for i in ids)
        # 8-connected exterior prevents diagonal-only contacts from closing a ring.
        surrounds = bool(ndimage.binary_fill_holes(selected,structure=np.ones((3,3),bool))[hole]) if hole else None
        return connected and surrounds is not False, selected, ids, surrounds
    fits, selected, ids, surrounds = component(args.radius)
    low = 0.0
    high = min(float(distance[y,x]) for x,y in points)
    best_mask = None
    zero_fits = component(0)[0]
    if zero_fits:
        while high-low > args.step/4:
            mid = (low+high)/2
            if component(mid)[0]:
                low = mid
            else:
                high = mid
        best_mask = component(low)[1]
    if best_mask is None and all(i > 0 and i == ids[0] for i in ids):
        best_mask = selected  # The anchor path can succeed even when the optional loop is open.
    paths = find_paths(best_mask,points) if best_mask is not None else []
    pinch = None
    if paths:
        idx = min(paths,key=distance.ravel().__getitem__)
        y,x = divmod(idx,distance.shape[1])
        pinch = dict(x=left+x*args.step,y=top-y*args.step,sampledClearance=float(distance[y,x]))

    rgb = np.zeros((*free.shape,3),dtype=np.uint8)
    rgb[free] = [82,84,100]
    rgb[(distance >= args.radius) & free] = [121,112,129]
    rgb[selected] = [33,135,118]
    if paths:
        rgb.reshape(-1,3)[paths] = [255,188,63]
    overlay = Image.fromarray(rgb)
    pen = ImageDraw.Draw(overlay)
    if pinch:
        y,x = divmod(idx,distance.shape[1])
        pen.ellipse((x-5,y-5,x+5,y+5),outline="red",width=2)
    if hole:
        cy,cx = hole
        pen.ellipse((cx-4,cy-4,cx+4,cy+4),outline="cyan",width=2)
    for i,(x,y) in enumerate(points,1):
        pen.rectangle((x-2,y-2,x+2,y+2),fill="white")
        pen.text((x+3,y-4),str(i),fill="white",stroke_width=1,stroke_fill="black")
    anchors = [dict(name=n,sampledXY=[left+x*args.step,top-y*args.step],
                    sampledClearance=float(distance[y,x]),connectedToFirst=bool(ids[0] and ids[i]==ids[0]))
               for i,(n,(x,y)) in enumerate(zip(args.anchor,points))]
    result = dict(schemaVersion=1, method="sampled-static-planar-clearance", levelSha256=jkl_sha,
                  sector=args.sector, floorZ=args.floor_z, bodySlab=[args.floor_z+args.foot_offset,args.floor_z+args.height],
                  resolution=args.step, radius=args.radius, gridSize=list(overlay.size), floorFaces=len(floors),
                  sampledConnected=bool(ids[0] and all(i==ids[0] for i in ids)), passes=fits,
                  aroundXY=args.around, surroundsAtRadius=surrounds, widestSampledRadius=low if zero_fits else None, anchorPathPinch=pinch,
                  anchors=anchors, appliedStaticObstacles=solids, stationaryPathPoses=fixed_path_poses, transientThingsExcluded=ignored, modelSha256=hashes)
    return result, overlay


def main(argv=None):
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("level",type=Path,help="Extracted or exported native JKL")
    cli.add_argument("--sector",type=int,required=True)
    cli.add_argument("--floor-z",type=float,required=True,help="Horizontal floor height in JKU")
    cli.add_argument("--radius",type=float,required=True,help="Player collision radius in JKU")
    cli.add_argument("--height",type=float,required=True,help="Top of occupied slab above floor")
    cli.add_argument("--foot-offset",type=float,default=.002,help="Bottom of occupied slab above floor (default .002 JKU)")
    cli.add_argument("--step",type=float,default=.005,help="Plan grid resolution in JKU (default .005)")
    cli.add_argument("--anchor",action="append",required=True,help="Named Thing in the sector; repeat for each start or destination")
    cli.add_argument("--stationary-paths",action="store_true",help="Use stored poses of frameless move=path architecture after reviewing it")
    cli.add_argument("--around",nargs=2,type=float,metavar=("X","Y"),help="Also require a connected loop around this excluded interior point")
    cli.add_argument("--models",type=Path,help="Flat folder of extracted native collision .3do files")
    cli.add_argument("--out",type=Path,required=True,help="New directory for report.json and overlay.png")
    args = cli.parse_args(argv)
    try:
        result, overlay = analyze(args)
        import tempfile
        args.out.parent.mkdir(parents=True,exist_ok=True)
        if args.out.exists():
            raise ValueError(f"Output already exists: {args.out}")
        with tempfile.TemporaryDirectory(dir=args.out.parent) as staging:
            out = Path(staging)/"result"
            out.mkdir()
            overlay.save(out/"overlay.png")
            (out/"report.json").write_text(json.dumps(result,indent=2,allow_nan=False)+"\n",encoding="utf8")
            out.rename(args.out)
        print(f"{args.out}: passes={result['passes']}")
        return 0 if result["passes"] else 1
    except (OSError, ValueError, IndexError, OverflowError) as error:
        print(f"walkway-clearance: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
