# publish_fbx_to_usd.py

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
from typing import Any, Callable, Dict, List, Optional, Tuple

import maya.cmds as cmds
import maya.mel as mel


# Canonical output space contract: every published layer is Y-up, right-handed, METRES
# (metersPerUnit = 1.0, values themselves in metres), rightHanded winding. Up-axis is enforced at FBX
# import (Maya prefs forced to Y + FBX axis conversion); unit conversion happens as a deterministic
# USD-side pass (canonicalize_usd_layer) because mayaUSDExport always writes Maya-internal centimetre
# values regardless of UI unit. _STAGE_METRICS therefore always holds the canonical values; the Maya
# session units are only printed for the audit trail.
_STAGE_METRICS: Dict[str, Any] = {"metersPerUnit": None, "upAxis": None}

_LINEAR_UNIT_TO_METERS = {
    "mm": 0.001,
    "cm": 0.01,
    "m": 1.0,
    "km": 1000.0,
    "in": 0.0254,
    "ft": 0.3048,
    "yd": 0.9144,
}


def _capture_stage_metrics() -> None:
    """Force canonical stage metrics ({1.0, Y}); print what Maya reports for the audit trail."""
    try:
        linear = cmds.currentUnit(query=True, linear=True)
        up = cmds.upAxis(query=True, axis=True)
    except Exception as exc:
        linear, up = "?", "?"
        print("Warning: could not query Maya units:", exc)

    _STAGE_METRICS["metersPerUnit"] = 1.0
    _STAGE_METRICS["upAxis"] = "Y"
    print("Stage metrics: canonical metersPerUnit=1.0 upAxis=Y (Maya session reported linear='{}' up='{}')".format(
        linear, up))


def _apply_stage_metrics(stage) -> None:
    """Author the captured metersPerUnit / upAxis onto a stage (no-op if not captured)."""
    from pxr import UsdGeom

    if _STAGE_METRICS["metersPerUnit"] is not None:
        UsdGeom.SetStageMetersPerUnit(stage, float(_STAGE_METRICS["metersPerUnit"]))
    if _STAGE_METRICS["upAxis"] is not None:
        UsdGeom.SetStageUpAxis(stage, _STAGE_METRICS["upAxis"])


def canonicalize_usd_layer(usd_path: str) -> None:
    """Convert a freshly exported layer to canonical space: values in METRES, metersPerUnit = 1.0.

    mayaUSDExport writes Maya-internal centimetre values and stamps the layer's metersPerUnit
    accordingly, so the layer is self-describing: multiplying every linear value by the layer's own
    declared metersPerUnit yields metres regardless of what the Maya session reported. Rotations,
    scales and normals (directions) are untouched; matrices keep their linear part and scale only the
    translation (uniform-scale conjugation S*M*S^-1). Idempotent: a layer already at 1.0 is a no-op.
    """
    from pxr import Gf, Usd, UsdGeom, Vt

    stage = _open_stage(usd_path)

    up_axis = UsdGeom.GetStageUpAxis(stage)
    if str(up_axis) != "Y":
        raise RuntimeError(
            "{}: exported with upAxis={} - unexpected, the FBX import path forces Y-up".format(usd_path, up_axis))

    scale = float(UsdGeom.GetStageMetersPerUnit(stage))
    if scale <= 0.0:
        scale = 0.01  # USD fallback
    if abs(scale - 1.0) > 1e-9:
        def scale_vec_attr(attr) -> None:
            """Scale a vec3-array or vec3 attribute (default value + every time sample)."""
            def scaled(value):
                if value is None:
                    return None
                if isinstance(value, (Vt.Vec3fArray, Vt.Vec3dArray, Vt.Vec3hArray)):
                    return type(value)([v * scale for v in value])
                return value * scale

            times = attr.GetTimeSamples()
            if times:
                for t in times:
                    attr.Set(scaled(attr.Get(t)), t)
            if attr.HasAuthoredValueOpinion() and attr.Get() is not None:
                attr.Set(scaled(attr.Get()))

        def scale_matrix_translation(matrix):
            matrix = Gf.Matrix4d(matrix)
            matrix.SetTranslateOnly(matrix.ExtractTranslation() * scale)
            return matrix

        def scale_matrix_attr(attr) -> None:
            """Scale the translation of a Matrix4d / Matrix4dArray attribute (default + samples)."""
            def scaled(value):
                if value is None:
                    return None
                if isinstance(value, Vt.Matrix4dArray):
                    return Vt.Matrix4dArray([scale_matrix_translation(m) for m in value])
                return scale_matrix_translation(value)

            times = attr.GetTimeSamples()
            if times:
                for t in times:
                    attr.Set(scaled(attr.Get(t)), t)
            if attr.HasAuthoredValueOpinion() and attr.Get() is not None:
                attr.Set(scaled(attr.Get()))

        for prim in stage.Traverse():
            type_name = str(prim.GetTypeName())

            for attr in prim.GetAttributes():
                name = attr.GetName()

                if name.startswith("xformOp:translate"):
                    scale_vec_attr(attr)
                elif name.endswith("skel:geomBindTransform"):
                    scale_matrix_attr(attr)
                elif type_name == "Mesh" and name in ("points", "extent"):
                    scale_vec_attr(attr)
                elif type_name == "Skeleton" and name in ("bindTransforms", "restTransforms"):
                    scale_matrix_attr(attr)
                elif type_name == "SkelAnimation" and name == "translations":
                    scale_vec_attr(attr)

    UsdGeom.SetStageMetersPerUnit(stage, 1.0)
    UsdGeom.SetStageUpAxis(stage, UsdGeom.Tokens.y)
    stage.GetRootLayer().Save()
    print("Canonicalized {} (linear scale applied: {})".format(usd_path, scale))


def _mel_path(path: str) -> str:
    """Convert a path to a MEL-safe path."""
    return path.replace("\\", "/").replace('"', '\\"')


def _safe_name(name: str) -> str:
    """Convert a display name to a safe USD/file name."""
    name = name.strip()
    name = re.sub(r"[^\w\-]+", "_", name)
    return name.strip("_") or "asset"


def _ensure_dir(path: str) -> None:
    """Create a directory if it does not exist."""
    if path and not os.path.exists(path):
        os.makedirs(path)


def _rel_path(path: str, base_dir: str) -> str:
    """Return a normalized relative path."""
    return os.path.relpath(path, base_dir).replace("\\", "/")


def _to_long_name(node: str) -> str:
    """Return the long DAG path for a Maya node."""
    matches = cmds.ls(node, long=True) or []
    if not matches:
        raise RuntimeError("Node does not exist: {}".format(node))
    return matches[0]


def _short_name(node: str) -> str:
    """Return the short DAG node name."""
    return node.split("|")[-1]


def _is_descendant(node: str, ancestor: str) -> bool:
    """Return whether node is under ancestor in the DAG."""
    node_long = _to_long_name(node)
    ancestor_long = _to_long_name(ancestor)
    return node_long.startswith(ancestor_long + "|")


