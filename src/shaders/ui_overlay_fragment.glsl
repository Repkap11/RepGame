#version 300 es

precision highp float;
precision lowp sampler2DArray;

uniform sampler2DArray u_Texture;

flat in uint v_is_block;
in vec3 v_TexCoordBlock;
in vec4 v_Tint;
flat in vec2 v_tex_min;
flat in vec2 v_tex_max;

layout(location = 0) out vec4 color;

void main() {
    if(v_Tint.a == 0.0f) {
        discard;
    }
    if(v_is_block != 0u) {
        // Edge-clamp inside this face's texture region, same as
        // chunk_fragment.glsl: bilinear taps at the boundary bleed in
        // transparent-border texels (chest edges, slab bands) and hit the
        // alpha discard below, leaving background-colored hairlines on the
        // icon. Interior texels are untouched; only the outermost half-texel
        // is pinned to the border texel center.
        vec2 working_lo = v_tex_min + (0.5f / 16.0f);
        vec2 working_hi = max(v_tex_max - (0.5f / 16.0f), working_lo);
        vec2 working = clamp(v_TexCoordBlock.xy, working_lo, working_hi);
        // The UI pass binds the atlas CLAMP_TO_EDGE (the world pass uses
        // REPEAT), so wrap inside the clamped window explicitly: faces whose
        // tex_offset pushes the crop window past the tile edge (PORTAL's
        // side faces sample v in [0.5, 1.5]) must roll over like the world
        // mesh instead of smearing the clamped edge row into stripes. fract
        // is identity for windows already inside the tile. Gradients use the
        // unwrapped UV so the wrap seam doesn't pick a degenerate mip.
        vec2 uv_dx = dFdx(v_TexCoordBlock.xy);
        vec2 uv_dy = dFdy(v_TexCoordBlock.xy);
        color = textureGrad(u_Texture, vec3(fract(working), v_TexCoordBlock.z), uv_dx, uv_dy);
        color *= v_Tint;
    } else {
        color = v_Tint;
    }
    if(color.a == 0.0f) {
        discard;
    }
}
