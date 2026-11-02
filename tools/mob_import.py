#!/usr/bin/env python3
# Imports the Bedrock geo.json models + skins listed in mobs_manifest.txt and
# writes include/common/mob_models_gen.hpp (static model tables) and
# bitmaps/mobs.png (one 64x64 layer per skin, 64x32 skins padded at the top).
# Run by hand via `make mobs-import`; the outputs are committed so normal
# builds never need bedrock-samples. Both outputs are deterministic.

import json
import os
import sys

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BEDROCK_SAMPLES = os.environ.get("BEDROCK_SAMPLES", os.path.join(ROOT, "bedrock-samples", "resource_pack"))
MANIFEST = os.path.join(ROOT, "tools", "mobs_manifest.txt")
GEN_HPP = os.path.join(ROOT, "include", "common", "mob_models_gen.hpp")
OUT_PNG = os.path.join(ROOT, "bitmaps", "mobs.png")

LAYER_SIZE = 64


def fail(msg):
    sys.stderr.write("mob_import: %s\n" % msg)
    sys.exit(1)


def strip_comments(text):
    # Bedrock geo files carry // comments. Remove them without touching
    # string literals.
    out = []
    i = 0
    in_string = False
    while i < len(text):
        c = text[i]
        if in_string:
            out.append(c)
            if c == "\\" and i + 1 < len(text):
                out.append(text[i + 1])
                i += 2
                continue
            if c == '"':
                in_string = False
            i += 1
            continue
        if c == '"':
            in_string = True
            out.append(c)
            i += 1
            continue
        if c == "/" and i + 1 < len(text) and text[i + 1] == "/":
            while i < len(text) and text[i] != "\n":
                i += 1
            continue
        out.append(c)
        i += 1
    return "".join(out)


def parse_bone(b):
    name = b.get("name")
    if not name:
        fail("bone without a name")
    never_render = bool(b.get("neverRender", False))
    cubes = []
    bone_mirror = bool(b.get("mirror", False))
    for c in b.get("cubes", []):
        uv = c.get("uv")
        if not isinstance(uv, list) or len(uv) != 2:
            fail("bone %s: cube has non-box uv (per-face uv is not supported)" % name)
        cubes.append(
            {
                "origin": [float(v) for v in c["origin"]],
                "size": [float(v) for v in c["size"]],
                "uv": [float(v) for v in uv],
                "inflate": float(c.get("inflate", 0.0)),
                "mirror": bool(c.get("mirror", bone_mirror)),
            }
        )
    return {
        "name": name,
        "parent": b.get("parent"),
        "pivot": [float(v) for v in b.get("pivot", [0.0, 0.0, 0.0])],
        "rot": [float(v) for v in b.get("rotation", b.get("bind_pose_rotation", [0.0, 0.0, 0.0]))],
        "mirror": bone_mirror,
        "never_render": never_render,
        "cubes": cubes,
    }


def load_geometries(path):
    with open(path, "r") as f:
        doc = json.loads(strip_comments(f.read()))
    geos = {}  # id -> {"tw","th","bones" raw dict list, "parent"}
    if "minecraft:geometry" in doc:
        # 1.12+ format: list of geometry objects with a description block.
        for g in doc["minecraft:geometry"]:
            desc = g["description"]
            ident = desc["identifier"]
            parent = None
            if ":" in ident:
                ident, parent = ident.split(":", 1)
            geos[ident] = {
                "tw": int(desc.get("texture_width", 64)),
                "th": int(desc.get("texture_height", 64)),
                "tex_set": "texture_width" in desc or "texture_height" in desc,
                "bones": g.get("bones", []),
                "parent": parent,
            }
    else:
        # 1.8 format: geometry bodies are top-level keys.
        for key, g in doc.items():
            if not key.startswith("geometry."):
                continue
            ident = key
            parent = None
            if ":" in key:
                ident, parent = key.split(":", 1)
            geos[ident] = {
                "tw": int(g.get("texturewidth", 64)),
                "th": int(g.get("textureheight", 64)),
                "tex_set": "texturewidth" in g or "textureheight" in g,
                "bones": g.get("bones", []),
                "parent": parent,
            }
    return geos


