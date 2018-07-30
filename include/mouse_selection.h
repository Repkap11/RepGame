#ifndef HEADER_MOUSE_SELECTION_H
#define HEADER_MOUSE_SELECTION_H

typedef struct {
    IndexBuffer ib;
    VertexArray va;
    BlockCoords block;
    VertexBuffer vb_coords;
} MouseSelection;

void mouse_selection_init( MouseSelection *mouseSelection );
void mouse_selection_draw( MouseSelection *mouseSelection );

#endif