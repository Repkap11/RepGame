#include "linked_list.h"
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

LinkedListItem *linked_list_add_element( LinkedList *l, void *ptr ) {
    LinkedListItem *li;

    pthread_mutex_lock( &( l->mutex ) );

    li = ( LinkedListItem * )malloc( sizeof( LinkedListItem ) );
    li->value = ptr;
    li->next = NULL;
    li->prev = l->tail;

    if ( l->tail == NULL ) {
        l->head = l->tail = li;
    } else {
        l->tail = li;
    }
    l->count++;

    pthread_mutex_unlock( &( l->mutex ) );

    return li;
}

int linked_list_pop_element( LinkedList *l, void *ptr ) {
    int result = 0;
    LinkedListItem *li = l->head;

    pthread_mutex_lock( &( l->mutex ) );

    if ( li != NULL ) {
        if ( li->prev == NULL ) {
            l->head = li->next;
        } else {
            li->prev->next = li->next;
        }

        if ( li->next == NULL ) {
            l->tail = li->prev;
        } else {
            li->next->prev = li->prev;
        }
        l->count--;
        free( li );
        result = 1;
    }

    pthread_mutex_unlock( &( l->mutex ) );

    return result;
}

void list_each_element( LinkedList *l, int ( *func )( LinkedListItem * ) ) {
    LinkedListItem *li;

    pthread_mutex_lock( &( l->mutex ) );

    li = l->head;
    while ( li != NULL ) {
        if ( func( li ) == 1 ) {
            break;
        }
        li = li->next;
    }

    pthread_mutex_unlock( &( l->mutex ) );
}