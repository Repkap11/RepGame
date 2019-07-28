#ifndef HEADER_ITEM_H
#define HEADER_ITEM_H

typedef struct {
    BlockID id;
    int item_stack;

    BlockCoords item_placement;
    VertexBuffer vb_item_placement;

    CubeFace item_shape;
    VertexBuffer vb_item_shape;
} ItemBlock;

void item_init( Block *block );
void item_update( );
void item_cleanup( Item *item );

#ifndef HEADER_ITEM_H