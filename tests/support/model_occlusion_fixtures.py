#!/usr/bin/env python3
"""遮蔽テクスチャのGPU比較に使うGLBを生成する。"""

import argparse
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import model_emissive_fixtures as glb


def material(base=(0.8, 0.8, 0.8), occlusion=None, texcoord=0, strength=1.0,
             sampler=None, mask=False, emission=None):
    """base色、遮蔽参照、材質の補助入力を持つGLB材質を作る。"""
    result = {"pbrMetallicRoughness": {
        "baseColorFactor": list(base) + [1.0], "metallicFactor": 0.0,
        "roughnessFactor": 1.0}}
    if occlusion is not None:
        view = {"index": occlusion, "texCoord": texcoord}
        if sampler is not None:
            view.update(sampler)
        result["occlusionTexture"] = view
    if strength != 1.0:
        result["occlusionTexture"]["strength"] = strength
    if mask:
        result["alphaMode"] = "MASK"
        result["alphaCutoff"] = 0.5
    if emission is not None:
        result["emissiveFactor"] = list(emission)
    return result


def textures_for(image, samplers, sources=None):
    """共通画像をsampler variantごとのtexture entryへ割り当てる。"""
    sources = sources or [0] * len(samplers)
    return [image], samplers, [{"source": source, "sampler": index}
                               for index, source in enumerate(sources)]


def write_pair(output, name, quads, actual_materials, images, samplers,
               textures, reference_quads, reference_materials):
    """実材質と焼込みbase色の参照材質を同じ形状で保存する。"""
    glb.write_glb(output, name, quads, actual_materials, images, samplers, textures)
    glb.write_glb(output, f"{name}-reference", reference_quads,
                  reference_materials)


def baked(base, red, strength):
    """線形AO redとstrengthをbaseColorへ焼き込む。"""
    factor = 1.0 + strength * (red - 1.0)
    return tuple(channel * factor for channel in base)


def pair_for_constant(output, name, pixel, strength=1.0, base=(0.8, 0.8, 0.8),
                      sampler=None):
    """一定AO画素の材質と独立した色参照を生成する。"""
    image = glb.solid_png(pixel)
    quads = [glb.quad()]
    reference = [glb.quad()]
    red = pixel[0] / 255.0
    # AOはsRGBではなく線形値として扱う。
    write_pair(output, name, quads, [material(base, 0, strength=strength)],
               [image], [sampler or {}], [{"source": 0, "sampler": 0}],
               reference, [glb.factor_material(baked(base, red, strength))])


