// Erica Pistolesi 518169

#include <assert.h>
#include <errno.h>
#include <stdlib.h>

#ifndef DEBUG
#define DEBUG 0
#endif

#define NBUCK 16

#include "storage.h"
#include "fd_cleaner.h"

void myFree(void* p){
    if ( p ){
        free(p);
        p = NULL;
    }
}

// Accedo a un file in lettura e NON modifico i suoi campi
int startReadFile(file_t* file){
    if ( pthread_mutex_lock(&file->lock_file) != 0 ){
        perror("Pthread_mutex_lock");
        return -1;
    }

    file->lettori_file_attesa++;
    while( file->scrittori_file_attesa > 0 || file->scrittori_file_attivi > 0 )
        if ( pthread_cond_wait(&file->cond_lettori_file, &file->lock_file) != 0  ){
            perror("Pthread_cond_wait");
            return -1;
        }

    file->lettori_file_attesa--;
    file->lettori_file_attivi++;
    if ( pthread_mutex_unlock(&file->lock_file) != 0 ){
        perror("Pthread_mutex_unlock");
        return -1;
    }

    return 0;
}

// Termino la lettura di un file
int endReadFile(file_t* file){

    if ( pthread_mutex_lock(&file->lock_file) != 0 ){
        perror("Pthread_mutex_lock");
        return -1;
    }

    file->lettori_file_attivi--;
    if ( file->lettori_file_attivi == 0 && file->scrittori_file_attesa > 0 )
        if ( pthread_cond_signal(&file->cond_scrittura_file) != 0 ){
            perror("Pthread_cond_signal");
            return -1;
        }

    if ( pthread_mutex_unlock(&file->lock_file) != 0 ){
        perror("Pthread_mutex_unlock");
        return -1;
    }

    return 0;
}

// Accedo a un file in scrittura e modifico i suoi campi
int startWriteFile(file_t* file){
    if ( pthread_mutex_lock(&file->lock_file) != 0 ){
        perror("Pthread_mutex_lock");
        return -1;
    }
    file->scrittori_file_attesa++;
    while ( file->scrittori_file_attivi > 0 || file->lettori_file_attivi > 0 )
        if ( pthread_cond_wait(&file->cond_scrittura_file, &file->lock_file) != 0 ){
            perror("Pthread_cond_wait");
            return -1;
        }

    file->scrittori_file_attesa--;
    file->scrittori_file_attivi++;
    assert(file->scrittori_file_attivi == 1);

    if ( pthread_mutex_unlock(&file->lock_file) != 0 ){
        perror("Pthread_mutex_unlock");
        return -1;
    }

    return 0;
}

// Termino la scrittura di un file
int endWriteFile(file_t* file){

    if ( pthread_mutex_lock(&file->lock_file) != 0 ){
        perror("Pthread_mutex_lock");
        return -1;
    }

    file->scrittori_file_attivi--;
    assert(file->scrittori_file_attivi == 0);
    if ( file->scrittori_file_attesa > 0 ){
        if ( pthread_cond_signal(&file->cond_scrittura_file) != 0 ){
            perror("Pthread_cond_signal");
            return -1;
        }
    }
    else
        if ( pthread_cond_broadcast(&file->cond_lettori_file) != 0 ){
            perror("pthread_cond_broadcast");
            return -1;
        }

    if ( pthread_mutex_unlock(&file->lock_file) != 0 ){
        perror("Pthread_mutex_unlock");
        return -1;
    }

    return 0;
}

// Accedo allo storage ma NON devo aggiungere né rimuovere dei file
int startReadCache(storage_t* cache){
    if ( pthread_mutex_lock(&cache->lock_cache) != 0 ){
        perror("Pthread_mutex_lock");
        return -1;
    }

    cache->lettori_cache_attesa++;
    while( cache->scrittori_cache_attesa > 0 || cache->scrittori_cache_attivi > 0 )
        if ( pthread_cond_wait(&cache->cond_lettori_cache, &cache->lock_cache) != 0  ){
            perror("Pthread_cond_wait");
            return -1;
        }

    cache->lettori_cache_attesa--;
    cache->lettori_cache_attivi++;
    if ( pthread_mutex_unlock(&cache->lock_cache) != 0 ){
        perror("Pthread_mutex_unlock");
        return -1;
    }

    return 0;
}

