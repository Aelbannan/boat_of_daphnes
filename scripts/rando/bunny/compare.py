"""Comparison renders: MM child Link with the MM Bunny Hood vs WW Link with the generated hood, same views."""
from __future__ import annotations
import numpy as np
from PIL import Image, ImageDraw
import softrender as SR
import refmodel as R

VIEWS = {
    "left": (1, 0, 0),
    "front": (0, 0, 1),
    "right": (-1, 0, 0),
    "back": (0, 0, -1),
    "three_quarter": (0.8, 0.25, 0.8),
    "three_quarter_back": (0.8, 0.25, -0.8),
    "top": (0, 1, 0),
}
# both games: up = +Y, Link faces +Z, his left is +X (WW world as loaded here: facing +Z, up +Y)


def mat_to_srt(M):
    """Rotation/translation of a 4x4 as a J3D joint (T * Rz * Ry * Rx)."""
    R3 = M[:3, :3]
    ry = -np.arcsin(np.clip(R3[2, 0], -1, 1))
    return (np.arctan2(R3[2, 1], R3[2, 2]), ry, np.arctan2(R3[1, 0], R3[0, 0])), tuple(M[:3, 3])


def follower_override(link, names_to_link_joint):
    """The follower joints' transforms for the Link pose `link` (a linkww.WWLink after pose()): the game's per-frame rule,
    inverse(head node) * node of the followed joint, in the head frame."""
    return {name: mat_to_srt(link.to_head @ link.world[jn]) for name, jn in names_to_link_joint.items()}


def hood_meshes(bdl: bytes, to_world: np.ndarray, pose=None, tints=None, joint_override=None):
    m, j, meshes = R.load_meshes(bdl, joint_pose=pose, tints=tints, joint_override=joint_override)
    out = []
    for me in meshes:
        o = dict(me)
        o["pos"] = me["pos"] @ to_world[:3, :3].T + to_world[:3, 3]
        o["nrm"] = me["nrm"] @ to_world[:3, :3].T
        o["vcolor"] = True
        o["unlit"] = True
        out.append(o)
    return out


def render(meshes, centre, units_across, view, size=512, light=(0.4, 0.7, 0.6), ambient=0.7, bg=(0.93, 0.93, 0.95, 1)):
    d = np.array(VIEWS[view] if isinstance(view, str) else view, float)
    d /= np.linalg.norm(d)
    V = SR.look_at(np.asarray(centre) + d * units_across * 4, centre, up=(0, 0, -1) if view == 'top' else (0, 1, 0))
    rd = SR.Renderer(size, size, bg=bg)
    for me in meshes:
        rd.draw(me, V, size / units_across, light=light, ambient=ambient, unlit=bool(me.get("unlit", False)))
    return rd.image().convert("RGB")


def label(im, text):
    d = ImageDraw.Draw(im)
    d.rectangle((0, 0, 8 * len(text) + 8, 16), fill=(255, 255, 255))
    d.text((4, 2), text, fill=(0, 0, 0))
    return im


def sheet(rows, names=None):
    w = sum(i.width for i in rows[0])
    h = sum(r[0].height for r in rows)
    out = Image.new("RGB", (w, h), (255, 255, 255))
    y = 0
    for r in rows:
        x = 0
        for im in r:
            out.paste(im, (x, y))
            x += im.width
        y += r[0].height
    return out
