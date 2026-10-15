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
layout(location = 4) in vec3 element_cube_position;

// From UIOverlayInstance
layout(location = 5) in vec3 instance_position;
layout(location = 6) in vec2 instance_size;
layout(location = 7) in uint instance_is_block;
layout(location = 8) in uint instance_is_isometric;
layout(location = 9) in vec3 instance_id_isos;
layout(location = 10) in vec4 instance_tint;
layout(location = 11) in vec3 instance_block_scale;
layout(location = 12) in vec3 instance_block_offset;
layout(location = 13) in vec3 instance_block_tex_offset;

flat out uint v_is_block;
out vec3 v_TexCoordBlock;
out vec4 v_Tint;

void main() {

    if(element_is_isometric != instance_is_isometric) {
        gl_Position = vec4(0);
        return;
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
    if(v_Tint.a == 0.0f) {
        gl_Position = vec4(0);
    } else {
        vec2 icon_position = element_position;
        vec2 face_uv = element_texture_coords;
        if(element_is_isometric != 0u) {
            // Project the cube corner through the block's scale/offset with the
            // same isometric projection the icon quads were authored in:
            // x and z step half a unit right/left-down, y steps 5/8 up.
            vec3 scale_f = instance_block_scale / 16.0f;
            vec3 offset_f = instance_block_offset / 16.0f;
            vec3 tex_offset_f = instance_block_tex_offset / 16.0f;
            vec3 cube = element_cube_position * scale_f + offset_f;
            icon_position = vec2(0.5f * cube.x - 0.5f * cube.z + 0.5f,     //
                                 -0.1875f * (cube.x + cube.z) + 0.625f * cube.y + 0.375f);

            // Clip the UV window to the block's sub-size exactly like
            // chunk_vertex.glsl does for an unrotated block.
            vec2 face_scale;
            vec2 face_shift;
            if(element_face_type == ISO_FACE_TOP) {
                face_scale = scale_f.xz;
                face_shift = tex_offset_f.xz;
            } else if(element_face_type == ISO_FACE_FRONT) {
                face_scale = scale_f.xy;
                face_shift = tex_offset_f.zy;
            } else { // ISO_FACE_RIGHT
                face_scale = scale_f.zy;
                face_shift = tex_offset_f.xy;
            }
            face_uv = face_uv * face_scale - face_shift;
        }
        gl_Position = u_MVP * vec4(instance_position.xy + icon_position * instance_size, instance_position.z, 1);
        v_TexCoordBlock = vec3(face_uv, instance_id_isos[element_face_type]);
        v_is_block = instance_is_block;
    }
}
