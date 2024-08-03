#ifndef HEADER_INDEX_BUFFER_H
#define HEADER_INDEX_BUFFER_H

typedef struct {
    unsigned int mRendererId;
    unsigned int count;
} IndexBuffer;

void index_buffer_init(IndexBuffer* indexBuffer);
void index_buffer_set_data(IndexBuffer* indexBuffer, const unsigned int* data, unsigned int count);
void index_buffer_destroy(const IndexBuffer* indexBuffer);
void index_buffer_bind(const IndexBuffer* indexBuffer);
void index_buffer_unbind(const IndexBuffer* indexBuffer);

#endif