def _asset_root_name(asset_name: str) -> str:
    """Return the stable root prim name for the asset."""
    return "{}_AssetRoot".format(_safe_name(asset_name))


def ensure_plugins() -> None:
    """Load required Maya plugins."""
    if not cmds.pluginInfo("fbxmaya", query=True, loaded=True):
        cmds.loadPlugin("fbxmaya")

    if not cmds.pluginInfo("mayaUsdPlugin", query=True, loaded=True):
        cmds.loadPlugin("mayaUsdPlugin")


def get_fbx_takes(fbx_path: str) -> List[Dict[str, Any]]:
    """Return all FBX takes in the source file."""
    ensure_plugins()

    mel.eval('FBXRead -f "{}";'.format(_mel_path(fbx_path)))

    take_count = int(mel.eval("FBXGetTakeCount;"))
    takes: List[Dict[str, Any]] = []

    for index in range(1, take_count + 1):
        name = mel.eval("FBXGetTakeName {};".format(index))
        takes.append({
            "index": index,
            "name": name,
            "safe_name": _safe_name(name),
        })

    try:
        mel.eval("FBXClose;")
    except Exception:
        pass

    return takes


def import_fbx(fbx_path: str, take_index: Optional[int] = None) -> None:
    """Import an FBX file into a clean Maya scene, normalized to Y-up.

    This is the single choke point every export path goes through (the model export and each per-take
    anim export all re-import), so scene-level normalization lives here. Handedness needs no branch:
    Maya is right-handed and the FBX importer converts every source axis system into Maya's space, so a
    left-handed source cannot reach the USD export - the mirror conversion (winding flip, tangent-w
    rederivation) is intentionally not implemented.
    """
    cmds.file(new=True, force=True)
    ensure_plugins()

    # Canonical output is Y-up; force the session preference before import so the FBX axis conversion
    # below targets Y-up regardless of how the user's Maya is configured.
    if str(cmds.upAxis(query=True, axis=True)).lower() != "y":
        cmds.upAxis(ax="y")

    mel.eval("FBXImportShowUI -v false;")
    mel.eval("FBXImportAxisConversionEnable -v true;")
    mel.eval("FBXImportMode -v add;")
    mel.eval("FBXImportSkins -v true;")
    mel.eval("FBXImportShapes -v true;")
    mel.eval("FBXImportCameras -v false;")
    mel.eval("FBXImportLights -v false;")
    mel.eval("FBXImportFillTimeline -v true;")
    mel.eval("FBXImportSetMayaFrameRate -v true;")

    if take_index is None:
        mel.eval('FBXImport -f "{}";'.format(_mel_path(fbx_path)))
    else:
        mel.eval('FBXImport -f "{}" -t {};'.format(_mel_path(fbx_path), int(take_index)))


def _count_descendant_joints(joint: str) -> int:
    """Count descendant joints under a joint."""
    descendants = cmds.listRelatives(
        joint,
        allDescendents=True,
        type="joint",
        fullPath=False
    ) or []
    return len(descendants)


def _count_keyframes_under(root: str) -> int:
    """Count keyframes under a hierarchy."""
    nodes = [root]
    nodes.extend(cmds.listRelatives(root, allDescendents=True, fullPath=False) or [])

    count = 0
    for node in nodes:
        times = cmds.keyframe(node, query=True, timeChange=True) or []
        count += len(times)

    return count


def find_root_joint(preferred_root_joint: Optional[str] = None) -> str:
    """Find the most likely skeleton root joint."""
    if preferred_root_joint:
        if cmds.objExists(preferred_root_joint) and cmds.nodeType(preferred_root_joint) == "joint":
            return _to_long_name(preferred_root_joint)
        raise RuntimeError("Preferred root joint is not a joint: {}".format(preferred_root_joint))

    joints = cmds.ls(type="joint", long=True) or []
    root_joints = []

    for joint in joints:
        parents = cmds.listRelatives(joint, parent=True, fullPath=True) or []

        if not parents:
            root_joints.append(joint)
            continue

        parent = parents[0]
        if cmds.nodeType(parent) != "joint":
            root_joints.append(joint)

    if not root_joints:
        raise RuntimeError("No root joint found.")

    def score(joint: str) -> Tuple[int, int, int]:
        name = _short_name(joint).lower()
        name_score = 1 if name in ("root", "rootjoint", "skeleton", "hips") else 0
        joint_count = _count_descendant_joints(joint)
        key_count = _count_keyframes_under(joint)
        return name_score, joint_count, key_count

    root_joints = sorted(root_joints, key=score, reverse=True)

    if len(root_joints) > 1:
        print("Warning: multiple root joints found. Use:", root_joints[0])
        print("Candidates:", root_joints)

    return root_joints[0]


def find_skeleton_container(root_joint: str) -> str:
    """Find the top transform container above the root joint."""
    root_joint = _to_long_name(root_joint)

    current = root_joint
    result = root_joint

    while True:
        parents = cmds.listRelatives(current, parent=True, fullPath=True) or []
        if not parents:
            break

        parent = parents[0]
        result = parent
        current = parent

    return result


def _top_world_ancestor(node: str) -> str:
    """Return the highest DAG ancestor under world."""
    current = _to_long_name(node)

    while True:
        parents = cmds.listRelatives(current, parent=True, fullPath=True) or []
        if not parents:
            return current
        current = parents[0]


def find_mesh_roots(preferred_mesh_root: Optional[str] = None) -> List[str]:
    """Find top-level mesh roots, preferably from skinned meshes."""
    if preferred_mesh_root:
        if cmds.objExists(preferred_mesh_root):
            return [_to_long_name(preferred_mesh_root)]
        raise RuntimeError("Preferred mesh root does not exist: {}".format(preferred_mesh_root))

    mesh_roots = set()

    skin_clusters = cmds.ls(type="skinCluster") or []
    for skin in skin_clusters:
        geometry = cmds.skinCluster(skin, query=True, geometry=True) or []

        for geo in geometry:
            if not cmds.objExists(geo):
                continue

            node_type = cmds.nodeType(geo)

            if node_type == "mesh":
                parents = cmds.listRelatives(geo, parent=True, fullPath=True) or []
                if parents:
                    mesh_roots.add(_top_world_ancestor(parents[0]))
            else:
                mesh_roots.add(_top_world_ancestor(geo))

    if not mesh_roots:
        mesh_shapes = cmds.ls(type="mesh", long=True) or []

        for shape in mesh_shapes:
            if cmds.getAttr(shape + ".intermediateObject"):
                continue

            parents = cmds.listRelatives(shape, parent=True, fullPath=True) or []
            if parents:
                mesh_roots.add(_top_world_ancestor(parents[0]))

    if not mesh_roots:
        raise RuntimeError("No mesh root found.")

    return sorted(mesh_roots)


def get_hierarchy_nodes(root: str) -> List[str]:
    """Return all descendants under a root node."""
    root = _to_long_name(root)
    nodes = [root]
    nodes.extend(cmds.listRelatives(root, allDescendents=True, fullPath=True) or [])
    return nodes


