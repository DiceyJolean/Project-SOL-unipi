// Erica Pistolesi 518169

#ifndef STORAGE_H
#define STORAGE_H

#include <pthread.h>
#include <sys/select.h>
#include <string.h>

#include "queue.h"
#include "intqueue.h"
#include "icl_hash.h"
// #include "serverutility.h"

#ifndef EFATAL
#define EFATAL -1
#endif

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
    int actual_size; // Dimensione attuale dello storage
    int max_used_size; // Dimensione massima raggiunta dallo storage
    int actual_file; // Numero di file attualmente presenti nello storage
    int max_used_file; // Numero di file massimo raggiunto nello storage
    int max_size; // Limite massimo per la dimensione totale dello storage
    int max_file; // Numero massimo di file contenuti nello storage
} storage_t;

// -------------------- Struttura dati e attributi per i singoli file --------------------

typedef struct file{
    char* pathname;     // Nome univoco del file (path assoluto)
    void* content;      // Contenuto del file
    size_t size;        // Dimensione del file
    fd_set openedBy;    // Insieme dei fd dei client da cui è stato aperto
    int lockedBy;       // FD del detentore della lock
    fd_set lockWait; // Insieme dei fd dei client in attesa della lock
    int maxfd; // Massimo fd dell'insime lockwaiters, corrisponde al prossimo client che otterrà la lock
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
 * @brief   Per ogni file presente nello storage aperto o bloccato dal client indicato dal file descriptor fd,
 *          elimina il client dall'insieme dei client che avevano aperto tali file e sblocca tutti i file da 
 *          lui bloccati, restituisce inoltre la coda dei client in attesa di ottenere la lock su quegli stessi file
 * 
 * @param cache Puntatore allo storage
 * @param fd File descriptor del client da eliminare dallo storage
 * @param lockWaiters Coda dei file descriptor dei client che erano in attesa della lock su file bloccati dal client da eliminare
 * 
 * @return 0 in caso di successo, -1 altrimenti
 */
int closeFd(storage_t* cache, int fd, intqueue_t* lockWaiters);

void printCache(storage_t* cache);

/**
 * @brief Inizializza uno storage
 * 
 * @param max_file Numero massimo di file contenuti nello storage
 * @param max_size Capacità massima dello storage
 * 
 * @exception ENOMEM se fallisce l'allocazione dei campi dello storage
 * 
 * @return il puntatore allo storage appena create, NULL in caso di errore
 */
storage_t* S_createStorage(int max_file, int max_size);

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
 * @exception   EINVAL se pathname è NULL, se enqueued è NULL o se l'idClient è un valore negativo\n
 *              ENOENT se il pathname non è presente nello storage
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
 * @exception   EINVAL se pathname è NULL, se newLocker è NULL o se l'idClient è un valore negativo\n
 *              EPERM se chi richiede la unlock non deteneva la lock sul file\n
 *              ENOENT se il pathname non è presente nello storage
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
 * @param lockWaiters Coda dei file descriptor dei client che erano in attesa della lock sul file eliminato
 * 
 * @exception   EINVAL se pathname è NULL, se lockWaiters è NULL o se l'idClient è un valore negativo\n
 *              EPERM se il client non ha la lock e il file è lockato
 *              ENOENT se il pathname non è presente nello storage
 *
 * @return La coda dei client da avvisare, -1 altrimenti
 */
int S_removeFile(storage_t* cache, char* pathname, int idClient, intqueue_t* lockWaiters);

/**
 * @brief Crea un nuovo file nello storage NON INCREMENTA IL CONTATORE DEI FILE
 * 
 * @param cache Puntatore allo storage
 * @param pathname Pathname del file da aggiungere allo storage
 * @param idClient FD del client che crea il file
 * 
 * @exception   EINVAL se pathname è NULL o se l'idClient è un valore negativo\n
 *              EEXIST se il file era già presente in cache\n
 *              ENOMEM se fallisce l'allocazione di memoria
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
 * @exception   EINVAL se pathname è NULL o se l'idClient è un valore negativo\n
 *              ENOENT se il pathname non è presente nello storage
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
 * @exception   EINVAL se pathname è NULL o se l'idClient è un valore negativo\n
 *              ENOENT se il pathname non è presente nello storage
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
 * 
 * @exception   EINVAL se pathname è NULL o se l'idClient è un valore negativo\n
 *              ENOENT se il pathname non è presente nello storage\n
 *              EPERM se il file è bloccato da un client che non è idClient, oppure se idClient non ha aperto il file
 *
 * @return 0 in caso di successo, -1 altrimenti
 */
int S_readFile(storage_t* cache, char* pathname, int idClient, void** buf, size_t* size);

/**
 * @brief Legge al più n file (o tutti se n è un intero minore o uguale a zero)
 *        e li mette nella coda passata dal server, anche se sono chiusi o bloccati
 * 
 * @param cache Puntatore allo storage
 * @param idClient FD del client che richiede la lettura
 * @param n Numero di file da leggere (se <= 0 vengono letti tutti i file presenti)
 * @param fileRead Coda che conterrà i file letti
 * 
 * @exception EINVAL se fileRead è NULL, se fileRead è NULL o se l'idClient è un valore negativo
 * 
 * @return 0 in caso di successo, -1 altrimenti
 */
int S_readNFiles(storage_t* cache, int idClient, int n, Queue_t* fileRead);

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
 * 
 * @exception   EINVAL se pathname è NULL, se victim è NULL, se content è NULL, se size è un valore negativo o se l'idClient è un valore negativo\n
 *              ENOENT se il file non è stato ancora creato (non è presente in cache)\n
 *              EPERM se il file è bloccato da un client che non è idClient, se openFile(OCREAT) non è stata l'operazione precedente oppure se idClient non ha aperto il file\n
 *              EFBIG se il file è troppo grande
 * 
 * @return 0 in caso di successo, -1 altrimenti
 */
int S_uploadFile(storage_t* cache, char* pathname, int idClient, void* content, size_t size, Queue_t* victim);

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
 * 
 * @exception   EINVAL se pathname è NULL, se victim è NULL, se content è NULL, se size è un valore negativo o se l'idClient è un valore negativo\n
 *              ENOENT se il file non è presente in cache\n
 *              EPERM se il file è bloccato da un client che non è idClient, oppure se idClient non ha aperto il file\n
 *              EFBIG se il file è troppo grande
 *
 * @return 0 in caso di successo, -1 altrimenti
 */
int S_appendFile(storage_t* cache, char* pathname, int idClient, Queue_t* victim, void* content, size_t size);

/**
 * @brief Esegue il rimpiazzamento, non fa la free dei file e li pusha in victim,
 * finché nello storage non c'è abbastanza spazio per un file grande size. 
 * Se size è 0 elimina un solo file
 * 
 * @param cache Puntatore allo storage
 * @param victim Coda dei file espulsi
 * @param size Minimo spazio che dovrà essere libero nello storage al termine della funzione
 * 
 * @exception EIDRM se durante l'esecuzione viene eliminato il file pathname (solo caso APPEND)
 * 
 * @return 0 in caso di successo, -1 altrimenti
 */
int cacheMiss(storage_t* cache, Queue_t* victim, size_t size, char* pathname);

#endif //STORAGE_H
