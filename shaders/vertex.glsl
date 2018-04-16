#version 330 core
uniform vec4 u_CameraPosition;
uniform vec4 u_CameraUnit;
uniform vec4 u_LightPosition;

layout( location = 0 ) in vec4 position;
layout( location = 1 ) in vec2 texCoord;

out vec2 v_TexCoord;

void main( ) {
    gl_Position = position;
    v_TexCoord = texCoord;
}
