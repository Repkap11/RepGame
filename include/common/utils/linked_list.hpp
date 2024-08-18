#pragma once

#include "common/chunk.hpp"
#include <pthread.h>
#include "common/RepGame.hpp"

struct LinkedListValue {
    Chunk *chunk;
    glm::ivec3 new_chunk_pos;
    int persist;
    int valid;
};

struct LinkedListItem {
    LinkedListValue value;
    struct LinkedListItem *prev;
    struct LinkedListItem *next;
};

struct LinkedList {
    int count;
    LinkedListItem *head;
    LinkedListItem *tail;
    pthread_mutex_t mutex;
};

LinkedList *linked_list_create( );
void linked_list_free( LinkedList *l );

void linked_list_add_element( LinkedList *l, LinkedListValue value );
LinkedListValue linked_list_pop_element( LinkedList *l );