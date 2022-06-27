#ifndef HEADER_MOUSE_SELECTION_H
#define HEADER_MOUSE_SELECTION_H

typedef struct {
    IndexBuffer ib;
    VertexArray va;
    BlockCoords block;
    VertexBuffer vb_coords;
    int shouldDraw;
    VertexBuffer vb_block;
    BlockState selected_block_state;
} MouseSelection;

void mouse_selection_init( MouseSelection *mouseSelection, VertexBufferLayout *vbl_block, VertexBufferLayout *vbl_coords );
void mouse_selection_set_block( MouseSelection *mouseSelection, glm::ivec3 pos, int shouldDraw, BlockState blockState );
void mouse_selection_draw( MouseSelection *mouseSelection, Renderer *renderer, Shader *shader );
#endif