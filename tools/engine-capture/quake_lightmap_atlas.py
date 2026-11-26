"""Bake Quake III / Live RGB lightmaps into UV atlases for RED model-scene imports."""
import argparse
from collections import Counter, defaultdict
from contextlib import ExitStack
from functools import lru_cache
import hashlib
import io
import json
from pathlib import Path
import re
import struct
import sys
import tempfile
import zipfile

from quake_bsp_adapter import LIMIT, source_lumps
from verify_episode_pair import archive_index

try:
    import numpy as np
    from PIL import Image
    from scipy.spatial import Delaunay, cKDTree, QhullError
    IMPORT_ERROR = None
except ImportError as dependency_error:
    IMPORT_ERROR = str(dependency_error)

PAGE = 256
GUTTER = 4
MAX_CHART_VERTICES = 262144  # Count tessellated patch vertices as well as planar corners.
MAX_PAGES = 1024
MAX_MODEL_BYTES = 8 << 20
MAX_TOTAL_MODEL_BYTES = 128 << 20
MAX_IMAGE_CACHE_BYTES = 128 << 20


def digest(data):
    return hashlib.sha256(data).hexdigest()


def file_digest(path):
    sha = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1 << 20), b""):
            sha.update(chunk)
    return sha.hexdigest()


def source_charts(data, shaders, subdivisions, models):
    _, lumps = source_lumps(data)
    surface_count = len(lumps[13]) // 104
    requested = set()
    for model in models:
        for number in model["sourceSurfaces"]:
            if type(number) is not int or not 0 <= number < surface_count:
                raise ValueError(f"RED plan references a missing source surface: {number!r}")
            requested.add(number)
    # View source vertices in-place. Promote just the admitted chart's xyz/ST/LMST.
    vertices = np.frombuffer(lumps[10], "<f4").reshape(-1, 11)[:, :7]
    shader_count = len(lumps[1]) // 72
    lightmaps = np.frombuffer(lumps[14], np.uint8).reshape(-1, 128, 128, 3)
    curves = np.array([[(1-t)**2, 2*t*(1-t), t*t] for t in np.linspace(0, 1, subdivisions+1)])
    faces = {}
    skipped = Counter()
    total_vertices = 0
    for number, row in enumerate(struct.iter_unpack("<8i64x2i", lumps[13])):
        shader, _, kind, start, count, _, _, lm, width, height = row
        if number not in requested or kind not in (1, 2) or lm < 0:
            continue
        if not (0 <= shader < shader_count and lm < len(lightmaps) and start >= 0
                and count >= 3 and start+count <= len(vertices)):
            raise ValueError(f"Invalid lightmapped surface {number}")
        name = bytes(lumps[1][shader*72:shader*72+64]).partition(b"\0")[0].decode("latin1")
        binding = shaders.get(name)
        if (binding is None or binding.get("cutout") or binding.get("translucent") or binding.get("fullbright")
                or binding.get("alpha_mode", "opaque") != "opaque"):
            skipped[name] += 1
            continue
        if binding.get("pendingStages") or ((binding.get("additionalStages") or binding.get("animates"))
                                             and "additiveStages" not in binding):
            raise ValueError(f"Review extra shader stages for {name}; supply explicit additiveStages")
        expanded = count
        if kind == 2:
            if not (width >= 3 and height >= 3 and width % 2 and height % 2 and width*height == count):
                raise ValueError(f"Invalid patch grid on surface {number}")
            expanded = (width//2) * (height//2) * (subdivisions+1)**2
        total_vertices += expanded
        if total_vertices > MAX_CHART_VERTICES:
            raise ValueError(f"Chart expanded vertex budget exceeded at surface {number}")
        v = vertices[start:start+count].astype(float)
        if not np.isfinite(v).all():
            raise ValueError(f"Nonfinite source vertex on surface {number}")
        if kind == 2:
            grid = v.reshape(height, width, 7)
            v = np.concatenate([np.einsum("ai,ijc,bj->abc", curves, grid[y:y+3, x:x+3], curves).reshape(-1, 7)
                                for y in range(0, height-2, 2) for x in range(0, width-2, 2)])
        faces[number] = {"vertices": v, "shader": name, "lightmap": lm}
    if not faces:
        raise ValueError("No supported opaque lightmapped surfaces with material bindings")
    return faces, lightmaps, skipped


def pack_charts(faces, density):
    shelves = []
    for number, face in faces.items():
        v = face["vertices"]
        uv = v[:, 5:7] * 128
        if np.min(np.ptp(uv, axis=0)) < 0.0001 or np.linalg.matrix_rank(uv - uv.mean(axis=0), tol=1e-7) < 2:
            # Constant/rank-one light coordinates still need two diffuse dimensions.
            centered = v[:, :3] - v[:, :3].mean(axis=0)
            if np.linalg.matrix_rank(centered, tol=1e-7) < 2:
                raise ValueError(f"Degenerate source surface {number}")
            axes = np.linalg.svd(centered, full_matrices=False)[2][:2].T
            uv = (v[:, :3] - v[0, :3]) @ axes / 16
        span = np.ptp(uv, axis=0)
        rate = min(density, (PAGE - 2*GUTTER - 2) / max(np.max(span), 0.0001))
        width, height = np.maximum(1, np.ceil(span * rate).astype(int) + 1) + 2*GUTTER
        face.update(chart=uv, origin=uv.min(axis=0), rate=rate, width=width, height=height)
    for number, face in sorted(faces.items(), key=lambda entry: (-entry[1]["height"], -entry[1]["width"], entry[0])):
        w, h = face["width"], face["height"]
        best = None
        for pg, rows in enumerate(shelves):
            for index, (y, height, used) in enumerate(rows):
                if height >= h and used+w <= PAGE:
                    candidate = (height-h, PAGE-used-w, pg, index)
                    if best is None or candidate < best:
                        best = candidate
        if best:
            _, _, pg, index = best
        else:
            pg = next((i for i, rows in enumerate(shelves) if rows[-1][0]+rows[-1][1]+h <= PAGE), len(shelves))
            if pg == len(shelves):
                if pg >= MAX_PAGES:
                    raise ValueError("Atlas page budget exceeded")
                shelves.append([])
            rows = shelves[pg]
            y = rows[-1][0] + rows[-1][1] if rows else 0
            index = len(rows)
            rows.append([y, h, 0])
        row = shelves[pg][index]
        face.update(page=pg, offset=np.array([row[2], row[0]]))
        row[2] += w
    return len(shelves)


def sample(image, uv, wrap):
    h, w = image.shape[:2]
    xy = uv * [w, h] - 0.5
    if not wrap:
        xy = np.clip(xy, [0, 0], [w-1, h-1])
    ij = np.floor(xy).astype(np.int64)
    x, y = ij.T
    fx, fy = (xy-ij).T[:, :, None]
    top = (1-fx)*image[y % h, x % w] + fx*image[y % h, (x+1) % w]
    bottom = (1-fx)*image[(y+1) % h, x % w] + fx*image[(y+1) % h, (x+1) % w]
    return top*(1-fy) + bottom*fy


def chart_coordinates(face, query):
    chart = face["chart"]
    values = face["vertices"][:, 3:7]
    basis = np.column_stack([chart, np.ones(len(chart))])
    affine = np.linalg.lstsq(basis, values, rcond=None)[0]
    interpolated = query @ affine[:2] + affine[2]
    if np.max(abs(basis @ affine - values)) < 0.0002:
        return interpolated
    # Curved surfaces can have non-affine diffuse coordinates. Extend the
    # closest chart triangle into the gutter for bilinear filtering at edges.
    coordinates, indices = np.unique(chart.round(6), axis=0, return_index=True)
    try:
        triangles = Delaunay(coordinates)
    except QhullError as exc:
        raise ValueError("Non-affine chart cannot be triangulated") from exc
    simplex = triangles.find_simplex(query)
    outside = simplex < 0
    if np.any(outside):
        centers = coordinates[triangles.simplices].mean(axis=1)
        simplex[outside] = cKDTree(centers).query(query[outside])[1]
    transforms = triangles.transform[simplex]
    weights = np.einsum("nij,nj->ni", transforms[:, :2], query - transforms[:, 2])
    bary = np.column_stack([weights, 1-weights.sum(axis=1)])
    return np.einsum("ni,nic->nc", bary, values[indices][triangles.simplices[simplex]])


class Images:
    def __init__(self, root, packs):
        self.root = root
        self.index = {}
        self.cache = {}
        self.digests = {}
        self.decoded_bytes = 0
        for pack in packs:
            for member in pack.infolist():
                self.index[member.filename.replace("\\", "/").lower()] = pack, member

    def load(self, name):
        if name in self.cache:
            return self.cache[name]
        path = Path(name.replace("\\", "/"))
        if path.is_absolute() or ":" in name or ".." in path.parts:
            raise ValueError(f"Expected a relative source image name: {name}")
        def exists(p):
            return (self.root / p).is_file() or p.as_posix().lower() in self.index

        extensions = (".tga", ".jpg", ".png", ".jpeg", ".bmp")
        # Retail packs sometimes ship a .jpg for a shader spelling ending in .tga.
        # Keep exact overrides; use a format substitution only when unambiguous.
        if exists(path):
            matches = [path]
        else:
            stem = path.with_suffix("") if path.suffix.lower() in extensions else path
            candidates = [Path(str(stem) + ext) for ext in extensions]
            matches = [p for p in candidates if exists(p)]
        if not matches:
            raise ValueError(f"Missing source image: {name}")
        if len(matches) != 1:
            raise ValueError(f"Multiple source images match {name}; specify the file extension")
        local = self.root / matches[0]
        if local.is_file():
            if local.stat().st_size > 32 << 20:
                raise ValueError(f"Source image too large: {name}")
            raw = local.read_bytes()
        else:
            pack, member = self.index[matches[0].as_posix().lower()]
            if member.file_size > 32 << 20:
                raise ValueError(f"Source image too large: {name}")
            raw = pack.read(member)
        with Image.open(io.BytesIO(raw)) as image:
            if max(image.size) > 8192 or image.width*image.height > 16777216:
                raise ValueError(f"Source image dimensions exceed limit: {name}")
            decoded = image.width * image.height * 3
            if self.decoded_bytes + decoded > MAX_IMAGE_CACHE_BYTES:
                raise ValueError("Decoded source image cache budget exceeded")
            self.cache[name] = np.array(image.convert("RGB"))
            self.decoded_bytes += decoded
            self.digests[name] = digest(raw)
        return self.cache[name]


def raster(directory, faces, lightmaps, bindings, images, args):
    @lru_cache(maxsize=16)
    def corrected_lightmap(index):
        shifted = lightmaps[index].astype(np.uint32) << args.lightmap_shift
        return (shifted*255 // np.maximum(255, shifted.max(axis=2, keepdims=True))).astype(np.uint8)

    pages = defaultdict(list)
    for face in faces.values():
        pages[face["page"]].append(face)
    for number, page_faces in pages.items():
        page = np.zeros((PAGE, PAGE, 3), dtype=np.uint8)
        for face in page_faces:
            w, h = face["width"], face["height"]
            y, x = np.mgrid[:h, :w]
            query = face["origin"] + (np.stack([x, y], axis=-1).reshape(-1, 2)-GUTTER) / face["rate"]
            uv = chart_coordinates(face, query)
            base = uv[:, :2]
            binding = bindings[face["shader"]]
            diffuse = sample(images.load(binding["chosenStage"]), base, True)
            lighting = sample(corrected_lightmap(face["lightmap"]), uv[:, 2:4], False) / 255
            rgb = diffuse * lighting
            for stage in binding.get("additiveStages", []):
                scale, gain = stage.get("scale", 1), stage.get("gain", 1)
                wrap = stage.get("wrap", True)
                if not isinstance(wrap, bool) or not (0 < scale <= 1024 and 0 <= gain <= 16):
                    raise ValueError(f"Invalid additive stage on {face['shader']}")
                coords = (base - .5) * scale + .5 if scale != 1 else base
                rgb += gain * sample(images.load(stage["image"]), coords, wrap)
            px, py = face["offset"]
            page[py:py+h, px:px+w] = np.clip(rgb * args.display_gain, 0, 255).round().astype(np.uint8).reshape(h, w, 3)
        Image.fromarray(page).save(directory / f"{args.prefix}{number:03}.png", compress_level=2)


def table(text, label):
    pattern = rf"(?m)^{re.escape(label)} (\d+)\n((?:\d+: [^\n]*\n)*)"
    matches = list(re.finditer(pattern, text))
    if len(matches) != 1:
        raise ValueError(f"Expected one RED model table {label}")
    match = matches[0]
    rows = []
    for i, row in enumerate(match[2].splitlines()):
        index, _, value = row.partition(": ")
        if int(index) != i:
            raise ValueError(f"Out-of-order index in {label}")
        rows.append(value)
    if len(rows) != int(match[1]):
        raise ValueError(f"Incomplete {label} table")
    return match.span(), rows


def rewrite(raw, record, faces, bindings, args, coverage):
    text = raw.decode("ascii").replace("\r\n", "\n")
    if not re.search(r"(?m)^GEOSETS 1$", text) or not re.search(r"(?m)^MESHES 1$", text):
        raise ValueError(f"Expected RED's single-mesh model, {record['name']}")
    material_span, mats = table(text, "MATERIALS")
    uv_span, uv_rows = table(text, "TEXTURE VERTICES")
    face_span, rows = table(text, "FACES")
    _, vertex_rows = table(text, "VERTICES")
    pos = np.array([[float(s) for s in row.split()[:3]] for row in vertex_rows])
    origin = np.array(record["position"])
    if origin.shape != (3,) or pos.ndim != 2 or pos.shape[1] != 3:
        raise ValueError(f"Invalid RED model positions: {record['name']}")
    pos = (pos + origin) / args.scale
    tex = np.array([[float(s) for s in row.split()] for row in uv_rows])
    if tex.ndim != 2 or tex.shape[1] != 2 or not np.isfinite(pos).all() or not np.isfinite(tex).all():
        raise ValueError(f"Invalid RED model vertices: {record['name']}")
    all_materials = {v.lower(): i for i, v in enumerate(mats)}
    generated_names = {f"{args.prefix}{face['page']:03}.mat" for face in faces.values()}
    if generated_names.intersection(all_materials):
        raise ValueError(f"Atlas material name already in use: {record['name']}")
    by_position = defaultdict(list)
    eligible = set()
    for number in dict.fromkeys(record["sourceSurfaces"]):
        if number not in faces:
            continue
        face = faces[number]
        binding = bindings[face["shader"]]
        material = binding["mat"].lower()
        dims = np.array(binding["dimensions"])
        if dims.shape != (2,) or np.min(dims) <= 0 or not np.isfinite(dims).all():
            raise ValueError(f"Invalid RED MAT dimensions for {face['shader']}")
        eligible.add(material)
        for j, v in enumerate(face["vertices"]):
            key = (material, tuple(np.rint(v[:3]*1000).astype(np.int64)))
            by_position[key].append((number, j, v[3:5]*dims))

    out = []
    new_uv = []
    indices = {}

    def uv_index(value, original=None):
        # Retain the full authored coordinates on residual faces. Keep compact
        # five-place atlas UVs on matched corners (1e-5 of a native texel).
        formatted = " ".join(f"{x:.5f}" for x in value)
        key = tuple(map(float, formatted.split()))
        if original is not None and tuple(value) != key:
            formatted, key = original, tuple(value)
        if key not in indices:
            indices[key] = len(new_uv)
            new_uv.append(formatted)
        return indices[key]

    mapped_here = 0
    for row in rows:
        fields = row.split(maxsplit=7)
        if len(fields) != 8 or not re.fullmatch(r"\d+,\s*\d+(?:\s+\d+,\s*\d+)*", fields[7]):
            raise ValueError("Expected RED textured face indices")
        material = int(fields[0])
        pairs = [(int(v), int(u)) for v, u in re.findall(r"(\d+),\s*(\d+)", fields[7])]
        if not 0 <= material < len(mats) or len(pairs) < 3 or len(pairs) != int(fields[6]):
            raise ValueError("Invalid RED face material or corner count")
        if any(v >= len(pos) or u >= len(tex) for v, u in pairs):
            raise ValueError("Face references a missing vertex")
        mat = mats[material].lower()
        matches = []
        for v, u in pairs:
            key = (mat, tuple(np.rint(pos[v]*1000).astype(np.int64)))
            available = {}
            for number, j, base in by_position[key]:
                if np.max(abs(base-tex[u])) < .007:
                    available[number] = j
            matches.append(available)
        common = set(matches[0]).intersection(*matches[1:])
        if len(common) > 1:
            raise ValueError(f"Ambiguous source lightmap for {record['name']}")
        if common:
            number = common.pop()
            face = faces[number]
            name = f"{args.prefix}{face['page']:03}.mat"
            if name not in all_materials:
                all_materials[name] = len(mats)
                mats.append(name)
            fields[0] = str(all_materials[name])
            fields[3] = "0"  # FULLYLIT for surfaces with lighting baked into RGB.
            chart = [face["chart"][candidate[number]] for candidate in matches]
            uv_ids = [uv_index(face["offset"] + GUTTER + .5 + (p-face["origin"])*face["rate"]) for p in chart]
            coverage["surfaces"][number] += 1
            coverage["mapped"] += 1
            mapped_here += 1
        else:
            if mat in eligible:
                coverage["unmatched"][mat] += 1
            coverage["untouched"] += 1
            uv_ids = [uv_index(tex[u], uv_rows[u]) for _, u in pairs]
        out.append(" ".join(fields[:7]) + " " + " ".join(f"{v}, {i}" for (v, _), i in zip(pairs, uv_ids)))
    if not mapped_here:
        return raw
    tables = [(material_span, "MATERIALS", mats), (uv_span, "TEXTURE VERTICES", new_uv), (face_span, "FACES", out)]
    for (start, end), name, values in sorted(tables, reverse=True):
        body = "".join(f"{i}: {v}\n" for i, v in enumerate(values))
        text = text[:start] + f"{name} {len(values)}\n" + body + text[end:]
    newline = "\r\n" if b"\r\n" in raw else "\n"
    return text.replace("\n", newline).encode("ascii")


def read_bsp(path, member):
    if member:
        with zipfile.ZipFile(path) as pack:
            info = pack.getinfo(member)
            if not 0 < info.file_size <= LIMIT:
                raise ValueError("BSP member exceeds RED's input limit")
            return pack.read(info)
    if path.stat().st_size > LIMIT:
        raise ValueError("BSP exceeds RED's input limit")
    return path.read_bytes()


def bake(args):
    if IMPORT_ERROR:
        raise ValueError(f"RGB atlas baking requires NumPy, Pillow and SciPy ({IMPORT_ERROR})")
    if args.output.exists():
        raise ValueError("Choose a new output directory")
    if not (0 < args.scale <= 1 and 0 < args.density <= 128 and 0 < args.display_gain <= 16):
        raise ValueError("Scale, density or display gain outside supported range")
    if not re.fullmatch(r"[a-z][a-z0-9_]{0,8}", args.prefix):
        raise ValueError("Prefix must be 1 to 9 lowercase letters, digits or underscores, starting with a letter")
    if max(args.plan.stat().st_size, args.materials.stat().st_size) > 16 << 20:
        raise ValueError("Plan or material manifest exceeds size limit")
    plan = json.loads(args.plan.read_bytes())
    if not 0 < len(plan["models"]) <= 4096:
        raise ValueError("Model count outside supported range")
    bindings = json.loads(args.materials.read_bytes())["shaders"]
    bsp = read_bsp(args.source, args.member)
    faces, lightmaps, skipped = source_charts(bsp, bindings, args.patch_subdivisions, plan["models"])
    count = pack_charts(faces, args.density)
    receipt = {"surfaces": Counter(), "unmatched": Counter(), "mapped": 0, "untouched": 0,
               "skippedBindings": skipped, "sourceCharts": len(faces), "pageCount": count,
               "modelCount": len(plan["models"]), "density": args.density, "displayGain": args.display_gain,
               "lightmapShift": args.lightmap_shift, "scale": args.scale, "patchSubdivisions": args.patch_subdivisions,
               "sourceBspSha256": digest(bsp), "planSha256": file_digest(args.plan),
               "archiveSha256": file_digest(args.archive), "materialsSha256": file_digest(args.materials),
               "pageSha256": {}, "parentModelSha256": {}, "modelSha256": {}}
    # Publish a complete set or leave the caller's destination absent.
    with tempfile.TemporaryDirectory(dir=args.output.parent) as scratch, ExitStack() as stack:
        temp = Path(scratch) / "bake"
        (temp / "models").mkdir(parents=True)
        (temp / "rgb").mkdir()
        archive = stack.enter_context(args.archive.open("rb"))
        entries = archive_index(archive)
        for i in range(count):
            mat = f"mat/{args.prefix}{i:03}.mat"
            if mat in entries:
                raise ValueError(f"Existing native MAT conflicts with atlas: {mat}")
        names = set()
        total_model_bytes = 0
        for record in plan["models"]:
            name = record["name"]
            if not re.fullmatch(r"[a-zA-Z0-9_-]+\.3do", name, re.IGNORECASE) or name.lower() in names:
                raise ValueError(f"Invalid or duplicate model name {name!r}")
            names.add(name.lower())
            key = "3do/" + name.lower()
            if key not in entries:
                raise ValueError(f"Plan model missing from archive: {name}")
            offset, size = entries[key]
            total_model_bytes += size
            if size > MAX_MODEL_BYTES or total_model_bytes > MAX_TOTAL_MODEL_BYTES:
                raise ValueError("Native model byte budget exceeded")
            archive.seek(offset)
            raw = archive.read(size)
            model = rewrite(raw, record, faces, bindings, args, receipt)
            (temp / "models" / name).write_bytes(model)
            receipt["parentModelSha256"][name] = digest(raw)
            receipt["modelSha256"][name] = digest(model)
        receipt["mappedSourceCharts"] = len(receipt["surfaces"])
        if receipt["mapped"] == 0:
            raise ValueError("No RED model face matched the source position, shader and base UV")
        if receipt["unmatched"] and not args.allow_unmatched:
            raise ValueError(f"Eligible native faces did not match source charts: {dict(receipt['unmatched'])}")
        packs = [stack.enter_context(zipfile.ZipFile(path)) for path in args.pk3]
        images = Images(args.materials.parent, packs)
        raster(temp / "rgb", faces, lightmaps, bindings, images, args)
        receipt["sourceImageSha256"] = images.digests
        for path in sorted((temp / "rgb").iterdir()):
            receipt["pageSha256"][path.name] = file_digest(path)
        (temp / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
        if args.output.exists():
            raise ValueError("Output appeared during bake")
        temp.rename(args.output)
    return receipt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="Original IBSP v46/v47 or its PK3 (use --member)")
    parser.add_argument("plan", type=Path, help="RED jo-ja-plan JSON matching the imported geometry")
    parser.add_argument("archive", type=Path, help="Input GOB/GOO containing the plan's static 3DOs")
    parser.add_argument("materials", type=Path, help="JSON mapping source shaders to images, native MATs and pixel sizes")
    parser.add_argument("output", type=Path, help="New directory for RGB pages, model overrides and receipt")
    parser.add_argument("--member", help="Exact source BSP member inside the input PK3")
    parser.add_argument("--pk3", action="append", type=Path, default=[], help="Source texture pack, later packs override earlier")
    parser.add_argument("--scale", type=float, default=1/320, help="JK units per Quake unit, as used by RED")
    parser.add_argument("--patch-subdivisions", type=int, choices=range(1, 17), default=3, help="Same tessellation as the RED import")
    parser.add_argument("--lightmap-shift", type=int, choices=(0, 1, 2), default=0)
    parser.add_argument("--density", type=float, default=7, help="Atlas pixels per source lightmap texel")
    parser.add_argument("--display-gain", type=float, default=1, help="Gain after material composition")
    parser.add_argument("--prefix", default="ql", help="Unused native MAT name prefix")
    parser.add_argument("--allow-unmatched", action="store_true", help="Keep and count unmatched faces of eligible materials")
    args = parser.parse_args()
    result = bake(args)
    print(f"{result['mapped']} mapped faces, {result['untouched']} residual faces, {result['pageCount']} RGB pages")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, KeyError, TypeError, zipfile.BadZipFile) as error:
        sys.exit(str(error))
