#version 300 es

precision highp float;

uniform sampler2D u_Texture;
uniform vec4 u_Tint;

in vec2 v_TexCoord;

layout(location = 0) out vec4 color;

void main() {
    float alpha = texture(u_Texture, v_TexCoord).a;
    if (alpha == 0.0) {
        discard;
    }
    color = vec4(u_Tint.rgb, u_Tint.a * alpha);
}