def get_key_range(root_joint: str) -> Optional[Tuple[int, int]]:
    """Get min and max keyframe range from the skeleton hierarchy."""
    key_times = []

    for node in get_hierarchy_nodes(root_joint):
        times = cmds.keyframe(node, query=True, timeChange=True) or []
        key_times.extend(times)

    if not key_times:
        return None

    return int(min(key_times)), int(max(key_times))


def get_fallback_playback_range() -> Tuple[int, int]:
    """Get the Maya playback range as fallback animation range."""
    start = int(cmds.playbackOptions(query=True, minTime=True))
    end = int(cmds.playbackOptions(query=True, maxTime=True))
    return start, end


def _filter_export_roots(nodes: List[str]) -> List[str]:
    """Remove nodes that are already covered by an ancestor export root."""
    long_nodes = [_to_long_name(node) for node in nodes]
    result = []

    for node in long_nodes:
        covered = False

        for other in long_nodes:
            if node == other:
                continue
            if _is_descendant(node, other):
                covered = True
                break

        if not covered:
            result.append(node)

    return result


def _print_detected_scene(root_joint: str, skeleton_container: str, mesh_roots: List[str]) -> None:
    """Print detected scene roots."""
    print("Detected root joint:", root_joint)
    print("Detected skeleton container:", skeleton_container)
    print("Detected mesh roots:")
    for mesh_root in mesh_roots:
        print("  ", mesh_root)


def _create_temp_export_root(
    export_root_name: str,
    export_roots: List[str],
) -> Tuple[str, Dict[str, Optional[str]], Dict[str, str]]:
    """Create a temporary common parent for USD export."""
    export_roots = _filter_export_roots(export_roots)

    if cmds.objExists(export_root_name):
        cmds.delete(export_root_name)

    export_root = cmds.createNode("transform", name=export_root_name)

    old_parents: Dict[str, Optional[str]] = {}
    new_paths: Dict[str, str] = {}

    for node in export_roots:
        node = _to_long_name(node)

        parents = cmds.listRelatives(node, parent=True, fullPath=True) or []
        old_parents[node] = parents[0] if parents else None

        new_node = cmds.parent(node, export_root, absolute=True)[0]
        new_paths[node] = _to_long_name(new_node)

    return export_root, old_parents, new_paths


def _restore_temp_export_root(
    export_root: str,
    old_parents: Dict[str, Optional[str]],
    new_paths: Dict[str, str],
) -> None:
    """Restore the original Maya hierarchy after export."""
    for old_node, new_node in list(new_paths.items()):
        if not cmds.objExists(new_node):
            continue

        old_parent = old_parents.get(old_node)

        if old_parent and cmds.objExists(old_parent):
            restored = cmds.parent(new_node, old_parent, absolute=True)[0]
        else:
            restored = cmds.parent(new_node, world=True, absolute=True)[0]

        print("Restored:", restored)

    if cmds.objExists(export_root):
        cmds.delete(export_root)


def _with_temp_export_root(
    export_root_name: str,
    export_roots: List[str],
    callback: Callable[[str], None],
) -> None:
    """Run a callback with a temporary USD export root."""
    export_root, old_parents, new_paths = _create_temp_export_root(
        export_root_name=export_root_name,
        export_roots=export_roots,
    )

    try:
        callback(export_root)
    finally:
        _restore_temp_export_root(
            export_root=export_root,
            old_parents=old_parents,
            new_paths=new_paths,
        )


def export_full_model_usd(
    output_path: str,
    export_root_name: str,
    skeleton_container: str,
    mesh_roots: List[str],
) -> None:
    """Export a full static model USD used as source for layer splitting."""
    _ensure_dir(os.path.dirname(output_path))

    export_roots = [skeleton_container] + mesh_roots

    def do_export(temp_root: str) -> None:
        cmds.select(temp_root, replace=True)

        cmds.mayaUSDExport(
            file=output_path,
            selection=True,
            staticSingleSample=True,
            exportSkels="auto",
            exportSkin="auto",
            exportVisibility=True,
            eulerFilter=True,
            # Export as polygonal (not catmullClark) so USD stores explicit mesh normals. Without this the
            # mesh is a subdivision surface with no authored 'normals', and a poly renderer shades it black.
            defaultMeshScheme="none",
        )

    _with_temp_export_root(
        export_root_name=export_root_name,
        export_roots=export_roots,
        callback=do_export,
    )


def export_anim_usd(
    output_path: str,
    export_root_name: str,
    skeleton_container: str,
    start_frame: int,
    end_frame: int,
) -> None:
    """Export animation-only USD with the same asset root path as model layers."""
    _ensure_dir(os.path.dirname(output_path))

    def do_export(temp_root: str) -> None:
        cmds.select(temp_root, replace=True)

        cmds.mayaUSDExport(
            file=output_path,
            selection=True,
            frameRange=(float(start_frame), float(end_frame)),
            frameStride=1.0,
            staticSingleSample=False,
            exportSkels="auto",
            exportSkin="none",
            exportVisibility=True,
            eulerFilter=True,
        )

    _with_temp_export_root(
        export_root_name=export_root_name,
        export_roots=[skeleton_container],
        callback=do_export,
    )


def _copy_usd_file(src_path: str, dst_path: str) -> None:
    """Copy a USD file."""
    _ensure_dir(os.path.dirname(dst_path))
    shutil.copyfile(src_path, dst_path)


def _open_stage(usd_path: str):
    """Open a USD stage."""
    from pxr import Usd

    stage = Usd.Stage.Open(usd_path)
    if not stage:
        raise RuntimeError("Failed to open USD file: {}".format(usd_path))
    return stage


def _remove_prim_paths(stage, paths) -> None:
    """Remove prims by path, deeper paths first."""
    sorted_paths = sorted(paths, key=lambda p: len(str(p)), reverse=True)

    for path in sorted_paths:
        if stage.GetPrimAtPath(path):
            stage.RemovePrim(path)


def _remove_prims_by_type_or_name(
    usd_path: str,
    type_names: Optional[set] = None,
    names: Optional[set] = None,
) -> None:
    """Remove prims matching type names or prim names."""
    stage = _open_stage(usd_path)

    type_names = type_names or set()
    names = names or set()

    remove_paths = []

    for prim in stage.Traverse():
        if prim.GetTypeName() in type_names or prim.GetName() in names:
            remove_paths.append(prim.GetPath())

    _remove_prim_paths(stage, remove_paths)
    stage.GetRootLayer().Save()


def _is_skinning_property_name(name: str) -> bool:
    """Return whether a USD property belongs to skinning or skeleton binding."""
    return (
        name.startswith("skel:")
        or name.startswith("primvars:skel:")
        or ":skel:" in name
        or name == "blendShapes"
        or name.startswith("blendShapes:")
    )


def _is_material_property_name(name: str) -> bool:
    """Return whether a USD property belongs to material binding."""
    return name.startswith("material:binding")


def _copy_metadata(src_prim, dst_prim, copy_api_schemas: bool = True) -> None:
    """Copy useful prim metadata."""
    keys = ["kind", "instanceable"]

    if copy_api_schemas:
        keys.append("apiSchemas")

    for key in keys:
        if src_prim.HasAuthoredMetadata(key):
            dst_prim.SetMetadata(key, src_prim.GetMetadata(key))


