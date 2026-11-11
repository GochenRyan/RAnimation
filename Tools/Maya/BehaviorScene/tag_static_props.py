"""Tag the static content of a SkeletalAnimBehavior test level for USD export (Maya 2026 / mayaUSD 0.32).

Everything under the top-level group ``Static`` is classified by its node-name prefix and gets ``ranim_*``
attributes plus the ``USD_UserExportedAttributesJson`` string that tells mayaUSDExport to write them as
``ranim:*`` USD attributes (Maya attribute names cannot contain ':'; attributes not listed in that JSON are
not exported at all, which keeps Maya's own bookkeeping out of the file).

    COL_<name>               polyCube   ranim:collider = "box", ranim:collisionTag = "obstacle"
    BND_<name>               polyCube   ranim:collider = "box", ranim:collisionTag = "boundary"
    ZONE_<name>              polyCube   ranim:marker = "zone"      (not rendered by the engine)
    WP_<route>_<index>       locator    ranim:marker = "waypoint"  (+ ranim:radius, metres)
    SPAWN_<name>             locator    ranim:marker = "spawn"     (forward = the locator's local -Z)

Existing ranim_* values are kept, so hand-tuned numbers survive re-tagging. Lengths authored as attributes
(ranim_radius) are METRES and are not scaled by the export's unit conversion; positions and box sizes come
from the transforms and the geometry.

Usage in the Script Editor (Python tab):
    import sys; sys.path.append(r"S:/Github/RAnimation/Tools/Maya/BehaviorScene")
    import tag_static_props; tag_static_props.tag_static_props()
"""

import json

import maya.cmds as cmds

ROOT_GROUP = "Static"
JSON_ATTR = "USD_UserExportedAttributesJson"

# prefix -> attributes to add when missing: name -> (kind, value); kind is "string" | "double" | "long" | "bool"
PREFIX_RULES = (
    ("COL_", {"ranim_collider": ("string", "box"), "ranim_collisionTag": ("string", "obstacle")}),
    ("BND_", {"ranim_collider": ("string", "box"), "ranim_collisionTag": ("string", "boundary")}),
    ("ZONE_", {"ranim_marker": ("string", "zone")}),
    ("WP_", {"ranim_marker": ("string", "waypoint"), "ranim_radius": ("double", 0.5)}),
    ("SPAWN_", {"ranim_marker": ("string", "spawn")}),
)


def _short_name(node):
    return node.split("|")[-1]


def _add_attr(node, name, kind, value):
    if cmds.attributeQuery(name, node=node, exists=True):
        return False
    if kind == "string":
        cmds.addAttr(node, longName=name, dataType="string")
        cmds.setAttr("{}.{}".format(node, name), value, type="string")
    elif kind == "double":
        cmds.addAttr(node, longName=name, attributeType="double")
        cmds.setAttr("{}.{}".format(node, name), value)
    elif kind == "long":
        cmds.addAttr(node, longName=name, attributeType="long")
        cmds.setAttr("{}.{}".format(node, name), value)
    elif kind == "bool":
        cmds.addAttr(node, longName=name, attributeType="bool")
        cmds.setAttr("{}.{}".format(node, name), value)
    else:
        raise ValueError("unknown attribute kind " + kind)
    return True


def _rewrite_export_json(node):
    """List every user-defined ranim_* attribute of the node in USD_UserExportedAttributesJson."""
    mapping = {}
    for attr in cmds.listAttr(node, userDefined=True) or []:
        if attr.startswith("ranim_"):
            mapping[attr] = {
                "usdAttrName": "ranim:" + attr[len("ranim_"):],
                "translateMayaDoubleToUsdSinglePrecision": True,
            }
    if not mapping:
        if cmds.attributeQuery(JSON_ATTR, node=node, exists=True):
            cmds.deleteAttr("{}.{}".format(node, JSON_ATTR))
        return 0
    if not cmds.attributeQuery(JSON_ATTR, node=node, exists=True):
        cmds.addAttr(node, longName=JSON_ATTR, dataType="string")
    cmds.setAttr("{}.{}".format(node, JSON_ATTR), json.dumps(mapping, sort_keys=True), type="string")
    return len(mapping)


def tag_static_props(root=ROOT_GROUP, verbose=True):
    """Add missing ranim_* attributes by name prefix and refresh the export JSON on every node under root.

    Returns a list of (node, [added attribute names]) for the nodes that carry ranim_* attributes."""
    if not cmds.objExists(root):
        raise RuntimeError("no '{}' group: put the level's static content under a top-level group named {}".format(root, root))

    report = []
    transforms = cmds.listRelatives(root, allDescendents=True, type="transform", fullPath=True) or []
    for node in sorted(transforms):
        name = _short_name(node)
        added = []
        for prefix, attrs in PREFIX_RULES:
            if name.startswith(prefix):
                for attr_name, (kind, value) in attrs.items():
                    if _add_attr(node, attr_name, kind, value):
                        added.append(attr_name)
                break
        if _rewrite_export_json(node):
            report.append((node, added))
            if verbose:
                print("tag_static_props: {:<40} {}".format(name, ("added " + ", ".join(added)) if added else "up to date"))
    if verbose:
        print("tag_static_props: {} tagged node(s) under {}".format(len(report), root))
    return report


if __name__ == "__main__":
    tag_static_props()
