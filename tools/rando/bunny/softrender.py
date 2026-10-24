"""A tiny numpy software rasterizer for the Bunny Hood comparison renders (developer tool; ships no data).

Mesh = dict(pos (T,3,3), nrm (T,3,3), uv (T,3,2), clr (T,3,4), tex: PIL.Image RGBA or None, wrap: 'repeat'|'mirror'|
'clamp', double: bool).  Camera is a look-at with orthographic or perspective projection.
"""
from __future__ import annotations
import numpy as np
from PIL import Image


def look_at(eye, target, up=(0, 1, 0)):
    eye, target, up = (np.asarray(x, float) for x in (eye, target, up))
    f = target - eye
    f /= np.linalg.norm(f)
    r = np.cross(f, up)
    r /= np.linalg.norm(r)
    u = np.cross(r, f)
    V = np.eye(4)
    V[0, :3], V[1, :3], V[2, :3] = r, u, -f
    V[:3, 3] = -V[:3, :3] @ eye
    return V


def sample(tex: np.ndarray, uv: np.ndarray, wrap: str):
    h, w = tex.shape[:2]
    u, v = uv[:, 0], uv[:, 1]
    if wrap == "mirror":
        u = 1 - np.abs((u % 2) - 1)
        v = 1 - np.abs((v % 2) - 1)
    elif wrap == "clamp":
        u, v = np.clip(u, 0, 1), np.clip(v, 0, 1)
    else:
        u, v = u % 1, v % 1
    x = u * w - 0.5
    y = v * h - 0.5
    x0 = np.floor(x).astype(int)
    y0 = np.floor(y).astype(int)
    fx = (x - x0)[:, None]
    fy = (y - y0)[:, None]

    def px(xx, yy):
        return tex[np.clip(yy, 0, h - 1) % h, np.clip(xx, 0, w - 1) % w]

    return (px(x0, y0) * (1 - fx) * (1 - fy) + px(x0 + 1, y0) * fx * (1 - fy) +
            px(x0, y0 + 1) * (1 - fx) * fy + px(x0 + 1, y0 + 1) * fx * fy)