// Termino la lettura sullo storage
int endReadCache(storage_t* cache){

    if ( pthread_mutex_lock(&cache->lock_cache) != 0 ){
        perror("Pthread_mutex_lock");
        return -1;
    }

    cache->lettori_cache_attivi--;
    if ( cache->lettori_cache_attivi == 0 && cache->scrittori_cache_attesa > 0 )
        if ( pthread_cond_signal(&cache->cond_scrittura_cache) != 0 ){
            perror("Pthread_cond_signal");
            return -1;
        }

    if ( pthread_mutex_unlock(&cache->lock_cache) != 0 ){
        perror("Pthread_mutex_unlock");
        return -1;
    }
    
    return 0;
}

// Accedo allo storage e devo aggiungere dei file oppure c'è la possibilità di rimuoverne alcuni (CASO CACHE MISS)
int startWriteCache(storage_t* cache){
    if ( pthread_mutex_lock(&cache->lock_cache) != 0 ){
        perror("Pthread_mutex_lock");
        return -1;
    }
    cache->scrittori_cache_attesa++;
    while ( cache->scrittori_cache_attivi > 0 || cache->lettori_cache_attivi > 0 )
        if ( pthread_cond_wait(&cache->cond_scrittura_cache, &cache->lock_cache) != 0 ){
            perror("Pthread_cond_wait");
            return -1;
        }

    cache->scrittori_cache_attesa--;
    cache->scrittori_cache_attivi++;

    if ( pthread_mutex_unlock(&cache->lock_cache) != 0 ){
        perror("Pthread_mutex_unlock");
        return -1;
    }
    
    return 0;
}

// Termino la scrittura sullo storage
int endWriteCache(storage_t* cache){

    if ( pthread_mutex_lock(&cache->lock_cache) != 0 ){
        perror("Pthread_mutex_lock");
        return -1;
    }

    cache->scrittori_cache_attivi--;
    assert(cache->scrittori_cache_attivi == 0);
    if ( cache->scrittori_cache_attesa > 0 ){
        if ( pthread_cond_signal(&cache->cond_scrittura_cache) != 0 ){
            perror("Pthread_cond_signal");
            return -1;
        }
    }
    else
        if ( pthread_cond_broadcast(&cache->cond_lettori_cache) != 0 ){
            perror("pthread_cond_broadcast");
            return -1;
        }

    if ( pthread_mutex_unlock(&cache->lock_cache) != 0 ){
        perror("Pthread_mutex_unlock");
        return -1;
    }
    
    return 0;
}

void printCache(storage_t* cache){
    startReadCache(cache);
    int i = 0;
    icl_entry_t* entry = NULL;
    char* key = NULL;
    file_t* file = NULL;

    if ( DEBUG ) printf("STORAGE: Stampo tutta la cache\n\n ~~~~~~~~~~~~~~~~~~~ \n\n");

    icl_hash_foreach(cache->cache, i, entry, key, file){
        printf("\n%s:\n"
            "Contenuto: %s\n"
            "-Altre info qui -\n", file->pathname, (char*)file->content);
    }

    if ( DEBUG ) printf("STORAGE: Stampa terminata\n\n ~~~~~~~~~~~~~~~~~~~ \n\n");
    endReadCache(cache);
}