def _copy_attribute(src_attr, dst_prim) -> None:
    """Copy a USD attribute to another prim."""
    dst_attr = dst_prim.CreateAttribute(
        src_attr.GetName(),
        src_attr.GetTypeName(),
        custom=src_attr.IsCustom(),
        variability=src_attr.GetVariability(),
    )

    if src_attr.HasAuthoredValueOpinion():
        samples = src_attr.GetTimeSamples()

        if samples:
            for time_code in samples:
                dst_attr.Set(src_attr.Get(time_code), time_code)
        else:
            dst_attr.Set(src_attr.Get())

    metadata = src_attr.GetAllMetadata()
    for key, value in metadata.items():
        try:
            dst_attr.SetMetadata(key, value)
        except Exception:
            pass


def _copy_relationship(src_rel, dst_prim) -> None:
    """Copy a USD relationship to another prim."""
    dst_rel = dst_prim.CreateRelationship(
        src_rel.GetName(),
        custom=src_rel.IsCustom(),
    )

    targets = src_rel.GetTargets()
    if targets:
        dst_rel.SetTargets(targets)

    metadata = src_rel.GetAllMetadata()
    for key, value in metadata.items():
        try:
            dst_rel.SetMetadata(key, value)
        except Exception:
            pass


def _copy_property(src_prop, dst_prim) -> None:
    """Copy a USD property to another prim."""
    from pxr import Usd

    if isinstance(src_prop, Usd.Attribute):
        _copy_attribute(src_prop, dst_prim)
        return

    if isinstance(src_prop, Usd.Relationship):
        _copy_relationship(src_prop, dst_prim)
        return

    print("Warning: unsupported USD property type:", src_prop)


def _copy_prim_subtree(src_prim, dst_stage) -> None:
    """Copy a prim subtree by recreating prims and properties."""
    dst_prim = dst_stage.DefinePrim(src_prim.GetPath(), src_prim.GetTypeName())
    _copy_metadata(src_prim, dst_prim, copy_api_schemas=True)

    for attr in src_prim.GetAttributes():
        _copy_attribute(attr, dst_prim)

    for rel in src_prim.GetRelationships():
        _copy_relationship(rel, dst_prim)

    for child in src_prim.GetChildren():
        _copy_prim_subtree(child, dst_stage)


def _copy_filtered_properties_to_over_layer(
    src_usd_path: str,
    dst_usd_path: str,
    predicate,
    start_time: Optional[float] = None,
    end_time: Optional[float] = None,
    fps: Optional[float] = None,
) -> None:
    """Create an overlay USD layer containing only selected properties."""
    from pxr import Usd

    if os.path.exists(dst_usd_path):
        os.remove(dst_usd_path)

    src_stage = _open_stage(src_usd_path)
    dst_stage = Usd.Stage.CreateNew(dst_usd_path)

    if start_time is not None:
        dst_stage.SetStartTimeCode(start_time)
    if end_time is not None:
        dst_stage.SetEndTimeCode(end_time)
    if fps is not None:
        dst_stage.SetFramesPerSecond(fps)
        dst_stage.SetTimeCodesPerSecond(fps)

    default_prim = src_stage.GetDefaultPrim()
    if default_prim:
        dst_root = dst_stage.OverridePrim(default_prim.GetPath())
        dst_stage.SetDefaultPrim(dst_root)

    for src_prim in src_stage.Traverse():
        selected_props = [prop for prop in src_prim.GetProperties() if predicate(src_prim, prop)]

        if not selected_props:
            continue

        dst_prim = dst_stage.OverridePrim(src_prim.GetPath())
        _copy_metadata(src_prim, dst_prim, copy_api_schemas=True)

        for prop in selected_props:
            _copy_property(prop, dst_prim)

    dst_stage.GetRootLayer().Save()


def make_skeleton_layer(full_model_usd: str, skeleton_usd: str) -> None:
    """Create skeleton.usda from the full static model USD."""
    from pxr import Usd

    if os.path.exists(skeleton_usd):
        os.remove(skeleton_usd)

    src_stage = _open_stage(full_model_usd)
    dst_stage = Usd.Stage.CreateNew(skeleton_usd)

    dst_stage.SetStartTimeCode(src_stage.GetStartTimeCode())
    dst_stage.SetEndTimeCode(src_stage.GetEndTimeCode())
    dst_stage.SetFramesPerSecond(src_stage.GetFramesPerSecond())
    dst_stage.SetTimeCodesPerSecond(src_stage.GetTimeCodesPerSecond())

    default_prim = src_stage.GetDefaultPrim()

    for src_prim in src_stage.Traverse():
        name = src_prim.GetName()
        type_name = src_prim.GetTypeName()

        if name == "mtl":
            continue

        if type_name in ("Mesh", "Material", "Shader", "SkelAnimation"):
            continue

        dst_prim = dst_stage.DefinePrim(src_prim.GetPath(), type_name)
        _copy_metadata(src_prim, dst_prim, copy_api_schemas=True)

        for prop in src_prim.GetProperties():
            prop_name = prop.GetName()

            if _is_skinning_property_name(prop_name):
                continue

            if _is_material_property_name(prop_name):
                continue

            _copy_property(prop, dst_prim)

    if default_prim:
        dst_default = dst_stage.GetPrimAtPath(default_prim.GetPath())
        if dst_default:
            dst_stage.SetDefaultPrim(dst_default)

    dst_stage.GetRootLayer().Save()


def make_geometry_layer(full_model_usd: str, geometry_usd: str) -> None:
    """Create geometry.usda from the full static model USD."""
    from pxr import Usd

    if os.path.exists(geometry_usd):
        os.remove(geometry_usd)

    src_stage = _open_stage(full_model_usd)
    dst_stage = Usd.Stage.CreateNew(geometry_usd)

    dst_stage.SetStartTimeCode(src_stage.GetStartTimeCode())
    dst_stage.SetEndTimeCode(src_stage.GetEndTimeCode())
    dst_stage.SetFramesPerSecond(src_stage.GetFramesPerSecond())
    dst_stage.SetTimeCodesPerSecond(src_stage.GetTimeCodesPerSecond())

    default_prim = src_stage.GetDefaultPrim()

    for src_prim in src_stage.Traverse():
        name = src_prim.GetName()
        type_name = src_prim.GetTypeName()

        if name == "mtl":
            continue

        if type_name in ("Skeleton", "SkelAnimation", "Material", "Shader"):
            continue

        dst_prim = dst_stage.DefinePrim(src_prim.GetPath(), type_name)

        # Do not copy apiSchemas here, because geometry layer should not own
        # SkelBindingAPI or MaterialBindingAPI opinions.
        _copy_metadata(src_prim, dst_prim, copy_api_schemas=False)

        for prop in src_prim.GetProperties():
            prop_name = prop.GetName()

            if _is_skinning_property_name(prop_name):
                continue

            if _is_material_property_name(prop_name):
                continue

            _copy_property(prop, dst_prim)

    if default_prim:
        dst_default = dst_stage.GetPrimAtPath(default_prim.GetPath())
        if dst_default:
            dst_stage.SetDefaultPrim(dst_default)

    dst_stage.GetRootLayer().Save()


