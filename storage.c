#include <assert.h>
#include <errno.h>
#include <stdlib.h>

#include "storage.h"

int startReadFile(file_t file){
    if ( pthread_mutex_lock(&file.lock_file) != 0 ){
        perror("Pthread_mutex_lock");
        return -1;
    }

    file.lettori_file_attesa++;
    while( file.scrittori_file_attesa > 0 || file.scrittori_file_attivi > 0 )
        if ( pthread_cond_wait(&file.cond_lettori_file, &file.lock_file) != 0  ){
            perror("Pthread_cond_wait");
            return -1;
        }

    file.lettori_file_attesa--;
    file.lettori_file_attivi++;
    if ( pthread_mutex_unlock(&file.lock_file) != 0 ){
        perror("Pthread_mutex_unlock");
        return -1;
    }

    return 0;
}

int endReadFile(file_t file){

    if ( pthread_mutex_lock(&file.lock_file) != 0 ){
        perror("Pthread_mutex_lock");
        return -1;
    }

    file.lettori_file_attivi--;
    if ( file.lettori_file_attivi == 0 && file.scrittori_file_attesa > 0 )
        if ( pthread_cond_signal(&file.cond_scrittura_file) != 0 ){
            perror("Pthread_cond_signal");
            return -1;
        }

    if ( pthread_mutex_unlock(&file.lock_file) != 0 ){
        perror("Pthread_mutex_unlock");
        return -1;
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
