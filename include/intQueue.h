// Coda di interi sincronizzata con politica FIFO

#ifndef INTQUEUE_H
#define INTQUEUE_H

#include <pthread.h>

/** Elemento della coda
 */
typedef struct Node {
    int         data;
    struct Node *next;
} Node_t;

/** Struttura della coda
 */
typedef struct Queue {
    Node_t      *head; // elemento di testa
    Node_t      *tail; // elemento di coda
    size_t      len;  // lunghezza
    pthread_mutex_t qlock;
    pthread_cond_t  qcond;
} Queue_t;

/**
 *  @brief Inizializza una coda di interi sincronizzata con politica FIFO
 *
 *  @return Il puntatore alla coda in caso di successo, NULL in caso di fallimento
 */
Queue_t* initQueue();

/**
 *  @brief Controlla se la coda è vuota (NON in mutua esclusione)
 *
 *  @param q Coda da controllare
 *
 *  @return 0 se la coda è vuota, -1 altrimenti
 */
int isEmpty(Queue_t* q);

/**
 *  @brief Inserisce un numero in fondo alla coda
 *
 *  @param q    Coda in cui inserire l'elemento
 *  @param elem    Elemento da inserire
 *
 *  @return 0 se l'inserimento è andato a buon fine, -1 altrimenti
 */
int push(Queue_t *q, int elem);

/**
 *  @brief Rimuove e restituisce l'elemento in testa alla coda se c'è, altrimenti si sospende in attesa
 *
 *  @param q Coda da cui estrarre l'elemento
 *
 *  @return l'elemento in testa alla coda
 */
int pop(Queue_t *q);

/**
 *  @brief Legge la lunghezza della coda in mutua esclusione
 *
 *  @param q Coda da analizzare
 *
 *  @return la lunghezza della coda
 */
unsigned long length(Queue_t* q);

/**
 *  @brief Svuota la coda e libera il puntatore
 *
 *  @param q Coda da eliminare
 *  @return 0 se l'operazione è andata a buon fine, -1 in caso di errore
 */
int deleteQueue(Queue_t *q);

#endif //INTQUEUE_H
