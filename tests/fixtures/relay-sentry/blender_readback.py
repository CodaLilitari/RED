"""Verify the RED-emitted fixture through an installed Blender-Sith add-on in background mode."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def readback(source):
    import bpy
    import addon_utils
    from sith.model import model3doImporter, model3doLoader
    from sith.key import keyImporter, keyLoader

    require(bpy.app.background, "Use a fresh background Blender process for this fixture.")
    require(addon_utils.enable("sith", default_set=False) is not None, "Enable the maintained Blender-Sith package.")
    report = {"blender": list(bpy.app.version), "models": []}
    with tempfile.TemporaryDirectory(prefix="red-model-readback-") as temp:
        palette = Path(temp) / "relay.cmp"
        palette.write_bytes(struct.pack("<4sii52s", b"CMP ", 30, 0, bytes(52))
                            + bytes(v for i in range(256) for v in (i, 255-i, i)) + bytes(range(256))*64)
        for name, count in (("relay_single.3do", 1), ("relay_sentry.3do", 2)):
            path = source / name
            model, _ = model3doLoader.load3do(str(path), allowErrors=False)
            require(len(model.geosets) == count and all(len(g.meshes) == 2 for g in model.geosets), "Source geoset mismatch.")
            require([(n.name, n.parentIdx) for n in model.meshHierarchy] == [("base", -1), ("boom", 0)], "Hierarchy mismatch.")
            require(tuple(model.meshHierarchy[1].pivot) == (0.01, 0, 0), "Unexpected boom pivot.")
            require(list(model.materials) == ["relay_skin.mat"], "Material dependency mismatch.")
            root = model3doImporter.import3do(str(path), mat_dirs=[str(source)], cmp_file=str(palette),
                    uvAbsolute_2_1=True, importVertexColors=True, importRadiusObj=False, preserveOrder=True, clearScene=True)
            require(root is not None, "Scene importer refused the model.")
            meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
            require(sorted(o.name for o in meshes) == ["no0_base", "no1_boom"] and all(o.data.materials for o in meshes),
                    "Scene did not bind both meshes and their material.")
            report["models"].append({"file": name, "sourceGeosets": count, "sceneLod": 0,
                                    "sha256": hashlib.sha256(path.read_bytes()).hexdigest()})
    path = source / "relay_point.key"
    key = keyLoader.loadKey(path, allowErrors=False)
    require(key.fps == 12 and key.numJoints == 2 and [(n.idx, n.meshName) for n in key.nodes] == [(1, "boom")],
            "KEY timing or native joint mapping changed.")
    markers = [(m.frame, m.type.value) for m in key.markers]
    require(markers == [(3, 1), (9, 6)], "Marker frame or type changed.")
    bpy.context.view_layer.objects.active = root
    keyImporter.importKey(str(path), bpy.context.scene, False, True, False, allowErrors=False)
    actions = [a.name for a in bpy.data.actions]
    require("no1_boomAction" in actions, "No scene action bound to the KEY's mesh.")
    report.update(keySha256=hashlib.sha256(path.read_bytes()).hexdigest(), fps=key.fps, joint=1, markers=markers, actions=actions)
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("emitted_fixture", type=Path)
    args = parser.parse_args(sys.argv[sys.argv.index("--")+1:] if "--" in sys.argv else sys.argv[1:])
    print(json.dumps(readback(args.emitted_fixture), indent=2))