class Renderer:
    def __init__(self, width, height, bg=(0.93, 0.93, 0.95, 1.0), ss=2):
        self.W, self.H, self.ss = width * ss, height * ss, ss
        self.color = np.empty((self.H, self.W, 4), np.float32)
        self.color[:] = bg
        self.depth = np.full((self.H, self.W), np.inf, np.float32)
        self.outw, self.outh = width, height

    def draw(self, mesh, view, proj_scale, persp=None, light=(0.4, 0.7, 0.6), ambient=0.55, unlit=False,
             tint=None, flat_color=None, toon=None):
        """proj_scale: pixels per unit (ortho).  persp: focal length in pixels when perspective."""
        pos = mesh["pos"].reshape(-1, 3)
        pv = (view[:3, :3] @ pos.T).T + view[:3, 3]
        W, H = self.W, self.H
        if persp:
            z = -pv[:, 2]
            sx = pv[:, 0] / z * persp * self.ss + W / 2
            sy = -pv[:, 1] / z * persp * self.ss + H / 2
            depth = z
        else:
            sx = pv[:, 0] * proj_scale * self.ss + W / 2
            sy = -pv[:, 1] * proj_scale * self.ss + H / 2
            depth = -pv[:, 2]
        nrm = mesh["nrm"].reshape(-1, 3)
        vn = (view[:3, :3] @ nrm.T).T
        vn /= np.maximum(np.linalg.norm(vn, axis=1, keepdims=True), 1e-9)
        L = np.asarray(light, float)
        L /= np.linalg.norm(L)
        lam = np.clip(vn @ L, 0, 1)
        shade = ambient + (1 - ambient) * lam
        clr = mesh["clr"].reshape(-1, 4)
        uv = mesh["uv"].reshape(-1, 2)
        tex = mesh.get("tex")
        texa = np.asarray(tex.convert("RGBA"), np.float32) / 255 if tex is not None else None
        T = len(pos) // 3
        SX, SY, D = sx.reshape(T, 3), sy.reshape(T, 3), depth.reshape(T, 3)
        double = mesh.get("double", False)
        for t in range(T):
            x, y, d = SX[t], SY[t], D[t]
            area = (x[1] - x[0]) * (y[2] - y[0]) - (x[2] - x[0]) * (y[1] - y[0])
            if abs(area) < 1e-6:
                continue
            if not double and ((area < 0) if mesh.get('cw_front', False) else (area > 0)):
                continue  # screen y is down: CCW front faces have negative area (N64); GX front faces are CW
            x0, x1 = int(max(np.floor(x.min()), 0)), int(min(np.ceil(x.max()), W - 1))
            y0, y1 = int(max(np.floor(y.min()), 0)), int(min(np.ceil(y.max()), H - 1))
            if x1 < x0 or y1 < y0:
                continue
            gx, gy = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
            w0 = ((x[1] - gx) * (y[2] - gy) - (x[2] - gx) * (y[1] - gy)) / area
            w1 = ((x[2] - gx) * (y[0] - gy) - (x[0] - gx) * (y[2] - gy)) / area
            w2 = 1 - w0 - w1
            m = (w0 >= 0) & (w1 >= 0) & (w2 >= 0)
            if not m.any():
                continue
            w0, w1, w2 = w0[m], w1[m], w2[m]
            if persp:  # perspective-correct
                iw = w0 / d[0] + w1 / d[1] + w2 / d[2]
                p0, p1, p2 = w0 / d[0] / iw, w1 / d[1] / iw, w2 / d[2] / iw
                dz = 1 / iw
            else:
                p0, p1, p2 = w0, w1, w2
                dz = w0 * d[0] + w1 * d[1] + w2 * d[2]
            ys, xs = np.nonzero(m)
            ys, xs = ys + y0, xs + x0
            ok = dz < self.depth[ys, xs]
            if not ok.any():
                continue
            ys, xs, p0, p1, p2, dz = ys[ok], xs[ok], p0[ok], p1[ok], p2[ok], dz[ok]
            i = t * 3
            shp = (p0 * shade[i] + p1 * shade[i + 1] + p2 * shade[i + 2])[:, None]
            if toon is not None:
                lv = np.asarray(toon[0], np.float32)
                shp = lv[np.searchsorted(np.asarray(toon[1], np.float32), shp[:, 0])][:, None]
            col = p0[:, None] * clr[i] + p1[:, None] * clr[i + 1] + p2[:, None] * clr[i + 2]
            if flat_color is not None:
                base = np.tile(np.asarray(flat_color, np.float32), (len(xs), 1))
            elif texa is not None:
                base = sample(texa, p0[:, None] * uv[i] + p1[:, None] * uv[i + 1] + p2[:, None] * uv[i + 2],
                              mesh.get("wrap", "repeat"))
            else:
                base = np.ones((len(xs), 4), np.float32)
            rgb = base[:, :3] * (col[:, :3] if mesh.get("vcolor", True) else 1)
            if not unlit:
                rgb = rgb * shp
            if tint is not None:
                rgb = rgb * np.asarray(tint, np.float32)
            alpha = base[:, 3]
            keep = alpha > 0.5
            ys, xs, rgb, dz = ys[keep], xs[keep], rgb[keep], dz[keep]
            self.depth[ys, xs] = dz
            self.color[ys, xs, :3] = rgb
            self.color[ys, xs, 3] = 1

    def image(self):
        a = np.clip(self.color, 0, 1)
        a = a.reshape(self.outh, self.ss, self.outw, self.ss, 4).mean((1, 3))
        return Image.fromarray((a * 255 + 0.5).astype(np.uint8), "RGBA")
