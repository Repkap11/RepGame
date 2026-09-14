#version 300 es

uniform mat4 u_MVP;

layout(location = 0) in vec2 a_Position;
layout(location = 1) in vec2 a_TexCoord;

out vec2 v_TexCoord;

void main() {
    v_TexCoord = a_TexCoord;
    gl_Position = u_MVP * vec4(a_Position, 0.0, 1.0);
}
