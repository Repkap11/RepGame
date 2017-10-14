#ifndef HEADER_CHUNK_H
#define HEADER_CHUNK_H

#define CHUNK_SIZE 8

typedef struct {
    int displayList;
    int blocks[ CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE ];
} Chunk;

void chunk_create_display_list( Chunk *chunk );

void chunk_draw( Chunk *chunk );

#endif