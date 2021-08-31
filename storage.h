#ifndef STORAGE_H
#define STORAGE_H

#define _POSIX_C_SOURCE 200809L

#include <pthread.h>
#include <sys/select.h>
#include <string.h>

#include "queue.h"
#include "intqueue.h"
#include "icl_hash.h"
// #include "serverutility.h"

#define MAX_BUCKETS 100;
#define FATAL 35;

// TODO
#define MAX_CHAR_LENGHT 1024
static int max_storage = 1028;
static int max_file = 10;

// -------------------- Struttura dati e attributi per la cache --------------------

typedef struct storage{
    icl_hash_t* cache;
    Queue_t* uploaded;    // Coda FIFO dei pathname dei file, mantiene l'ordine di espulsione dalla cache
    int scrittori_cache_attivi;
    int lettori_cache_attivi;
    int lettori_cache_attesa;
    int scrittori_cache_attesa;
    pthread_mutex_t lock_cache;
    pthread_cond_t cond_lettori_cache;
    pthread_cond_t cond_scrittura_cache;
    int actual_size;
    int max_used_size;
    int actual_file;
    int max_used_file;
} storage_t;

// -------------------- Struttura dati e attributi per i singoli file --------------------

typedef struct file{
    char* pathname;     // Nome univoco del file (path assoluto)
    void* content;      // Contenuto del file
    size_t size;        // Dimensione del file
    fd_set openedBy;    // Insieme dei fd dei client da cui è stato aperto
    int lockedBy;       // FD del detentore della lock
    fd_set lockWait; // Insieme dei fd dei client in attesa della lock
    int maxfd; // Massimo fd dell'insime lockwaiters
    int okUpload; // Flag che indica se è consentina la writeFile

    int scrittori_file_attivi;
    int lettori_file_attivi;
    int lettori_file_attesa;
    int scrittori_file_attesa;
    pthread_mutex_t lock_file;
    pthread_cond_t cond_lettori_file;
    pthread_cond_t cond_scrittura_file;

} file_t;

/**
 * @brief Inizializza uno storage
 * 
 * @param nbuck Numero di bucket che conterrà la tabella hash nello storage
 * 
 * @return il puntatore allo storage appena create, NULL in caso di errore
 */
storage_t* S_createStorage(int nbuck);

/**
 * @brief Elimina uno storage e libera la memoria
 * 
 * @param storage Storage da eliminare
 * 
 * @return 0 in caso di successo, -1 altrimenti
 */
int deleteStorage(storage_t* storage);

/**
 * @brief Blocca in mutua esclusione il file per il client indicato,
 *        se il file è già lockato mette il client in coda per la lock
 * 
 * @param cache Puntatore allo storage
 * @param pathname Pathname del file da lockare
 * @param idClient FD del client che richiede la lock
 * @param enqueued Flag che indica se il client è in attesa della lock
 * 
 * @return 0 in caso di successo, -1 altrimenti
 */
int S_lockFile(storage_t* cache, char* pathname, int idClient, int* enqueued);

/**
 * @brief Sblocca il file se era in mutua esclusione per il client indicato,
 *        altrimenti elimina il client dalla coda per la lock
 * 
 * @param cache Puntatore allo storage
 * @param pathname Pathname del file da unlockare
 * @param idClient FD del client che rilascia la lock
 * @param newLocker FD del client che adesso detiene la lock
 *
 * @return 0 in caso di successo, -1 altrimenti
 */
int S_unlockFile(storage_t* cache, char* pathname, int idClient, int* newLocker);

/**
 * @brief Elimina il file dallo storage se il client aveva la lock sul file
 * 
 * @param cache Puntatore allo storage
 * @param pathname Pathname del file da eliminare
 * @param idClient FD del client che richiede la rimozione
 * @param stopLock FD dei client in attesa della lock
 * @param maxfd FD max in stopLock
 *
 * @return La coda dei client da avvisare, -1 altrimenti
 */
int S_removeFile(storage_t* cache, char* pathname, int idClient, fd_set* stopLock, int* maxfd);

/**
 * @brief Crea un nuovo file nello storage NON INCREMENTA IL CONTATORE DEI FILE
 * 
 * @param cache Puntatore allo storage
 * @param pathname Pathname del file da aggiungere allo storage
 * @param idClient FD del client che crea il file
 * 
 * @return 0 in caso di successo, -1 altrimenti
 */
int S_createFile(storage_t* cache, char* pathname, int idClient);

/**
 * @brief Apre in lettura/scrittura per il client indicato un file nello storage
 * 
 * @param cache Puntatore allo storage
 * @param pathname Pathname del file da aprire
 * @param idClient FD del client che ha accesso al file
 *
 * @return 0 in caso di successo, -1 altrimenti
 */
int S_openFile(storage_t* cache, char* pathname, int idClient);

/**
 * @brief Chiude il file
 * 
 * @param cache Puntatore allo storage
 * @param pathname Pathname del file da chiudere
 *
 * @return 0 in caso di successo, -1 altrimenti
 */
int S_closeFile(storage_t* cache, char*pathname);

/**
 * @brief Legge il file se era stato aperto dal client
 *        e mette il contenuto in buf
 * 
 * @param cache Puntatore allo storage
 * @param pathname Pathname del file da leggere
 * @param idClient FD del client che richiede la lettura
 * @param buf Contenuto del file
 * @param size Dimensione del contenuto
 * @param read Bytes letti
 *
 * @return Il numero dei bytes letti, -1 in caso di errore
 */
int S_readFile(storage_t* cache, char* pathname, int idClient, void** buf, size_t* size, int* read);

/**
 * @brief Carica effettivamente il file nello storage INCREMENTA IL CONTATORE DEI FILE
 *        Se non c'è spazio per un nuovo file esegue il rimpiazzamento
 *        e restituisce il file espulso in dirname
 * 
 * @param cache Puntatore allo storage
 * @param pathname Pathname del file da caricare
 * @param idClient FD del client che carica il file
 * @param content Contenuto da caricare nel file
 * @param size Dimesione del contenuto da scrivere
 * @param victim Coda di file espulsi
 * @param written Bytes scritti
 * 
 * @return 0 in caso di successo, -1 altrimenti
 */
int S_uploadFile(storage_t* cache, char* pathname, int idClient, void* content, size_t size, Queue_t* victim, int* written);

/**
 * @brief Scrive in append sul file indicato il contenuto passato.
 *        Se non c'è sufficiente spazio in memoria esegue il
 *        rimpiazzamento e restituisce uno o più file espulsi in dirname
 * 
 * @param cache Puntatore allo storage
 * @param pathname Pathname del file da aggiornare
 * @param idClient FD del client che richiede la scrittura
 * @param victim Coda di file espulsi
 * @param content Contenuto da scrivere in append
 * @param size Dimesione del contenuto da scrivere
 * @param written Bytes scritti
 *
 * @return 0 in caso di successo, -1 altrimenti
 */
int S_appendFile(storage_t* cache, char* pathname, int idClient, Queue_t* victim, void* content, size_t size, int* written);

/**
 * @brief Esegue il rimpiazzamento, non fa la free dei file e li pusha in victim,
 * finché nello storage non c'è abbastanza spazio per un file grande size. 
 * Se size è 0 elimina un solo file
 * 
 * @param cache Puntatore allo storage
 * @param victim Coda dei file espulsi
 * @param size Minimo spazio che dovrà essere libero nello storage al termine della funzione
 * 
 * @return 0 in caso di successo, -1 altrimenti 
 */
int cacheMiss(storage_t* cache, Queue_t* victim, size_t size);

#endif //STORAGE_H