#include "linked_list.h"
#include "RepGame.h"
#include <stdlib.h>

/* Naive linked list implementation from https://gist.github.com/wayling/3877624*/

LinkedList *linked_list_create( ) {
    LinkedList *l = ( LinkedList * )malloc( sizeof( LinkedList ) );
    l->count = 0;
    l->head = NULL;
    l->tail = NULL;
    pthread_mutex_init( &( l->mutex ), NULL );
    return l;
}

void linked_list_free( LinkedList *l ) {
    LinkedListItem *li, *tmp;
    pthread_mutex_lock( &( l->mutex ) );

    if ( l != NULL ) {
        li = l->head;
        while ( li != NULL ) {
            tmp = li->next;
            free( li );
            li = tmp;
        }
    }
    pthread_mutex_unlock( &( l->mutex ) );

    pthread_mutex_destroy( &( l->mutex ) );
    free( l );
}

// Add element to tail
void linked_list_add_element( LinkedList *l, LinkedListValue value ) {
    LinkedListItem *li;
    pthread_mutex_lock( &( l->mutex ) );

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
    pthread_mutex_unlock( &( l->mutex ) );
}

// Pop off head
LinkedListValue linked_list_pop_element( LinkedList *l ) {
    void *result = NULL;
    pthread_mutex_lock( &( l->mutex ) );

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
        free( li );
    }
    pthread_mutex_unlock( &( l->mutex ) );

    return result;
}