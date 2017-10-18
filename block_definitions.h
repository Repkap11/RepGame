#ifndef HEADER_BLOCK_DEFINITIONS_H
#define HEADER_BLOCK_DEFINITIONS_H

typedef struct {
    int top;
    int side;
    int bottom;
    float alpha;
    float height;
} BlockTextureMap;

typedef enum { AIR = 0, GRASS, WATER, LAST_TYPE } BlockID;

typedef struct {
    BlockID id;
    BlockTextureMap btm;
    int opacity;
} BlockDefinition;

void block_definitions_initilize_definitions( );
BlockDefinition *block_definition_get_definition( BlockID blockID );
void block_definitions_free_definitions( );

#endif