#include <stdlib.h>

#include "common/utils/linked_list.hpp"
#include "common/RepGame.hpp"

// Naive linked list implementation from https://gist.github.com/wayling/3877624

LinkedList *linked_list_create( ) {
    LinkedList *l = ( LinkedList * )malloc( sizeof( LinkedList ) );
    l->count = 0;
    l->head = NULL;
    l->tail = NULL;
#if defined( REPGAME_WASM ) || defined( REPGAME_WINDOWS )
#else
    pthread_mutex_init( &( l->mutex ), NULL );
#endif
    return l;
}

void linked_list_free( LinkedList *l ) {
    LinkedListItem *li, *tmp;
#if defined( REPGAME_WASM ) || defined( REPGAME_WINDOWS )
#else
    pthread_mutex_lock( &( l->mutex ) );
#endif

    if ( l != NULL ) {
        li = l->head;
        while ( li != NULL ) {
            tmp = li->next;
            free( li );
            li = tmp;
        }
    }
#if defined( REPGAME_WASM ) || defined( REPGAME_WINDOWS )
#else
    pthread_mutex_unlock( &( l->mutex ) );
    pthread_mutex_destroy( &( l->mutex ) );
#endif
    free( l );
}

// Add element to tail
void linked_list_add_element( LinkedList *l, LinkedListValue value ) {
    LinkedListItem *li;
#if defined( REPGAME_WASM ) || defined( REPGAME_WINDOWS )
#else
    pthread_mutex_lock( &( l->mutex ) );
#endif

    li = ( LinkedListItem * )malloc( sizeof( LinkedListItem ) );
    li->value = value;
    if ( l->tail != NULL ) {
        l->tail->prev = li;
    }
    li->next = l->tail;
    li->prev = NULL;

    if ( l->head == NULL ) {
        l->head = li;
    }
    l->tail = li;
    l->count++;
#if defined( REPGAME_WASM ) || defined( REPGAME_WINDOWS )
#else
    pthread_mutex_unlock( &( l->mutex ) );
#endif
}

// Pop off head
LinkedListValue linked_list_pop_element( LinkedList *l ) {
    LinkedListValue result;
    result.valid = 0;
#if defined( REPGAME_WASM ) || defined( REPGAME_WINDOWS )
#else
    pthread_mutex_lock( &( l->mutex ) );
#endif

    LinkedListItem *li = l->head;
    if ( li != NULL ) {
        if ( li->prev != NULL ) {
            li->prev->next = NULL;
        } else {
            l->tail = NULL;
        }
        l->head = li->prev;
        l->count--;
        result = li->value;
        result.valid = 1;
        free( li );
    }
#if defined( REPGAME_WASM ) || defined( REPGAME_WINDOWS )
#else
    pthread_mutex_unlock( &( l->mutex ) );
#endif
    return result;
}