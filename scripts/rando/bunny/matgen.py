"""MAT3 / MDL3 / TEX1 chunks for the hood: the prop's own material (mask_v) and texture (mask_all), cut out of the
gallery room's BDL on the player's disc.  Needs gclib (wwrando's, on the embedded Python's path)."""
from __future__ import annotations
import io


def material_chunks(prop_bdl: bytes, material_index: int, name: str = "hood", cull_none: bool = True):
    import gclib.fs_helpers as fs
    import gclib.gx_enums as GX
    from gclib.j3d import J3D

    j = J3D(io.BytesIO(prop_bdl))
    mat3, tex1, mdl3 = j.mat3, j.tex1, j.mdl3
    mm = mat3.materials[material_index]
    ti = mm.textures[0]
    if cull_none:
        mm.cull_mode = GX.CullMode.Cull_None
    header = bytes(tex1.textures[ti].data.getvalue()[tex1.textures[ti].header_offset:tex1.textures[ti].header_offset + 0x20])
    tex_name = tex1.texture_names[ti]

    mat3.indirects = [mat3.indirects[material_index]]
    mat3.materials = [mm]
    mat3.mat_names = [name]
    mat3.material_count = 1
    mm.textures[0] = 0
    tex1.data.seek(0x20)
    tex1.data.write(header)
    tex1.textures = [tex1.textures[ti]]
    tex1.textures[0].header_offset = 0x20
    tex1.texture_names = [tex_name]
    tex1.num_textures = 1

    mdl3.entries = [mdl3.entries[material_index]]
    mdl3.mat_names = [name]

    mat3.save()
    tex1.save()
    fs.write_u16(mat3.data, 8, 1)
    fs.write_u16(tex1.data, 8, 1)
    mdl3.generate_from_mat3(mat3, tex1)
    return mat3.data.getvalue(), mdl3.data.getvalue(), tex1.data.getvalue()