def make_skinning_layer(full_model_usd: str, skinning_usd: str) -> None:
    """Create skinning.usda as an overlay layer."""
    stage = _open_stage(full_model_usd)

    _copy_filtered_properties_to_over_layer(
        src_usd_path=full_model_usd,
        dst_usd_path=skinning_usd,
        predicate=lambda prim, prop: _is_skinning_property_name(prop.GetName()),
        start_time=stage.GetStartTimeCode(),
        end_time=stage.GetEndTimeCode(),
        fps=stage.GetFramesPerSecond(),
    )


def make_material_layer(full_model_usd: str, material_usd: str) -> None:
    """Create material.usda as material definitions plus material binding overlays."""
    from pxr import Usd

    if os.path.exists(material_usd):
        os.remove(material_usd)

    src_stage = _open_stage(full_model_usd)
    dst_stage = Usd.Stage.CreateNew(material_usd)

    dst_stage.SetFramesPerSecond(src_stage.GetFramesPerSecond())
    dst_stage.SetTimeCodesPerSecond(src_stage.GetTimeCodesPerSecond())

    default_prim = src_stage.GetDefaultPrim()
    if default_prim:
        dst_root = dst_stage.OverridePrim(default_prim.GetPath())
        dst_stage.SetDefaultPrim(dst_root)

    copied_roots = set()

    for src_prim in src_stage.Traverse():
        name = src_prim.GetName()
        type_name = src_prim.GetTypeName()

        if name == "mtl" or type_name in ("Material", "Shader"):
            path = src_prim.GetPath()

            already_copied = False
            for copied_root in copied_roots:
                if path == copied_root or str(path).startswith(str(copied_root) + "/"):
                    already_copied = True
                    break

            if already_copied:
                continue

            _copy_prim_subtree(src_prim, dst_stage)
            copied_roots.add(path)

    for src_prim in src_stage.Traverse():
        binding_props = [
            prop for prop in src_prim.GetProperties()
            if _is_material_property_name(prop.GetName())
        ]

        if not binding_props:
            continue

        dst_prim = dst_stage.OverridePrim(src_prim.GetPath())
        _copy_metadata(src_prim, dst_prim, copy_api_schemas=True)

        for prop in binding_props:
            _copy_property(prop, dst_prim)

    dst_stage.GetRootLayer().Save()


def localize_material_textures(material_usd: str, textures_dir: str) -> None:
    """Copy every UsdUVTexture source image into textures_dir and rewrite inputs:file to a relative path.

    mayaUSDExport bakes absolute authoring-machine texture paths into the material; this makes the asset
    self-contained and portable (paths relative to material.usda).
    """
    from pxr import Usd, Sdf, UsdShade

    stage = Usd.Stage.Open(material_usd)
    if not stage:
        return

    material_dir = os.path.dirname(os.path.abspath(material_usd))
    copied: Dict[str, bool] = {}
    localized = 0

    for prim in stage.Traverse():
        shader = UsdShade.Shader(prim)
        if not shader:
            continue
        if shader.GetIdAttr().Get() != "UsdUVTexture":
            continue

        file_input = shader.GetInput("file")
        if not file_input:
            continue

        asset_val = file_input.Get()
        if not isinstance(asset_val, Sdf.AssetPath):
            continue

        src = asset_val.resolvedPath or asset_val.path
        if not src or not os.path.isfile(src):
            print("Warning: texture not found, leaving as-is:", asset_val.path)
            continue

        _ensure_dir(textures_dir)
        dst = os.path.join(textures_dir, os.path.basename(src))
        if os.path.abspath(src) != os.path.abspath(dst) and dst not in copied:
            shutil.copyfile(src, dst)
            copied[dst] = True

        rel = os.path.relpath(dst, material_dir).replace("\\", "/")
        file_input.Set(Sdf.AssetPath(rel))
        localized += 1

    if localized:
        stage.GetRootLayer().Save()
        print("Localized {} texture(s) into {}".format(localized, textures_dir))


def make_model_composition_layer(
    model_usd: str,
    asset_root: str,
    skeleton_usd: str,
    geometry_usd: str,
    skinning_usd: str,
    material_usd: str,
) -> None:
    """Create model.usda that composes split model layers and has a defaultPrim."""
    from pxr import Usd, UsdSkel

    if os.path.exists(model_usd):
        os.remove(model_usd)

    _ensure_dir(os.path.dirname(model_usd))

    stage = Usd.Stage.CreateNew(model_usd)
    root_layer = stage.GetRootLayer()

    # Stronger layers should appear first.
    root_layer.subLayerPaths = [
        _rel_path(material_usd, os.path.dirname(model_usd)),
        _rel_path(skinning_usd, os.path.dirname(model_usd)),
        _rel_path(geometry_usd, os.path.dirname(model_usd)),
        _rel_path(skeleton_usd, os.path.dirname(model_usd)),
    ]

    root_prim = UsdSkel.Root.Define(stage, "/{}".format(asset_root)).GetPrim()
    stage.SetDefaultPrim(root_prim)

    _apply_stage_metrics(stage)

    root_layer.Save()


def split_full_model_usd(
    full_model_usd: str,
    model_dir: str,
    asset_root: str,
) -> Dict[str, str]:
    """Split a full static model USD into source model layers."""
    _ensure_dir(model_dir)

    skeleton_usd = os.path.join(model_dir, "skeleton.usda")
    geometry_usd = os.path.join(model_dir, "geometry.usda")
    skinning_usd = os.path.join(model_dir, "skinning.usda")
    material_usd = os.path.join(model_dir, "material.usda")
    model_usd = os.path.join(model_dir, "model.usda")

    make_skeleton_layer(full_model_usd, skeleton_usd)
    make_geometry_layer(full_model_usd, geometry_usd)
    make_skinning_layer(full_model_usd, skinning_usd)
    make_material_layer(full_model_usd, material_usd)

    # Copy textures next to the asset (../textures) and relativize the material's inputs:file paths.
    output_dir = os.path.dirname(model_dir)
    localize_material_textures(material_usd, os.path.join(output_dir, "textures"))

    make_model_composition_layer(
        model_usd=model_usd,
        asset_root=asset_root,
        skeleton_usd=skeleton_usd,
        geometry_usd=geometry_usd,
        skinning_usd=skinning_usd,
        material_usd=material_usd,
    )

    return {
        "model": model_usd,
        "skeleton": skeleton_usd,
        "geometry": geometry_usd,
        "skinning": skinning_usd,
        "material": material_usd,
    }


def clean_anim_usd(usd_path: str) -> None:
    """Remove mesh and material noise from animation-only USD."""
    _remove_prims_by_type_or_name(
        usd_path,
        type_names={"Mesh", "Material", "Shader"},
        names={"mtl"},
    )