int closeFd(storage_t* cache, int fd, intqueue_t* lockWaiters){
        
    if ( pthread_mutex_lock(&fdLock) == -1 )
        return -1;

    fdCleaner_t* tmp = findPerFd(fd);
    if ( tmp ){
        // Il client ha attualmente aperto o lockato alcuni file
        while ( length(tmp->fileLocked) > 0 ){
            char* pathname = pop(tmp->fileLocked);
            int newLocker = 0;
            S_unlockFile(cache, pathname, fd, &newLocker); // Non mi interessa il valore di ritorno
            // Se c'era un client in attesa della lock
            if ( newLocker )
                // Aggiungo il client alla coda di chi deve essere risvegliato dall'attesa della lock
                enqueue(lockWaiters, newLocker);
            
            free(pathname);
        }
        while ( tmp->fileOpened->len > 0 ){
            // Per ogni file che il client ha aperto, rimuovo il suo fd dall'insieme di chi ha aperto quel file
            char* pathname = pop(tmp->fileOpened);
            startReadCache(cache);

            file_t* file = icl_hash_find(cache->cache, pathname);
            if ( file ){
                startWriteFile(file);
                FD_CLR(fd, &file->openedBy);
                endWriteFile(file);
            }

            endReadCache(cache);
            free(pathname);
        }
    }

    if ( pthread_mutex_unlock(&fdLock) == -1 )
        return -1;

    return 0;
}

void freeFile(void* p){
    free(((file_t*)p)->content);
    free(((file_t*)p)->pathname);
    free(p);
}

storage_t* S_createStorage(int max_file, int max_size){

    fdClean = icl_hash_create(NBUCK, NULL, NULL);

    storage_t* cache = malloc(sizeof(storage_t));
    if ( !cache ){
        errno = ENOMEM;
        icl_hash_destroy(fdClean, NULL, NULL);
        return NULL;
    }
    
    cache->cache = icl_hash_create(NBUCK, NULL, NULL);
    if ( !cache->cache ){
        errno = ENOMEM;
        icl_hash_destroy(fdClean, NULL, NULL);
        free(cache);
        return NULL;
    }
    
    cache->uploaded = initQueue(); // Coda FIFO dei pathname dei file, mantiene l'ordine di espulsione dalla cache
    if ( !cache->uploaded ){
        errno = ENOMEM;
        icl_hash_destroy(fdClean, NULL, NULL);
        icl_hash_destroy(cache->cache, free, free);
        free(cache);
        return NULL;
    }
    
    cache->scrittori_cache_attivi = 0;
    cache->lettori_cache_attivi = 0;
    cache->lettori_cache_attesa = 0;
    cache->scrittori_cache_attesa = 0;
    if ( pthread_mutex_init(&cache->lock_cache, NULL) != 0 || pthread_cond_init(&cache->cond_lettori_cache, NULL) != 0 || pthread_cond_init(&cache->cond_scrittura_cache, NULL) != 0 ){
        icl_hash_destroy(fdClean, NULL, NULL);
        icl_hash_destroy(cache->cache, free, free);
        deleteQueue(cache->uploaded);
        free(cache);
        return NULL;
    }
    
    cache->actual_size = 0;
    cache->max_used_size = 0;
    cache->actual_file = 0;
    cache->max_used_file = 0;
    cache->max_file = max_file;
    cache->max_size = max_size;

    return cache;
}

int deleteStorage(storage_t* storage){

    icl_hash_destroy(fdClean, free, freeFD);
    icl_hash_destroy(storage->cache, free, freeFile);

    deleteQueue(storage->uploaded);
    free(storage);

    return 0;
}

