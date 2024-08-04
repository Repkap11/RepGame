#version 300 es
uniform mat4 u_MVP;

#define ISO_FACE_TOP 0u
#define ISO_FACE_FRONT 1u
#define ISO_FACE_RIGHT 2u

// From UIOverlayVertex
layout(location = 0) in vec2 element_position;
layout(location = 1) in vec2 element_texture_coords;
layout(location = 2) in uint element_is_isometric;
layout(location = 3) in uint element_face_type;

// From UIOverlayInstance
layout(location = 4) in vec3 instance_position;
layout(location = 5) in vec2 instance_size;
layout(location = 6) in uint instance_is_block;
layout(location = 7) in uint instance_is_isometric;
layout(location = 8) in vec3 instance_id_isos;
layout(location = 9) in vec4 instance_tint;

flat out uint v_is_block;
out vec3 v_TexCoordBlock;
out vec4 v_Tint;

void main() {
    gl_Position = u_MVP * vec4(instance_position.xy + element_position * instance_size, instance_position.z, 1);
    v_TexCoordBlock = vec3(element_texture_coords, instance_id_isos[element_face_type]);
    v_is_block = instance_is_block;

    if(element_is_isometric != instance_is_isometric) {
        v_Tint = vec4(0, 0, 0, 0);
    } else {
        if(instance_is_block != 0u) {
            float face_light;
            if(element_face_type == ISO_FACE_TOP) {
                face_light = 1.0f;
            } else if(element_face_type == ISO_FACE_FRONT) {
                face_light = 0.8f;
            } else {
                face_light = 0.6f;
            }
            v_Tint = vec4(instance_tint.rgb * face_light, instance_tint.a);
        } else {
            v_Tint = instance_tint;
        }
    }
}