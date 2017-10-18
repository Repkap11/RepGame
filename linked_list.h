#ifndef HEADER_LINKED_LIST_H
#define HEADER_LINKED_LIST_H

#include <pthread.h>

typedef struct _list_item {
    void *value;
    struct _list_item *prev;
    struct _list_item *next;
} LinkedListItem;

typedef struct {
    int count;
    LinkedListItem *head;
    LinkedListItem *tail;
    pthread_mutex_t mutex;
} LinkedList;

LinkedList *list_create( );
void list_free( LinkedList *l );

LinkedListItem *linked_list_add_element( LinkedList *l, void *ptr );
int linked_list_remove_element( LinkedList *l, void *ptr );
void linked_list_each_element( LinkedList *l, int ( *func )( LinkedListItem * ) );

#endif