int S_lockFile(storage_t* cache, char* pathname, int idClient, int* enqueued){

    if ( !pathname || idClient <= 0 || !enqueued ){
        // Paramentri errati
        errno = EINVAL;
        return -1;
    }

    startReadCache(cache);
    file_t* file = icl_hash_find(cache->cache, pathname);
    if ( !file ){
        // Il file non era presente nella struttura
        errno = ENOENT;
        endReadCache(cache);
        return -1;
    }

    if ( updateFDcleaner(idClient, pathname, 0) != 0 ){
        errno = EFATAL;
        endReadCache(cache);
        return -1;
    }    

    startReadFile(file);
    // Controllo se il client non avesse già la lock sul file
    if ( idClient == file->lockedBy){
        // Aveva già la lock
        *enqueued = 0;
        endReadFile(file);
        endReadCache(cache);
        return 0;
    }
    
    endReadFile(file);
    startWriteFile(file);
    // Controllo se il set di fd è vuoto, in questo caso assegno la lock
    if ( file->lockedBy == 0 ){
        file->lockedBy = idClient;
        *enqueued = 0;
        endWriteFile(file);
        endReadCache(cache);
        return 0;
    }

    // Aggiungo il client nell'insieme di chi attende la lock
    FD_SET(idClient, &file->lockWait);
    *enqueued = 1;
    if ( idClient > file->maxfd )
        file->maxfd = idClient;

    endWriteFile(file);
    endReadCache(cache);
    return 0;
}

int S_unlockFile(storage_t* cache, char* pathname, int idClient, int *newLocker){

    if ( !pathname || idClient <= 0 || !newLocker ){
        // Paramentri errati
        errno = EINVAL;
        return -1;
    }

    startReadCache(cache);
    file_t* file = icl_hash_find(cache->cache, pathname);
    if ( !file ){
        // Il file non era presente nella struttura
        errno = ENOENT;
        endReadCache(cache);
        return -1;
    }

    startReadFile(file);
    if ( DEBUG ) printf("STORAGE: È stata richiesta una S_unlock, il file %s è bloccato dal client %d\n", pathname, file->lockedBy);
    // Controllo che il client avesse effettivamente la lock sul file
    if ( !FD_ISSET(idClient, &file->lockWait) && idClient != file->lockedBy ){
        // Se non ho la lock, l'operazione non è permessa
        errno = EPERM;
        endReadFile(file);
        endReadCache(cache);
        return -1;
    }
    
    // Controllo se il client che rilascia la lock era l'attuale detentore
    if ( idClient != file->lockedBy || idClient == file->maxfd){
        // Se così non fosse ho un errore fatale
        errno = EFATAL;
        endReadFile(file);
        endReadCache(cache);
        return -1;
    }

    fdCleaner_t* tmp = icl_hash_find(fdClean, &idClient);
    if ( tmp )
        delete(tmp->fileLocked, pathname);
    // Il ramo else non può esistere, se sono qui il client aveva richiesto una lock

    endReadFile(file);
    startWriteFile(file);
    if ( file->maxfd == 0 ){
        // Non c'erano altri client in attesa della lock
        file->lockedBy = *newLocker = 0;
        endWriteFile(file);
        endReadCache(cache);
        return 0;
    }

    file->lockedBy = *newLocker = file->maxfd;
    // Aggiorno maxfd, ovvero il prossimo candidato a ottenere la lock
    for ( int i = (file->maxfd-1); i >= 0; --i )
        if ( FD_ISSET(i, &file->lockWait) )
            file->maxfd = i;
    // Se non ci sono altri client in coda
    if ( file->lockedBy == file->maxfd )
        file->maxfd = 0;

    file->okUpload = 0;

    endWriteFile(file);
    endReadCache(cache);
    return 0;
}

