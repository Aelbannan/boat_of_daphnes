"""WW Link's standing rest pose: his wait animation (frame 0) with the hat joints at the pose the game's own hat physics
settles to at a standstill (daPy_lk_c::setHatAngle and jointBeforeCB, d_a_player_main.cpp).

At standstill the hat's drive vector is straight down (the constant -7.5 bias), so the first link's target is 0x800; the clamp
"r0_2 < -0x3000 -> m34F6 = 0x1000 - m3528" then makes the net first-joint Z rotation (m3528 + (m34F6 - 0x8000)) equal
0x1000 - 0x8000 = -0x7000 (-157.5 degrees) whatever the head's pitch; the second and third links have no extra angle.
jointBeforeCB replaces the three joints' rotations with those angles (the animation's hat rotations are discarded).
"""
from __future__ import annotations
import numpy as np
import bck as B

HATA, HATB, HATC = 26, 27, 28
HAT_REST_Z_DEG = -157.5  # (0x1000 - 0x8000) * 180 / 0x8000


def apply(model, wait_bck: bytes, frame: float = 0.0, hat=(HAT_REST_Z_DEG, 0.0, 0.0), hat_extra=(0.0, 0.0, 0.0)):
    """Overwrite model.joints' scale/rot/trans with the animation sample and the hat rest rotations."""
    sm = B.sample(B.parse(wait_bck), frame)
    for i, (s, r, t) in enumerate(sm[: len(model.joints)]):
        j = model.joints[i]
        j["scale"], j["rot"], j["trans"] = tuple(s), list(r), tuple(t)
    for jn, z, extra in zip((HATA, HATB, HATC), hat, hat_extra):
        model.joints[jn]["rot"] = [0.0, 0.0, np.radians(z + extra)]
    return model
