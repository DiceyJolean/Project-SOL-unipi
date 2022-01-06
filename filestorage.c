
// Erica Pistolesi 518169

#if !defined(_XOPEN_SOURCE)
#define _XOPEN_SOURCE  600
#endif

#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <signal.h>
#include <assert.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/stat.h>

#include "serverutility.h"
#include "storage.h"
#include "util.h"
#include "intqueue.h"
#include "api.h"

#ifndef DEBUG
#define DEBUG 0
#endif

#ifndef BUFFERSIZE
#define BUFFERSIZE 1024
#endif

// define per evitare refusi sul file di log

#define LOG_OPEN_FILE_CREAT "OPEN_FILE(OCREAT)\t"
#define LOG_OPEN_FILE_LOCK "OPEN_FILE(OLOCK)\t"
#define LOG_OPEN_FILE_CREAT_LOCK "OPEN_FILE(OLOCK|OCREAT)"
#define LOG_OPEN_FILE "OPEN_FILE()\t\t"
#define LOG_WRITE_FILE "WRITE_FILE()\t\t"
#define LOG_APPEND_FILE "APPEND_FILE()\t\t"
#define LOG_READ_FILE "READ_FILE()\t\t"
#define LOG_READ_N_FILES "READ_N_FILES()\t"
#define LOG_LOCK_FILE "LOCK_FILE()\t\t"
#define LOG_UNLOCK_FILE "UNLOCK_FILE()\t\t"
#define LOG_CLOSE_FILE "CLOSE_FILE()\t\t"
#define LOG_REMOVE_FILE "REMOVE_FILE()\t\t"
#define LOG_CLOSE_CONNECTION "CLOSE_CONNECTION\t"
#define LOG_OPEN_CONNECTION "OPEN_CONNECTION\t"
#define LOG_CACHE_MISS "CACHE_MISS\t\t"
#define LOG_EVICTED "EVICTED\t\t"

char buffer[BUFFERSIZE];   // Buffer per la scrittura sul file di log
int fdpipe[2];  // Pipe condivisa con i worker per il reinserimento dei fd dei client serviti
pthread_mutex_t lock_clientconnessi = PTHREAD_MUTEX_INITIALIZER; // Lock per accedere in mutua esclusione alla variabile clientconnessi
int clientconnessi = 0; // Contatore dei client attualmente connessi
int max_clienti = 0; // Numero massimo di client connessi contemporaneamente
intqueue_t* fdqueue = NULL; // Coda condivisa tra il server e i worker dove vengono caricati i file descriptor con richieste pronte
storage_t* cache = NULL; // Puntatore allo storage
pthread_mutex_t lock_log = PTHREAD_MUTEX_INITIALIZER; // Lock per accedere in mutua esclusione al file di log
FILE* logfile = NULL; // File di log su cui scrivo le operazioni eseguite

// Parametri passati ai singoli thread per aggiornare il numero delle operazioni eseguite
typedef struct workerArgs{
    int id;
    int opDone;
} workerArgs_t;

// Aggiorna il file di log in mutua esclusione
int write_log(const char* operator, char* operation, int fd, char* pathname, size_t bytes, int result){
    /*
    Durante l’esecuzione, il processo server storage effettua il logging, su un file di log specificato nel file di
    configurazione, di tutte le operazioni che vengono richieste dai client o di gestione interna del server (ad esempio,
    l’arrivo di una nuova connessione, il nome del file letto e l’ammontare dei byte restituiti al client, la quantità di dati
    scritti, se è stata richiesta una operazione di lock su un file, se è partito l’algoritmo di rimpiazzamento dei file della
    cache e quale vittima è stata selezionata, etc.). La scelta del formato del file di log è lasciata allo studente.

    Al termine dell’esecuzione, il server stampa sullo standard output in modo formattato un sunto delle operazioni
    effettuate durante l’esecuzione, e tra queste almeno le seguenti informazioni:
    1. numero di file massimo memorizzato nel server;
    2. dimensione massima in Mbytes raggiunta dal file storage;
    3. numero di volte in cui l’algoritmo di rimpiazzamento della cache è stato eseguito per selezionare uno o più file “vittima”;
    4. lista dei file contenuti nello storage al momento della chiusura del server.
    */
    memset(buffer, 0, BUFFERSIZE);

    if ( pthread_mutex_lock(&lock_log) == -1 ){
        perror("pthread_mutex_lock");
        return -1;
    }

    int toFree = 0;

    if ( !pathname ){
        pathname = realloc(pathname, BUFFERSIZE);
        memset(pathname, 0, BUFFERSIZE);
        snprintf(pathname, BUFFERSIZE, "\t\t(null)\t\t\t\t\t\t\t\t\t");
        toFree = 1;
    }

    snprintf(buffer, BUFFERSIZE,
    " %s\t| %s | %d\t| %s\t| %ld\t| %s\n",
    operator, operation, fd, pathname, bytes, strerror(result));

    fprintf(logfile, "%s", buffer);
    if ( DEBUG ) fprintf(stdout, "%s", buffer);

    if ( pthread_mutex_unlock(&lock_log) == -1 ){
        perror("pthread_mutex_unlock");
        return -1;
    }

    if ( toFree )
        free(pathname);

    return 0;
}

/*
    Per ogni file lockato o aperto dal client indicato dal file descriptor fd,
    sblocca i file ed elimina il fd dall'insieme dei client che avevano aperto
    quel file. In caso ci fossero client in attesa della lock su uno di quei file,
    ottengono la lock. Infine chiude il file descriptor
*/
void close_fd_routine(int fd);

void close_fd_routine(int fd){    
    intqueue_t* lockWaiters = createQ(); // Coda dei fd dei client in attesa della lock su un file bloccato da un client che ha chiuso la connessione
    if ( closeFd(cache, fd, lockWaiters) != 0 ){
        deleteQ(lockWaiters);
        close(fd);
    }

    // Finché ci sono client in attesa della lock
    while ( !isEmptySinchronized(lockWaiters) ){
        int toWakeUp = dequeue(lockWaiters);
        if ( toWakeUp < 0 ){
            deleteQ(lockWaiters);
            close(fd);
        }

        // Risveglio un client in attesa della lock
        int ok = 0;
        if ( write(toWakeUp, &ok, sizeof(int)) == -1 ){
            // Errore con il client che volevo risvegliare, lo elimino a sua volta
            close_fd_routine(toWakeUp);
        }
    }

    deleteQ(lockWaiters);
    
    // Aggiorno il contatore dei client connessi
    if ( pthread_mutex_lock(&lock_clientconnessi) == -1 ){
        perror("pthread_mutex_lock");
        exit(-1);
    }
    clientconnessi--;
    assert(clientconnessi>=0);
    if ( pthread_mutex_unlock(&lock_clientconnessi) == -1 ){
        perror("pthread_mutex_unlock");
        exit(-1);
    }

    close(fd);
}