int S_removeFile(storage_t* cache, char* pathname, int idClient, intqueue_t* lockWaiters){

    if ( !pathname || idClient <= 0 || !lockWaiters ){
        // Paramentri errati
        errno = EINVAL;
        return -1;
    }

    startWriteCache(cache);
    file_t* file = icl_hash_find(cache->cache, pathname);
    if ( !file ){
        // Il file non era presente nella struttura
        if ( DEBUG ) printf("STORAGE: Il file %s non è presente in cache\n", pathname);
        errno = ENOENT;
        endWriteCache(cache);
        return -1;
    }

    // Controllo se il client non ha la lock e il file è lockato
    if ( idClient != file->lockedBy && file->lockedBy != 0 ){
        if ( DEBUG ) printf("STORAGE: Il client %d non ha i permessi per rimuovere il file %s\n", idClient, pathname);
        errno = EPERM;
        endWriteCache(cache);
        return -1;
    }

    cache->actual_file--;
    cache->actual_size -= file->size;

    // Procedo alla rimozione del file
    if ( icl_hash_delete(cache->cache, pathname, myFree, NULL) != 0){
        errno = EFATAL;
        endWriteCache(cache);
        return -1;
    }
    if ( DEBUG ) printf("STORAGE: Il file %s è stato rimosso dalla cache\n", pathname);

    if ( delete(cache->uploaded, pathname) != 0 ){
        errno = EFATAL;
        endWriteCache(cache);
        return -1;
    }
    if ( DEBUG ) printf("STORAGE: Il file %s è stato rimosso dalla coda dei file caricati\n", pathname);

    // tutti i client in attesa della lock devono essere avvisati
    for ( int i = 0; i < file->maxfd; i++ )
        if ( FD_ISSET(i, &file->lockWait) )
            enqueue(lockWaiters, i);
        
    freeFile(file);

    endWriteCache(cache);
    return 0;
}

int S_createFile(storage_t* cache, char* pathname, int idClient){

    if ( !pathname || idClient <= 0 ){
        errno = EINVAL;
        return -1;
    }

    startWriteCache(cache);
    if ( icl_hash_find(cache->cache, pathname) != NULL ){
        // Esiste già 
        if ( DEBUG ) printf("STORAGE: Il file %s è già presente in cache\n", pathname);
        errno = EEXIST;
        endWriteCache(cache);
        return -1;
    }

    file_t* new = malloc(sizeof(file_t));
    if ( !new ){
        errno = ENOMEM;
        return -1;
    }
    memset(new, 0, sizeof(file_t));
    if ( pthread_cond_init(&new->cond_lettori_file, NULL) != 0 ){
        errno = EFATAL;
        return -1;
    }
    if ( pthread_cond_init(&new->cond_scrittura_file, NULL) != 0 ){
        errno = EFATAL;
        return -1;
    }
    if ( pthread_mutex_init(&new->lock_file, NULL) != 0 ){
        errno = EFATAL;
        return -1;
    }

    new->pathname = my_strcpy(pathname);
    new->content = NULL;
    new->okUpload = 1;
    new->lettori_file_attesa = 0;
    new->lettori_file_attivi = 0;
    new->scrittori_file_attesa = 0;
    new->scrittori_file_attivi = 0;
    new->lockedBy = 0;
    FD_ZERO(&new->lockWait);
    new->maxfd = 0;
    FD_ZERO(&new->openedBy);
    new->size = 0;

    char* key = my_strcpy(pathname);

    if ( icl_hash_insert(cache->cache, key, new) == NULL ){
        errno = EFATAL;
        freeFile(new);
        endWriteCache(cache);
        return -1;
    }

    endWriteCache(cache);

    if ( DEBUG ) printf("STORAGE: Il file %s è stato inserito in cache (ancora non è nella coda dei caricati)\n", pathname);
    return 0;
}

int S_openFile(storage_t* cache, char* pathname, int idClient){

    if ( !pathname || idClient <= 0 ){
        errno = EINVAL;
        return -1;
    }

    startReadCache(cache); // Non devo né aggiungere né rimuovere file
    file_t* file = icl_hash_find(cache->cache, pathname);
    if ( !file ){
        // Il file non era presente nella struttura
        if ( DEBUG ) printf("STORAGE: Il file %s non è presente in cache\n", pathname);
        errno = ENOENT;
        endReadCache(cache);
        return -1;
    }

    if ( updateFDcleaner(idClient, pathname, 1) != 0 ){
        // TODO
        errno = EFATAL;
        endReadCache(cache);
        return -1;
    }

    startWriteFile(file); // Modifico il campo openendBy del file
    
    FD_SET(idClient, &file->openedBy);
    if ( DEBUG ) printf("STORAGE: Il client %d ha aperto il file %s\n", idClient, pathname);

    endWriteFile(file);
    endReadCache(cache);
    return 0;
}