def generate_valid(output):
    """wrap、strength、UV、mip、alpha、照明独立性を確認する。"""
    pair_for_constant(output, "occlusion-default", (255, 0, 255, 0))

    no_texture = material(base=(0.8, 0.8, 0.8))
    glb.write_glb(output, "occlusion-no-texture", [glb.quad()], [no_texture])
    glb.write_glb(output, "occlusion-no-texture-reference", [glb.quad()],
                  [glb.factor_material((0.8, 0.8, 0.8))])

    base = (0.8, 0.8, 0.8)
    values = (0, 128, 255)
    colors = ((0, 255, 0, 255), (128, 255, 0, 255), (255, 0, 255, 0))
    bounds = ((-0.9, -0.3, -0.65, 0.65), (-0.3, 0.3, -0.65, 0.65),
              (0.3, 0.9, -0.65, 0.65))
    image = glb.make_png([list(colors)])
    actual_quads = [glb.quad(0, uv=(index / 3.0 + 0.1, 0.5), bounds=rect,
                             width=3, height=1)
                    for index, rect in enumerate(bounds)]
    references = [glb.quad(index, bounds=rect) for index, rect in enumerate(bounds)]
    write_pair(output, "occlusion-r-values", actual_quads,
               [material(base, 0)] * 3, [image], [{"minFilter": 9728,
               "magFilter": 9728}], [{"source": 0, "sampler": 0}], references,
               [glb.factor_material(baked(base, value / 255.0, 1.0))
                for value in values])

    for name, strength in (("occlusion-strength-0", 0.0),
                           ("occlusion-strength-half", 0.5),
                           ("occlusion-strength-1", 1.0)):
        pair_for_constant(output, name, (64, 255, 0, 128), strength)

    pattern_pixels = ((0, 255, 0, 255), (64, 0, 255, 0),
                      (128, 255, 0, 255), (255, 0, 255, 0))
    pattern = glb.make_png([list(pattern_pixels[:2]), list(pattern_pixels[2:])])
    quads = []
    refs = []
    ref_materials = []
    quadrant_bounds = ((-0.9, 0.0, 0.0, 0.65), (0.0, 0.9, 0.0, 0.65),
                       (-0.9, 0.0, -0.65, 0.0), (0.0, 0.9, -0.65, 0.0))
    uv1_values = ((0.25, 0.25), (0.75, 0.25), (0.25, 0.75), (0.75, 0.75))
    for index, (rect, uv) in enumerate(zip(quadrant_bounds, uv1_values)):
        quad = glb.quad(0, bounds=rect, uv1=(uv,) * 4, width=2, height=2)
        quad["uv0"] = glb.quad_uv(rect, (0.5, 0.5), width=2, height=2)
        quads.append(quad)
        refs.append(glb.quad(index, bounds=rect))
        ref_materials.append(glb.factor_material(
            baked(base, pattern_pixels[index][0] / 255.0, 1.0)))
    write_pair(output, "occlusion-uv1-pattern", quads,
               [material(base, 0, texcoord=1)], [pattern], [{}],
               [{"source": 0, "sampler": 0}], refs, ref_materials)

    shared = glb.solid_png((128, 255, 0, 255))
    shared_textures = [{"source": 0, "sampler": 0}]
    shared_material = material((1.0, 1.0, 1.0), 0)
    shared_material["pbrMetallicRoughness"].update({
        "baseColorTexture": {"index": 0}, "metallicFactor": 0.0,
        "roughnessFactor": 1.0,
        "metallicRoughnessTexture": {"index": 0}})
    write_pair(output, "occlusion-shared-roles", [glb.quad()], [shared_material],
               [shared], [{}], shared_textures, [glb.quad()],
               [glb.factor_material((glb.srgb_to_linear(128) * 128 / 255.0,
                                     128 / 255.0, 0.0))])

    quadrant = glb.make_png([[(0, 255, 0, 0), (255, 0, 255, 255)],
                             [(64, 255, 255, 0), (128, 0, 0, 255)]])
    uv = (1.25, 0.75)
    left = (-0.9, 0.0, -0.65, 0.65)
    right = (0.0, 0.9, -0.65, 0.65)
    materials = [material(base, 0, sampler={"wrapS": 10497, "wrapT": 10497,
                                             "minFilter": 9728, "magFilter": 9728}),
                 material(base, 1, sampler={"wrapS": 33071, "wrapT": 33071,
                                             "minFilter": 9728, "magFilter": 9728})]
    quads = [glb.quad(0, uv=uv, bounds=left, sample=(250, 240), width=2, height=2),
             glb.quad(1, uv=uv, bounds=right, sample=(390, 240), width=2, height=2)]
    refs = [glb.quad(0, bounds=left, sample=(250, 240)),
            glb.quad(1, bounds=right, sample=(390, 240))]
    # Repeat at (0.25, 0.75) selects bottom-left (R=64); clamp selects bottom-right (R=128).
    colors = [baked(base, 64 / 255.0, 1.0), baked(base, 128 / 255.0, 1.0)]
    write_pair(output, "occlusion-sampler-mixed", quads, materials, [quadrant],
               [{"wrapS": 10497, "wrapT": 10497, "minFilter": 9728, "magFilter": 9728},
                {"wrapS": 33071, "wrapT": 33071, "minFilter": 9728, "magFilter": 9728}],
               [{"source": 0, "sampler": 0}, {"source": 0, "sampler": 1}],
               refs, [glb.factor_material(colors[0]), glb.factor_material(colors[1])])
    default_repeat_actual = material(base, 0)
    write_pair(output, "occlusion-default-repeat",
               [glb.quad(0, uv=uv, width=2, height=2)],
               [default_repeat_actual], [quadrant], [{}],
               [{"source": 0, "sampler": 0}], [glb.quad()],
               [glb.factor_material(baked(base, 64 / 255.0, 1.0))])

    strength_image = glb.solid_png((64, 0, 255, 0))
    strengths = [material(base, 0, strength=0.0), material(base, 0, strength=1.0)]
    refs = [glb.quad(0, bounds=left, sample=(250, 240)),
            glb.quad(1, bounds=right, sample=(390, 240))]
    write_pair(output, "occlusion-strength-mixed",
               [glb.quad(0, bounds=left, sample=(250, 240)),
                glb.quad(1, bounds=right, sample=(390, 240))], strengths,
               [strength_image], [{}], [{"source": 0, "sampler": 0}], refs,
               [glb.factor_material(base), glb.factor_material(
                   baked(base, 64 / 255.0, 1.0))])

    checker = glb.make_png([[(0, 255, 0, 255) if (x // 4) % 2 == 0 else
                             (255, 0, 255, 0) for x in range(64)]
                            for _ in range(64)])
    rho = 256.0
    mip_quad = glb.quad(0, rho_u=rho, width=64)
    mip_sampler = {"minFilter": 9987, "magFilter": 9729,
                   "wrapS": 33071, "wrapT": 33071}
    write_pair(output, "occlusion-mip-9987", [mip_quad], [material(base, 0)],
               [checker], [mip_sampler], [{"source": 0, "sampler": 0}],
               [mip_quad], [glb.factor_material(baked(base, 128 / 255.0, 1.0))])
    write_pair(output, "occlusion-mip-9987-no-mip", [mip_quad],
               [material(base, 0)], [checker],
               [{"minFilter": 9729, "magFilter": 9729, "wrapS": 33071,
                 "wrapT": 33071}], [{"source": 0, "sampler": 0}],
               [mip_quad], [glb.factor_material(baked(base, 128 / 255.0, 1.0))])

    mask_image = glb.make_png([[(255, 255, 255, 0), (255, 255, 255, 255)]])
    ao_image = glb.solid_png((128, 0, 0, 255))
    mask_quad = glb.quad()
    mask_quad["uv0"] = ((0.0, 1.0), (1.0, 1.0), (1.0, 0.0), (0.0, 0.0))
    mask_mat = material(base, 1, mask=True)
    mask_mat["pbrMetallicRoughness"]["baseColorTexture"] = {"index": 0}
    write_pair(output, "occlusion-mask", [mask_quad], [mask_mat],
               [mask_image, ao_image],
               [{"wrapS": 33071, "wrapT": 33071,
                 "minFilter": 9728, "magFilter": 9728}],
               [{"source": 0, "sampler": 0}, {"source": 1}],
               [glb.quad(bounds=(0.0, 0.9, -0.65, 0.65))],
               [glb.factor_material(baked(base, 128 / 255.0, 1.0))])

    # ambient=0時のAOはdirect lightとemissionを変化させない。
    direct_base = (0.6, 0.4, 0.2)
    direct_image = glb.solid_png((0, 255, 0, 255))
    write_pair(output, "occlusion-direct-only", [glb.quad()],
               [material(direct_base, 0)], [direct_image], [{}],
               [{"source": 0, "sampler": 0}], [glb.quad()],
               [glb.factor_material(direct_base)])
    emission_mat = material((0.0, 0.0, 0.0), 0, emission=(0.5, 0.25, 0.75))
    emission_ref = {"pbrMetallicRoughness": {
        "baseColorFactor": [0.0, 0.0, 0.0, 1.0], "metallicFactor": 0.0,
        "roughnessFactor": 1.0}, "emissiveFactor": [0.5, 0.25, 0.75]}
    write_pair(output, "occlusion-emission-only", [glb.quad()], [emission_mat],
               [glb.solid_png((0, 0, 0, 255))], [{}],
               [{"source": 0, "sampler": 0}], [glb.quad()], [emission_ref])

    mixed_base = (0.6, 0.4, 0.2)
    mixed_emission = (0.1, 0.2, 0.3)
    mixed_actual = material(mixed_base, 0, strength=0.5,
                            emission=mixed_emission)
    mixed_reference = {"pbrMetallicRoughness": {
        "baseColorFactor": list(mixed_base) + [1.0], "metallicFactor": 0.0,
        "roughnessFactor": 1.0}, "emissiveFactor": list(mixed_emission)}
    write_pair(output, "occlusion-mixed-lighting", [glb.quad()],
               [mixed_actual], [glb.solid_png((0, 0, 0, 255))], [{}],
               [{"source": 0, "sampler": 0}], [glb.quad()],
               [mixed_reference])

    stress_image = glb.solid_png((96, 255, 0, 0))
    write_pair(output, "occlusion-stress", [glb.quad()], [material(base, 0)],
               [stress_image], [{}], [{"source": 0, "sampler": 0}],
               [glb.quad()], [glb.factor_material(baked(base, 96 / 255.0, 1.0))])


def generate_negative(output):
    """壊れたUV、変換、strength、texture/image参照を作る。"""
    image = glb.solid_png((128, 128, 128, 255))
    quad = glb.quad()
    missing = glb.quad()
    missing["uv0"] = ((0.5, 0.5),) * 4
    bad = material((0.8, 0.8, 0.8), 0, texcoord=1)
    glb.write_glb(output, "occlusion-missing-uv", [missing], [bad], [image],
                  [{}], [{"source": 0, "sampler": 0}])
    transform = material((0.8, 0.8, 0.8), 0)
    transform["occlusionTexture"]["extensions"] = {
        "KHR_texture_transform": {"offset": [0.1, 0.2]}}
    glb.write_glb(output, "occlusion-transform", [quad], [transform], [image],
                  [{}], [{"source": 0, "sampler": 0}],
                  ("KHR_texture_transform",))
    for name, strength in (("occlusion-invalid-strength-low", -0.1),
                           ("occlusion-invalid-strength-high", 1.1),
                           ("occlusion-invalid-strength-overflow", 1.0e39)):
        bad = material((0.8, 0.8, 0.8), 0, strength=strength)
        glb.write_glb(output, name, [quad], [bad], [image], [{}],
                      [{"source": 0, "sampler": 0}])
    bad_texture = material((0.8, 0.8, 0.8), 9)
    glb.write_glb(output, "occlusion-invalid-texture-ref", [quad], [bad_texture],
                  [image], [{}], [{"source": 0, "sampler": 0}])
    glb.write_glb(output, "occlusion-invalid-image-ref", [quad],
                  [material((0.8, 0.8, 0.8), 0)], [image], [{}],
                  [{"source": 9, "sampler": 0}])


def generate(output):
    """全fixtureを出力先へ生成する。"""
    output.mkdir(parents=True, exist_ok=True)
    generate_valid(output)
    generate_negative(output)
    return len(tuple(output.glob("occlusion-*.glb")))


def main():
    """出力先へGLB fixtureを生成する。"""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args()
    print(generate(args.output_dir))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
