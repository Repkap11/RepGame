#ifndef HEADER_BLOCK_H
#define HEADER_BLOCK_H

#include "block_definitions.h"

typedef struct {
    BlockDefinition *blockDef;
    struct {
        int top;
        int bottom;
        int left;
        int right;
        int front;
        int back;
    } draw_sides;

} Block;

#endif