int S_closeFile(storage_t* cache, char* pathname){

    if ( !pathname ){
        errno = EINVAL;
        return -1;
    }

    startReadCache(cache);
    file_t* file = icl_hash_find(cache->cache, pathname);
    if ( !file ){
        // Il file non era presente nella struttura
        if ( DEBUG ) printf("STORAGE: Il file %s non è presente in cache\n", pathname);
        errno = ENOENT;
        endReadCache(cache);
        return -1;
    }

    startWriteFile(file);

    FD_ZERO(&file->openedBy);
    if ( DEBUG ) printf("STORAGE: Il file %s è stato chiuso per tutti\n", pathname);
    file->okUpload = 0;
    
    endWriteFile(file);
    endReadCache(cache);
    return 0;
}

int S_readFile(storage_t* cache, char* pathname, int idClient, void** buf, size_t* size){

    if ( !pathname || idClient <= 0 ){
        errno = EINVAL;
        return -1;
    }

    startReadCache(cache);
    file_t* file = icl_hash_find(cache->cache, pathname);
    if ( !file ){
        // Il file non era presente nella struttura
        if ( DEBUG ) printf("STORAGE: Il file %s non è presente in cache\n", pathname);
        errno = ENOENT;
        endReadCache(cache);
        return -1;
    }

    startReadFile(file);
    if ( (file->lockedBy != 0 && file->lockedBy != idClient) || !(FD_ISSET(idClient, &file->openedBy)) ){
        if ( DEBUG ) printf("STORAGE: Il client %d non ha i permessi per leggere il file %s\n", idClient, pathname);
        errno = EPERM;
        endReadFile(file);
        endReadCache(cache);
        return -1;
    }

    *buf = malloc(file->size+1);
    if ( !*buf ){
        errno = EFATAL;
        endReadFile(file);
        endReadCache(cache);
        return -1;
    }
    memset(*buf, 0, file->size+1);
    memcpy(*buf, file->content, file->size);
    size_t temp = file->size; // Evito il puntatore al file che potrei eventualmente eliminare prossimamente
    *size = temp;
    if ( DEBUG ) printf("STORAGE: Ho caricato in buf %s e in size %ld, il client %d potrà leggere\n", (char*)*buf, *size, idClient);

    endReadFile(file);
    // In questo frattempo il file non può essere rimosso perché ho la lock sulla cache
    startWriteFile(file);

    file->okUpload = 0;
    
    endWriteFile(file);
    endReadCache(cache);
    return 0;
}

int S_readNFiles(storage_t* cache, int idClient, int n, Queue_t* fileRead){

    if ( !fileRead || idClient < 0 || !fileRead ){
        errno = EINVAL;
        return -1;
    }

    startReadCache(cache);
    if ( DEBUG ) printf("STORAGE: È stata richiesta una readNFiles con n=%d, preparo la coda dei file\n", n);
    if ( n <= 0 || n > cache->actual_file ) 
        n = cache->actual_file;

    // Prendo i pathname dalla queue e per ognuno carico i contenuti in fileRead
    Node_t* corr = cache->uploaded->head;
    for ( int i = 0; i < n; i++ ){
        if ( DEBUG ) printf("STORAGE: Cerco il file %s in cache\n", (char*)corr->data);
        file_t* file = icl_hash_find(cache->cache, corr->data);
        if ( !file )
            // Il file non era presente nella struttura
            continue;
        
        startReadFile(file);

        // Copia del file da mettere nella coda da resituire al server
        file_t* cpy = malloc(sizeof(file_t));
        cpy->pathname = my_strcpy(file->pathname);
        cpy->content = malloc(file->size);
        memset(cpy->content, 0, file->size);
        memcpy(cpy->content, file->content, file->size);
        cpy->size = file->size;

        push(fileRead, cpy);
        
        endReadFile(file);
        
        startWriteFile(file);
        file->okUpload = 0;
        endWriteFile(file);

        corr = corr->next;
    }

    endReadCache(cache);
    return 0;
}

