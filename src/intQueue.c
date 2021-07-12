
#include <stdlib.h>
#include <assert.h>
#include "intQueue.h"

Queue_t* initQueue(){
    Queue_t *q = malloc(sizeof(Queue_t));
    if ( !q )
        return NULL;

    q->head = q->tail = NULL;
    q->len = 0;
    if ( pthread_mutex_init(&q->qlock, NULL) != 0 )
        return NULL;
    if ( pthread_cond_init(&q->qcond, NULL) != 0 )
        return NULL;

    return q;
}

int isEmpty(Queue_t* q){
    return (q == NULL || q->head == NULL );
}

int push(Queue_t *q, int elem){
    if ( elem < 0 )
        return -1;

    Node_t* new = malloc(sizeof(Node_t));
    if ( !new )
        return -1;

    new->data = elem;
    new->next = NULL;

    if ( pthread_mutex_lock(&q->qlock) != 0 )
        return -1;

    if ( q->head == NULL )
        q->head = q->tail = new;
    else{
        q->tail->next = new;
        q->tail = new;
    }
    q->len++;

    if ( pthread_mutex_unlock(&q->qlock) != 0 )
        return -1;

    if ( pthread_cond_signal(&q->qcond) != 0 )
        return -1;

    return 0;
}

int pop(Queue_t *q){
    if ( pthread_mutex_lock(&q->qlock) != 0 )
        return -1;

    while ( isEmpty(q) )
        pthread_cond_wait(&q->qcond, &q->qlock);

    int elem = q->head->data;
    if ( q->head == q->tail ){
        // c'è un unico elemento in coda
        free(q->head);
        q->head = q->tail = NULL;
    }
    else{
        // c'è più di un elemento in coda
        Node_t* tmp = q->head;
        q->head = q->head->next;
        free(tmp);
    }
    q->len--;
    assert(q->len >= 0);
    if ( pthread_mutex_unlock(&q->qlock) != 0 )
        return -1;

    return elem;
}

size_t length(Queue_t* q){
    size_t len;
    pthread_mutex_destroy(&q->qlock);
    assert(q->len >= 0);
    len = q->len;
    pthread_mutex_unlock(&q->qlock);

    return len;
}

int deleteQueue(Queue_t *q){
    if ( pthread_mutex_lock(&q->qlock) != 0 )
        return -1;
    if ( isEmpty(q) ){
        if ( pthread_mutex_unlock(&q->qlock) != 0 )
            return -1;
        return 0;
    }

    while ( q->head != q->tail ){
        int elem = q->head->data;
        Node_t* tmp = q->head;
        q->head = q->head->next;

        free(tmp);
    }
    // rimane solo un elemento (ex ultimo)
    int elem = q->head->data;
    Node_t* tmp = q->head;

    free(tmp);

    q->head = q->tail = NULL;
    if ( pthread_mutex_unlock(&q->qlock) != 0 )
        return -1;

    pthread_mutex_destroy(&q->qlock);
    pthread_cond_destroy(&q->qcond);
    free(q->head);
    free(q);

    return 0;
}
