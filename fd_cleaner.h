// Erica Pistolesi 518169

#include <pthread.h>
#include <stdlib.h>
#include <assert.h>

#include "icl_hash.h"
#include "storage.h"

// Struct che contiene i file aperti e lockati dal client identificato con il fd
typedef struct fdCleaner {
    int fd;
    Queue_t* fileOpened; // Coda dei file aperti dal client indicato con fd
    Queue_t* fileLocked; // Coda dei file bloccati dal client indicato con fd
} fdCleaner_t;

icl_hash_t* fdClean; // Tabella hash per gli fdCleaner, ogni client identificato con il proprio fd ha associato la coda dei file da esso aperto o bloccati
pthread_mutex_t fdLock = PTHREAD_MUTEX_INITIALIZER; // Lock per accedere in mutua esclusione a fdClean

// Restituisce la struct che contiene la coda dei file aperti o bloccati dal client identificato con fd
fdCleaner_t* findPerFd(int fd){
    int* key = (int*) malloc(sizeof(int));
    *key = fd;
    fdCleaner_t* tmp = (fdCleaner_t*) icl_hash_find(fdClean, key);
    free(key);
    return tmp;
}

// Restituisce un buffer allocato contentente la copia di dest
char* my_strcpy(char* dest){
    int len = (strlen(dest)+1)*sizeof(char);

    char* src = (char*) malloc(len);
    memset(src, 0, len);
    strncpy(src, dest, len);

    if ( !src )
        return NULL;

    return src;
}

void freeFD(void* p){
    deleteQueue(((fdCleaner_t*)p)->fileLocked);
    deleteQueue(((fdCleaner_t*)p)->fileOpened);
    free(p);
}

/*
    @brief  Aggiunge il client idClient alla struct degli fdCleaner se non era presente.
            Aggiunge il pathname ai file aperti se il flag è 1,
            altrimenti lo aggiunge ai file bloccati se il flag è 0

    @param idClient fd del client da aggiungere o aggiornare
    @param pathname Nome del file da aggiungere al fdCleaner associato a idClient
    @param flag 1 se l'operazione è una open, 0 se l'operazione è una lock

    @return 0 in caso di successo, -1 altrimenti
*/
int updateFDcleaner(int idClient, char* pathname, int flag){
    int* key = (int*) malloc(sizeof(int));
    *key = idClient;

    if ( pthread_mutex_lock(&fdLock) == -1 )
        return -1;
    
    fdCleaner_t* tmp = (fdCleaner_t*) icl_hash_find(fdClean, key);
    
    if ( !tmp ){
        // Aggiungo il client alla coda di chi ha aperto il file nella struct per la pulizia
        tmp = ( fdCleaner_t*) malloc(sizeof(fdCleaner_t));
        tmp->fd = idClient;
        tmp->fileLocked = initQueue();
        tmp->fileOpened = initQueue();
        icl_hash_insert(fdClean, key, tmp);
    }
    else
        free(key);

    char* pathnamecpy = my_strcpy(pathname);
    
    if ( flag )
        push(tmp->fileOpened, pathnamecpy);
    else
        push(tmp->fileLocked, pathnamecpy);

    if ( pthread_mutex_unlock(&fdLock) == -1 )
        return -1;
    
    return 0;
}
