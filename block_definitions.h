#ifndef HEADER_BLOCK_DEFINITIONS_H
#define HEADER_BLOCK_DEFINITIONS_H

#include <GL/gl.h>

typedef struct {
    GLuint top;
    GLuint side;
    GLuint bottom;
} BlockTextureMap;

typedef enum { AIR = 0, GRASS, WATER, LAST_BLOCK_ID } BlockID;

typedef struct {
    BlockID id;
    BlockTextureMap textures;
    float alpha;
    float height;
    int special_grass_logic;
} BlockDefinition;

void block_definitions_initilize_definitions( );
BlockDefinition *block_definition_get_definition( BlockID blockID );
void block_definitions_free_definitions( );

#endif