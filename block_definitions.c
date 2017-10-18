#include "block_definitions.h"
#include <stdlib.h>

BlockDefinition *block_definitions;
void block_definitions_initilize_definitions( ) {
    block_definitions = malloc( LAST_TYPE * sizeof( BlockDefinition ) );
    // block_definitions[ AIR ].
}

BlockDefinition *block_definition_get_definition( BlockID blockID ) {
    return &block_definitions[ blockID ];
}

void block_definitions_free_definitions( ) {
    free( block_definitions );
}