def inspect_usd_anim(usd_path: str) -> None:
    """Print a short USD animation inspection report."""
    stage = _open_stage(usd_path)

    print("USD:", usd_path)
    print("  start:", stage.GetStartTimeCode())
    print("  end:", stage.GetEndTimeCode())
    print("  fps:", stage.GetFramesPerSecond())

    for prim in stage.Traverse():
        type_name = prim.GetTypeName()

        if type_name in ("SkelRoot", "Skeleton", "SkelAnimation"):
            print(" ", type_name, prim.GetPath())

        if type_name == "SkelAnimation":
            for attr_name in ("rotations", "translations", "scales"):
                attr = prim.GetAttribute(attr_name)
                if not attr:
                    continue

                samples = attr.GetTimeSamples()
                if samples:
                    print("    {}: {} -> {}, count={}".format(
                        attr_name,
                        samples[0],
                        samples[-1],
                        len(samples),
                    ))


def load_clip_manifest(clips_json: str) -> List[Dict[str, Any]]:
    """Load external clip ranges."""
    with open(clips_json, "r", encoding="utf-8") as f:
        data = json.load(f)

    if isinstance(data, list):
        clips = data
    else:
        clips = data.get("clips", [])

    result = []

    for clip in clips:
        result.append({
            "name": clip["name"],
            "safe_name": _safe_name(clip["name"]),
            "start": int(clip["start"]),
            "end": int(clip["end"]),
            "loop": bool(clip.get("loop", False)),
        })

    return result


def write_output_clips_json(
    output_path: str,
    output_dir: str,
    asset_name: str,
    asset_usd: str,
    model_layers: Dict[str, str],
    clips: List[Dict[str, Any]],
) -> None:
    """Write engine/editor clip metadata."""
    _ensure_dir(os.path.dirname(output_path))

    data = {
        "asset": asset_name,
        "assetUsd": _rel_path(asset_usd, output_dir),
        "model": _rel_path(model_layers["model"], output_dir),
        "layers": {
            "skeleton": _rel_path(model_layers["skeleton"], output_dir),
            "geometry": _rel_path(model_layers["geometry"], output_dir),
            "skinning": _rel_path(model_layers["skinning"], output_dir),
            "material": _rel_path(model_layers["material"], output_dir),
        },
        "clips": clips,
    }

    with open(output_path, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=4)


def make_preview_usd(
    preview_usd: str,
    asset_usd: str,
    anim_usd: str,
    start_frame: int,
    end_frame: int,
    fps: float = 30.0,
) -> None:
    """Create a preview USD that composes asset and one animation clip."""
    from pxr import Usd

    if os.path.exists(preview_usd):
        os.remove(preview_usd)

    _ensure_dir(os.path.dirname(preview_usd))

    asset_stage = Usd.Stage.Open(asset_usd)
    if not asset_stage:
        raise RuntimeError("Failed to open asset USD: {}".format(asset_usd))

    asset_default = asset_stage.GetDefaultPrim()
    if not asset_default:
        raise RuntimeError("asset.usda has no defaultPrim: {}".format(asset_usd))

    stage = Usd.Stage.CreateNew(preview_usd)
    root_layer = stage.GetRootLayer()

    root_layer.subLayerPaths = [
        _rel_path(anim_usd, os.path.dirname(preview_usd)),
        _rel_path(asset_usd, os.path.dirname(preview_usd)),
    ]

    root_prim = stage.OverridePrim(asset_default.GetPath())
    stage.SetDefaultPrim(root_prim)

    stage.SetStartTimeCode(float(start_frame))
    stage.SetEndTimeCode(float(end_frame))
    stage.SetFramesPerSecond(float(fps))
    stage.SetTimeCodesPerSecond(float(fps))

    root_layer.Save()


def make_asset_usd(
    asset_usd: str,
    model_usd: str,
    clips_json: str,
    asset_name: str,
    asset_root: str,
) -> None:
    """Create a referenceable source asset entry USD."""
    from pxr import Sdf, Usd, UsdSkel

    if os.path.exists(asset_usd):
        os.remove(asset_usd)

    _ensure_dir(os.path.dirname(asset_usd))

    model_stage = Usd.Stage.Open(model_usd)
    if not model_stage:
        raise RuntimeError("Failed to open model USD: {}".format(model_usd))

    model_default = model_stage.GetDefaultPrim()
    if not model_default:
        raise RuntimeError("model.usda must have a defaultPrim: {}".format(model_usd))

    stage = Usd.Stage.CreateNew(asset_usd)

    root_prim = UsdSkel.Root.Define(stage, "/{}".format(asset_root)).GetPrim()
    root_prim.GetReferences().AddReference(
        _rel_path(model_usd, os.path.dirname(asset_usd)),
        Sdf.Path("/{}".format(asset_root)),
    )

    stage.SetDefaultPrim(root_prim)

    stage.GetRootLayer().customLayerData = {
        "assetName": asset_name,
        "clips": _rel_path(clips_json, os.path.dirname(asset_usd)),
        "model": _rel_path(model_usd, os.path.dirname(asset_usd)),
    }

    # The runtime opens THIS file, so units/orientation must be authored here (stage metadata is not
    # inherited across references/sublayers).
    _apply_stage_metrics(stage)

    stage.GetRootLayer().Save()


def make_level_reference_example(
    example_usd: str,
    asset_usd: str,
    asset_root: str,
) -> None:
    """Create a small example USD that references the asset entry USD."""
    from pxr import Sdf, Usd, UsdGeom

    if os.path.exists(example_usd):
        os.remove(example_usd)

    _ensure_dir(os.path.dirname(example_usd))

    stage = Usd.Stage.CreateNew(example_usd)
    level = UsdGeom.Xform.Define(stage, "/Level").GetPrim()
    stage.SetDefaultPrim(level)

    inst = stage.DefinePrim("/Level/Frog_01", "Xform")
    inst.GetReferences().AddReference(
        _rel_path(asset_usd, os.path.dirname(example_usd)),
        Sdf.Path("/{}".format(asset_root)),
    )

    stage.GetRootLayer().Save()


def validate_model_layers(paths: Dict[str, str]) -> None:
    """Run basic validation on model layers."""
    print("Validate model layers:")

    for key, path in paths.items():
        if not os.path.exists(path):
            raise RuntimeError("Missing {} layer: {}".format(key, path))
        print("  {}: {}".format(key, path))

    model_stage = _open_stage(paths["model"])
    if not model_stage.GetDefaultPrim():
        raise RuntimeError("model.usda has no defaultPrim: {}".format(paths["model"]))

    geometry_stage = _open_stage(paths["geometry"])
    skinning_stage = _open_stage(paths["skinning"])
    material_stage = _open_stage(paths["material"])

    for prim in geometry_stage.Traverse():
        for prop in prim.GetProperties():
            prop_name = prop.GetName()

            if _is_skinning_property_name(prop_name):
                raise RuntimeError("Skinning property leaked into geometry.usda: {}".format(prop.GetPath()))

            if _is_material_property_name(prop_name):
                raise RuntimeError("Material binding leaked into geometry.usda: {}".format(prop.GetPath()))

    has_skinning_opinion = False
    for prim in skinning_stage.Traverse():
        for prop in prim.GetProperties():
            if _is_skinning_property_name(prop.GetName()):
                has_skinning_opinion = True
                break

    if not has_skinning_opinion:
        print("Warning: skinning.usda has no skinning properties.")

    has_material_opinion = False
    for prim in material_stage.Traverse():
        if prim.GetTypeName() in ("Material", "Shader"):
            has_material_opinion = True
            break

    if not has_material_opinion:
        print("Warning: material.usda has no Material or Shader prim.")


