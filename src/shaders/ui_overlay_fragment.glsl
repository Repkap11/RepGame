#version 300 es

precision highp float;
precision lowp sampler2DArray;

uniform sampler2DArray u_Texture;

flat in uint v_is_block;
in vec3 v_TexCoordBlock;
in vec4 v_Tint;

layout(location = 0) out vec4 color;

void main() {
    if(v_Tint.a == 0.0f) {
        discard;
    }
    if(v_is_block != 0u) {
        color = texture(u_Texture, v_TexCoordBlock);
        color *= v_Tint;
    } else {
        color = v_Tint;
    }
    if(color.a == 0.0f) {
        discard;
    }
}
