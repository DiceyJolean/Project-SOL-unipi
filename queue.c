#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "queue.h"

Queue_t* initQueue(){
    Queue_t *q = malloc(sizeof(Queue_t));
    if ( !q )
        return NULL;

    q->head = q->tail = NULL;
    q->len = 0;

    return q;
}

int isEmpty(Queue_t* q){
    return (q == NULL || q->head == NULL );
}

int push(Queue_t *q, void* elem){
    if ( !elem )
        return -1;

    Node_t* new = malloc(sizeof(Node_t));
    if ( !new )
        return -1;

    new->data = elem;
    new->next = NULL;


    if ( q->head == NULL )
        q->head = q->tail = new;
    else{
        q->tail->next = new;
        q->tail = new;
    }
    q->len++;

    return 0;
}

int delete(Queue_t* q, void* elem){

    Node_t* corr = q->head, *prec = corr;
    while ( corr ){
        if ( corr->data == elem ){
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

            // free(corr->data); TODO
            free(corr);
            q->len--;

            return 0;
        }
        else{
            prec = corr;
            corr = corr->next;
        }
    }

    return 0;
}

void printQueue(Queue_t *q){
    for ( Node_t* corr = q->head; corr; corr = corr->next )
        printf("%s\n", (char*)corr->data);

}

void* pop(Queue_t *q){
    if ( isEmpty(q) )
        return NULL;
    
    void* elem = q->head->data;
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

    return elem;
}

size_t length(Queue_t* q){
    return q->len;
}

int deleteQueue(Queue_t *q){
    if ( isEmpty(q) ){
        free(q);
        return 0;
    }

    while ( q->head != q->tail ){
        Node_t* tmp = q->head;
        q->head = q->head->next;
        free(tmp->data);
        free(tmp);
    }
    // rimane solo un elemento (ex ultimo)
    Node_t* tmp = q->head;
    free(tmp->data);
    free(tmp);

    q->head = q->tail = NULL;
    free(q->head);
    free(q);

    return 0;
}