def resolve_geometry(geos, ident):
    # Resolve a:b inheritance. A same-named child bone merges into the parent
    # bone field-by-field (child wins), and its cubes are ADDED to the
    # inherited ones — that's how the sheep's wool cubes layer over the
    # sheared body so the face and leg bottoms still show. "reset": true
    # drops the inherited bone outright (armor pieces strip the body to keep
    # only their own part). A child cube at the exact same box replaces the
    # parent's cube there so coincident twins never z-fight.
    if ident not in geos:
        fail("geometry id %s not found" % ident)
    geo = geos[ident]
    if geo["parent"] is None:
        return geo
    parent = resolve_geometry(geos, geo["parent"])
    if not geo["tex_set"]:
        geo["tw"] = parent["tw"]
        geo["th"] = parent["th"]
    bones = [dict(b) for b in parent["bones"]]
    order = {b["name"]: i for i, b in enumerate(bones)}
    for child in geo["bones"]:
        name = child["name"]
        if name not in order:
            order[name] = len(bones)
            bones.append(child)
            continue
        base = bones[order[name]]
        if child.get("reset"):
            merged = {k: base[k] for k in ("name", "parent", "pivot", "rotation", "bind_pose_rotation") if k in base}
            merged.update({k: v for k, v in child.items() if k != "reset"})
        else:
            boxes = {(tuple(c["origin"]), tuple(c["size"])) for c in child.get("cubes", [])}
            merged = dict(base)
            merged.update(child)
            merged["cubes"] = [c for c in base.get("cubes", []) if (tuple(c["origin"]), tuple(c["size"])) not in boxes] + child.get(
                "cubes", []
            )
            if child.get("cubes"):
                # The child brings its own renderable content, so rendering
                # flags of the parent's slot must not leak in (the player's
                # hat over humanoid's hidden hat bone; custom's explicitly
                # mapped left limbs over humanoid's mirrored right limbs).
                for flag in ("neverRender", "mirror"):
                    if flag not in child:
                        merged.pop(flag, None)
        bones[order[name]] = merged
    geo["bones"] = bones
    geo["parent"] = None
    return geo


def fmt_f(x):
    # repr(float) always carries a decimal point, which a C++ float literal needs.
    return repr(float(x)) + "f"


def fmt_vec(v):
    return "{ %s }" % ", ".join(fmt_f(x) for x in v)


def parse_manifest():
    entries = []
    with open(MANIFEST, "r") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            fields = line.split("|")
            if len(fields) < 4 or len(fields) > 5:
                fail("bad manifest line: %s" % line)
            rot_overrides = {}
            if len(fields) == 5:
                for item in fields[4].split(";"):
                    if not item:
                        continue
                    bone, rot = item.split("=", 1)
                    rot_overrides[bone] = [float(x) for x in rot.split(",")]
                    if len(rot_overrides[bone]) != 3:
                        fail("bad rest rotation: %s" % item)
            entries.append(
                {
                    "name": fields[0],
                    "geo_file": fields[1],
                    "geo_id": fields[2],
                    "skins": fields[3].split(","),
                    "rot_overrides": rot_overrides,
                }
            )
    return entries


