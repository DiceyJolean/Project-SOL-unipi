#include <assert.h>
#include <errno.h>
#include <stdlib.h>

#ifndef DEBUG
#define DEBUG 0
#endif

#ifndef PRINT_ENABLE
#define PRINT_ENABLE 1
#endif

#define NBUCK 16

#include "storage.h"

icl_hash_t* fdClean; // Tabella hash per gli fdCleaner
pthread_mutex_t fdLock = PTHREAD_MUTEX_INITIALIZER; // Lock per accedere in mutua esclusione a fdClean


void lockFdLock(){
    if ( pthread_mutex_lock(&fdLock) == -1 ){
        perror("pthread_mutex_lock");
        return;
    }
}

fdCleaner_t* findPerFd(int fd){
    return icl_hash_find(fdClean, &fd);
}

void lockFdUnlock(){
    if ( pthread_mutex_unlock(&fdLock) == -1 ){
        perror("pthread_mutex_unlock");
        return;
    }
}

void myFree(void* p){
    if ( p ){
        free(p);
        p = NULL;
    }
}

// Accedo a un file in lettura e NON modifico i suoi campi
int startReadFile(file_t* file){
    if ( DEBUG) printf("FILE: Provo ad acquisire la lock in lettura, lettori attivi %d, lettori attesa %d\n", file->lettori_file_attivi, file->lettori_file_attesa);
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
    if ( DEBUG) printf("FILE: Ho acquisito la lock in lettura, lettori attivi %d, lettori attesa %d\n", file->lettori_file_attivi, file->lettori_file_attesa);
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

    if ( DEBUG) printf("FILE: Rilascio la lock in lettura, lettori attivi %d, lettori attesa %d\n", file->lettori_file_attivi, file->lettori_file_attesa);
    return 0;
}

// Accedo a un file in scrittura e modifico i suoi campi
int startWriteFile(file_t* file){
    if ( DEBUG) printf("FILE: Provo ad acquisire la lock in scrittura\n");
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

    if ( DEBUG) printf("FILE: Ho acquisito la lock in scrittura\n");
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
    if ( DEBUG) printf("FILE: Rilascio la lock in scrittura\n");

    return 0;
}

// Accedo allo storage ma NON devo aggiungere né rimuovere dei file
int startReadCache(storage_t* cache){
    if ( DEBUG) printf("CACHE: Provo ad acquisire la lock in lettura, lettori attivi %d, lettori attesa %d\n", cache->lettori_cache_attivi, cache->lettori_cache_attesa);
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

    if ( DEBUG) printf("CACHE: Ho acquisito la lock in lettura, lettori attivi %d, lettori attesa %d\n", cache->lettori_cache_attivi, cache->lettori_cache_attesa);
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
    
    if ( DEBUG) printf("CACHE: Rilascio la lock in lettura, lettori attivi %d, lettori attesa %d\n", cache->lettori_cache_attivi, cache->lettori_cache_attesa);
    return 0;
}

// Accedo allo storage e devo aggiungere dei file oppure c'è la possibilità di rimuoverne alcuni (CASO CACHE MISS)
int startWriteCache(storage_t* cache){
    if ( DEBUG) printf("CACHE: Provo ad acquisire la lock in scrittura\n");
    
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
    if ( DEBUG) printf("CACHE: Ho acquisito la lock in scrittura\n");
    
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
    
    if ( DEBUG) printf("CACHE: Rilascio la lock in scrittura\n");
    return 0;
}

void printCache(storage_t* cache){
    startReadCache(cache);
    int i = 0;
    icl_entry_t* entry = NULL;
    char* key = NULL;
    file_t* file = NULL;

    if ( PRINT_ENABLE ) printf("STORAGE: Stampo tutta la cache\n\n ~~~~~~~~~~~~~~~~~~~ \n\n");

    icl_hash_foreach(cache->cache, i, entry, key, file){
        printf("\n%s:\n"
            "Contenuto: %s\n"
            "-Altre info qui -\n", file->pathname, file->content);
    }

    if ( PRINT_ENABLE ) printf("STORAGE: Stampa terminata\n\n ~~~~~~~~~~~~~~~~~~~ \n\n");
    endReadCache(cache);
}

void freeFile(void* p){
    free(((file_t*)p)->content);
    free(p);
}

void closePerFD(storage_t* cache, char* pathname, int idClient){
    startReadCache(cache);

    file_t* file = icl_hash_find(cache->cache, pathname);
    if ( file ){
        startReadFile(file);
        FD_CLR(idClient, &file->openedBy);
        endReadFile(file);
    }

    endReadCache(cache);
}