def validate_asset_usd(asset_usd: str) -> None:
    """Validate that asset.usda is referenceable and in canonical space (Y-up, metres, rightHanded)."""
    from pxr import UsdGeom, UsdSkel

    stage = _open_stage(asset_usd)
    default_prim = stage.GetDefaultPrim()

    if not default_prim:
        raise RuntimeError("asset.usda has no defaultPrim: {}".format(asset_usd))

    print("Asset USD defaultPrim:", default_prim.GetPath())

    # Canonical-space tripwires (hard failures). These catch any regression where exported data is
    # still centimetre-scale or mis-oriented, independent of what the canonicalize pass believed.
    meters_per_unit = float(UsdGeom.GetStageMetersPerUnit(stage))
    up_axis = str(UsdGeom.GetStageUpAxis(stage))
    if meters_per_unit != 1.0 or up_axis != "Y":
        raise RuntimeError(
            "asset.usda is not canonical: metersPerUnit={} upAxis={} (expected 1.0 / Y): {}".format(
                meters_per_unit, up_axis, asset_usd))

    for prim in stage.Traverse():
        if prim.IsA(UsdSkel.Skeleton):
            bind_transforms = UsdSkel.Skeleton(prim).GetBindTransformsAttr().Get()
            if bind_transforms:
                root_translation = bind_transforms[0].ExtractTranslation()
                if root_translation.GetLength() >= 10.0:
                    raise RuntimeError(
                        "Skeleton {} root-joint bind translation |{}| >= 10 - data looks centimetre-scale, "
                        "not metres: {}".format(prim.GetPath(), root_translation, asset_usd))
        if prim.IsA(UsdGeom.Mesh):
            orientation = UsdGeom.Mesh(prim).GetOrientationAttr().Get()
            if orientation is not None and str(orientation) == "leftHanded":
                raise RuntimeError(
                    "Mesh {} authors orientation=leftHanded (canonical is rightHanded): {}".format(
                        prim.GetPath(), asset_usd))

    print("Canonical-space validation passed: metersPerUnit=1.0 upAxis=Y winding=rightHanded")


def _export_model_layers(
    fbx_path: str,
    take_index: Optional[int],
    full_model_usd: str,
    model_dir: str,
    asset_root: str,
    root_joint: Optional[str],
    mesh_root: Optional[str],
) -> Dict[str, str]:
    """Import FBX and export split model layers."""
    import_fbx(fbx_path, take_index=take_index)

    _capture_stage_metrics()

    real_root_joint = find_root_joint(root_joint)
    skeleton_container = find_skeleton_container(real_root_joint)
    mesh_roots = find_mesh_roots(mesh_root)

    _print_detected_scene(real_root_joint, skeleton_container, mesh_roots)

    export_full_model_usd(
        output_path=full_model_usd,
        export_root_name=asset_root,
        skeleton_container=skeleton_container,
        mesh_roots=mesh_roots,
    )

    # Convert to metres BEFORE splitting so every derived sublayer is canonical.
    canonicalize_usd_layer(full_model_usd)

    model_layers = split_full_model_usd(
        full_model_usd=full_model_usd,
        model_dir=model_dir,
        asset_root=asset_root,
    )

    validate_model_layers(model_layers)

    return model_layers


def _validate_canonical_anim(anim_usd: str) -> None:
    """Hard-fail if an anim layer is not canonical (metadata {1.0, Y}, metre-scale translations)."""
    from pxr import UsdGeom, UsdSkel

    stage = _open_stage(anim_usd)

    meters_per_unit = float(UsdGeom.GetStageMetersPerUnit(stage))
    up_axis = str(UsdGeom.GetStageUpAxis(stage))
    if meters_per_unit != 1.0 or up_axis != "Y":
        raise RuntimeError(
            "Anim layer is not canonical: metersPerUnit={} upAxis={} (expected 1.0 / Y): {}".format(
                meters_per_unit, up_axis, anim_usd))

    for prim in stage.Traverse():
        if not prim.IsA(UsdSkel.Animation):
            continue
        translations_attr = UsdSkel.Animation(prim).GetTranslationsAttr()
        times = translations_attr.GetTimeSamples() or [None]
        translations = translations_attr.Get(times[0]) if times[0] is not None else translations_attr.Get()
        if translations:
            max_norm = max(t.GetLength() for t in translations)
            if max_norm >= 10.0:
                raise RuntimeError(
                    "SkelAnimation {} max joint translation {} >= 10 - data looks centimetre-scale, "
                    "not metres: {}".format(prim.GetPath(), max_norm, anim_usd))


def _export_one_anim_from_current_scene(
    anim_usd: str,
    asset_root: str,
    root_joint: Optional[str],
    start_frame: int,
    end_frame: int,
    clean_anim: bool,
) -> None:
    """Export one animation USD from the current imported scene."""
    real_root_joint = find_root_joint(root_joint)
    skeleton_container = find_skeleton_container(real_root_joint)

    export_anim_usd(
        output_path=anim_usd,
        export_root_name=asset_root,
        skeleton_container=skeleton_container,
        start_frame=start_frame,
        end_frame=end_frame,
    )

    canonicalize_usd_layer(anim_usd)

    if clean_anim:
        clean_anim_usd(anim_usd)

    _validate_canonical_anim(anim_usd)

    inspect_usd_anim(anim_usd)


