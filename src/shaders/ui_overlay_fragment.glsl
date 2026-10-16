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
        color = texture(u_Texture, vec3(working, v_TexCoordBlock.z));
        color *= v_Tint;
    } else {
        color = v_Tint;
    }
    if(color.a == 0.0f) {
        discard;
    }
}
