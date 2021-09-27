#ifndef INTQUEUE_H
#define INTQUEUE_H

#include <pthread.h>

/** Elemento della coda
 */
typedef struct elem{
    int fd;
    struct elem *next;
} elem_t;

/** Struttura della coda
 */
typedef struct intqueue{
    elem_t *head;
    elem_t *tail;
    pthread_mutex_t qlock;
    pthread_cond_t qcond;
} intqueue_t;

/**
 * @brief Inizializza una coda di interi sincronizzata con politica FIFO
 * 
 * @return Il puntatore alla coda in caso di successo, NULL altrimenti
 */
intqueue_t* createQ();

/**
 *  @brief Controlla se la coda è vuota in mutua esclusione
 * 
 *  @param q Coda da controllare
 * 
 *  @return 1 se la coda è vuota, 0 altrimenti
 */
int isEmptySinchronized(intqueue_t* q);

/**
 *  @brief Inserisce l'intero indicato in fondo alla coda
 * 
 *  @param q Coda in cui inserire l'elemento
 *  @param fd Elemento da inserire
 * 
 *  @return 0 in caso di successo, -1 altrimenti
 */
int enqueue(intqueue_t* q, int fd);

/**
 *  @brief Rimuove e restituisce l'elemento in testa alla coda se presente,
 *         altrimenti si sospende in attesa
 * 
 *  @param q Coda da cui estrarre l'elemento
 * 
 *  @return L'elemento in testa alla coda
 */
int dequeue(intqueue_t* q);

/**
 *  @brief Restituisce una copia dell'elemento in testa alla coda
 * 
 *  @param q Coda da cui ottenere l'elemento
 * 
 *  @return Una copia del primo elemento, 0 se la coda è vuota, -1 in caso di errore
 */
int getFirst(intqueue_t* q);

/**
 *  @brief Cerca un elemento nella coda
 * 
 *  @param q Coda in cui cercare
 *  @param fd Elemento da cercare
 * 
 *  @return 1 se l'elemento era presente, 0 altrimenti, -1 in caso di errore
 */
int search(intqueue_t* q, int fd);

/**
 *  @brief Elimina l'elemento indicato dalla coda
 * 
 *  @param q Coda da cui eliminare l'elemento
 *  @param fd Elemento da eliminare
 * 
 *  @return 0 in caso di successo, -1 altrimenti
 */
int deleteElem(intqueue_t* q, int fd);

/**
 *  @brief Svuota ed elimina la coda
 * 
 *  @param q Coda da cancellare
 * 
 *  @return 0 in caso di successo, -1 altrimenti
 */
int deleteQ(intqueue_t* q);

#endif //INTQUEUE_H