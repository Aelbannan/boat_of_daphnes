"""Stand-in for the `imagequant` package, installed into the rando runtime's site-packages.

gclib (MIT) does `import imagequant` at module load, but only calls it when re-encoding palette textures
(`texture_utils.encode_image`), which the seed generator never does (a dry run touches no game files).
The real wheel bundles libimagequant, which is GPL-3.0-or-later (the Python binding itself is BSD-3), so
the runtime ships this stub instead and the GPL binary is never fetched. Anything that really needs
quantisation (building textures from a disc, a later milestone) must replace this with a proper choice.
"""


def quantize_pil_image(*args, **kwargs):
    raise RuntimeError(
        "imagequant is a stub in the boat-of-daphnes rando runtime (the seed generator never quantises "
        "images). See scripts/rando/shims/imagequant.py."
    )