// Restituisce un buffer allocato contenente il pathname inviato dal client con file descriptor fd
char* read_pathname(int fd){
    int n = 0;
    if ( read(fd, &n, sizeof(int)) == -1 )
        return NULL;

    char* pathname = malloc(n);
    if ( !pathname )
        return NULL;

    memset(pathname, 0, n);

    if ( readn(fd, pathname, n) == -1 )
        return NULL;

    return pathname;
}

void* Worker(void* args){
    int tid = ((workerArgs_t*)args)->id; // Thread id del worker
    
    if ( DEBUG) printf("WORKER %d: Sono attivo\n", tid);
    
    int fd = -1; // File descriptor del client pronto
    request_t* req = malloc(sizeof(request_t));
    int err = -1;
    int okPush = 0; // Flag che indica se il fd può essere reinserito in fdqueue
    
    while ( (fd = dequeue(fdqueue)) > 2 ){ // 0 stdin, 1 stdout, 2 stderr
        // if ( DEBUG ) printIntQueue(fdqueue);
        okPush = 1; // Di default reinserisco sempre il fd in fdqueue
        memset(req, 0, sizeof(request_t));
        if ( DEBUG ) printf("WORKER %d: Estraggo dalla coda dei fd il numero %d\n", tid, fd);

        int r, w;
        if ( ( r = readn(fd, req, sizeof(request_t)) ) == -1){
            printf("AH\n");
            close_fd_routine(fd);
            continue;
        }
    
        switch(req->op){
            case( OPEN_FILE ):{
                char* pathname = read_pathname(fd);
                int enqueued = 0; // Flag che indica se il client verrà messo nella coda di attesa della lock sul file

                if ( req->flags_N & OCREAT ){
                    // Creo il file                    
                    if ( DEBUG ) printf("WORKER %d: Il client %d ha richiesto una openFile con OCREAT\n", tid, fd);
                    errno = 0;
                    err = S_createFile(cache, pathname, fd) ? errno : 0;
                    // Se la creazione del file non va a buon fine rispondo con l'esito al client ed effettuo il logging
                    if ( err ){
                        if ( DEBUG) printf("WORKER %d: S_createFile fallita con errore - %s\n", tid, strerror(err));
                        char* op = ( req->flags_N & OLOCK ) ? LOG_OPEN_FILE_CREAT_LOCK : LOG_OPEN_FILE_CREAT;
                        char name[10];
                        snprintf(name, 10, "WORKER %d", tid);
                        write_log(name, op, fd, pathname, 0, err);
                        // Comunico l'esito negativo dell'operazione al client
                        if ( write(fd, &err, sizeof(int)) == -1 ){
                            close_fd_routine(fd);
                            okPush = 0;
                        }
                        free(pathname);
                        break;
                    }
                    else if ( DEBUG ) printf("WORKER %d: S_createFile eseguita con successo\n", tid);
                }

                // Apro il file
                if ( DEBUG ) printf("WORKER %d: Il client %d ha richiesto una openFile\n", tid, fd);
                errno = 0;
                err = S_openFile(cache, pathname, fd) ? errno : 0;
                if ( err ){
                    if ( DEBUG) printf("WORKER %d: S_openFile fallita con errore - %s\n", tid, strerror(err));
                    char* op = ( req->flags_N & OCREAT ) ? ( req->flags_N & OLOCK ) ? LOG_OPEN_FILE_CREAT_LOCK : LOG_OPEN_FILE_CREAT : ( req->flags_N & OLOCK ) ? LOG_OPEN_FILE_LOCK : LOG_OPEN_FILE;
                    char name[10];
                    snprintf(name, 10, "WORKER %d", tid);
                    write_log(name, op, fd, pathname, 0, err);
                    // Comunico l'esito negativo dell'operazione al client
                    if ( write(fd, &err, sizeof(int)) == -1 ){
                        close_fd_routine(fd);
                        okPush = 0;
                    }
                    free(pathname);
                    break;
                }
                else if ( DEBUG ) printf("WORKER %d: S_openFile eseguita con successo\n", tid);

                if ( req->flags_N & OLOCK ){
                    // Blocco il file dopo averlo aperto
                    if ( DEBUG ) printf("WORKER %d: Il client %d ha richiesto una opneFile con OLOCK\n", tid, fd);
                    errno = 0;
                    err = S_lockFile(cache, pathname, fd, &enqueued) ? errno : 0;
                    if ( err ){
                        if ( DEBUG) printf("WORKER %d: S_lockFile fallita con errore - %s\n", tid, strerror(err));
                        char* op = ( req->flags_N & OCREAT ) ? LOG_OPEN_FILE_CREAT_LOCK : LOG_OPEN_FILE_LOCK;
                        char name[10];
                        snprintf(name, 10, "WORKER %d", tid);
                        write_log(name, op, fd, pathname, 0, err);
                        // Comunico l'esito negativo dell'operazione al client
                        if ( write(fd, &err, sizeof(int)) == -1 ){
                            close_fd_routine(fd);
                            okPush = 0;
                        }
                        free(pathname);
                        break;
                    }
                    else if ( DEBUG ) printf("WORKER %d: S_lockFile eseguita con successo, non so ancora se il client ha ottenuto la lock\n", tid);

                    if ( !enqueued ){
                        /* 
                            Posso rispondere al client, 
                            altrimenti lo lascio in sospeso sulla read finché:
                            - un altro client non rilascia la lock e può ottenerla
                            - il file non viene rimosso
                        */
                        char* op = ( req->flags_N & OCREAT ) ? LOG_OPEN_FILE_CREAT_LOCK : LOG_OPEN_FILE_LOCK;
                        char name[10];
                        snprintf(name, 10, "WORKER %d", tid);
                        write_log(name, op, fd, pathname, 0, err);
                        // Comunico l'esito positivo al client
                        if ( write(fd, &err, sizeof(int)) == -1 ){
                            close_fd_routine(fd);
                            okPush = 0;
                            free(pathname);
                            break;
                        }
                        if ( DEBUG ) printf("WORKER %d: Il client %d ha ottenuto la lock sul file\n", tid, fd);
                    }
                    // Caso in cui il client è in coda ad attendere la lock
                }
                else{
                    // Caso in cui è stato richiesta una openFile senza OLOCK
                    // Posso rispondere al client
                    char* op = ( req->flags_N & OCREAT ) ? LOG_OPEN_FILE_CREAT : LOG_OPEN_FILE;
                    char name[10];
                    snprintf(name, 10, "WORKER %d", tid);
                    write_log(name, op, fd, pathname, 0, err);
                    // Comunico l'esito positivo dell'operazione al client
                    if ( write(fd, &err, sizeof(int)) == -1 ){
                        close_fd_routine(fd);
                        okPush = 0;
                        free(pathname);
                        break;
                    }
                    if ( DEBUG ) printf("WORKER %d: openFile eseguita con successo con i flag %d\n", tid, req->flags_N);
                    
                }

                free(pathname);
            } break;
            case( CLOSE_FILE ):{
                char* pathname = read_pathname(fd);
                
                if ( DEBUG ) printf("WORKER %d: Il client %d ha richiesto una closeFile\n", tid, fd);

                errno = 0;
                err = ( S_closeFile(cache, pathname) ) ? errno : 0;

                if ( err ){ if ( DEBUG) printf("WORKER %d: S_closeFile fallita con errore - %s\n", tid, strerror(err)); }
                else if ( DEBUG ) printf("WORKER %d: S_closeFile eseguita con successo\n", tid);
                
                if ( DEBUG ) printf("WORKER %d: Sto inviando %d come risultato della closeFile\n", tid, err);

                // Comunico l'esito al client
                if ( write(fd, &err, sizeof(int)) == -1 ){
                    close_fd_routine(fd);
                    okPush = 0;
                    free(pathname);
                    break;
                }

                // Aggiorno il file di log
                char name[10];
                snprintf(name, 10, "WORKER %d", tid);
                write_log(name, LOG_CLOSE_FILE, fd, pathname, 0, err);
                
                free(pathname);
            } break;
            case( WRITE_FILE ):{
                char* pathname = read_pathname(fd);

                size_t size = 0; // Dimesione del contenuto del file in bytes
                
                if ( DEBUG ) printf("WORKER %d: Il client %d ha richiesto una writeFile\n", tid, fd);
                if ( DEBUG ) printf("WORKER %d: Leggo quanti bytes mi sta per inviare il client\n", tid);
                // Il client mi comunica quanti bytes vuole scrivere
                if ( ( r = readn(fd, &size, sizeof(size_t))) == -1 ){
                    printf("AH\n");
                    close_fd_routine(fd);
                    okPush = 0;
                    free(pathname);
                    break;
                }

                void* buf = malloc(size);
                if ( !buf) {
                    perror("Malloc");
                    close_fd_routine(fd);
                    okPush = 0;
                    break;
                }
                
                memset(buf, 0, size);
                if ( DEBUG ) printf("WORKER %d: Sto per leggere %ld bytes dal client\n", tid, size);
                
                // Il client invia il contenuto da scrivere
                if ( ( r = readn(fd, buf, size) ) == -1 ){
                    close_fd_routine(fd);
                    okPush = 0;
                    free(pathname);
                    break;
                }

                Queue_t* victim = initQueue(); // Coda contenente i file espulsi dallo storage tramite l'algoritmo di rimpiazzo
                
                errno = 0;
                err = ( S_uploadFile(cache, pathname, fd, buf, size, victim) ) ? errno : 0;

                if ( err ){ if ( DEBUG) printf("WORKER %d: S_uploadFile fallita con errore - %s\n", tid, strerror(err)); }
                else if ( DEBUG ) printf("WORKER %d: S_uploadFile eseguita con successo\n", tid);

                // Aggiorno il file di log
                char name[10];
                snprintf(name, 10, "WORKER %d", tid);
                write_log(name, LOG_WRITE_FILE, fd, pathname, size, err);
                
                // Rispondo con l'esito
                if ( write(fd, &err, sizeof(int)) == -1 ){
                    close_fd_routine(fd);
                    okPush = 0;
                    // Niente break per entrare nel ciclo dove avviso chi era fermo sulla lock
                }
                // Comunico al client quanti file espulsi sto per inviargli (in else perché la write non ha il break)
                else if ( write(fd, &victim->len, sizeof(int)) == -1 ){
                    close_fd_routine(fd);
                    okPush = 0;
                    // break? No, devo comunque avvisare i client in attesa della lock su quel file
                }

                if ( victim->len > 0 ){
                    if ( DEBUG ) printf("WORKER %d: La writeFile(%s) ha causato un CACHEMISS, invio %ld file esplulsi al client %d\n", tid, pathname, victim->len, fd);
                    // C'è stato un cache miss causato dal file pathname
                    char name[10];
                    snprintf(name, 10, "WORKER %d", tid);
                    write_log(name, LOG_CACHE_MISS, fd, pathname, 0, err);
                }

                
                // Invio al client i len file espulsi dalla cache
                int len = victim->len;
                for ( int i = 0; i < len; i++ ){
                    file_t* vic = pop(victim);
                    size_t n = strlen(vic->pathname)+1;
                    if ( DEBUG ) printf("WORKER %d: Invio il file espulso %s al client %d\n", tid, vic->pathname, fd);
                    
                    // Avviso i client bloccati sulla lock che il file non c'è (più)
                    err = ENOENT;
                    for ( int j = 0; j < vic->maxfd; j++){
                        if ( FD_ISSET(j, &vic->lockWait) ){
                            // Sblocco un client che era in attesa di prendere la lock
                            if ( write(j, &err, sizeof(int)) == -1 )
                                close_fd_routine(j);

                            // Aggiorno il file di log riguardo l'operazione di lock fallita
                            char name[10];
                            snprintf(name, 10, "WORKER %d", tid);
                            write_log(name, LOG_LOCK_FILE, j, vic->pathname, 0, err);
                        }
                            
                        // Per i client che avevano semplicemente aperto il file non faccio niente
                        // quando faranno delle operazioni vedranno rispondersi ENOENT
                    }

                    // Invio la dimensione del nome del file
                    if ( writen(fd, &n, sizeof(size_t)) == -1 ){
                        close_fd_routine(fd);
                        okPush = 0;
                        free(pathname);
                        break;
                    }
                    // Invio il nome del file
                    if ( writen(fd, vic->pathname, n) == -1 ){
                        close_fd_routine(fd);
                        okPush = 0;
                        free(pathname);
                        break;
                    }
                    // Invio la dimensione del contenuto del file
                    if ( writen(fd, &vic->size, sizeof(size_t)) == -1 ){
                        close_fd_routine(fd);
                        okPush = 0;
                        free(pathname);
                        break;
                    }
                    // Invio il contenuto del file
                    if ( writen(fd, vic->content, vic->size) == -1 ){
                        close_fd_routine(fd);
                        okPush = 0;
                        free(pathname);
                        break;
                    }
                    
                    // il file vic è stato espulso e invio vic->size bytes di contenuto al client
                    char name[10];
                    snprintf(name, 10, "WORKER %d", tid);
                    write_log(name, LOG_EVICTED, fd, vic->pathname, vic->size, 0);

                    // Libero il file
                    free(vic->content);
                    free(vic->pathname);
                    free(vic);
                }

                free(pathname);
                free(buf);
                deleteQueue(victim);
                
            } break;
            case ( APPEND_FILE):{
                char* pathname = read_pathname(fd);

                size_t size = 0;

                if ( DEBUG ) printf("WORKER %d: Il client %d ha richiesto una appendFile\n", tid, fd);
                if ( DEBUG ) printf("WORKER %d: Leggo quanti bytes mi sta per inviare il client\n", tid);
                // Il client mi comunica quanti bytes vuole scrivere
                if ( readn(fd, &size, sizeof(size_t)) == -1 ){
                    close_fd_routine(fd);
                    okPush = 0;
                    free(pathname);
                    break;
                }

                void* buf = malloc(size);
                if ( !buf ){
                    perror("Malloc");
                    close_fd_routine(fd);
                    okPush = 0;
                    break;
                }
                memset(buf, 0, size);
                if ( DEBUG ) printf("WORKER %d: Sto per leggere %ld bytes dal client\n", tid, size);
                // Il client invia il contenuto da scrivere
                if ( readn(fd, buf, size) == -1 ){
                    close_fd_routine(fd);
                    okPush = 0;
                    free(pathname);
                    break;
                }

                Queue_t* victim = initQueue(); // Coda contenente i file espulsi dallo storage tramite l'algoritmo di rimpiazzo
                
                errno = 0;
                err = ( S_appendFile(cache, pathname, fd, victim, buf, size) ) ? errno : 0;
                if ( err ){ if ( DEBUG) printf("WORKER %d: S_appendFile fallita con errore - %s\n", tid, strerror(err)); }
                else if ( DEBUG ) printf("WORKER %d: S_appendToFile eseguita con successo\n", tid);
                char name[10];
                        snprintf(name, 10, "WORKER %d", tid);
                        write_log(name, LOG_APPEND_FILE, fd, pathname, size, err);

                // Rispondo con l'esito
                if ( write(fd, &err, sizeof(int)) == -1 ){
                    close_fd_routine(fd);
                    okPush = 0;
                    // Niente break, devo comunque avvisare i client in attesa della lock su quel file
                }
                // Comunico al client quanti file espulsi sto per inviargli
                else if ( write(fd, &victim->len, sizeof(int)) == -1 ){
                    close_fd_routine(fd);
                    okPush = 0;
                    // break; Devo comunque avvisare i client in attesa della lock su quel file
                }
                
                if ( victim->len > 0 ){
                    if ( DEBUG ) printf("WORKER %d: La appendFile(%s) ha causato un CACHEMISS, invio %ld file esplulsi al client %d\n", tid, pathname, victim->len, fd);
                    // C'è stato un cache miss causato dal file pathname
                    char name[10];
                    snprintf(name, 10, "WORKER %d", tid);
                    write_log(name, LOG_CACHE_MISS, fd, pathname, 0, err);
                }


                // Invio al client i len file espulsi dalla cache
                int len = victim->len; // victim->len viene modificata dopo la pop, devo salvare una copia di len
                for ( int i = 0; i < len; i++ ){
                    file_t* vic = pop(victim);
                    size_t n = strlen(vic->pathname)+1;
                    if ( DEBUG ) printf("WORKER %d: Invio il file espulso %s al client %d\n", tid, vic->pathname, fd);

                    // Avviso i client bloccati sulla lock che il file non c'è (più)
                    err = ENOENT;
                    for ( int j = 0; j < vic->maxfd; j++){
                        if ( FD_ISSET(j, &vic->lockWait) ){
                            // Sblocco un client che era in attesa di prendere la lock
                            if ( write(j, &err, sizeof(int)) == -1 )
                                close_fd_routine(j);
                            
                            char name[10];
                            snprintf(name, 10, "WORKER %d", tid);
                            write_log(name, LOG_LOCK_FILE, j, vic->pathname, 0, err);
                        }
                            
                        // Per i client che avevano semplicemente aperto il file non faccio niente
                        // quando faranno delle operazioni vedranno rispondersi ENOENT
                    }

                    // Invio la dimensione del nome del file
                    if ( writen(fd, &n, sizeof(size_t)) == -1 ){
                        close_fd_routine(fd);
                        okPush = 0;
                        break;
                    }
                    // Invio il nome del file
                    if ( writen(fd, vic->pathname, n) == -1 ){
                        close_fd_routine(fd);
                        okPush = 0;
                        break;
                    }
                    // Invio la dimensione del contenuto del file
                    if ( writen(fd, &vic->size, sizeof(size_t)) == -1 ){
                        close_fd_routine(fd);
                        okPush = 0;
                        break;
                    }
                    // Invio il contenuto del file
                    if ( writen(fd, vic->content, vic->size) == -1 ){
                        close_fd_routine(fd);
                        okPush = 0;
                        break;
                    }
                    
                    // il file vic è stato espulso e invio vic->size bytes di contenuto al client
                    char name[10];
                    snprintf(name, 10, "WORKER %d", tid);
                    write_log(name, LOG_EVICTED, fd, vic->pathname, vic->size, 0);

                    // Libero il file
                    free(vic->content);
                    free(vic->pathname);
                    free(vic);
                }

                free(pathname);
                free(buf);
                deleteQueue(victim);
                
            } break;
            case ( READ_FILE ):{
                char* pathname = read_pathname(fd);

                void* buf = NULL; // Buffer dove caricare il contenuto del file da leggere
                size_t size; // Dimensione del buffer

                if ( DEBUG ) printf("WORKER %d: Il client %d ha richiesto una readFile\n", tid, fd);
                errno = 0;
                err = S_readFile(cache, pathname, fd, &buf, &size) ? errno : 0;
                if ( err ){
                    // Esito negativo
                    if ( DEBUG) printf("WORKER %d: S_readFile fallita con errore - %s\n", tid, strerror(err));
                    char name[10];
                    snprintf(name, 10, "WORKER %d", tid);
                    write_log(name, LOG_READ_FILE, fd, pathname, 0, err);
                    if ( write(fd, &err, sizeof(int)) == -1 ){
                        close_fd_routine(fd);
                        okPush = 0;
                        free(pathname);
                        break;
                    }
                    free(pathname);
                    break;
                }
                else if ( DEBUG ) printf("WORKER %d: S_readFile eseguita con successo\n", tid);

                // Rispondo che l'operazione è andata a buon fine e invio il contenuto
                if ( write(fd, &err, sizeof(int)) == -1 ){
                    close_fd_routine(fd);
                    okPush = 0;
                    free(pathname);
                    break;
                }

                // Invio la size
                if ( DEBUG ) printf("WORKER %d: Invio la size %ld al client \n", tid, size);
                if ( writen(fd, &size, sizeof(size_t)) == -1 ){
                    close_fd_routine(fd);
                    okPush = 0;
                    free(pathname);
                    break;
                }

                // Invio il contenuto
                // if ( DEBUG ) printf("\nWORKER: Sto inviando il contenuto %s al CLIENT\n", (char*)buf);
                if ( writen(fd, buf, size) == -1 ){
                    close_fd_routine(fd);
                    okPush = 0;
                    free(pathname);
                    break;
                }
                
                char name[10];
                snprintf(name, 10, "WORKER %d", tid);
                write_log(name, LOG_READ_FILE, fd, pathname, size, 0);
                free(pathname);
                free(buf);
                
            } break;
            case ( READ_N_FILES):{
                Queue_t* fileRead = initQueue(); // Coda dei file letti da inviare al client
                
                errno = 0;
                err = S_readNFiles(cache, fd, req->flags_N, fileRead) ? errno : 0;
                if ( err ){
                    if ( DEBUG) printf("WORKER %d: S_readNFiles fallita con errore - %s\n", tid, strerror(err));
                    // Se la S_readNFiles fallisce è un problema lato server
                    deleteQueue(fileRead);
                    break;
                }
                
                // Rispondo con esito positivo
                err = 0;
                if ( write(fd, &err, sizeof(int)) == -1 ){
                    close_fd_routine(fd);
                    okPush = 0;
                    deleteQueue(fileRead);
                    break;
                }
                
                // Comunico al client quanti file sto per spedire
                if ( DEBUG ) printf("WORKER %d: Sto per inviare %ld file al client\n", tid, fileRead->len);
                if ( write(fd, &fileRead->len, sizeof(int)) == -1 ){
                    close_fd_routine(fd);
                    okPush = 0;
                    deleteQueue(fileRead);
                    break;
                }

                int len = fileRead->len;
                for ( int i = 0; i < len; i++ ){
                    file_t* tmp = pop(fileRead);
                    if ( DEBUG ) printf("WORKER %d: Spedisco al CLIENT che ha richiesta una ReadNFiles il file %s\n", tid, tmp->pathname);

                    size_t n = strlen(tmp->pathname)+1;
                    // Invio la dimensione del nome del file
                    if ( writen(fd, &n, sizeof(size_t)) == -1 ){
                        close_fd_routine(fd);
                        okPush = 0;
                        free(tmp->pathname);
                        free(tmp->content);
                        free(tmp);
                        deleteQueue(fileRead);
                        break;
                    }
                    // Invio il nome del file
                    if ( writen(fd, tmp->pathname, n) == -1 ){
                        close_fd_routine(fd);
                        okPush = 0;
                        free(tmp->pathname);
                        free(tmp->content);
                        free(tmp);
                        deleteQueue(fileRead);
                        break;
                    }
                    // Invio la dimensione del contenuto del file
                    if ( writen(fd, &tmp->size, sizeof(size_t)) == -1 ){
                        close_fd_routine(fd);
                        okPush = 0;
                        free(tmp->pathname);
                        free(tmp->content);
                        free(tmp);
                        deleteQueue(fileRead);
                        break;
                    }
                    // Invio il contenuto del file
                    if ( writen(fd, tmp->content, tmp->size) == -1 ){
                        close_fd_routine(fd);
                        okPush = 0;
                        free(tmp->pathname);
                        free(tmp->content);
                        free(tmp);
                        deleteQueue(fileRead);
                        break;
                    }
                    
                    char name[10];
                    snprintf(name, 10, "WORKER %d", tid);
                    write_log(name, LOG_READ_N_FILES, fd, tmp->pathname, tmp->size, 0);
                    free(tmp->pathname);
                    free(tmp->content);
                    free(tmp);
                }

                deleteQueue(fileRead);
                
            } break;
            case ( LOCK_FILE ):{
                char* pathname = read_pathname(fd);
                int enqueued = 0; // Flag che indica se il client è in attesa della lock o no
                
                if ( DEBUG ) printf("WORKER %d: È stata richiesta una lockFile(%s)\n", tid, pathname);
                errno = 0;
                err = ( S_lockFile(cache, pathname, fd, &enqueued) ) ? errno : 0;
                if ( err ){
                    // Esito negativo
                    if ( DEBUG) printf("WORKER %d: S_lockFile fallita con errore - %s\n", tid, strerror(err));
                    char name[10];
                    snprintf(name, 10, "WORKER %d", tid);
                    write_log(name, LOG_LOCK_FILE, fd, pathname, 0, err);
                    if ( write(fd, &err, sizeof(int)) == -1 ){
                        close_fd_routine(fd);
                        okPush = 0;
                        free(pathname);
                        break;
                    }
                    free(pathname);
                    break;
                }

                err = 0;
                if ( !enqueued ){
                    // enqueued è a 0, significa che il client ha ottenuto la lock e rispondo con esito positivo
                    if ( write(fd, &err, sizeof(int)) == -1 ){
                        close_fd_routine(fd);
                        okPush = 0;
                        free(pathname);
                        break;
                    }
                    char name[10];
                    snprintf(name, 10, "WORKER %d", tid);
                    write_log(name, LOG_LOCK_FILE, fd, pathname, 0, 0);
                }

                // Altrimenti non rispondo e lascio il client in sospeso sulla read
                // Verrà risvegliato da una unlock o se il file verrà eliminato

                free(pathname);
            } break;
            case ( UNLOCK_FILE ):{
                char* pathname = read_pathname(fd);
                int newLocker = 0; // fd del client a cui assegnare la lock del file dopo che è stato sbloccato
                
                if ( DEBUG ) printf("WORKER %d: È stata richiesta una unlockFile(%s)\n", tid, pathname);
                errno = 0;
                err = ( S_unlockFile(cache, pathname, fd, &newLocker) ) ? errno : 0;
                
                if ( err ){ if ( DEBUG) printf("WORKER %d: S_unlockFile fallita con errore - %s\n", tid, strerror(err)); }
                else if ( DEBUG ) printf("WORKER %d: S_unlockFile(%s) eseguita con successo\n", tid, pathname);

                // Rispondo al client che ha richiesto la unlock
                if ( write(fd, &err, sizeof(int)) == -1 ){
                    close_fd_routine(fd);
                    okPush = 0;
                    free(pathname);
                    break;
                }
                char name[10];
                snprintf(name, 10, "WORKER %d", tid);
                write_log(name, LOG_UNLOCK_FILE, fd, pathname, 0, err);
                
                if ( newLocker )
                    // Sblocco un client che era in attesa di prendere la lock
                    if ( write(newLocker, &err, sizeof(int)) == -1 )
                        close_fd_routine(newLocker);

                free(pathname);
            } break;
            case ( REMOVE_FILE ):{
                char* pathname = read_pathname(fd);
                // Coda dei client che erano in attesa della lock sul file che verrà eliminato
                intqueue_t* lockWaiters = createQ(); // Coda dei fd dei client in attesa della lock sul file

                errno = 0;
                err = ( S_removeFile(cache, pathname, fd, lockWaiters) ) ? errno : 0;

                if ( err ){ if ( DEBUG) printf("WORKER %d: S_removeFile fallita con errore - %s\n", tid, strerror(err)); }
                else if ( DEBUG ) printf("WORKER %d: S_removeFile(%s) eseguita con successo\n", tid, pathname);

                // Comunico l'esito al client che ha richiesto la remove
                if ( write(fd, &err, sizeof(int)) == -1 ){
                    close_fd_routine(fd);
                    okPush = 0;
                    // Non faccio la break perché devo sbloccare i client in attesa della lock
                }

                if ( DEBUG ) printf("WORKER %d: Sblocco i client in atttesa della lock sul file %s che è appena stato rimosso\n", tid, pathname);
                err = ENOENT; // Il file è stato rimosso

                // Sblocco tutti i client fermi sulla read in attesa della lock
                while ( !isEmptySinchronized(lockWaiters) ){
                    int toWakeUp = dequeue(lockWaiters); // Fd di un client in attesa della lock, lo risveglio dalla read
                    if ( DEBUG ) printf("WORKER %d: Sblocco il client con fd %d\n", tid, toWakeUp);
                    if ( write(toWakeUp, &err, sizeof(int)) == -1 )
                        close_fd_routine(toWakeUp);

                    char name[10];
                    snprintf(name, 10, "WORKER %d", tid);
                    write_log(name, LOG_LOCK_FILE, toWakeUp, pathname, 0, err);
                }

                deleteQ(lockWaiters);
                free(pathname);
            } break;
            case ( CLOSE_CONNECTION ):{
                /*
                    Il client identificato con fd ha chiuso la connessione,
                    devo toglierlo da tutti i file che aveva aperto o bloccato
                */
                close_fd_routine(fd);

                okPush = 0; // Non reinserisco il fd nella coda dei client da ascoltare perché ha chiuso la connessione

                if ( pthread_mutex_lock(&lock_clientconnessi) == -1 ){
                    perror("pthread_mutex_lock");
                    exit(-1);
                }

                if ( clientconnessi == 0 && softquit )
                    okPush = 1; // Caso in cui devo risvegliare il server bloccato sulla select

                assert(clientconnessi>=0);
                if ( pthread_mutex_unlock(&lock_clientconnessi) == -1 ){
                    perror("pthread_mutex_unlock");
                    exit(-1);
                }

                char name[10];
                snprintf(name, 10, "WORKER %d", tid);
                write_log(name, LOG_CLOSE_CONNECTION, fd, NULL, 0, 0);

                if ( okPush )
                    fd = -1; // Valore fittizio, il ciclo for che cerca i fd non arriva a -1
                
                if ( DEBUG ) printf("WORKER %d: closeConnection eseguita con successo se non viene reinserito il fd nel readset\n", tid);

            } break;
            default:{
                // Ho ricevuto una richiesta non formattata correttamente, chiudo il fd
                close_fd_routine(fd);
                okPush = 0;
            } break;
        }

        if ( okPush ){
            ((workerArgs_t*)args)->opDone++;
            // Scrivo il fd del client appena servito nella pipe, così che il server possa reinserirlo tra gli fd ascoltati sulla select
            if ( write(fdpipe[1], &fd, sizeof(int)) == -1 ) {
                perror("Write");
                fprintf(stderr, "WORKER %d: Errore scrivendo sulla pipe\n", tid);
                continue;
            }
            // if (DEBUG) printf("WORKER %d: Rimando al server il fd %d\n", fd);
        }
        
    }
    
    if ( DEBUG ) printf("WORKER %d: Ricevo un segnale di terminazione dal master\n", tid);
    free(req);

    return NULL;
}

