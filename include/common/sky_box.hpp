#ifndef HEADER_SKY_BOX_H
#define HEADER_SKY_BOX_H

#include "abstract/index_buffer.hpp"
#include "abstract/vertex_buffer.hpp"
#include "abstract/vertex_array.hpp"
#include "abstract/renderer.hpp"

typedef struct {
    VertexBufferLayout vbl;
    IndexBuffer ib;
    VertexArray va;
    VertexBuffer vb;
    int vertex_size;
    Shader shader;
    Texture texture;
} SkyBox;

typedef struct {
    float x;
    float y;
    float z;
    float u;
    float v;
} SkyVertex;

void sky_box_init( SkyBox *skyBox );
void sky_box_draw( SkyBox *skyBox, Renderer *renderer, glm::mat4 &mvp_sky );

#endif