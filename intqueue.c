#include <stdlib.h>
#include <pthread.h>

#include "intqueue.h"

intqueue_t* createQ(){
    intqueue_t* q = malloc(sizeof(intqueue_t));
    if ( !q )
        return NULL;

    q->head = q->tail = NULL;
    if ( pthread_mutex_init(&q->qlock, NULL) != 0 )
        return NULL;
    if ( pthread_cond_init(&q->qcond, NULL) != 0 )
        return NULL;

    return q;
}

int isEmptySinchronized(intqueue_t* q){
    if ( pthread_mutex_lock(&q->qlock) != 0 )
        return -1;

    int empty = ( q == NULL || q->head == NULL ) ? 1 : 0;

    if ( pthread_mutex_unlock(&q->qlock) != 0 )
        return -1;
    
    return empty;
}

int enqueue(intqueue_t* q, int fd){
    if ( fd < 0 )
        return -1;

    elem_t* new = malloc(sizeof(elem_t));
    if ( !new )
        return -1;

    new->fd = fd;
    new->next = NULL;

    if ( pthread_mutex_lock(&q->qlock) != 0 )
        return -1;

    if ( q->head == NULL )
        q->head = q->tail = new;
    else{
        q->tail->next = new;
        q->tail = new;
    }

    if ( pthread_mutex_unlock(&q->qlock) != 0 )
        return -1;

    if ( pthread_cond_signal(&q->qcond) != 0 )
        return -1;

    return 0;
}

int dequeue(intqueue_t* q){
    if ( pthread_mutex_lock(&q->qlock) != 0 )
        return -1;

    while ( q->head == NULL )
        pthread_cond_wait(&q->qcond, &q->qlock);

    int first = q->head->fd;
    if ( q->head == q->tail ){
        // c'è un unico elemento in coda
        free(q->head);
        q->head = q->tail = NULL;
    }
    else{
        // c'è più di un elemento in coda
        elem_t* tmp = q->head;
        q->head = q->head->next;
        free(tmp);
    }
    if ( pthread_mutex_unlock(&q->qlock) != 0 )
        return -1;

    return first;
}

int getFirst(intqueue_t* q){

    if ( pthread_mutex_lock(&q->qlock) != 0 )
        return -1;

    if ( q->head == NULL ){
        if ( pthread_mutex_unlock(&q->qlock) != 0 )
            return -1;
        return 0;
    }
    
    int first = q->head->fd;
    if ( pthread_mutex_unlock(&q->qlock) != 0 )
        return -1;

    return first;
}

int search(intqueue_t* q, int fd){

    if ( pthread_mutex_lock(&q->qlock) != 0 )
        return -1;

    elem_t* corr = q->head;
    while ( corr ){
        if ( corr->fd == fd ){
            if ( pthread_mutex_unlock(&q->qlock) != 0 )
                return -1;

            return 1;
        }
        corr = corr->next;
    }

    if ( pthread_mutex_unlock(&q->qlock) != 0 )
        return -1;

    return 0;
}

int deleteElem(intqueue_t* q, int fd){

    if ( pthread_mutex_lock(&q->qlock) != 0 )
        return -1;

    elem_t* corr = q->head, *prec = corr;
    while ( corr ){
        if ( corr->fd == fd ){
            if ( corr == q->head ){
                // L'elemento da eliminare è il primo,
                // comprende il caso in cui sia anche l'unico
                q->head = q->head->next;
                if ( q->tail == corr )
                    q->tail = q->head; // Ovvero NULL

            }
            if ( corr == q->tail ){
                // L'elemento da eliminare è l'ultimo
                q->tail = prec;
                q->tail->next = NULL; // Elimino il puntatore all'ex ultimo
            }
            else
                // L'elemento da eliminare sta nel mezzo
                prec->next = corr->next;

            free(corr);
            if ( pthread_mutex_unlock(&q->qlock) != 0 )
                return -1;

            return 0;
        }
        else{
            prec = corr;
            corr = corr->next;
        }
    }

    if ( pthread_mutex_unlock(&q->qlock) != 0 )
        return -1;

    return 0;
}

int deleteQ(intqueue_t* q){

    if ( pthread_mutex_lock(&q->qlock) != 0 )
        return -1;
    if ( q->head == NULL ){
        if ( pthread_mutex_unlock(&q->qlock) != 0 )
            return -1;
        return 0;
    }

    while ( q->head != q->tail ){
        elem_t* tmp = q->head;
        q->head = q->head->next;

        free(tmp);
    }
    free(q->head);
    if ( pthread_mutex_unlock(&q->qlock) != 0 )
        return -1;

    free(q);

    return 0;
}