storage_t* S_createStorage(int max_file, int max_size){

    fdClean = icl_hash_create(NBUCK, NULL, NULL);

    storage_t* cache = malloc(sizeof(storage_t));
    if ( !cache ) return NULL;
    cache->cache = icl_hash_create(NBUCK, NULL, NULL);
    if ( !cache->cache ){
        free(cache);
        return NULL;
    }
    cache->uploaded = initQueue();    // Coda FIFO dei pathname dei file, mantiene l'ordine di espulsione dalla cache
    if ( !cache->uploaded ){
        icl_hash_destroy(cache->cache, free, free);
        free(cache);
        return NULL;
    }
    cache->scrittori_cache_attivi = 0;
    cache->lettori_cache_attivi = 0;
    cache->lettori_cache_attesa = 0;
    cache->scrittori_cache_attesa = 0;
    if ( pthread_mutex_init(&cache->lock_cache, NULL) != 0 || pthread_cond_init(&cache->cond_lettori_cache, NULL) != 0 || pthread_cond_init(&cache->cond_scrittura_cache, NULL) != 0 ){
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

    deleteQueue(storage->uploaded);
    // free(storage->uploaded);
    icl_hash_destroy(storage->cache, NULL, freeFile);
    free(storage);

    return 0;
}

int S_lockFile(storage_t* cache, char* pathname, int idClient, int* enqueued){

    if ( !pathname || idClient <= 0 ){
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

    if ( pthread_mutex_lock(&fdLock) == -1 )
        return -1;
    
    fdCleaner_t* tmp = icl_hash_find(fdClean, &idClient);
    if ( !tmp ){
        tmp = malloc(sizeof(fdCleaner_t));
        tmp->fd = idClient;
        tmp->fileLocked = initQueue();
        tmp->fileOpened = initQueue();
        icl_hash_insert(fdClean, &idClient, tmp);
    }
    push(tmp->fileLocked, pathname);

    if ( pthread_mutex_unlock(&fdLock) == -1 )
        return -1;
    
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

    if ( !pathname || idClient <= 0 ){
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
    if ( PRINT_ENABLE ) printf("STORAGE: È stata richiesta una S_unlock, il file %s è bloccato dal client %d\n", pathname, file->lockedBy);
    // Controllo se il client non avesse già la lock sul file
    if ( !FD_ISSET(idClient, &file->lockWait) && idClient != file->lockedBy ){
        // Se non sono in coda e/o non ho la lock, l'operazione non è permessa
        errno = EPERM;
        endReadFile(file);
        endReadCache(cache);
        return -1;
    }
    
    // Il client che rilascia la lock era l'attuale detentore
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
    // Aggiorno maxfd
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

int S_removeFile(storage_t* cache, char* pathname, int idClient, fd_set* stopLock, int *maxfd){

    if ( !pathname || idClient <= 0 ){
        // Paramentri errati
        errno = EINVAL;
        return -1;
    }

    startWriteCache(cache);
    file_t* file = icl_hash_find(cache->cache, pathname);
    if ( !file ){
        // Il file non era presente nella struttura
        if ( PRINT_ENABLE ) printf("STORAGE: Il file %s non è presente in cache\n", pathname);
        errno = ENOENT;
        endWriteCache(cache);
        return -1;
    }

    // Controllo se il client non ha la lock e il file è lockato
    if ( idClient != file->lockedBy && file->lockedBy != 0 ){
        if ( PRINT_ENABLE ) printf("STORAGE: Il client %d non ha i permessi per rimuovere il file %s\n", idClient, pathname);
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
    if ( PRINT_ENABLE ) printf("STORAGE: Il file %s è stato rimosso dalla cache\n", pathname);

    if ( delete(cache->uploaded, pathname) != 0 ){
        errno = EFATAL;
        endWriteCache(cache);
        return -1;
    }
    if ( PRINT_ENABLE ) printf("STORAGE: Il file %s è stato rimosso dalla coda dei file caricati\n", pathname);

    // tutti i client in attesa della lock devono essere avvisati
    for ( int i = 0; i < file->maxfd; i++ )
        if ( FD_ISSET(i, &file->lockWait) ){
            FD_SET(i, stopLock);
            *maxfd = i;
        }
        
    free(file->content);
    //free(file->pathname);
    free(file);

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
        if ( PRINT_ENABLE ) printf("STORAGE: Il file %s è già presente in cache\n", pathname);
        errno = EEXIST;
        endWriteCache(cache);
        return -1;
    }

    file_t* new = malloc(sizeof(file_t));
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
    new->pathname = malloc(strlen(pathname)+1);
    memset(new->pathname, 0, strlen(pathname)+1);
    strncpy(new->pathname, pathname, strlen(pathname)+1);
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

    if ( icl_hash_insert(cache->cache, pathname, new) == NULL ){
        errno = EFATAL;
        freeFile(new);
        endWriteCache(cache);
        return -1;
    }

    endWriteCache(cache);

    if ( PRINT_ENABLE ) printf("STORAGE: Il file %s è stato inserito in cache (ancora non è nella coda dei caricati)\n", pathname);
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
        if ( PRINT_ENABLE ) printf("STORAGE: Il file %s non è presente in cache\n", pathname);
        errno = ENOENT;
        endReadCache(cache);
        return -1;
    }

    if ( pthread_mutex_lock(&fdLock) == -1 )
        return -1;
    
    fdCleaner_t* tmp = icl_hash_find(fdClean, &idClient);
    if ( !tmp ){
        // Aggiungo il client alla coda di chi ha aperto il file nella struct per la pulizia
        tmp = malloc(sizeof(fdCleaner_t));
        tmp->fd = idClient;
        tmp->fileLocked = initQueue();
        tmp->fileOpened = initQueue();
        icl_hash_insert(fdClean, &idClient, tmp);
    }
    push(tmp->fileOpened, pathname);

    if ( pthread_mutex_unlock(&fdLock) == -1 )
        return -1;
    
    startWriteFile(file); // Modifico il campo openendBy del file
    
    FD_SET(idClient, &file->openedBy);
    if ( PRINT_ENABLE ) printf("STORAGE: Il client %d ha aperto il file %s\n", idClient, pathname);

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
        if ( PRINT_ENABLE ) printf("STORAGE: Il file %s non è presente in cache\n", pathname);
        errno = ENOENT;
        endReadCache(cache);
        return -1;
    }

    startWriteFile(file);

    FD_ZERO(&file->openedBy);
    if ( PRINT_ENABLE ) printf("STORAGE: Il file %s è stato chiuso per tutti\n", pathname);
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
        if ( PRINT_ENABLE ) printf("STORAGE: Il file %s non è presente in cache\n", pathname);
        errno = ENOENT;
        endReadCache(cache);
        return -1;
    }

    startReadFile(file);
    if ( (file->lockedBy != 0 && file->lockedBy != idClient) || !(FD_ISSET(idClient, &file->openedBy)) ){
        if ( PRINT_ENABLE ) printf("STORAGE: Il client %d non ha i permessi per leggere il file %s\n", idClient, pathname);
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
    size_t temp = file->size; // Evito il puntatore al file che potrei eliminare
    *size = temp;
    if ( PRINT_ENABLE ) printf("STORAGE: Ho caricato in buf %s e in size %ld, il client %d potrà leggere\n", *buf, *size, idClient);

    endReadFile(file);
    // In questo frattempo il file non può essere rimosso perché ho la lock sulla cache
    startWriteFile(file);

    file->okUpload = 0;
    
    endWriteFile(file);
    endReadCache(cache);
    return 0;
}

int S_readNFiles(storage_t* cache, int idClient, int n, Queue_t* fileRead, int* nRead){

    if ( !fileRead || !nRead ){
        errno = EINVAL;
        return -1;
    }

    *nRead = 0;
    startReadCache(cache);
    if ( PRINT_ENABLE ) printf("STORAGE: È stata richiesta una readNFiles con n=%d, preparo la coda dei file\n", n);
    if ( n <= 0 || n > cache->actual_file ) 
        n = cache->actual_file;

    // Prendo i pathname dalla queue e per ognuno carico i contenuti in fileRead
    Node_t* corr = cache->uploaded->head;
    for ( int i = 0; i < n; i++ ){
        if ( PRINT_ENABLE ) printf("STORAGE: Cerco il file %s in cache\n", corr->data);
        file_t* file = icl_hash_find(cache->cache, corr->data);
        if ( !file )
            // Il file non era presente nella struttura
            continue;
        
        startReadFile(file);
        
        file_t* cpy = malloc(sizeof(file_t));
        cpy->pathname = malloc(strlen(file->pathname)+1);
        memset(cpy->pathname, 0, strlen(file->pathname)+1);
        strncpy(cpy->pathname, file->pathname, strlen(file->pathname)+1);
        cpy->content = malloc(file->size);
        memset(cpy->content, 0, file->size);
        memcpy(cpy->content, file->content, file->size);
        cpy->size = file->size;

        push(fileRead, cpy);
        (*nRead)++;

        endReadFile(file);
        startWriteFile(file);
        file->okUpload = 0;
        endWriteFile(file);
    }

    endReadCache(cache);
    return 0;
}

int S_uploadFile(storage_t* cache, char* pathname, int idClient, void* content, size_t size, Queue_t* victim, int* deleted){

    *deleted = 0;
    if ( !pathname || idClient <= 0 || !victim || !content || size <= 0 ){
        errno = EINVAL;
        return -1;
    }

    startWriteCache(cache); // Potrei rimuovere alcuni file in caso di cache miss
    file_t* file = icl_hash_find(cache->cache, pathname);
    if ( !file ){
        // Il file non era presente nella struttura
        if ( PRINT_ENABLE ) printf("STORAGE: Il file %s non è presente in cache\n", pathname);
        errno = ENOENT;
        endReadCache(cache);
        return -1;
    }

    if ( (file->lockedBy != 0 && file->lockedBy != idClient) || !(FD_ISSET(idClient, &file->openedBy)) || !file->okUpload ){
        errno = EPERM;
        if ( PRINT_ENABLE ) printf("STORAGE: Il client %d non ha i permessi per caricare il file %s\n", idClient, pathname);
        endWriteCache(cache);
        return -1;
    }

    if ( size > cache->max_size ){
        errno = EFBIG;
        if ( PRINT_ENABLE ) printf("STORAGE: Il file %s è troppo grande per essere inserito in cache\n", pathname);
        // TODO, devo rimuoverlo dalla tabella hash?
        endWriteCache(cache);
        return -1;
    }

    while ( cache->actual_file == cache->max_file || (cache->actual_size + size) > cache->max_size ){
        if ( PRINT_ENABLE ) printf("STORAGE: Eseguo l'algoritmo di rimpiazzo\n");
        // Non c'è spazio per il nuovo file, eseguo il rimpiazzamento
        if ( (*deleted =  cacheMiss(cache, victim, size, pathname)) == -1 ){
            // Il file che volevo modificare è stato rimosso o c'è stato un errore fatale.
            // In entrambi i casi errno è già settato e lo faccio galleggiare
            *deleted = 0;
            endWriteCache(cache);
            return -1;
        }
    }

    // Posso caricare il file
    file->size = size;
    if ( push(cache->uploaded, pathname) != 0 ){
        errno = EFATAL;
        endWriteCache(cache);
        return -1;
    }
    if ( PRINT_ENABLE) printf("STORAGE: Ho aggiunto %s alla coda dei file caricati\n", (char*)cache->uploaded->tail->data);

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

int S_appendFile(storage_t* cache, char* pathname, int idClient, Queue_t* victim, void* content, size_t size, int* deleted){

    *deleted = 0;
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
        if ( (*deleted =  cacheMiss(cache, victim, size, pathname)) == -1 ){
            // Il file che volevo modificare è stato rimosso o c'è stato un errore fatale.
            // In entrambi i casi errno è già settato e lo faccio galleggiare
            *deleted = 0;
            endWriteCache(cache);
            return -1;
        }

    // Alloco lo spazio per il nuovo contenuto sia se era vuoto sia se devo espanderlo
    file->content = ( !file->content ) ? malloc(size) : realloc(file->content, size+file->size);
    if ( !file->content ){
        errno = EFATAL;
        endWriteCache(cache);
        return -1;
    }
    memset(file->content + file->size, 0, size);
    memcpy(file->content + file->size, content, size);
    file->size += size;

    cache->actual_size += size;
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

    if ( PRINT_ENABLE) printf("STORAGE: Per l'inserimento di %s avvio l'algoritmo di espulsione, devo liberare %ld bytes\n", pathname, ( (cache->actual_size - cache->max_size) + size));
    int err = 0;
    int deleted = 0;
    do{
        printQueue(cache->uploaded);
        char* evicted = pop(cache->uploaded);
        if ( !evicted ){
            errno = EFATAL;
            return -1;
        }
        if ( strcmp(evicted, pathname) == 0 )
            // Il file che volevo modificare è stato eliminato
            {
            err = 1;
            if ( PRINT_ENABLE) printf("STORAGE: Sto eliminando %s e il file che voglio inserire è %s\n", evicted, pathname);
            }
        file_t* file = icl_hash_find(cache->cache, evicted);
        if ( !file ){
            errno = EFATAL;
            return -1;
        }

        errno = 0;
        if ( icl_hash_delete(cache->cache, evicted, myFree, NULL) != 0 ){
            errno = EFATAL;
            return -1;
        }
        deleted++;
        //free(file);

        // Il file è stato rimosso correttamente
        if ( PRINT_ENABLE) printf("STORAGE: Ho eliminato il file %s dallo storage\n", evicted);
        cache->actual_file--;
        cache->actual_size -= file->size;
        push(victim, file); // Lascio al worker il resto delle operazioni per la pulizia
        // free(file->pathname);
        
    } while( cache->actual_size + size  > cache->max_size );

    if ( err ){
        errno = EIDRM;
        return -1;
    }

    return deleted;
}
int startReadFile(file_t* file){
    if ( DEBUG) printf("FILE: Provo ad acquisire la lock in lettura, lettori attivi %d, lettori attesa %d\n", file->lettori_file_attivi, file->lettori_file_attesa);
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
    if ( DEBUG) printf("FILE: Ho acquisito la lock in lettura, lettori attivi %d, lettori attesa %d\n", file->lettori_file_attivi, file->lettori_file_attesa);
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

    if ( DEBUG) printf("FILE: Rilascio la lock in lettura, lettori attivi %d, lettori attesa %d\n", file->lettori_file_attivi, file->lettori_file_attesa);
    return 0;
}

// Accedo a un file in scrittura e modifico i suoi campi
int startWriteFile(file_t* file){
    if ( DEBUG) printf("FILE: Provo ad acquisire la lock in scrittura\n");
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

    if ( DEBUG) printf("FILE: Ho acquisito la lock in scrittura\n");
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
    if ( DEBUG) printf("FILE: Rilascio la lock in scrittura\n");

    return 0;
}

// Accedo allo storage ma NON devo aggiungere né rimuovere dei file
int startReadCache(storage_t* cache){
    if ( DEBUG) printf("CACHE: Provo ad acquisire la lock in lettura, lettori attivi %d, lettori attesa %d\n", cache->lettori_cache_attivi, cache->lettori_cache_attesa);
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

    if ( DEBUG) printf("CACHE: Ho acquisito la lock in lettura, lettori attivi %d, lettori attesa %d\n", cache->lettori_cache_attivi, cache->lettori_cache_attesa);
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
    
    if ( DEBUG) printf("CACHE: Rilascio la lock in lettura, lettori attivi %d, lettori attesa %d\n", cache->lettori_cache_attivi, cache->lettori_cache_attesa);
    return 0;
}

// Accedo allo storage e devo aggiungere dei file oppure c'è la possibilità di rimuoverne alcuni (CASO CACHE MISS)
int startWriteCache(storage_t* cache){
    if ( DEBUG) printf("CACHE: Provo ad acquisire la lock in scrittura\n");
    
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
    if ( DEBUG) printf("CACHE: Ho acquisito la lock in scrittura\n");
    
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
    
    if ( DEBUG) printf("CACHE: Rilascio la lock in scrittura\n");
    return 0;
}

void printCache(storage_t* cache){
    startReadCache(cache);
    int i = 0;
    icl_entry_t* entry = NULL;
    char* key = NULL;
    file_t* file = NULL;

    if ( PRINT_ENABLE ) printf("STORAGE: Stampo tutta la cache\n\n ~~~~~~~~~~~~~~~~~~~ \n\n");

    icl_hash_foreach(cache->cache, i, entry, key, file){
        printf("\n%s:\n"
            "Contenuto: %s\n"
            "-Altre info qui -\n", file->pathname, file->content);
    }

    if ( PRINT_ENABLE ) printf("STORAGE: Stampa terminata\n\n ~~~~~~~~~~~~~~~~~~~ \n\n");
    endReadCache(cache);
}

void freeFile(void* p){
    free(((file_t*)p)->content);
    free(p);
}

void closePerFD(storage_t* cache, char* pathname, int idClient){
    startReadCache(cache);

    file_t* file = icl_hash_find(cache->cache, pathname);
    if ( file ){
        startReadFile(file);
        FD_CLR(idClient, &file->openedBy);
        endReadFile(file);
    }

    endReadCache(cache);
}

storage_t* S_createStorage(int max_file, int max_size){

    fdClean = icl_hash_create(NBUCK, NULL, NULL);

    storage_t* cache = malloc(sizeof(storage_t));
    if ( !cache ) return NULL;
    cache->cache = icl_hash_create(NBUCK, NULL, NULL);
    if ( !cache->cache ){
        free(cache);
        return NULL;
    }
    cache->uploaded = initQueue();    // Coda FIFO dei pathname dei file, mantiene l'ordine di espulsione dalla cache
    if ( !cache->uploaded ){
        icl_hash_destroy(cache->cache, free, free);
        free(cache);
        return NULL;
    }
    cache->scrittori_cache_attivi = 0;
    cache->lettori_cache_attivi = 0;
    cache->lettori_cache_attesa = 0;
    cache->scrittori_cache_attesa = 0;
    if ( pthread_mutex_init(&cache->lock_cache, NULL) != 0 || pthread_cond_init(&cache->cond_lettori_cache, NULL) != 0 || pthread_cond_init(&cache->cond_scrittura_cache, NULL) != 0 ){
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

    deleteQueue(storage->uploaded);
    // free(storage->uploaded);
    icl_hash_destroy(storage->cache, NULL, freeFile);
    free(storage);

    return 0;
}

int S_lockFile(storage_t* cache, char* pathname, int idClient, int* enqueued){

    if ( !pathname || idClient <= 0 ){
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

    if ( pthread_mutex_lock(&fdLock) == -1 )
        return -1;
    
    fdCleaner_t* tmp = icl_hash_find(fdClean, &idClient);
    if ( !tmp ){
        tmp = malloc(sizeof(fdCleaner_t));
        tmp->fd = idClient;
        tmp->fileLocked = initQueue();
        tmp->fileOpened = initQueue();
        icl_hash_insert(fdClean, &idClient, tmp);
    }
    push(tmp->fileLocked, pathname);

    if ( pthread_mutex_unlock(&fdLock) == -1 )
        return -1;
    
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

    if ( !pathname || idClient <= 0 ){
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
    // Controllo se il client non avesse già la lock sul file
    if ( !FD_ISSET(idClient, &file->lockWait) || idClient != file->lockedBy ){
        // Se non sono in coda e/o non ho la lock, l'operazione non è permessa
        errno = EPERM;
        endReadFile(file);
        endReadCache(cache);
        return -1;
    }
    
    // Il client che rilascia la lock era l'attuale detentore
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
    // Aggiorno maxfd
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

int S_removeFile(storage_t* cache, char* pathname, int idClient, fd_set* stopLock, int *maxfd){

    if ( !pathname || idClient <= 0 ){
        // Paramentri errati
        errno = EINVAL;
        return -1;
    }

    startWriteCache(cache);
    file_t* file = icl_hash_find(cache->cache, pathname);
    if ( !file ){
        // Il file non era presente nella struttura
        if ( PRINT_ENABLE ) printf("STORAGE: Il file %s non è presente in cache\n", pathname);
        errno = ENOENT;
        endWriteCache(cache);
        return -1;
    }

    // Controllo se il client non ha la lock e il file è lockato
    if ( idClient != file->lockedBy && file->lockedBy != 0 ){
        if ( PRINT_ENABLE ) printf("STORAGE: Il client %d non ha i permessi per rimuovere il file %s\n", idClient, pathname);
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
    if ( PRINT_ENABLE ) printf("STORAGE: Il file %s è stato rimosso dalla cache\n", pathname);

    if ( delete(cache->uploaded, pathname) != 0 ){
        errno = EFATAL;
        endWriteCache(cache);
        return -1;
    }
    if ( PRINT_ENABLE ) printf("STORAGE: Il file %s è stato rimosso dalla coda dei file caricati\n", pathname);

    // tutti i client in attesa della lock devono essere avvisati
    for ( int i = 0; i < file->maxfd; i++ )
        if ( FD_ISSET(i, &file->lockWait) ){
            FD_SET(i, stopLock);
            *maxfd = i;
        }
        
    free(file->content);
    //free(file->pathname);
    free(file);

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
        if ( PRINT_ENABLE ) printf("STORAGE: Il file %s è già presente in cache\n", pathname);
        errno = EEXIST;
        endWriteCache(cache);
        return -1;
    }

    file_t* new = malloc(sizeof(file_t));
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
    new->pathname = malloc(strlen(pathname)+1);
    memset(new->pathname, 0, strlen(pathname)+1);
    strncpy(new->pathname, pathname, strlen(pathname)+1);
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

    if ( icl_hash_insert(cache->cache, pathname, new) == NULL ){
        errno = EFATAL;
        freeFile(new);
        endWriteCache(cache);
        return -1;
    }

    endWriteCache(cache);

    if ( PRINT_ENABLE ) printf("STORAGE: Il file %s è stato inserito in cache (ancora non è nella coda dei caricati)\n", pathname);
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
        if ( PRINT_ENABLE ) printf("STORAGE: Il file %s non è presente in cache\n", pathname);
        errno = ENOENT;
        endReadCache(cache);
        return -1;
    }

    if ( pthread_mutex_lock(&fdLock) == -1 )
        return -1;
    
    fdCleaner_t* tmp = icl_hash_find(fdClean, &idClient);
    if ( !tmp ){
        // Aggiungo il client alla coda di chi ha aperto il file nella struct per la pulizia
        tmp = malloc(sizeof(fdCleaner_t));
        tmp->fd = idClient;
        tmp->fileLocked = initQueue();
        tmp->fileOpened = initQueue();
        icl_hash_insert(fdClean, &idClient, tmp);
    }
    push(tmp->fileOpened, pathname);

    if ( pthread_mutex_unlock(&fdLock) == -1 )
        return -1;
    
    startWriteFile(file); // Modifico il campo openendBy del file
    
    FD_SET(idClient, &file->openedBy);
    if ( PRINT_ENABLE ) printf("STORAGE: Il client %d ha aperto il file %s\n", idClient, pathname);

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
        if ( PRINT_ENABLE ) printf("STORAGE: Il file %s non è presente in cache\n", pathname);
        errno = ENOENT;
        endReadCache(cache);
        return -1;
    }

    startWriteFile(file);

    FD_ZERO(&file->openedBy);
    if ( PRINT_ENABLE ) printf("STORAGE: Il file %s è stato chiuso per tutti\n", pathname);
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
        if ( PRINT_ENABLE ) printf("STORAGE: Il file %s non è presente in cache\n", pathname);
        errno = ENOENT;
        endReadCache(cache);
        return -1;
    }

    startReadFile(file);
    if ( (file->lockedBy != 0 && file->lockedBy != idClient) || !(FD_ISSET(idClient, &file->openedBy)) ){
        if ( PRINT_ENABLE ) printf("STORAGE: Il client %d non ha i permessi per leggere il file %s\n", idClient, pathname);
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
    size_t temp = file->size; // Evito il puntatore al file che potrei eliminare
    *size = temp;
    if ( PRINT_ENABLE ) printf("STORAGE: Ho caricato in buf %s e in size %ld, il client %d potrà leggere\n", *buf, *size, idClient);

    endReadFile(file);
    // In questo frattempo il file non può essere rimosso perché ho la lock sulla cache
    startWriteFile(file);

    file->okUpload = 0;
    
    endWriteFile(file);
    endReadCache(cache);
    return 0;
}

int S_readNFiles(storage_t* cache, int idClient, int n, Queue_t* fileRead, int* nRead){

    if ( !fileRead || !nRead ){
        errno = EINVAL;
        return -1;
    }

    *nRead = 0;
    startReadCache(cache);
    if ( PRINT_ENABLE ) printf("STORAGE: È stata richiesta una readNFiles con n=%d, preparo la coda dei file\n", n);
    if ( n <= 0 || n > cache->actual_file ) 
        n = cache->actual_file;

    // Prendo i pathname dalla queue e per ognuno carico i contenuti in fileRead
    Node_t* corr = cache->uploaded->head;
    for ( int i = 0; i < n; i++ ){
        if ( PRINT_ENABLE ) printf("STORAGE: Cerco il file %s in cache\n", corr->data);
        file_t* file = icl_hash_find(cache->cache, corr->data);
        if ( !file )
            // Il file non era presente nella struttura
            continue;
        
        startReadFile(file);
        
        file_t* cpy = malloc(sizeof(file_t));
        cpy->pathname = malloc(strlen(file->pathname)+1);
        memset(cpy->pathname, 0, strlen(file->pathname)+1);
        strncpy(cpy->pathname, file->pathname, strlen(file->pathname)+1);
        cpy->content = malloc(file->size);
        memset(cpy->content, 0, file->size);
        memcpy(cpy->content, file->content, file->size);
        cpy->size = file->size;

        push(fileRead, cpy);
        (*nRead)++;

        endReadFile(file);
        startWriteFile(file);
        file->okUpload = 0;
        endWriteFile(file);
    }

    endReadCache(cache);
    return 0;
}

int S_uploadFile(storage_t* cache, char* pathname, int idClient, void* content, size_t size, Queue_t* victim, int* deleted){

    *deleted = 0;
    if ( !pathname || idClient <= 0 || !victim || !content || size <= 0 ){
        errno = EINVAL;
        return -1;
    }

    startWriteCache(cache); // Potrei rimuovere alcuni file in caso di cache miss
    file_t* file = icl_hash_find(cache->cache, pathname);
    if ( !file ){
        // Il file non era presente nella struttura
        if ( PRINT_ENABLE ) printf("STORAGE: Il file %s non è presente in cache\n", pathname);
        errno = ENOENT;
        endReadCache(cache);
        return -1;
    }

    if ( (file->lockedBy != 0 && file->lockedBy != idClient) || !(FD_ISSET(idClient, &file->openedBy)) || !file->okUpload ){
        errno = EPERM;
        if ( PRINT_ENABLE ) printf("STORAGE: Il client %d non ha i permessi per caricare il file %s\n", idClient, pathname);
        endWriteCache(cache);
        return -1;
    }

    if ( size > cache->max_size ){
        errno = EFBIG;
        if ( PRINT_ENABLE ) printf("STORAGE: Il file %s è troppo grande per essere inserito in cache\n", pathname);
        // TODO, devo rimuoverlo dalla tabella hash?
        endWriteCache(cache);
        return -1;
    }

    while ( cache->actual_file == cache->max_file || (cache->actual_size + size) > cache->max_size ){
        if ( PRINT_ENABLE ) printf("STORAGE: Eseguo l'algoritmo di rimpiazzo\n");
        // Non c'è spazio per il nuovo file, eseguo il rimpiazzamento
        if ( (*deleted =  cacheMiss(cache, victim, size, pathname)) == -1 ){
            // Il file che volevo modificare è stato rimosso o c'è stato un errore fatale.
            // In entrambi i casi errno è già settato e lo faccio galleggiare
            *deleted = 0;
            endWriteCache(cache);
            return -1;
        }
    }

    // Posso caricare il file
    file->size = size;
    if ( push(cache->uploaded, pathname) != 0 ){
        errno = EFATAL;
        endWriteCache(cache);
        return -1;
    }
    if ( PRINT_ENABLE) printf("STORAGE: Ho aggiunto %s alla coda dei file caricati\n", (char*)cache->uploaded->tail->data);

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

int S_appendFile(storage_t* cache, char* pathname, int idClient, Queue_t* victim, void* content, size_t size, int* deleted){

    *deleted = 0;
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
        if ( (*deleted =  cacheMiss(cache, victim, size, pathname)) == -1 ){
            // Il file che volevo modificare è stato rimosso o c'è stato un errore fatale.
            // In entrambi i casi errno è già settato e lo faccio galleggiare
            *deleted = 0;
            endWriteCache(cache);
            return -1;
        }

    // Alloco lo spazio per il nuovo contenuto sia se era vuoto sia se devo espanderlo
    file->content = ( !file->content ) ? malloc(size) : realloc(file->content, size+file->size);
    if ( !file->content ){
        errno = EFATAL;
        endWriteCache(cache);
        return -1;
    }
    memset(file->content + file->size, 0, size);
    memcpy(file->content + file->size, content, size);
    file->size += size;

    cache->actual_size += size;
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

    if ( PRINT_ENABLE) printf("STORAGE: Per l'inserimento di %s avvio l'algoritmo di espulsione, devo liberare %ld bytes\n", pathname, ( (cache->actual_size - cache->max_size) + size));
    int err = 0;
    int deleted = 0;
    do{
        printQueue(cache->uploaded);
        char* evicted = pop(cache->uploaded);
        if ( !evicted ){
            errno = EFATAL;
            return -1;
        }
        if ( strcmp(evicted, pathname) == 0 )
            // Il file che volevo modificare è stato eliminato
            {
            err = 1;
            if ( PRINT_ENABLE) printf("STORAGE: Sto eliminando %s e il file che voglio inserire è %s\n", evicted, pathname);
            }
        file_t* file = icl_hash_find(cache->cache, evicted);
        if ( !file ){
            errno = EFATAL;
            return -1;
        }

        errno = 0;
        if ( icl_hash_delete(cache->cache, evicted, myFree, NULL) != 0 ){
            errno = EFATAL;
            return -1;
        }
        deleted++;
        //free(file);

        // Il file è stato rimosso correttamente
        if ( PRINT_ENABLE) printf("STORAGE: Ho eliminato il file %s dallo storage\n", evicted);
        cache->actual_file--;
        cache->actual_size -= file->size;
        push(victim, file); // Lascio al worker il resto delle operazioni per la pulizia
        // free(file->pathname);
        
    } while( cache->actual_size + size  > cache->max_size );

    if ( err ){
        errno = EIDRM;
        return -1;
    }

    return deleted;
}
    }

    return 0;
}

int startWriteFile(file_t file){

    if ( pthread_mutex_lock(&file.lock_file) != 0 ){
        perror("Pthread_mutex_lock");
        return -1;
    }
    file.scrittori_file_attesa++;
    while ( file.scrittori_file_attivi > 0 || file.lettori_file_attivi > 0 )
        if ( pthread_cond_wait(&file.cond_scrittura_file, &file.lock_file) != 0 ){
            perror("Pthread_cond_wait");
            return -1;
        }

    file.scrittori_file_attesa--;
    file.scrittori_file_attivi++;

    if ( pthread_mutex_unlock(&file.lock_file) != 0 ){
        perror("Pthread_mutex_unlock");
        return -1;
    }

    return 0;
}

int endWriteFile(file_t file){

    if ( pthread_mutex_lock(&file.lock_file) != 0 ){
        perror("Pthread_mutex_lock");
        return -1;
    }

    file.scrittori_file_attivi--;
    assert(file.scrittori_file_attivi == 0);
    if ( file.scrittori_file_attesa > 0 ){
        if ( pthread_cond_signal(&file.cond_scrittura_file) != 0 ){
            perror("Pthread_cond_signal");
            return -1;
        }
    }
    else
        if ( pthread_cond_broadcast(&file.cond_lettori_file) != 0 ){
            perror("pthread_cond_broadcast");
            return -1;
        }

    if ( pthread_mutex_unlock(&file.lock_file) != 0 ){
        perror("Pthread_mutex_unlock");
        return -1;
    }

    return 0;
}

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

storage_t* S_createStorage(int nbuck){

    storage_t* cache = malloc(sizeof(storage_t));
    if ( !cache ) return NULL;
    cache->cache = icl_hash_create(nbuck, NULL, NULL);
    if ( !cache->cache ){
        free(cache);
        return NULL;
    }
    cache->uploaded = initQueue();    // Coda FIFO dei pathname dei file, mantiene l'ordine di espulsione dalla cache
    if ( !cache->uploaded ){
        icl_hash_destroy(cache->cache, free, free);
        free(cache);
        return NULL;
    }
    cache->scrittori_cache_attivi = 0;
    cache->lettori_cache_attivi = 0;
    cache->lettori_cache_attesa = 0;
    cache->scrittori_cache_attesa = 0;
    if ( pthread_mutex_init(&cache->lock_cache, NULL) != 0 || pthread_cond_init(&cache->cond_lettori_cache, NULL) != 0 || pthread_cond_init(&cache->cond_scrittura_cache, NULL) != 0 ){
        icl_hash_destroy(cache->cache, free, free);
        deleteQueue(cache->uploaded);
        free(cache);
        return NULL;
    }
    cache->actual_size = 0;
    cache->max_used_size = 0;
    cache->actual_file = 0;
    cache->max_used_file = 0;

    return cache;
}

int deleteStorage(storage_t* storage){

    deleteQueue(storage->uploaded);
    free(storage->uploaded);
    icl_hash_destroy(storage->cache, free, free);
    free(storage);

    return 0;
}

int S_lockFile(storage_t* cache, char* pathname, int idClient, int* enqueued){

    if ( !pathname || idClient <= 0 || !enqueued ){
        // Paramentri errati
        errno = EINVAL;
        return -1;
    }

    file_t* file = icl_hash_find(cache->cache, pathname);
    if ( !file ){
        // Il file non era presente nella struttura
        errno = ENOENT;
        return -1;
    }

    // Controllo se il client non avesse già la lock sul file
    if ( idClient == file->lockedBy){
        // Aveva già la lock
        *enqueued = 0;
        return 0;
    }
    
    // Controllo se il set di fd è vuoto, in questo caso assegno la lock
    if ( file->lockedBy == 0 ){
        file->lockedBy = idClient;
        *enqueued = 0;
        return 0;
    }

    // Aggiungo il client nell'insieme di chi attende la lock
    FD_SET(idClient, &file->lockWait);
    *enqueued = 1;
    if ( idClient > file->maxfd )
        file->maxfd = idClient;

    return 0;
}

int S_unlockFile(storage_t* cache, char* pathname, int idClient, int *newLocker){

    if ( !pathname || idClient <= 0 ){
        // Paramentri errati
        errno = EINVAL;
        return -1;
    }

    file_t* file = icl_hash_find(cache->cache, pathname);
    if ( !file ){
        // Il file non era presente nella struttura
        errno = ENOENT;
        return -1;
    }
    file->okUpload = 0;

    // Controllo se il client non avesse già la lock sul file
    if ( !FD_ISSET(idClient, &file->lockWait) || idClient != file->lockedBy ){
        // Il client non aveva la lock sul file e non era nemmeno in coda
        errno = EPERM;
        return -1;
    }
    
    // Il client che rilascia la lock era l'attuale detentore
    if ( idClient != file->lockedBy || idClient == file->maxfd){
        errno = FATAL;
        return -1;
    }

    if ( file->maxfd == 0 ){
        // Non c'erano altri client in attesa della lock
        file->lockedBy = *newLocker = 0;
        return 0;
    }

    file->lockedBy = *newLocker = file->maxfd;
    // Aggiorno maxfd
    for ( int i = (file->maxfd-1); i >= 0; --i )
        if ( FD_ISSET(i, &file->lockWait) )
            file->maxfd = i;
    // Se non ci sono altri client in coda
    if ( file->lockedBy == file->maxfd )
        file->maxfd = 0;

    return 0;
}

int S_removeFile(storage_t* cache, char* pathname, int idClient, fd_set* stopLock, int *maxfd){

    if ( !pathname || idClient <= 0 ){
        // Paramentri errati
        errno = EINVAL;
        return -1;
    }

    file_t* file = icl_hash_find(cache->cache, pathname);
    if ( !file ){
        // Il file non era presente nella struttura
        errno = ENOENT;
        return -1;
    }

    // Controllo se il client non ha la lock e il file è lockato
    if ( idClient != file->lockedBy && file->lockedBy != 0 ){
        errno = EPERM;
        return -1;
    }

    cache->actual_file--;
    cache->actual_size -= file->size;

    // Procedo alla rimozione del file
    if ( icl_hash_delete(cache->cache, pathname, NULL, NULL) != 0){ // TODO
        errno = FATAL;
        return -1;
    }
    
    if ( delete(cache->uploaded, pathname) != 0 ){
        errno = FATAL;
        return -1;
    }

    // tutti i client in attesa della lock devono essere avvisati
    for ( int i = 0; i < file->maxfd; i++ )
        if ( FD_ISSET(i, &file->lockWait) ){
            FD_SET(i, stopLock);
            *maxfd = i;
        }
        
    free(file->content);
    free(file->pathname);
    free(file);

    return 0;
}

int S_createFile(storage_t* cache, char* pathname, int idClient){

    if ( !pathname || idClient <= 0 ){
        errno = EINVAL;
        return -1;
    }

    if ( icl_hash_find(cache->cache, pathname) != NULL ){
        // Esiste già 
        errno = EEXIST;
        return -1;
    }

    file_t* new = malloc(sizeof(file_t));
    memset(new, 0, sizeof(file_t));
    if ( pthread_cond_init(&new->cond_lettori_file, NULL) != 0 ){
        errno = FATAL;
        return -1;
    }
    if ( pthread_cond_init(&new->cond_scrittura_file, NULL) != 0 ){
        errno = FATAL;
        return -1;
    }
    if ( pthread_mutex_init(&new->lock_file, NULL) != 0 ){
        errno = FATAL;
        return -1;
    }
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
    int len = ( sizeof(pathname) > MAX_CHAR_LENGHT) ? MAX_CHAR_LENGHT : sizeof(pathname);
    new->pathname = malloc(len);
    strncpy(new->pathname, pathname, len);

    if ( icl_hash_insert(cache->cache, pathname, new) == NULL ){
        errno = FATAL;
        return -1;
    }

    return 0;
}

int S_openFile(storage_t* cache,char* pathname, int idClient){

    if ( !pathname || idClient <= 0 ){
        errno = EINVAL;
        return -1;
    }

    file_t* file = icl_hash_find(cache->cache, pathname);
    if ( !file ){
        // Il file non era presente nella struttura
        errno = ENOENT;
        return -1;
    }

    FD_SET(idClient, &file->openedBy);

    return 0;
}

int S_closeFile(storage_t* cache,char*pathname){

    if ( !pathname ){
        errno = EINVAL;
        return -1;
    }

    file_t* file = icl_hash_find(cache->cache, pathname);
    if ( !file ){
        // Il file non era presente nella struttura
        errno = ENOENT;
        return -1;
    }
    file->okUpload = 0;

    FD_ZERO(&file->openedBy);

    return 0;
}

int S_readFile(storage_t* cache,char* pathname, int idClient, void** buf, size_t* size, int* read){

    if ( !pathname || idClient <= 0 ){
        errno = EINVAL;
        return -1;
    }

    file_t* file = icl_hash_find(cache->cache, pathname);
    if ( !file ){
        // Il file non era presente nella struttura
        errno = ENOENT;
        return -1;
    }
    file->okUpload = 0;

    if ( (file->lockedBy != 0 && file->lockedBy != idClient) || !(FD_ISSET(idClient, &file->openedBy)) ){
        errno = EPERM;
        *read = 0;
        return -1;
    }

    *buf = malloc(file->size+1);
    if ( !*buf ){
        errno = FATAL;
        return -1;
    }
    memset(*buf, 0, file->size+1);
    memcpy(*buf, file->content, file->size);
    size_t temp = file->size; // Evito il puntatore al file che potrei eliminare
    *size = temp;

    return 0;
}

/*
int S_readNFiles(storage_t* cache,int idClient, int n, char* dirname, int* read){

    if ( idClient <= 0 || !dirname ){
        errno = EINVAL;
        return -1;
    }

    if ( n <= 0 || n > cache->actual_file ) 
        n = cache->actual_file;

    // Prendo i pathname dalla queue e per ognuno carico i contenuti in dirname
    Node_t* corr = cache->uploaded->head;
    int count_readn = 0;
    for ( int i = 0; i < n; i++ ){
        file_t* file = icl_hash_find(cache->cache, corr->data);
        if ( !file ){
            // Il file non era presente nella struttura
            continue;
        
        int inset = 0; 
        if ( !FD_ISSET(idClient, &file->openedBy) ){
            FD_SET(idClient, &file->openedBy); // Bypasso il controllo
            inset = 1;
        }

        errno = 0;
        void** buf = NULL;
        size_t size = 0;
        int readn = 0;
        int err = S_readFile(cache, file->pathname, idClient, buf, &size, &readn);
        if ( err != 0 && errno != 35 ) // TODO problemi con la define
            continue;
        
        if ( inset )
            FD_CLR(idClient, &file->openedBy);
    
        }
        
        

    }

    return 0;
}
*/

int S_uploadFile(storage_t* cache,char* pathname, int idClient, void* content, size_t size, Queue_t* victim, int* written){

    if ( !pathname || idClient <= 0 || !victim || !content || size <= 0 ){
        errno = EINVAL;
        return -1;
    }

    file_t* file = icl_hash_find(cache->cache, pathname);
    if ( !file ){
        // Il file non era presente nella struttura
        errno = ENOENT;
        return -1;
    }

    if ( (file->lockedBy != 0 && file->lockedBy != idClient) || !(FD_ISSET(idClient, &file->openedBy)) || !file->okUpload ){
        errno = EPERM;
        *written = 0;
        return -1;
    }
    file->okUpload = 0;

    if ( size > max_storage ){
        errno = E2BIG;
        *written = 0;
        return -1;
    }

    while ( cache->actual_file == max_file || (cache->actual_size + size) > max_storage ){
        // Non c'è spazio per il nuovo file, eseguo il rimpiazzamento
        cacheMiss(cache, victim, size);
    }

    // Posso caricare il file
    file->size = size;
    if ( push(cache->uploaded, pathname) != 0 ){
        errno = FATAL;
        return -1;
    }
    max_file++;
    // Aggiorno il massimo di file occupati in memoria
    cache->max_used_file = ( cache->max_used_file > cache->actual_file ) ? cache->max_used_file : cache->actual_file;


    file->content = malloc(size);
    memset(file->content, 0, size);
    memcpy(file->content, content, size);
    *written = size;
    // Aggiorno il massimo di spazio occupato in memoria
    cache->max_used_size = ( cache->max_used_size > cache->actual_size ) ? cache->max_used_size : cache->actual_size;

    return 0;
}

int S_appendFile(storage_t* cache,char* pathname, int idClient, Queue_t* victim, void* content, size_t size, int* written){

        if ( !pathname || idClient <= 0 || !victim || !content || size <= 0 ){
        errno = EINVAL;
        *written = 0;
        return -1;
    }

    file_t* file = icl_hash_find(cache->cache, pathname);
    if ( !file ){
        // Il file non era presente nella struttura
        errno = ENOENT;
        *written = 0;
        return -1;
    }
    file->okUpload = 0;

    if ( (file->lockedBy != 0 && file->lockedBy != idClient) || !(FD_ISSET(idClient, &file->openedBy)) ){
        errno = EPERM;
        *written = 0;
        return -1;
    }

    if ( size > max_storage ){
        errno = E2BIG;
        *written = 0;
        return -1;
    }

    while ( (cache->actual_size + size) > max_storage ){
        // Non c'è spazio per il nuovo file, eseguo il rimpiazzamento
        cacheMiss(cache, victim, size);
    }

    // Alloco lo spazio per il nuovo contenuto sia se era vuoto sia se devo espanderlo
    file->content = ( !file->content ) ? malloc(size) : realloc(file->content, size+file->size);
    if ( !file->content ){
        errno = FATAL;
        *written = 0;
        return -1;
    }
    memset(file->content + file->size, 0, size);
    memcpy(file->content + file->size, content, size);
    file->size += size;

    *written = size;
    cache->actual_size += size;
    // Aggiorno il massimo di spazio occupato in memoria
    cache->max_used_size = ( cache->max_used_size > cache->actual_size ) ? cache->max_used_size : cache->actual_size;

    return 0;
}

int cacheMiss(storage_t* cache, Queue_t* victim, size_t size){

    do{
        Node_t* evicted = pop(cache->uploaded);
        if ( !evicted || !evicted->data ){
            errno = FATAL;
            return -1;
        }
        file_t* file = icl_hash_find(cache->cache, evicted->data);
        if ( !file ){
            errno = FATAL;
            return -1;
        }
        errno = 0;
        if ( icl_hash_delete(cache->cache, file->pathname, free, NULL) != 0 )
            return -1; // ernno è settato da icl_hash_delete

        // Il file è stato rimosso correttamente
        cache->actual_file--;
        cache->actual_size -= file->size;
        push(victim, file); // Lascio al worker il resto delle operazioni per la pulizia
        free(file->pathname);
        free(file);

    } while( cache->actual_size >= max_storage - size);
    

    return 0;
}