int S_uploadFile(storage_t* cache, char* pathname, int idClient, void* content, size_t size, Queue_t* victim){

    if ( !pathname || idClient <= 0 || !victim || !content || size <= 0 ){
        errno = EINVAL;
        return -1;
    }

    startWriteCache(cache); // Potrei rimuovere alcuni file in caso di cache miss
    file_t* file = icl_hash_find(cache->cache, pathname);
    if ( !file ){
        // Il file non era presente nella struttura
        if ( DEBUG ) printf("STORAGE: Il file %s non è presente in cache\n", pathname);
        errno = ENOENT;
        endReadCache(cache);
        return -1;
    }

    if ( (file->lockedBy != 0 && file->lockedBy != idClient) || !(FD_ISSET(idClient, &file->openedBy)) || !file->okUpload ){
        errno = EPERM;
        if ( DEBUG ) printf("STORAGE: Il client %d non ha i permessi per caricare il file %s\n", idClient, pathname);
        endWriteCache(cache);
        return -1;
    }

    if ( size > cache->max_size ){
        errno = EFBIG;
        if ( DEBUG ) printf("STORAGE: Il file %s è troppo grande per essere inserito in cache\n", pathname);
        // TODO, devo rimuoverlo dalla tabella hash?
        endWriteCache(cache);
        return -1;
    }

    while ( cache->actual_file == cache->max_file || (cache->actual_size + size) > cache->max_size ){
        if ( DEBUG ) printf("STORAGE: Eseguo l'algoritmo di rimpiazzo\n");
        // Non c'è spazio per il nuovo file, eseguo il rimpiazzamento
        if ( cacheMiss(cache, victim, size, pathname) != 0 ){
            // Il file che volevo modificare è stato rimosso o c'è stato un errore fatale.
            // In entrambi i casi errno è già settato e lo faccio galleggiare
            endWriteCache(cache);
            return -1;
        }
    }

    // Posso caricare il file
    file->size = size;
    char* pathcpy = my_strcpy(pathname);
    if ( push(cache->uploaded, pathcpy) != 0 ){
        errno = EFATAL;
        endWriteCache(cache);
        return -1;
    }
    if ( DEBUG) printf("STORAGE: Ho aggiunto %s alla coda dei file caricati\n", (char*)cache->uploaded->tail->data);

    file->content = malloc(size);
    memset(file->content, 0, size);
    memcpy(file->content, content, size);

        // Aggiorno il contatore di file e la size correnti
    cache->actual_file++;
    cache->actual_size += size;
    // Aggiorno il massimo di file occupati in memoria
    cache->max_used_file = ( cache->max_used_file > cache->actual_file ) ? cache->max_used_file : cache->actual_file;
    // Aggiorno il massimo di spazio occupato in memoria
    cache->max_used_size = ( cache->max_used_size > cache->actual_size ) ? cache->max_used_size : cache->actual_size;

    file->okUpload = 0;
    endWriteCache(cache);
    return 0;
}

