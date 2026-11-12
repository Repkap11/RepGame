#version 300 es

uniform mat4 u_MVP;

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec2 a_TexCoord;
layout(location = 2) in vec4 a_Tint;

out vec2 v_TexCoord;
out vec4 v_Tint;
out vec3 v_world_coords;

void main() {
    v_TexCoord = a_TexCoord;
    v_Tint = a_Tint;
    // Rebased world position (camera-relative), like chunk_vertex's
    // v_world_coords — the fragment fogs by distance to u_CameraPos.
    v_world_coords = a_Position;
    gl_Position = u_MVP * vec4(a_Position, 1.0);
}
