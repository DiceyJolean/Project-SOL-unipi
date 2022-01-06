#ifndef QUEUE_H
#define QUEUE_H

#include <pthread.h>

/** Elemento della coda
 */
typedef struct Node {
    void* data;
    struct Node *next;
} Node_t;

/** Struttura della coda
 */
typedef struct Queue {
    Node_t      *head; // elemento di testa
    Node_t      *tail; // elemento di coda
    size_t      len;  // lunghezza
} Queue_t;

void printQueue(Queue_t *q);

/**
 *  @brief Inizializza una coda generica sincronizzata con politica FIFO
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
 *  @brief Inserisce un elemento in fondo alla coda
 *
 *  @param q    Coda in cui inserire l'elemento
 *  @param elem    Elemento da inserire
 *
 *  @return 0 se l'inserimento è andato a buon fine, -1 altrimenti
 */
int push(Queue_t *q, void* elem);

/**
 *  @brief Rimuove e restituisce l'elemento in testa alla coda se c'è, altrimenti si sospende in attesa
 *
 *  @param q Coda da cui estrarre l'elemento
 *
 *  @return l'elemento in testa alla coda
 */
void* pop(Queue_t *q);

/**
 *  @brief Elimina l'elemento indicato dalla coda
 * 
 *  @param q Coda da cui eliminare l'elemento
 *  @param elem Elemento da eliminare
 * 
 *  @return 0 in caso di successo, -1 altrimenti
 */
int delete(Queue_t* q, void* elem);

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

#endif //QUEUE_H