int S_appendFile(storage_t* cache, char* pathname, int idClient, Queue_t* victim, void* content, size_t size){

    if ( !pathname || idClient <= 0 || !victim || !content || size <= 0 ){
        errno = EINVAL;
        return -1;
    }

    startWriteCache(cache);
    file_t* file = icl_hash_find(cache->cache, pathname);
    if ( !file ){
        // Il file non era presente nella struttura
        errno = ENOENT;
        endWriteCache(cache);
        return -1;
    }

    if ( (file->lockedBy != 0 && file->lockedBy != idClient) || !(FD_ISSET(idClient, &file->openedBy)) ){
        errno = EPERM;
        if ( file->lockedBy )
            if ( DEBUG ) printf("STORAGE: Il file %s è bloccato dal client %d, quindi il client %d non ha i permessi\n", pathname, file->lockedBy, idClient);

        if ( !(FD_ISSET(idClient, &file->openedBy) ) )
            if ( DEBUG ) printf("STORAGE: Il client %d non ha aperto il file %s per poterci scrivere in append\n", idClient, pathname);
        endWriteCache(cache);
        return -1;
    }

    if ( size > cache->max_size || ((file->size + size) > cache->max_size) ){
        errno = EFBIG;
        endWriteCache(cache);
        return -1;
    }

    while ( (cache->actual_size + size) > cache->max_size )
        // Non c'è spazio per il nuovo file, eseguo il rimpiazzamento
        if ( cacheMiss(cache, victim, size, pathname) != 0 ){
            // Il file che volevo modificare è stato rimosso o c'è stato un errore fatale.
            // In entrambi i casi errno è già settato e lo faccio galleggiare
            endWriteCache(cache);
            return -1;
        }

    int newsize = 0;
    // Alloco lo spazio per il nuovo contenuto sia se era vuoto sia se devo espanderlo
    if ( !file->content ){
        newsize = size;
        file->content = malloc(newsize);
    }
    else {
        newsize = size -1; // Tolgo il vecchio terminatore di file
        file->content = realloc(file->content, newsize+file->size);
        file->size--;
    }
    if ( !file->content ){
        errno = EFATAL;
        endWriteCache(cache);
        return -1;
    }

    memset(file->content + file->size, 0, newsize);
    memcpy(file->content + file->size, content, newsize);
    file->size += newsize;

    cache->actual_size += newsize;
    // Aggiorno il massimo di spazio occupato in memoria
    cache->max_used_size = ( cache->max_used_size > cache->actual_size ) ? cache->max_used_size : cache->actual_size;

    if ( file->okUpload )
        // Il file non è presente nella coda dei caricati perché non è stata eseguita la uplodadFile, ce lo carico adesso
        push(cache->uploaded, pathname);

    file->okUpload = 0;

    endWriteCache(cache);
    return 0;
}

int cacheMiss(storage_t* cache, Queue_t* victim, size_t size, char* pathname){

    if ( DEBUG ) printf("STORAGE: Per l'inserimento di %s avvio l'algoritmo di espulsione, devo liberare %ld bytes\n", pathname, ( (cache->actual_size - cache->max_size) + size));
    int eidrm = 0; // Flag che indica se ho rimosso il file che volevo modificare (nel caso delle append)
    
    do{
        // Estraggo il primo file (FIFO) dalla coda dei file caricati in cache
        char* evicted = pop(cache->uploaded);
        if ( !evicted ){
            errno = EFATAL;
            return -1;
        }
        if ( strcmp(evicted, pathname) == 0 ){
            // Il file che volevo modificare è il più vecchio in cache, lo elimino comunque
            eidrm = 1; // 
            if ( DEBUG ) printf("STORAGE: Sto eliminando %s, per cui non sarà più possibile eseguire operazioni su di esso\n", evicted);
        }

        // Cerco il file più vecchio in cache
        file_t* file = icl_hash_find(cache->cache, evicted);
        if ( !file ){
            errno = EFATAL;
            return -1;
        }

        errno = 0;
        // Elimino il file dalla cache ma non libero il puntatore per inviarlo al client
        if ( icl_hash_delete(cache->cache, evicted, myFree, NULL) != 0 ){
            errno = EFATAL;
            return -1;
        }

        // Il file è stato rimosso correttamente
        if ( DEBUG ) printf("STORAGE: Ho eliminato il file %s dallo storage\n", evicted);
        cache->actual_file--;
        cache->actual_size -= file->size;
        free(evicted);

        push(victim, file); // Lascio al worker il resto delle operazioni per la pulizia
                
    } while( cache->actual_size + size  > cache->max_size );

    if ( eidrm ){
        errno = EIDRM;
        return -1;
    }

    return 0;
}
