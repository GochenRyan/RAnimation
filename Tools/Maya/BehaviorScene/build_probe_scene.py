"""Build the P0 probe static layer headless (mayapy) and export it.

    "D:/Autodesk/Maya2026/bin/mayapy.exe" build_probe_scene.py S:/Github/RAnimation/Assets/SkeletalAnimBehavior/level/p0_probe.static.usda

Contents (Maya default units: centimetres, Y-up):
    Ground          20 x 20 m plane, display only
    COL_Pillar      0.5 x 1 x 0.5 m cube at (3, 0.5, 2) m, rotated 30 deg about Y  -> the P0 acceptance object
    BND_North       20 m wall along z = -10 m
    ZONE_Plaza      10 x 2 x 10 m volume around the origin (not rendered by the engine)
    ZONE_Corner     2 x 2 x 2 m volume inside the plaza (nested-zone case)
    WP_patrol_0..3  square of waypoints at +-4 m
    SPAWN_player    at (0, 0, 5) m facing -Z
"""

import os
import sys

import maya.standalone

maya.standalone.initialize(name="python")
import maya.cmds as cmds  # noqa: E402

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import export_static_layer  # noqa: E402
import tag_static_props  # noqa: E402


def cube(name, size, position, rotation=(0, 0, 0)):
    node = cmds.polyCube(name=name, width=size[0], height=size[1], depth=size[2])[0]
    cmds.xform(node, translation=position, rotation=rotation, worldSpace=True)
    return node


def locator(name, position, rotation=(0, 0, 0)):
    node = cmds.spaceLocator(name=name)[0]
    cmds.xform(node, translation=position, rotation=rotation, worldSpace=True)
    return node


def build():
    cmds.file(new=True, force=True)
    cmds.loadPlugin("mayaUsdPlugin", quiet=True)
    if str(cmds.upAxis(query=True, axis=True)).lower() != "y":
        cmds.upAxis(axis="y")
    cmds.currentUnit(linear="cm")

    nodes = [
        cmds.polyPlane(name="Ground", width=2000, height=2000, subdivisionsX=1, subdivisionsY=1)[0],
        cube("COL_Pillar", (50, 100, 50), (300, 50, 200), (0, 30, 0)),
        cube("BND_North", (2000, 200, 20), (0, 100, -1000)),
        cube("ZONE_Plaza", (1000, 200, 1000), (0, 100, 0)),
        cube("ZONE_Corner", (200, 200, 200), (-300, 100, -300)),
        locator("WP_patrol_0", (-400, 0, -400)),
        locator("WP_patrol_1", (400, 0, -400)),
        locator("WP_patrol_2", (400, 0, 400)),
        locator("WP_patrol_3", (-400, 0, 400)),
        locator("SPAWN_player", (0, 0, 500)),
    ]
    root = cmds.group(empty=True, name="Static")
    cmds.parent(nodes, root)

    tag_static_props.tag_static_props(root)
    # a hand-tuned value that re-tagging must keep
    cmds.setAttr("|Static|WP_patrol_0.ranim_radius", 0.75)
    tag_static_props.tag_static_props(root, verbose=False)
    return root


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("usage: mayapy build_probe_scene.py <out.static.usda>")
        sys.exit(2)
    try:
        build()
        export_static_layer.export_static_layer(sys.argv[1])
    finally:
        maya.standalone.uninitialize()
