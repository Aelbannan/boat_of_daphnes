"""WW Link (cl.bdl) loaded for the hood fit and the comparison renders.  All coordinates returned in the head_jnt
local frame (+X up, +Y front, Z lateral, origin = head_jnt origin) unless a world matrix is asked for."""
from __future__ import annotations
import copy
import numpy as np
import j3d_geom as G
import refmodel as R

HEAD, HATA, HATB, HATC = 15, 26, 27, 28


class WWLink:
    def __init__(self, cl_bdl: bytes, hat_droop_deg: float = 0.0, wait_bck: bytes = None):
        self.wait_bck = wait_bck
        self.m, self.j, meshes = R.load_meshes(cl_bdl)
        self.textures = []
        for me in meshes:
            tex = me["tex"]
            if me["matname"] in ("eyeL", "eyeR"):
                tex = self._eye(me["mat"])
            self.textures.append((tex, me["wrap"], me["double"], me["matname"], me["joint"]))
        self.hat_droop_deg = hat_droop_deg
        self.pose()

    def _eye(self, mat_index):
        """The game's eye: the eye shape (IA8) multiplied by the iris page (C8), the iris shifted by the material's
        second texture matrix."""
        from PIL import Image, ImageChops
        mat = self.j.mat3.materials[mat_index]
        eye = self.j.tex1.textures[mat.textures[0]].render().convert("RGBA")
        iris = self.j.tex1.textures[mat.textures[1]].render().convert("RGBA")
        tx = mat.tex_matrixes[1].translation.x if len(mat.tex_matrixes) > 1 and mat.tex_matrixes[1] else 0.0
        iris = ImageChops.offset(iris, int(round(-tx * iris.width)), 0)
        rgb = ImageChops.multiply(eye.convert("RGB"), iris.convert("RGB"))
        out = rgb.convert("RGBA")
        out.putalpha(eye.getchannel("A"))
        return out

    def pose(self, hat_deg=None, hat_b_deg=0.0, hat_c_deg=0.0, hat_extra=None):
        """Rebuild all skinned triangles in world space with the hat chain bent by hat_deg about its Z axis."""
        hd = self.hat_droop_deg if hat_deg is None else hat_deg
        m = copy.deepcopy(self.m)
        if self.wait_bck is not None:
            import restpose
            restpose.apply(m, self.wait_bck, hat_extra=hat_extra or (0.0, 0.0, 0.0))
            hd = hat_deg = 0.0
            hat_b_deg = hat_c_deg = 0.0
        r = list(m.joints[HATA]["rot"]); r[2] += np.radians(hd); m.joints[HATA]["rot"] = r
        r = list(m.joints[HATB]["rot"]); r[2] += np.radians(hat_b_deg); m.joints[HATB]["rot"] = r
        r = list(m.joints[HATC]["rot"]); r[2] += np.radians(hat_c_deg); m.joints[HATC]["rot"] = r
        self.world = G.joint_world(m)
        self.head_world = self.world[HEAD]
        self.to_head = np.linalg.inv(self.head_world)
        self.meshes = []
        for k, (jt, mt, s) in enumerate(m.scene):
            t = G.triangles(m, s, self.world)
            tex, wrap, dbl, matname, joint = self.textures[k]
            if "dam" in matname:
                continue
            t.update(cw_front=True, tex=tex, wrap=wrap, double=dbl, matname=matname, joint=joint, vcolor=False)
            self.meshes.append(t)
        return self

    def head_frame_meshes(self, names=None, exclude=()):
        out = []
        for me in self.meshes:
            if names is not None and me["matname"] not in names:
                continue
            if me["matname"] in exclude:
                continue
            o = dict(me)
            R3 = self.to_head[:3, :3]
            o["pos"] = (me["pos"] @ R3.T) + self.to_head[:3, 3]
            o["nrm"] = me["nrm"] @ R3.T
            out.append(o)
        return out
