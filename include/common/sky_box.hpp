#ifndef HEADER_SKY_BOX_H
#define HEADER_SKY_BOX_H

#include "abstract/index_buffer.hpp"
#include "abstract/vertex_buffer.hpp"
#include "abstract/vertex_array.hpp"
#include "abstract/renderer.hpp"

typedef struct {
    IndexBuffer ib;
    VertexArray va;
    VertexBuffer vb_vertex;
    VertexBuffer vb_position;
    int vertex_size;
    Shader shader;
    Texture texture;
} SkyBox;

void sky_box_init( SkyBox *skyBox, VertexBufferLayout *vbl_object_vertex, VertexBufferLayout *vbl_object_position );
void sky_box_draw( SkyBox *skyBox, Renderer *renderer, glm::mat4 &mvp_sky, Shader *sky_shader );
void sky_box_destroy( SkyBox *skyBox );
#endif