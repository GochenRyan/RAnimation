"""Export the ``Static`` group of a SkeletalAnimBehavior test level as the level's static USD layer.

The export parameters are fixed (verified 2026-11-10 with Maya 2026 / mayaUSD 0.32):

    unit="m"                   metersPerUnit = 1. mayaUSD implements it as a 0.01 scale on the /Static root
                               prim; points and child translations stay in centimetres. The engine reads
                               every transform as the full local-to-world, so this is correct as long as
                               nothing reads a prim's own xformOps in isolation.
    shadingMode="none"         no initialShadingGroup material
    exportComponentTags=False  no per-face GeomSubsets from polyCube
    defaultMeshScheme="none"   polygonal meshes with authored normals (same as the FBX2USD pipeline)
    staticSingleSample=True    no time samples

The file goes next to the level (Assets/SkeletalAnimBehavior/level/<name>.static.usda); the engine-written
<name>.level.usda lists it in subLayers. Run tag_static_props first (this script warns about prefixed
nodes that are not tagged).

Usage in the Script Editor (Python tab):
    import sys; sys.path.append(r"S:/Github/RAnimation/Tools/Maya/BehaviorScene")
    import export_static_layer
    export_static_layer.export_static_layer(r"S:/Github/RAnimation/Assets/SkeletalAnimBehavior/level/my.static.usda")
"""

import json
import os

import maya.cmds as cmds

ROOT_GROUP = "Static"
JSON_ATTR = "USD_UserExportedAttributesJson"
TAGGED_PREFIXES = ("COL_", "BND_", "ZONE_", "WP_", "SPAWN_")

_LINEAR_TO_METRES = {"mm": 0.001, "cm": 0.01, "m": 1.0, "km": 1000.0, "in": 0.0254, "ft": 0.3048, "yd": 0.9144, "mi": 1609.344}


def _short_name(node):
    return node.split("|")[-1]


def _metres_per_scene_unit():
    return _LINEAR_TO_METRES.get(cmds.currentUnit(query=True, linear=True), 0.01)


def check_tags(root=ROOT_GROUP):
    """Prefixed nodes without an export JSON (they would be exported as plain geometry)."""
    missing = []
    for node in cmds.listRelatives(root, allDescendents=True, type="transform", fullPath=True) or []:
        name = _short_name(node)
        if name.startswith(TAGGED_PREFIXES) and not cmds.attributeQuery(JSON_ATTR, node=node, exists=True):
            missing.append(node)
    return missing


def report(root=ROOT_GROUP):
    """Print every tagged node with its world position in metres (what the engine should show)."""
    scale = _metres_per_scene_unit()
    rows = []
    for node in sorted(cmds.listRelatives(root, allDescendents=True, type="transform", fullPath=True) or []):
        if not cmds.attributeQuery(JSON_ATTR, node=node, exists=True):
            continue
        mapping = json.loads(cmds.getAttr("{}.{}".format(node, JSON_ATTR)) or "{}")
        values = []
        for maya_attr, spec in sorted(mapping.items()):
            values.append("{}={}".format(spec.get("usdAttrName", maya_attr), cmds.getAttr("{}.{}".format(node, maya_attr))))
        p = cmds.xform(node, query=True, worldSpace=True, translation=True)
        rows.append((_short_name(node), [v * scale for v in p], values))
        print("export_static_layer: {:<28} world ({:7.3f}, {:7.3f}, {:7.3f}) m  {}".format(
            _short_name(node), p[0] * scale, p[1] * scale, p[2] * scale, " ".join(values)))
    return rows


def export_static_layer(path, root=ROOT_GROUP):
    if not cmds.objExists(root):
        raise RuntimeError("no '{}' group to export".format(root))
    if not cmds.pluginInfo("mayaUsdPlugin", query=True, loaded=True):
        cmds.loadPlugin("mayaUsdPlugin")
    if str(cmds.upAxis(query=True, axis=True)).lower() != "y":
        raise RuntimeError("the scene is not Y-up; the engine expects a Y-up Maya scene")

    for node in check_tags(root):
        cmds.warning("export_static_layer: '{}' looks like a gameplay node but is not tagged (run tag_static_props)".format(node))

    folder = os.path.dirname(path)
    if folder and not os.path.isdir(folder):
        os.makedirs(folder)

    selection = cmds.ls(selection=True, long=True)
    try:
        cmds.select(root, replace=True)
        cmds.mayaUSDExport(
            file=path.replace("\\", "/"),
            selection=True,
            staticSingleSample=True,
            defaultMeshScheme="none",
            exportVisibility=True,
            unit="m",
            shadingMode="none",
            exportComponentTags=False,
        )
    finally:
        if selection:
            cmds.select(selection, replace=True)
        else:
            cmds.select(clear=True)
    print("export_static_layer: wrote {}".format(path))
    report(root)
    return path
