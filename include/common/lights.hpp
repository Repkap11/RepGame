#ifndef HEADER_LIGHTS_H
#define HEADER_LIGHTS_H

#include <vector>
#include "abstract/shader.hpp"

#define MAX_LIGHT_COUNT 20

typedef struct {
    float x, y, z;
} LightPosition;

typedef struct {
    float r, g, b;
} LightColor;

typedef struct {
    std::vector<LightPosition> light_positions;
    std::vector<LightColor> light_colors;

    int light_index_lookup[ MAX_LIGHT_COUNT ];
    unsigned int last_light_id;
} Lights;

void lights_init( Lights *lights );
void lights_add_light( Lights *lights, unsigned int light_id );
void lights_update_position( Lights *lights, int light_id, float x, float y, float z );
void lights_update_color( Lights *lights, int light_id, float r, float g, float b );
void lights_remove_light( Lights *lights, int light_id );
void lights_set_uniforms( Lights *lights, Shader *shader );
void lights_cleanup( Lights *Lights );

#endif