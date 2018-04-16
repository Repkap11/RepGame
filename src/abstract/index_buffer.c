#define GL_GLEXT_PROTOTYPES
#include "RepGame.h"
#include "abstract/index_buffer.h"

void index_buffer_init(IndexBuffer* indexBuffer, unsigned int* data, unsigned int count){
    indexBuffer->count = count;
    glGenBuffers(1, &indexBuffer->mRendererId);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer->mRendererId);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, count * sizeof(unsigned int), data, GL_STATIC_DRAW);
}

void index_buffer_destroy(const IndexBuffer* indexBuffer){
    glDeleteBuffers(1, &indexBuffer->mRendererId);
}

void index_buffer_bind(const IndexBuffer* indexBuffer){
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer->mRendererId);
}

void index_buffer_unbind(const IndexBuffer* indexBuffer){
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}