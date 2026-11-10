#version 300 es

precision highp float;

uniform sampler2D u_Texture;

in vec2 v_TexCoord;
in vec4 v_Tint;

layout(location = 0) out vec4 color;

void main() {
    float alpha = texture(u_Texture, v_TexCoord).a;
    if (alpha == 0.0) {
        discard;
    }
    color = vec4(v_Tint.rgb, v_Tint.a * alpha);
}