def main():
    entries = parse_manifest()
    models = []
    skins = []
    for e in entries:
        path = os.path.join(BEDROCK_SAMPLES, e["geo_file"])
        if not os.path.isfile(path):
            fail("missing geo file %s" % path)
        geo = resolve_geometry(load_geometries(path), e["geo_id"])
        bones = [parse_bone(b) for b in geo["bones"]]
        all_bones = {b["name"]: b for b in bones}
        # Bedrock's neverRender bones (armor slot holders, base-model stubs)
        # carry no renderable geometry for us.
        bones = [b for b in bones if not b["never_render"]]
        # Cube-less bones are transform-only parents; drop the ones nothing
        # references (cape/item attachment points, armor stubs).
        referenced = {b["parent"] for b in bones if b["parent"] is not None}
        bones = [b for b in bones if b["cubes"] or b["name"] in referenced]
        # A dropped bone can still be named as a parent (e.g. zombie's
        # neverRender "waist"): re-root each survivor to its nearest kept
        # ancestor, or the root when the whole chain was dropped.
        kept = {b["name"] for b in bones}
        for b in bones:
            p = b["parent"]
            while p is not None and p not in kept:
                p = all_bones[p]["parent"] if p in all_bones else None
            b["parent"] = p
        for b in bones:
            if b["name"] in e["rot_overrides"]:
                b["rot"] = e["rot_overrides"][b["name"]]
        for b in bones:
            if b["parent"] is not None and not any(p["name"] == b["parent"] for p in bones):
                fail("%s: bone %s has unknown parent %s" % (e["name"], b["name"], b["parent"]))
        # bone_world[i] composes bone_world[parent] in index order, so every
        # parent must come first (.custom lists body before its waist).
        ordered = []
        remaining = list(bones)
        while remaining:
            emitted = {b["name"] for b in ordered}
            ready = [b for b in remaining if b["parent"] is None or b["parent"] in emitted]
            if not ready:
                fail("%s: bone parent cycle at %s" % (e["name"], remaining[0]["name"]))
            ordered.append(ready[0])
            remaining.remove(ready[0])
        models.append({"name": e["name"], "tw": geo["tw"], "th": geo["th"], "bones": ordered})
        for s in e["skins"]:
            base = os.path.splitext(os.path.basename(s))[0]
            skins.append(e["name"] if base == e["name"] else "%s_%s" % (e["name"], base))

    # --- bitmaps/mobs.png: 64x64 cell per skin, manifest order from the top.
    atlas = Image.new("RGBA", (LAYER_SIZE, LAYER_SIZE * len(skins)), (0, 0, 0, 0))
    for i, e in enumerate(entries):
        for s in e["skins"]:
            skin_path = os.path.join(BEDROCK_SAMPLES, s)
            if not os.path.isfile(skin_path):
                fail("missing skin %s" % skin_path)
            img = Image.open(skin_path).convert("RGBA")
            if img.width > LAYER_SIZE or img.height > LAYER_SIZE:
                fail("skin %s is %dx%d, max is 64x64" % (s, img.width, img.height))
            # Bedrock renders entity skins with an opaque material: nonzero
            # alpha bytes are authoring layer markers (sheep.tga stores its
            # undercoat/face texels as a=3), not real transparency. Promote
            # them to opaque — the renderer's a==0 cutout keeps true holes.
            img.putalpha(img.getchannel("A").point(lambda a: 255 if a else 0))
            base = os.path.splitext(os.path.basename(s))[0]
            skin_name = e["name"] if base == e["name"] else "%s_%s" % (e["name"], base)
            atlas.paste(img, (0, skins.index(skin_name) * LAYER_SIZE))
    atlas.save(OUT_PNG)

    # --- include/common/mob_models_gen.hpp
    out = []
    out.append("#pragma once")
    out.append("")
    out.append("// Generated by tools/mob_import.py from tools/mobs_manifest.txt - do not edit.")
    out.append("// Re-run `make mobs-import` after changing the manifest.")
    out.append("")
    out.append("typedef struct {")
    out.append("    const char *name;")
    out.append("    int parent;          // bone index, -1 for a root bone")
    out.append("    float pivot[ 3 ];    // model-space rotation pivot, in 1/16-block pixels")
    out.append("    float rest_rot[ 3 ]; // rest-pose rotation, degrees (x, y, z)")
    out.append("    int mirror;          // bone mirrors its cube UVs")
    out.append("    int cube_start;      // first cube in the model's MobCubeDef array")
    out.append("    int cube_count;")
    out.append("} MobBoneDef;")
    out.append("")
    out.append("typedef struct {")
    out.append("    float origin[ 3 ]; // min corner, 1/16-block pixels")
    out.append("    float size[ 3 ];   // box size (w, h, d), 1/16-block pixels")
    out.append("    float uv[ 2 ];     // box-uv corner in skin pixels")
    out.append("    float inflate;")
    out.append("    int mirror;")
    out.append("} MobCubeDef;")
    out.append("")
    out.append("typedef struct {")
    out.append("    const MobBoneDef *bones;")
    out.append("    const MobCubeDef *cubes;")
    out.append("    int bone_count;")
    out.append("    int cube_count;")
    out.append("    int tex_w; // skin pixel size the uv coords refer to")
    out.append("    int tex_h;")
    out.append("} MobModelDef;")
    out.append("")
    model_ids = []
    skin_ids = []
    for m in models:
        enum_name = "MOB_MODEL_" + m["name"].upper()
        model_ids.append(enum_name)
    for i, e in enumerate(entries):
        for s in e["skins"]:
            base = os.path.splitext(os.path.basename(s))[0]
            skin_ids.append("MOB_SKIN_" + (e["name"] if base == e["name"] else "%s_%s" % (e["name"], base)).upper())
    out.append("enum MobModel { %s, MOB_MODEL_COUNT };" % ", ".join(model_ids))
    out.append("enum MobSkin { %s, MOB_SKIN_COUNT };" % ", ".join(skin_ids))
    out.append("")
    for m in models:
        name = m["name"]
        cubes = []
        for b in m["bones"]:
            for c in b["cubes"]:
                cubes.append(c)
        out.append("static const MobCubeDef mob_cubes_%s[] = {" % name)
        for c in cubes:
            out.append(
                "    { %s, %s, %s, %s, %d },"
                % (fmt_vec(c["origin"]), fmt_vec(c["size"]), fmt_vec(c["uv"]), fmt_f(c["inflate"]), 1 if c["mirror"] else 0)
            )
        out.append("};")
        name_index = {b["name"]: i for i, b in enumerate(m["bones"])}
        out.append("static const MobBoneDef mob_bones_%s[] = {" % name)
        start = 0
        for b in m["bones"]:
            parent = name_index[b["parent"]] if b["parent"] is not None else -1
            out.append(
                "    { \"%s\", %d, %s, %s, %d, %d, %d },"
                % (b["name"], parent, fmt_vec(b["pivot"]), fmt_vec(b["rot"]), 1 if b["mirror"] else 0, start, len(b["cubes"]))
            )
            start += len(b["cubes"])
        out.append("};")
        out.append("")
    out.append("static const MobModelDef mob_models[ MOB_MODEL_COUNT ] = {")
    for m in models:
        out.append(
            "    { mob_bones_%s, mob_cubes_%s, %d, %d, %d, %d },"
            % (m["name"], m["name"], len(m["bones"]), sum(len(b["cubes"]) for b in m["bones"]), m["tw"], m["th"])
        )
    out.append("};")
    out.append("")
    with open(GEN_HPP, "w") as f:
        f.write("\n".join(out))

    # --- self-check (pig model counts; player layout; sheep a:b inheritance,
    # print only).
    for m in models:
        seen = set()
        for b in m["bones"]:
            assert b["parent"] is None or b["parent"] in seen, (
                "%s: parent %s must precede %s" % (m["name"], b["parent"], b["name"])
            )
            seen.add(b["name"])
    pig = next(m for m in models if m["name"] == "pig")
    assert len(pig["bones"]) == 6, "pig must have 6 bones"
    assert sum(len(b["cubes"]) for b in pig["bones"]) == 7, "pig must have 7 cubes"
    head = next(b for b in pig["bones"] if b["name"] == "head")
    assert len(head["cubes"]) == 2 and head["cubes"][1]["size"] == [4.0, 3.0, 1.0], "snout must be the head's second cube"
    player = next((m for m in models if m["name"] == "player"), None)
    if player is not None:
        names = {b["name"] for b in player["bones"]}
        for want in ("head", "body", "leftArm", "rightArm", "leftLeg", "rightLeg"):
            assert want in names, "player: missing bone %s" % want
        overlays = {"hat", "jacket", "leftSleeve", "rightSleeve", "leftPants", "rightPants"}
        assert overlays <= names, "player: missing overlay bones %s" % (overlays - names)
        assert sum(len(b["cubes"]) for b in player["bones"]) == 12, "player must have 12 cubes"
    sheep_path = os.path.join(BEDROCK_SAMPLES, "models/entity/sheep_v1.0.geo.json")
    if os.path.isfile(sheep_path):
        sheep = resolve_geometry(load_geometries(sheep_path), "geometry.sheep")
        wool = {b["name"]: b for b in sheep["bones"]}
        print(
            "sheep inheritance: geometry.sheep resolves to %d bones: %s"
            % (len(sheep["bones"]), ", ".join(sorted(wool)))
        )
    print("mob_import: %d models, %d skins -> %s, %s" % (len(models), len(skins), GEN_HPP, OUT_PNG))


if __name__ == "__main__":
    main()
