"""Build the behaviour test level's static layer headless (mayapy) and export it (design doc 10.3).

    "D:/Autodesk/Maya2026/bin/mayapy.exe" build_behavior_test_scene.py S:/Github/RAnimation/Assets/SkeletalAnimBehavior/level/behavior_test.static.usda

40 x 40 m, Y-up, centimetres in Maya (metres below). Forward of a character is -Z.

    BND_North/South/West/East   outer walls (boundary): the bouncer turns away from the east wall
    corridor (north-west)       COL_CorridorN / COL_CorridorS: 1.5 m wide along x in [-16, -6] at z = -15;
                                two NPCs walk towards each other (contact Begin / End)
    screen + pillars (south-west) COL_Screen 10 m wide at z = 8 hides the player from the watcher at (-10, 4)
                                while the player walks along z = 11; COL_Pillar_0..3 decoration
    zones (centre-south)        ZONE_Plaza 10 x 8 m around (0, 6), ZONE_Fountain 3 x 3 m nested inside
    patrol ring (south-east)    WP_patrol_0..5 on a 3 m circle around (10, 10)
    player route                WP_player_0..8 (the --behavior scripted player follows it)
    spawn                       SPAWN_player at (-17, 0, 11) facing +X
"""

import math
import os
import sys

import maya.standalone

maya.standalone.initialize(name="python")
import maya.cmds as cmds  # noqa: E402

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import export_static_layer  # noqa: E402
import tag_static_props  # noqa: E402

M = 100.0  # metres -> centimetres


def box(name, center, size, rotation_y=0.0):
    node = cmds.polyCube(name=name, width=size[0] * M, height=size[1] * M, depth=size[2] * M)[0]
    cmds.xform(node, translation=(center[0] * M, center[1] * M, center[2] * M), rotation=(0, rotation_y, 0), worldSpace=True)
    return node


def locator(name, position, rotation_y=0.0):
    node = cmds.spaceLocator(name=name)[0]
    cmds.xform(node, translation=(position[0] * M, position[1] * M, position[2] * M), rotation=(0, rotation_y, 0), worldSpace=True)
    return node


PLAYER_ROUTE = [
    (-1.0, 11.0),   # 0: out from behind the screen -> the watcher sees the player
    (0.0, 9.0),     # 1: into ZONE_Plaza
    (0.0, 6.0),     # 2: ZONE_Fountain (nested)
    (0.0, 3.0),     # 3: back in the plaza
    (0.0, 0.0),     # 4: out of every zone
    (3.0, -3.0),    # 5
    (3.0, -11.0),   # 6
    (0.0, -11.0),   # 7: north of the FOV array: only the north-facing lookout sees the player
    (6.5, -11.0),   # 8: 1.5 m in front of the greeter -> interaction request (U)
]


def build():
    cmds.file(new=True, force=True)
    cmds.loadPlugin("mayaUsdPlugin", quiet=True)
    if str(cmds.upAxis(query=True, axis=True)).lower() != "y":
        cmds.upAxis(axis="y")
    cmds.currentUnit(linear="cm")

    nodes = [cmds.polyPlane(name="Ground", width=40 * M, height=40 * M, subdivisionsX=1, subdivisionsY=1)[0]]
    # outer boundary
    nodes += [
        box("BND_North", (0, 1, -20), (40.5, 2, 0.5)),
        box("BND_South", (0, 1, 20), (40.5, 2, 0.5)),
        box("BND_West", (-20, 1, 0), (0.5, 2, 40.5)),
        box("BND_East", (20, 1, 0), (0.5, 2, 40.5)),
    ]
    # corridor (inside z in (-15.75, -14.25))
    nodes += [box("COL_CorridorN", (-11, 1, -15.9), (10, 2, 0.3)), box("COL_CorridorS", (-11, 1, -14.1), (10, 2, 0.3))]
    # sight screen and pillars
    nodes += [box("COL_Screen", (-10, 1, 8), (10, 2, 0.6))]
    for i, (x, z) in enumerate([(-16, 15), (-13, 16), (-7, 16), (-4, 15)]):
        nodes.append(box("COL_Pillar_{}".format(i), (x, 1, z), (0.6, 2, 0.6), 15 * i))
    # zones
    nodes += [box("ZONE_Plaza", (0, 1, 6), (10, 2, 8)), box("ZONE_Fountain", (0, 1, 6), (3, 2, 3))]
    # patrol ring
    for i in range(6):
        a = math.radians(60 * i)
        nodes.append(locator("WP_patrol_{}".format(i), (10 + 3 * math.cos(a), 0, 10 + 3 * math.sin(a))))
    # player route + spawn
    for i, (x, z) in enumerate(PLAYER_ROUTE):
        nodes.append(locator("WP_player_{}".format(i), (x, 0, z)))
    nodes.append(locator("SPAWN_player", (-17, 0, 11), -90))

    root = cmds.group(empty=True, name="Static")
    cmds.parent(nodes, root)
    # the player route markers are small so they read as such in the engine's debug view
    for i in range(len(PLAYER_ROUTE)):
        tag_static_props.tag_static_props(root, verbose=False)
        cmds.setAttr("|Static|WP_player_{}.ranim_radius".format(i), 0.3)
    tag_static_props.tag_static_props(root)
    return root


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("usage: mayapy build_behavior_test_scene.py <out.static.usda>")
        sys.exit(2)
    try:
        build()
        export_static_layer.export_static_layer(sys.argv[1])
    finally:
        maya.standalone.uninitialize()
