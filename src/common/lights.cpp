#include "common/lights.hpp"
#include "common/RepGame.hpp"

void lights_init( Lights *lights ) {
}

void lights_remove_light( Lights *lights, int light_id ) {
    std::vector<LightPosition> &positions = lights->light_positions;
    std::vector<LightColor> &colors = lights->light_colors;
    int removed_index = lights->light_index_lookup[ light_id ];
    lights->light_index_lookup[ light_id ] = -1;

    LightPosition *swapped_light_position = &positions.back( );
    LightColor *swapped_light_color = &colors.back( );
    // ObjectPosition *removed_light = &positions[ removed_index ];
    int swapped_index = positions.size( ) - 1;
    // swapped_light->id = removed_index;

    std::swap( positions[ removed_index ], positions.back( ) );
    std::swap( colors[ removed_index ], colors.back( ) );

    lights->light_index_lookup[ lights->last_light_id ] = removed_index;
    positions.pop_back( );
    colors.pop_back( );
    pr_debug( "Removed light:%d", light_id );
}

void lights_add_light( Lights *lights, unsigned int light_id ) {
    if ( light_id >= MAX_LIGHT_COUNT ) {
        pr_debug( "Light id too high, got %d, max:%d", light_id, MAX_LIGHT_COUNT );
        return;
    }
    pr_debug( "Added new light:%d", light_id );
    std::vector<LightPosition> &positions = lights->light_positions;
    std::vector<LightColor> &colors = lights->light_colors;
    int next_index = positions.size( );
    lights->light_index_lookup[ light_id ] = next_index;
    positions.emplace_back( );
    colors.emplace_back( );
    lights->last_light_id = light_id;
}

void lights_update_position( Lights *lights, int light_id, float x, float y, float z ) {
    int light_index = lights->light_index_lookup[ light_id ];
    if ( light_index < 0 ) {
        pr_debug( "Not adding light" );
        return;
    }
    LightPosition *light = &lights->light_positions[ light_index ];
    light->x = x;
    light->y = y;
    light->z = z;
}

void lights_update_color( Lights *lights, int light_id, float r, float g, float b ) {
    int light_index = lights->light_index_lookup[ light_id ];
    if ( light_index < 0 ) {
        pr_debug( "Not adding light" );
        return;
    }
    LightColor *lightColor = &lights->light_colors[ light_index ];
    lightColor->r = r;
    lightColor->g = g;
    lightColor->b = b;
}

void lights_set_uniforms( Lights *lights, Shader *shader ) {
    // pr_debug( "Uploading num_lights:%d", ( int )lights->light_positions.size( ) );
    shader_set_uniform3fv( shader, "u_LightPositions", ( float * )&lights->light_positions[ 0 ], lights->light_positions.size( ) );
    shader_set_uniform3fv( shader, "u_LightColors", ( float * )&lights->light_colors[ 0 ], lights->light_colors.size( ) );

    // asset( lights->light_positions.size( ) == lights->light_colors.size( ) );
    shader_set_uniform1ui( shader, "u_NumLights", lights->light_positions.size( ) );
}

void lights_cleanup( Lights *lights ) {
}
