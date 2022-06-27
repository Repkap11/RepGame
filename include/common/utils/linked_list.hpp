#ifndef HEADER_LINKED_LIST_H
#define HEADER_LINKED_LIST_H

#include "common/chunk.hpp"
#include <pthread.h>
#include "common/RepGame.hpp"

typedef struct {
	Chunk * chunk;
	glm::ivec3 &new_chunk_pos;
	int persist;
	int valid;
} LinkedListValue;

typedef struct _list_item {
    LinkedListValue value;
    struct _list_item *prev;
    struct _list_item *next;
} LinkedListItem;

typedef struct {
    int count;
    LinkedListItem *head;
    LinkedListItem *tail;
    pthread_mutex_t mutex;
} LinkedList;

LinkedList *linked_list_create( );
void linked_list_free( LinkedList *l );

void linked_list_add_element( LinkedList *l, LinkedListValue value );
LinkedListValue linked_list_pop_element( LinkedList *l );

#endif
