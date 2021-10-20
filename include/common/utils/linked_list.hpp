#ifndef HEADER_LINKED_LIST_H
#define HEADER_LINKED_LIST_H

#include "common/chunk.hpp"
#if defined( REPGAME_WASM) || defined(REPGAME_WINDOWS )
#else
#include <pthread.h>
#endif
#include "common/RepGame.hpp"

typedef struct {
	Chunk * chunk;
	TRIP_STATE(int new_chunk_);
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
#if defined( REPGAME_WASM) || defined(REPGAME_WINDOWS )
#else
    pthread_mutex_t mutex;
 #endif
} LinkedList;

LinkedList *linked_list_create( );
void linked_list_free( LinkedList *l );

void linked_list_add_element( LinkedList *l, LinkedListValue value );
LinkedListValue linked_list_pop_element( LinkedList *l );

#endif