int main(int argc, char* argv[]) {

    // Setto i parametri dal file di configurazione
    if ( set_config(argc, argv) != 0 )
        return -1;

    // Installo il signal handler per i segnali da gestire
    if ( installSigHand() != 0 ) {
        fprintf(stderr, "SERVER: Errore nell'installazione del signal handler\n");
        free(filestat);
        free(filesocketname);
        return -1;
    }
    signal(SIGPIPE, SIG_IGN); // Ignoro sigpipe

    // Apro il file di log in modalità scrittura
    mode_t oldmask = umask(033);
    logfile = fopen(filestat, "w+");
    umask(oldmask);
    if ( !logfile ){
        fprintf(stderr, "SERVER: Errore nell'apertura del file di logging\n");
        free(filestat);
        free(filesocketname);
        return -1;
    }
    fprintf(logfile, " OPERATOR\t|\tOPERATION\t| CLIENT |\t\tFILENAME\t\t\t\t\t\t\t\t\t| BYTES | RESULT |\n"
    "-----------------------------------------------------------------------------------------------------------------------------------------------------------------\n");

    // Creo la coda dei fd pronti condivisa con i worker
    fdqueue = createQ();
    if ( !fdqueue ) {
        fprintf(stderr, "SERVER: Errore nella creazione delle coda dei file descriptor\n");
        fclose(logfile);
        free(filestat);
        free(filesocketname);
        return -1;
    }

    // Creo lo storage
    cache = S_createStorage(max_file, max_storage);
    if ( !cache ){
        fprintf(stderr, "SERVER: Errore nella creazione della cache\n");
        fclose(logfile);
        deleteQ(fdqueue);
        free(filestat);
        free(filesocketname);
        return -1;
    }

    // Creo e avvio i worker
    pthread_t worker[max_worker];
    workerArgs_t argWorker[max_worker];
    for ( int i = 0; i < max_worker; i++ ){
        argWorker[i].id = i+1;
        argWorker[i].opDone = 0;
        if ( pthread_create(&worker[i], NULL, Worker, &argWorker[i]) != 0 ){
            perror("Pthread_create");

            fclose(logfile);
            deleteQ(fdqueue);
            deleteStorage(cache);
            free(filestat);
            free(filesocketname);

            return -1;
        }
    }
    
    // Apro la connessione socket
    int listenfd = openServerConnection(filesocketname); // Socket su cui fare la accept
    if ( listenfd == -1 ){
        fprintf(stderr, "SERVER: Errore nella creazione della connessione socket\n");
        
        fclose(logfile);
        deleteQ(fdqueue);
        deleteStorage(cache);
        free(filestat);
        free(filesocketname);
        
        return -1;
    }

    // Preparo i parametri della select
    int fdmax, tmpfdmax;
    fd_set readset, tmpset;
    // Azzero sia il readset che il set temporaneo usato per la select
    FD_ZERO(&readset);
    FD_ZERO(&tmpset);
    // Aggiungo il listenfd fd al readset
    FD_SET(listenfd, &readset);

    // Creo le pipe di terminazione e di ritorno degli fd
    if ( pipe(fdpipe) == -1 || pipe(sigpipe) == -1 ){
        perror("Pipe");
        return -1;
    }
    // Aggiungo le pipe al readset
    FD_SET(fdpipe[0], &readset);
    FD_SET(sigpipe[0], &readset);

    // Assegno a fdmax il fd maggiore fra quelli attualmente nel readset
    // if ( DEBUG ) printf("SERVER: Scelgo il massimo tra %d, %d e %d\n", sigpipe[0], fdpipe[0], listenfd);
    fdmax = ( sigpipe[0] > fdpipe[0] ) ? sigpipe[0] : fdpipe[0];
    fdmax = ( fdmax > listenfd ) ? fdmax : listenfd;
    // if ( DEBUG ) printf("SERVER: fdmax vale %d\n", fdmax);
















    // Inizio la routine di servire le richieste client finché non ricevo un segnale di chiusura

    // Il server accetta nuove connessioni e smista le richeste dei client fino all'arrivo di un segnale di chiusura
    if ( pthread_mutex_lock(&lock_clientconnessi) != 0 ){
        perror("Pthread_mutex_lock");

        fclose(logfile);
        deleteStorage(cache);
        deleteQ(fdqueue);
        free(filestat);
        free(filesocketname);

        return -1;
    }
    while ( !ragequit && ( !softquit || clientconnessi ) ){
        if ( DEBUG ) printf("SERVER: Il flag softquit è a %d e ci sono %d client connessi\n", softquit, clientconnessi); 
        if ( pthread_mutex_unlock(&lock_clientconnessi) != 0 ){
            perror("Pthread_mutex_unlock");
            return -1;
        }
        tmpset = readset;

        // if ( DEBUG ) printf("SERVER: fdmax vale %d\n", fdmax);
        if ( select(fdmax+1, &tmpset, NULL, NULL, NULL) == -1 ){
            if ( errno != EINTR ){
                perror("Select");
                break;
            }
            else
                // Sono stato interrotto da un segnale
                // Riprendo e attendo di essere avvisato dalla sigpipe per applicare la giusta politica di uscita
                if ( DEBUG ) printf("SERVER: Sono stato interrotto da un segnale, ci sono ancora %d client connessi\n", clientconnessi);
        }
            
        if ( ragequit )
            goto quit;

        tmpfdmax = fdmax;
        for ( int i = tmpfdmax; i > 0; i-- )
            if ( FD_ISSET(i, &tmpset) ){
                // if ( DEBUG ) printf("SERVER: Mi domando quale sia l'ultimo indice che visito (%d)\n", i);
                if ( i == sigpipe[0] ){
                    if ( DEBUG ) printf("SERVER: In chiusura...\n");
                    // È arrivato un messaggio sulla pipe per la terminzaione
                    int sig;
                    if ( read(sigpipe[0], &sig, sizeof(int)) == -1 ){
                        perror("Read");
                        return -1;
                    }
                    // Chiuso il listenfd e continuo il ciclo della select
                    if ( ragequit )
                        goto quit;
                    
                    if ( softquit ){
                        if (DEBUG ) printf("SERVER: Il segnale ricevuto era SIGHUP ci sono %d clienti connessi\n", clientconnessi);
                        // Chiudo il listenfd e continuo a servire richieste finché ci sono ancora client connessi
                        FD_CLR(listenfd, &readset);

                        // Aggiorno fdmax se ce n'è bisogno
                        if ( i == fdmax )
                            for ( int i = (fdmax-1); i >= 0; --i )
                                if ( FD_ISSET(i, &readset) )
                                    fdmax = i;

                        close(listenfd);
                        if ( pthread_mutex_lock(&lock_clientconnessi) != 0 ){
                            perror("Pthread_mutex_lock");
                            return -1;
                        }
                        break; // Evito la accept sul listenfd e riparto dalla select
                    }
                }
                else if ( i == listenfd ){
                    // Nuova connessione da parte di un client
                    int connfd; // fd del nuovo client da inserire nel readset
                    if ( DEBUG ) printf("SERVER: In attesa di una nuova connessione\n");
                    if ( ( connfd = accept(listenfd, NULL, NULL ) ) == -1 ){
                        perror("Accept");
                        return -1;
                    }
                    fdmax = (connfd > fdmax ) ? connfd : fdmax;

                    FD_SET(connfd, &readset);
                    if ( pthread_mutex_lock(&lock_clientconnessi) != 0 ){
                        perror("Pthread_mutex_lock");
                        return -1;
                    }
                    clientconnessi++;
                    max_clienti = ( max_clienti > clientconnessi ) ? max_clienti : clientconnessi;
                    if ( pthread_mutex_unlock(&lock_clientconnessi) != 0 ){
                        perror("Pthread_mutex_unlock");
                        return -1;
                    }
                    write_log("SERVER   ", LOG_OPEN_CONNECTION, connfd, NULL, 0, 0);
                }
                else if ( i == fdpipe[0] ){
                    // Reinserisco il fd contenuto in fdpipe nel readset
                    int connfd; // fd del client da reinserire
                    if ( read(fdpipe[0], &connfd, sizeof(int)) == -1 ){
                        perror("Read");
                        return -1;
                    }
                    // if ( DEBUG ) printf("SERVER: Reinserisco tra i fd da ascoltare il %d\n", connfd);
                    FD_SET(connfd, &readset);
                    fdmax = ( connfd > fdmax ) ? connfd : fdmax; // max(fdmax, connfd);
                }
                else{
                    // È una nuova richiesta da un client già connesso
                    // Devo pushare i nella coda dei fd da servire
                    if ( DEBUG ) printf("SERVER: Nuova richiesta dal fd %d\n", i);
                    FD_CLR(i, &readset);
                    /*
                    // Aggiorno fdmax se ce n'è bisogno
                    if ( i == fdmax )
                        for ( int i = (fdmax-1); i >= 0; --i )
                            if ( FD_ISSET(i, &readset) )
                                fdmax = i;
                    */
                    if ( enqueue(fdqueue, i) != 0 ){
                        if ( DEBUG ) printf("SERVER: Errore nell'inserimento di un nuovo fd in coda\n");
                        if ( pthread_mutex_lock(&lock_clientconnessi) != 0 ){
                            perror("Pthread_mutex_lock");
                            return -1;
                        }
                        clientconnessi--;
                        if ( pthread_mutex_unlock(&lock_clientconnessi) != 0 ){
                            perror("Pthread_mutex_unlock");
                            return -1;
                        }
                        close(i);
                        continue;
                    }
                }
            }
    }
    
    // Ho la lock su clientconnessi
    // Se tutti i clienti hanno chiuso la connessione posso procedere alla terminazione del server
    if ( softquit && clientconnessi == 0 ){
        if ( pthread_mutex_unlock(&lock_clientconnessi) != 0 ){
            perror("Pthread_mutex_unlock");

            fclose(logfile);
            deleteStorage(cache);
            deleteQ(fdqueue);
            free(filestat);
            free(filesocketname);

            return -1;
        }

        goto quit;
    }
    else if ( ragequit )
        goto quit;

    quit: {
        if ( DEBUG ) printf("SERVER: Sono alla goto di uscita\n");
        // Chiudo tutti gli fd, invio un segnale di chiusura ai worker e attendo la loro terminazione

        // Invio un segnale di terminazione ai worker
        for ( int i = 0; i < max_worker; i++ )
            enqueue(fdqueue, -1);
        
        if ( DEBUG ) printf("SERVER: Ho inviato un segnale di terminazione ai worker\n");
        
        for ( int i = 0; i < max_worker; i++ ){
            // Aggiorno le informazioni dei worker sul file di log
            if ( softquit )
                pthread_join(worker[i], NULL);
            else
                pthread_detach(worker[i]);

            if ( pthread_mutex_lock(&lock_log) == -1 ){
                perror("pthread_mutex_lock");

                fclose(logfile);
                deleteStorage(cache);
                deleteQ(fdqueue);
                free(filestat);
                free(filesocketname);

                return -1;
            }

            char buffer[BUFFERSIZE];
            memset(buffer, 0, BUFFERSIZE);
            snprintf(buffer, BUFFERSIZE, "WORKER %d: Operazioni eseguite %d\n", argWorker[i].id, argWorker[i].opDone);
            fprintf(logfile, "%s", buffer);
            if ( DEBUG ) fprintf(stdout, "%s", buffer);

            if ( pthread_mutex_unlock(&lock_log) == -1 ){
                perror("Pthread_mutex_unlock");

                fclose(logfile);
                deleteStorage(cache);
                deleteQ(fdqueue);
                free(filestat);
                free(filesocketname);

                return -1;
            }
        }

        if ( DEBUG ) printf("SERVER: Tutti i worker sono terminati correttamente\n");

        // Ignoro i file descriptor di stdout, stdin, stderr e quelli dei worker
        // Chiudo tutti i fd dei client
        for ( int i = 3 + max_worker; i < fdmax; i++ ){
            if ( DEBUG ) printf("SERVER: Tolgo il file descriptor %d dal readset\n", i);
            FD_CLR(i, &readset);
            // Chiudo senza interessarmi della corretta terminazione dei client
            close(i);
            if ( DEBUG ) printf("SERVER: Ho chiuso il file descriptor %d\n", i);
        }

        if ( DEBUG ) printf("SERVER: Ho chiuso tutte le connessioni con i client\n");

        // Aggiungo le ultime informazioni sul file di log
        char buffer[BUFFERSIZE];
        memset(buffer, 0, BUFFERSIZE);
        snprintf(buffer, BUFFERSIZE, "MAX STORAGE FILE: %d\n"
        "MAX STORAGE SIZE: %d\n"
        "FILES RIMANENTI NELLO STORAGE: %d\n"
        "MAX CLIENT CONNESSI CONTEMPORANEAMENTE: %d\n",
        cache->max_used_file, cache->max_used_size, cache->actual_file, max_clienti);
        fprintf(logfile, "%s", buffer);
        if ( DEBUG ) fprintf(stdout, "%s", buffer);

    };

    fclose(logfile);

    deleteStorage(cache);
    deleteQ(fdqueue);
    
    free(filestat);
    free(filesocketname);

    // Terminazione corretta del server
    return 0;
}