def publish_fbx_to_usd(
    fbx_path: str,
    output_dir: str,
    asset_name: Optional[str] = None,
    root_joint: Optional[str] = None,
    mesh_root: Optional[str] = None,
    clips_json: Optional[str] = None,
    clean_anim: bool = True,
    make_previews: bool = False,
    keep_intermediate_full_model: bool = False,
    make_reference_example: bool = False,
) -> None:
    """Publish FBX into split, referenceable USD source layers."""
    ensure_plugins()

    fbx_path = os.path.abspath(fbx_path)
    output_dir = os.path.abspath(output_dir)

    if asset_name is None:
        asset_name = _safe_name(os.path.splitext(os.path.basename(fbx_path))[0])
    else:
        asset_name = _safe_name(asset_name)

    asset_root = _asset_root_name(asset_name)

    model_dir = os.path.join(output_dir, "model")
    anim_dir = os.path.join(output_dir, "anim")
    metadata_dir = os.path.join(output_dir, "metadata")
    preview_dir = os.path.join(output_dir, "preview")
    example_dir = os.path.join(output_dir, "examples")
    intermediate_dir = os.path.join(output_dir, "_intermediate")

    _ensure_dir(model_dir)
    _ensure_dir(anim_dir)
    _ensure_dir(metadata_dir)
    _ensure_dir(intermediate_dir)

    if make_previews:
        _ensure_dir(preview_dir)

    if make_reference_example:
        _ensure_dir(example_dir)

    full_model_usd = os.path.join(intermediate_dir, "{}_full_model.usda".format(asset_name))
    asset_usd = os.path.join(output_dir, "{}.asset.usda".format(asset_name))
    clips_output_json = os.path.join(metadata_dir, "{}_clips.json".format(asset_name))

    output_clips: List[Dict[str, Any]] = []

    if clips_json:
        clips = load_clip_manifest(clips_json)

        model_layers = _export_model_layers(
            fbx_path=fbx_path,
            take_index=None,
            full_model_usd=full_model_usd,
            model_dir=model_dir,
            asset_root=asset_root,
            root_joint=root_joint,
            mesh_root=mesh_root,
        )

        write_output_clips_json(
            output_path=clips_output_json,
            output_dir=output_dir,
            asset_name=asset_name,
            asset_usd=asset_usd,
            model_layers=model_layers,
            clips=[],
        )

        make_asset_usd(
            asset_usd=asset_usd,
            model_usd=model_layers["model"],
            clips_json=clips_output_json,
            asset_name=asset_name,
            asset_root=asset_root,
        )

        validate_asset_usd(asset_usd)

        for clip in clips:
            anim_usd = os.path.join(anim_dir, "{}.anim.usda".format(clip["safe_name"]))

            _export_one_anim_from_current_scene(
                anim_usd=anim_usd,
                asset_root=asset_root,
                root_joint=root_joint,
                start_frame=clip["start"],
                end_frame=clip["end"],
                clean_anim=clean_anim,
            )

            clip_record = {
                "name": clip["name"],
                "file": _rel_path(anim_usd, output_dir),
                "start": clip["start"],
                "end": clip["end"],
                "loop": clip["loop"],
            }

            if make_previews:
                preview_usd = os.path.join(preview_dir, "{}.preview.usda".format(clip["safe_name"]))
                make_preview_usd(
                    preview_usd=preview_usd,
                    asset_usd=asset_usd,
                    anim_usd=anim_usd,
                    start_frame=clip["start"],
                    end_frame=clip["end"],
                )
                clip_record["preview"] = _rel_path(preview_usd, output_dir)

            output_clips.append(clip_record)

    else:
        takes = get_fbx_takes(fbx_path)
        if not takes:
            raise RuntimeError("No FBX takes found.")

        print("FBX takes:")
        for take in takes:
            print("  [{index}] {name}".format(**take))

        model_layers = _export_model_layers(
            fbx_path=fbx_path,
            take_index=takes[0]["index"],
            full_model_usd=full_model_usd,
            model_dir=model_dir,
            asset_root=asset_root,
            root_joint=root_joint,
            mesh_root=mesh_root,
        )

        write_output_clips_json(
            output_path=clips_output_json,
            output_dir=output_dir,
            asset_name=asset_name,
            asset_usd=asset_usd,
            model_layers=model_layers,
            clips=[],
        )

        make_asset_usd(
            asset_usd=asset_usd,
            model_usd=model_layers["model"],
            clips_json=clips_output_json,
            asset_name=asset_name,
            asset_root=asset_root,
        )

        validate_asset_usd(asset_usd)

        for take in takes:
            import_fbx(fbx_path, take_index=take["index"])

            real_root_joint = find_root_joint(root_joint)
            key_range = get_key_range(real_root_joint)

            if key_range is None:
                print("Warning: no keyframes found on skeleton. Use playback range.")
                key_range = get_fallback_playback_range()

            start_frame, end_frame = key_range
            anim_usd = os.path.join(anim_dir, "{}.anim.usda".format(take["safe_name"]))

            _export_one_anim_from_current_scene(
                anim_usd=anim_usd,
                asset_root=asset_root,
                root_joint=root_joint,
                start_frame=start_frame,
                end_frame=end_frame,
                clean_anim=clean_anim,
            )

            clip_base_name = take["name"].lower().split("|")[-1]
            loop = clip_base_name in ("idle", "walk", "run")

            clip_record = {
                "name": take["name"],
                "file": _rel_path(anim_usd, output_dir),
                "start": start_frame,
                "end": end_frame,
                "loop": loop,
            }

            if make_previews:
                preview_usd = os.path.join(preview_dir, "{}.preview.usda".format(take["safe_name"]))
                make_preview_usd(
                    preview_usd=preview_usd,
                    asset_usd=asset_usd,
                    anim_usd=anim_usd,
                    start_frame=start_frame,
                    end_frame=end_frame,
                )
                clip_record["preview"] = _rel_path(preview_usd, output_dir)

            output_clips.append(clip_record)

    write_output_clips_json(
        output_path=clips_output_json,
        output_dir=output_dir,
        asset_name=asset_name,
        asset_usd=asset_usd,
        model_layers=model_layers,
        clips=output_clips,
    )

    make_asset_usd(
        asset_usd=asset_usd,
        model_usd=model_layers["model"],
        clips_json=clips_output_json,
        asset_name=asset_name,
        asset_root=asset_root,
    )

    validate_asset_usd(asset_usd)

    if make_reference_example:
        example_usd = os.path.join(example_dir, "{}_reference_example.usda".format(asset_name))
        make_level_reference_example(
            example_usd=example_usd,
            asset_usd=asset_usd,
            asset_root=asset_root,
        )

    if not keep_intermediate_full_model:
        try:
            shutil.rmtree(intermediate_dir)
        except Exception:
            pass

    print("Publish finished.")
    print("Asset USD:", asset_usd)
    print("Asset root:", asset_root)
    print("Model USD:", model_layers["model"])
    print("Skeleton USD:", model_layers["skeleton"])
    print("Geometry USD:", model_layers["geometry"])
    print("Skinning USD:", model_layers["skinning"])
    print("Material USD:", model_layers["material"])
    print("Clips JSON:", clips_output_json)


def main() -> None:
    """Command line entry."""
    parser = argparse.ArgumentParser()

    parser.add_argument("--fbx", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--asset", default=None)
    parser.add_argument("--root-joint", default=None)
    parser.add_argument("--mesh-root", default=None)
    parser.add_argument("--clips-json", default=None)
    parser.add_argument("--no-clean-anim", action="store_true")
    parser.add_argument("--preview", action="store_true")
    parser.add_argument("--keep-intermediate-full-model", action="store_true")
    parser.add_argument("--reference-example", action="store_true")

    args = parser.parse_args()

    publish_fbx_to_usd(
        fbx_path=args.fbx,
        output_dir=args.out,
        asset_name=args.asset,
        root_joint=args.root_joint,
        mesh_root=args.mesh_root,
        clips_json=args.clips_json,
        clean_anim=not args.no_clean_anim,
        make_previews=args.preview,
        keep_intermediate_full_model=args.keep_intermediate_full_model,
        make_reference_example=args.reference_example,
    )


if __name__ == "__main__":
    main()