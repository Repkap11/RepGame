#version 300 es

uniform mat4 u_MVP;

layout(location = 0) in vec2 a_Position;
layout(location = 1) in vec2 a_TexCoord;
layout(location = 2) in vec4 a_Tint;

out vec2 v_TexCoord;
out vec4 v_Tint;

void main() {
    v_TexCoord = a_TexCoord;
    v_Tint = a_Tint;
    gl_Position = u_MVP * vec4(a_Position, 0.0, 1.0);
}
