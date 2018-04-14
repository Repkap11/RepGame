#ifndef HEADER_INDEX_BUFFER_H
#define HEADER_INDEX_BUFFER_H

typedef struct {
    unsigned int mRendererId;
} IndexBuffer;

void index_buffer_init(IndexBuffer* indexBuffer, unsigned int* data, unsigned int count);
void index_buffer_destroy(const IndexBuffer* indexBuffer);
void index_buffer_bind(const IndexBuffer* indexBuffer);
void index_buffer_unbind(const IndexBuffer* indexBuffer);

#endif